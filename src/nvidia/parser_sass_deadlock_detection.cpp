/**
 * SASS code analysis to detect possibility of a deadlock in code
 *
 * @author Soumya Sen
 */

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <vector>
#include <tuple>
#include <set>
#include <fstream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <memory>

#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

using json = nlohmann::json;

struct deadlock_detect
{
    bool deadlock_detect_flag;
};

/// @brief Detects possibility of a deadlock in the user code
/// @param filename Disassembled SASS file
/// @return Map containing deadlock detection flag for each kernel
std::unordered_map<std::string, deadlock_detect> deadlock_detection_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    bool inside_cas, branch_in_cas, sync_in_cas = false;
    deadlock_detect deadlock_detect_obj;
    std::unordered_map<std::string, deadlock_detect> counter_map;

    std::string k_sass;
    int code_line_number;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                inside_cas = false;
                branch_in_cas = false;
                sync_in_cas = false;
                deadlock_detect_obj.deadlock_detect_flag = false;

                // https://cplusplus.com/reference/string/string/erase/     - erase part of a string
                line.erase(line.begin(), line.begin() + 16); // erase the first 16 character of the name of the kernel
                line.erase(line.end() - 15, line.end());     // erase the last 15 character of the name of the kernel
                k_sass = line;
                // std::cout << k_sass << std::endl;
            }

            if (line.find(" line ") != std::string::npos)
            {
                code_line_number = std::stoi(line.substr(line.find("line ") + 5)); // saving the current line number
            }

            if (line.find("ATOM.E.CAS") != std::string::npos)
            {
                inside_cas = true;
            }

            if ((line.find("@P") != std::string::npos) && (line.find(" BRA ") != std::string::npos) && (inside_cas))
            {
                branch_in_cas = true;
            }

            if ((line.find("SYNC") != std::string::npos) && (branch_in_cas))
            {
                sync_in_cas = true;
                // std::cout << "WARNING   ::  Deadlock possibility in kernel: " << k_sass << std::endl;
                deadlock_detect_obj.deadlock_detect_flag = true;
            }

            if (line.find("ATOM.E.EXCH") != std::string::npos)
            {
                inside_cas = false;
            }

            counter_map[k_sass] = deadlock_detect_obj;
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    return counter_map;
}

/*!
 * Build the reusable static result for deadlock detection.
 *
 * candidate_kernels: Kernels for which a possible deadlock was detected.
 * result: Final GUI-facing deadlock flag for every parsed kernel.
 * @param detection_map Analysis for deadlock detection
 */
json build_static_deadlock_detection_result(const std::unordered_map<std::string, deadlock_detect>& detection_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()}
    };

    for (const auto& [k_sass, v_sass] : detection_map)
    {
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass.empty())
        {
            continue;
        }

        static_result["result"][k_sass] = {
            {"metrics", {
                {"deadlock_detect_flag", v_sass.deadlock_detect_flag}
            }}
        };

        if (v_sass.deadlock_detect_flag)
        {
            static_result["candidate_kernels"].push_back({
                {"name", k_sass},
                {"demangled", get_demangled_kernel(k_sass)}
            });
        }
    }

    return static_result;
}

bool has_deadlock_detection_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <sass-file> <ptx-file>\n";
        return 2;
    }

    /*! Static detecion mode:
     *
     * exit 0 -> possible deadlock detected
     * exit 1 -> no possible deadlock detected
     * exit 2 -> invalid arguments or static-result write failure
     */
    const std::string assembly = argv[1];
    const auto detection_map = deadlock_detection_analysis(assembly);
    const json static_result = build_static_deadlock_detection_result(detection_map);

    const auto static_result_file = static_result_path(assembly, "deadlock_detection");

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr
            << "ERROR: Could not save static deadlock-detection result to "
            << static_result_file << std::endl;
        return 2;
    }

    return has_deadlock_detection_candidate(static_result) ? 0 : 1;
}
