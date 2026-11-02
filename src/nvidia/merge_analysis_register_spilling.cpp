/**
 * Merge analysis for register spilling
 * SASS analysis - register spilling (instruction LDL/STL) -> output code line number and register pressure
 * PC Sampling analysis - pc stalls (instruction LDL/STL) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> long scoreboard stall and % of memory traffic due to LMEM in L1-L2
 *
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"
#include "kernel_filter.hpp"
#include <ostream>
#include <string>

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
/// @param static_result Static register-spilling result produced by parser_sass_register_spilling
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
/// @param live_register_map Currently used (or live) register count denoting register pressure
json merge_analysis_register_spill(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, std::unordered_map<std::string, std::vector<live_registers>> live_register_map, int total_SM, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    const auto& result = static_result["result"];
    const auto& metadata = static_result["metadata"];

    for (const auto& [krn_name, krn_result] : result.items())
    {
        // Fix for blank kernel name appearing in the analysis_map
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        std::cout << "--------------------- Register spilling analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        const int spill_count = metadata[krn_name]["spill_count"].get<int>();
        bool spilled_detected_flag = spill_count > 0;
        json kernel_result = krn_result;
        auto& occurrences = kernel_result["occurrences"];

        for (auto& occurrence : occurrences)
        {
            // Find the register spill info from the SASS analysis
            std::cout << "WARNING   ::  Spill detected in line number " << occurrence["line_number"].get<int>() << " of your code. Base register number " << occurrence["register"].get<std::string>() << " spilled in " << occurrence["operation"].get<std::string>() << " operation" << std::endl;
            
            if (occurrence.count("previous_compute_instruction") > 0)
            {
                const auto& previous_compute = occurrence["previous_compute_instruction"];
                std::cout << "The previous compute instruction of register: " << occurrence["register"].get<std::string>() << " before spilling was " << previous_compute["instruction"].get<std::string>() << " at line number " << previous_compute["line_number"].get<int>() << " of your code" << std::endl;
            }

            // Print the number of current number of active registers
            int pcOffset_to_search = std::stoul(occurrence["pc_offset"].get<std::string>(), nullptr, 16); // convert hex to dec
            std::vector<live_registers>::iterator reg_search_it = std::find_if(live_register_map[krn_name].begin(), live_register_map[krn_name].end(), [&](const live_registers &register_index)
                                                                               { return pcOffset_to_search == std::stoul(register_index.pcOffset, nullptr, 16); });
            if (reg_search_it != live_register_map[krn_name].end())
            {
                // std::cout << reg_search_it->gen_reg << ", " << reg_search_it->pred_reg << " ," << reg_search_it->u_gen_reg << std::endl;
                std::cout << "INFO  ::  Total current registers for the SASS instruction: " << reg_search_it->gen_reg + reg_search_it->pred_reg + reg_search_it->u_gen_reg << std::endl;
                occurrence["used_register_count"] = reg_search_it->gen_reg + reg_search_it->pred_reg + reg_search_it->u_gen_reg;
                if (reg_search_it->change_reg_from_last > 0)
                {
                    std::cout << "Increased register pressure with " << std::abs(reg_search_it->change_reg_from_last) << " more registers compared to last SASS instruction" << std::endl;
                    occurrence["register_pressure_increase"] = std::abs(reg_search_it->change_reg_from_last);
                } else {
                    occurrence["register_pressure_increase"] = 0;
                }
            }

            // Map kernel with the PC Stall map
            for (auto [k_pc, v_pc] : pc_stall_map)
            {
                if ((k_pc == krn_name)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                {
                    for (const auto &j : v_pc)
                    {
                        if (occurrence["line_number"].get<int>() == j.line_number) // analyze for the same line numbers in the code
                        {
                            print_stalls_percentage(j);
                            break;
                        }
                    }
                }
            }
        }

        if (!spilled_detected_flag)
        {
            std::cout << "INFO  ::  No register spilling detected in your kernel: " << krn_name << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "INFO  ::  Data flow in memory for load operations" << std::endl;
                load_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check local memory flow)

                // copied register_spilling_analysis from stalls_static_analysis_relation() method
                std::cout << "For register spilling, check Long Scoreboard stalls: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " % per warp active" << std::endl;
                std::cout << "For register spilling, check LG Throttle stalls: " << v_metric.metrics_list.smsp__warp_issue_stalled_lg_throttle_per_warp_active << " % per warp active" << std::endl;
               
                // The suggested way is to estimate the L1-missed sectors to L2 for local-memory loads and stores and then compute the percentage of total L2 queries due to LMEM.
                // Estimate the corresponding L1-missed sectors for local-memory loads.
                auto local_load_l1_missed_sectors =  v_metric.metrics_list.l1tex__t_sectors_pipe_lsu_mem_local_op_ld * (1 - (v_metric.metrics_list.l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate / 100.0));
                // Estimate the corresponding L1-missed sectors for local-memory stores.
                auto local_store_l1_missed_sectors =  v_metric.metrics_list.l1tex__t_sectors_pipe_lsu_mem_local_op_st * (1 - (v_metric.metrics_list.l1tex__t_sector_pipe_lsu_mem_local_op_st_hit_rate / 100.0));
                // Total L2 sector queries are computed as the sum of read and write sector queries. According to the Nsight Compute Nvprof Transition Guide: https://archive.docs.nvidia.com/nsight-compute/2022.1/NsightComputeCli/index.html#nvprof-guide
                // total read  = read  + atom + red
                // total write = write + atom + red
                // Hence atomic and reduction sectors contribute to both sides and are counted twice.
                auto total_l2_queries = v_metric.metrics_list.lts__t_sectors_op_read + v_metric.metrics_list.lts__t_sectors_op_write + 2 * v_metric.metrics_list.lts__t_sectors_op_atom + 2 * v_metric.metrics_list.lts__t_sectors_op_red;
                // Approximate the share of total L2 read/write sector traffic caused by local-memory accesses that miss in L1.
                auto estimated_l2_queries_lmem_allSM = local_load_l1_missed_sectors + local_store_l1_missed_sectors;
                auto l2_queries_lmem_percent = 100.0f * estimated_l2_queries_lmem_allSM / total_l2_queries; // multiply by 100 to get percentage
                std::cout << estimated_l2_queries_lmem_allSM << " - " << total_l2_queries << std::endl;
                std::cout << "Percentage of total L2 queries due to LMEM: " << l2_queries_lmem_percent << " %" << std::endl;
                std::cout << "WARNING   ::  If the above percentage is high, it means the memory traffic between the SMs and L2 cache is mostly due to LMEM (need to contain register spills)" << std::endl;
            };
        }

        final_result[krn_name] = kernel_result;
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
    if (argc != 10 && argc != 11)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <hpctoolkit-sass> <executable-sass> <executable-ptx>"
                  << " <sampling-file> <metrics-file> <register-file>"
                  << " <save-as-json> <json-output-dir> <sm-count>"
                  << " [kernel-filter-csv]\n";
        return 2;
    }

    // std::string filename_hpctoolkit_sass = argv[1];
    std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "register_spilling");
    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static register spilling result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_executable_sass, analysis_kind::REGISTER_SPILLING);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    std::string filename_registers = argv[6];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(filename_registers);

    int save_as_json = std::strcmp(argv[7], "true") == 0;
    std::string json_output_dir = argv[8];
    int sm_count = std::stoi(argv[9]);
    std::vector<std::string> kernel_filters;
    if (argc > 10)
    {
        kernel_filters = parse_kernel_filter_csv(argv[10]);
    }

    json result = merge_analysis_register_spill(static_result, pc_stall_map, metric_map, live_register_map, sm_count, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/register_spilling.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
