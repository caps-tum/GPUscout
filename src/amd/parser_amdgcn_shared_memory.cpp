#include "amdgcn_instructions.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <iostream>
#include <unordered_map>
#include <fstream>
#include <string>
#include <vector>
#include <tuple>

using json = nlohmann::json;

/// @brief         count of number of instructions between start and end PC offsets in decimal
/// @param start   PC offset in hex format
/// @param end     PC offset in hex format
/// @return        PC offset difference in decimal
inline int diff(const std::string& start, const std::string& end)
{
    // pcOffset difference: 0550 - 04c0     -> compute 144
    // now each cycle difference is 10 (in hex) or 16 (in dec), divide by 16 to get the difference in instructions apart
    const int diff = std::stoul(end, nullptr, 16) - std::stoul(start, nullptr, 16);
    return diff / 16;
}

/// @brief the structure contains information about the source code location of instructions in the assembly
struct location
{
    int line_num;
    std::string file_name;
};

/// @brief target branch information to detect if instruction is in a for-loop
struct brc
{
    bool loop = false;     // is branch instruction more than one line after the branch label it references
    std::string tgt;       // target branch label of the branch instruction
    std::string PC_offset; // PC offset of the branch instruction
};

struct global_load
{
    bool lds_bit;           // is the global load instruction using the LDS bit
    std::string brc;        // last branch target before the appearance of the global load instruction
    std::string PC_offset;  // PC offset of the global load instruction
    location loc;                // corresponding source code location of the global load instruction
};

struct shared_write
{
    int cnt_to_shrd_mem_st; // number of instructions (or cycles) since last seen global load instruction using LDS
                            // write instructions vdata register as vdst
    std::string brc;        // last branch target before the appearance of the LDS write instruction
    std::string PC_offset;  // PC offset of the LDS write instruction
    location loc;                // corresponding source code location of the LDS write instruction
};

/// @brief stores information about register holding data from global memory
struct reg
{
    int ld_count;                // number of times the register was used as destination for a load instruction
    int op_count;                // number of times the register was used as destination for a arithmetic instruction
    std::string reg_num;         // register number that was used as destination for a load instruction
    std::vector<global_load> gbl_ld;  // vector of all global load instructions using the register as their vdst register
    std::vector<shared_write> shr_wr;  // vector of all LDS write instructions using the register as their vdata register
};

/// @brief          SASS analysis if shared memory can be used instead of global loads
/// @param filename assembly file
/// @return         tuple of two maps:
///                 - first map holds registers accessing global loads, sorted by kernel name 
///                 - second map includes the target branch information to detect for-loop
inline std::tuple<std::unordered_map<std::string, std::vector<reg>>, std::unordered_map<std::string, std::vector<brc>>>
parser_shared_memory(const std::string &filename)
{
    std::fstream file(filename, std::ios::in);
    std::string line;

    std::unordered_map<std::string, std::vector<reg>> reg_map;
    std::unordered_map<std::string, std::vector<brc>> brc_map;
    std::vector<std::string> lbl_vec;

    std::vector<reg> reg_vec;
    std::vector<brc> brc_vec;

    std::string krn_name; // current kernel name
    std::string cur_brc;  // current branch
    location loc_obj;          // current location

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            std::smatch match;

            if (std::regex_search(line, match, regex_krn_name))
            {
                cur_brc = "";

                reg_vec.clear();
                brc_vec.clear();

                krn_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_loc))
            {
                loc_obj.line_num = std::stoi(match[2].str());
                loc_obj.file_name = match[1].str();
            }

            /** when a global memory load is detected:
              * 0. determine if a global memory to LDS load is at hand
              * 1. determine the destination register
              * 2. determine if the destination register has already been seen
              * 2.1 if true: update the seen register count
              * 2.2 if false: add the register to the register holding vector
            */
            if (std::regex_search(line, match, regex_FLAT("(global_load\\w*)")) ||
                std::regex_search(line, match, regex_MIMG("(image_load\\w*)", "((?!s\\[0:3\\])[\\w\\[\\]:\\-]+)")) ||
                std::regex_search(line, match, regex_MUBUF("(buffer_load\\w*)", "((?!s\\[0:3\\])[\\w\\[\\]:\\-]+)")) ||
                std::regex_search(line, match, regex_MTBUF("(tbuffer_load_format\\w*)",
                                                           "((?!s\\[0:3\\])[\\w\\[\\]:\\-]+)")))
            {
                bool lds_bit = false;

                // if LDS bit is set, data is transferred between LDS and memory
                if (std::regex_search(line, regex_FLAT("(global_load\\w*)")))
                {
                    // match.size() - 1 is the binary instruction encoding
                    char half_byte = match[match.size() - 1].str().at(4);
                    std::string LDS = "2367ABFE";
                    if (LDS.find(half_byte) != std::string::npos)
                    {
                        lds_bit = true;
                    }
                }

                // if LDS bit is set, data is transferred between LDS and memory
                if (std::regex_search(line, regex_MUBUF("(buffer_load\\w*)", "((?!s\\[0:3\\])[\\w\\[\\]:\\-]+)")))
                {
                    // match.size() - 1 is the binary instruction encoding
                    char half_byte = match[match.size() - 1].str().at(3);
                    std::string LDS = "13579BDF";
                    if (LDS.find(half_byte) != std::string::npos)
                    {
                        lds_bit = true;
                    }
                }

                // if LDS bit is set, data is transferred between LDS and memory
                if (std::regex_search(line, regex_MUBUF("(tbuffer_load_format\\w*)", "((?!s\\[0:3\\])[\\w\\[\\]:\\-]+)")))
                {
                    // match.size() - 1 is the binary instruction encoding
                    std::string instruction = match[match.size() - 1].str();
                    // remove whitespace form 64 bit instruction
                    instruction.erase(
                        std::remove_if(instruction.begin(), instruction.end(), ::isspace),
                        instruction.end());
                    char half_byte = instruction.at(10);
                    std::string LDS = "2367ABFE";
                    if (LDS.find(half_byte) != std::string::npos)
                    {
                        lds_bit = true;
                    }
                }

                // get destination register of instruction
                auto vdst = match[2].str();

                global_load gbl_ld_obj;
                gbl_ld_obj.lds_bit = lds_bit;
                gbl_ld_obj.brc = cur_brc;
                gbl_ld_obj.PC_offset = match[match.size() - 2];
                gbl_ld_obj.loc = loc_obj;

                auto it = std::find_if(
                    reg_vec.begin(), 
                    reg_vec.end(), 
                    [&](const auto &i) { return vdst == i.reg_num; }
                );

                if (it != reg_vec.end())
                {
                    // if the register has been encountered before, 
                    // increase the number of times the register has been used for a load
                    it->ld_count++;
                    it->gbl_ld.push_back(gbl_ld_obj);
                }
                else 
                {
                    reg reg_obj;
                    // if the register has not been encountered before, 
                    // add a new register object to the register object vector
                    reg_obj.ld_count = 1;
                    reg_obj.op_count = 0;
                    reg_obj.reg_num = vdst;

                    reg_obj.gbl_ld.push_back(gbl_ld_obj);
                    reg_vec.push_back(reg_obj);
                }
            }

            // if a DS write instruction is encountered:
            // 1. determine the data source register of the instruction
            // 2. determine if the data source register has previously been used 
            //    as the destination register for a global load instruction
            // 2.1 if true: - calculate and remember the number of instructions 
            //                between the global load and DS write instruction
            //              - mark the data source register as "used for shared memory"
            if (std::regex_search(line, match, regex_DS("(ds_write\\w*)")))
            {
                std::vector<std::string> vdata;

                if (std::regex_search(match[1].str(), std::regex("ds_write2\\w*")))
                {
                    vdata.push_back(match[3].str());
                    vdata.push_back(match[4].str());
                } 
                else 
                {
                    vdata.push_back(match[3].str());
                }

                shared_write shr_wr_obj;
                shr_wr_obj.brc = cur_brc;
                shr_wr_obj.PC_offset = match[match.size() - 2];
                shr_wr_obj.loc = loc_obj;

                std::unordered_map<std::string, std::vector<reg>::iterator> iters;
                for (auto it = reg_vec.begin(); it != reg_vec.end(); ++it) 
                {
                    if (
                        std::any_of(
                            vdata.begin(),
                            vdata.end(),
                            [&](const auto &j) { return j == it->reg_num; }
                        )
                    ) 
                    {
                        iters[it->reg_num] = it;
                    }
                }

                for (const auto& j : vdata)
                {
                    int cnt_to_shrd_mem_st = 0;
                    if (iters.find(j) != iters.end())
                    {
                        auto gbl_ld = iters[j]->gbl_ld;
                        for (auto it = gbl_ld.rbegin(); it != gbl_ld.rend(); ++it)
                        {
                            if (!it->lds_bit)
                            {
                                cnt_to_shrd_mem_st = diff(it->PC_offset, match[match.size() - 2].str());
                            }
                        }
                        shr_wr_obj.cnt_to_shrd_mem_st = cnt_to_shrd_mem_st;
                        iters[j]->shr_wr.push_back(shr_wr_obj);
                    }
                    else
                    {
                        shr_wr_obj.cnt_to_shrd_mem_st = cnt_to_shrd_mem_st;

                        reg reg_obj;
                        // if the register has not been encountered before,
                        // add a new register object to the register object vector
                        reg_obj.ld_count = 1;
                        reg_obj.op_count = 0;
                        reg_obj.reg_num = j;

                        reg_obj.shr_wr.push_back(shr_wr_obj);
                        reg_vec.push_back(reg_obj);
                    }
                }
            }

            if (std::regex_search(line, match, regex_VOP1("((?:v_rcp|v_sqrt|v_rsq|v_sin|v_cos|v_log|v_exp)\\w*)")) ||
                std::regex_search(line, match, regex_VOP2("((?:v_add|v_mul|v_mad)\\w*)")) ||
                std::regex_search(line, match, regex_VOP3("((?:v_add|v_mul|v_mad|v_fma|v_div_fma|v_rcp|v_sqrt|v_rsq"
                                                          "|v_sin|v_cos|v_log|v_exp)\\w*)")) || 
                std::regex_search(line, match, regex_VOP3P("((?:v_fma|v_mfma)\\w*)")) ||
                std::regex_search(line, match, regex_DS("((?:ds_add|ds_and|ds_dec|ds_inc|ds_max|ds_min|ds_or|ds_rsub"
                                                        "|ds_xor)\\w*)")) ||
                std::regex_search(line, match, regex_FLAT("((?:global_atomic)\\w*)")) ||
                std::regex_search(line, match, regex_MIMG("((?:image_atomic)\\w*)", "([\\w\\[\\]:\\-]+)")) ||
                std::regex_search(line, match, regex_MUBUF("((?:buffer_atomic)\\w*)", "([\\w\\[\\]:\\-]+)")))
            {
                std::vector<std::string> regs;
                for (const auto& m : match) {
                    if (m.str()[0] == 'v') 
                    { 
                        regs.push_back(m.str());
                    }
                }

                for (const auto &reg : regs)
                {
                    auto it = std::find_if(
                        reg_vec.begin(), 
                        reg_vec.end(), 
                        [&](const auto &i) { return reg == i.reg_num && i.gbl_ld.size() != 0; }
                    );

                    if (it != reg_vec.end())
                    {
                        // if the register has been encountered before, 
                        // increase the number of times the register has been used for a arithmetic operation
                        it->op_count++; 
                    }
                }
            }

            // check if the LDG operations are in a for/while loop 
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

            // if the current line contains a branch label, extract the name of the branch label.
            if (std::regex_search(line, match, regex_brc_lbl))
            {
                cur_brc = match[1].str();
                lbl_vec.push_back(match[1].str());
            }

            reg_map[krn_name] = reg_vec;
            brc_map[krn_name] = brc_vec;
        }
        file.close();
    }
    else
    {
        std::cout << "==== ERROR" << std::endl;
        std::cout << "==== could not open the file " << filename << std::endl;
    }

    return std::make_tuple(reg_map, brc_map);
}

/*!
 * Build the reusable static result for the shared-memory analysis.
 * "result" keeps the existing final shared_memory.json structure.
 */
json build_static_shared_memory_result(const std::unordered_map<std::string, std::vector<reg>>& reg_map, const std::unordered_map<std::string, std::vector<brc>>& brc_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()},
    };

    for (const auto& [krn_name, reg_vec] : reg_map)
    {
        if (krn_name.empty())
        {
            break;
        }

        json krn_result = {{"occurrences", json::array()}};

        bool shared_recommend_flag = false;

        for (auto reg_obj : reg_vec)
        {
            // consider only registers with load count > 0, operation count > 1 and operation count > load count
            if (!((reg_obj.ld_count > 0) && (reg_obj.op_count > 1) && (reg_obj.op_count > reg_obj.ld_count)))
                continue;
        
            // -------------------------------------------------
            // Global loads that already use the LDS bit
            // -------------------------------------------------
            for (const auto& gbl_ld_obj : reg_obj.gbl_ld)
            {
                if (!gbl_ld_obj.lds_bit)
                {
                    continue;
                }

                json line_result = {
                    {"severity", "INFO"},
                    {"file_name", gbl_ld_obj.loc.file_name},
                    {"line_number", gbl_ld_obj.loc.line_num},
                    {"instruction_type", "global_load"},
                    {"register", reg_obj.reg_num},
                    {"uses_LDS_bit", true},
                    {"pc_offset", gbl_ld_obj.PC_offset}
                };

                krn_result["occurrences"].push_back(line_result);
            }

            // ------------------------------------------------------------
            // LDS writes
            // ------------------------------------------------------------
            for (const auto& shr_wr_obj : reg_obj.shr_wr)
            {
                json line_result = {
                    {"severity", "INFO"},
                    {"file_name", shr_wr_obj.loc.file_name},
                    {"line_number", shr_wr_obj.loc.line_num},
                    {"instruction_type", "lds_write"},
                    {"register", reg_obj.reg_num},
                    {"pc_offset", shr_wr_obj.PC_offset}
                };

                if (shr_wr_obj.cnt_to_shrd_mem_st > 0)
                {
                    line_result["instruction_count_to_shared_mem_store"] = shr_wr_obj.cnt_to_shrd_mem_st;
                }

                krn_result["occurrences"].push_back(line_result);
            }

            // ------------------------------------------------------------
            // Candidate global loads without the LDS bit
            // ------------------------------------------------------------
            for (const auto& gbl_ld_obj : reg_obj.gbl_ld)
            {
                if (gbl_ld_obj.lds_bit)
                {
                    continue;
                }

                // branch map stores an accumulative vector of branch instructions per branch

                // returns all conditional branch instructions encountered up to the end of the branch to which
                // the global load instruction belongs
                //for (auto j : brc_map[gbl_ld_obj.tgt_brc])
                //{
                    // if the branch instructions target the branch, the global load instruction belongs to
                    //if ((j.loc.line_num != 0) && (gbl_ld_obj.tgt_brc == j.tgt))
                bool inside_loop = false;
                auto brc_it = brc_map.find(krn_name);
                if (brc_it != brc_map.end())
                {
                    for (const auto& brc_obj : brc_it->second)
                    {
                        if (brc_obj.tgt == gbl_ld_obj.brc &&
                            std::stoi(brc_obj.PC_offset) > std::stoi(gbl_ld_obj.PC_offset) &&
                            brc_obj.loop)
                        {
                            inside_loop = true;
                            break;
                        }
                    }
                }

                json line_result = {
                    {"severity", "WARNING"},
                    {"file_name", gbl_ld_obj.loc.file_name},
                    {"line_number", gbl_ld_obj.loc.line_num},
                    // Current candidate global-load instruction.
                    // {"pc_offset", gbl_ld_obj.PC_offset},
                    {"register", reg_obj.reg_num},
                    {"global_load_count", reg_obj.ld_count},
                    {"computation_instruction_count", reg_obj.op_count},
                    {"computation_instruction_pc_offsets", 0}, // TODO: parser currently does not preserve these PCs.
                    {"uses_shared_memory", false},
                    {"in_for_loop", inside_loop}
                };

                krn_result["occurrences"].push_back(line_result);

                shared_recommend_flag = true;
            }
        }

        static_result["result"][krn_name] = krn_result;

        if (!shared_recommend_flag)
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

bool has_shared_memory_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char **argv)
{
    /*! Static analysis mode:
     *
     *  1. Parse AMDGCN assembly.
     *  2. Build the reusable static result.
     *  3. Detect whether a shared-memory candidate exists.
     *  4. Preserve the result for the later full-analysis stage.
     *
     *  exit 0 -> shared-memory candidate detected
     *  exit 1 -> no shared-memory candidate detected
     *  exit 2 -> error
     */
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <assembly-file>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "shared_memory");
    auto tuple = parser_shared_memory(assembly);
    auto reg_map = std::get<0>(tuple);
    auto brc_map = std::get<1>(tuple);
    const json static_result = build_static_shared_memory_result(reg_map, brc_map);

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static shared memory result to " << static_result_file << std::endl;
        return 2;
    }

    return has_shared_memory_candidate(static_result) ? 0 : 1;
}
