/**
 * Merge analysis for using __restrict__
 * SASS analysis - restrict usage (instruction LDG) -> output code line number, flag if the register is unused later in the code and register pressure
 * PC Sampling analysis - pc stalls (instruction LDG) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> IMC miss stall
 *
 * @author Soumya Sen
 */

#include "parser_sass_restrict.hpp"
#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "utilities/json.hpp"
#include <cstddef>

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

/// @brief Merge analysis (SASS, CUPTI, Metrics) for using restricted pointers
/// @param restrict_analysis_map Read-only and non-aliased data of registers in the kernel
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
/// @param live_register_map Currently used (or live) register count denoting register pressure
void merge_analysis_restrict(std::unordered_map<std::string, std::vector<register_used>> restrict_analysis_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, std::unordered_map<std::string, std::vector<live_registers>> live_register_map, int save_as_json, std::string json_output_dir)
{
    json result;

    for (auto [k_sass, v_sass] : restrict_analysis_map)
    {
        json kernel_result = {
            {"occurrences", json::array()}
        };
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Use of __restrict__ analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        std::vector<register_used> unused_registers;
        for (auto index_sass : v_sass)
        {
            json line_result = {};
            if (index_sass.flag == NOT_USED)
            {
                if (index_sass.read_only_mem_used)
                {
                    std::cout << "INFO  ::  Register " << index_sass.register_number << ", in line number " << index_sass.line_number << " of your code, is already using read-only cache" << std::endl;
                }
                else
                {
                    std::cout << "INFO  ::  Register " << index_sass.register_number << ", in line number " << index_sass.line_number << " of your code, is not aliased anywhere in the kernel" << std::endl;
                    unused_registers.push_back(index_sass);
                    std::cout << "WARNING  ::  You can benifit from using __restrict__ for register " << index_sass.register_number << " at line number " << index_sass.line_number << " of your code" << std::endl;
                }

                line_result = {
                    {"line_number", index_sass.line_number},
                    {"register", index_sass.register_number},
                    {"read_only_memory_used", index_sass.read_only_mem_used}
                };

                // Map kernel with the PC Stall map
                for (auto [k_pc, v_pc] : pc_stall_map)
                {
                    if ((k_pc == k_sass)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                    {
                        for (const auto &j : v_pc)
                        {
                            if ((index_sass.line_number == j.line_number) && (get_register_from_line(j.sass_instruction) == index_sass.register_number)) // analyze for the same line numbers in the code and same registers in SASS
                            {
                                // Print the number of current number of active registers
                                int pcOffset_to_search = j.pc_offset; // convert dec to hex
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

                                if (!index_sass.read_only_mem_used)
                                {
                                    json stalls = print_stalls_percentage(j);
                                    line_result["stalls"] = stalls;
                                }
                                break; // once register matched/found, get out of the loop
                            }
                        }
                    }
                }
            }
            if (!line_result.is_null())
                kernel_result["occurrences"].push_back(line_result);
        }

        if (unused_registers.size() == 0)
        {
            // std::cout << "INFO  ::  You can not benifit from using __restrict__ for any of the registers at the given line numbers in your code" << std::endl;
            std::cout << "INFO  ::  None of the registers, not already using read-only cache, can benifit from using __restrict__" << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == k_sass)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "If using __restrict__ (read-only cache), check IMC miss: " << v_metric.metrics_list.smsp__warp_issue_stalled_imc_miss_per_warp_active << " % per warp active" << std::endl;
                kernel_result["metrics"] = {
                    {"smsp__warp_issue_stalled_imc_miss_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_imc_miss_per_warp_active}
                };
            }
        }

        result[k_sass] = kernel_result;
    }

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/use_restricted.json");
        json_file << result.dump(4);
        json_file.close();
    }
}

int main(int argc, char **argv)
{
    std::string filename_hpctoolkit_sass = argv[1];
    std::unordered_map<std::string, std::vector<register_used>> restrict_analysis_map = restrict_analysis(filename_hpctoolkit_sass);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::RESTRICT_USE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    std::string filename_registers = argv[6];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(filename_registers);

    int save_as_json = std::strcmp(argv[7], "true") == 0;
    std::string json_output_dir = argv[8];

    merge_analysis_restrict(restrict_analysis_map, pc_stall_map, metric_map, live_register_map, save_as_json, json_output_dir);
}
