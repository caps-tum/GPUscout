/**
 * For each SASS instruction, currently used (or live) registers saved
 * Additonally computes if there is an increase in register usage compared to last SASS instruction
 * Lower the number of registers used, more is the occupancy and more threads can be run per block
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

/// @brief Each SASS instruction contains number of currently active registers, corresponding to the pcOffset of the instruction
struct live_registers
{
    std::string pcOffset;
    int gen_reg;
    int pred_reg;
    int u_gen_reg;
    int change_reg_from_last; // change in number of registers compared to the last SASS instruction
};

std::string get_pcOffset_sass_registers(std::string line)
{
    //         /*0080*/                   ISETP.GE.U32.OR P0, PT, R5, c[0x0][0x170], P0 ;                           // |  3 |  1  |     |       -> extract 0080
    std::string substr;

    std::istringstream ss(line);
    std::getline(ss, substr, '*');
    std::getline(ss, substr, '*');

    return substr;
}

std::vector<int> get_all_registers(std::string line)
{
    //         /*0080*/                   ISETP.GE.U32.OR P0, PT, R5, c[0x0][0x170], P0 ;                           // |  3 |  1  |     |       -> extract 3,1,0
    std::vector<int> reg_vec(3, 0);
    std::string substr;

    // remove everything in line prior to // |
    line = line.erase(0, line.find("// |"));

    std::istringstream ss(line);
    std::getline(ss, substr, '|');
    std::getline(ss, substr, '|');
    std::string remove_chars = " ";
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());
    reg_vec[0] = (substr != "") ? std::stoi(substr) : 0;
    std::getline(ss, substr, '|');
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());
    reg_vec[1] = (substr != "") ? std::stoi(substr) : 0;
    std::getline(ss, substr, '|');
    substr.erase(std::remove_if(substr.begin(), substr.end(), [&remove_chars](const char &c)
                                { return remove_chars.find(c) != std::string::npos; }),
                 substr.end());
    reg_vec[2] = (substr != "") ? std::stoi(substr) : 0;

    return reg_vec;
}

/// @brief For every kernel, stores a vector of live registers
/// @param filename of the sass
/// @return mapping of each kernel with a vector of live registers
std::unordered_map<std::string, std::vector<live_registers>> live_registers_analysis(const std::string &filename)
{
    std::string line;
    std::fstream file(filename, std::ios::in);

    live_registers counter_obj;
    std::unordered_map<std::string, std::vector<live_registers>> counter_map;
    std::vector<live_registers> live_registers_vec;
    std::string kernel_name;
    int code_line_number, last_inst_register_count;

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            if (line.find(".section	.text.") != std::string::npos) // denotes start of the kernel
            {
                counter_obj.gen_reg = 0;
                counter_obj.pred_reg = 0;
                counter_obj.u_gen_reg = 0;
                counter_obj.pcOffset = "";
                counter_obj.change_reg_from_last = 0;
                live_registers_vec.clear();

                last_inst_register_count = 0;

                // https://cplusplus.com/reference/string/string/erase/     - erase part of a string
                line.erase(line.begin(), line.begin() + 16); // erase the first 16 character of the name of the kernel
                line.erase(line.end() - 15, line.end());     // erase the last 15 character of the name of the kernel
                kernel_name = line;
                // std::cout << kernel_name << std::endl;
            }

            if (line.find(" line ") != std::string::npos)
            {
                code_line_number = std::stoi(line.substr(line.find("line ") + 5)); // saving the current line number
            }

            // analyse lines with an /*pcOffset*/ and // | register count |
            if ((line.find("/*") != std::string::npos) && (line.find("*/") != std::string::npos) && (line.find("// |") != std::string::npos))
            {
                counter_obj.pcOffset = get_pcOffset_sass_registers(line);
                counter_obj.gen_reg = get_all_registers(line)[0];
                counter_obj.pred_reg = get_all_registers(line)[1];
                counter_obj.u_gen_reg = get_all_registers(line)[2];

                counter_obj.change_reg_from_last = ((counter_obj.gen_reg) + (counter_obj.pred_reg) + (counter_obj.u_gen_reg)) - last_inst_register_count;
                // update the current sum of registers to the last instruction registers count
                last_inst_register_count = (counter_obj.gen_reg) + (counter_obj.pred_reg) + (counter_obj.u_gen_reg);

                live_registers_vec.push_back(counter_obj);
            }

            counter_map[kernel_name] = live_registers_vec;
        }
    }

    else
        std::cout << "Could not open the file" << filename << std::endl;

    // std::string pcOffset_to_search = "0350";
    // std::cout << "Kernel name: _Z22gpu_shared_matrix_multPiS_S_i, " << "pcoffset: " << pcOffset_to_search << std::endl;
    // auto reg_search_it = std::find_if(counter_map["_Z22gpu_shared_matrix_multPiS_S_i"].begin(), counter_map["_Z22gpu_shared_matrix_multPiS_S_i"].end(), [&](const live_registers& register_index){return pcOffset_to_search == register_index.pcOffset;});
    // if (reg_search_it != counter_map["_Z22gpu_shared_matrix_multPiS_S_i"].end())
    // {
    //     std::cout << reg_search_it->gen_reg << ", " << reg_search_it->pred_reg << " ," << reg_search_it->u_gen_reg << std::endl;
    // }

    return counter_map;
}
