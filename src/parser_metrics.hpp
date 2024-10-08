/**
 * Metric analysis based on metric data collected from Nsight Compute CLI
 * Keep the number of metrics as less as possible to minimize tool overhead
 *
 * @author Soumya Sen
 */

#ifndef PARSER_METRICS_HPP
#define PARSER_METRICS_HPP

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <vector>
#include <tuple>
#include <fstream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <memory>
#include <cmath>
#include "utilities/json.hpp"

using json = nlohmann::json;

/// @brief Metric data to collect from Nsight Compute CLI
struct cuda_metrics
{
    double smsp__warps_active;
    double smsp__warp_issue_stalled_barrier_per_warp_active;
    double smsp__warp_issue_stalled_membar_per_warp_active;
    double smsp__warp_issue_stalled_short_scoreboard_per_warp_active;
    double smsp__warp_issue_stalled_wait_per_warp_active;
    double smsp__thread_inst_executed_per_inst_executed;
    double sm__sass_branch_targets;
    double sm__sass_branch_targets_threads_divergent;
    double smsp__warp_issue_stalled_imc_miss_per_warp_active;
    double smsp__warp_issue_stalled_long_scoreboard_per_warp_active;
    double sm__warps_active;
    double smsp__warp_issue_stalled_lg_throttle_per_warp_active;
    double smsp__warp_issue_stalled_mio_throttle_per_warp_active;
    double smsp__warp_issue_stalled_tex_throttle_per_warp_active;
    double sm__sass_inst_executed_op_global_red;
    double sm__sass_inst_executed_op_shared_atom;
    double l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld;
    double sm__sass_inst_executed_op_shared_ld;
    double l1tex__data_pipe_lsu_wavefronts_mem_shared_op_st;
    double sm__sass_inst_executed_op_shared_st;
    double smsp__sass_average_data_bytes_per_wavefront_mem_shared;
    double smsp__inst_executed_op_local_ld;
    double smsp__inst_executed_op_local_st;
    double lts__t_sectors_op_atom;
    double lts__t_sectors_op_read;
    double lts__t_sectors_op_red;
    double lts__t_sectors_op_write;
    double l1tex__t_sector_hit_rate;
    double sm__sass_inst_executed_op_global_ld;
    double l1tex__t_sectors_pipe_lsu_mem_global_op_ld;
    double l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate;
    double lts__t_sector_op_read_hit_rate;
    double l1tex__t_sectors_pipe_lsu_mem_local_op_ld;
    double l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate;
    double l1tex__t_sectors_pipe_lsu_mem_global_op_red;
    double l1tex__t_sectors_pipe_lsu_mem_global_op_atom;
    double l1tex__t_sector_pipe_lsu_mem_global_op_red_hit_rate;
    double l1tex__t_sector_pipe_lsu_mem_global_op_atom_hit_rate;
    double lts__t_sector_op_red_hit_rate;
    double lts__t_sector_op_atom_hit_rate;
    double sm__sass_data_bytes_mem_shared_op_atom;
    double l1tex__m_xbar2l1tex_read_sectors_mem_lg_op_ld_bandwidth;
    double l1tex__average_t_sectors_per_request_pipe_lsu_mem_global_op_ld;
    double smsp__inst_executed_op_global_ld;
    double memory_l2_theoretical_sectors_global;
    double memory_l2_theoretical_sectors_global_ideal;
    double memory_l1_wavefronts_shared;
    double memory_l1_wavefronts_shared_ideal;
    double sm__sass_inst_executed_op_texture;
    double l1tex__t_sectors_pipe_tex_mem_texture;
    double l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate;
    double smsp__sass_average_data_bytes_per_wavefront_mem_shared_op_ld;
};

struct kernel_metrics
{
    int id;
    std::string kernel_name;
    cuda_metrics metrics_list;
};

json load_data_memory_flow(const kernel_metrics &);
json atomic_data_memory_flow(const kernel_metrics &);
json texture_data_memory_flow(const kernel_metrics &);
json shared_data_memory_flow(const kernel_metrics &);
void bypass_L1(const kernel_metrics &);
void coalescing_efficiency(const kernel_metrics &);
json shared_memory_bank_conflict(const kernel_metrics &);
// void stalls_static_analysis_relation(const kernel_metrics&);

/// @brief Parse the cuda metrics file to store the values in variables
/// @param filename cuda metrics file
/// @return stored metric values for each kernel
std::unordered_map<std::string, kernel_metrics> create_metrics(const std::string &filename)
{
    std::vector<std::vector<std::string>> data;

    std::fstream file(filename, std::ios::in);
    if (file.is_open())
    {
        std::vector<std::string> row;
        std::string line, word;

        // This will skip the first three lines (including the header)
        // https://stackoverflow.com/questions/33250380/c-skip-first-line-of-csv-file
        std::getline(file, line);
        std::getline(file, line);
        std::getline(file, line);

        // Read from the next lines (contains the data)
        while (std::getline(file, line))
        {
            row.clear();

            std::stringstream str(line);

            while (std::getline(str, word, '"'))
            {
                row.push_back(word);
            }
            data.push_back(row);
        }
    }
    else
        std::cout << "Could not open the file: " << filename << std::endl;

    // Parsing the metrics file to store the values in the variables
    // Log file content header looks like:
    // "ID","Process ID","Process Name","Host Name","Kernel Name","Kernel Time","Context","Stream","Section Name","Metric Name","Metric Unit","Metric Value"

    cuda_metrics metric_obj;
    std::unordered_map<std::string, kernel_metrics> metric_map; // create a map with the kernel id as the key and the metrics metadata as the value

    // Note: Clean the metrics_list file: remove from begining till the headers (including)

    for (auto i : data)
    {
        // Clean the " character from the metric name and metric value and remove the dot (uses German format here)
        i[25].erase(std::remove(i[25].begin(), i[25].end(), '"'), i[25].end()); // metric name
        i[29].erase(std::remove(i[29].begin(), i[29].end(), '"'), i[29].end()); // metric value
        i[29].erase(std::remove(i[29].begin(), i[29].end(), '.'), i[29].end()); // remove the dot (since in numeral German, dot = comma in English)
        std::replace(i[29].begin(), i[29].end(), ',', '.');                     // replace comma with dot
        // std::cout << i[9] << std::endl;

        // Note: loosing the decimal part of the value since in German it is denoted as comma and we are using comma as delimiter
        if (i[25] == "smsp__warps_activec.sum")
        {
            metric_obj.smsp__warps_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_barrier_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_barrier_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_membar_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_membar_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_short_scoreboard_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_short_scoreboard_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_wait_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_wait_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__thread_inst_executed_per_inst_executed.ratio")
        {
            metric_obj.smsp__thread_inst_executed_per_inst_executed = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_branch_targets.avg")
        {
            metric_obj.sm__sass_branch_targets = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_branch_targets_threads_divergent.avg")
        {
            metric_obj.sm__sass_branch_targets_threads_divergent = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_imc_miss_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_imc_miss_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_long_scoreboard_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "sm__warps_active.avg.pct_of_peak_sustained_active")
        {
            metric_obj.sm__warps_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_lg_throttle_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_lg_throttle_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_mio_throttle_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_mio_throttle_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "smsp__warp_issue_stalled_tex_throttle_per_warp_active.pct")
        {
            metric_obj.smsp__warp_issue_stalled_tex_throttle_per_warp_active = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_inst_executed_op_global_red.sum")
        {
            metric_obj.sm__sass_inst_executed_op_global_red = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_inst_executed_op_shared_atom.sum")
        {
            metric_obj.sm__sass_inst_executed_op_shared_atom = std::stod(i[29]);
        }
        if (i[25] == "l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld.sum")
        {
            metric_obj.l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_inst_executed_op_shared_ld.sum")
        {
            metric_obj.sm__sass_inst_executed_op_shared_ld = std::stod(i[29]);
        }
        if (i[25] == "l1tex__data_pipe_lsu_wavefronts_mem_shared_op_st.sum")
        {
            metric_obj.l1tex__data_pipe_lsu_wavefronts_mem_shared_op_st = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_inst_executed_op_shared_st.sum")
        {
            metric_obj.sm__sass_inst_executed_op_shared_st = std::stod(i[29]);
        }
        if (i[25] == "smsp__sass_average_data_bytes_per_wavefront_mem_shared.pct")
        {
            metric_obj.smsp__sass_average_data_bytes_per_wavefront_mem_shared = std::stod(i[29]);
        }
        if (i[25] == "smsp__inst_executed_op_local_ld.sum")
        {
            metric_obj.smsp__inst_executed_op_local_ld = std::stod(i[29]);
        }
        if (i[25] == "smsp__inst_executed_op_local_st.sum")
        {
            metric_obj.smsp__inst_executed_op_local_st = std::stod(i[29]);
        }
        if (i[25] == "lts__t_sectors_op_atom.sum")
        {
            metric_obj.lts__t_sectors_op_atom = std::stod(i[29]);
        }
        if (i[25] == "lts__t_sectors_op_read.sum")
        {
            metric_obj.lts__t_sectors_op_read = std::stod(i[29]);
        }
        if (i[25] == "lts__t_sectors_op_red.sum")
        {
            metric_obj.lts__t_sectors_op_red = std::stod(i[29]);
        }
        if (i[25] == "lts__t_sectors_op_write.sum")
        {
            metric_obj.lts__t_sectors_op_write = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sector_hit_rate.pct")
        {
            metric_obj.l1tex__t_sector_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_inst_executed_op_global_ld.sum")
        {
            metric_obj.sm__sass_inst_executed_op_global_ld = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum")
        {
            metric_obj.l1tex__t_sectors_pipe_lsu_mem_global_op_ld = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate.pct")
        {
            metric_obj.l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "lts__t_sector_op_read_hit_rate.pct")
        {
            metric_obj.lts__t_sector_op_read_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sectors_pipe_lsu_mem_local_op_ld.sum")
        {
            metric_obj.l1tex__t_sectors_pipe_lsu_mem_local_op_ld = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate.pct")
        {
            metric_obj.l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sectors_pipe_lsu_mem_global_op_red.sum")
        {
            metric_obj.l1tex__t_sectors_pipe_lsu_mem_global_op_red = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sectors_pipe_lsu_mem_global_op_atom.sum")
        {
            metric_obj.l1tex__t_sectors_pipe_lsu_mem_global_op_atom = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sector_pipe_lsu_mem_global_op_red_hit_rate.pct")
        {
            metric_obj.l1tex__t_sector_pipe_lsu_mem_global_op_red_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sector_pipe_lsu_mem_global_op_atom_hit_rate.pct")
        {
            metric_obj.l1tex__t_sector_pipe_lsu_mem_global_op_atom_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "lts__t_sector_op_red_hit_rate.pct")
        {
            metric_obj.lts__t_sector_op_red_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "lts__t_sector_op_atom_hit_rate.pct")
        {
            metric_obj.lts__t_sector_op_atom_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_data_bytes_mem_shared_op_atom.sum")
        {
            metric_obj.sm__sass_data_bytes_mem_shared_op_atom = std::stod(i[29]);
        }
        if (i[25] == "l1tex__m_xbar2l1tex_read_sectors_mem_lg_op_ld.sum.pct_of_peak_sustained_elapsed")
        {
            metric_obj.l1tex__m_xbar2l1tex_read_sectors_mem_lg_op_ld_bandwidth = std::stod(i[29]);
        }
        if (i[25] == "l1tex__average_t_sectors_per_request_pipe_lsu_mem_global_op_ld.ratio")
        {
            metric_obj.l1tex__average_t_sectors_per_request_pipe_lsu_mem_global_op_ld = std::stod(i[29]);
        }
        if (i[25] == "smsp__inst_executed_op_global_ld.sum")
        {
            metric_obj.smsp__inst_executed_op_global_ld = std::stod(i[29]);
        }
        if (i[25] == "memory_l2_theoretical_sectors_global")
        {
            metric_obj.memory_l2_theoretical_sectors_global = std::stod(i[29]);
        }
        if (i[25] == "memory_l2_theoretical_sectors_global_ideal")
        {
            metric_obj.memory_l2_theoretical_sectors_global_ideal = std::stod(i[29]);
        }
        if (i[25] == "memory_l1_wavefronts_shared")
        {
            metric_obj.memory_l1_wavefronts_shared = std::stod(i[29]);
        }
        if (i[25] == "memory_l1_wavefronts_shared_ideal")
        {
            metric_obj.memory_l1_wavefronts_shared_ideal = std::stod(i[29]);
        }
        if (i[25] == "sm__sass_inst_executed_op_texture.sum")
        {
            metric_obj.sm__sass_inst_executed_op_texture = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sectors_pipe_tex_mem_texture.sum")
        {
            metric_obj.l1tex__t_sectors_pipe_tex_mem_texture = std::stod(i[29]);
        }
        if (i[25] == "l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate.pct")
        {
            metric_obj.l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate = std::stod(i[29]);
        }
        if (i[25] == "smsp__sass_average_data_bytes_per_wavefront_mem_shared_op_ld.pct")
        {
            metric_obj.smsp__sass_average_data_bytes_per_wavefront_mem_shared_op_ld = std::stod(i[29]);
        }

        std::string id_name = i[9]; // key of the map is the name of the kernel
        kernel_metrics kernel_obj = {std::stoi(i[1]), i[9], metric_obj};
        metric_map[id_name] = kernel_obj;
    }
    // std::cout << metric_obj.smsp__warp_issue_stalled_long_scoreboard_per_warp_active + metric_obj.smsp__warp_issue_stalled_wait_per_warp_active << std::endl;
    // std::cout << metric_map["bodyForce(Body *, float, int)"].kernel_name << " : " << metric_map["bodyForce(Body *, float, int)"].metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active + metric_map["bodyForce(Body *, float, int)"].metrics_list.smsp__warp_issue_stalled_wait_per_warp_active << std::endl;

    return metric_map;
}

json load_data_memory_flow(const kernel_metrics &all_metrics)
{
    // std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;

    // ---------------- GLOBAL and LOCAL LOAD OPERATIONS ---------------------
    // involves global memory, local memory, L1, L2, DRAM
    std::cout << "Kernel ---- request load data ----> Global Memory " << all_metrics.metrics_list.sm__sass_inst_executed_op_global_ld << " instructions" << std::endl;

    std::cout << "Global memory ---- request load data ----> L1 cache " << 32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_global_op_ld << " bytes" << std::endl;
    std::cout << "L1 Cache miss % (due to global memory load request) " << 100 - all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate << std::endl;
    auto requests_l1_l2_global_ld = (32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_global_op_ld) * (1 - (all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate / 100));
    std::cout << "L1 cache ---- request load data ----> L2 cache (due to global memory load request) " << requests_l1_l2_global_ld << " bytes" << std::endl; // add local memory together (?)

    std::cout << "Local memory used in case of register spilling . . ." << std::endl;
    std::cout << "Local memory ---- request load data ----> L1 cache " << 32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_local_op_ld << " bytes" << std::endl;
    std::cout << "L1 Cache miss % (due to local memory load request) " << 100 - all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate << std::endl;
    auto requests_l1_l2_local_ld = (32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_local_op_ld) * (1 - (all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate / 100));
    std::cout << "L1 cache ---- request load data ----> L2 cache (due to local memory load request) " << requests_l1_l2_local_ld << " bytes" << std::endl; // add local memory together (?)

    std::cout << "L2 Cache miss % (due to L1 load data request) " << 100 - all_metrics.metrics_list.lts__t_sector_op_read_hit_rate << std::endl;
    auto requests_l2_dram_ld = (requests_l1_l2_global_ld + requests_l1_l2_local_ld) * (1 - (all_metrics.metrics_list.lts__t_sector_op_read_hit_rate / 100));
    std::cout << "L2 cache ---- request load data ----> DRAM " << requests_l2_dram_ld << " bytes" << std::endl;

    return {
        {"num_loads", all_metrics.metrics_list.sm__sass_inst_executed_op_global_ld},
        {"global_to_l1_bytes", 32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_global_op_ld},
        {"global_to_l1_cache_miss_perc", all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate},
        {"global_l1_to_l2_bytes", requests_l1_l2_global_ld},
        {"local_to_l1_bytes", 32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_local_op_ld},
        {"local_to_l1_cache_miss_perc", 100 - all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate},
        {"local_l1_to_l2_bytes", requests_l1_l2_local_ld},
        {"l1_to_l2_cache_miss_perc", 100 - all_metrics.metrics_list.lts__t_sector_op_read_hit_rate},
        {"l2_to_dram_bytes", requests_l2_dram_ld}
    };
}

json atomic_data_memory_flow(const kernel_metrics &all_metrics)
{
    // std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;

    // ---------------- ATOMIC OPERATIONS ---------------------
    // involves shared memory, global memory, L1, L2, DRAM
    auto red_atom_requests = all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_global_op_red + all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_global_op_atom;
    std::cout << "Global memory ---- request reduction/atomic data ----> L1 cache " << 32 * red_atom_requests << " bytes" << std::endl;

    auto l1_red_atom_hit_rate = all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_global_op_red_hit_rate + all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_global_op_atom_hit_rate;
    std::cout << "L1 Cache miss % (due to global memory atomic request) " << 100 - l1_red_atom_hit_rate << std::endl;
    auto requests_l1_l2_global_red = (32 * red_atom_requests) * (1 - (l1_red_atom_hit_rate / 100));
    std::cout << "L1 cache ---- request atomic data ----> L2 cache (due to global memory atomic request) " << requests_l1_l2_global_red << " bytes" << std::endl;

    auto lts_red_atom_hit_rate = all_metrics.metrics_list.lts__t_sector_op_red_hit_rate + all_metrics.metrics_list.lts__t_sector_op_atom_hit_rate;
    std::cout << "L2 Cache miss % (due to L1 atomic data request) " << 100 - lts_red_atom_hit_rate << std::endl;
    auto requests_l2_dram_red = (requests_l1_l2_global_red) * (1 - (lts_red_atom_hit_rate / 100));
    std::cout << "L2 cache ---- request atomic data ----> DRAM " << requests_l2_dram_red << " bytes" << std::endl;

    std::cout << "Incase using shared memory for atomics . . . " << std::endl;
    std::cout << "Kernel ---- request atomic data ----> Shared Memory " << all_metrics.metrics_list.sm__sass_data_bytes_mem_shared_op_atom << " bytes" << std::endl;

    return {
        {"global_to_l1_cache_miss_perc", 100 - l1_red_atom_hit_rate},
        {"l1_to_l2_cache_miss_perc", 100 - lts_red_atom_hit_rate},
        {"l1_to_l2__bytes", requests_l1_l2_global_red},
        {"l2_to_dram_bytes", requests_l2_dram_red},
        {"global_to_l1_red_atom_bytes", 32 * red_atom_requests},
        {"kernel_to_shared_bytes", all_metrics.metrics_list.sm__sass_data_bytes_mem_shared_op_atom}
    };
}

json texture_data_memory_flow(const kernel_metrics &all_metrics)
{
    // std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;

    // ---------------- TEXTURE LOAD OPERATIONS ---------------------
    // involves texture memory, L1, L2, DRAM
    std::cout << "Kernel ---- request load data ----> Texture Memory " << all_metrics.metrics_list.sm__sass_inst_executed_op_texture << " instructions" << std::endl;

    std::cout << "Texture memory ---- request load data ----> L1 cache " << 32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_tex_mem_texture << " bytes" << std::endl;
    std::cout << "L1 Cache miss % (due to texture memory load request) " << 100 - all_metrics.metrics_list.l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate << std::endl;
    auto requests_l1_l2_texture_ld = (32 * all_metrics.metrics_list.l1tex__t_sectors_pipe_tex_mem_texture) * (1 - (all_metrics.metrics_list.l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate / 100));
    std::cout << "L1 cache ---- request load data ----> L2 cache (due to texture memory load request) " << requests_l1_l2_texture_ld << " bytes" << std::endl; // add local memory together (?)

    std::cout << "L2 Cache miss % (due to L1 load data request) " << 100 - all_metrics.metrics_list.lts__t_sector_op_read_hit_rate << std::endl; // no metrics found for texture-only L2 cache hit %
    auto requests_l2_dram_ld = (requests_l1_l2_texture_ld) * (1 - (all_metrics.metrics_list.lts__t_sector_op_read_hit_rate / 100));
    std::cout << "L2 cache ---- request load data ----> DRAM " << requests_l2_dram_ld << " bytes" << std::endl;

    return {
        {"kernel_to_tex_instr", all_metrics.metrics_list.sm__sass_inst_executed_op_texture},
        {"tex_to_l1_bytes", all_metrics.metrics_list.l1tex__t_sectors_pipe_tex_mem_texture},
        {"tex_to_l1_cache_miss_perc", 100 - all_metrics.metrics_list.l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate},
        {"l1_to_l2_cache_miss_perc", 100 - all_metrics.metrics_list.lts__t_sector_op_read_hit_rate},
        {"l1_to_l2__bytes", requests_l1_l2_texture_ld},
        {"l2_to_dram_bytes", requests_l2_dram_ld},
    };
}

json shared_data_memory_flow(const kernel_metrics &all_metrics)
{
    // std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;

    // ---------------- SHARED MEMORY LOAD OPERATIONS ---------------------
    // involves shared memory
    std::cout << "Kernel ---- request load data ----> Shared Memory " << all_metrics.metrics_list.sm__sass_inst_executed_op_shared_ld << " instructions" << std::endl;

    return {
        {"shared_mem_load_operations", all_metrics.metrics_list.sm__sass_inst_executed_op_shared_ld}
    };
}

void bypass_L1(const kernel_metrics &all_metrics)
{
    std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;

    // Set the thresholds to 0 for display in stdout
    double threshold_l1_hit = 40.0;         // assuming threshold of L1 cache hit rate 40%
    double threshold_l1_l2_bandwidth = 0.0; // assuming threshold of L1-L2 bandwidth compared to average peak sustained 40%
    // if (all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate < threshold_l1_hit)
    {
        // A memory "request" is an instruction which accesses memory, and a "transaction" is the movement of a unit of data between two regions of memory.
        std::cout << "Low L1 cache hit: " << all_metrics.metrics_list.l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate << " %" << std::endl;

        if (all_metrics.metrics_list.l1tex__m_xbar2l1tex_read_sectors_mem_lg_op_ld_bandwidth > threshold_l1_l2_bandwidth)
        {
            std::cout << "High L1-L2 bandwidth wrt to sustained peak load : " << all_metrics.metrics_list.l1tex__m_xbar2l1tex_read_sectors_mem_lg_op_ld_bandwidth << " %" << std::endl;
            /*
            Check https://docs.nvidia.com/nsight-compute/ProfilingGuide/
            % Peak to L2: Percentage of peak utilization of the L1-to-XBAR interface, used to send L2 cache requests.
            If this number is high, the workload is likely dominated by scattered {writes, atomics, reductions},
            which can increase the latency and cause warp stalls.
            */
            std::cout << "L1 bytes transacted per request made: " << 32 * all_metrics.metrics_list.l1tex__average_t_sectors_per_request_pipe_lsu_mem_global_op_ld << " bytes/request" << std::endl;
            std::cout << "For low L1 cache hit, high L1-l2 bandwidth and high L1 transactions per request, consider bypassing L1 and go directly to L2" << std::endl;
        }
    }
}

/**
 * @brief
 * coal_eff - percentage of threads in a warp that make use of a particular load operation
 * excess_sectors_global - additional amount of data transferred from L2 to L1 compared to what was loaded from L1 to the threads
 */
void coalescing_efficiency(const kernel_metrics &all_metrics)
{
    std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;

    // coalescing_efficiency is given as ratio of total number of global memory instructions executed to corresponding number of global memory transactions issued
    // https://link.springer.com/content/pdf/10.1007/s10766-022-00729-2.pdf?pdf=button%20sticky
    double coal_eff = (all_metrics.metrics_list.smsp__inst_executed_op_global_ld) / (all_metrics.metrics_list.l1tex__t_sectors_pipe_lsu_mem_global_op_ld);
    // auto coal_eff_threshold = 0.04;     // threshold set for almost fully divergent code (which is 1 thread/32 threads ~ 3%)
    auto coal_eff_threshold = 0.9; // for demo purposes, set at 90%
    // if (coal_eff < coal_eff_threshold)
    {
        std::cout << "Low Coalescing efficiency: " << coal_eff * 100 << " %" << std::endl;
        // For global memory coalescing
        double excess_sectors_global = all_metrics.metrics_list.memory_l2_theoretical_sectors_global - all_metrics.metrics_list.memory_l2_theoretical_sectors_global_ideal;
        if (excess_sectors_global > 0)
        {
            std::cout << "Excessive bytes requested in L2 from global memory: " << 32 * excess_sectors_global << " bytes. This can be due to uncoalesced access to global memory" << std::endl;
        }
        // For shared memory coalescing
        double excess_sectors_shared = all_metrics.metrics_list.memory_l1_wavefronts_shared - all_metrics.metrics_list.memory_l1_wavefronts_shared_ideal;
        if (excess_sectors_shared > 0)
        {
            std::cout << "Excessive bytes requested in L1 from shared memory: " << 32 * excess_sectors_shared << " bytes. This can be due to uncoalesced access to shared memory" << std::endl;
            // DOUBT: can we multiply by 32 to get the bytes? For the shared memory case, we get wavefronts and not sectors
        }
    }
}

json shared_memory_bank_conflict(const kernel_metrics &all_metrics)
{
    json result;
    // std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;
    // https://github.com/Kobzol/hardware-effects-gpu/blob/master/bank-conflicts/README.md
    // Incase of bank conflicts, the shared memory efficiency will be quite low
    std::cout << "Shared memory efficiency for load operations: " << all_metrics.metrics_list.smsp__sass_average_data_bytes_per_wavefront_mem_shared_op_ld << " %" << std::endl;
    result["shared_mem_load_efficiency_perc"] = all_metrics.metrics_list.smsp__sass_average_data_bytes_per_wavefront_mem_shared_op_ld;
    result["shared_mem_data_requests"] = all_metrics.metrics_list.sm__sass_inst_executed_op_shared_ld;
    // Incase of n-way bank conflict, shared_load_transactions_per_request should be n
    if (all_metrics.metrics_list.sm__sass_inst_executed_op_shared_ld == 0)
    {
        std::cout << "No shared memory data request made" << std::endl;
    }
    else
    {
        double shared_load_transactions_per_request = std::floor(1.0 * all_metrics.metrics_list.l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld / all_metrics.metrics_list.sm__sass_inst_executed_op_shared_ld);
        (shared_load_transactions_per_request == 1) ? std::cout << "No bank conflicts detected" << std::endl : std::cout << shared_load_transactions_per_request << "-way bank conflict detected in shared memory acccess" << std::endl;
        result["bank_conflict"] = (shared_load_transactions_per_request == 1) ? 0 : shared_load_transactions_per_request;
    }

    return result;
}

// Used/unused metric analysis
// Copy the relevant parts of the analysis in the merge_analysis codes
// void stalls_static_analysis_relation(const kernel_metrics& all_metrics){
//     std::cout << "For kernel name: " << all_metrics.kernel_name << std::endl;

//     // map the SASS code static analysis with the kernel-level warp stall reasons
//     // global_mem_atomics_analysis
//     std::cout << "For global atomics, check LG Throttle: " << all_metrics.metrics_list.smsp__warp_issue_stalled_lg_throttle_per_warp_active << " %" << std::endl;
//     std::cout << "For global atomics, check Long Scoreboard: " << all_metrics.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;
//     std::cout << "For high values of the above stalls, advice user to use shared memory instead for atomics" << std::endl;
//     std::cout << "For shared atomics, check MIO throttle: " << all_metrics.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " %" << std::endl;

//     // datatype_conversions_analysis
//     std::cout << "For datatype conversions, check Tex Throttle: " << all_metrics.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active << " %" << std::endl;

//     // branches_detection
//     double branch_divergence_percent = all_metrics.metrics_list.sm__sass_branch_targets_threads_divergent/all_metrics.metrics_list.sm__sass_branch_targets;
//     std::cout << "Average number of branches that diverge: " << branch_divergence_percent << " %" << std::endl;

//     // register_spilling_analysis
//     std::cout << "For register spilling, check Long Scoreboard: " << all_metrics.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;
//     auto local_load_store = all_metrics.metrics_list.smsp__inst_executed_op_local_ld + all_metrics.metrics_list.smsp__inst_executed_op_local_st;
//     int total_SM = 16;
//     auto estimated_l2_queries_lmem_allSM = 2*4*total_SM*((1-(all_metrics.metrics_list.l1tex__t_sector_hit_rate/100))*local_load_store);
//     auto total_l2_queries = all_metrics.metrics_list.lts__t_sectors_op_read + all_metrics.metrics_list.lts__t_sectors_op_write + all_metrics.metrics_list.lts__t_sectors_op_atom + all_metrics.metrics_list.lts__t_sectors_op_red;
//     auto l2_queries_lmem_percent = estimated_l2_queries_lmem_allSM/total_l2_queries;
//     std::cout << "Percentage of total L2 queries due to LMEM: " << l2_queries_lmem_percent << " %" << std::endl;
//     std::cout << "If the above percentage is high, it means the memory traffic between the SMs and L2 cache is mostly due to LMEM (need to contain register spills)" << std::endl;

//     // restrict_analysis
//     std::cout << "If using __restrict__ (constant memory), check IMC miss: " << all_metrics.metrics_list.smsp__warp_issue_stalled_imc_miss_per_warp_active << " %" << std::endl;

//     // use_texture_memory_analysis
//     std::cout << "If using texture memory, check Tex Throttle: " << all_metrics.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active << " %" << std::endl;
//     std::cout << "If using texture memory, check Long Scoreboard: " << all_metrics.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;

//     // vectorized_analysis
//     std::cout << "For not fully utilizing required resources using non-vectorized load/store, check Long Scoreboard: " << all_metrics.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;
//     std::cout << "Using vectorized load increases the register pressure and hence might affect occupancy" << std::endl;
//     std::cout << "Occupancy achieved: " << all_metrics.metrics_list.sm__warps_active << std::endl;

//    // use_shared_memory_analysis
//     std::cout << "If using shared memory, check Long Scoreboard: " << all_metrics.metrics_list.smsp__warp_issue_stalled_long_scoreboard_per_warp_active << " %" << std::endl;
//     std::cout << "If using shared memory, check MIO throttle: " << all_metrics.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " %" << std::endl;

//    // datatype_conversions
//    // https://forums.developer.nvidia.com/t/sass-for-f2i-and-i2f-conversions/238320/3
//      std::cout << "For F2F.F64.F32 conversions, check Tex throttle: " << all_metrics.metrics_list.smsp__warp_issue_stalled_tex_throttle_per_warp_active << " %" << std::endl;
//      std::cout << "For I2F and F2F (32 bit only) conversions, check MIO throttle: " << all_metrics.metrics_list.smsp__warp_issue_stalled_mio_throttle_per_warp_active << " %" << std::endl;
//      std::cout << "For I2F and F2F (32 bit only) conversions, check Short Scoreboard: " << all_metrics.metrics_list.smsp__warp_issue_stalled_short_scoreboard_per_warp_active << " %" << std::endl;
// }

#endif // PARSER_METRICS_HPP
