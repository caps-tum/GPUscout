/**
 * SASS code analysis to detect warp divergence due to conditional branching
 *
 * @author Soumya Sen
 */

#ifndef PARSER_SASS_DIVERGENCE_HPP
#define PARSER_SASS_DIVERGENCE_HPP

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

/// @brief Target branch information stored
struct branch_counter
{
    int line_number;
    std::string pcOffset;
    std::string target_branch;
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

std::string get_pcoffset_sass(std::string line)
{
    //         /*00a0*/                   ISETP.GE.AND P1, PT, R2, c[0x0][0x168], PT ;         -> extract 00a0
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

/// @brief Detects conditional branching
/// @param filename Disassembled SASS file
/// @return Tuple of two maps, first map includes branch information and second map includes target branch line number
std::tuple<std::unordered_map<std::string, std::vector<branch_counter>>, std::unordered_map<std::string, int>> branches_detection(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    branch_counter counter_obj;
    std::vector<branch_counter> branch_vec;
    std::unordered_map<std::string, std::vector<branch_counter>> counter_map;
    std::string kernel_name;
    int code_line_number;

    std::unordered_map<std::string, int> branch_target_line_number_map;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                branch_vec.clear();
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

            if (line.find(" BRA ") != std::string::npos)
            {
                counter_obj.line_number = code_line_number;
                counter_obj.pcOffset = get_pcoffset_sass(line);
                counter_obj.target_branch = find_branch(line);
                branch_vec.push_back(counter_obj);
            }

            // https://stackoverflow.com/questions/46656688/given-a-string-how-to-check-if-the-first-few-characters-another-string-c
            if (line.substr(0, 5) == ".L_x_") // compare the first 5 characters of the string
            {
                // clean the line
                std::string remove_chars = ":"; // remove the brackets
                line.erase(std::remove_if(line.begin(), line.end(), [&remove_chars](const char &c)
                                          { return remove_chars.find(c) != std::string::npos; }),
                           line.end());
                // Store the first line number of the .L_x_ branch target
                branch_target_line_number_map[line] = code_line_number;
            }

            counter_map[kernel_name] = branch_vec;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // for (const auto& i:counter_map["_Z3dotPiS_S_"])
    // {
    //     std::cout << "Kernel name: _Z3dotPiS_S_, " << "Branch line number: " << i.line_number << ", with target branch: " << i.target_branch << " (target branch starts at line: " << branch_target_line_number_map[i.target_branch] << ")" << std::endl;
    // }

    return std::make_tuple(counter_map, branch_target_line_number_map);
}

#endif // PARSER_SASS_DIVERGENCE_HPP
