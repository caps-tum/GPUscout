/**
 * SASS code analysis to detect datatype conversions
 *
 * @author Soumya Sen
 */
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

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

using json = nlohmann::json;

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
    std::string k_sass;
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
                k_sass = line;
                // std::cout << k_sass << std::endl;
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

            counter_map[k_sass] = counter_obj;
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

/*!
 * Build the reusable static result for datatype-conversion analysis.
 *
 * candidate_kernels: Kernels for which at least one conversion was detected.
 * result: Fields belonging to the final datatype_conversion.json consumed by GUI.
 * metadata: Internal values required for terminal output. These values are not copied to the final GUI JSON.
 * @param datatype_conversion_map Includes I2F, F2I and F2F conversion data
 */
json build_static_datatype_conversion_result(const std::unordered_map<std::string, datatype_conversions_counter>& datatype_conversion_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()},
        {"metadata", json::object()}
    };

    for (const auto& [k_sass, v_sass] : datatype_conversion_map)
    {
        json kernel_result = {
            {"occurrences", json::array()}
        };

        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass.empty())
        {
            continue;
        }

        const bool is_candidate = v_sass.F2F_count > 0 || v_sass.I2F_count > 0 || v_sass.F2I_count > 0;

        // Information needed only for terminal output.
        static_result["metadata"][k_sass] = {
            {"F2F_count", v_sass.F2F_count},
            {"I2F_count", v_sass.I2F_count},
            {"F2I_count", v_sass.F2I_count}
        };

        // Information consumed by the GUI.
        for (const auto& [line_number, pc_offset] : v_sass.F2F_line)
        {
            kernel_result["occurrences"].push_back({
                {"severity", "WARNING"},
                {"line_number", line_number},
                {"pc_offset", pc_offset},
                {"type", "F2F"}
            });
        }

        for (const auto& [line_number, pc_offset] : v_sass.I2F_line)
        {
            kernel_result["occurrences"].push_back({
                {"severity", "WARNING"},
                {"line_number", line_number},
                {"pc_offset", pc_offset},
                {"type", "I2F"}
            });
        }

        for (const auto& [line_number, pc_offset] : v_sass.F2I_line)
        {
            kernel_result["occurrences"].push_back({
                {"severity", "WARNING"},
                {"line_number", line_number},
                {"pc_offset", pc_offset},
                {"type", "F2I"}
            });
        }

        static_result["result"][k_sass] = std::move(kernel_result);

        if (!is_candidate)
        {
            continue;
        }

        static_result["candidate_kernels"].push_back({
            {"name", k_sass},
            {"demangled", get_demangled_kernel(k_sass)}
        });
    }

    return static_result;
}

bool has_datatype_conversion_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <sass-file> <ptx-file>\n";
        return 2;
    }

    /*! Static detection mode:
     *
     *  1. Parse SASS using the existing detection algorithm.
     *  2. Build and save the reusable static JSON.
     *  3. Return whether at least one candidate kernel was detected.
     *
     *  exit 0 -> datatype conversion detected
     *  exit 1 -> no datatype conversion detected
     *  exit 2 -> invalid arguments or static-result write failure
     */
    const std::string assembly = argv[1];
    std::ifstream assembly_file(assembly);
    if (!assembly_file.is_open())
    {
        std::cerr << "ERROR: Could not open SASS file: "
                << assembly << std::endl;
        return 2;
    }
    assembly_file.close();
    const auto datatype_conversion_map = datatype_conversions_analysis(assembly);
    const json static_result = build_static_datatype_conversion_result(datatype_conversion_map);

    const auto static_result_file = static_result_path(assembly, "datatype_conversion");

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static datatype conversion result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_datatype_conversion_candidate(static_result) ? 0 : 1;
}
