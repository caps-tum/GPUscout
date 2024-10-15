/**
 * Merge analysis for register spilling
 * SASS analysis - register spilling (instruction LDL/STL) -> output code line number and register pressure
 * PC Sampling analysis - pc stalls (instruction LDL/STL) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> long scoreboard stall and % of memory traffic due to LMEM in L1-L2
 *
 * @author Soumya Sen
 */

#include "parser_sass_register_spilling.hpp"
#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "utilities/json.hpp"
#include <ostream>

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

/// @brief Merge analysis (SASS, CUPTI, Metrics) for register spilling to local memory
/// @param spilling_analysis_map Includes register load/store to local memory data
/// @param track_register_map Includes previous arithmetic SASS instruction of the register
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
/// @param live_register_map Currently used (or live) register count denoting register pressure
json merge_analysis_register_spill(std::unordered_map<std::string, std::vector<local_memory_counter>> spilling_analysis_map, std::unordered_map<std::string, std::vector<track_register_instruction>> track_register_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, std::unordered_map<std::string, std::vector<live_registers>> live_register_map)
{
    json result;

    for (auto [k_sass, v_sass] : spilling_analysis_map)
    {
        json kernel_result = {
            {"occurrences", json::array()}
        };
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Register spilling analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        bool spilled_detected_flag = false;
        for (auto index_sass : v_sass)
        {
            // Find the register spill info from the SASS analysis
            std::cout << "WARNING   ::  Spill detected in line number " << index_sass.line_number << " of your code. Base register number " << index_sass.register_number << " spilled in " << lmem_operation_type_string[index_sass.op_type] << " operation" << std::endl;
            json line_result = {
                {"line_number", index_sass.line_number},
                {"register", index_sass.register_number},
                {"pc_offset", index_sass.pcOffset},
                {"operation", lmem_operation_type_string[index_sass.op_type]}
            };
            for (auto last_reg : track_register_map[k_sass])
            {
                if (index_sass.register_number == last_reg.register_number)
                {
                    std::cout << "The previous compute instruction of register: " << index_sass.register_number << " before spilling was " << last_reg.last_instruction << " at line number " << last_reg.last_line_number << " of your code" << std::endl;
                    line_result["previous_compute_instruction"] = {
                        {"instruction", last_reg.last_instruction},
                        {"line_number", last_reg.last_line_number},
                        {"pc_offset", last_reg.last_pcOffset}
                    };
                }
            }

            // Print the number of current number of active registers
            int pcOffset_to_search = std::stoul(index_sass.pcOffset, nullptr, 16); // convert hex to dec
            std::vector<live_registers>::iterator reg_search_it = std::find_if(live_register_map[k_sass].begin(), live_register_map[k_sass].end(), [&](const live_registers &register_index)
                                                                               { return pcOffset_to_search == std::stoul(register_index.pcOffset, nullptr, 16); });
            if (reg_search_it != live_register_map[k_sass].end())
            {
                // std::cout << reg_search_it->gen_reg << ", " << reg_search_it->pred_reg << " ," << reg_search_it->u_gen_reg << std::endl;
                std::cout << "INFO  ::  Total current registers for the SASS instruction: " << reg_search_it->gen_reg + reg_search_it->pred_reg + reg_search_it->u_gen_reg << std::endl;
                line_result["used_register_count"] = reg_search_it->gen_reg + reg_search_it->pred_reg + reg_search_it->u_gen_reg;
                if (reg_search_it->change_reg_from_last > 0)
                {
                    std::cout << "Increased register pressure with " << std::abs(reg_search_it->change_reg_from_last) << " more registers compared to last SASS instruction" << std::endl;
                    line_result["register_pressure_increase"] = std::abs(reg_search_it->change_reg_from_last);
                } else {
                    line_result["register_pressure_increase"] = 0;
                }
            }

            spilled_detected_flag = true;

            // Map kernel with the PC Stall map
            for (auto [k_pc, v_pc] : pc_stall_map)
            {
                if ((k_pc == k_sass)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                {
                    for (const auto &j : v_pc)
                    {
                        if (index_sass.line_number == j.line_number) // analyze for the same line numbers in the code
                        {
                            print_stalls_percentage(j);
                            break;
                        }
                    }
                }
            }
            if (!line_result.is_null())
                kernel_result["occurrences"].push_back(line_result);
        }

        if (!spilled_detected_flag)
        {
            std::cout << "INFO  ::  No register spilling detected in your kernel: " << k_sass << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == k_sass)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "INFO  ::  Data flow in memory for load operations" << std::endl;
                json memory_flow_metrics = load_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check local memory flow)

                // copied register_spilling_analysis from stalls_static_analysis_relation() method
                std::cout << "For register spilling, check Long Scoreboard stalls: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " % per warp active" << std::endl;
                std::cout << "For register spilling, check LG Throttle stalls: " << v_metric.metrics_list.smsp__warp_issue_stalled_lg_throttle_per_warp_active << " % per warp active" << std::endl;
                auto local_load_store = v_metric.metrics_list.smsp__inst_executed_op_local_ld + v_metric.metrics_list.smsp__inst_executed_op_local_st;
                int total_SM = 144; // TODO: this should be editable
                auto estimated_l2_queries_lmem_allSM = 2 * 4 * total_SM * ((1 - (v_metric.metrics_list.l1tex__t_sector_hit_rate / 100)) * local_load_store);
                auto total_l2_queries = v_metric.metrics_list.lts__t_sectors_op_read + v_metric.metrics_list.lts__t_sectors_op_write + v_metric.metrics_list.lts__t_sectors_op_atom + v_metric.metrics_list.lts__t_sectors_op_red;
                auto l2_queries_lmem_percent = estimated_l2_queries_lmem_allSM / total_l2_queries;
                std::cout << estimated_l2_queries_lmem_allSM << " - " << total_l2_queries << std::endl;
                std::cout << "Percentage of total L2 queries due to LMEM: " << l2_queries_lmem_percent << " %" << std::endl;
                std::cout << "WARNING   ::  If the above percentage is high, it means the memory traffic between the SMs and L2 cache is mostly due to LMEM (need to contain register spills)" << std::endl;
                kernel_result["metrics"] = {
                    {"memory_flow", memory_flow_metrics},
                    {"smsp__warp_issue_stalled_long_scoreboard_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active},
                    {"smsp__warp_issue_stalled_lg_throttle_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_lg_throttle_per_warp_active},
                    {"l2_queries_due_to_mem_perc", l2_queries_lmem_percent},
                    {"total_l2_queries", total_l2_queries},
                    {"smsp__inst_executed_op_local_ld", v_metric.metrics_list.smsp__inst_executed_op_local_ld},
                    {"smsp__inst_executed_op_local_st", v_metric.metrics_list.smsp__inst_executed_op_local_st},
                    {"smsp__sass_inst_executed", v_metric.metrics_list.smsp__sass_inst_executed},
                    {"sm__warps_active", v_metric.metrics_list.sm__warps_active},
                    {"smsp__warps_active", v_metric.metrics_list.smsp__warps_active}
                };
            };
        }

        result[k_sass] = kernel_result;
    }

    return result;
}

int main(int argc, char **argv)
{
    // std::string filename_hpctoolkit_sass = argv[1];
    std::string filename_executable_sass = argv[2];
    auto sass_spilling_tuple = register_spilling_analysis(filename_executable_sass);
    std::unordered_map<std::string, std::vector<local_memory_counter>> spilling_analysis_map = std::get<0>(sass_spilling_tuple);
    std::unordered_map<std::string, std::vector<track_register_instruction>> track_register_map = std::get<1>(sass_spilling_tuple);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_executable_sass, analysis_kind::REGISTER_SPILLING);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    std::string filename_registers = argv[6];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(filename_registers);

    int save_as_json = std::strcmp(argv[7], "true") == 0;
    std::string json_output_dir = argv[8];

    json result = merge_analysis_register_spill(spilling_analysis_map, track_register_map, pc_stall_map, metric_map, live_register_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/register_spilling.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
