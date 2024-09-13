/**
 * SASS code analysis to detect use of shared memory instead of global memory
 *
 * @author Soumya Sen
 */

#ifndef PARSER_SASS_USE_SHARED_HPP
#define PARSER_SASS_USE_SHARED_HPP

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

/// @brief Target branch information to detect if instruction is in a for-loop
struct branch_counter
{
    int line_number;
    std::string target_branch;
    int target_branch_line_number;
    bool inside_for_loop = false; // if the target branch is inside a for loop
};

std::string find_branch(const std::string &line)
{
    //         /*0100*/              @!P0 BRA `(.L_x_1) ;   -> extract .L_x_1
    std::string substr, last_string;

    std::istringstream ss(line);
    while (std::getline(ss, substr, '`')) // delimiter at ` character
    {
        last_string = substr;
    }
    std::istringstream ss2(last_string);
    std::getline(ss2, substr, ' '); // get the first part of the string before space

    std::string remove_chars = "();"; // remove the brackets
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());

    return substr;
}

/// @brief Stores information of register loading data from global memory
struct register_access
{
    int line_number;
    std::string register_number;
    int register_load_count;
    int register_operation_count;
    std::string target_branch;
    std::string pcOffset;
    std::string LDG_pcOffset;      // pcOffset of the load instruction of the register
    int count_to_shared_mem_store; // number of instructions (or cycles) between LDG and STS
    bool shared_mem_use;           // turn flag ON if the register contents are used in shared memory
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

std::string find_write_register(const std::string line)
{
    //        /*02a0*/                   STS [R25.X4], R4 ;        -> extract R4
    std::string substr, last_string;

    std::istringstream ss(line);
    while (std::getline(ss, substr, ','))
    {
        last_string = substr;
    }

    std::string remove_chars = " ;[]";
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());

    return substr;
}

std::vector<std::string> all_registers_used_in_line(std::string line)
{
    //         /*0540*/                   DFMA R44, R40, R44, R48 ;     -> extract R40, R44, R48 as vectors
    std::vector<std::string> registers_used;
    std::string substr, last_string, last_string_clean;

    std::istringstream ss(line);
    std::getline(ss, substr, ',');
    while (std::getline(ss, substr, ','))
    {
        std::string remove_chars = "; "; // remove spaces and ; from the substr (clean the register strings)
        substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                    { return remove_chars.find(c) != std::string::npos; }),
                     substr.end());
        registers_used.push_back(substr);
    }

    return registers_used;
}

std::string pcoffset_sass(std::string line)
{
    //        /*04c0*/                   LDG.E.128.SYS R24, [R24] ;";         -> extract 04c0
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

/// @brief Finds difference of SASS instructions between two pcOffsets
/// @param pcOffset_start start pcOffset in hex format
/// @param pcOffset_end end pcOffset in hex format
/// @return count of number of instructions between start and end pcOffsets in decimal
int find_difference_cycles(std::string pcOffset_start, std::string pcOffset_end)
{
    // pcOffset difference: 0550 - 04c0     -> compute 144
    // now each cycle difference is 10 (in hex) or 16 (in dec), divide by 16 to get the difference in instructions apart
    int diff = std::stoul(pcOffset_end, nullptr, 16) - std::stoul(pcOffset_start, nullptr, 16);
    return diff / 16;
}

/// @brief SASS analysis if shared memory can be used instead of global loads
/// @param filename Disassembled SASS file
/// @return Tuple of two maps, first map includes register accessing global loads, second includes the target branch information to detect for-loop
std::tuple<std::unordered_map<std::string, std::vector<register_access>>, std::unordered_map<std::string, std::vector<branch_counter>>> use_shared_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    std::vector<register_access> register_vec;
    std::unordered_map<std::string, std::vector<register_access>> counter_map;
    std::string kernel_name;
    int code_line_number;
    register_access register_obj;

    branch_counter branch_obj;
    std::vector<branch_counter> branch_vec;
    std::unordered_map<std::string, std::vector<branch_counter>> branch_map;
    std::string target_branch, current_target_branch;
    bool add_to_branch_target_map;

    std::unordered_map<std::string, int> branch_target_line_number_map;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                register_obj.register_load_count = 0;
                register_obj.register_operation_count = 0;
                register_obj.target_branch = "";
                register_obj.count_to_shared_mem_store = 0;
                register_obj.shared_mem_use = false;
                register_vec.clear();

                branch_vec.clear();
                target_branch = "";
                current_target_branch = "";
                add_to_branch_target_map = false;

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

            // find load operations
            if (line.find("LDG.") != std::string::npos)
            {
                std::string current_register = find_register(line);
                // if current_register already present, then add to the load_count, else create a new register_access object
                std::vector<register_access>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_access &register_index)
                                                                                     { return current_register == register_index.register_number; });
                if (register_match != register_vec.end())
                {
                    register_match->register_load_count++; // if register present, increase the load count for that register
                }
                else // else create a new register object and add to the register_vector
                {
                    register_obj.line_number = code_line_number;
                    register_obj.register_load_count = 1;
                    register_obj.register_number = current_register;
                    register_obj.register_operation_count = 0;
                    register_obj.target_branch = current_target_branch;
                    register_obj.LDG_pcOffset = pcoffset_sass(line);
                    register_obj.pcOffset = pcoffset_sass(line);
                    register_obj.count_to_shared_mem_store = 0;
                    register_vec.push_back(register_obj);
                }
            }

            if (line.find("MAD") != std::string::npos || line.find("FMA") != std::string::npos ||
                line.find("MUL") != std::string::npos || line.find("ADD") != std::string::npos ||
                line.find("ATOMS") != std::string::npos || line.find("ATOMG") != std::string::npos ||
                line.find("MUFU") != std::string::npos || line.find("RED.") != std::string::npos)
            {
                std::vector<std::string> all_registers = all_registers_used_in_line(line);
                for (const auto &i : all_registers)
                {
                    std::vector<register_access>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_access &register_index)
                                                                                         { return i == register_index.register_number; });
                    if (register_match != register_vec.end())
                    {
                        register_match->register_operation_count++; // if register is already present in the register_vec, then increase operation count
                    }
                }
            }

            // If store to shared memory detected, the register uses shared memory already
            // If LDGSTS not detected, the Asynchronous Global to Shared Memcopy can be used if LDG and STS instructions are closeby (see Hopper Instruction Set)
            // https://developer.nvidia.com/blog/controlling-data-movement-to-boost-performance-on-ampere-architecture/
            if ((line.find("STS") != std::string::npos) && (line.find("LDGSTS") == std::string::npos))
            {
                std::string current_register = find_write_register(line);
                std::vector<register_access>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_access &register_index)
                                                                                     { return current_register == register_index.register_number; });
                if (register_match != register_vec.end())
                {
                    register_match->shared_mem_use = true;
                    register_match->count_to_shared_mem_store = find_difference_cycles(register_match->LDG_pcOffset, pcoffset_sass(line));
                }
            }

            // Both LDG and STS instructions are combined
            if (line.find("LDGSTS") != std::string::npos)
            {
                std::string current_register = find_register(line);
                // if current_register already present, then add to the load_count, else create a new register_access object
                std::vector<register_access>::iterator register_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_access &register_index)
                                                                                     { return current_register == register_index.register_number; });
                if (register_match != register_vec.end())
                {
                    register_match->shared_mem_use = true;
                    register_match->register_load_count++; // if register present, increase the load count for that register
                }
                else // else create a new register object and add to the register_vector
                {
                    register_obj.shared_mem_use = true;
                    register_obj.line_number = code_line_number;
                    register_obj.register_load_count = 1;
                    register_obj.register_number = current_register;
                    register_obj.register_operation_count = 0;
                    register_obj.target_branch = current_target_branch;
                    register_obj.LDG_pcOffset = "LDGSTS";       // for async LDGSTS, label the pcOffset as different
                    register_obj.pcOffset = pcoffset_sass(line);
                    register_obj.count_to_shared_mem_store = 0;
                    register_vec.push_back(register_obj);
                }
            }

            // Check if the LDG operations are in a for/while loop
            if (line.find(" BRA ") != std::string::npos)
            {
                branch_obj.line_number = code_line_number;
                branch_obj.target_branch = find_branch(line);
                branch_obj.inside_for_loop = false;
                branch_obj.target_branch_line_number = 0;
                // the current target branch is before the BRA instruction and the line numbers are different
                if ((current_target_branch != "") && (current_target_branch == branch_obj.target_branch) && (branch_target_line_number_map[current_target_branch] != branch_obj.line_number))
                {
                    branch_obj.inside_for_loop = true;
                    branch_obj.target_branch_line_number = branch_target_line_number_map[current_target_branch];
                }

                branch_vec.push_back(branch_obj);
            }

            // https://stackoverflow.com/questions/46656688/given-a-string-how-to-check-if-the-first-few-characters-another-string-c
            if (line.substr(0, 5) == ".L_x_") // compare the first 5 characters of the string
            {
                // clean the line
                std::string remove_chars = ":"; // remove the brackets
                line.erase(std::remove_if(line.begin(), line.end(), [&remove_chars](const char &c)
                                          { return remove_chars.find(c) != std::string::npos; }),
                           line.end());
                target_branch = line;
                add_to_branch_target_map = true;
                current_target_branch = target_branch;
            }
            // add to branch_map
            if ((target_branch != line) && (add_to_branch_target_map == true))
            {
                // Store the first line number of the .L_x_ branch target
                branch_target_line_number_map[target_branch] = code_line_number;
                add_to_branch_target_map = false;
            }

            branch_map[target_branch] = branch_vec;

            counter_map[kernel_name] = register_vec;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // for (const auto& i : counter_map["_Z9bodyForceP4Bodyfi"])
    // {
    //     // only print if the load and operations on the reigster count is more than 1
    //     // (multiple access to the registers and hence can be benefitted using shared memory)
    //     if ((i.register_load_count > 0) && (i.register_operation_count > 0))
    //     {
    //         std::cout << "Register number: " << i.register_number << ", Line number: " << i.line_number << ", LDG count: " << i.register_load_count << ", operations on the register count: " << i.register_operation_count << std::endl;
    //         for (const auto& j : branch_map[i.target_branch])
    //         {
    //             if ((j.target_branch_line_number != 0) && (j.inside_for_loop == 1) && (i.target_branch == j.target_branch))
    //             {
    //                 std::cout << "Line number: " << j.line_number << ", with target branch: " << j.target_branch << ", target branch line number: " << j.target_branch_line_number << ", in for loop: " << j.inside_for_loop << std::endl;
    //             }
    //         }

    //     }
    // }

    return std::make_tuple(counter_map, branch_map);
}

#endif // PARSER_USE_SHARED_HPP
