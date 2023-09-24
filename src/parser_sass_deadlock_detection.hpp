/**
 * SASS code analysis to detect possibility of a deadlock in code
 *
 * @author Soumya Sen
 */

#ifndef PARSER_SASS_DEADLOCK_DETECTION_HPP
#define PARSER_SASS_DEADLOCK_DETECTION_HPP

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

struct deadlock_detect
{
    bool deadlock_detect_flag;
};

/// @brief Detects possibility of a deadlock in the user code
/// @param filename Disassembled SASS file
/// @return Map containing deadlock detection flag for each kernel
std::unordered_map<std::string, deadlock_detect> deadlock_detection_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    bool inside_cas, branch_in_cas, sync_in_cas = false;
    deadlock_detect deadlock_detect_obj;
    std::unordered_map<std::string, deadlock_detect> counter_map;

    std::string kernel_name;
    int code_line_number;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                inside_cas = false;
                branch_in_cas = false;
                sync_in_cas = false;
                deadlock_detect_obj.deadlock_detect_flag = false;

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

            if (line.find("ATOM.E.CAS") != std::string::npos)
            {
                inside_cas = true;
            }

            if ((line.find("@P") != std::string::npos) && (line.find(" BRA ") != std::string::npos) && (inside_cas))
            {
                branch_in_cas = true;
            }

            if ((line.find("SYNC") != std::string::npos) && (branch_in_cas))
            {
                sync_in_cas = true;
                // std::cout << "WARNING   ::  Deadlock possibility in kernel: " << kernel_name << std::endl;
                deadlock_detect_obj.deadlock_detect_flag = true;
            }

            if (line.find("ATOM.E.EXCH") != std::string::npos)
            {
                inside_cas = false;
            }

            counter_map[kernel_name] = deadlock_detect_obj;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    return counter_map;
}


#endif // PARSER_SASS_DEADLOCK_DETECTION_HPP
