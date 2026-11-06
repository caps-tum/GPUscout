#include "parser_liveregisters.hpp"
#include "parser_metrics.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <unordered_map>
#include <iostream>
#include <vector>
#include <string>

using json = nlohmann::json;

json analysis_register_spilling (
    json static_result,
    std::unordered_map<std::string, mtc> mtc_map,
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map)
{
    auto& result = static_result["result"];
    for (auto& [krn_name, krn_result] : result.items())
    { 
        auto& occurrences = krn_result["occurrences"];

        if (occurrences.empty())
        {
            std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no register spilling detected in kernel " << krn_name << std::endl;
            continue;
        }

        std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : register spilling" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        
        
        for (auto& line_result : occurrences)
        {
            std::cout << std::endl; 
            std::cout << "==== WARNING" << std::endl;
            std::cout << "==== register spilling detected in file " << line_result["file_name"].get<std::string>() << " at line "
                          << line_result["line_number"].get<int>() << std::endl;
                std::cout << "==== instruction: " << line_result["instruction"].get<std::string>() << std::endl;
                std::cout << "==== vdata register: " << line_result["register"].get<std::string>() << std::endl;

                if (line_result.contains("previous_compute_instruction"))
                {
                    const auto& previous = line_result["previous_compute_instruction"];
                    std::cout << "==== the succeeding compute instruction using register " << line_result["register"].get<std::string>()
                              <<std::endl;
                    std::cout << "     after spilling was " << previous["instruction"].get<std::string>() << " in file "
                              << previous["file_name"].get<std::string>() << " at line number " << previous["line_number"].get<int>()
                              << std::endl;
                }

                // --------- Register Pressure ------------------
                // search for a match between the PC offset of the current local memory instruction and the PC offset of
                // an entry in the live register map
                const std::string pc_offset = line_result["pc_offset"].get<std::string>();
                auto reg_search_it = std::find_if(
                    live_register_map[krn_name].begin(),
                    live_register_map[krn_name].end(),
                    [&](const auto &i) { return pc_offset == i.pcOffset; }
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
        }

        // Metrics
        // Avoid implicitly inserting a default metric record when this kernel has no parsed metrics.
        const auto metric_it = mtc_map.find(krn_name);
        if (metric_it == mtc_map.end())
        {
            std::cerr << "ERROR: Missing metrics for kernel " << krn_name << std::endl;
            continue;
        }
        const auto& mtc_obj = metric_it->second;

        auto approx_percent = mtc_obj.ID_17_3_1 ? mtc_obj.ID_15_2_5/*mtc_obj.ID_15_1_9*/ * mtc_obj.ID_16_3_5 / mtc_obj.ID_17_3_1 : 0.0;

        std::cout << std::endl;
        std::cout << "==== INFO" << std::endl;
        std::cout << "==== number of cycles a wavefront in the kernel dispatch stalled waiting on memory of any kind"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_7_2_4 << std::endl;
        std::cout << "==== total number of spill/stack memory instructions executed on all compute units"
                  << " on the accelerator" << std::endl;
        std::cout << "     " << mtc_obj.ID_15_2_5/*mtc_obj.ID_15_1_9*/ << std::endl;
        std::cout << "==== number of cycles the address processing unit spent working on spill/stack instructions"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_15_3_0/*mtc_obj.ID_15_1_13*/ << std::endl;
        std::cout << "==== APPROXIMATE percentage of total L2 cache requests due to private memory" << std::endl;
        std::cout << "==== if the percentage is high, the memory traffic between the CUs and L2 is mostly due to"
                  << " private" << std::endl;
        std::cout << "     st memory (need to contain register spills)" << std::endl;
        std::cout << "     " << approx_percent << std::endl;
    }

    return result;
}

int main(int argc, char **argv)
{
    /*! Full analysis mode:
     *  exit 0 -> successful analysis
     *  exit 1 -> invalid static result
     *  exit 2 -> invalid arguments
     */
    if (argc != 6)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <assembly-file> <metrics-dir> <livereg-dir> <save-as-json> <json-output-dir>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "register_spilling");

    json result;
    if (!load_static_result(static_result_file, result))
    {
        std::cerr << "ERROR: Missing or invalid static register spilling result: "
                  << static_result_file << std::endl;
        return 1;
    }

    // TODO PC stalls

    const std::string mtc_dir = argv[2];
    const auto mtc_map = parser_metrics(mtc_dir, assembly);

    // live registers
    const std::string livereg_dir = argv[3];
    const auto live_register_map = live_registers_analysis(livereg_dir, assembly);

    const bool save_as_json = std::strcmp(argv[4], "true") == 0;
    const std::string json_out_dir = argv[5];

    result = analysis_register_spilling(result, mtc_map, live_register_map);

    if (save_as_json)
    {
        std::ofstream json_file(json_out_dir + "/register_spilling.json");
        if (!json_file)
        {
            std::cerr << "ERROR: Could not write register_spilling.json" << std::endl;
            return 2;
        }
        json_file << result.dump(4);
    }

    return 0;
}

