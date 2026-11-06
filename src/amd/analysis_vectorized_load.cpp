#include "parser_metrics.hpp"
#include "parser_liveregisters.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

using json = nlohmann::json;

json analysis_vectorized_load(
    json static_result,
    std::unordered_map<std::string, mtc> mtc_map,
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map)
{
    auto& result = static_result["result"];
    const auto& metadata = static_result["metadata"];

    for (auto& [krn_name, krn_result] : result.items())
    {
        // TODO check if this is happening
        if (krn_name == "")
        {
            break;
        }

        std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : vectorized load" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;

        int ld_cnt = 0;

        if (metadata.contains(krn_name))
        {
            ld_cnt = metadata[krn_name]["non_vectorized_load_count"].get<int>();
        }

        std::cout << std::endl;
        std::cout << "==== INFO" << std::endl;
        std::cout << "==== total number of non-vectorized global vector load instances for this kernel "
                  << ld_cnt << std::endl;

        for (auto& occurrence : krn_result["occurrences"])
        {
            const std::string severity = occurrence["severity"].get<std::string>();
            const std::string file_name = occurrence["file_name"].get<std::string>();
            const int line_number = occurrence["line_number"].get<int>();
            const std::string reg = occurrence["register"].get<std::string>();
            const std::string pc_offset = occurrence["pc_offset"].get<std::string>();
            
            if (severity == "WARNING")
            {
                std::cout << std::endl;
                std::cout << "==== WARNING" << std::endl;
                std::cout << "==== load multiple dwords at once for register " << reg
                            << ", first used in a global load instruction in" << std::endl;
                std::cout << "     " << "file " << file_name << " at"
                            << " line " << line_number << " of your code" << std::endl;
                std::cout << "==== register " << reg << " has " << occurrence["adjacent_memory_accesses"].get<int>()
                            << " adjacent memory accesses" << std::endl;
            }
            else
            {
                const std::string load_type = occurrence["register_load_type"].get<std::string>();
                if (load_type == "x2")
                {
                    std::cout << std::endl;
                    std::cout << "==== INFO" << std::endl;
                    std::cout << "==== global load instruction using register " << reg
                                << ", in file " << file_name << " at line number "
                                << line_number << " of your code, is already loading 2 double words at"
                                << " once" << std::endl;
                }
                if (load_type == "x3")
                {
                    std::cout << std::endl;
                    std::cout << "==== INFO" << std::endl;
                    std::cout << "==== global load instruction using register " << reg
                                << ", in file " << file_name << " at line number "
                                << line_number << " of your"
                                << " code, is already loading 3 double words at once" << std::endl;
                }
                if (load_type == "x4")
                {
                    std::cout << std::endl;
                    std::cout << "==== INFO" << std::endl;
                    std::cout << "==== global load instruction using register " << reg
                                << ", in file "
                                << file_name << " at line number " << line_number << " of your"
                                << " code, is already loading 4 double words at once" << std::endl;
                }
            }

            // --------- Register Pressure ------------------
            // search for a match between the PC offset of the current local load instruction and the PC offset of
            // an entry in the live register map
            auto reg_search_it = std::find_if(
                live_register_map[krn_name].begin(),
                live_register_map[krn_name].end(),
                [&](const auto &i) { return pc_offset == i.pcOffset; }
                );

            if (reg_search_it != live_register_map[krn_name].end()) 
            {
                std::cout << "==== INFO :: Current VGPRs for the AMDGCN ISA instruction: " << reg_search_it->vgp_reg << std::endl;
                occurrence["used_register_count"] = reg_search_it->vgp_reg;

                if (reg_search_it->change_vgpr_from_last > 0) {
                    std::cout << "Increased VGPR pressure with " << std::abs(reg_search_it->change_vgpr_from_last) << " more re compared to last AMGGCN ISA instruction" << std::endl;
                    occurrence["register_pressure_increase"] = std::abs(reg_search_it->change_vgpr_from_last);
                } else {
                    occurrence["register_pressure_increase"] = 0;
                }
            }

            // TODO PC stall
        }

        // Avoid implicitly inserting a default metric record when this kernel has no parsed metrics.
        const auto metric_it = mtc_map.find(krn_name);
        if (metric_it == mtc_map.end())
        {
            std::cerr << "ERROR: Missing metrics for kernel " << krn_name << std::endl;
            continue;
        }
        const auto& mtc_obj = metric_it->second;

        std::cout << std::endl;
        std::cout << "==== INFO" << std::endl;
        std::cout << "==== number of cycles a wavefront in the kernel dispatch stalled waiting on memory of any kind"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_7_2_4 << std::endl;
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
                  << " <assembly-file> <metrics-dir> <live-register-dir> <save-as-json> <json-output-dir>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "vectorized_load");
    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static vectorized load result: " << static_result_file << std::endl;
        return 1;
    }

    std::string mtc_dir = argv[2];
    auto mtc_map = parser_metrics(mtc_dir, assembly);

    // live registers
    std::string livereg_dir = argv[3];
    std::unordered_map<std::string, std::vector<live_registers>> live_register_map = live_registers_analysis(livereg_dir, assembly);

    bool save_as_json = std::strcmp(argv[4], "true") == 0;
    std::string json_out_dir = argv[5];

    json result = analysis_vectorized_load(static_result, mtc_map, live_register_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/vectorization.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
