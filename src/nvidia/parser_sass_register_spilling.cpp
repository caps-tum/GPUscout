/**
 * SASS code analysis to detect register spilling to local memory
 *
 * @author Soumya Sen
 */

#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <vector>
#include <tuple>
#include <set>
#include <fstream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <memory>

using json = nlohmann::json;

/// @brief operations to the local memory can be a load or a store
enum lmem_operation_type
{
    LOAD,
    STORE
};
static const char *lmem_operation_type_string[] =
    {
        "LOAD",
        "STORE"}; // https://linuxhint.com/cpp-ways-to-convert-enum-to-string/

/// @brief Register information loading/storing data to the local memory
struct local_memory_counter
{
    std::string register_number;
    int line_number;
    lmem_operation_type op_type;
    std::string pcOffset;
};

/// @brief Track last SASS instruction of the spilled register
struct track_register_instruction
{
    std::string register_number;
    std::string last_instruction;
    std::string last_pcOffset;
    int last_line_number;
    bool flag_reached;
};

std::string lmem_register(const std::string &line)
{
    //         /*03c0*/                   STL [R2], R5 ;        -> extract R5
    std::string substr, last_string;

    std::istringstream ss(line);
    while (std::getline(ss, substr, ','))
    {
        last_string = substr;
    }

    std::string remove_chars = " ;{}[]";
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());

    return substr;
}

std::string get_lmem_base_register(const std::string &line)
{
    //         /*0340*/                   LDG.E.128.SYS R16, [R20+-0x10] ;      -> extract R20
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

    std::string register_base;
    std::istringstream ss3(substr);
    std::getline(ss3, register_base, '+');

    return register_base;
}

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

std::string get_instruction_from_line(std::string line)
{
    //         /*03a0*/                   IMAD.IADD R5, R3, 0x1, R5 ;       -> extract IMAD.IADD
    line.erase(line.begin(), line.begin() + 35); // erase the first 35 character of the name of the kernel

    std::string substr, last_string;
    std::istringstream ss(line);
    std::getline(ss, substr, ' ');
    return substr;
}

std::string get_pcoffset_sass(std::string line)
{
    //         /*00a0*/                   ISETP.GE.AND P1, PT, R2, c[0x0][0x168], PT ;         -> extract 00a0
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

/// @brief SASS analysis if register has spilled data to local memory
/// @param filename Disassembled SASS file
/// @return Tuple of two maps - first map includes local memory load/store data for the kernel, second includes last instruction data for the spilled register
std::tuple<std::unordered_map<std::string, std::vector<local_memory_counter>>, std::unordered_map<std::string, std::vector<track_register_instruction>>> register_spilling_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    local_memory_counter counter_obj;
    std::unordered_map<std::string, std::vector<local_memory_counter>> counter_map;
    std::string kernel_name;
    int code_line_number;
    std::vector<local_memory_counter> lmem_vec;

    track_register_instruction last_reg_obj;
    std::unordered_map<std::string, std::vector<track_register_instruction>> track_register_map;
    std::vector<track_register_instruction> last_reg_vec;
    std::string current_register;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                counter_obj.line_number = 0;
                counter_obj.register_number = "";

                last_reg_obj.register_number = "";
                last_reg_obj.last_instruction = "";
                last_reg_obj.last_pcOffset = "";
                last_reg_obj.last_line_number = 0;
                last_reg_obj.flag_reached = false;

                lmem_vec.clear();
                last_reg_vec.clear();

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

            if ((line.find("STL ") != std::string::npos) || (line.find("LDL ") != std::string::npos))
            {
                counter_obj.op_type = ((line.find("STL ") != std::string::npos)) ? STORE : LOAD;
                counter_obj.line_number = code_line_number;
                counter_obj.register_number = ((line.find("+0x") != std::string::npos) || (line.find("+-0x") != std::string::npos)) ? get_lmem_base_register(line) : lmem_register(line);
                counter_obj.pcOffset = get_pcoffset_sass(line);
                lmem_vec.push_back(counter_obj);

                // track_register_map[counter_obj.register_number].flag_reached = true;
                std::vector<track_register_instruction>::iterator last_reg_match = std::find_if(last_reg_vec.begin(), last_reg_vec.end(), [&](const track_register_instruction &i)
                                                                                                { return i.register_number == counter_obj.register_number; });
                if (last_reg_match != last_reg_vec.end())
                {
                    last_reg_match->flag_reached = true;
                }
            }

            if (line.find("MAD") != std::string::npos || line.find("ADD") != std::string::npos ||
                line.find("MUL") != std::string::npos || line.find("FMA") != std::string::npos ||
                line.find("MUFU") != std::string::npos || line.find("RRO") != std::string::npos)
            {
                // to track the last operation of the register
                current_register = get_register_from_line(line);
                // if (!track_register_map[current_register].flag_reached)
                // {
                //     last_reg_obj.last_line_number = code_line_number;
                //     last_reg_obj.last_instruction = get_instruction_from_line(line);
                //     track_register_map[current_register] = last_reg_obj;
                // }
                std::vector<track_register_instruction>::iterator last_reg_match = std::find_if(last_reg_vec.begin(), last_reg_vec.end(), [&](const track_register_instruction &i)
                                                                                                { return i.register_number == current_register; });
                if (last_reg_match != last_reg_vec.end())
                {
                    if (last_reg_match->flag_reached == false)
                    {
                        last_reg_match->last_line_number = code_line_number;
                        last_reg_match->last_instruction = get_instruction_from_line(line);
                        last_reg_match->last_pcOffset = get_pcoffset_sass(line);
                    }
                }
                else
                {
                    last_reg_obj.register_number = current_register;
                    last_reg_obj.last_line_number = code_line_number;
                    last_reg_obj.last_instruction = get_instruction_from_line(line);
                    last_reg_obj.last_pcOffset = get_pcoffset_sass(line);
                    last_reg_obj.flag_reached = false;
                    last_reg_vec.push_back(last_reg_obj);
                }
            }

            counter_map[kernel_name] = lmem_vec;
            track_register_map[kernel_name] = last_reg_vec;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // for (const auto& i : counter_map["_Z3dotPiS_S_"])
    // {
    //     std::cout << "Kernel name: _Z3dotPiS_S_, " << "Register spill: " << lmem_operation_type_string[i.op_type] << " by register: " << i.register_number << " at code line number: " << i.line_number << std::endl;
    //     std::cout << "The previous instruction of register: " << i.register_number << " was: " << track_register_map[i.register_number].last_instruction << " at code line number: " << track_register_map[i.register_number].last_line_number << std::endl;
    // }

    return std::make_tuple(counter_map, track_register_map);
}

/*!
 * Build the reusable static result for register-spilling analysis.
 *
 * candidate_kernels: Kernels for which at least one register spill was detected.
 * result: Fields belonging to the final register_spilling.json consumed by GUI.
 * metadata: Internal values required for terminal output. These values are not copied to the final GUI JSON.
 * @param spilling_analysis_map Includes register load/store to local memory data
 * @param track_register_map Includes previous arithmetic SASS instruction of the register
 */
json build_static_register_spilling_result(const std::unordered_map<std::string, std::vector<local_memory_counter>>& spilling_analysis_map, const std::unordered_map<std::string, std::vector<track_register_instruction>>& track_register_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()},
        {"metadata", json::object()}
    };

    for (const auto& [k_sass, v_sass] : spilling_analysis_map)
    {
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass.empty())
        {
            continue;
        }

        json kernel_result = {
            {"occurrences", json::array()}
        };

        const auto track_register_it = track_register_map.find(k_sass);

        for (const auto& index_sass : v_sass)
        {
            json line_result = {
                {"severity", "WARNING"},
                {"line_number", index_sass.line_number},
                {"register", index_sass.register_number},
                {"pc_offset", index_sass.pcOffset},
                {"operation", lmem_operation_type_string[index_sass.op_type]}
            };

            if (track_register_it != track_register_map.end())
            {
                for (const auto& last_reg : track_register_it->second)
                {
                    if (index_sass.register_number == last_reg.register_number)
                    {
                        line_result["previous_compute_instruction"] = {
                            {"instruction", last_reg.last_instruction},
                            {"line_number", last_reg.last_line_number},
                            {"pc_offset", last_reg.last_pcOffset}
                        };
                    }
                }
            }

            kernel_result["occurrences"].push_back(std::move(line_result));
        }

        static_result["result"][k_sass] = std::move(kernel_result);
        static_result["metadata"][k_sass] = {
            {"spill_count", v_sass.size()}
        };

        if (v_sass.empty())
            continue;

        static_result["candidate_kernels"].push_back({
            {"name", k_sass},
            {"demangled", get_demangled_kernel(k_sass)}
        });
    }

    return static_result;
}

bool has_register_spilling_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <sass-file> <ptx-file>\n";
        return 2;
    }

    /*! Static detection mode:
     *
     *  1. Parse SASS using the existing detection algorithm.
     *  2. Build and save the reusable static JSON.
     *  3. Return whether at least one candidate kernel was detected.
     *
     *  exit 0 -> register spilling detected
     *  exit 1 -> no register spilling detected
     *  exit 2 -> invalid arguments or static-result write failure
     */
    const std::string assembly = argv[1];
    const auto sass_spilling_tuple = register_spilling_analysis(assembly);
    const auto& spilling_analysis_map = std::get<0>(sass_spilling_tuple);
    const auto& track_register_map = std::get<1>(sass_spilling_tuple);
    const json static_result = build_static_register_spilling_result(spilling_analysis_map, track_register_map);

    const auto static_result_file = static_result_path(assembly, "register_spilling");

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static register spilling result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_register_spilling_candidate(static_result) ? 0 : 1;
}

