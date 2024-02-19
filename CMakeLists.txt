cmake_policy(SET CMP0048 NEW)
cmake_minimum_required(VERSION 3.27)

set (CMAKE_CXX_STANDARD 20)
set (CXX_STANDARD_REQUIRED ON)
set (CMAKE_CUDA_STANDARD 17)

#gcc11+ (GLIBCXX_3.4.29)

project(GPUscout VERSION 0.1.0 LANGUAGES CXX CUDA )

#find_package(CUDA 11.8 REQUIRED)
find_package(CUDAToolkit 11.8 REQUIRED)
include_directories(CUDA::cupti)
#link_libraries(CUDA::toolkit)

install(PROGRAMS GPUscout.sh DESTINATION .)
#install(PROGRAMS measurements.sh DESTINATION analysis)

#file(COPY ${CUDAToolkit_LIBRARY_ROOT}/extras/CUPTI/samples/pc_sampling_utility DESTINATION "${CMAKE_BINARY_DIR}")
#
## Define the command as a variable
#set(install_path "CUDA_INSTALL_PATH=${CUDAToolkit_LIBRARY_ROOT}")
#set(includes "INCLUDES=\"-I$(CUDA_INSTALL_PATH)/include -I$(CUDA_INSTALL_PATH)/extras/CUPTI\"")
#set(cmd_to_execute "${install_path} ${includes} make")
#message(STATUS "${cmd_to_execute}")
#
#execute_process(
#    COMMAND ${cmd_to_execute}
#    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/pc_sampling_utility
#    RESULT_VARIABLE result
#    OUTPUT_VARIABLE output
#    ERROR_VARIABLE error
#)
#
## Print the result, output, and error
#message(STATUS "Result: ${result}")
#message(STATUS "Output: ${output}")
#message(STATUS "Error: ${error}")
#
#install(DIRECTORY ${CMAKE_BINARY_DIR}/pc_sampling_utility/
#        DESTINATION ${CMAKE_INSTALL_PREFIX}/xxx
#        USE_SOURCE_PERMISSIONS
#)


add_subdirectory(src)

