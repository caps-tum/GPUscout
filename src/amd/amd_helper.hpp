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
#include "../utilities/json.hpp"

/*
 * Defines functions used in multiple analyses
 */

// Used for extracting the kernel name from the rocprof-compute file
inline std::regex kernel_name_pattern() {
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
        ".*\\*\\s*│\\s*$"// selected kernel is marked with an asterisk in the last column
    );
}

inline std::regex wrapped_kernel_name_start_pattern()
{
    /*
     * Matches the first row of a wrapped kernel name in rocprof-compute's “Top Kernels" Table. Long kernel names are wrapped across multiple rows, e.g.:
     * ╒═════════╤════════════════════════════════════════╤═════════╤═══════════╤════════════╤══════════════╤═══════╤═════╕
     * │   index │ Kernel_Name                              │   Count │   Sum(ns) │   Mean(ns) │   Median(ns) │   Pct │ S   │
     * ╞═════════╪══════════════════════════════════════════╪═════════╪═══════════╪════════════╪══════════════╪═══════╪═════╡
     * │       0 │ __amd_rocclr_copyBuffer                  │    2.00 │  15040.00 │    7520.00 │      7520.00 │ 62.25 │     │
     * ├─────────┼──────────────────────────────────────────┼─────────┼───────────┼────────────┼──────────────┼───────┼─────┤
     * │       1 │ Hist(PixelType const*, int, int, unsigne │    1.00 │   9120.00 │    9120.00 │      9120.00 │ 37.75 │ *   │
     * │         │ d int*)  
     *
     * This pattern matches only the first row of a wrapped kernel name 
     */
    return std::regex(
        "^\\s*" // optional whitespaces at the beginning of the line
        "│"     // left table border
        "\\s*"  // whitespaces after the left border 
        "\\d+"  // kernel index, consisting of one or more digits
        "\\s*│" // whitespaces after the index and end of index column
        "\\s*"  // whitespaces before the kernel name
        "([^│]*\\([^│]*\\S)" // capture the first fragment of the wrapped kernel name
        "\\s*│" // end of kernel name column
        ".*\\*\\s*│\\s*$"// selected kernel is marked with an asterisk in the last column
    );
}

/* 
 * \param pending_kernel_name contains the first row of a wrapped kernel name.
 * This function appends the fragment from the continuation row. Once the
 * closing ')' of the kernel signature is found, the complete demangled
 * kernel name is mapped back to its mangled name using \param kernel_names_table.
 *
 * @return true if the kernel name is complete and mapped to its mangled name, false otherwise
 * if another continuation row is expected or if the kernel name could not be mapped to a mangled name.
 */
inline bool parse_wrapped_kernel_name_continuation(const std::string& line, std::string& pending_kernel_name, const std::unordered_map<std::string, std::string>& kernel_names_table, std::string& krn_name)
{
    static const std::regex continuation_pattern( "^\\s*│\\s*│\\s*([^│]*\\S)\\s*│" );

    std::smatch match;

    if (!std::regex_search(line, match, continuation_pattern))
    {
        return false;
    }

    pending_kernel_name += match[1].str();

    const auto closing_parenthesis = pending_kernel_name.find(')');

    if (closing_parenthesis == std::string::npos)
    {
        return false;
    }

    pending_kernel_name.resize(closing_parenthesis + 1);

    auto it = kernel_names_table.find(pending_kernel_name);

    if (it != kernel_names_table.end())
    {
        krn_name = it->second;
    }

    return true;
}

/// @brief Builds mangled kernel name lookup table because rocprof-compute cant provide this
/// @param  object file for matching mangled kernel names
/// @return lookup table containg the mangled and unmangled kernel
inline std::unordered_map<std::string, std::string> build_kernel_names_table(const std::string &assembly_filename) {
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