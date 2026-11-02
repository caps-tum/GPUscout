#include "parser_amdgcn_register_spilling.hpp"
#include "parser_liveregisters.hpp"
#include "parser_metrics.hpp"
#include "../utilities/json.hpp"

#include <unordered_map>
#include <iostream>
#include <vector>
#include <string>

using json = nlohmann::json;

json analysis_register_spilling (
    const std::unordered_map<std::string, std::vector<mem>>& mem_map,
    std::unordered_map<std::string, mtc> mtc_map,
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map)
{
    json result;

    for (const auto& [krn_name, mem_vec] : mem_map)
    {
        // create a JSON entry for each distinctive kernel
        json krn_result = {
            {"occurrences", json::array()}
        };

        // TODO check if this is happening
        if (krn_name == "")
        {
            break;
        }
	
	    std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : register spilling" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        
        bool spill_detected = false;
        
        for (auto mem_obj : mem_vec)
        {
            if (mem_obj.type == WRITE || mem_obj.type == STORE)
            {
                spill_detected = true;
                std::cout << std::endl; 
		        std::cout << "==== WARNING" << std::endl;
                std::cout << "==== register spilling detected in file " << mem_obj.loc.file_name << " at line "
                          << mem_obj.loc.line_num << std::endl;
                std::cout << "==== instruction: " << mem_obj.name << std::endl;
                std::cout << "==== vdata register: " << mem_obj.reg_num << std::endl;

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
                    std::cout << "==== the succeeding compute instruction using register " << mem_obj.reg_num
                              <<std::endl;
                    std::cout << "     after spilling was " << mem_obj.fst.name << " in file "
                              << mem_obj.fst.loc.file_name << " at line number " << mem_obj.fst.loc.line_num
                              << std::endl;

                    line_result["previous_compute_instruction"] = {
                        {"instruction", mem_obj.fst.name},
                        {"file_name", mem_obj.fst.loc.file_name},
                        {"line_number", mem_obj.fst.loc.line_num}/*,
                        {"pc_offset", 0} // TODO pc_offset*/
                    };
                }

                // --------- Register Pressure ------------------
                // search for a match between the PC offset of the current local memory instruction and the PC offset of
                // an entry in the live register map
                auto reg_search_it = std::find_if(
                    live_register_map[krn_name].begin(),
                    live_register_map[krn_name].end(),
                    [&](const auto &i) { return mem_obj.PC_offset == i.pcOffset; }
                    );

                if (reg_search_it != live_register_map[krn_name].end()) {
                    std::cout << "==== INFO :: Current VGPRs for the AMDGCN ISA instruction: " << reg_search_it->vgp_reg << std::endl;
                    line_result["used_register_count"] = reg_search_it->vgp_reg;

                    if (reg_search_it->change_vgpr_from_last > 0) {
                        std::cout << "Increased VGPR pressure with " << std::abs(reg_search_it->change_vgpr_from_last) << " more registers compared to last AMGGCN ISA instruction" << std::endl;
                        line_result["register_pressure_increase"] = std::abs(reg_search_it->change_vgpr_from_last);
                    } else {
                        line_result["register_pressure_increase"] = 0;
                    }
                }

                // TODO PC stall

                if (!line_result.is_null())
                {
                    krn_result["occurrences"].push_back(line_result);
                }
            }
        }

        if (!spill_detected)
        {
	        std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no register spilling detected in kernel " << krn_name << std::endl;
        }

        // Metrics
        auto mtc_obj = mtc_map[krn_name];

        auto approx_percent = mtc_obj.ID_17_3_1 ? mtc_obj.ID_15_1_9 * mtc_obj.ID_16_3_5 / mtc_obj.ID_17_3_1 : 0.0;

        std::cout << std::endl;
        std::cout << "==== INFO" << std::endl;
        std::cout << "==== number of cycles a wavefront in the kernel dispatch stalled waiting on memory of any kind"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_7_2_4 << std::endl;
        std::cout << "==== total number of spill/stack memory instructions executed on all compute units"
                  << " on the accelerator" << std::endl;
        std::cout << "     " << mtc_obj.ID_15_1_9 << std::endl;
        std::cout << "==== number of cycles the address processing unit spent working on spill/stack instructions"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_15_1_13 << std::endl;
        std::cout << "==== APPROXIMATE percentage of total L2 cache requests due to private memory" << std::endl;
        std::cout << "==== if the percentage is high, the memory traffic between the CUs and L2 is mostly due to"
                  << " private" << std::endl;
        std::cout << "     st memory (need to contain register spills)" << std::endl;
        std::cout << "     " << approx_percent << std::endl;
        
        result[krn_name] = krn_result;
    }

    return result;
}

int main(int argc, char **argv)
{
    std::string assembly = argv[1];
    auto mem_map = parser_register_spilling(assembly);

    //TODO PC stalls

    std::string mtc_dir = argv[2];
    auto mtc_map = parser_metrics(mtc_dir, assembly);

    // live registers
    std::string livereg_dir = argv[3];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(livereg_dir, assembly);

    bool save_as_json = std::strcmp(argv[4], "true") == 0;
    std::string json_out_dir = argv[5];

    json result = analysis_register_spilling(mem_map, mtc_map, live_register_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/register_spilling.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
