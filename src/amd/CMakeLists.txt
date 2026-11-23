set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(parser_amdgcn_register_spilling parser_amdgcn_register_spilling.cpp)
add_executable(parser_amdgcn_restrict parser_amdgcn_restrict.cpp)
add_executable(parser_amdgcn_vectorized_load parser_amdgcn_vectorized_load.cpp)
add_executable(parser_amdgcn_atomic_instruction parser_amdgcn_atomic_instruction.cpp)
add_executable(parser_amdgcn_wavefront_divergence parser_amdgcn_wavefront_divergence.cpp)
add_executable(parser_amdgcn_shared_memory parser_amdgcn_shared_memory.cpp)
add_executable(parser_amdgcn_datatype_conversion parser_amdgcn_datatype_conversion.cpp)
add_executable(parser_amdgcn_deadlock_detection parser_amdgcn_deadlock_detection.cpp)
add_executable(analysis_register_spilling analysis_register_spilling.cpp)
add_executable(analysis_restrict analysis_restrict.cpp)
add_executable(analysis_vectorized_load analysis_vectorized_load.cpp)
add_executable(analysis_atomic_instruction analysis_atomic_instruction.cpp)
add_executable(analysis_wavefront_divergence analysis_wavefront_divergence.cpp)
add_executable(analysis_shared_memory analysis_shared_memory.cpp)
add_executable(analysis_datatype_conversion analysis_datatype_conversion.cpp)
add_executable(analysis_deadlock_detection analysis_deadlock_detection.cpp)
add_executable(amd_save_to_json save_to_json.cpp)
set_target_properties(amd_save_to_json PROPERTIES OUTPUT_NAME save_to_json)

install(TARGETS parser_amdgcn_register_spilling
                parser_amdgcn_restrict
                parser_amdgcn_vectorized_load
                parser_amdgcn_atomic_instruction
                parser_amdgcn_wavefront_divergence
                parser_amdgcn_shared_memory
                parser_amdgcn_datatype_conversion
                parser_amdgcn_deadlock_detection
                analysis_register_spilling
                analysis_restrict
                analysis_vectorized_load
                analysis_atomic_instruction
                analysis_wavefront_divergence
                analysis_shared_memory
                analysis_datatype_conversion
                analysis_deadlock_detection
                amd_save_to_json
                DESTINATION analysis_amd)

install(PROGRAMS measurements.sh DESTINATION analysis_amd)