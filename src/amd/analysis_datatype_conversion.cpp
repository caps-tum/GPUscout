#include "parser_metrics.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

using json = nlohmann::json;

json analysis_datatype_conversion(
    json static_result,
    std::unordered_map<std::string, mtc> mtc_map)
{
    auto& result = static_result["result"];
    const auto& metadata = static_result["metadata"];

    for (auto& [krn_name, krn_result] : result.items())
    {
        std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : datatype conversion" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;

        const auto& occurrences = krn_result["occurrences"];

        const int F2F_count = metadata[krn_name]["F2F_count"].get<int>();
        const int I2F_count = metadata[krn_name]["I2F_count"].get<int>();
        const int F2I_count = metadata[krn_name]["F2I_count"].get<int>();

        // ---------------- F2F ----------------
        if (F2F_count > 0)
        {
            std::cout << std::endl;
            std::cout << "==== WARNING" << std::endl;
            std::cout << "==== there are " << F2F_count << " F2F conversions found at the following locations:"
                      << std::endl;

            for (const auto& occurrence : occurrences)
            {
                if (occurrence["type"].get<std::string>() != "F2F")
                {
                    continue;
                }
                std::cout << "     file name " << occurrence["file_name"].get<std::string>() << " line " << occurrence["line_number"].get<int>()
                          << std::endl;
            }
        }
        else
        {
            std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no F2F conversions found" << std::endl;
        }

        // ---------------- I2F ----------------
        if (I2F_count > 0)
        {
            std::cout << std::endl;
            std::cout << "==== WARNING" << std::endl;
            std::cout << "==== there are " << I2F_count << " I2F conversions found at the following locations:"
                      << std::endl;

            for (const auto& occurrence : occurrences)
            {
                if (occurrence["type"].get<std::string>() != "I2F")
                {
                    continue;
                }
                std::cout << "     file name " << occurrence["file_name"].get<std::string>() << " line " << occurrence["line_number"].get<int>()
                          << std::endl;
            }
        }
        else
        {
            std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no I2F conversions found" << std::endl;
        }

        // ---------------- F2I ----------------
        if (F2I_count > 0)
        {
            std::cout << std::endl;
            std::cout << "==== WARNING" << std::endl;
            std::cout << "==== there are " << F2I_count << " F2I conversions found at the following locations:"
                      << std::endl;

            for (const auto& occurrence : occurrences)
            {
                if (occurrence["type"].get<std::string>() != "F2I")
                {
                    continue;
                }

                std::cout
                    << "     file name "
                    << occurrence["file_name"].get<std::string>()
                    << " line "
                    << occurrence["line_number"].get<int>()
                    << std::endl;
            }
        }
        else
        {
            std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no F2I conversions found" << std::endl;
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
        std::cout << "==== total number of type conversion instructions (such as converting data to or from F32 to F64)"
                  << std::endl;
        std::cout << "     issued to the VALU" << std::endl;
        std::cout << "     " << mtc_obj.ID_10_2_14 << std::endl;
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
        std::cerr << "Usage: " << argv[0]
                  << " <assembly-file> <metrics-dir> <save-as-json> <json-output-dir>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "datatype_conversion");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static datatype conversion result: "
                  << static_result_file << std::endl;
        return 1;
    }

    const std::string mtc_dir = argv[2];
    const auto mtc_map = parser_metrics(mtc_dir, assembly);

    const int save_as_json = std::strcmp(argv[3], "true") == 0;
    const std::string json_out_dir = argv[4];

    const json result = analysis_datatype_conversion(static_result, mtc_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/datatype_conversion.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
