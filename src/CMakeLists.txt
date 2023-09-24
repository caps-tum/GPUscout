

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(merge_analysis_register_spilling merge_analysis_register_spilling.cpp)
add_executable(merge_analysis_use_restrict merge_analysis_use_restrict.cpp)
add_executable(merge_analysis_vectorization merge_analysis_vectorization.cpp)
add_executable(merge_analysis_global_atomics merge_analysis_global_atomics.cpp)
add_executable(merge_analysis_warp_divergence merge_analysis_warp_divergence.cpp)
add_executable(merge_analysis_use_texture merge_analysis_use_texture.cpp)
add_executable(merge_analysis_use_shared merge_analysis_use_shared.cpp)
add_executable(merge_analysis_datatype_conversion merge_analysis_datatype_conversion.cpp)
add_executable(merge_analysis_deadlock_detection merge_analysis_deadlock_detection.cpp)

install(TARGETS merge_analysis_register_spilling 
                merge_analysis_use_restrict
                merge_analysis_vectorization
                merge_analysis_global_atomics
                merge_analysis_warp_divergence
                merge_analysis_use_texture
                merge_analysis_use_shared
                merge_analysis_datatype_conversion
                merge_analysis_deadlock_detection
        DESTINATION analysis)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sampling_utilities/sampling_continuous/Makefile.in ${CMAKE_CURRENT_SOURCE_DIR}/sampling_utilities/sampling_continuous/Makefile @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sampling_utilities/sampling_utility/Makefile.in ${CMAKE_CURRENT_SOURCE_DIR}/sampling_utilities/sampling_utility/Makefile @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sampling_utilities/generate_sampling_stalls.sh.in ${CMAKE_CURRENT_SOURCE_DIR}/sampling_utilities/generate_sampling_stalls.sh @ONLY)

install(DIRECTORY sampling_utilities
        DESTINATION .
        USE_SOURCE_PERMISSIONS
        PATTERN "sampling_continuous/Makefile.in" EXCLUDE
        PATTERN "sampling_utility/Makefile.in" EXCLUDE
        PATTERN "generate_sampling_stalls.sh.in" EXCLUDE
)