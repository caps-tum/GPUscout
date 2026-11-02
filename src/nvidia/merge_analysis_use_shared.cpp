/**
 * Merge analysis for using Shared memory
 * SASS analysis - shared usage (instruction LDG) -> output code line number and if the load is present in a for- loop
 * PC Sampling analysis - pc stalls (instruction LDG) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> long scoreboard stall, MIO stall, shared memory bank conflicts and data flow in shared memory
 *
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "kernel_filter.hpp"
#include "../utilities/json.hpp"
#include "../utilities/helper.hpp"

using json = nlohmann::json;

std::string get_register_from_line(std::string line)
{
    //         /*03a0*/                   IMAD.IADD R5, R3, 0x1, R7 ;       -> extract R5
    std::string substr, last_string;
    line.erase(line.begin(), line.begin() + 35); // erase the first 35 character of the name of the kernel
    std::istringstream ss(line);
    std::getline(ss, substr, ',');
    std::istringstream ss1(substr);
    while (std::getline(ss1, substr, ' '))
    {
        last_string = substr;
    }
    return last_string;
}

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

/// @brief Merge analysis (SASS, CUPTI, Metrics) for using shared memory instead of global memory
/// @param static_result Static result produced by parser_sass_use_shared.
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
json merge_analysis_use_shared(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    const auto& result = static_result["result"];

    for (const auto& [krn_name, krn_result] : result.items())
    {
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        std::cout << "--------------------- Use shared memory analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        const auto& occurrences = krn_result["occurrences"];
        bool shared_recommend_flag = false;

        for (const auto& occurrence : occurrences)
        {
            const bool uses_shared_memory = occurrence["uses_shared_memory"].get<bool>();

            if (uses_shared_memory)
            {
                const bool uses_async_copy = occurrence["uses_async_global_to_shared_memory_copy"].get<bool>();
                
                if (uses_async_copy)
                {   
                    std::cout << "INFO  ::  Register number " << occurrence["register"].get<std::string>() << " is already using asynchronous global to shared memory copy at line number " << occurrence["line_number"].get<int>() << " of your code" << std::endl;
                }

                else
                {
                    std::cout << "INFO  ::  Register number " << occurrence["register"].get<std::string>() << " is already storing data in shared memory at line number " << occurrence["line_number"].get<int>() << " of your code" << std::endl;
                    const int instruction_count = occurrence["instruction_count_to_shared_mem_store"].get<int>();
                    if (instruction_count > 0)
                    {
                        std::cout << "Data loaded from global memory is stored to shared memory after " << instruction_count << " instructions. Asynchronous global to shared memcopy might help for SM > 80" << std::endl;
                    }
                }
            }
            else
            {
                shared_recommend_flag = true;
                std::cout << "Register number " << occurrence["register"].get<std::string>() << " at line number " << occurrence["line_number"].get<int>() << " of your code has " << occurrence["global_load_count"].get<int>() << " total global load counts and " << occurrence["computation_instruction_count"].get<int>() << " computation instruction counts" << std::endl;
                
                if (occurrence["in_for_loop"].get<bool>())
                {
                    std::cout << "This register seems to be in a for loop and hence will perform multiple load operations" << std::endl;

                    // // Map kernel with the PC Stall map
                    for (auto [k_pc, v_pc] : pc_stall_map)
                    {
                        if ((k_pc == krn_name)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                        {
                            for (const auto &j_pc : v_pc)
                            {
                                if ((occurrence["line_number"].get<int>() == j_pc.line_number) && (get_register_from_line(j_pc.sass_instruction) == occurrence["register"].get<std::string>()))
                                {
                                    print_stalls_percentage(j_pc);
                                    break;
                                }
                            }
                        }
                    }
                }
                std::cout << "WARNING  ::  Since the data at register number " << occurrence["register"].get<std::string>() << " is accessed multiple times, you can benifit from using shared memory instead of global memory." << std::endl;
            }
        }

        if (!shared_recommend_flag)
        {
            std::cout << "INFO  ::  No global loads found in the kernel which can benifit from using shared memory" << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "INFO  ::  Check data flow in shared memory, if you modify your code to use shared memory" << std::endl;
                shared_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check shared memory flow)

                // copied use_shared_memory_analysis from stalls_static_analysis_relation() method
                std::cout << "If using shared memory, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;
                std::cout << "If using shared memory, check MIO throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " %" << std::endl;

                //  If multiple threads in the same warp request access to the same memory bank, the accesses are serialized
                std::cout << "INFO  ::  Check bank conflict in shared memory, if you modify your code to use shared memory." << std::endl;
                shared_memory_bank_conflict(metric_map[k_metric]); // show how many way bank conflict present in the shared memory access
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
    const auto static_result_file = static_result_path(filename_executable_sass, "use_shared");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static use-shared result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::SHARED_USE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];
    std::vector<std::string> kernel_filters;
    if (argc > 8)
    {
        kernel_filters = parse_kernel_filter_csv(argv[8]);
    }

    json result = merge_analysis_use_shared(static_result, pc_stall_map, metric_map, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/use_shared.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
