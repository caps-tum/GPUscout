/**
 * SASS code analysis to detect datatype conversions
 *
 * @author Soumya Sen
 */

#ifndef PARSER_SASS_DATATYPE_CONVERSION_HPP
#define PARSER_SASS_DATATYPE_CONVERSION_HPP

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

std::string get_pcoffset_sass(std::string line)
{
    //         /*00a0*/                   ISETP.GE.AND P1, PT, R2, c[0x0][0x168], PT ;         -> extract 00a0
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

/// @brief Datatype conversion type and count information
struct datatype_conversions_counter
{
    int I2F_count;
    int F2I_count;
    int F2F_count;

    std::set<std::pair<int, std::string>> I2F_line;
    std::set<std::pair<int, std::string>> F2I_line;
    std::set<std::pair<int, std::string>> F2F_line;
};

/// @brief Detects type of conversion from one dataype to another
/// @param filename Disassembled SASS file
/// @return Map including datatype conversion type and count information for each kernel
std::unordered_map<std::string, datatype_conversions_counter> datatype_conversions_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    datatype_conversions_counter counter_obj;
    std::unordered_map<std::string, datatype_conversions_counter> counter_map;
    std::string kernel_name;
    int code_line_number;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                counter_obj.F2F_count = 0;
                counter_obj.F2I_count = 0;
                counter_obj.I2F_count = 0;
                counter_obj.I2F_line.clear();
                counter_obj.F2I_line.clear();
                counter_obj.F2F_line.clear();

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

            if (line.find("I2F") != std::string::npos)
            {
                counter_obj.I2F_count++;
                counter_obj.I2F_line.insert(std::make_pair(code_line_number, get_pcoffset_sass(line))); // save the last stored line number
            }
            if (line.find("F2I") != std::string::npos)
            {
                counter_obj.F2I_count++;
                counter_obj.F2I_line.insert(std::make_pair(code_line_number, get_pcoffset_sass(line))); // save the last stored line number
            }
            if (line.find("F2F") != std::string::npos)
            {
                counter_obj.F2F_count++;
                counter_obj.F2F_line.insert(std::make_pair(code_line_number, get_pcoffset_sass(line))); // save the last stored line number
            }

            counter_map[kernel_name] = counter_obj;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    return counter_map;

    // std::cout << "Kernel name: HistSM, " << "F2F count: " << counter_map["_Z9bodyForceP4Bodyfi"].F2F_count << std::endl;
    // std::cout << "Kernel name: HistSM, " << "F2I count: " << counter_map["_Z9bodyForceP4Bodyfi"].F2I_count << std::endl;
    // std::cout << "Kernel name: HistSM, " << "I2F count: " << counter_map["_Z9bodyForceP4Bodyfi"].I2F_count << std::endl;

    // for (auto i:counter_map["_Z9bodyForceP4Bodyfi"].F2I_line)
    // {
    //     std::cout << "Line number F2I: " << i << std::endl;
    // }
    // for (auto i:counter_map["_Z9bodyForceP4Bodyfi"].I2F_line)
    // {
    //     std::cout << "Line number I2F: " << i << std::endl;
    // }
    // for (auto i:counter_map["_Z9bodyForceP4Bodyfi"].F2F_line)
    // {
    //     std::cout << "Line number F2F: " << i << std::endl;
    // }
}

#endif // PARSER_SASS_DATATYPE_CONVERSION_HPP
