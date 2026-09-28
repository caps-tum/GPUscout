/**
 * SASS code analysis to detect use of vectorized load
 *
 * @author Soumya Sen
*/


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

/// @brief Counts total number of global load instructions
struct load_counter
{
    int global_load_count;
};

struct register_data
{
    int line_number;
    std::string pcOffset;
    std::string base;
    std::vector<unsigned long> unrolls;
    std::vector<std::string> unroll_pc_offsets;
    load_type reg_load_type;
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

std::string get_pcoffset_sass(std::string line)
{
    //         /*00a0*/                   ISETP.GE.AND P1, PT, R2, c[0x0][0x168], PT ;         -> extract 00a0
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

/// @brief SASS analysis if vectorized load can be used
/// @param filename Disassembled SASS file
/// @return Tuple of two maps - first map includes total global load counts for the kernel, second includes register data for global loads
std::tuple<std::unordered_map<std::string, load_counter>, std::unordered_map<std::string, std::vector<register_data>>> vectorized_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    load_counter counter_obj;
    std::unordered_map<std::string, load_counter> counter_map;
    std::vector<register_data> register_vec;
    std::unordered_map<std::string, std::vector<register_data>> register_map;

    std::string k_sass;

    int code_line_number;
    // register_data register_obj;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                counter_obj.global_load_count = 0;
                register_vec.clear();

                // https://cplusplus.com/reference/string/string/erase/     - erase part of a string
                line.erase(line.begin(), line.begin() + 16); // erase the first 16 character of the name of the kernel
                line.erase(line.end() - 15, line.end());     // erase the last 15 character of the name of the kernel
                k_sass = line;
                // std::cout << k_sass << std::endl;
            }

            if (line.find(" line ") != std::string::npos)
            {
                code_line_number = std::stoi(line.substr(line.find("line ") + 5)); // saving the current line number
            }

            // looking for not vectorized global load
            // if ((line.find("LDG.E ") != std::string::npos) ||
            //     (line.find("LDG.E.CI ") != std::string::npos) ||
            //     (line.find("LDG.E.U ") != std::string::npos) ||
            //     (line.find("LDG.E.CONSTANT.SYS ") != std::string::npos) ||
            //     (line.find("LDG.E.SYS ") != std::string::npos) )
            if (line.find("LDG.") != std::string::npos)
            {
                counter_obj.global_load_count++;

                std::pair<std::string, unsigned long> register_pair = read_register_pair(line);

                // Check if the line is already present (i.e. did LDG already)
                std::vector<register_data>::iterator line_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_data &i)
                                                                               { return code_line_number == i.line_number; });
                std::vector<register_data>::iterator base_match = std::find_if(register_vec.begin(), register_vec.end(), [&](const register_data &i)
                                                                               { return (register_pair.first == i.base) && (code_line_number == i.line_number); });
                if (line_match == register_vec.end()) // line not present
                {
                    register_data register_obj;

                    register_obj.reg_load_type = VEC_32;
                    if (line.find(".64") != std::string::npos)
                    {
                        register_obj.reg_load_type = VEC_64;
                    }
                    if (line.find(".128") != std::string::npos)
                    {
                        register_obj.reg_load_type = VEC_128;
                    }
                    register_obj.line_number = code_line_number;
                    register_obj.pcOffset = get_pcoffset_sass(line);
                    register_obj.base = register_pair.first;
                    register_obj.unrolls.push_back(register_pair.second);
                    register_obj.unroll_pc_offsets.push_back(get_pcoffset_sass(line));
                    register_vec.push_back(register_obj);
                }
                else // line present
                {
                    // Check if the base (register) is present
                    if (base_match == register_vec.end()) // base not present
                    {
                        register_data register_obj;

                        register_obj.reg_load_type = VEC_32;
                        if (line.find(".64") != std::string::npos)
                        {
                            register_obj.reg_load_type = VEC_64;
                        }
                        if (line.find(".128") != std::string::npos)
                        {
                            register_obj.reg_load_type = VEC_128;
                        }
                        register_obj.line_number = code_line_number;
                        register_obj.pcOffset = get_pcoffset_sass(line);
                        register_obj.base = register_pair.first;
                        register_obj.unrolls.push_back(register_pair.second);
                        register_obj.unroll_pc_offsets.push_back(get_pcoffset_sass(line));
                        register_vec.push_back(register_obj);
                    }
                    else // line and base present
                    {
                        // Add the unroll part
                        base_match->unrolls.push_back(register_pair.second);
                        base_match->unroll_pc_offsets.push_back(get_pcoffset_sass(line));
                    }
                }
            }

            counter_map[k_sass] = counter_obj;
            register_map[k_sass] = register_vec;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // // To suggest vectorization: https://stackoverflow.com/questions/69464386/is-there-a-way-to-load-128-bits-from-memory-directly-to-registers
    // // If there are, say, 4 seperate LDG.E (for a single line), like LDG.E.SYS R13, [UR4]; , LDG.E.SYS R17, [UR4+0x18] ; , LDG.E.SYS R21, [UR4+0x30] ; , LDG.E.SYS R21, [UR4+0x48] ;
    // // then suggest to use 1 LDG.E.128 vectorized load for that line (it then becomes LDG.E.128.SYS R20, [UR6] ;)

    return std::make_tuple(counter_map, register_map);
}

/*!
 * Build the reusable static result for vectorization analysis.
 *
 * candidate_kernels: Kernels with at least one non-vectorized 32-bit load that accesses adjacent memory.
 * result: GUI-facing data for every parsed kernel, including kernels with no vectorization recommendation.
 * @param vectorize_analysis_map Total global load count
 * @param register_map Includes base register and unrolled values data
 */
json build_static_vectorization_result(const std::unordered_map<std::string, load_counter>& vectorize_analysis_map, const std::unordered_map<std::string, std::vector<register_data>>& register_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()}
    };

    for (const auto& [k_sass, v_sass] : vectorize_analysis_map)
    {   
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass.empty())
        {
            continue;
        }

        json kernel_result = {
            {"total", v_sass.global_load_count},
            {"occurrences", json::array()}
        };
        bool is_candidate = false;

        const auto register_it = register_map.find(k_sass);
        if (register_it != register_map.end())
        {
            for (const auto& index_sass : register_it->second)
            {
                const auto zero_count = std::count(index_sass.unrolls.begin(), index_sass.unrolls.end(), 0UL);
                const auto adjacent_count = index_sass.unrolls.size() - zero_count;

                // Base registers with no unrolling will show 0, hence need to ignore those counts
                if (adjacent_count > 0 && index_sass.reg_load_type == VEC_32)
                {
                    kernel_result["occurrences"].push_back({
                        {"severity", "WARNING"},
                        {"line_number", index_sass.line_number},
                        {"pc_offset", index_sass.pcOffset},
                        {"register", index_sass.base},
                        {"unroll_pc_offsets", index_sass.unroll_pc_offsets},
                        {"adjacent_memory_accesses", adjacent_count}
                    });
                    is_candidate = true;
                }
                else
                {
                    kernel_result["occurrences"].push_back({
                        {"severity", "INFO"},
                        {"line_number", index_sass.line_number},
                        {"pc_offset", index_sass.pcOffset},
                        {"register", index_sass.base},
                        {"register_load_type",
                         static_cast<int>(index_sass.reg_load_type)}
                    });
                }
            }
        }

        static_result["result"][k_sass] = std::move(kernel_result);

        if (is_candidate)
        {
            static_result["candidate_kernels"].push_back({
                {"name", k_sass},
                {"demangled", get_demangled_kernel(k_sass)}
            });
        }
    }

    return static_result;
}

bool has_vectorization_candidate(const json& static_result)
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
     * exit 0 -> vectorization candidate detected
     * exit 1 -> no vectorization candidate detected
     * exit 2 -> invalid arguments or static-result write failure
     */
    const std::string assembly = argv[1];
    const auto vectorize_tuple = vectorized_analysis(assembly);
    const auto& vectorize_analysis_map = std::get<0>(vectorize_tuple);
    const auto& register_map = std::get<1>(vectorize_tuple);

    const json static_result = build_static_vectorization_result(vectorize_analysis_map, register_map);

    const auto static_result_file = static_result_path(assembly, "vectorization");

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static vectorization result to "
                  << static_result_file << std::endl;
        return 2;
    }
}

