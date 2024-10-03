#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <fstream>
#include <string>
#include <filesystem>
#include "parser_pcsampling.hpp"
#include "utilities/json.hpp"

using json = nlohmann::json;

int main(int argc, char **argv)
{
    std::string json_files_dir = argv[1];
    std::string output_file_path = argv[2];
    std::string sass_file = argv[3];
    std::string pc_samples_file = argv[4];
    int copy_sources = std::strcmp(argv[5], "true") == 0;
    std::string ptx_file = argv[6];
    std::string run_prefix = argv[7];
    std::string tmp_dir = argv[8];

    json result = {
        {"analyses", json::object()},
        {"stalls", json::object()},
        {"source_files", json::object()}
    };

    // Add individual analysis results to result file
    for (const auto& file : std::filesystem::directory_iterator(json_files_dir)) {
        if (file.is_directory())
            continue;
        std::string path = file.path();
        std::string filename = path.substr(path.find_last_of("/") + 1);
        std::string extension = filename.substr(filename.find_last_of(".") + 1);
        filename = filename.substr(0, filename.length() - 5);

        if (extension.compare("json") != 0)
            continue;

        std::ifstream analysis_file(path);
        json analysis_content = json::parse(analysis_file);
        result["analyses"][filename] = analysis_content;
    }

    // Add stall information to result file
    std::unordered_map<std::string, std::vector<pc_issue_samples>> stall_map = get_warp_stalls(pc_samples_file, sass_file, analysis_kind::ALL);
    for (auto [k_pc, v_pc] : stall_map) {
        result["stalls"][k_pc] = json::object();

        for (auto sample : v_pc) {
            result["stalls"][k_pc][std::to_string(sample.pc_offset)] = {
                {"line_number", sample.line_number},
                {"stalls", sample.stall_name_count_pair}
            };
        }
    }

    // Copy source files and save their mapping
    if (copy_sources) {
        std::ifstream ptx_content(ptx_file);
        std::string line;
        std::string tmp_string;

        std::ostringstream rm_source_folder("");
        rm_source_folder << "rm -rf " << tmp_dir << "/sources/" << run_prefix;
        std::system(rm_source_folder.str().c_str());
        std::ostringstream mk_source_folder("");
        mk_source_folder << "mkdir -p " << tmp_dir << "/sources/" << run_prefix;
        std::system(mk_source_folder.str().c_str());

        if (ptx_content.is_open()) {
            while (std::getline(ptx_content, line)) {
                if (line.find(".file") != std::string::npos) {
                    std::istringstream ss(line);
                    std::getline(ss, tmp_string, '\t');
                    std::getline(ss, tmp_string, ' ');
                    int file_index = std::stoi(tmp_string);
                    std::getline(ss, tmp_string, '"');
                    std::getline(ss, tmp_string, '"');

                    std::ostringstream copied_file_name("");
                    copied_file_name << "sources/";
                    copied_file_name << run_prefix << "/";
                    copied_file_name << std::to_string(file_index);
                    copied_file_name << ".cu";

                    std::ostringstream cp_command("");
                    cp_command << "cp ";
                    cp_command << tmp_string << " ";
                    cp_command << tmp_dir << "/sources/" << run_prefix << "/" << std::to_string(file_index) << ".cu";
                    std::system(cp_command.str().c_str());

                    result["source_files"][copied_file_name.str()] = tmp_string;
                }
            }
        }
    }

    std::ofstream result_file;
    result_file.open(output_file_path);

    if (result_file.is_open()) {
        result_file << result.dump(4);
        result_file.close();
    }
}
