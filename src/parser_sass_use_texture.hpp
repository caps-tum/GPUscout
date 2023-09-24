/**
 * SASS code analysis to detect use of texture memory instead of global memory
 *
 * @author Soumya Sen
 */

#ifndef PARSER_SASS_USE_TEXTURE_HPP
#define PARSER_SASS_USE_TEXTURE_HPP

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <vector>
#include <tuple>
#include <set>
#include <fstream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <memory>

/// @brief A register with data written to it is labelled as USED, while a read-only register is labelled NOT_USED
enum used_flag
{
    NOT_USED,
    USED
};

/// @brief Register information with read from global memory or texture memory
struct register_used
{
    int line_number;
    std::string write_to_register_number;
    std::string load_from_register;
    std::set<unsigned long> load_from_register_unrolls;
    used_flag flag;

    bool is_texture_load;
};

std::string find_register(std::string line)
{
    // /*01a0*/                   LDG.E.SYS R7, [R8] ;      -> extract R7 from here
    std::istringstream ss(line);
    std::string substr, last_string;

    std::getline(ss, substr, ',');
    std::istringstream ss2(substr);
    while (std::getline(ss2, substr, ' '))
    {
        last_string = substr;
    }

    return last_string;
}

std::string find_register_reduction(std::string line)
{
    //         /*0260*/                   RED.E.ADD.F32.FTZ.RN.STRONG.GPU [UR4], R0 ;       -> extract R0
    std::string substr;

    std::istringstream ss(line);
    while (std::getline(ss, substr, ','))
    {
        std::string remove_chars = "; "; // remove spaces and ; from the substr (clean the register strings)
        substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                    { return remove_chars.find(c) != std::string::npos; }),
                     substr.end());
    }

    return substr;
}

bool same_register_read_write(std::string line)
{
    //         /*0408*/                   FADD R8, R8, R12 ;        -> return true since R8 is read and written
    bool read_only = false;
    std::string write_register = find_register(line);

    std::string substr, last_string;
    line.erase(line.begin(), line.begin() + 35); // erase the first 35 character of the name of the kernel
    std::istringstream ss(line);
    std::getline(ss, substr, ',');
    while (std::getline(ss, substr, ','))
    {
        std::istringstream ss1(substr);
        while (std::getline(ss1, last_string, ' '))
        {
            if (last_string == write_register)
            {
                read_only = true;
                return read_only;
            }
        }
    }

    return read_only;
}

std::pair<std::string, unsigned long> read_register_pair(const std::string &line)
{
    std::string substr, last_string, last_string_clean;

    std::istringstream ss(line);
    while (std::getline(ss, substr, ','))
    {
        last_string = substr;
    }

    std::istringstream ss2(last_string);
    std::getline(ss2, substr, ' ');
    std::getline(ss2, substr, ' ');

    std::string remove_chars = "[]-";
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());

    std::string register_base, register_unroll;
    std::istringstream ss3(substr);
    std::getline(ss3, register_base, '+');
    std::getline(ss3, register_unroll, 'x');
    std::getline(ss3, register_unroll, 'x');

    /*
    Need to put in try-catch block since the register unroll might not always be numbers
    For example:    (18a0*) LDG.E.SYS R34, [R2.64+UR4] ;
    */
    try
    {
        std::stoul(register_unroll, nullptr, 16);
    }
    catch (const std::invalid_argument &e)
    {
        register_unroll = "";
        // std::cerr << e.what() << std::endl;
    }

    std::pair<std::string, unsigned long> register_pair = (register_unroll != "") ? std::make_pair(register_base, std::stoul(register_unroll, nullptr, 16)) : std::make_pair(register_base, (ulong)0);
    return register_pair;
}

/// @brief Detects read-only register loads from global memory with spatial locality
/// @param filename Disassembled SASS file
/// @return Map with information regarding read-only load from global memory for every kernel
std::unordered_map<std::string, std::vector<register_used>> use_texture_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    std::vector<register_used> register_vec;
    std::unordered_map<std::string, std::vector<register_used>> counter_map;
    std::string kernel_name;
    int code_line_number;
    register_used register_obj;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                register_vec.clear();

                // https://cplusplus.com/reference/string/string/erase/     - erase part of a string
                line.erase(line.begin(), line.begin() + 16); // erase the first 16 character of the name of the kernel
                line.erase(line.end() - 15, line.end());     // erase the last 15 character of the name of the kernel
                kernel_name = line;
                // std::cout << kernel_name << std::endl;
            }

            if (line.find(" line ") != std::string::npos)
            {
                code_line_number = std::stoi(line.substr(line.find("line ") + 5)); // saving the current line number
            }

            // find load operations that don't already use constant memory
            if ((line.find("LDG.") != std::string::npos) && (line.find("LDG.E.CI") == std::string::npos) && (line.find("LDG.E.CONSTANT.") == std::string::npos))
            {
                std::string current_register = find_register(line);
                std::vector<register_used>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_used &i)
                                                                                   { return current_register == i.write_to_register_number; });
                if (register_match == register_vec.end())
                {
                    register_obj.write_to_register_number = current_register;
                    register_obj.line_number = code_line_number;
                    register_obj.flag = NOT_USED;

                    // For a given register, If unroll distance is 4 for 32 bits, 8 for 64 bits and 16 for 128 bits => spatial locality of the data loaded
                    register_obj.load_from_register = read_register_pair(line).first;
                    register_obj.load_from_register_unrolls.insert(read_register_pair(line).second);

                    register_obj.is_texture_load = false;

                    register_vec.push_back(register_obj);
                }
            }

            if ((line.find("TEX.") != std::string::npos) || (line.find("TLD") != std::string::npos) || (line.find("TXQ") != std::string::npos))
            {
                // Found texture instructions
                register_obj.is_texture_load = true;
                register_vec.push_back(register_obj);
            }

            // For (fused) multiply-add, the register is being written to - hence not read-only
            if (line.find("MAD") != std::string::npos || line.find("FMA") != std::string::npos ||
                line.find("ATOMS") != std::string::npos || line.find("ATOMG") != std::string::npos ||
                line.find("MUFU") != std::string::npos || line.find("RED.") != std::string::npos)
            {
                std::string current_register = (line.find("RED.") != std::string::npos) ? find_register_reduction(line) : find_register(line);
                std::vector<register_used>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_used &i)
                                                                                   { return current_register == i.write_to_register_number; });
                if (register_match != register_vec.end())
                {
                    register_match->flag = USED;
                }
            }

            // For multiply or add, if the register read and written to is the same - read-only (e.g. R8, R8, R12)
            if (line.find("MUL") != std::string::npos || line.find("ADD") != std::string::npos)
            {
                std::string current_register = find_register(line);
                std::vector<register_used>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_used &i)
                                                                                   { return current_register == i.write_to_register_number; });
                if (register_match != register_vec.end())
                {
                    if (!same_register_read_write(line))
                    {
                        register_match->flag = USED;
                    }
                    else
                    {
                        register_match->flag = NOT_USED;
                    }
                }
            }

            counter_map[kernel_name] = register_vec;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // for (const auto& i: counter_map["_Z13touch3DlinearPvS_l"])
    // {
    //     // std::cout << "Register number: " << i.write_to_register_number << " Line number: " << i.line_number << " Flag: " << i.flag << ", register read from: " << i.load_from_register << std::endl;
    //     // Find the difference between the unrolls
    //     // check https://www.geeksforgeeks.org/absolute-difference-of-all-pairwise-consecutive-elements-in-a-set/
    //     std::set<unsigned long>::iterator it1 = i.load_from_register_unrolls.begin();
    //     std::set<unsigned long>::iterator it2 = i.load_from_register_unrolls.begin();
    //     bool spatial_locality_flag = true;
    //     while (1)
    //     {
    //         it2++;  // 2nd iterator at position 1
    //         if (it2 == i.load_from_register_unrolls.end())
    //         {
    //             break;
    //         }
    //         if((abs(*it2 - *it1) != 4) && (abs(*it2 - *it1) != 8) && (abs(*it2 - *it1) != 16))
    //         {
    //             spatial_locality_flag = false;  // if the unrolls are not at a difference of 4 (for LDG) or 8 (for LDG.64) or 16 (for LDG.128), spatial locality not there
    //             break;
    //         }
    //     }
    //     // For flag NOT_USED (=0) and spatial locality (above algo check) use texture memory
    //     if ((spatial_locality_flag) && (i.flag == NOT_USED))  // spatial locality present
    //     {
    //         std::cout << "Use texture memory for register number (write-to): " << i.write_to_register_number << " Line number: " << i.line_number << " Flag: " << i.flag << ", register read from: " << i.load_from_register << std::endl;
    //         for (auto j : i.load_from_register_unrolls)
    //         {
    //             std::cout << "Spatial locality for above (read-from) register with locality distance" << j << std::endl;
    //         }

    //     }

    // }

    return counter_map;
}

#endif // PARSER_SASS_USE_TEXTURE_HPP
