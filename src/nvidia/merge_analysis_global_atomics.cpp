/**
 * Merge analysis for global and shared atomics
 * PTX analysis - global atomics (instruction atom.global.add/atom.shared.add) -> output code line number, number of atomics and if the atomic is present in for-loop
 * PC Sampling analysis - pc stalls (instruction RED.E.ADD/ATOMS.ADD) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> LG Throttle, Long Scoreboard and MIO Throttle
 * 
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "kernel_filter.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"
#include <cstring>
#include <iostream>

using json = nlohmann::json;

void print_stalls_percentage(const pc_issue_samples &index)
{
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
    }
}

/// @brief Merge analysis (PTX, CUPTI, Metrics) for using shared atomics instead of global atomics
/// @param static_result Static global-atomics result produced by parser_sass_global_atomics
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
json merge_analysis_global_shared_atomic(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    const auto& result = static_result["result"];
    const auto& metadata = static_result["metadata"];

    for (const auto& [krn_name, krn_result] : result.items())
    {
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        std::cout << "--------------------- Global atomics analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        const int atom_global_count = metadata[krn_name]["atom_global_count"].get<int>();
        const int atom_shared_count = metadata[krn_name]["atom_shared_count"].get<int>();
        const auto& global_line_numbers = metadata[krn_name]["global_line_numbers"];
        const auto& shared_line_numbers = metadata[krn_name]["shared_line_numbers"];
        const auto& occurrences = krn_result["occurrences"];
        json kernel_result = krn_result;

        if (atom_global_count > 0)
        {
            std::cout << "WARNING  ::  Number of global atomic instructions in the ptx file: " << atom_global_count << " detected" << std::endl;
            for (const auto& occurrence : occurrences)
            {
                if (!occurrence["is_global"].get<bool>())
                {
                    continue;
                }

                std::cout << "Global atomic operation found at line number " << occurrence["line_number"].get<int>() << " of your source code. ";
                if (occurrence["in_for_loop"].get<bool>())
                    std::cout << "This atomic instruction is found inside a for-loop" << std::endl;
            }
        }
        else if (atom_global_count == 0)
        {
            std::cout << "INFO  ::  No global atomics detected in the ptx file" << std::endl;
        }

        if (atom_shared_count > 0)
        {
            std::cout << "INFO  ::  Number of shared atomic instructions in the ptx file: " << atom_shared_count << " recorded." << std::endl;
            for (const auto& occurrence : occurrences)
            {
                if (occurrence["is_global"].get<bool>())
                {
                    continue;
                }
                std::cout << "Shared atomic operation found at line number " << occurrence["line_number"].get<int>() << " of your source code. ";
                if (occurrence["in_for_loop"].get<bool>())
                    std::cout << "This atomic instruction is found inside a for-loop" << std::endl;
            }
        }
        else if (atom_shared_count == 0)
        {
            std::cout << "INFO  ::  No shared atomics detected in the ptx file" << std::endl;
        }

        // Map kernel with the PC Stall map
        for (auto [k_pc, v_pc] : pc_stall_map)
        {
            if ((k_pc == krn_name)) // analyze for the same kernel (sass analysis and pc sampling analysis)
            {
                std::vector<int> printed_line_numbers;
                for (const auto &j : v_pc)
                {
                    for (const auto &i : global_line_numbers)
                    {
                        const int line_number = i.get<int>();
                        if (line_number == j.line_number) // analyze for the same line numbers in the code
                        {
                            if (std::find(printed_line_numbers.begin(), printed_line_numbers.end(), line_number) == printed_line_numbers.end()) // Can skip the stalls for the same code line number but different SASS lines
                            {
                                print_stalls_percentage(j);
                                printed_line_numbers.push_back(line_number); // stalls for this code line number is already printed.
                            }

                            break;
                        }
                    }
                    for (const auto &i : shared_line_numbers)
                    {
                        const int line_number = i.get<int>();
                        if (line_number == j.line_number) // analyze for the same line numbers in the code
                        {
                            if (std::find(printed_line_numbers.begin(), printed_line_numbers.end(), line_number) == printed_line_numbers.end()) // Can skip the stalls for the same code line number but different SASS lines
                            {
                                print_stalls_percentage(j);
                                printed_line_numbers.push_back(line_number); // stalls for this code line number is already printed.
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
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "INFO  ::  Data flow in memory for atomic operations" << std::endl;
                atomic_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check atomic/reduction operation)

                // copied global_mem_atomics_analysis from stalls_static_analysis_relation() method
                std::cout << "Incase of using global atomics, check LG Throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_lg_throttle_per_warp_active << " % per warp active" << std::endl;
                std::cout << "Incase of using global atomics, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " % per warp active" << std::endl;
                std::cout << "INFO  ::  For high values of the above stalls, you should prefer using shared memory instead of global memory for atomics" << std::endl;
                std::cout << "Incase of using shared atomics, check MIO throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " % per warp active" << std::endl;
                kernel_result["metrics"] = {
                    {"atom_global_count", atom_global_count},
                    {"atom_shared_count", atom_shared_count},
                };
            }
        }

        final_result[krn_name] = kernel_result;
    }

    return final_result;
}

int main(int argc, char **argv)
{
    /*! Full analysis mode:
     * exit 0 -> successful analysis
     * exit 1 -> missing or invalid static result
     * exit 2 -> invalid arguments
     */
    if (argc != 8 && argc != 9)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <hpctoolkit-sass> <executable-sass> <executable-ptx>"
                  << " <sampling-file> <metrics-file> <save-as-json>"
                  << " <json-output-dir> [kernel-filter-csv]\n";
        return 2;
    }

    std::string filename_hpctoolkit_sass = argv[1];
    // Static PTX result
    std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "global_atomics");
    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static global atomics result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::ATOMICS_GLOBAL);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];
    std::vector<std::string> kernel_filters;
    if (argc > 8)
    {
        kernel_filters = parse_kernel_filter_csv(argv[8]);
    }

    json result = merge_analysis_global_shared_atomic(static_result, pc_stall_map, metric_map, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/global_atomics.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
