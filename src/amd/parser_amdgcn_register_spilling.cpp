#include "amdgcn_instructions.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <unordered_map>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <tuple>
#include <regex>

using json = nlohmann::json;

/// @brief local memory operations can either be LOAD or STORE
enum operation
{
    LOAD,
    READ,
    STORE,
    WRITE
};

// https://linuxhint.com/cpp-ways-to-convert-enum-to-string/
static const char *type_string[] =
{
    "LOAD",
    "READ",
    "STORE",
    "WRITE"
}; 

/// @brief the structure contains information about the source code location of instructions in the assembly
struct location
{
    int line_num;
    std::string file_name;
};

/// @brief struct holding information about registers used for arithmetic operations
struct arith
{
    std::string name;
    std::string reg_num;
    std::string PC_offset;
    location loc;
};

/// @brief struct holding information about accumulation VGPR moves and private memory load / store operations
struct mem
{
    bool successor = false;
    std::string name;
    std::string reg_num;
    std::string PC_offset;
    location loc;
    operation type;
    arith fst; // first arithmetic instruction following the memory (load) instruction
};

/// @brief          assembly analysis if a register has spilled data to accumulation VGPRs or private memory
/// @param filename assembly file
/// @return         tuple of two maps:
///                 - first map stores information about accumulation VGPR moves and private memory load / store 
///                   operations performed by different kernels in the assembly file
///                 - second map stores information about registers used for arithmetic operations by different kernels 
///                   in the assembly file
inline std::unordered_map<std::string, std::vector<mem>>
parser_register_spilling(const std::string &filename)
{
    std::fstream file(filename, std::ios::in);
    std::string line;

    // map (key: kernel name | value: vector of all accumulation VGPR moves and 
    //                                load / store instruction from / to private memory)
    std::unordered_map<std::string, std::vector<mem>> mem_map;

    // vector of all accumulation VGPR moves and load / store instruction from / to private memory
    std::vector<mem> mem_vec;

    location loc_obj;
    std::string krn_name;

    if (file.is_open())
    {
        while (std::getline(file, line))
        { 
            std::smatch match;

            if (std::regex_search(line, match, regex_krn_name))
            {
                mem_vec.clear();

                krn_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_loc))
            {
                loc_obj.line_num = std::stoi(match[2].str());
                loc_obj.file_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_VOP3P("(v_accvgpr_(read|write)\\w*)")))
            {
                mem mem_obj;

                mem_obj.name = match[1].str();
                // If a read instruction is encountered, remember the architected VGPR destination register; 
                // if a write instruction is encountered, remember the architected VGPR source register.
                mem_obj.reg_num = (match[2] == "read") ? match[3].str() : match[4].str();
                mem_obj.PC_offset = match[5].str();
                mem_obj.loc = loc_obj;
                mem_obj.type = (match[2].str() == "read") ? READ : WRITE;

                mem_vec.push_back(mem_obj);
            }

            if (std::regex_search(line, match, regex_FLAT("(scratch_(load|store)\\w*)")))
            {
                mem mem_obj;

                mem_obj.name = match[1].str();
                // If a load instruction is encountered, remember the architected VGPR data destination register; 
                // if a store instruction is encountered, remember the architected VGPR data source register.
                mem_obj.reg_num = (match[2].str() == "load") ? match[3].str() : match[4].str();
                mem_obj.PC_offset = match[6].str();
                mem_obj.loc = loc_obj;
                mem_obj.type = (match[2].str() == "load") ? LOAD : STORE;

                mem_vec.push_back(mem_obj);
            }

            if (std::regex_search(line, match, regex_MIMG("(image_load\\w*)", "s\\[0:3\\]")) ||
                std::regex_search(line, match, regex_MUBUF("(buffer_(load|store)\\w*)", "s\\[0:3\\]")) ||
                std::regex_search(line, match, regex_MTBUF("(tbuffer_(load|store)_format\\w*)", "s\\[0:3\\]")))
            {
                mem mem_obj;

                mem_obj.name = match[1].str();
                // If a load instruction is encountered, remember the architected VGPR data destination register; 
                // if a store instruction is encountered, remember the architected VGPR data source register.
                mem_obj.reg_num = match[3].str();
                mem_obj.PC_offset = match[6].str();
                mem_obj.loc = loc_obj;
                mem_obj.type = (match[2].str() == "load") ? LOAD : STORE;

                mem_vec.push_back(mem_obj);
            }

            if (std::regex_search(line, match, regex_VOP1("((?:v_rcp|v_sqrt|v_rsq|v_sin|v_cos|v_log|v_exp)\\w*)")) ||
                std::regex_search(line, match, regex_VOP2("((?:v_add|v_mul|v_mad)\\w*)")) ||
                std::regex_search(line, match, regex_VOP3("((?:v_add|v_mul|v_mad|v_fma|v_div_fma|v_rcp|v_sqrt|v_rsq"
                                                                         "|v_sin|v_cos|v_log|v_exp)\\w*)")) ||
                std::regex_search(line, match, regex_VOP3P("((?:v_fma|v_mfma)\\w*)")))
            {
                std::string vdst = match[2].str();

                for (auto it = mem_vec.rbegin(); it != mem_vec.rend(); ++it)
                {
                    if (it->reg_num == vdst && (it->type == STORE || it->type == WRITE))
                    {
                        if (!it->successor)
                        {
                            arith arith_obj;

                            arith_obj.name = match[1].str();
                            arith_obj.reg_num = vdst;
                            arith_obj.PC_offset = match[match.size() - 2].str();
                            arith_obj.loc = loc_obj;

                            it->fst = arith_obj;
                            it->successor = true;
                        }
                        break;
                    }
                }
            }

            mem_map[krn_name] = mem_vec;
        }
        file.close();
    }
    else
    {
        std::cout << "==== ERROR" << std::endl;
        std::cout << "==== could not open the file " << filename << std::endl;
    }
    
    return mem_map;
}

/*!
 * Build the part of the final register-spilling result that can already be determined from static AMDGCN assembly analysis.
 * The JSON structure intentionally matches the existing final output structure. 
 * Dynamic information such as register pressure is added later.
 */
json build_static_register_spilling_result(const std::unordered_map<std::string, std::vector<mem>>& mem_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()}
    };

    for (const auto& [krn_name, mem_vec] : mem_map)
    {
        json krn_result = {
            {"occurrences", json::array()}
        };

        // TODO: check if this is happening
        if (krn_name == "")
        {
            break;
        }

        for (const auto& mem_obj : mem_vec)
        {
            if (mem_obj.type != WRITE && mem_obj.type != STORE)
            {
                continue;
            }

            json line_result = {
                {"file_name", mem_obj.loc.file_name},
                {"line_number", mem_obj.loc.line_num},
                {"pc_offset", mem_obj.PC_offset},
                {"instruction", mem_obj.name},
                {"register", mem_obj.reg_num},
                {"operation", mem_obj.type}
            };

            if (mem_obj.successor == true)
            {
                line_result["previous_compute_instruction"] = {
                    {"instruction", mem_obj.fst.name},
                    {"file_name", mem_obj.fst.loc.file_name},
                    {"line_number", mem_obj.fst.loc.line_num}/*,
                    {"pc_offset", 0} // TODO pc_offset*/
                };
            }
             krn_result["occurrences"].push_back(line_result);
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

/*!
 * Return true if at least one register-spilling occurrence was found during static analysis.
 */
bool has_register_spilling_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char **argv)
{
    /*! Static analysis mode:
     *
     *  1. Parse AMDGCN assembly.
     *  2. Build the static part of the final analysis result.
     *  3. Preserve the result for the later full-analysis stage.
     *
     *  exit 0 -> register spilling detected
     *  exit 1 -> no register spilling detected
     *  exit 2 -> error
     */
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <assembly-file>" << std::endl;
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "register_spilling");

    const auto mem_map = parser_register_spilling(assembly);
    const json static_result = build_static_register_spilling_result(mem_map);

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static register spilling result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_register_spilling_candidate(static_result) ? 0 : 1;
}
