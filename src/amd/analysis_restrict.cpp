#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

using json = nlohmann::json;

json analysis_restrict(
    json static_result,
    const std::unordered_map<std::string, mtc>& mtc_map,
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map)
{
    auto& result = static_result["result"];

    for (auto& [krn_name, krn_result] : result.items())
    {
        std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : __restrict__" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;

       auto& occurrences = krn_result["occurrences"];

        for (auto& occurrence : occurrences)
        {
            std::cout << std::endl; 
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== register " << occurrence["register"].get<std::string>() << ", first appearing used by global load in file "
                        << occurrence["file_name"].get<std::string>() << " at line " << occurrence["line_number"].get<int>() << " of your" << std::endl;
            std::cout << "     code, could benefit from using __restrict__" << std::endl;
    

            /* TODO Restrict analysis currently doesnt include pcOffset which is needed for matching register pressure
            // --------- Register Pressure ------------------
            // search for a match between the PC offset of the current local memory instruction and the PC offset of
            // an entry in the live register map
            const std::string pc_offset = occurrence["pc_offset"].get<std::string>();
            auto reg_search_it = std::find_if(
                live_register_map[krn_name].begin(),
                live_register_map[krn_name].end(),
                [&](const auto &i) { return pc_offset == i.pcOffset; }
                );

            if (reg_search_it != live_register_map[krn_name].end()) {
                std::cout << "==== INFO :: Current VGPRs for the AMDGCN ISA instruction: " << reg_search_it->vgp_reg << std::endl;
                occurrence["used_register_count"] = reg_search_it->vgp_reg;

                if (reg_search_it->change_reg_from_last > 0) {
                    std::cout << "Increased VGPR pressure with " << std::abs(reg_search_it->change_vgpr_from_last) << " more registers compared to last AMGGCN ISA instruction" << std::endl;
                    occurrence["register_pressure_increase"] = std::abs(reg_search_it->change_vgpr_from_last);
                } else {
                    occurrence["register_pressure_increase"] = 0;
                }
            }
            */

            // TODO PC stall
        }

        if (occurrences.empty())
        {
	        std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== none of the registers can benefit from using __restrict__" << std::endl;
        }

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
    const auto static_result_file = static_result_path(assembly, "restrict");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static restrict result: " << static_result_file << std::endl;
        return 1;
    }
    
    std::string mtc_dir = argv[2];
    auto mtc_map = parser_metrics(mtc_dir, assembly);

    // live registers
    std::string livereg_dir = argv[3];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(livereg_dir, assembly);

    bool save_as_json = std::strcmp(argv[4], "true") == 0;
    std::string json_out_dir = argv[5];

    auto result = analysis_restrict(static_result, mtc_map, live_register_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/restrict.json");
        json_file << result.dump(4);
        json_file.close();
    }
    return 0;
}
