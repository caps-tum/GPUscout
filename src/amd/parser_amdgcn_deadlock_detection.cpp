#include "amdgcn_instructions.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <unordered_map>
#include <iostream>
#include <fstream>
#include <string>
#include <regex>

using json = nlohmann::json;

std::unordered_map<std::string, bool>
parser_deadlock_detection(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    std::unordered_map<std::string, bool> deadlock_map;

    bool deadlock_potential = false;
    bool inside_cas, brc_in_cas, sync_in_cas = false;

    std::string krn_name;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            std::smatch match;

            if (std::regex_search(line, match, regex_krn_name))
            {
                deadlock_potential = false;
                inside_cas = false;
                brc_in_cas = false;
                sync_in_cas = false;

                krn_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_FLAT("(?:flat|global)_atomic_cmpswap\\w*")) ||
                std::regex_search(line, match, regex_MIMG("image_atomic_cmpswap\\w*", "([\\w\\[\\]:\\-]+)")) ||
                std::regex_search(line, match, regex_MUBUF("buffer_atomic_cmpswap\\w*", "([\\w\\[\\]:\\-]+)")))
            {
                inside_cas = true;
            }

            if (std::regex_search(line, match, regex_SOPP("s_cbranch\\w*")) && (inside_cas))
            {
                brc_in_cas = true;
            }

            if (std::regex_search(line, match, regex_SOPP("s_barrier")) && (brc_in_cas))
            {
                sync_in_cas = true;
                deadlock_potential = true;
            }

            if (std::regex_search(line, match, regex_FLAT("(?:flat|global)_atomic_swap\\w*")) ||
                std::regex_search(line, match, regex_MIMG("image_atomic_swap\\w*", "([\\w\\[\\]:\\-]+)")) ||
                std::regex_search(line, match, regex_MUBUF("buffer_atomic_swap\\w*", "([\\w\\[\\]:\\-]+)")))
            {
                inside_cas = false;
            }

            if (line.find("ATOM.E.EXCH") != std::string::npos)
            {
                inside_cas = false;
            }

            deadlock_map[krn_name] = deadlock_potential;
        }
        file.close();
    }
    else
    {
        std::cout << "Error :: Could not open the file: " << filename << std::endl;
    }

    return deadlock_map;
}

/*!
 * Build the reusable static result for deadlock detection.
 * The result contains all information required by the final deadlock_detection.json output
 */
json build_static_deadlock_detection_result(const std::unordered_map<std::string, bool>& deadlock_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()}
    };

    for (const auto& [krn_name, deadlock] : deadlock_map)
    {
        // TODO: check if this is happening
        if (krn_name.empty())
        {
            break;
        }

        json krn_result;

        krn_result["metrics"] = { {"deadlock_detect_flag", deadlock}};

        static_result["result"][krn_name] = krn_result;

        if (deadlock)
        {
            static_result["candidate_kernels"].push_back(
            {
                {"name", krn_name},
                {"demangled", get_demangled_kernel(krn_name, "c++filt")}
            });
        }
    }

    return static_result;
}

/*!
 * Return true if at least one possible deadlock was detected.
 */
bool has_deadlock_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char **argv)
{
    /*! Static analysis mode:
     *
     *  1. Parse AMDGCN assembly.
     *  2. Build the reusable static result.
     *  3. Preserve the result for the later full-analysis stage.
     *
     *  exit 0 -> deadlock candidate detected
     *  exit 1 -> no deadlock candidate detected
     *  exit 2 -> error
     */
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <assembly-file>" << std::endl;
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "deadlock_detection");

    const auto deadlock_map = parser_deadlock_detection(assembly);
    const json static_result = build_static_deadlock_detection_result(deadlock_map);

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static deadlock detection result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_deadlock_candidate(static_result) ? 0 : 1;
}
