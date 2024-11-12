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

// https://www.jeremymorgan.com/tutorials/c-programming/how-to-capture-the-output-of-a-linux-command-in-c/
std::string get_demangled_kernel(std::string kernel_name) {
    std::string command = "cu++filt -p " + kernel_name;
    std::string result;
    FILE* stream;
    const int max_buffer = 256;
    char buffer[max_buffer];

    stream = popen(command.c_str(), "r");

    if (stream) {
        while (!feof(stream)) {
            if (fgets(buffer, max_buffer, stream) != NULL)
                result.append(buffer);
        }
        pclose(stream);
    }
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());     
    return result;
}

int main(int argc, char **argv) 
{
    std::string json_files_dir = argv[1];
    std::string output_file_path = argv[2];
    std::string sass_file = argv[3];
    std::string sass_register_file = argv[4];
    std::string ptx_file = argv[5];
    std::string pc_samples_file = argv[6];
    std::string metrics_file = argv[7];

    json result = {
        {"kernels", json::object()},
        {"analyses", json::object()},
        {"metrics", json::object()},
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

            for (auto& kernel : result["analyses"][filename].items()) {
                if (!result["kernels"].contains(kernel.key())) {
                    result["kernels"][kernel.key()] = get_demangled_kernel(kernel.key());
                }
            }
        }
        analysis_file.close();
    }

    // Add metrics
    std::unordered_map<std::string, kernel_metrics> metric_map = create_metrics(metrics_file);
    json json_metrics = {};
    for (auto [k_metric, v_metric] : metric_map) {
        json_metrics[v_metric.kernel_name] = v_metric.metrics_list;
        json_metrics[v_metric.kernel_name]["atomic_data_memory_flow"] = atomic_data_memory_flow(v_metric);
        json_metrics[v_metric.kernel_name]["load_data_memory_flow"] = load_data_memory_flow(v_metric);
        json_metrics[v_metric.kernel_name]["shared_data_memory_flow"] = shared_data_memory_flow(v_metric);
        json_metrics[v_metric.kernel_name]["shared_memory_bank_conflict"] = shared_memory_bank_conflict(v_metric);
        json_metrics[v_metric.kernel_name]["texture_data_memory_flow"] = texture_data_memory_flow(v_metric);
        json_metrics[v_metric.kernel_name]["global_data_per_instruction"] = global_data_per_instr(v_metric);
        json_metrics[v_metric.kernel_name]["l2_queries"] = l2_query_information(v_metric);
    }
    result["metrics"] = json_metrics;

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

    std::ifstream sass_registers(sass_register_file);
    if (sass_registers.is_open()) {
        std::string content((std::istreambuf_iterator<char>(sass_registers)), (std::istreambuf_iterator<char>()));
        result["binary_files"]["sass_registers"] = content;
    }
    sass_registers.close();

    std::ofstream result_file;
    std::string save_file_path = output_file_path;
    int index = 1;
    while (std::filesystem::exists(save_file_path + ".json")) {
        save_file_path = output_file_path + " (" + std::to_string(index++) + ")";
    }
    result_file.open(save_file_path + ".json");

    if (result_file.is_open()) {
        result_file << result.dump(4);
        result_file.close();
    }
}
