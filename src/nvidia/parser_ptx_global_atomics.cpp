/**
 * PTX code analysis to detect global and shared atomics
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
#include <fstream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <memory>
#include <set>

using json = nlohmann::json;

/// @brief Target branch information to detect if instruction is in a for-loop
struct branch_counter
{
    std::set<int> atom_global_line_number;
    std::set<int> atom_shared_line_number;
    std::string target_branch;
    int target_branch_line_number;
    bool inside_for_loop = false; // if the target branch is inside a for loop
};

std::string find_target_branch(std::string line)
{
    // @%p8 bra $L__BB2_11;     -> extract $L__BB2_11
    std::string substr, last_string;

    std::istringstream ss(line);
    while (std::getline(ss, substr, ' ')) {
        last_string = substr;
    }

    std::string remove_chars = " ;";
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());

      return substr;
}

/// @brief Number of atomic instructions and corresponding code line number information
struct atomic_counter
{
    int atom_global_count;
    int atom_shared_count;
    std::set<std::tuple<int, int>> atom_global_line_number;
    std::set<std::tuple<int, int>> atom_shared_line_number;
};

int get_line_number_from_ptx(std::string line)
{
    // .loc	2 77 10, function_name $L__info_string0, inlined_at 1 18 5      -> extract 18
    std::string stall_name, substr, last_string;
    std::istringstream ss(line);
    while (std::getline(ss, substr, ','))
    {
        last_string = substr;
    }
    std::istringstream ss2(last_string);
    for (int i = 0; i < 4; i++)
    {
        std::getline(ss2, substr, ' ');
    }

    return std::stoi(substr);
}

int get_branch_line_number_from_ptx(std::string line)
{
    //.loc	1 39 34      -> extract 39
    std::string substr;
    std::istringstream ss(line);
    for (int i = 0; i < 2; i++)
    {
        std::getline(ss, substr, ' ');
    }
    return std::stoi(substr);
}

/// @brief Detects global and shared atomics by parsing PTX file
/// @param filename Decoded PTX file
/// @return Tuple of two maps, first map includes global and shared atomic count and second map includes target branch information
std::tuple<std::unordered_map<std::string, atomic_counter>, std::unordered_map<std::string, std::vector<branch_counter>>> global_mem_atomics_analysis(const std::string &filename) {

    std::string line;
    std::fstream file(filename, std::ios::in);

    atomic_counter counter_obj;
    std::unordered_map<std::string, atomic_counter> counter_map;
    std::string k_sass;

    branch_counter branch_obj;
    std::vector<branch_counter> branch_vec;
    std::unordered_map<std::string, std::vector<branch_counter>> branch_map;
    std::string target_branch, current_target_branch;
    bool add_to_branch_target_map;

    std::unordered_map<std::string, int> branch_target_line_number_map;

    int code_line_number = 0;
    int code_line_number_raw = 0;
    int branch_code_line_number = 0;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".visible .entry ") != std::string::npos) // denotes start of the kernel
            {
                counter_obj.atom_global_count = 0;
                counter_obj.atom_shared_count = 0;
                counter_obj.atom_global_line_number.clear();
                counter_obj.atom_shared_line_number.clear();
                code_line_number = 0;
                code_line_number_raw = 0;
                branch_code_line_number = 0;

                branch_vec.clear();
                branch_obj.atom_global_line_number.clear();
                branch_obj.atom_shared_line_number.clear();
                branch_obj.inside_for_loop = false;
                branch_obj.target_branch = "";
                branch_obj.target_branch_line_number = 0;
                target_branch = "";
                current_target_branch = "";
                add_to_branch_target_map = false;

                // https://cplusplus.com/reference/string/string/erase/     - erase part of a string
                line.erase(line.begin(), line.begin() + 16); // erase the first 16 character of the name of the kernel
                line.erase(line.end() - 1, line.end()); // erase the last 1 character of the name of the kernel
                k_sass = line;
                // std::cout << k_sass << std::endl;
            }

            if (line.find(".loc") == std::string::npos && line.find(".visible") == std::string::npos && line.find(".file") == std::string::npos && line.substr(0, 4) != "$L__")
            {
                code_line_number_raw++;
            }

            if ((line.find(".loc") != std::string::npos) && (line.find("inlined_at") != std::string::npos))
            {
                code_line_number = get_line_number_from_ptx(line);
            }
            if ((line.find(".loc") != std::string::npos) && (line.find("inlined_at") == std::string::npos) && (line.find(".local") == std::string::npos))
            {
                branch_code_line_number = get_branch_line_number_from_ptx(line);
            }

            if (line.find("atom.global.add") != std::string::npos)
            {
                counter_obj.atom_global_count++;
                counter_obj.atom_global_line_number.insert(std::make_tuple(code_line_number, code_line_number_raw));

                std::vector<branch_counter>::iterator last_branch_match = std::find_if(branch_vec.begin(), branch_vec.end(), [&](const branch_counter &i)
                                                                                       { return i.target_branch == current_target_branch;
                    });
                if (last_branch_match != branch_vec.end())
                {
                    last_branch_match->atom_global_line_number.insert(code_line_number);
                }
            }
            if (line.find("atom.shared.add") != std::string::npos)
            {
                counter_obj.atom_shared_count++;
                counter_obj.atom_shared_line_number.insert(std::make_tuple(code_line_number, code_line_number_raw));

                std::vector<branch_counter>::iterator last_branch_match = std::find_if(branch_vec.begin(), branch_vec.end(), [&](const branch_counter &i)
                                                                                       { return i.target_branch == current_target_branch;
                    });
                if (last_branch_match != branch_vec.end())
                {
                    last_branch_match->atom_shared_line_number.insert(code_line_number);
                }
            }

            // Find if a particular line number is in a for-loop.
            // No need for finding if a register is in a loop

            if (line.substr(0, 4) == "$L__") // compare the first 4 characters of the string
            {
                // clean the line
                std::string remove_chars = ":"; // remove the brackets
                line.erase(std::remove_if(line.begin(), line.end(), [&remove_chars](const char &c)
                                          { return remove_chars.find(c) != std::string::npos; }),
                           line.end());
                target_branch = line;
                add_to_branch_target_map = true;
                current_target_branch = target_branch;

                branch_obj.target_branch = target_branch;
                branch_obj.inside_for_loop = false;
                branch_obj.target_branch_line_number = 0;

                branch_vec.push_back(branch_obj);
            }

            // add to branch_map
            if ((target_branch != line) && (add_to_branch_target_map == true))
            {
                // Store the first line number after the .L_x_ branch target
                branch_target_line_number_map[current_target_branch] = branch_code_line_number;
                add_to_branch_target_map = false;
            }

            if (line.find("bra $L__") != std::string::npos)
            {
                std::vector<branch_counter>::iterator last_branch_match = std::find_if(branch_vec.begin(), branch_vec.end(), [&](const branch_counter &i)
                                                                                       { return i.target_branch == find_target_branch(line); });
                if (last_branch_match != branch_vec.end())
                {
                    last_branch_match->target_branch_line_number = branch_target_line_number_map[last_branch_match->target_branch];
                    if ((last_branch_match->target_branch != "") && (last_branch_match->target_branch_line_number != code_line_number))
                    {
                        last_branch_match->inside_for_loop = true;
                    }
                }
            }

            branch_map[k_sass] = branch_vec;

            counter_map[k_sass] = counter_obj;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // std::cout << "Kernel name: _Z4HistPiiPfi, " << "Global atomics: " <<
    // counter_map["_Z4HistPiiPfi"].atom_global_count << std::endl; std::cout <<
    // "Kernel name: _Z4HistPiiPfi, " << "Shared atomics: " <<
    // counter_map["_Z4HistPiiPfi"].atom_shared_count << std::endl; for (const
    // auto& i : counter_map["_Z4HistPiiPfi"].atom_global_line_number)
    // {
    //     std::cout << "Global atomic line number: " << i << std::endl;
    // }
    // for (const auto& i : counter_map["_Z4HistPiiPfi"].atom_shared_line_number)
    // {
    //     std::cout << "Shared atomic line number: " << i << std::endl;
    // }

    // for (const auto& k : branch_map["_Z28histogram_gmem_atomics_spillPK7IN_TYPEiiPj"])
    // {
    //     for (const auto& l : k.atom_global_line_number)
    //     std::cout << l << "," << k.target_branch_line_number << "," << k.inside_for_loop << std::endl;
    // }

    return std::make_tuple(counter_map, branch_map);
}

/*!
 * Build the reusable static result for global-atomics analysis.
 *
 * candidate_kernels: Kernels for which at least one global atomic was detected.
 * result: Fields belonging to the final global_atomics.json consumed by GUI.
 * metadata: Internal values required for terminal output and PC sampling. These values are not copied to the final GUI JSON.
 * @param ptx_atomic_map Includes global and shared atomic data
 * @param branch_map Target branch information to detect if the atomic operation is in a for-loop
 */
json build_static_global_atomics_result(const std::unordered_map<std::string, atomic_counter>& ptx_atomic_map, const std::unordered_map<std::string, std::vector<branch_counter>>& branch_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()},
        {"metadata", json::object()}
    };

    for (const auto& [k_sass, v_sass] : ptx_atomic_map)
    {
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass.empty())
        {
            continue;
        }

        json kernel_result = {
            {"occurrences", json::array()}
        };

        const auto branch_it = branch_map.find(k_sass);
        if (branch_it != branch_map.end())
        {
            if (v_sass.atom_global_count > 0)
            {
                for (auto& i : v_sass.atom_global_line_number)
                {
                    for (const auto& branch : branch_it->second)
                    {
                        if ((branch.atom_global_line_number.find(std::get<0>(i)) != branch.atom_global_line_number.end()) && (branch.target_branch_line_number != 0))
                        {
                            kernel_result["occurrences"].push_back({
                                {"severity", "WARNING"},
                                {"line_number", std::get<0>(i)},
                                {"line_number_raw", std::get<1>(i)},
                                {"in_for_loop", branch.inside_for_loop},
                                {"is_global", true}
                            });
                        }
                    }
                }
            }

            if (v_sass.atom_shared_count > 0)
            {
                for (const auto& i : v_sass.atom_shared_line_number)
                {
                    for (const auto& branch : branch_it->second)
                    {
                        if ((branch.atom_shared_line_number.find(std::get<0>(i)) != branch.atom_shared_line_number.end()) && (branch.target_branch_line_number != 0))
                        {
                            kernel_result["occurrences"].push_back({
                                {"severity", "INFO"},
                                {"line_number", std::get<0>(i)},
                                {"line_number_raw", std::get<1>(i)},
                                {"in_for_loop", branch.inside_for_loop},
                                {"is_global", false}
                            });
                        }
                    }
                }
            }
        }

        json global_line_numbers = json::array();
        for (const auto& i : v_sass.atom_global_line_number)
        {
            global_line_numbers.push_back(std::get<0>(i));
        }

        json shared_line_numbers = json::array();
        for (const auto& i : v_sass.atom_shared_line_number)
        {
            shared_line_numbers.push_back(std::get<0>(i));
        }

        static_result["result"][k_sass] = std::move(kernel_result);
        static_result["metadata"][k_sass] = {
            {"atom_global_count", v_sass.atom_global_count},
            {"atom_shared_count", v_sass.atom_shared_count},
            {"global_line_numbers", std::move(global_line_numbers)},
            {"shared_line_numbers", std::move(shared_line_numbers)}
        };

        // Only kernels with global atomics considered as candidate
        if (v_sass.atom_global_count == 0)
            continue;

        static_result["candidate_kernels"].push_back({
            {"name", k_sass},
            {"demangled", get_demangled_kernel(k_sass)}
        });
    }

    return static_result;
}

bool has_global_atomics_candidate(const json& static_result)
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
     *  1. Parse PTX using the existing detection algorithm.
     *  2. Build and save the reusable static JSON.
     *  3. Return whether at least one candidate kernel was detected.
     *
     *  exit 0 -> global atomic operation detected
     *  exit 1 -> no global atomic operation detected
     *  exit 2 -> invalid arguments or static-result write failure
     */
    const std::string assembly = argv[1];
    const std::string ptx = argv[2];
    const auto atomics_analysis_tuple = global_mem_atomics_analysis(ptx);
    const auto& ptx_atomic_map = std::get<0>(atomics_analysis_tuple);
    const auto& branch_map = std::get<1>(atomics_analysis_tuple);
    const json static_result = build_static_global_atomics_result(ptx_atomic_map, branch_map);

    const auto static_result_file = static_result_path(assembly, "global_atomics");

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static global atomics result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_global_atomics_candidate(static_result) ? 0 : 1;
}

