/**
 * Merge analysis for warp divergence
 * SASS analysis - warp divergence/conditional branching (instruction BRA) -> output code line number and target branch with it's line number
 * PC Sampling analysis - pc stalls (instruction BRA) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> coalescing efficiency and branch divergent percentage
 *
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "kernel_filter.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

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
    std::cout << "Stalls are detected with % of occurence for the SASS instruction" << std::endl;
    for (const auto &[k, v] : map_stall_name_count)
    {
        // std::cout << "Stall detected: " << k << ", accounting for: " << (100.0*v)/total_samples<< " % of stalls for this SASS" << std::endl;
        std::cout << k << " (" << (100.0 * v) / total_samples << " %)" << std::endl;
    }
}

/// @brief Merge analysis (SASS, CUPTI, Metrics) for detecting conditional branching and warp divergence
/// @param static_result Static result produced by parser_sass_warp_divergence.
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
json merge_analysis_divergence(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, const std::vector<std::string> &kernel_filters)
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

        std::cout << "--------------------- Warp divergence detection analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        const int conditional_branch_count = metadata[krn_name]["conditional_branch_count"].get<int>();
        const auto& occurrences = krn_result["occurrences"];
        json kernel_result = krn_result;

        for (const auto& occurrence : occurrences)
        {
            std::cout << "Conditional branching detected in line number " << occurrence["line_number"].get<int>() << " of your code, with target branch: " << occurrence["target_branch"].get<std::string>() << " (target branch starts at line number: " << occurrence["target_branch_start_line_number"].get<int>() << ")" << std::endl;

            // Map kernel with the PC Stall map
            for (auto [k_pc, v_pc] : pc_stall_map)
            {
                if ((k_pc == krn_name)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                {
                    for (const auto &j : v_pc)
                    {
                        if ((occurrence["line_number"].get<int>()== j.line_number)) // analyze for the same line numbers in the code
                        {
                            print_stalls_percentage(j);
                            break; // once register matched/found, get out of the loop
                        }
                    }
                }
            }
        }

        if (conditional_branch_count == 0)
        {
            std::cout << "INFO  ::  No conditional branching detected in the kernel"
                      << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
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
        final_result[krn_name] = std::move(kernel_result);
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
    const std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "warp_divergence");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static warp divergence result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::WARP_DIVERGENCE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];
    std::vector<std::string> kernel_filters;
    if (argc > 8)
    {
        kernel_filters = parse_kernel_filter_csv(argv[8]);
    }

    json result = merge_analysis_divergence(static_result, pc_stall_map, metric_map, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/warp_divergence.json");
        json_file << result.dump(4);
        json_file.close();
    }
}
