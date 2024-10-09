#include "parser_metrics.hpp"
#include "parser_pcsampling.hpp"
#include "utilities/json.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <format>

using json = nlohmann::json;

int main(int argc, char **argv) 
{
    std::string json_files_dir = argv[1];
    std::string output_file_path = argv[2];
    std::string sass_file = argv[3];
    std::string sass_register_file = argv[4];
    std::string ptx_file = argv[5];
    std::string pc_samples_file = argv[6];

    json result = {
        {"analyses", json::object()},
        {"stalls", json::object()},
        {"binary_files", {
            {"sass", ""},
            {"sass_registers", ""},
            {"ptx", ""},
        }},
        {"source_files", json::object()}
    };

    // Add individual analysis results to result file
    for (const auto &file : std::filesystem::directory_iterator(json_files_dir)) 
    {
        if (file.is_directory())
            continue;
        std::string path = file.path();
        std::string filename = path.substr(path.find_last_of("/") + 1);

        if (filename.find(".json") == std::string::npos)
            continue;

        filename = filename.substr(0, filename.length() - 5);
        std::ifstream analysis_file(path);
        if (analysis_file.is_open()) {
            result["analyses"][filename] = json::parse(analysis_file);
        }
        analysis_file.close();
    }

    // Add stall information to result file
    std::unordered_map<std::string, std::vector<pc_issue_samples>> stall_map = get_warp_stalls(pc_samples_file, sass_file, analysis_kind::ALL);
    for (auto [k_pc, v_pc] : stall_map) 
    {
        result["stalls"][k_pc] = json::array();

        for (auto sample : v_pc) 
        {
            result["stalls"][k_pc].push_back({
                {"line_number", sample.line_number},
                {"pc_offset", std::format("{:x}", sample.pc_offset)},
                {"stalls", sample.stall_name_count_pair}
            });
        }
    }

    // Copy source files and save their mapping
    // Get source files used in ptx
    std::ifstream ptx_content(ptx_file);
    std::string line;
    std::string file_content = "";
    std::string tmp_string;

    if (ptx_content.is_open()) {
        while (std::getline(ptx_content, line)) {
            file_content += line + '\n';
            if (line.find(".file") != std::string::npos) {
                // .file	1 "/home/tobias/Coding/Studium/cuda-scripts/memcpy.cu"
                std::istringstream ss(line);
                std::getline(ss, tmp_string, '\t');
                std::getline(ss, tmp_string, '"');
                std::getline(ss, tmp_string, '"');

                std::ifstream source_file(tmp_string);
                if (source_file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(source_file)), (std::istreambuf_iterator<char>()));
                    result["source_files"][tmp_string] = content;
                }
                source_file.close();
            }
        }
    }
    result["binary_files"]["ptx"] = file_content;
    ptx_content.close();

    // Get source files used in sass and are not already stored
    std::ifstream sass_content(sass_file);
    tmp_string.clear();
    file_content.clear();

    if (sass_content.is_open()) {
        while (std::getline(sass_content, line)) {
            file_content += line + '\n';
            if (line.find("File \"") != std::string::npos) {
                // //## File "/home/tobias/Coding/Studium/cuda-scripts/stencil.cu", line 7
                std::istringstream ss(line);
                std::getline(ss, tmp_string, '\t');
                std::getline(ss, tmp_string, '\"');
                std::getline(ss, tmp_string, '\"');

                if (result["source_files"].find(tmp_string) != result["source_files"].end()) {
                    continue;
                }             

                std::ifstream source_file(tmp_string);
                if (source_file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(source_file)), (std::istreambuf_iterator<char>()));
                    result["source_files"][tmp_string] = content;
                }
                source_file.close();
            }
        }
    }
    result["binary_files"]["sass"] = file_content;
    sass_content.close();

    std::ifstream sass_registers(tmp_string);
    if (sass_registers.is_open()) {
        std::string content((std::istreambuf_iterator<char>(sass_registers)), (std::istreambuf_iterator<char>()));
        result["binary_files"]["sass_registers"] = content;
    }
    sass_registers.close();

    std::ofstream result_file;
    result_file.open(output_file_path);

    if (result_file.is_open()) {
        result_file << result.dump();
        result_file.close();
    }
}
