/**
 * Merge analysis for datatype conversion
 * SASS analysis - datatype conversion (instruction I2F,F2I,F2F) -> output code line number and number of conversions
 * PC Sampling analysis - pc stalls (instruction I2F,F2I,F2F) -> N/A
 * Metric analysis - get metrics for entire kernel -> Stall Tex throttle
 *
 * @author Soumya Sen
 */

#include "parser_sass_datatype_conversion.hpp"
#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "utilities/json.hpp"
#include <cstring>
#include <fstream>

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
        std::cout << k << " (" << (100.0 * v) / total_samples << " %)" << std::endl;
    }
}

/// @brief Merge analysis (SASS, CUPTI, Metrics) for datatype conversion
/// @param datatype_conversion_map Includes I2F, F2I and F2F conversion data
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
json merge_analysis_datatype_conversion(std::unordered_map<std::string, datatype_conversions_counter> datatype_conversion_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map)
{
    json result;

    for (auto [k_sass, v_sass] : datatype_conversion_map)
    {
        json kernel_result = {
            {"occurrences", json::array()}
        };

        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Datatype conversion analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        if (v_sass.F2F_count > 0)
        {
            std::cout << "WARNING   ::  There are " << v_sass.F2F_count << " F2F conversions found at line numbers: ";
            for (auto i : v_sass.F2F_line)
            {
                std::cout << std::get<0>(i) << ", ";
                kernel_result["occurrences"].push_back({
                    {"severity", "WARNING"},
                    {"line_number", std::get<0>(i)},
                    {"pc_offset", std::get<1>(i)},
                    {"type", "F2F"}
                });
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "INFO  ::  No F2F conversions found" << std::endl;
        }

        if (v_sass.I2F_count > 0)
        {
            std::cout << "WARNING   ::  There are " << v_sass.I2F_count << " I2F conversions found at line numbers: ";
            for (auto i : v_sass.I2F_line)
            {
                std::cout << std::get<0>(i) << ", ";
                kernel_result["occurrences"].push_back({
                    {"severity", "WARNING"},
                    {"line_number", std::get<0>(i)},
                    {"pc_offset", std::get<1>(i)},
                    {"type", "I2F"}
                });
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "INFO  ::  No I2F conversions found" << std::endl;
        }

        if (v_sass.F2I_count > 0)
        {
            std::cout << "WARNING   ::  There are " << v_sass.F2I_count << " F2I conversions found at line numbers: ";
            for (auto i : v_sass.F2I_line)
            {
                std::cout << std::get<0>(i) << ", ";
                kernel_result["occurrences"].push_back({
                    {"severity", "WARNING"},
                    {"line_number", std::get<0>(i)},
                    {"pc_offset", std::get<1>(i)},
                    {"type", "F2I"}
                });
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "INFO  ::  No F2I conversions found" << std::endl;
        }

        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == k_sass)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                // copied datatype_conversions from stalls_static_analysis_relation() method
                std::cout << "For F2F (32 to 64 bit) conversions, check Tex throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active << " %" << std::endl;
                std::cout << "For I2F and F2F (32 bit only) conversions, check MIO throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " %" << std::endl;
                std::cout << "For I2F and F2F (32 bit only) conversions, check Short Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_short_scoreboard_per_warp_active << " %" << std::endl;
            }
        }

        result[k_sass] = kernel_result;
    }

    return result;
}

int main(int argc, char **argv)
{
    std::string filename_hpctoolkit_sass = argv[1];
    std::unordered_map<std::string, datatype_conversions_counter> datatype_conversion_map = datatype_conversions_analysis(filename_hpctoolkit_sass);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::DATATYPE_CONVERSION);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];

    json result = merge_analysis_datatype_conversion(datatype_conversion_map, pc_stall_map, metric_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/datatype_conversion.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}

