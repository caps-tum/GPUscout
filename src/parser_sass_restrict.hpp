/**
 * SASS code analysis to detect use of restricted pointers
 *
 * @author Soumya Sen
 */

#ifndef PARSER_SASS_RESTRICT_HPP
#define PARSER_SASS_RESTRICT_HPP

#include "parser_ptx_global_atomics.hpp"
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

/// @brief Register information with read from global memory or read-only cache
struct register_used
{
    std::string register_number;
    int line_number;
    std::string pcOffset;
    used_flag flag;
    bool read_only_mem_used;
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

    std::string remove_chars = "; []"; // remove spaces,[] and ; from the substr (clean the register strings)
    last_string.erase(std::remove_if(last_string.begin(), last_string.end(), [&remove_chars](const char &c)
                                     { return remove_chars.find(c) != std::string::npos; }),
                      last_string.end());

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

std::string get_pcoffset_sass(std::string line)
{
    //         /*00a0*/                   ISETP.GE.AND P1, PT, R2, c[0x0][0x168], PT ;         -> extract 00a0
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

/// @brief Detects read-only register loads from global memory
/// @param filename Disassembled SASS file
/// @return Map with information regarding load from global memory including read-only cache for every kernel
std::unordered_map<std::string, std::vector<register_used>> restrict_analysis(const std::string &filename)
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
            if ((line.find("LDG.") != std::string::npos))
            {
                std::string current_register = find_register(line);
                std::vector<register_used>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_used &i)
                                                                                   { return current_register == i.register_number; });
                if (register_match == register_vec.end())
                {
                    register_obj.register_number = current_register;
                    register_obj.line_number = code_line_number;
                    register_obj.flag = NOT_USED;
                    register_obj.read_only_mem_used = false;
                    register_obj.pcOffset = get_pcoffset_sass(line);
                    if ((line.find(".CI") != std::string::npos) || (line.find(".CONSTANT") != std::string::npos))
                    {
                        register_obj.read_only_mem_used = true;
                    }
                    register_vec.push_back(register_obj);
                }
            }

            if (line.find("MAD") != std::string::npos || line.find("ADD") != std::string::npos ||
                line.find("MUL") != std::string::npos || line.find("FMA") != std::string::npos ||
                // line.find("F2F") != std::string::npos || line.find("I2F") != std::string::npos || // conversion operations not considered as changing the data of the register
                line.find("ATOMS") != std::string::npos || line.find("ATOMG") != std::string::npos ||
                line.find("MUFU") != std::string::npos || line.find("RED.") != std::string::npos)
            {
                std::string current_register = (line.find("RED.") != std::string::npos) ? find_register_reduction(line) : find_register(line);
                std::vector<register_used>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_used &i)
                                                                                   { return current_register == i.register_number; });
                if (register_match != register_vec.end())
                {
                    register_match->flag = USED;
                }
            }

            counter_map[kernel_name] = register_vec;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // for (const auto& i: counter_map["_Z9bodyForceP4Bodyfi"])
    // {
    //     // For registers with flag 0 (NOT USED), we can advice the user to use the __restrict__ keyword because
    //     // it asserts to the compiler that the pointers are in fact not aliased and hence would never overwrite those elements
    //     // https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#restrict
    //     std::cout << "Register number: " << i.register_number << " Line number: " << i.line_number << " Flag: " << i.flag << std::endl;
    // }

    return counter_map;
}


#endif // PARSER_SASS_RESTRICT_HPP
