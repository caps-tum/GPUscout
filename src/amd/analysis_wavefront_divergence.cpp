#include "parser_metrics.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

using json = nlohmann::json;

json analysis_wavefront_divergence(
    json static_result,
    std::unordered_map<std::string, mtc> mtc_map)
{
    auto& result = static_result["result"];

    for (auto& [krn_name, krn_result] : result.items())
    {
        // TODO check if this is happening
        if (krn_name.empty())
        {
            continue;
        }

        std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : wavefront divergence" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;

        for (auto& occurrence : krn_result["occurrences"])
        {
            const std::string file_name = occurrence["file_name"].get<std::string>();
            const int line_number = occurrence["line_number"].get<int>();
            const std::string target_branch = occurrence["target_branch"].get<std::string>();
            const std::string target_file_name = occurrence["target_branch_start_file_name"].get<std::string>();
            const int target_line_number = occurrence["target_branch_start_line_number"].get<int>();
            const std::string pc_offset = occurrence["pc_offset"].get<std::string>();

            std::cout << std::endl;
            std::cout << "==== WARNING" << std::endl;
            std::cout << "==== conditional branching detected in file " << file_name
                      << "at line number " << line_number << " of your code, "
                      << "with target branch: " << target_branch << std::endl;
            std::cout << "     (target branch starts at in file " << target_file_name
                      << " at line number " << target_line_number << ")" << std::endl;

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
    /*! Full analysis mode:
     *  exit 0 -> successful analysis
     *  exit 1 -> invalid static result
     *  exit 2 -> invalid arguments
     */
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " <assembly-file> <metrics-dir> <save-as-json> <json-output-dir>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "wavefront_divergence");
    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static wavefront divergence result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string mtc_dir = argv[2];
    auto mtc_map = parser_metrics(mtc_dir, assembly);

    int save_as_json = std::strcmp(argv[3], "true") == 0;
    std::string json_out_dir = argv[4];

    json result = analysis_wavefront_divergence(static_result, mtc_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/warp_divergence.json");
        json_file << result.dump(4);
        json_file.close();
    }
}
