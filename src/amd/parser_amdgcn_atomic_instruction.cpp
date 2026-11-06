#include "amdgcn_instructions.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <tuple>
#include <regex>
#include <set>

using json = nlohmann::json;

/// @brief the structure contains information about the source code location of instructions in the assembly
struct location
{
    int line_num;
    std::string file_name;

    bool operator<(const location& loc) const
    {
        return line_num < loc.line_num;
    }
};

struct shared_atom
{
    std::string brc;
    std::string PC_offset;
    location loc;

    bool operator<(const shared_atom& shr_atom) const {
        return PC_offset < shr_atom.PC_offset;
    }
};

struct global_atom
{
    std::string brc;
    std::string PC_offset;
    location loc;

    bool operator<(const global_atom& gbl_atom) const {
        return PC_offset < gbl_atom.PC_offset;
    }
};

/// @brief structure that stores the number of global and shared atomic instruction occurrences, along with the
///        corresponding source code line numbers for each occurrence
struct atom
{
    int num_g;                   // number of global atomic instructions
    int num_s;                   // number of shared atomic instructions
    std::set<global_atom> gbl_atom; // set of source code line numbers corresponding to global atomic instructions
    std::set<shared_atom> shr_atom; // set of source code line numbers corresponding to shared atomic instructions
};

struct brc
{
    bool loop = false;     // is branch instruction more than one line after the branch label it references
    std::string tgt;       // target branch label of the branch instruction
    std::string PC_offset; // PC offset of the branch instruction
};

/// @brief          find global and shared atomic instructions in the assembly code
/// @param filename assembly file
/// @return         tuple of two maps:
///                 - first map holds occurences of global and shared atomic instructions per kernel
///                 - second map holds occurences of branch labels per kernel
std::tuple<std::unordered_map<std::string, atom>, std::unordered_map<std::string, std::vector<brc>>>
parser_atomic_instruction(const std::string &filename)
{
    std::fstream file(filename, std::ios::in);
    std::string line;

    std::unordered_map<std::string, atom> atom_map;
    std::unordered_map<std::string, std::vector<brc>> brc_map;
    std::vector<std::string> lbl_vec;

    std::vector<brc> brc_vec;

    std::string krn_name; // kernel name
    std::string cur_brc;  // current branch
    location loc_obj;          // line number in source code corresponding to the current assembly code line
    atom atom_obj;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            std::smatch match;

            if (std::regex_search(line, match, regex_krn_name))
            {
                atom_obj.num_g = 0;
                atom_obj.num_s = 0;
                atom_obj.gbl_atom.clear();
                atom_obj.shr_atom.clear();

                brc_vec.clear();
                lbl_vec.clear();
                cur_brc.clear();

                krn_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_loc))
            {
                loc_obj.line_num = std::stoi(match[2].str());
                loc_obj.file_name = match[1].str();
            }

            /** If the current line contains a global atomic instruction, increment the count of observed global atomic
             * instructions and record the line number of the newly detected global atomic instruction.
             *
             * If a branch instruction exists that targets the current branch label, record the line number of the newly
             * detected global atomic instruction.
            */
            if (std::regex_search(line, match, regex_FLAT("((?:global_atomic)\\w*)")) ||
                std::regex_search(line, match, regex_MIMG("((?:image_atomic)\\w*)", "([\\w\\[\\]:\\-]+)")) ||
                std::regex_search(line, match, regex_MUBUF("((?:buffer_atomic)\\w*)", "([\\w\\[\\]:\\-]+)")))
            {
                global_atom gbl_atom_obj;
                gbl_atom_obj.brc = cur_brc;
                gbl_atom_obj.PC_offset = match[match.size() - 2];
                gbl_atom_obj.loc = loc_obj;

                atom_obj.num_g++;
                atom_obj.gbl_atom.insert(gbl_atom_obj);
            }

            /** 
             * If the current line contains a shared atomic instruction, increment the count of observed shared atomic 
             * instructions and record the line number of the newly detected shared atomic instruction.
             *
             * If a branch instruction exists that targets the current branch label, record the line number of the newly 
             * detected shared atomic instruction.
            */
            if (std::regex_search(line, match, regex_DS("((?:ds_add|ds_and|ds_dec|ds_inc|ds_max|ds_min|ds_or|ds_rsub"
                                                                   "|ds_xor)\\w*)")))
            {
                shared_atom shr_atom_obj;
                shr_atom_obj.brc = cur_brc;
                shr_atom_obj.PC_offset = match[match.size() - 2];
                shr_atom_obj.loc = loc_obj;

                atom_obj.num_s++;
                atom_obj.shr_atom.insert(shr_atom_obj);
            }

            // if the current line contains a branch label, add it to the label vector
            if (std::regex_search(line, match, regex_brc_lbl))
            {
                cur_brc = match[1].str();
                lbl_vec.push_back(match[1].str());
            }

            /**
             * Check if the current line contains a branch instruction. If it does, determine whether it references a 
             * previously encountered branch label. If a reference to a branch label is found, add the location of its 
             * first instruction to the corresponding branch label structure. Additionally, if there are instructions 
             * between the current branch instruction and the target label, set the loop flag of the referenced label to
             * true, indicating that the branch forms a loop.
            */
            if (std::regex_search(line, match, regex_SOPP("((?:s_branch|s_cbranch)\\w*)")))
            {
                brc brc_obj;

                brc_obj.loop = false;
                brc_obj.tgt = match[2].str();
                brc_obj.PC_offset = match[match.size() - 2];

                if (std::find(lbl_vec.begin(), lbl_vec.end(), brc_obj.tgt) != lbl_vec.end())
                {
                    brc_obj.loop = true;
                }

                brc_vec.push_back(brc_obj);
            }

            brc_map[krn_name] = brc_vec;
            atom_map[krn_name] = atom_obj;
        }
        file.close();
    }
    else
    {
        std::cout << "==== ERROR" << std::endl;
        std::cout << "==== could not open the file " << filename << std::endl;
    }

    return std::make_tuple(atom_map, brc_map);
}


/*!
 * Build the reusable static result for the atomic-instruction analysis.
 * "candidate_kernels" contains the kernel demangled name of which the bottleneck is detected 
 * "result" contains the information that belongs to the existing final global_atomics.json output.
 * "metadata" contains internal information required by later pipeline stages and is not be written to the final JSON output.
 */
json build_static_atomic_instruction_result(const std::unordered_map<std::string, atom>& atom_map,
                                            const std::unordered_map<std::string, std::vector<brc>>& brc_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()},
        {"metadata", json::object()}
    };

    for (const auto& [krn_name, atom_obj] : atom_map)
    {
        // TODO check if this is happening
        if (krn_name == "")
        {
            break;
        }

        json krn_result = {
            {"occurrences", json::array()},
            {"shared_atomics", atom_obj.num_s}
        };

        json occurrence_metadata = json::array();

        // for every occurring global atomic instruction in the kernel
        for (const auto& gbl_atom_obj : atom_obj.gbl_atom)
        {
            bool inside_loop = false;
            // loop through all branch instructions in the kernel
            auto brc_it = brc_map.find(krn_name);
            if (brc_it != brc_map.end())
            {
                for (const auto& brc_obj : brc_it->second)
                {
                    if (brc_obj.tgt == gbl_atom_obj.brc &&
                        std::stoi(brc_obj.PC_offset) > std::stoi(gbl_atom_obj.PC_offset) &&
                        brc_obj.loop)
                    {
                        inside_loop = true;
                    }
                }
            }

            krn_result["occurrences"].push_back({
                    {"severity", "INFO"},
                    {"file_name", gbl_atom_obj.loc.file_name},
                    {"line_number", gbl_atom_obj.loc.line_num},
                    {"in_for_loop", inside_loop},
                    {"is_global", true},
            });

            // Internal information corresponding to the occurrence at the same array index.
            occurrence_metadata.push_back({
                {"pc_offset", gbl_atom_obj.PC_offset}
            });
        }

        // Shared atomic instructions
        for (const auto &shr_atom_obj : atom_obj.shr_atom)
        {
            bool inside_loop = false;
            auto brc_it = brc_map.find(krn_name);
            if (brc_it != brc_map.end())
            {
                // loop through all branch instructions in the kernel
                for (const auto& brc_obj : brc_it->second)
                {
                    if (brc_obj.tgt == shr_atom_obj.brc &&
                        std::stoi(brc_obj.PC_offset) > std::stoi(shr_atom_obj.PC_offset) &&
                        brc_obj.loop)
                    {
                        inside_loop = true;
                    }
                }
            }

            krn_result["occurrences"].push_back({
                    {"severity", "INFO"},
                    {"file_name", shr_atom_obj.loc.file_name},
                    {"line_number", shr_atom_obj.loc.line_num},
                    {"in_for_loop", inside_loop},
                    {"is_global", false},
            });

            // Internal information corresponding to the occurrence at the same array index.
            occurrence_metadata.push_back({
                {"pc_offset", shr_atom_obj.PC_offset}
            });
        }

        static_result["result"][krn_name] = krn_result;
        static_result["metadata"][krn_name] = {
            {"global_atomics", atom_obj.num_g},
            {"occurrence_metadata", occurrence_metadata}
        };

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

/*!
 * Return true if at least one atomic instruction occurrence was found during static analysis.
 */
bool has_atomic_instruction_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <assembly-file>\\n";
        return 2;
    }

    /*! Static detection mode:
     *
     *  1. Parse AMDGCN assembly.
     *  2. Build the static part of the final analysis result.
     *  3. If a candidate exists, preserve the result for the later full-analysis stage.
     *
     *  exit 0 -> atomic instruction detected
     *  exit 1 -> no atomic instruction detected
     *  exit 2 -> error
     */
    std::string assembly = argv[1];
    auto tuple = parser_atomic_instruction(assembly);
    auto atom_map = std::get<0>(tuple);
    auto lbl_map = std::get<1>(tuple);
    json static_result = build_static_atomic_instruction_result(atom_map, lbl_map);
    const auto static_result_file = static_result_path(assembly, "atomic_instruction");

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static atomic instruction result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_atomic_instruction_candidate(static_result) ? 0 : 1;
}

