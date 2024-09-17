/**
 * Merge analysis for using Shared memory
 * SASS analysis - shared usage (instruction LDG) -> output code line number and if the load is present in a for- loop
 * PC Sampling analysis - pc stalls (instruction LDG) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> long scoreboard stall, MIO stall, shared memory bank conflicts and data flow in shared memory
 *
 * @author Soumya Sen
 */

#include "parser_sass_use_shared.hpp"
#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "utilities/json.hpp"

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
        std::cout << k << " (" << (100.0 * v) / total_samples << " %)" << std::endl;
        stalls[k] = (100.0  * v / total_samples);
    }
    return stalls;
}

/// @brief Merge analysis (SASS, CUPTI, Metrics) for using shared memory instead of global memory
/// @param shared_analysis_map Includes information about the register load from global memory and arithmetic instructions using it
/// @param branch_map Target branch information to detect if the atomic operation is in a for-loop
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
void merge_analysis_use_shared(std::unordered_map<std::string, std::vector<register_access>> shared_analysis_map, std::unordered_map<std::string, std::vector<branch_counter>> branch_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, int save_as_json, std::string json_output_dir)
{
    json result;

    for (auto [k_sass, v_sass] : shared_analysis_map)
    {
        json kernel_result = {
            {"occurrences", {}}
        };

        bool shared_recommend_flag = false;
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Use shared memory analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        for (auto index_sass : v_sass)
        {
            json line_result;
            // only print if the load count > 0 and operations on the register count is more than 1 and operations count more than load count
            // (i.e. multiple access to the registers and hence can be benefitted using shared memory)
            // also only print if there is a for loop detected inside which the load operations of the registers happen
            if ((index_sass.register_load_count > 0) && (index_sass.register_operation_count > 1) && (index_sass.register_operation_count > index_sass.register_load_count))
            {
                if (index_sass.shared_mem_use)
                {
                    shared_recommend_flag = false;
                    // If already using async memcpy for SM >80 (LDGSTS instruction)
                    if (index_sass.LDG_pcOffset == "LDGSTS")
                    {
                        std::cout << "INFO  ::  Register number " << index_sass.register_number << " is already using asynchronous global to shared memory copy at line number " << index_sass.line_number << " of your code" << std::endl;
                        line_result = {
                            {"line_number", index_sass.line_number},
                            {"pc_offset", index_sass.pcOffset},
                            {"register", index_sass.register_number},
                            {"uses_shared_memory", true},
                            {"uses_async_global_to_shared_memory_copy", true},
                        };
                    }

                    // If already storing global reads to shared memory (without async memcpy)
                    else
                    {
                        std::cout << "INFO  ::  Register number " << index_sass.register_number << " is already storing data in shared memory at line number " << index_sass.line_number << " of your code" << std::endl;
                        line_result = {
                            {"line_number", index_sass.line_number},
                            {"pc_offset", index_sass.pcOffset},
                            {"register", index_sass.register_number},
                            {"uses_shared_memory", true},
                            {"uses_async_global_to_shared_memory_copy", false},
                        };
                        if (index_sass.count_to_shared_mem_store > 0)
                        {
                            std::cout << "Data loaded from global memory is stored to shared memory after " << index_sass.count_to_shared_mem_store << " instructions. Asynchronous global to shared memcopy might help for SM > 80" << std::endl;
                            line_result["instruction_count_to_shared_mem_store"] = index_sass.count_to_shared_mem_store;
                            line_result["lgd_pc_offset"] = index_sass.LDG_pcOffset;
                        }
                    }
                }
                else
                {
                    for (const auto &j : branch_map[index_sass.target_branch])
                    {
                        if ((j.target_branch_line_number != 0) && (index_sass.target_branch == j.target_branch))
                        {
                            std::cout << "Register number " << index_sass.register_number << " at line number " << index_sass.line_number << " of your code has " << index_sass.register_load_count << " total global load counts and " << index_sass.register_operation_count << " computation instruction counts" << std::endl;
                            line_result = {
                                {"line_number", index_sass.line_number},
                                {"pc_offset", index_sass.pcOffset},
                                {"register", index_sass.register_number},
                                {"uses_shared_memory", false},
                                {"global_load_count", index_sass.register_load_count},
                                {"global_load_pc_offsets", index_sass.register_load_pc_offsets},
                                {"computation_instruction_count", index_sass.register_operation_count},
                                {"computation_instruction_pc_offsets", index_sass.register_operation_pc_offsets},
                                {"in_for_loop", j.inside_for_loop},
                            };
                            if (j.inside_for_loop)
                            {
                                std::cout << "This register seems to be in a for loop and hence will perform multiple load operations" << std::endl;

                                // // Map kernel with the PC Stall map
                                for (auto [k_pc, v_pc] : pc_stall_map)
                                {
                                    if ((k_pc == k_sass)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                                    {
                                        for (const auto &j_pc : v_pc)
                                        {
                                            if ((index_sass.line_number == j_pc.line_number) && (get_register_from_line(j_pc.sass_instruction) == index_sass.register_number))
                                            {
                                                json stalls = print_stalls_percentage(j_pc);
                                                stalls["line_number"] = index_sass.line_number;
                                                kernel_result["stalls"].push_back(stalls);
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            std::cout << "WARNING  ::  Since the data at register number " << index_sass.register_number << " is accessed multiple times, you can benifit from using shared memory instead of global memory." << std::endl;

                            shared_recommend_flag = true;
                        }
                    }
                }
            }

            if (!line_result.is_null())
                kernel_result["occurrences"].push_back(line_result);
        }

        if (!shared_recommend_flag)
        {
            std::cout << "INFO  ::  No global loads found in the kernel which can benifit from using shared memory" << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == k_sass)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "INFO  ::  Check data flow in shared memory, if you modify your code to use shared memory" << std::endl;
                json data_memory_flow_metrics = shared_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check shared memory flow)

                // copied use_shared_memory_analysis from stalls_static_analysis_relation() method
                std::cout << "If using shared memory, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;
                std::cout << "If using shared memory, check MIO throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " %" << std::endl;

                //  If multiple threads in the same warp request access to the same memory bank, the accesses are serialized
                std::cout << "INFO  ::  Check bank conflict in shared memory, if you modify your code to use shared memory." << std::endl;
                json bank_conflict_metrics = shared_memory_bank_conflict(metric_map[k_metric]); // show how many way bank conflict present in the shared memory access

                kernel_result["metrics"] = {
                    {"data_memory_flow", data_memory_flow_metrics},
                    {"bank_conflict", bank_conflict_metrics},
                    {"smsp__warp_issue_stalled_long_scoreboard_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active},
                    {"smsp__warp_issue_stalled_mio_throttle_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active}
                };
            }
        }

        result[k_sass] = kernel_result;
    }

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/use_shared.json");
        json_file << result.dump(4);
        json_file.close();
    }
}

int main(int argc, char **argv)
{
    std::string filename_hpctoolkit_sass = argv[1];
    auto shared_analysis_tuple = use_shared_analysis(filename_hpctoolkit_sass);
    std::unordered_map<std::string, std::vector<register_access>> shared_analysis_map = std::get<0>(shared_analysis_tuple);
    std::unordered_map<std::string, std::vector<branch_counter>> branch_map = std::get<1>(shared_analysis_tuple);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::SHARED_USE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];

    merge_analysis_use_shared(shared_analysis_map, branch_map, pc_stall_map, metric_map, save_as_json, json_output_dir);

    return 0;
}
