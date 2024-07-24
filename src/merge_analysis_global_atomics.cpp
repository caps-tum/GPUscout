/**
 * Merge analysis for global and shared atomics
 * PTX analysis - global atomics (instruction atom.global.add/atom.shared.add) -> output code line number, number of atomics and if the atomic is present in for-loop
 * PC Sampling analysis - pc stalls (instruction RED.E.ADD/ATOMS.ADD) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> LG Throttle, Long Scoreboard and MIO Throttle
 *
 * @author Soumya Sen
 */

#include "parser_ptx_global_atomics.hpp"
#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "utilities/json.hpp"
#include <cstring>

using json = nlohmann::json;

json print_stalls_percentage(const pc_issue_samples &index)
{
    json stalls;

    // Printing the stall with percentage of samples
    // std::cout << "Underlying SASS Instruction: " << index.sass_instruction << " corresponding to your code line number: " << index.line_number << std::endl;
    auto total_samples = 0;
    for (const auto &j : index.stall_name_count_pair)
    {
        total_samples += j.second;
    }
    std::unordered_map<std::string, int> map_stall_name_count;
    for (const auto &j : index.stall_name_count_pair)
    {
        map_stall_name_count[mapping_stall_reasons_to_names(j.first)] += j.second;
    }
    std::cout << "Stalls are detected with % of occurence for the PTX instruction" << std::endl;
    for (const auto &[k, v] : map_stall_name_count)
    {
        std::cout << k << " (" << (100.0 * v) / total_samples << " %)" << std::endl;
        stalls[k + "_perc"] = (100.0  * v / total_samples);
    }

    return stalls;
}

/// @brief Merge analysis (PTX, CUPTI, Metrics) for using shared atomics instead of global atomics
/// @param ptx_atomic_map Includes global and shared atomic data
/// @param branch_map Target branch information to detect if the atomic operation is in a for-loop
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
void merge_analysis_global_shared_atomic(std::unordered_map<std::string, atomic_counter> ptx_atomic_map, std::unordered_map<std::string, std::vector<branch_counter>> branch_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, int save_as_json, std::string json_output_dir)
{
    json result;

    for (auto [k_sass, v_sass] : ptx_atomic_map)
    {
        json kernel_result = {
            {"global_atomics", {
                {"total", 0},
                {"line_numbers", {}},
            }},
            {"shared_atomics", {
                {"total", 0},
                {"line_numbers", {}},
            }}
        };

        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Global atomics analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        if (v_sass.atom_global_count > 0)
        {
            std::cout << "WARNING  ::  Number of global atomic instructions in the ptx file: " << v_sass.atom_global_count << " detected" << std::endl;
            kernel_result["global_atomics"]["total"] = v_sass.atom_global_count;
            for (const auto &i : v_sass.atom_global_line_number)
            {
                for (const auto [k_branch, v_branch] : branch_map)
                {
                    for (const auto &k : v_branch)
                    {
                        if (k_branch == k_sass)
                        {
                            if ((k.atom_global_line_number.find(i) != k.atom_global_line_number.end()) && (k.target_branch_line_number != 0))
                            {
                                std::cout << "Global atomic operation found at line number " << i << " of your source code. ";
                                if (k.inside_for_loop == true)
                                    std::cout << "This atomic instruction is found inside a for-loop" << std::endl;

                                result["global_atomics"]["line_numbers"][i] = {
                                    {"in_for_loop", k.inside_for_loop}
                                };
                            }
                        }
                    }
                }
            }
        }
        else if (v_sass.atom_global_count == 0)
        {
            std::cout << "INFO  ::  No global atomics detected in the ptx file" << std::endl;
        }

        if (v_sass.atom_shared_count > 0)
        {
            std::cout << "INFO  ::  Number of shared atomic instructions in the ptx file: " << v_sass.atom_shared_count << " recorded." << std::endl;
            kernel_result["shared_atomics"]["total"] = v_sass.atom_shared_count;
            for (const auto &i : v_sass.atom_shared_line_number)
            {
                for (const auto [k_branch, v_branch] : branch_map)
                {
                    for (const auto &k : v_branch)
                    {
                        if (k_branch == k_sass)
                        {
                            if ((k.atom_shared_line_number.find(i) != k.atom_shared_line_number.end()) && (k.target_branch_line_number != 0))
                            {
                                std::cout << "Shared atomic operation found at line number " << i << " of your source code. ";
                                if (k.inside_for_loop == true)
                                    std::cout << "This atomic instruction is found inside a for-loop" << std::endl;

                                result["shared_atomics"]["line_numbers"][i] = {
                                    {"in_for_loop", k.inside_for_loop}
                                };
                            }
                        }
                    }
                }
            }
        }
        else if (v_sass.atom_shared_count == 0)
        {
            std::cout << "INFO  ::  No shared atomics detected in the ptx file" << std::endl;
        }

        // Map kernel with the PC Stall map
        for (auto [k_pc, v_pc] : pc_stall_map)
        {
            if ((k_pc == k_sass)) // analyze for the same kernel (sass analysis and pc sampling analysis)
            {
                std::vector<int> printed_line_numbers;
                for (const auto &j : v_pc)
                {
                    for (const auto &i : v_sass.atom_global_line_number)
                    {
                        if (i == j.line_number) // analyze for the same line numbers in the code
                        {
                            if (std::find(printed_line_numbers.begin(), printed_line_numbers.end(), i) == printed_line_numbers.end()) // Can skip the stalls for the same code line number but different SASS lines
                            {
                                json stalls = print_stalls_percentage(j);
                                printed_line_numbers.push_back(i); // stalls for this code line number is already printed.
                                kernel_result["global_atomics"]["line_numbers"][i]["stalls"] = stalls;
                            }

                            break;
                        }
                    }
                    for (const auto &i : v_sass.atom_shared_line_number)
                    {
                        if (i == j.line_number) // analyze for the same line numbers in the code
                        {
                            if (std::find(printed_line_numbers.begin(), printed_line_numbers.end(), i) == printed_line_numbers.end()) // Can skip the stalls for the same code line number but different SASS lines
                            {
                                json stalls = print_stalls_percentage(j);
                                printed_line_numbers.push_back(i); // stalls for this code line number is already printed.
                                kernel_result["shared_atomics"]["line_numbers"][i]["stalls"] = stalls;
                            }

                            break;
                        }
                    }
                }
            }
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == k_sass)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                kernel_result["metrics"] = {};
                std::cout << "INFO  ::  Data flow in memory for atomic operations" << std::endl;
                json memory_flow_metrics = atomic_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check atomic/reduction operation)
                kernel_result["metrics"]["memory_flow"] = memory_flow_metrics;

                // copied global_mem_atomics_analysis from stalls_static_analysis_relation() method
                std::cout << "Incase of using global atomics, check LG Throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_lg_throttle_per_warp_active << " % per warp active" << std::endl;
                std::cout << "Incase of using global atomics, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " % per warp active" << std::endl;
                std::cout << "INFO  ::  For high values of the above stalls, you should prefer using shared memory instead of global memory for atomics" << std::endl;
                std::cout << "Incase of using shared atomics, check MIO throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " % per warp active" << std::endl;
                kernel_result["metrics"]["lg_throttle_perc"] = v_metric.metrics_list.smsp__warp_issue_stalled_lg_throttle_per_warp_active;
                kernel_result["metrics"]["long_scoreboard_perc"] = v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active;
                kernel_result["metrics"]["mio_throttle_perc"] = v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active;
            }
        }

        result[k_sass] = kernel_result;
    }

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/global_atomics.json");
        json_file << result.dump();
        json_file.close();
    }
}

int main(int argc, char **argv)
{
    std::string filename_hpctoolkit_sass = argv[1];
    std::string filename_ptx = argv[3];
    auto atomics_analysis_tuple = global_mem_atomics_analysis(filename_ptx);
    std::unordered_map<std::string, atomic_counter> ptx_atomic_map = std::get<0>(atomics_analysis_tuple);
    std::unordered_map<std::string, std::vector<branch_counter>> branch_map = std::get<1>(atomics_analysis_tuple);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::ATOMICS_GLOBAL);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];

    merge_analysis_global_shared_atomic(ptx_atomic_map, branch_map, pc_stall_map, metric_map, save_as_json, json_output_dir);

    return 0;
}
