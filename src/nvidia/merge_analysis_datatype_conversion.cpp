/**
 * Merge analysis for datatype conversion
 * SASS analysis - datatype conversion (instruction I2F,F2I,F2F) -> output code line number and number of conversions
 * PC Sampling analysis - pc stalls (instruction I2F,F2I,F2F) -> N/A
 * Metric analysis - get metrics for entire kernel -> Stall Tex throttle
 *
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"
#include "kernel_filter.hpp"
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
/// @param static_result Static datatype-conversion result produced by parser_sass_datatype_conversion
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
json merge_analysis_datatype_conversion(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    auto& result = static_result["result"];
    auto& metadata = static_result["metadata"];

    for (const auto& [krn_name, krn_result] : result.items())
    {
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        const int F2F_count = metadata[krn_name]["F2F_count"].get<int>();
        const int I2F_count = metadata[krn_name]["I2F_count"].get<int>();
        const int F2I_count = metadata[krn_name]["F2I_count"].get<int>();
        const auto& occurrences = krn_result["occurrences"];

        std::cout << "--------------------- Datatype conversion analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        if (F2F_count > 0)
        {
            std::cout << "WARNING   ::  There are " << F2F_count << " F2F conversions found at line numbers: ";
            for (const auto& occurrence : occurrences)
            {
                if (occurrence["type"].get<std::string>() != "F2F")
                {
                    continue;
                }
                std::cout << occurrence["line_number"].get<int>() << ", ";
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "INFO  ::  No F2F conversions found" << std::endl;
        }

        if (I2F_count > 0)
        {
            std::cout << "WARNING   ::  There are " << I2F_count << " I2F conversions found at line numbers: ";
            for (const auto& occurrence : occurrences)
            {
                if (occurrence["type"].get<std::string>() != "I2F")
                {
                    continue;
                }
                std::cout << occurrence["line_number"].get<int>() << ", ";
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "INFO  ::  No I2F conversions found" << std::endl;
        }

        if (F2I_count > 0)
        {
            std::cout << "WARNING   ::  There are " << F2I_count << " F2I conversions found at line numbers: ";
            for (const auto& occurrence : occurrences)
            {
                if (occurrence["type"].get<std::string>() != "F2I")
                {
                    continue;
                }
                std::cout << occurrence["line_number"].get<int>() << ", ";
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "INFO  ::  No F2I conversions found" << std::endl;
        }

        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                // copied datatype_conversions from stalls_static_analysis_relation() method
                std::cout << "For F2F (32 to 64 bit) conversions, check Tex throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active << " %" << std::endl;
                std::cout << "For I2F and F2F (32 bit only) conversions, check MIO throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " %" << std::endl;
                std::cout << "For I2F and F2F (32 bit only) conversions, check Short Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_short_scoreboard_per_warp_active << " %" << std::endl;
            }
        }
        final_result[krn_name] = krn_result;
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
    if (argc != 8 && argc != 9) // “kernel_filters" as optional arg
    {
        std::cerr << "Usage: " << argv[0]
                  << " <hpctoolkit-sass> <executable-sass> <executable-ptx>"
                  << " <sampling-file> <metrics-file> <save-as-json>"
                  << " <json-output-dir> [kernel-filter-csv]\n";
        return 2;
    }
    std::string filename_hpctoolkit_sass = argv[1];
    std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "datatype_conversion");
    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static datatype conversion result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::DATATYPE_CONVERSION);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];
    std::vector<std::string> kernel_filters;
    if (argc > 8)
    {
        kernel_filters = parse_kernel_filter_csv(argv[8]);
    }

    json result = merge_analysis_datatype_conversion(static_result, pc_stall_map, metric_map, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/datatype_conversion.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
