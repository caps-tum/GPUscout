/**
 * Merge analysis for using __restrict__
 * SASS analysis - restrict usage (instruction LDG) -> output code line number, flag if the register is unused later in the code and register pressure
 * PC Sampling analysis - pc stalls (instruction LDG) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> IMC miss stall
 *
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "../utilities/json.hpp"
#include "../utilities/helper.hpp"
#include "kernel_filter.hpp"
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

/// @brief Merge analysis (SASS, CUPTI, Metrics) for using restricted pointers
/// @param static_result Static restict result produced by parser_sass_use_restrict
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
/// @param live_register_map Currently used (or live) register count denoting register pressure
json merge_analysis_restrict(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, std::unordered_map<std::string, std::vector<live_registers>> live_register_map, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    const auto& result = static_result["result"];

    for (const auto& [krn_name, static_kernel_result] : result.items())
    {
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        // Copy the static GUI result so live-register fields can be appended.
        json kernel_result = static_kernel_result;
        auto& occurrences = kernel_result["occurrences"];

        std::cout << "--------------------- Use of __restrict__ analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        std::size_t restrict_recommendation_count = 0;

        for (auto& occurrence : occurrences)
        {
            const bool read_only_memory_used = occurrence["read_only_memory_used"].get<bool>();
            const int line_number = occurrence["line_number"].get<int>();
            const std::string register_name = occurrence["register"].get<std::string>();

            if (read_only_memory_used)
            {
                std::cout << "INFO  ::  Register " << register_name << ", in line number " << line_number << " of your code, is already using read-only cache" << std::endl;
            }
            else
            {
                std::cout << "INFO  ::  Register " << register_name << ", in line number " << line_number << " of your code, is not aliased anywhere in the kernel" << std::endl;
                std::cout << "WARNING  ::  You can benifit from using __restrict__ for register " << register_name << " at line number " << line_number << " of your code" << std::endl;
                restrict_recommendation_count++;
            }

            // Map kernel with the PC Stall map
            for (auto [k_pc, v_pc] : pc_stall_map)
            {
                if ((k_pc == krn_name)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                {
                    for (const auto &j : v_pc)
                    {
                        if ((line_number == j.line_number) && (get_register_from_line(j.sass_instruction) == register_name)) // analyze for the same line numbers in the code and same registers in SASS
                        {
                            // Print the number of current number of active registers
                            int pcOffset_to_search = j.pc_offset; // convert dec to hex
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
                                }
                            }

                            if (!read_only_memory_used)
                            {
                                print_stalls_percentage(j);
                            }
                            break; // once register matched/found, get out of the loop
                        }
                    }
                }
            }
        }

        if (restrict_recommendation_count == 0)
        {
            // std::cout << "INFO  ::  You can not benifit from using __restrict__ for any of the registers at the given line numbers in your code" << std::endl;
            std::cout << "INFO  ::  None of the registers, not already using read-only cache, can benifit from using __restrict__" << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "If using __restrict__ (read-only cache), check IMC miss: " << v_metric.metrics_list.smsp__warp_issue_stalled_imc_miss_per_warp_active << " % per warp active" << std::endl;
            }
        }

        final_result[krn_name] = std::move(kernel_result);
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
    if (argc != 9 && argc != 10)
    {
        std::cerr
            << "Usage: " << argv[0]
            << " <hpctoolkit-sass> <executable-sass> <executable-ptx>"
            << " <sampling-file> <metrics-file> <register-file>"
            << " <save-as-json> <json-output-dir> [kernel-filter-csv]\n";
        return 2;
    }

    std::string filename_hpctoolkit_sass = argv[1];
    // Load the reusable static result produced by parser_sass_use_restrict.
    const std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "use_restrict");
    
    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static use-restrict result: "
                  << static_result_file << std::endl;
        return 1;
    }
    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::RESTRICT_USE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    std::string filename_registers = argv[6];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(filename_registers);

    int save_as_json = std::strcmp(argv[7], "true") == 0;
    std::string json_output_dir = argv[8];
    std::vector<std::string> kernel_filters;
    if (argc > 9)
    {
        kernel_filters = parse_kernel_filter_csv(argv[9]);
    }

    json result = merge_analysis_restrict(static_result, pc_stall_map, metric_map, live_register_map, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/use_restrict.json");
        json_file << result.dump(4);
        json_file.close();
    }
}
