/**
 * PC Sampling analysis based on data read from CUPTI PC Sampling API
 * Parses the SASS file to connect the pcOffset with the PC sampling data collected
 *
 * @author Soumya Sen
 */

#ifndef PARSER_PCSAMPLING_HPP
#define PARSER_PCSAMPLING_HPP

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <vector>
#include <tuple>
#include <fstream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <memory>

/// @brief Kind of bottleneck analysis performed
enum analysis_kind
{
    ALL,
    REGISTER_SPILLING,
    VECTORIZED_LOAD,
    ATOMICS_GLOBAL,
    RESTRICT_USE,
    WARP_DIVERGENCE,
    TEXTURE_USE,
    SHARED_USE,
    DATATYPE_CONVERSION,
};

/// @brief Based on the type of bottleneck detection analysis, the relevant SASS instructions are returned
/// @param analysis_kind Kind of bottleneck analysis to perform
/// @param line SASS instruction line
/// @return true if the line contains the relevant SASS instruction, else false
bool sampling_type(analysis_kind analysis_kind, std::string line)
{
    if (analysis_kind == ALL)
    {
        return line.find("/*") != std::string::npos && line.find("*/") != std::string::npos;
    }
    if (analysis_kind == REGISTER_SPILLING)
    {
        return (line.find("STL ") != std::string::npos) || (line.find("LDL ") != std::string::npos);
    }
    if (analysis_kind == RESTRICT_USE)
    {
        return (line.find("LDG.") != std::string::npos) || (line.find("LDG.E.CI") != std::string::npos) || (line.find("LDG.E.CONSTANT.") != std::string::npos);
    }
    if (analysis_kind == VECTORIZED_LOAD)
    {
        // return (line.find("LDG.E ") != std::string::npos) || (line.find("LDG.E.CI ") != std::string::npos) || (line.find("LDG.E.U ") != std::string::npos) || (line.find("LDG.E.CONSTANT.SYS ") != std::string::npos) || (line.find("LDG.E.SYS ") != std::string::npos);
        return line.find("LDG.") != std::string::npos;
    }
    if (analysis_kind == ATOMICS_GLOBAL)
    {
        return (line.find("ATOMG.ADD") != std::string::npos) || (line.find("ATOMS.ADD") != std::string::npos) || (line.find("RED.E.ADD") != std::string::npos);
    }
    if (analysis_kind == WARP_DIVERGENCE)
    {
        return (line.find(" BRA ") != std::string::npos);
    }
    if (analysis_kind == TEXTURE_USE)
    {
        return (line.find("LDG.") != std::string::npos) || (line.find("LDG.E.CI") != std::string::npos) || (line.find("LDG.E.CONSTANT.") != std::string::npos);
    }
    if (analysis_kind == SHARED_USE)
    {
        return line.find("LDG.") != std::string::npos;
    }
    if (analysis_kind == DATATYPE_CONVERSION)
    {
        return (line.find("F2F") != std::string::npos) || (line.find("I2F") != std::string::npos) || (line.find("F2I") != std::string::npos);
    }

    return false;
}

/// @brief For every warp stall, an issued and not_issued reason is generated which is combined to a meaningful stall reason name
/// @param pc_stall_reason Warp stall reason from CUPTI
/// @return Meaningful stall reason name
std::string mapping_stall_reasons_to_names(const std::string &pc_stall_reason)
{
    std::string stall_name = "STALL UNKNOWN";

    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_barrier") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_barrier_not_issued"))
    {
        stall_name = "stalled_barrier";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_branch_resolving") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_branch_resolving_not_issued"))
    {
        stall_name = "stalled_branch";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_dispatch_stall") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_dispatch_stall_not_issued"))
    {
        stall_name = "stalled_dispatch";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_drain") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_drain_not_issued"))
    {
        stall_name = "stalled_drain";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_imc_miss") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_imc_miss_not_issued"))
    {
        stall_name = "stalled_imc_miss";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_lg_throttle") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_lg_throttle_not_issued"))
    {
        stall_name = "stalled_lg_throttle";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_long_scoreboard") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_long_scoreboard_not_issued"))
    {
        stall_name = "stalled_long_scoreboard";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_math_pipe_throttle") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_math_pipe_throttle_not_issued"))
    {
        stall_name = "stalled_math_pipe_throttle";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_membar") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_membar_not_issued"))
    {
        stall_name = "stalled_membar";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_mio_throttle") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_mio_throttle_not_issued"))
    {
        stall_name = "stalled_mio_throttle";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_misc") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_misc_not_issued"))
    {
        stall_name = "stalled_misc";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_no_instructions") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_no_instructions_not_issued"))
    {
        stall_name = "stalled_no_instructions";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_not_selected") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_not_selected_not_issued"))
    {
        stall_name = "stalled_not_selected";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_selected") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_selected_not_issued"))
    {
        stall_name = "stalled_selected";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_short_scoreboard") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_short_scoreboard_not_issued"))
    {
        stall_name = "stalled_short_scoreboard";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_sleeping") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_sleeping_not_issued"))
    {
        stall_name = "stalled_sleeping";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_tex_throttle") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_tex_throttle_not_issued"))
    {
        stall_name = "stalled_tex_throttle";
    }
    if ((pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_wait") || (pc_stall_reason == "smsp__pcsamp_warps_issue_stalled_wait_not_issued"))
    {
        stall_name = "stalled_wait";
    }

    return stall_name;
}

struct pc_issue_samples
{
    int pc_offset;
    int line_number;
    std::string sass_instruction;
    std::vector<std::pair<std::string, int>> stall_name_count_pair;
};

std::string get_pcoffset_from_sass(std::string line)
{
    //         /*00a0*/                   ISETP.GE.AND P1, PT, R2, c[0x0][0x168], PT ;         -> extract 00a0
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

std::string get_pcoffset_from_sampling(std::string line)
{
    //  pcOffset: 352       -> extract 352
    std::string remove_chars = "pcOffset: ";
    std::string::size_type i = line.find(remove_chars);
    if (i != std::string::npos)
    {
        line.erase(i, remove_chars.length());
    }
    return line;
}

std::string get_kernelname_from_sampling(std::string line)
{
    //  functionName: _Z6HistSMPiiPfi       -> extract _Z6HistSMPiiPfi
    std::string remove_chars = "functionName: ";
    std::string::size_type i = line.find(remove_chars);
    if (i != std::string::npos)
    {
        line.erase(i, remove_chars.length());
    }
    return line;
}

int get_stallcount_from_sampling(std::string line)
{
    //  stallReasonCount: 2       -> extract 2
    std::string remove_chars = "stallReasonCount: ";
    std::string::size_type i = line.find(remove_chars);
    if (i != std::string::npos)
    {
        line.erase(i, remove_chars.length());
    }
    return std::stoi(line);
}

std::pair<std::string, int> get_stall_reason_from_sampling(std::string line)
{
    // smsp__pcsamp_warps_issue_stalled_mio_throttle: 1     -> extract pair smsp__pcsamp_warps_issue_stalled_mio_throttle,1

    line.erase(std::remove(line.begin(), line.end(), ' '), line.end()); // remove spaces
    std::string stall_name, substr, last_string;
    std::istringstream ss(line);
    std::getline(ss, stall_name, ':');
    while (std::getline(ss, substr, ':'))
    {
        last_string = substr;
    }

    return std::make_pair(stall_name, stoi(last_string));
}

/// @brief Get the warp stall reasons and their corresponding stall values by connecting the pcOffset with the PC sampling data
/// @param filename_sampling PC sampling data file
/// @param filename_sass SASS instruction file
/// @param analysis_input Kind of bottleneck analysis performed
/// @return Vector of stall reasons and stall values of the relevant SASS instructions for each kernel
std::unordered_map<std::string, std::vector<pc_issue_samples>> get_warp_stalls(const std::string &filename_sampling, const std::string &filename_sass, analysis_kind analysis_input)
{

    // Get the pc sampling stall data from the file first
    std::vector<std::vector<std::string>> data;

    std::fstream file_sampling(filename_sampling, std::ios::in);
    if (file_sampling.is_open())
    {
        std::vector<std::string> row;
        std::string line, word;

        // This will skip the first two lines
        // https://stackoverflow.com/questions/33250380/c-skip-first-line-of-csv-file
        std::getline(file_sampling, line);
        std::getline(file_sampling, line);

        // Read from the next lines (contains the data)
        while (std::getline(file_sampling, line))
        {
            row.clear();

            std::stringstream str(line);

            while (std::getline(str, word, ','))
            {
                row.push_back(word);
            }
            data.push_back(row);
        }
    }
    else
        std::cout << "Could not open the file: " << filename_sampling << std::endl;

    // Parsing the sass file to connect the pcoffset with the above read pc sampling data
    // Log file content looks like:
    // functionName: _Z6HistSMPiiPfi, functionIndex: 11, pcOffset: 320, lineNumber:0, fileName: ERROR_NO_CUBIN, dirName: , stallReasonCount: 2, smsp__pcsamp_warps_issue_stalled_mio_throttle: 1, smsp__pcsamp_warps_issue_stalled_wait: 1

    std::string line;
    std::fstream file_sass(filename_sass, std::ios::in);
    pc_issue_samples pc_obj;
    std::unordered_map<std::string, std::vector<pc_issue_samples>> counter_map;
    std::string kernel_name;
    int code_line_number;
    std::vector<pc_issue_samples> pc_samp_vec;
    std::vector<std::pair<std::string, int>> stalls_vec;

    if (file_sass.is_open())
    {
        while (std::getline(file_sass, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                pc_obj.line_number = 0;
                pc_obj.pc_offset = 0;
                pc_obj.sass_instruction = "";
                pc_obj.stall_name_count_pair.clear();
                pc_samp_vec.clear();

                // https://cplusplus.com/reference/string/string/erase/     - erase part of a string
                line.erase(line.begin(), line.begin() + 16); // erase the first 16 character of the name of the kernel
                line.erase(line.end() - 15, line.end());     // erase the last 15 character of the name of the kernel
                kernel_name = line;
                // std::cout << kernel_name << std::endl;
            }

            if (line.find(" line ") != std::string::npos)
            {
                code_line_number = std::stoi(line.substr(line.find("line ") + 5)); // saving the current line number
            }

            if (sampling_type(analysis_input, line))
            {
                stalls_vec.clear();

                std::string pcoffset_sass_hex = get_pcoffset_from_sass(line);
                unsigned long pcoffset_sass_dec = std::stoul(pcoffset_sass_hex, nullptr, 16); // hex to dec conversion using std::stoul

                // Traverse through the pc sampling file to match the pcoffset
                for (auto i : data)
                {
                    auto pcoffset_sampling = get_pcoffset_from_sampling(i[2]);
                    if ((kernel_name == get_kernelname_from_sampling(i[0])) && (pcoffset_sass_dec == std::stoul(pcoffset_sampling))) // Note: sampling file contains data for both the kernels together
                    {
                        pc_obj.line_number = code_line_number;
                        pc_obj.pc_offset = pcoffset_sass_dec;
                        pc_obj.sass_instruction = line; // note change this entire line to only give the command
                        for (auto j = 0; j < get_stallcount_from_sampling(i[6]); j++)
                        {
                            std::pair<std::string, int> stall_count_pair = get_stall_reason_from_sampling(i[7 + j]);
                            stalls_vec.push_back(stall_count_pair);
                        }
                        pc_obj.stall_name_count_pair = stalls_vec;
                        pc_samp_vec.push_back(pc_obj);
                    }
                }
            }

            counter_map[kernel_name] = pc_samp_vec;
        }
    }
    else
        std::cout << "Could not open the file: " << filename_sass << std::endl;

    // for (const auto& i : counter_map["_Z3dotPiS_S_"])
    // {
    //     // Printing the stall with most counts
    //     // std::cout << "SASS Line: " << i.sass_instruction << " at code line number: " << i.line_number << std::endl;
    //     // auto most_occur_stall = *std::max_element(i.stall_name_count_pair.begin(), i.stall_name_count_pair.end(), [](const auto& lhs, const auto& rhs){return lhs.second < rhs.second;});
    //     // std::cout << "Stall #1: " << most_occur_stall.first << " with number of issues: " << most_occur_stall.second << std::endl;

    //     // Printing the stall with percentage of samples
    //     std::cout << "SASS Line: " << i.sass_instruction << " at code line number: " << i.line_number << std::endl;
    //     auto total_samples = 0;
    //     for (const auto& j : i.stall_name_count_pair)
    //     {
    //         total_samples += j.second;
    //     }
    //     std::unordered_map<std::string, int> map_stall_name_count;
    //     for (const auto& j : i.stall_name_count_pair)
    //     {
    //         // std::cout << "Stall: " << mapping_stall_reasons_to_names(j.first) << ", accounting for: " << (1.0*j.second/total_samples)*100 << " %" << std::endl;
    //         map_stall_name_count[mapping_stall_reasons_to_names(j.first)] += j.second;
    //     }
    //     for (const auto& [k,v] : map_stall_name_count)
    //     {
    //         std::cout << "Stall: " << k << ", accounting for: " << (100.0*v)/total_samples<< " % of the stalls for this SASS" << std::endl;
    //     }

    // }

    return counter_map;
}

#endif // PARSER_PCSAMPLING_HPP
