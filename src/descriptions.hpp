#ifndef DESCRIPTIONS_HPP
#define DESCRIPTIONS_HPP

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

// Check https://docs.nvidia.com/nsight-compute/ProfilingGuide/#metrics-reference for stall descriptions
std::unordered_map<std::string, std::string> stall_description(){
    std::unordered_map<std::string, std::string> stall_reason_map;
    stall_reason_map["stalled_barrier"] = "Warp was stalled waiting for sibling warps at a CTA barrier.\n Causes mostly because of code divergence before a barrier, so some warps need to wait a long time until other warps reach the synchronization point.";
    stall_reason_map["stalled_branch"] = "Warp was stalled waiting for a branch target to finish computing.\n Causes mostly becasue of excessive branch jumping.";
    stall_reason_map["stalled_dispatch"] = "Warp was stalled waiting on a dispatch stall.\n Causes mostly because the dispatcher holds back issuing the warp due to other conflicts or events";
    stall_reason_map["stalled_drain"] = "Warp was stalled waiting for all memory instructions to complete so that warp resources can be freed.\n Caused mostly because of lot of data written to memory towards the end of a kernel.";
    stall_reason_map["stalled_imc_miss"] = "Warp was stalled waiting for an immediate constant cache (IMC) miss.\n Caused mostly becasue of serialized constant memory access to different addresses by threads within a warp.";
    stall_reason_map["stalled_lg_throttle"] = "Warp was stalled waiting for the L1 instruction queue for local and global (LG) memory operations.\n Caused mostly becasue of executing local or global memory instructions too frequently.";
    stall_reason_map["stalled_long_scoreboard"] = "Warp was stalled waiting for a scoreboard dependency on a L1 operation.\n Caused mostly because of suboptimal memory access pattern and/or high cache misses.";
    stall_reason_map["stalled_math_pipe_throttle"] = "Warp was stalled waiting for the execution pipe to be available.\n Caused mostly because of active warps executing their instructions on specific, oversubscribed math pipelines.";
    stall_reason_map["stalled_membar"] = "Warp was stalled waiting on a memory barrier.\n Caused mostly because of excessive/unnecessary memory barriers like __syncthreads().";
    stall_reason_map["stalled_mio_throttle"] = "Warp was stalled waiting for the MIO (memory input/output) instructions.\n Caused mostly because of extreme utilization of the MIO pipelines, which include special math instructions and shared memory instructions.";
    stall_reason_map["stalled_misc"] = "Warp was stalled for a miscellaneous hardware reason.\n";
    stall_reason_map["stalled_no_instructions"] = "Warp was stalled waiting to be selected to fetch an instruction or waiting on a cache miss.\n Caused mostly because of short kernels, excessive jumping across large blocks of assembly code.";
    stall_reason_map["stalled_not_selected"] = "Warp was stalled waiting for the micro scheduler to select the warp.\n Caused mostly because of availability of sufficient warps to cover latencies.";
    stall_reason_map["stalled_selected"] = "Warp was selected by the micro scheduler and issued an instruction.\n";
    stall_reason_map["stalled_short_scoreboard"] = "Warp was stalled waiting for a scoreboard dependency on a memory input/ouput operation.\n Caused mostly because of high number of memory operations to shared memory or frequent execution of special math instructions.";
    stall_reason_map["stalled_sleeping"] = "Warp was stalled due to all threads in the warp being in the sleep state.\n Caused mostly because of high number of executed sleep instructions.";
    stall_reason_map["stalled_tex_throttle"] = "Warp was stalled waiting for the L1 instruction queue for texture operation.\n Caused mostly because of extreme utilization of the L1TEX pipeline.";
    stall_reason_map["stalled_wait"] = "Warp was stalled waiting on a fixed latency execution dependency.\n Caused mostly because of an already highly optimized kernel.";

    return stall_reason_map;
}

// https://carpentries-incubator.github.io/lesson-gpu-programming/06-global_local_memory/index.html
// http://cuda-programming.blogspot.com/2013/02/texture-memory-in-cuda-what-is-texture.html
// https://on-demand.gputechconf.com/gtc-express/2011/presentations/texture_webinar_aug_2011.pdf
std::unordered_map<std::string, std::string> memory_description(){
    std::unordered_map<std::string, std::string> memory_map;
    memory_map["global_memory"] = "Global memory is the largest memory space available but it is also slower to access.\n Global memory is accessible by all threads, from all thread blocks";
    memory_map["local_memory"] = "Local memory is a particular type of memory that can be used to store data that does not fit in registers and is private to a thread. All threads executing a kernel will have their own privately allocated local memory.\n It has similar throughput and latency of global memory, but it is much larger than registers.";
    memory_map["shared_memory"] = "Shared memory is faster because it is located on-chip.\n Shared memory is allocated to thread blocks; all threads in a block cooperate by sharing their input data and the intermediate results of their work.";
    memory_map["texture_memory"] = "Texture memory is a read-only memory that can improve performance when memory access patterns have spatial locality (2D/3D). Texture memory is cached on chip and provides higher bandwidth.\n Texture memory is accessible by all threads, from all thread blocks";

    return memory_map;
}


// int main(){
//     std::unordered_map<std::string, std::string> stall_description_map = stall_description();
//     for (const auto& [k,v] : stall_description_map)
//     {
//         std::cout << "Stall name: " << k << ", Description: " << v << std::endl;
//     }
//     std::unordered_map<std::string, std::string> memory_map = memory_description();
//     for (const auto& [k,v] : memory_map)
//     {
//         std::cout << "Memory name: " << k << ", Description: " << v << std::endl;
//     }
    
// }


#endif      // DESCRIPTION_HPP
