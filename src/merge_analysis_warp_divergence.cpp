/**
 * Merge analysis for warp divergence
 * SASS analysis - warp divergence/conditional branching (instruction BRA) -> output code line number and target branch with it's line number
 * PC Sampling analysis - pc stalls (instruction BRA) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> coalescing efficiency and branch divergent percentage
 *
 * @author Soumya Sen
 */

#include "parser_sass_divergence.hpp"
#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "utilities/json.hpp"

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
    std::cout << "Stalls are detected with % of occurence for the SASS instruction" << std::endl;
    for (const auto &[k, v] : map_stall_name_count)
    {
        // std::cout << "Stall detected: " << k << ", accounting for: " << (100.0*v)/total_samples<< " % of stalls for this SASS" << std::endl;
        std::cout << k << " (" << (100.0 * v) / total_samples << " %)" << std::endl;
        stalls[k] = (100.0  * v / total_samples);
    }
    return stalls;
}

/// @brief Merge analysis (SASS, CUPTI, Metrics) for detecting conditional branching and warp divergence
/// @param divergence_analysis_map Includes branch information
/// @param branch_target_map Includes target branch information
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
void merge_analysis_divergence(std::unordered_map<std::string, std::vector<branch_counter>> divergence_analysis_map, std::unordered_map<std::string, target_line> branch_target_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, int save_as_json, std::string json_output_dir)
{
    json result;

    for (auto [k_sass, v_sass] : divergence_analysis_map)
    {
        json kernel_result = {
            {"occurrences", json::array()}
        };

        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Warp divergence detection analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        for (const auto &index_sass : v_sass)
        {
            json line_result;

            if (index_sass.line_number != branch_target_map[index_sass.target_branch].line_number) // branches that has target branch in the same line numbers are not considered as conditional branching
            {
                std::cout << "Conditional branching detected in line number " << index_sass.line_number << " of your code, with target branch: " << index_sass.target_branch << " (target branch starts at line number: " << branch_target_map[index_sass.target_branch].line_number << ")" << std::endl;
                line_result = {
                   {"line_number", index_sass.line_number} ,
                   {"pc_offset", index_sass.pcOffset},
                   {"target_branch", index_sass.target_branch},
                   {"target_branch_start_line_number", branch_target_map[index_sass.target_branch].line_number},
                   {"target_branch_start_pc_offset", branch_target_map[index_sass.target_branch].pcOffset}
                };

                // Map kernel with the PC Stall map
                for (auto [k_pc, v_pc] : pc_stall_map)
                {
                    if ((k_pc == k_sass)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                    {
                        for (const auto &j : v_pc)
                        {
                            if ((index_sass.line_number == j.line_number)) // analyze for the same line numbers in the code
                            {
                                json stalls = print_stalls_percentage(j);
                                line_result["stalls"] = stalls;
                                break; // once register matched/found, get out of the loop
                            }
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
                double branch_divergence_percent = 100.0 * v_metric.metrics_list.sm__sass_branch_targets_threads_divergent / v_metric.metrics_list.sm__sass_branch_targets;
                if (branch_divergence_percent > 0)
                {
                    std::cout << "WARNING   ::  Average number of branches that diverge in your code: " << branch_divergence_percent << " %" << std::endl;
                }
                else
                {
                    std::cout << "INFO  ::  No branches are diverging in your code" << std::endl;
                }

                kernel_result["metrics"] = {
                    {"branch_divergence_perc", branch_divergence_percent}
                };
            }
        }
        result[k_sass] = kernel_result;
    }

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/warp_divergence.json");
        json_file << result.dump(4);
        json_file.close();
    }

}

int main(int argc, char **argv)
{
    std::string filename_hpctoolkit_sass = argv[1];
    auto divergence_tuple = branches_detection(filename_hpctoolkit_sass);
    std::unordered_map<std::string, std::vector<branch_counter>> divergence_analysis_map = std::get<0>(divergence_tuple);
    std::unordered_map<std::string, target_line> branch_target_map = std::get<1>(divergence_tuple);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::WARP_DIVERGENCE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];

    merge_analysis_divergence(divergence_analysis_map, branch_target_map, pc_stall_map, metric_map, save_as_json, json_output_dir);
}
