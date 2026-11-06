#include "amdgcn_instructions.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <unordered_map>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <tuple>

using json = nlohmann::json;

/// @brief the structure contains information about the source code location of instructions in the assembly
struct location
{
    int line_num;
    std::string file_name;
};

/// @brief struct representing a branch instruction
struct brc
{
    std::string tgt; // branch target
    location loc;
    std::string PC_offset; // PC offset of the branch instruction
};

/// @brief          assembly analysis, collects conditional branching instructions, and their targets  
/// @param filename assembly file
/// @return         tuple of two maps:
///                 - first map includes branch information
///                 - second map includes target branch line number
inline std::tuple<std::unordered_map<std::string, std::vector<brc>>, std::unordered_map<std::string, location>>
parser_wavefront_divergence(const std::string &filename)
{
    std::fstream file(filename, std::ios::in);
    std::string line;

    // map (key: kernel name | value: vector containing all occurrences of a branch instruction per kernel)
    std::unordered_map<std::string, std::vector<brc>> brc_map;
    // map (key: branch target | value: location of branch target)
    std::unordered_map<std::string, location> tgt_map;
    
    std::vector<brc> brc_vec;

    location loc_obj;

    std::string krn_name;
    bool set_lbl = false;
    std::string lbl;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            std::smatch match;

            if (std::regex_search(line, match, regex_krn_name))
            {
                brc_vec.clear();

                krn_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_loc))
            {
                loc_obj.line_num = std::stoi(match[2].str());
                loc_obj.file_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_SOPP("(s_cbranch\\w*)"))) 
            {
                brc brc_obj;
                
                brc_obj.tgt = match[2].str();
                brc_obj.loc = loc_obj;
                brc_obj.PC_offset = match[match.size() - 2];

                brc_vec.push_back(brc_obj);
            }
            
            if (std::regex_search(line, match, regex_brc_lbl))
            {
                lbl = match[1].str();
                set_lbl = true;
            }

            if (set_lbl)
            {
                tgt_map[lbl] = loc_obj;
                set_lbl = false;
            }

            brc_map[krn_name] = brc_vec;
        }
        file.close();
    }
    else
    {
        std::cout << "==== ERROR" << std::endl;
        std::cout << "==== could not open the file " << filename << std::endl;
    }

    return std::make_tuple(brc_map, tgt_map);
}

/*!
 * Build the reusable static result for the wavefront divergence analysis.
 * "result" contains the information that belongs to the existing final wavefront_divergence.json output.
 */
json build_static_wavefront_divergence_result(const std::unordered_map<std::string, std::vector<brc>>& brc_map, const std::unordered_map<std::string, location>& tgt_map)
{
    json static_result = {
        {"result", json::object()},
        {"candidate_kernels", json::array()}
    };

    for (const auto& [krn_name, brc_vec] : brc_map)
    {
        // TODO check if this is happening
        if (krn_name.empty())
        {
            continue;
        }

        json krn_result = {
            {"occurrences", json::array()}
        };

        for (const auto& brc_obj : brc_vec)
        {
            auto tgt_it = tgt_map.find(brc_obj.tgt);

            if (tgt_it == tgt_map.end())
            {
                continue;
            }

            // Branch instructions whose target maps to the same source location are not considered conditional branches.
            if (brc_obj.loc.file_name != tgt_it->second.file_name ||
                brc_obj.loc.line_num != tgt_it->second.line_num)
            {
                krn_result["occurrences"].push_back({
                    {"severity", "WARNING"},
                    {"pc_offset", brc_obj.PC_offset},
                    {"file_name", brc_obj.loc.file_name},
                    {"line_number", brc_obj.loc.line_num},
                    {"target_branch", brc_obj.tgt},
                    {"target_branch_start_file_name", tgt_it->second.file_name},
                    {"target_branch_start_line_number", tgt_it->second.line_num}
                });
            }
        }

        static_result["result"][krn_name] = krn_result;

        if (krn_result["occurrences"].empty())
        {
            continue;
        }   

        static_result["candidate_kernels"].push_back(
        {
            {"name", krn_name},
            {"demangled", get_demangled_kernel(krn_name, "c++filt")}
        });
    }

    return static_result;
}

bool has_wavefront_divergence_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char **argv)
{
    /*! Static analysis mode:
     *
     *  1. Parse AMDGCN assembly.
     *  2. Build the reusable static result.
     *  3. Detect whether a wavefront divergence candidate exists.
     *  4. Preserve the static result for the later full-analysis stage.
     *
     *  exit 0 -> wavefront divergence candidate detected
     *  exit 1 -> no wavefront divergence candidate detected
     *  exit 2 -> error
     */
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <assembly-file>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "wavefront_divergence");
    auto tuple = parser_wavefront_divergence(assembly);
    auto brc_map = std::get<0>(tuple);
    auto tgt_map = std::get<1>(tuple);
    const json static_result = build_static_wavefront_divergence_result(brc_map, tgt_map);

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Failed to save static wavefront divergence result." << std::endl;
        return 2;
    }

    return has_wavefront_divergence_candidate(static_result) ? 0 : 1;
}
