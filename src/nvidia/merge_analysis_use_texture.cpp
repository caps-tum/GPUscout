/**
 * Merge analysis for using Texture memory
 * SASS analysis - texture usage (instruction LDG/!LDG.E.CI/!LDG.E.CONSTANT) -> output code line number and spatial locality
 * PC Sampling analysis - pc stalls (instruction LDG) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> long scoreboard stall, Tex Throttle stall and data flow in texture memory
 *
 * @author Soumya Sen
 */

#include "parser_pcsampling.hpp"
#include "parser_metrics.hpp"
#include "kernel_filter.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

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


/// @brief Merge analysis (SASS, CUPTI, Metrics) for using texture memory instead of linear global memory
/// @param static_result Static use-texture result produced by parser_sass_use_texture
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
json merge_analysis_use_texture(const json& static_result, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, const std::vector<std::string> &kernel_filters)
{
    json final_result = json::object();
    const auto& result = static_result["result"];
    const auto& metadata = static_result["metadata"];

    for (const auto& [krn_name, krn_result] : result.items())
    {
        if (!kernel_matches_filter(krn_name, kernel_filters))
        {
            continue;
        }

        std::cout << "--------------------- Use texture memory analysis for kernel: " << krn_name << "   --------------------- " << std::endl;
        bool texture_recommend_flag = false;
        const auto& occurrences = krn_result["occurrences"];
        for (const auto& occurrence : occurrences)
        {
            if (metadata[krn_name]["texture_memory_used"].get<bool>())
            {
                std::cout << "INFO  ::  Use of texture memory detected in the kernel" << std::endl;
                break; // using break necessary, else code gets stuck in a loop
                // if break statement needs to be removed, add default values for the register_obj in the parser file
            }

           
            std::cout << "WARNING  ::  Use texture memory for register number (written-to): " << occurrence["written_register"].get<std::string>() << " at line number " << occurrence["line_number"].get<int>() << " of your code. The data is read from register number: " << occurrence["read_register"].get<std::string>() << std::endl;
            texture_recommend_flag = true;
            (occurrence["spatial_locality"].get<bool>()) ? std::cout << "Spatial locality found for the register data" << std::endl : std::cout << "No spatial locality found for the register data" << std::endl;

            // Map kernel with the PC Stall map
            for (auto [k_pc, v_pc] : pc_stall_map)
            {
                if ((k_pc == krn_name)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                {
                    for (const auto &j : v_pc)
                    {
                        if ((occurrence["line_number"].get<int>()== j.line_number) && (get_register_from_line(j.sass_instruction) == occurrence["written_register"].get<std::string>())) // analyze for the same line numbers in the code and same registers in SASS
                        {
                            print_stalls_percentage(j);
                            break;
                        }
                    }
                }
            }
        }

        if (!texture_recommend_flag)
        {
            std::cout << "INFO  ::  No global loads found in the kernel which can benefit from using Texture memory" << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == krn_name)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "INFO  ::  Check data flow in texture memory, if you modify your code to use textures" << std::endl;
                texture_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check texture memory flow)

                // copied use_texture_memory_analysis from stalls_static_analysis_relation() method
                std::cout << "If you are using texture memory, check Tex Throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active << " %" << std::endl;
                std::cout << "If you are using texture memory, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;
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

    // Static SASS result
    std::string filename_executable_sass = argv[2];
    const auto static_result_file = static_result_path(filename_executable_sass, "use_texture");
    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static texture result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::TEXTURE_USE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];
    std::vector<std::string> kernel_filters;
    if (argc > 8)
    {
        kernel_filters = parse_kernel_filter_csv(argv[8]);
    }

    json result = merge_analysis_use_texture(static_result, pc_stall_map, metric_map, kernel_filters);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/use_texture.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
