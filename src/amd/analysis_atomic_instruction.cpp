#include "parser_metrics.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"
#include <iostream>

using json = nlohmann::json;

json analysis_atomic_instruction(
    json static_result,
    std::unordered_map<std::string, mtc> mtc_map)
{
    auto& result = static_result["result"];
    auto& metadata = static_result["metadata"];

    for (const auto& [krn_name, krn_result] : result.items())
    {
	    std::cout << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : atomic instruction" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;

        auto& occurrences = krn_result["occurrences"];
        const int global_atomics = metadata[krn_name]["global_atomics"].get<int>();
        const int shared_atomics = krn_result["shared_atomics"].get<int>();


        if (global_atomics > 0)
        {
	        std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== number of global atomic instructions: " << global_atomics << std::endl;

            // for every occurring global atomic instruction in the kernel
            for (const auto& occurrence : occurrences)
            {
                if (!occurrence["is_global"].get<bool>())
                {
                    continue;
                }

                std::cout << std::endl;
                std::cout << "==== INFO" << std::endl;
                std::cout << "==== global atomic instruction found in file "
                          << occurrence["file_name"].get<std::string>() << " at line number "
                          << occurrence["line_number"].get<int>() << " of your source code" << std::endl;

                if (occurrence["in_for_loop"].get<bool>())
                {
                    std::cout << "==== the atomic instruction could be inside a loop" << std::endl;
                }
            }
		        
        }
        else if (global_atomics == 0)
        {
	        std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no global atomics detected in the assembly file" << std::endl;
        }

        if (shared_atomics > 0)
        {
	        std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== number of shared atomic instructions in the assembly file "
                      << shared_atomics << std::endl;
            
            for (const auto &occurrence : occurrences)
            {
                if (occurrence["is_global"].get<bool>())
                {
                    continue;
                }

                std::cout << std::endl;
                std::cout << "==== INFO" << std::endl;
                std::cout << "==== shared atomic instruction found in file "
                          << occurrence["file_name"].get<std::string>() << " at line number "
                          << occurrence["line_number"].get<int>() << " of your source code" << std::endl;

                if (occurrence["in_for_loop"].get<bool>())
                {
                    std::cout << "==== the atomic instruction could be inside a loop" << std::endl;
                }
            }   
        }
        else if (shared_atomics == 0)
        {
	        std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no shared atomics detected in the assembly file" << std::endl;
        }

        // TODO PC stall

        // Metrics
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
        std::cout << "==== total number of global & generic memory atomic (with and without return) instructions "
                  << "executed on" << std::endl;
        std::cout << "     all compute units on the accelerator" << std::endl;
        std::cout << "     " << mtc_obj.ID_16_3_3 << std::endl;
        std::cout << "==== total number of cycles spent on LDS atomics with return" << std::endl;
        std::cout << "     " << mtc_obj.ID_12_2_5 << std::endl;
        std::cout << "==== total number of atomic requests (with and without return) to the L2 from all clients"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_17_3_4 << std::endl;
        std::cout << "==== percent of write requests generated by the L2 cache that are atomic requests to any memory "
                  << std::endl;
        std::cout << "     location" << std::endl;
        std::cout << "     " << mtc_obj.ID_17_2_7 << std::endl;
        std::cout << "==== total number of L2 requests to Infinity Fabric to atomically update 32B or 64B of data in "
                     "any" << std::endl;
        std::cout << "     memory location" << std::endl;
        std::cout << "     " << mtc_obj.ID_17_6_10/*ID_17_5_10*/ << std::endl;
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
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <assembly-file> <metrics-dir> <save-as-json> <json-output-dir>\\n";
        return 2;
    }

    std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "atomic_instruction");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static atomic instruction result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string mtc_dir = argv[2];
    auto mtc_map = parser_metrics(mtc_dir, assembly);

    int save_as_json = std::strcmp(argv[3], "true") == 0;
    std::string json_out_dir = argv[4];

    json result = analysis_atomic_instruction(static_result, mtc_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/global_atomics.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
