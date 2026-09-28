enable_language(CUDA)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set (CMAKE_CUDA_STANDARD 17)
find_package(CUDAToolkit 11.8 REQUIRED)
include_directories(CUDA::cupti)

add_executable(parser_sass_register_spilling parser_sass_register_spilling.cpp)
add_executable(parser_sass_datatype_conversion parser_sass_datatype_conversion.cpp)
add_executable(parser_sass_use_texture parser_sass_use_texture.cpp)
add_executable(parser_sass_global_atomics parser_ptx_global_atomics.cpp)
add_executable(parser_sass_warp_divergence parser_sass_warp_divergence.cpp)
add_executable(parser_sass_use_shared parser_sass_use_shared.cpp)
add_executable(parser_sass_use_restrict parser_sass_use_restrict.cpp)
add_executable(parser_sass_vectorization parser_sass_vectorization.cpp)
add_executable(parser_sass_deadlock_detection parser_sass_deadlock_detection.cpp)
add_executable(merge_analysis_register_spilling merge_analysis_register_spilling.cpp)
add_executable(merge_analysis_use_restrict merge_analysis_use_restrict.cpp)
add_executable(merge_analysis_vectorization merge_analysis_vectorization.cpp)
add_executable(merge_analysis_global_atomics merge_analysis_global_atomics.cpp)
add_executable(merge_analysis_warp_divergence merge_analysis_warp_divergence.cpp)
add_executable(merge_analysis_use_texture merge_analysis_use_texture.cpp)
add_executable(merge_analysis_use_shared merge_analysis_use_shared.cpp)
add_executable(merge_analysis_datatype_conversion merge_analysis_datatype_conversion.cpp)
add_executable(merge_analysis_deadlock_detection merge_analysis_deadlock_detection.cpp)
add_executable(nvidia_save_to_json save_to_json.cpp)
set_target_properties(nvidia_save_to_json PROPERTIES OUTPUT_NAME save_to_json)

install(TARGETS parser_sass_register_spilling 
                parser_sass_datatype_conversion
                parser_sass_use_texture
                parser_sass_global_atomics
                parser_sass_warp_divergence
                parser_sass_use_shared
                parser_sass_use_restrict
                parser_sass_vectorization
                parser_sass_deadlock_detection
                merge_analysis_register_spilling 
                merge_analysis_use_restrict
                merge_analysis_vectorization
                merge_analysis_global_atomics
                merge_analysis_warp_divergence
                merge_analysis_use_texture
                merge_analysis_use_shared
                merge_analysis_datatype_conversion
                merge_analysis_deadlock_detection
                nvidia_save_to_json
        DESTINATION analysis_nvidia)
install(PROGRAMS measurements.sh DESTINATION analysis_nvidia)

# TODO currently only on Nvidia:
#configure_file(sampling_utilities/sampling_continuous/Makefile.in sampling_utilities/sampling_continuous/Makefile @ONLY)
#configure_file(sampling_utilities/sampling_utility/Makefile.in sampling_utilities/sampling_utility/Makefile @ONLY)
#configure_file(sampling_utilities/generate_sampling_stalls.sh.in sampling_utilities/generate_sampling_stalls.sh @ONLY)
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
