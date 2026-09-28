/**
 * SASS code analysis to detect warp divergence due to conditional branching
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

/// @brief Target branch information stored
struct branch_counter
{
    int line_number;
    std::string pcOffset;
    std::string target_branch;
};

struct target_line {
    int line_number;
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

/*!
 * Build the reusable static result for warp-divergence analysis.
 *
 * candidate_kernels: Kernels containing at least one conditional branch.
 * result: GUI-facing occurrences for every parsed kernel.
 * metadata: Internal counts used for terminal output.
 * @param divergence_analysis_map Includes branch information
 * @param branch_target_map Includes target branch information
 */
json build_static_warp_divergence_result(const std::unordered_map<std::string, std::vector<branch_counter>>&divergence_analysis_map, const std::unordered_map<std::string, int>& branch_target_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()},
        {"metadata", json::object()}
    };

    for (const auto& [k_sass, v_sass] : divergence_analysis_map)
    {
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass.empty())
        {
            continue;
        }

        json kernel_result = {
            {"occurrences", json::array()}
        };

        for (const auto& index_sass : v_sass)
        {
            const auto target_it = branch_target_map.find(index_sass.target_branch);
            const int target_line_number = target_it != branch_target_map.end() ? target_it->second : 0;

            // branches that has target branch in the same line numbers are not considered as conditional branching
            if (index_sass.line_number != target_line_number)
            {
                kernel_result["occurrences"].push_back({
                    {"severity", "WARNING"},
                    {"line_number", index_sass.line_number},
                    {"pc_offset", index_sass.pcOffset},
                    {"target_branch", index_sass.target_branch},
                    {"target_branch_start_line_number", target_line_number}
                });
            }
        }

        const int conditional_branch_count = static_cast<int>(kernel_result["occurrences"].size());
        const bool is_candidate = conditional_branch_count > 0;

        static_result["result"][k_sass] = std::move(kernel_result);
        static_result["metadata"][k_sass] = {
            {"conditional_branch_count", conditional_branch_count}
        };

        if (is_candidate)
        {
            static_result["candidate_kernels"].push_back({
                {"name", k_sass},
                {"demangled", get_demangled_kernel(k_sass)}
            });
        }
    }

    return static_result;
}

bool has_warp_divergence_candidate(const json& static_result)
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

    const std::string assembly = argv[1];
    const auto divergence_tuple = branches_detection(assembly);
    const auto& divergence_analysis_map = std::get<0>(divergence_tuple);
    const auto& branch_target_map = std::get<1>(divergence_tuple);

    const json static_result = build_static_warp_divergence_result(divergence_analysis_map, branch_target_map);

    const auto static_result_file = static_result_path(assembly, "warp_divergence");

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static warp divergence result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_warp_divergence_candidate(static_result) ? 0 : 1;
}

