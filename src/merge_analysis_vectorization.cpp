/**
 * Merge analysis for using vectorized load
 * SASS analysis - vectorized load (instruction LDG ) -> output code line number, no of unrolls of a register and register pressure
 * PC Sampling analysis - pc stalls (instruction LDG ) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> Long scoreboard, occupancy achieved
 *
 * @author Soumya Sen
 */

#include "parser_sass_vectorized.hpp"
#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "utilities/json.hpp"

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
    std::cout << "Stalls detected with % of occurence for the SASS instruction" << std::endl;
    for (const auto &[k, v] : map_stall_name_count)
    {
        std::cout << k << " (" << (100.0 * v) / total_samples << " %)" << std::endl;
    }
}

/// @brief Merge analysis (SASS, CUPTI, Metrics) for using vectorized load
/// @param vectorize_analysis_map Total global load count
/// @param register_map Includes base register and unrolled values data
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
/// @param live_register_map Currently used (or live) register count denoting register pressure
void merge_analysis_vectorize(std::unordered_map<std::string, load_counter> vectorize_analysis_map, std::unordered_map<std::string, std::vector<register_data>> register_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, std::unordered_map<std::string, std::vector<live_registers>> live_register_map, int save_as_json, std::string json_output_dir)
{
    json result;

    for (auto [k_sass, v_sass] : vectorize_analysis_map)
    {
        json kernel_result = {
            {"total", 0},
            {"occurrences", json::array()}
        };

        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Vectorized load analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        std::cout << "WARNING   ::  Total number of non-vectorized global load SASS instructions for this kernel: " << v_sass.global_load_count << std::endl;
        kernel_result["total"] = v_sass.global_load_count;

        for (auto [k_reg, v_reg] : register_map)
        {
            if (k_reg == k_sass) // analysis for the same kernel
            {
                for (auto index_sass : v_reg)
                {
                    json line_result;
                    // Base registers with no unrolling will show 0, hence need to ignore those counts
                    if (((index_sass.unrolls.size() - std::count(index_sass.unrolls.begin(), index_sass.unrolls.end(), 0)) > 0) && (index_sass.reg_load_type == VEC_32))
                    {
                        std::cout << "WARNING  ::  Use vectorized load for register " << index_sass.base << ", in line number " << index_sass.line_number << " of your code" << std::endl;
                        std::cout << "Register " << index_sass.base << " in line number " << index_sass.line_number << " of your code has " << index_sass.unrolls.size() - std::count(index_sass.unrolls.begin(), index_sass.unrolls.end(), 0) << " adjacent memory accesses" << std::endl;
                        line_result = {
                            {"line_number", index_sass.line_number},
                            {"pc_offset", index_sass.pcOffset},
                            {"register", index_sass.base},
                            {"unroll_pc_offsets", index_sass.unroll_pc_offsets},
                            {"adjacent_memory_accesses", index_sass.unrolls.size() - std::count(index_sass.unrolls.begin(), index_sass.unrolls.end(), 0)}
                        };
                    }
                    else
                    {
                        if (index_sass.reg_load_type == VEC_64)
                        {
                            std::cout << "INFO  ::  Register " << index_sass.base << ", in line number " << index_sass.line_number << " of your code, is already using 64-bit width vectorized load" << std::endl;
                        }
                        if (index_sass.reg_load_type == VEC_128)
                        {
                            std::cout << "INFO  ::  Register " << index_sass.base << ", in line number " << index_sass.line_number << " of your code, is already using 128-bit width vectorized load" << std::endl;
                        }
                        else
                        {
                            std::cout << "INFO  ::  Using vectorized load for register " << index_sass.base << ", in line number " << index_sass.line_number << " of your code, might not boost performance" << std::endl;
                        }
                        line_result = {
                            {"line_number", index_sass.line_number},
                            {"pc_offset", index_sass.pcOffset},
                            {"register", index_sass.base},
                            {"register_load_type", index_sass.reg_load_type}
                        };
                    }

                    // Map kernel with the PC Stall map
                    for (auto [k_pc, v_pc] : pc_stall_map)
                    {
                        if ((k_pc == k_sass)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                        {
                            for (const auto &j : v_pc)
                            {
                                if ((index_sass.line_number == j.line_number) && (index_sass.base == read_register_pair(j.sass_instruction).first)) // analyze for the same line numbers in the code and same registers in SASS
                                {
                                    /*
                                    Example: Register R12 in line number 28 in your code (for example) has 1 unrolls done by the compiler
                                    This line has a SASS instruction:    (06f0) LDG.E.SYS R15, [R12+-0x4] ;
                                    This SASSS instruction has no corresponding PC sampling stalls
                                    */

                                    // Print the number of current number of active registers
                                    if ((index_sass.reg_load_type == VEC_64) || (index_sass.reg_load_type == VEC_128) || (index_sass.reg_load_type == VEC_32))
                                    {
                                        int pcOffset_to_search = j.pc_offset;
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
                                            }
                                        }
                                    }

                                    // Note: Only printing PC stalls if there are unrolls present in the SASS for the register
                                    if (((index_sass.unrolls.size() - std::count(index_sass.unrolls.begin(), index_sass.unrolls.end(), 0)) > 0) && (index_sass.reg_load_type == VEC_32))
                                    {
                                        print_stalls_percentage(j);
                                    }
                                    break; // once register matched/found, get out of the loop
                                }
                            }
                        }
                    }

                    if (!line_result.is_null())
                        kernel_result["occurrences"].push_back(line_result);
                }
            }
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == k_sass)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "If you are using non-vectorized load/store, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " % per warp active" << std::endl;
                std::cout << "INFO  ::  Using vectorized load increases the register pressure and hence might affect occupancy. Occupancy achieved: " << v_metric.metrics_list.sm__warps_active << " %" << std::endl;

                kernel_result["metrics"] = {
                    {"smsp__warp_issue_stalled_long_scoreboard_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active},
                    {"sm__warps_active", v_metric.metrics_list.sm__warps_active},
                };
            }
        }

        result[k_sass] = kernel_result;
    }

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/vectorization.json");
        json_file << result.dump(4);
        json_file.close();
    }
}

int main(int argc, char **argv)
{
    std::string filename_hpctoolkit_sass = argv[1];
    auto sass_vectorize_tuple = vectorized_analysis(filename_hpctoolkit_sass);
    std::unordered_map<std::string, load_counter> vectorize_analysis_map = std::get<0>(sass_vectorize_tuple);
    std::unordered_map<std::string, std::vector<register_data>> register_map = std::get<1>(sass_vectorize_tuple);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::VECTORIZED_LOAD);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    std::string filename_registers = argv[6];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(filename_registers);

    int save_as_json = std::strcmp(argv[7], "true") == 0;
    std::string json_output_dir = argv[8];

    merge_analysis_vectorize(vectorize_analysis_map, register_map, pc_stall_map, metric_map, live_register_map, save_as_json, json_output_dir);

    return 0;
}
