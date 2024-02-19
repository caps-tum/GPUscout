cmake_policy(SET CMP0048 NEW)
cmake_minimum_required(VERSION 3.27)

set (CMAKE_CXX_STANDARD 20)
set (CXX_STANDARD_REQUIRED ON)
set (CMAKE_CUDA_STANDARD 17)

#gcc11+ (GLIBCXX_3.4.29)

project(GPUscout VERSION 0.2.1 LANGUAGES CXX CUDA )

find_package(CUDAToolkit 11.8 REQUIRED)
include_directories(CUDA::cupti)
#link_libraries(CUDA::toolkit)

install(PROGRAMS GPUscout.sh DESTINATION . RENAME GPUscout)

add_subdirectory(src)

