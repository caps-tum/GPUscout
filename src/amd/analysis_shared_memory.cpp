#include "parser_metrics.hpp"
#include "../utilities/helper.hpp"
#include <unordered_set>
#include "../utilities/json.hpp"

using json = nlohmann::json;

json analysis_shared_memory(
    json static_result,
    std::unordered_map<std::string, mtc> mtc_map)
{
    auto& result = static_result["result"];

    for (auto& [krn_name, krn_result] : result.items())
    {
        std::cout << std::endl;      
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        std::cout << "==== analysis    : local memory" << std::endl;
        std::cout << "==== kernel name : " << krn_name << std::endl;
        std::cout << "======================================================================"
                  << "================================" << std::endl;
        
        auto& occurrences = krn_result["occurrences"];
        bool shared_recommend_flag = false;

        /*
        * Used so that the register-level WARNING summary is printed
        * only once even when several candidate global-load instructions
        * belong to the same register.
        */
        std::unordered_set<std::string> warned_registers;

        for (auto& occurrence: occurrences)
        {
            const std::string severity = occurrence["severity"].get<std::string>();

            // INFO occurrences
            if (severity == "INFO")
            {
                const std::string instruction_type = occurrence["instruction_type"].get<std::string>();

                // Global load already using LDS bit
                if (instruction_type == "global_load")
                {
                    std::cout << std::endl;
                    std::cout << "==== INFO" << std::endl;
                    std::cout << "==== register number " << occurrence["register"].get<std::string>()
                              << " is used by global load instruction that transfers data between LDS and"
                              << " memory instead of VGPRs and memory, in file " << occurrence["file_name"].get<std::string>()
                              << " at line number " << occurrence["line_number"].get<int>() << " of your code" << std::endl;
                }

                // Data is latter written to LDS
                else if (instruction_type == "lds_write")
                {
                    std::cout << "==== INFO" << std::endl;
                    std::cout << "register number " << occurrence["register"].get<std::string>()
                              << " is storing data in local memory in file " << occurrence["file_name"].get<std::string>()
                              << " at line number " << occurrence["line_number"].get<int>() << " of your code" << std::endl;

                    if (occurrence.contains("instruction_count_to_shared_mem_store"))
                    {
                        std::cout << "==== data loaded from global memory is written to LDS after "
                                  << occurrence["instruction_count_to_shared_mem_store"].get<int>() << " instructions." << std::endl;
                        std::cout << "     using the LDS bit in global load instruction might help" << std::endl;
                    }
                }
                
                continue;
            }

            // Candidate global-load instruction
            if (severity == "WARNING" )
            {
                shared_recommend_flag = true;
                const std::string reg_num = occurrence["register"].get<std::string>();

                /*
                 * Print the register-level summary once.
                 *
                 * There may now be several WARNING occurrences for the same
                 * register because each candidate load is stored separately.
                 */
                if (warned_registers.insert(reg_num).second)
                {
                    std::cout << std::endl;
                    std::cout << "==== WARNING" << std::endl;
                    std::cout << "==== since the data at register number " << reg_num << " is accessed multiple "
                            << "times, you could benefit from using" << std::endl;
                    std::cout << "     local memory instead of global memory" << std::endl;
                    std::cout << "==== register number " << reg_num << " has " << occurrence["global_load_count"].get<int>()
                            << " total global load counts and " << occurrence["computation_instruction_count"].get<int>() << " computation "
                            << "instruction counts" << std::endl;
                    std::cout << "==== the following global load instruction (without LDS bit) use the register as vdst "
                            << "register" << std::endl;

                }

                std::cout << "==== global load instruction in file " << occurrence["file_name"].get<std::string>()
                        << " at line " << occurrence["line_number"].get<int>() << std::endl;
                
                if (occurrence["in_for_loop"].get<bool>())
                {
                    std::cout << "==== this global load instruction could be in a loop and "
                            << "hence could perform multiple load operations" << std::endl;

                    // TODO PC stall
                }
            }
        }

        if (!shared_recommend_flag)
        {
            std::cout << std::endl;
            std::cout << "==== INFO" << std::endl;
            std::cout << "==== no global loads found in the kernel which can benifit from using local memory"
                      << std::endl;
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
        std::cout << "==== indicates what percent of the kernel’s duration the LDS was actively executing instructions"
                  << std::endl;
        std::cout << "     (including, but not limited to, load, store and atomic operations"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_12_1_0 << std::endl;
        std::cout << "==== indicates the percentage of SIMDs in the VALU actively issuing LDS instructions, averaged "
                     "over" << std::endl;
        std::cout << "     the lifetime of the kernel"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_12_1_1 << std::endl;
        std::cout << "==== indicates the percentage of active LDS cycles that were spent servicing bank conflicts"
                  << std::endl;
        std::cout << "     " << mtc_obj.ID_12_1_3 << std::endl;

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
        std::cerr << "Usage: " << argv[0]
                  << " <assembly-file> <metrics-dir> <save-as-json> <json-output-dir>\\n";
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "shared_memory");

    json static_result;
    if (!load_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Missing or invalid static shared memory result: "
                  << static_result_file << std::endl;
        return 1;
    }

    std::string mtc_dir = argv[2];
    auto mtc_map = parser_metrics(mtc_dir, assembly);

    int save_as_json = std::strcmp(argv[3], "true") == 0;
    std::string json_out_dir = argv[4];

    json result = analysis_shared_memory(static_result, mtc_map);

    if (save_as_json)
    {
        std::ofstream json_file;
        json_file.open(json_out_dir + "/shared_memory.json");
        json_file << result.dump(4);
        json_file.close();
    }

    return 0;
}
