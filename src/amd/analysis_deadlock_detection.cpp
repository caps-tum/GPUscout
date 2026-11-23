#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"
#include <iostream>

using json = nlohmann::json;

json analysis_deadlock_detection(json static_result)
{
    auto& result = static_result["result"];

    for (const auto& [krn_name, krn_result] : result.items())
    {
        const bool deadlock = krn_result["metrics"]["deadlock_detect_flag"].get<bool>();

        std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : deadlock detection" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;

        std::cout << std::endl;
        std::cout << "==== INFO" << std::endl;
        if (deadlock)
        {
            std::cout << "==== deadlock in kernel could be possible" << krn_name << std::endl;
        }
        else
        {
            std::cout << "==== no possibility for deadlock detected in kernel " << krn_name << std::endl;
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
    if (argc != 4)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <assembly-file> <save-as-json> <json-output-dir>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "deadlock_detection");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static deadlock detection result: "
                  << static_result_file << std::endl;
        return 1;
    }

    const int save_as_json = std::strcmp(argv[2], "true") == 0;
    const std::string json_out_dir = argv[3];

    const json result = analysis_deadlock_detection(static_result);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/deadlock_detection.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
