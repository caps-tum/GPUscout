/**
 * Merge analysis for using Texture memory
 * SASS analysis - texture usage (instruction LDG/!LDG.E.CI/!LDG.E.CONSTANT) -> output code line number and spatial locality
 * PC Sampling analysis - pc stalls (instruction LDG) -> output stall reasons and percentage of stall
 * Metric analysis - get metrics for entire kernel -> long scoreboard stall, Tex Throttle stall and data flow in texture memory
 *
 * @author Soumya Sen
 */

#include "parser_sass_use_texture.hpp"
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

/// @brief Checks if the load addresses are in spatial locality
/// @param register_read set of address offsets for a given base register address
/// @return True if spatial locality found, false otherwise
bool check_spatial_locality(const register_used &register_read)
{
    // Find the difference between the unrolls
    // check https://www.geeksforgeeks.org/absolute-difference-of-all-pairwise-consecutive-elements-in-a-set/
    std::set<unsigned long>::iterator it1 = register_read.load_from_register_unrolls.begin();
    std::set<unsigned long>::iterator it2 = register_read.load_from_register_unrolls.begin();
    bool spatial_locality_flag = true;
    while (1)
    {
        it2++; // 2nd iterator at position 1
        if (it2 == register_read.load_from_register_unrolls.end())
        {
            break;
        }
        if ((abs(*it2 - *it1) != 4) && (abs(*it2 - *it1) != 8) && (abs(*it2 - *it1) != 16))
        {
            // if the unrolls are not at a difference of 4 (for LDG) or 8 (for LDG.64) or 16 (for LDG.128), spatial locality not there
            spatial_locality_flag = false;
            break;
        }
    }

    return spatial_locality_flag;
}

/// @brief Merge analysis (SASS, CUPTI, Metrics) for using texture memory instead of linear global memory
/// @param texture_analysis_map Includes read-only register data with spatial locality flag
/// @param pc_stall_map CUPTI warp stalls
/// @param metric_map Metric analysis
void merge_analysis_use_texture(std::unordered_map<std::string, std::vector<register_used>> texture_analysis_map, std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map, std::unordered_map<std::string, kernel_metrics> metric_map, int save_as_json, std::string json_output_dir)
{
    json result;

    for (auto [k_sass, v_sass] : texture_analysis_map)
    {
        json kernel_result = {
            {"texture_memory_used", false},
            {"occurrences", json::array()}
        };

        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Use texture memory analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        bool texture_recommend_flag = false;
        for (auto index_sass : v_sass)
        {
            json line_result;

            if (index_sass.is_texture_load)
            {
                std::cout << "INFO  ::  Use of texture memory detected in the kernel" << std::endl;
                kernel_result["texture_memory_used"] = true;
                break; // using break necessary, else code gets stuck in a loop
                // if break statement needs to be removed, add default values for the register_obj in the parser file
            }

            // Find the global linear memory load info to recommend use of texture from the SASS analysis
            bool spatial_locality_flag = check_spatial_locality(index_sass);
            bool multiple_reads_register_flag = false;

            // For flag NOT_USED (=0) and spatial locality (above algo check) use texture memory
            if ((spatial_locality_flag) && (index_sass.flag == NOT_USED)) // spatial locality present
            {
                for (auto j : index_sass.load_from_register_unrolls)
                {
                    // should we apply this filter of locality distance > 0?
                    // The current notion is: we consider spatial locality if a register is read followed by another read from the same register at an offset
                    // e.g. read R8 and then read R8+0x10
                    // if this second read is not present, there might not be any use of texture memory for that case. Global memory should be sufficient then
                    multiple_reads_register_flag = (j == 0) ? false : true;
                }
                std::cout << "WARNING  ::  Use texture memory for register number (written-to): " << index_sass.write_to_register_number << " at line number " << index_sass.line_number << " of your code. The data is read from register number: " << index_sass.load_from_register << std::endl;
                texture_recommend_flag = true;
                (multiple_reads_register_flag) ? std::cout << "Spatial locality found for the register data" << std::endl : std::cout << "No spatial locality found for the register data" << std::endl;
                line_result = {
                    {"line_number", index_sass.line_number},
                    {"written_register", index_sass.write_to_register_number},
                    {"read_register", index_sass.load_from_register},
                    {"spatial_locality", multiple_reads_register_flag}
                };

                // Map kernel with the PC Stall map
                for (auto [k_pc, v_pc] : pc_stall_map)
                {
                    if ((k_pc == k_sass)) // analyze for the same kernel (sass analysis and pc sampling analysis)
                    {
                        for (const auto &j : v_pc)
                        {
                            if ((index_sass.line_number == j.line_number) && (get_register_from_line(j.sass_instruction) == index_sass.write_to_register_number)) // analyze for the same line numbers in the code and same registers in SASS
                            {
                                json stalls = print_stalls_percentage(j);
                                line_result["stalls"] = stalls;
                                break;
                            }
                        }
                    }
                }
            }

            if (!line_result.is_null())
                kernel_result["occurrences"].push_back(line_result);
        }

        if (!texture_recommend_flag)
        {
            std::cout << "INFO  ::  No global loads found in the kernel which can benifit from using Texture memory" << std::endl;
        }

        // Map kernel with metrics collected
        for (auto [k_metric, v_metric] : metric_map)
        {
            if ((k_metric == k_sass)) // analyze for the same kernel (sass analysis and metric analysis)
            {
                std::cout << "INFO  ::  Check data flow in texture memory, if you modify your code to use textures" << std::endl;
                json tex_data_memory_metrics = texture_data_memory_flow(metric_map[k_metric]); // show the memory flow (to check texture memory flow)

                // copied use_texture_memory_analysis from stalls_static_analysis_relation() method
                std::cout << "If you are using texture memory, check Tex Throttle: " << v_metric.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active << " %" << std::endl;
                std::cout << "If you are using texture memory, check Long Scoreboard: " << v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;

                kernel_result["metrics"] = {
                    {"texture_data_memory_flow", tex_data_memory_metrics},
                    {"smsp__warp_issue_stalled_long_scoreboard_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active},
                    {"smsp__warp_issue_stalled_tex_throttle_per_warp_active", v_metric.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active},
                };
            }
        }

        result[k_sass] = kernel_result;
    }

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_output_dir + "/use_texture.json");
        json_file << result.dump(4);
        json_file.close();
    }
}

int main(int argc, char **argv)
{
    std::string filename_hpctoolkit_sass = argv[1];
    std::unordered_map<std::string, std::vector<register_used>> texture_analysis_map = use_texture_analysis(filename_hpctoolkit_sass);

    std::string filename_sampling = argv[4];
    std::unordered_map<std::string, std::vector<pc_issue_samples>> pc_stall_map = get_warp_stalls(filename_sampling, filename_hpctoolkit_sass, analysis_kind::TEXTURE_USE);

    std::string filename_metrics = argv[5];
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(filename_metrics);

    int save_as_json = std::strcmp(argv[6], "true") == 0;
    std::string json_output_dir = argv[7];

    merge_analysis_use_texture(texture_analysis_map, pc_stall_map, metric_map, save_as_json, json_output_dir);

    return 0;
}
