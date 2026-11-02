/**
 * Merge analysis for deadlock detection
 * SASS analysis - deadlock detection (instruction ATOM.E.CAS, @P BRA, SYNC, ATOM.E.EXCH) -> output if deadlock possible
 * PC Sampling analysis - N/A
 * Metric analysis - N/A
 * 
 * @author Soumya Sen
 */

#include "../utilities/json.hpp"
#include "../utilities/helper.hpp"
#include "kernel_filter.hpp"
#include <cstring>
#include <cstring>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

/// @brief Detects deadlock in code
/// @param static_result Static deadlock detection result produced by parser_deadlock_detection
json merge_analysis_deadlock_detection(const json& static_result, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    const auto& result = static_result["result"];

    for (const auto& [krn_name, kernel_result] : result.items())
    {
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        const bool deadlock_detect_flag = kernel_result["metrics"]["deadlock_detect_flag"].get<bool>();

        std::cout << "--------------------- Deadlock detect analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        (deadlock_detect_flag) ? std::cout << "WARNING   ::  Possibility of deadlock detected in kernel: " << krn_name << std::endl : std::cout << "INFO   ::  No deadlock detected in kernel: " << krn_name << std::endl;

        final_result[krn_name] = kernel_result;
    }

    return final_result;
}

int main(int argc, char **argv)
{
    /*! Full analysis mode:
     *
     * exit 0 -> successful analysis
     * exit 1 -> missing or invalid static result
     * exit 2 -> invalid arguments
     */
    if (argc != 8 && argc != 9)
    {
        std::cerr
            << "Usage: " << argv[0]
            << " <hpctoolkit-sass> <executable-sass> <executable-ptx>"
            << " <sampling-file> <metrics-file> <save-as-json>"
            << " <json-output-dir> [kernel-filter-csv]\n";
        return 2;
    }

    std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "deadlock_detection");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr
            << "ERROR: Missing or invalid static deadlock-detection result: "
            << static_result_file << std::endl;
        return 1;
    }

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];
    std::vector<std::string> kernel_filters;
    if (argc > 8)
    {
        kernel_filters = parse_kernel_filter_csv(argv[8]);
    }

    json result = merge_analysis_deadlock_detection(static_result, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/deadlock_detection.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
