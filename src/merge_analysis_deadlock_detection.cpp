/**
 * Merge analysis for deadlock detection
 * SASS analysis - deadlock detection (instruction ATOM.E.CAS, @P BRA, SYNC, ATOM.E.EXCH) -> output if deadlock possible
 * PC Sampling analysis - N/A
 * Metric analysis - N/A
 * 
 * @author Soumya Sen
 */

#include "parser_sass_deadlock_detection.hpp"

/// @brief Detects deadlock in code
/// @param detection_map Analysis for deadlock detection
void merge_analysis_deadlock_detection(std::unordered_map<std::string, deadlock_detect> detection_map)
{
    for (auto [k_sass, v_sass] : detection_map)
    {
        // Fix for blank kernel name appearing in the analysis_map
        if (k_sass == "")
        {
            break;
        }

        std::cout << "--------------------- Deadlock detect analysis for kernel: " << k_sass << "   --------------------- " << std::endl;
        (v_sass.deadlock_detect_flag) ? std::cout << "WARNING   ::  Possibility of deadlock detected in kernel: " << k_sass << std::endl : std::cout << "INFO   ::  No deadlock detected in kernel: " << k_sass << std::endl;
    }
}

int main(int argc, char **argv)
{
    std::string filename_executable_sass = argv[2];
    std::unordered_map<std::string, deadlock_detect> detection_map = deadlock_detection_analysis(filename_executable_sass);

    merge_analysis_deadlock_detection(detection_map);

    return 0;
}
