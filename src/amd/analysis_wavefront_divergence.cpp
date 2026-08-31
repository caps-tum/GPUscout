#include "parser_amdgcn_wavefront_divergence.hpp"
#include "parser_metrics.hpp"
#include "../utilities/json.hpp"

using json = nlohmann::json;

json analysis_wavefront_divergence(
    std::unordered_map<std::string, std::vector<brc>> brc_map,
    std::unordered_map<std::string, location> tgt_map,
    std::unordered_map<std::string, mtc> mtc_map)
{
    json result;

    for (auto [krn_name, brc_vec] : brc_map)
    {
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
        std::cout << "==== analysis    : wavefront divergence" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;

        for (auto brc_obj : brc_vec)
        {
            json line_result;;

            // branching instructions that have their target in the same file and line numbers are not considered
            // conditional branching
            if (brc_obj.loc.file_name != tgt_map[brc_obj.tgt].file_name ||
                brc_obj.loc.line_num != tgt_map[brc_obj.tgt].line_num)
            {
		std::cout << std::endl;
                std::cout << "==== WARNING" << std::endl;
                std::cout << "==== conditional branching detected in file " << brc_obj.loc.file_name
                          << "at line number " << brc_obj.loc.line_num << " of your code, "
                          << "with target branch: " << brc_obj.tgt << std::endl;
                std::cout << "     (target branch starts at in file " << tgt_map[brc_obj.tgt].file_name
                          << " at line number " << tgt_map[brc_obj.tgt].line_num << ")" << std::endl;

                line_result = {
                    {"severity", "WARNING"},
                    {"pc_offset", brc_obj.PC_offset},
                   {"file_name", brc_obj.loc.file_name},
                   {"line_number", brc_obj.loc.line_num},
                   {"target_branch", brc_obj.tgt},
                   {"target_branch_start_file_name", tgt_map[brc_obj.tgt].file_name},
                   {"target_branch_start_line_number", tgt_map[brc_obj.tgt].line_num}
                };

                // TODO PC stall
            }
            
            if (!line_result.is_null())
            {
                krn_result["occurrences"].push_back(line_result);
            }
        }

        auto mtc_obj = mtc_map[krn_name];

	std::cout << std::endl;
        std::cout << "==== INFO" << std::endl;
        std::cout << "==== total number of branch operations issued" << std::endl;
        std::cout << "     " << mtc_obj.ID_10_1_6 << std::endl;
        std::cout << "==== what percent of the kernel's duration the branch unit was busy executing instructions"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_11_2_5 << std::endl;

        result[krn_name] = krn_result;
    }

    return result;
}

int main(int argc, char **argv)
{
    std::string assembly = argv[1];
    auto tuple = parser_wavefront_divergence(assembly);
    auto brc_map = std::get<0>(tuple);
    auto tgt_map = std::get<1>(tuple);

    std::string mtc_dir = argv[2];
    auto mtc_map = parser_metrics(mtc_dir, assembly);

    int save_as_json = std::strcmp(argv[3], "true") == 0;
    std::string json_out_dir = argv[4];

    json result = analysis_wavefront_divergence(brc_map, tgt_map, mtc_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/warp_divergence.json");
        json_file << result.dump(4);
        json_file.close();
    }
}
