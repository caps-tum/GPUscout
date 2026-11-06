/**
 * Merge analysis for using vectorized load
 * SASS analysis - vectorized load (instruction LDG ) -> output code line number, no of unrolls of a register and register pressure
 * PC Sampling analysis - pc stalls (instruction LDG ) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> Long scoreboard, occupancy achieved
 *
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "kernel_filter.hpp"
#include "../utilities/json.hpp"
#include "../utilities/helper.hpp"

using json = nlohmann::json;

/// @brief Load types can be 32- 64- or 128-bit width
enum load_type
{
    VEC_32,
    VEC_64,
    VEC_128,
};

/// @brief Reads the global load address and splits into the base register and unrolled value, where [R2+0x10] denotes R2 as base register and 10 as the unrolled value
/// @param line SASS instruction line
/// @return Pair with the base register and unrolled values. For no unrolled values, returns 0 for the second pair element
std::pair<std::string, unsigned long> read_register_pair(const std::string &line)
{
    std::string substr, last_string, last_string_clean;

    std::istringstream ss(line);
    while (std::getline(ss, substr, ','))
    {
        last_string = substr;
    }

    std::istringstream ss2(last_string);
    std::getline(ss2, substr, ' ');
    std::getline(ss2, substr, ' ');

    std::string remove_chars = "[]-";
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());

    std::string register_base, register_unroll;
    std::istringstream ss3(substr);
    std::getline(ss3, register_base, '+');
    std::getline(ss3, register_unroll, 'x');
    std::getline(ss3, register_unroll, 'x');

    /*
    Need to put in try-catch block since the register unroll might not always be numbers
    For example:    (18a0*) LDG.E.SYS R34, [R2.64+UR4] ;
    */
    try
    {
        std::stoul(register_unroll, nullptr, 16);
    }
    catch (const std::invalid_argument &e)
    {
        register_unroll = "";
        // std::cerr << e.what() << std::endl;
    }

    std::pair<std::string, unsigned long> register_pair = (register_unroll != "") ? std::make_pair(register_base, std::stoul(register_unroll, nullptr, 16)) : std::make_pair(register_base, (ulong)0);
    return register_pair;
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
    std::cout << "Stalls detected with % of occurence for the SASS instruction" << std::endl;
    for (const auto &[k, v] : map_stall_name_count)
    {
        std::cout << k << " (" << (100.0 * v) / total_samples << " %)" << std::endl;
    }
}

/// @brief Merge analysis (SASS, CUPTI, Metrics) for using vectorized load
/// @param static_result Static vectorization result produced by parser_sass_vectorization
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
/// @param live_register_map Currently used (or live) register count denoting register pressure
json merge_analysis_vectorize(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, std::unordered_map<std::string, std::vector<live_registers>> live_register_map, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    const auto& result = static_result["result"];

    for (const auto& [krn_name, static_kernel_result] : result.items())
    {
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        json kernel_result = static_kernel_result;
        auto& occurrences = kernel_result["occurrences"];

        std::cout << "--------------------- Vectorized load analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        std::cout << "WARNING   ::  Total number of non-vectorized global load SASS instructions for this kernel: " << kernel_result["total"].get<int>() << std::endl;

        for (auto& occurrence : occurrences)
        {
            const std::string severity = occurrence["severity"].get<std::string>();
            const int line_number = occurrence["line_number"].get<int>();
            const std::string register_name = occurrence["register"].get<std::string>();

            if (severity == "WARNING")
            {
                std::cout << "WARNING  ::  Use vectorized load for register " << register_name << ", in line number " << line_number << " of your code" << std::endl;
                std::cout << "Register " << register_name << " in line number " << line_number << " of your code has " << occurrence["adjacent_memory_accesses"].get<std::size_t>() << " adjacent memory accesses" << std::endl;
            }
            else
            {
                const int reg_load_type = occurrence["register_load_type"].get<int>();
                if (reg_load_type == VEC_64)
                {
                    std::cout << "INFO  ::  Register " << register_name << ", in line number " << line_number << " of your code, is already using 64-bit width vectorized load" << std::endl;
                }
                else if (reg_load_type == VEC_128)
                {
                    std::cout << "INFO  ::  Register " << register_name << ", in line number " << line_number << " of your code, is already using 128-bit width vectorized load" << std::endl;
                }
                else
                {
                    std::cout << "INFO  ::  Using vectorized load for register " << register_name << ", in line number " << line_number << " of your code, might not boost performance" << std::endl;
                }
                    
            }

            // Map kernel with the PC Stall map
            for (auto [k_pc, v_pc] : pc_stall_map)
            {
                if ((k_pc == krn_name)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                {
                    for (const auto &j : v_pc)
                    {
                        if ((line_number == j.line_number) && (register_name == read_register_pair(j.sass_instruction).first)) // analyze for the same line numbers in the code and same registers in SASS
                        {
                            /*
                            Example: Register R12 in line number 28 in your code (for example) has 1 unrolls done by the compiler
                            This line has a SASS instruction:    (06f0) LDG.E.SYS R15, [R12+-0x4] ;
                            This SASSS instruction has no corresponding PC sampling stalls
                            */

                            // Print the number of current number of active registers
                            const int reg_load_type = occurrence["register_load_type"].get<int>();
                            if ((reg_load_type == VEC_64) || (reg_load_type == VEC_128) || (reg_load_type == VEC_32))
                            {
                                int pcOffset_to_search = j.pc_offset;
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
                            }

                            // Note: Only printing PC stalls if there are unrolls present in the SASS for the register
                            if (severity == "WARNING")
                            {
                                print_stalls_percentage(j);
                            }
                            break; // once register matched/found, get out of the loop
                        }
                    }
                    

                }
            }
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "If you are using non-vectorized load/store, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " % per warp active" << std::endl;
                std::cout << "INFO  ::  Using vectorized load increases the register pressure and hence might affect occupancy. Occupancy achieved: " << v_metric.metrics_list.sm__warps_active << " %" << std::endl;
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
    // Load the reusable static result produced by parser_sass_vectorized.
    const std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "vectorization");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static vectorization result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::VECTORIZED_LOAD);

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

    json result = merge_analysis_vectorize(static_result, pc_stall_map, metric_map, live_register_map, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/vectorization.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
