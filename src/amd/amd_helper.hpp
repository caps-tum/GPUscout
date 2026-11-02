#ifndef GPUSCOUT_AMD_HELPER_HPP
#define GPUSCOUT_AMD_HELPER_HPP

#include <unordered_map>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <tuple>
#include <regex>
#include "amdgcn_instructions.hpp"
#include "../utilities/helper.hpp"

/*
 * Defines functions used in multiple analyses
 */

// Used for extracting the kernel name from the rocprof-compute file
std::regex kernel_name_pattern() {
    /*
     * ╒═════════╤════════════════════════════════════════╤═════════╤═══════════╤════════════╤══════════════╤═══════╤═════╕
     * │   index │ Kernel_Name                            │   Count │   Sum(ns) │   Mean(ns) │   Median(ns) │   Pct │ S   │
     * ╞═════════╪════════════════════════════════════════╪═════════╪═══════════╪════════════╪══════════════╪═══════╪═════╡
     * │       0 │ __amd_rocclr_copyBuffer                │    5.00 │  29206.50 │    5841.30 │      5083.00 │ 42.80 │     │
     * ...
     * ├─────────┼────────────────────────────────────────┼─────────┼───────────┼────────────┼──────────────┼───────┼─────┤
     * │       3 │ spillingKernel(float*, float*, float*) │    1.00 │   7815.00 │    7815.00 │      7815.00 │ 11.45 │ *   │
     *
     * Some rocprof-compute versions append an AMDHSA descriptor suffix:
     *
     * │  0 │ spillingKernel(float*, float*) [clone .k │ ... │ * │
     * │    │ d]                                       │ ...     │
     *
     * Only match the stable beginning of the row and capture the demangled kernel signature up to its closing parenthesis. 
     * Remaining columns and optional suffixes such as "[clone .kd]" are deliberately ignored.
     */
    return std::regex(
        "^\\s*"          // optional whitespaces at the beginning of the line
        "│"              // left table border
        "\\s*"           // whitespaces after the left border
        "\\d+"           // kernel index, consisting of one or more digits
        "\\s*│"          // whitespaces after the index and end of index column
        "\\s*"           // whitespaces before the kernel name
        "([^│]*\\))"     // capture kernel signature up to the closing ')'
    );
}


/// @brief Builds mangled kernel name lookup table because rocprof-compute cant provide this
/// @param  object file for matching mangled kernel names
/// @return lookup table containg the mangled and unmangled kernel
std::unordered_map<std::string, std::string> build_kernel_names_table(const std::string &assembly_filename) {
    // Build mangled kernel name lookup table - rocprof-compute cant provide this
    // Because of this the mangled kernel name is taken out of the object file
    std::unordered_map<std::string, std::string> kernel_names_table;
    std::fstream as_file(assembly_filename, std::ios::in);
    std::string as_line; // stores the current line in the assembly file
    int instr_counter;
    if (as_file.is_open()) {
        while (std::getline(as_file, as_line)) {
            std::smatch match;

            // Line matches the selected kernel
            if (std::regex_search(as_line, match, regex_krn_name)) {
                std::string demangled_krn_name = get_demangled_kernel(match[1], "c++filt");

                if (kernel_names_table.find(demangled_krn_name) == kernel_names_table.end()) {
                    kernel_names_table[demangled_krn_name] = match[1];
                }
            }
        }
    }
    else {
        std::cout << "Building mangled kernel names: Failed opening assembly file";
    }
    return kernel_names_table;
}

#endif //GPUSCOUT_AMD_HELPER_HPP