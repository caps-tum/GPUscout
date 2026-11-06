#include "amdgcn_instructions.hpp"
#include "../utilities/helper.hpp"
#include "../utilities/json.hpp"

#include <unordered_map>
#include <set>
#include <iostream>
#include <fstream>
#include <string>
#include <regex>

using json = nlohmann::json;

struct loc
{
    bool operator<(const loc& loc) const
    {
        return line_num < loc.line_num;
    }

    int line_num;
    std::string file_name;
};

/// @brief struct that holds information about various datatype conversions
struct conv
{
    int I2F_cnt = 0; // integer to floating point conversion
    int F2I_cnt = 0; // floating point to integer conversion
    int F2F_cnt = 0; // floating point to floating point conversion

    std::set<std::pair<loc, std::string>> I2F_line;
    std::set<std::pair<loc, std::string>> F2I_line;
    std::set<std::pair<loc, std::string>> F2F_line;
};

inline std::unordered_map<std::string, conv>
parser_datatype_conversion(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    std::unordered_map<std::string, conv> conv_map;

    conv conv_obj;
    loc loc_obj;

    std::string krn_name;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            std::smatch match;

            if (std::regex_search(line, match, regex_krn_name))
            {
                conv_obj.F2F_cnt = 0;
                conv_obj.F2I_cnt = 0;
                conv_obj.I2F_cnt = 0;
                conv_obj.I2F_line.clear();
                conv_obj.F2I_line.clear();
                conv_obj.F2F_line.clear();

                krn_name = match[1].str();
            }

            if (std::regex_search(line, match, regex_loc))
            {
                loc_obj.line_num = std::stoi(match[2].str());
                loc_obj.file_name = match[1].str();
            }

            // F2F
            if (std::regex_search(line, match, regex_VOP1("((?:v_cvt_f16_f32|v_cvt_f32_f16|v_cvt_f32_f64"
                                                          "|v_cvt_f64_f32)\\w*)")))
            {
                conv_obj.F2F_cnt++;
                conv_obj.F2F_line.insert(std::make_pair(loc_obj, match[match.size() - 2]));
            }

            // I2F
            if (std::regex_search(line, match, regex_VOP1("((?:v_cvt_f16_i16|v_cvt_f16_u16|v_cvt_f32_i32"
                                                          "|v_cvt_f32_u32|v_cvt_f64_i32|v_cvt_f64_u32)\\w*)")))
            {
                conv_obj.I2F_cnt++;
                conv_obj.I2F_line.insert(std::make_pair(loc_obj, match[match.size() - 2]));
            }

            // F2I
            if (std::regex_search(line, match, regex_VOP1("((?:v_cvt_flr_i32_f32|v_cvt_i16_f16|v_cvt_i32_f32"
                                                          "|v_cvt_i32_f64|v_cvt_u16_f16|v_cvt_u32_f32|v_cvt_u32_f64)"
                                                          "\\w*)")))
            {
                conv_obj.F2I_cnt++;
                conv_obj.F2I_line.insert(std::make_pair(loc_obj, match[match.size() - 2]));
            }

            conv_map[krn_name] = conv_obj;
        }
        file.close();
    }
    else
    {
        std::cout << "==== ERROR" << std::endl;
        std::cout << "==== could not open the file " << filename << std::endl;
    }

    return conv_map;
}

/*!
 * Build the reusable static result for the datatype-conversion analysis.
 * "result" contains the information that belongs to the existing final datatype_conversion.json output.
 * "metadata" contains internal information required by later pipeline stages and is not written to the final JSON output.
 */
json build_static_datatype_conversion_result(const std::unordered_map<std::string, conv>& conv_map)
{
    json static_result = {
        {"candidate_kernels", json::array()},
        {"result", json::object()},
        {"metadata", json::object()}
    };

    for (const auto& [krn_name, conv_obj] : conv_map)
    {
        // TODO: check if this is happening
        if (krn_name.empty())
        {
            break;
        }

        json krn_result = {
            {"occurrences", json::array()}
        };

        // F2F conversions
        for (const auto& conversion : conv_obj.F2F_line)
        {
            krn_result["occurrences"].push_back({
                {"severity", "WARNING"},
                {"file_name", std::get<0>(conversion).file_name},
                {"line_number", std::get<0>(conversion).line_num},
                {"pc_offset", std::get<1>(conversion)},
                {"type", "F2F"}
            });
        }

        // I2F conversions
        for (const auto& conversion : conv_obj.I2F_line)
        {
            krn_result["occurrences"].push_back({
                {"severity", "WARNING"},
                {"file_name", std::get<0>(conversion).file_name},
                {"line_number", std::get<0>(conversion).line_num},
                {"pc_offset", std::get<1>(conversion)},
                {"type", "I2F"}
            });
        }

        // F2I conversions
        for (const auto& conversion : conv_obj.F2I_line)
        {
            krn_result["occurrences"].push_back({
                {"severity", "WARNING"},
                {"file_name", std::get<0>(conversion).file_name},
                {"line_number", std::get<0>(conversion).line_num},
                {"pc_offset", std::get<1>(conversion)},
                {"type", "F2I"}
            });
        }

        static_result["result"][krn_name] = krn_result;
        static_result["metadata"][krn_name] = {
            {"F2F_count", conv_obj.F2F_cnt},
            {"I2F_count", conv_obj.I2F_cnt},
            {"F2I_count", conv_obj.F2I_cnt}
        };

        if (krn_result["occurrences"].empty())
        {
            continue;
        }

        static_result["candidate_kernels"].push_back(
        {
            {"name", krn_name},
            {"demangled", get_demangled_kernel(krn_name, "c++filt")}
        });
    }

    return static_result;
}

/*!
 * Return true if at least one datatype-conversion candidate was found
 * during static analysis.
 */
bool has_datatype_conversion_candidate(const json& static_result)
{
    return !static_result["candidate_kernels"].empty();
}

int main(int argc, char **argv)
{
    /*! Static analysis mode:
     *
     *  1. Parse AMDGCN assembly.
     *  2. Build the reusable static result.
     *  3. Preserve the static result for the later full-analysis stage.
     *
     *  exit 0 -> datatype conversion detected
     *  exit 1 -> no datatype conversion detected
     *  exit 2 -> error
     */
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <assembly-file>" << std::endl;
        return 2;
    }

    const std::string assembly = argv[1];
    const auto static_result_file = static_result_path(assembly, "datatype_conversion");

    auto conv_map = parser_datatype_conversion(assembly);
    json static_result = build_static_datatype_conversion_result(conv_map);

    if (!save_static_result(static_result_file, static_result))
    {
        std::cerr << "ERROR: Could not save static datatype conversion result to "
                  << static_result_file << std::endl;
        return 2;
    }

    return has_datatype_conversion_candidate(static_result) ? 0 : 1;
}

