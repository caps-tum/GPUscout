#!/bin/bash


#@(#) This is the measurements script that collects the Nsight Compute metrics and executes the SASS analysis code

echo "======================================================================================================"


# Append only CSV data rows from a per-kernel NCU CSV.
append_ncu_csv_rows () {
    local src="$1"
    local dest="$2"

    # If src doesn't contain a CSV header, it likely has no data.
    if ! awk 'BEGIN{found=0} /^"ID"/{found=1} END{exit found?0:1}' "${src}"; then
        return 0
    fi

    if [ ! -f "${dest}" ]; then
        mv "${src}" "${dest}"
        return 0
    fi

    # Append only data rows (skip preamble and repeated header).
    awk 'BEGIN{in_csv=0} /^"ID"/{in_csv=1; next} in_csv==1 && /^"/{print}' "${src}" >> "${dest}"
    rm -f "${src}"
}

extract_kernels_from_generated_sass () {
    local sass_file="$1"
    local kernel

    extracted_cubin_kernels=()

    if [ ! -f "${sass_file}" ]; then
        echo "ERROR: Generated SASS file not found for kernel discovery: ${sass_file}"
        exit 1
    fi

    while IFS= read -r kernel; do
        kernel="$(trim_whitespace "${kernel}")"
        if [ -z "${kernel}" ]; then
            continue
        fi
        if ! array_contains "${kernel}" "${extracted_cubin_kernels[@]}"; then
            extracted_cubin_kernels+=("${kernel}")
        fi
    done < <(
        awk '
            /\.other[[:space:]]+/ && /STO_CUDA_ENTRY/ {
                line=$0
                sub(/^.*\.other[[:space:]]+/, "", line)
                sub(/,.*/, "", line)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
                if (line != "") print line
            }
        ' "${sass_file}"
    )

    if [ "${#extracted_cubin_kernels[@]}" -eq 0 ]; then
        while IFS= read -r kernel; do
            kernel="$(trim_whitespace "${kernel}")"
            if [ -z "${kernel}" ]; then
                continue
            fi
            if ! array_contains "${kernel}" "${extracted_cubin_kernels[@]}"; then
                extracted_cubin_kernels+=("${kernel}")
            fi
        done < <(
            awk '
                /\.section[[:space:]]+\.text\./ {
                    line=$0
                    sub(/^.*\.section[[:space:]]+\.text\./, "", line)
                    sub(/,.*/, "", line)
                    gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
                    if (line != "") print line
                }
            ' "${sass_file}"
        )
    fi

    if [ "${#extracted_cubin_kernels[@]}" -eq 0 ]; then
        echo "ERROR: No CUDA kernels were found in generated SASS: ${sass_file}"
        exit 1
    fi
}

# Extract a readable kernel base name from a mangled symbol.
# Example: _ZN5cuZFP11cudaDecode1IxEEv... -> cudaDecode1
# extract_kernel_base_name_from_symbol () {
#     local symbol="$1"
#     local rest component_len component base_name=""
#     local fallback="${symbol}"

#     fallback="${fallback%%(*}"
#     fallback="${fallback##*::}"
#     fallback="${fallback%%<*}"
#     fallback="$(trim_whitespace "${fallback}")"

#     if [[ "${symbol}" == _ZN* ]]; then
#         rest="${symbol#_ZN}"
#     elif [[ "${symbol}" == _Z* ]]; then
#         rest="${symbol#_Z}"
#     else
#         if [ -n "${fallback}" ]; then
#             printf '%s' "${fallback}"
#         else
#             printf '%s' "${symbol}"
#         fi
#         return
#     fi

#     while [[ "${rest}" =~ ^([0-9]+) ]]; do
#         component_len="${BASH_REMATCH[1]}"
#         rest="${rest#${component_len}}"

#         if [ "${#rest}" -lt "${component_len}" ]; then
#             break
#         fi

#         component="${rest:0:${component_len}}"
#         rest="${rest:${component_len}}"
#         base_name="${component}"
#     done

#     if [ -n "${base_name}" ]; then
#         printf '%s' "${base_name}"
#     elif [ -n "${fallback}" ]; then
#         printf '%s' "${fallback}"
#     else
#         printf '%s' "${symbol}"
#     fi
# }

# Build de-duplicated NCU kernel patterns for auto mode from mangled symbols.
# build_auto_ncu_kernel_patterns () {
#     local kernel base_name

#     auto_ncu_kernel_patterns=()

#     for kernel in "${top_kernels[@]}"; do
#         base_name="$(extract_kernel_base_name_from_symbol "${kernel}")"
#         base_name="$(trim_whitespace "${base_name}")"
#         if [ -z "${base_name}" ]; then
#             continue
#         fi
#         if ! array_contains "${base_name}" "${auto_ncu_kernel_patterns[@]}"; then
#             auto_ncu_kernel_patterns+=("${base_name}")
#         fi
#     done

#     if [ "${#auto_ncu_kernel_patterns[@]}" -eq 0 ]; then
#         auto_ncu_kernel_patterns=("${top_kernels[@]}")
#     fi
# }

kernels_selection_mode="user"
ncu_kernel_base_args=()
if [ -z "${kernels_arg:-}" ]; then
    extract_kernels_from_generated_sass "${gpuscout_tmp_dir}/nvdisasm-executable-${run_prefix}-sass.txt"
    top_kernels=("${extracted_cubin_kernels[@]}")
    # build_auto_ncu_kernel_patterns 
    # top_kernels=("${auto_ncu_kernel_patterns[@]}")
    ncu_kernel_base_args=(--kernel-name-base mangled)
    kernels_selection_mode="auto_from_generated_sass"
else
    parse_csv_list "${kernels_arg}" "kernels"
    ncu_kernel_base_args=(--kernel-name-base function)
    top_kernels=("${parsed_csv_list[@]}")
fi

if [ -z "${analysis_arg:-}" ]; then
    enabled_analyses=("${valid_analyses[@]}")
else
    parse_csv_list "${analysis_arg}" "analysis"
    enabled_analyses=("${parsed_csv_list[@]}")
fi


ncu_collection_kernels=("${top_kernels[@]}")

echo "Selected kernels for NCU collection: $(join_by_comma "${ncu_collection_kernels[@]}")"
echo "Selected analyses: $(join_by_comma "${enabled_analyses[@]}")"
echo "Kernel selection mode: ${kernels_selection_mode}"

# Always apply end-to-end kernel filtering based on the effective kernel set.
kernel_filter_csv="$(join_by_comma "${top_kernels[@]}")"

# Build a comma-separated metric list for NCU (de-duplicated).
# If `json=true`, include metrics needed by JSON export as well.
_metrics_set=()
_add_metrics () {
    local m
    for m in "$@"; do
        if ! array_contains "$m" "${_metrics_set[@]}"; then
            _metrics_set+=("$m")
        fi
    done
}
_metrics_csv () { # Output the metrics as a comma-separated string for NCU
    local IFS=,
    printf '%s' "${_metrics_set[*]}"
}

# Per-analysis metric requirements (must match parser_metrics.hpp names).
# Register spilling (includes load_data_memory_flow helper metrics)
_metrics_register_spilling=(
    smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct
    smsp__warp_issue_stalled_lg_throttle_per_warp_active.pct
    smsp__inst_executed_op_local_ld.sum
    smsp__inst_executed_op_local_st.sum
    l1tex__t_sector_hit_rate.pct
    lts__t_sectors_op_read.sum
    lts__t_sectors_op_write.sum
    lts__t_sectors_op_atom.sum
    lts__t_sectors_op_red.sum
    sm__sass_inst_executed_op_global_ld.sum
    l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum
    l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate.pct
    l1tex__t_sectors_pipe_lsu_mem_local_op_ld.sum
    l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate.pct
    lts__t_sector_op_read_hit_rate.pct
)
# __restrict__
_metrics_use_restrict=(
    smsp__warp_issue_stalled_imc_miss_per_warp_active.pct
)
# Vectorization
_metrics_vectorization=(
    smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct
    sm__warps_active.avg.pct_of_peak_sustained_active
)
# Global atomics (includes atomic_data_memory_flow helper metrics)
_metrics_global_atomics=(
    smsp__warp_issue_stalled_lg_throttle_per_warp_active.pct
    smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct
    smsp__warp_issue_stalled_mio_throttle_per_warp_active.pct
    l1tex__t_sectors_pipe_lsu_mem_global_op_red.sum
    l1tex__t_sectors_pipe_lsu_mem_global_op_atom.sum
    l1tex__t_sector_pipe_lsu_mem_global_op_red_hit_rate.pct
    l1tex__t_sector_pipe_lsu_mem_global_op_atom_hit_rate.pct
    lts__t_sector_op_red_hit_rate.pct
    lts__t_sector_op_atom_hit_rate.pct
    sm__sass_data_bytes_mem_shared_op_atom.sum
)
# Warp divergence
_metrics_warp_divergence=(
    sm__sass_branch_targets.avg
    sm__sass_branch_targets_threads_divergent.avg
)
# Texture (includes texture_data_memory_flow helper metrics)
_metrics_use_texture=(
    smsp__warp_issue_stalled_tex_throttle_per_warp_active.pct
    smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct
    sm__sass_inst_executed_op_texture.sum
    l1tex__t_sectors_pipe_tex_mem_texture.sum
    l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate.pct
    lts__t_sector_op_read_hit_rate.pct
)
# Shared (includes shared_data_memory_flow + shared_memory_bank_conflict helper metrics)
_metrics_use_shared=(
    smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct
    smsp__warp_issue_stalled_mio_throttle_per_warp_active.pct
    sm__sass_inst_executed_op_shared_ld.sum
    smsp__sass_average_data_bytes_per_wavefront_mem_shared_op_ld.pct
    l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld.sum
)
# Datatype conversion
_metrics_datatype_conversion=(
    smsp__warp_issue_stalled_tex_throttle_per_warp_active.pct
    smsp__warp_issue_stalled_mio_throttle_per_warp_active.pct
    smsp__warp_issue_stalled_short_scoreboard_per_warp_active.pct
)
# Deadlock detection does not use NCU metrics.

# Metrics needed by JSON export (save_to_json -> total_memory_flow + misc).
_metrics_json_export=(
            # total_memory_flow() inputs
            l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum
            l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate.pct
            l1tex__t_sectors_pipe_lsu_mem_global_op_st.sum
            l1tex__t_sector_pipe_lsu_mem_global_op_st_hit_rate.pct
            l1tex__t_sectors_pipe_lsu_mem_local_op_ld.sum
            l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate.pct
            l1tex__t_sectors_pipe_lsu_mem_local_op_st.sum
            l1tex__t_sector_pipe_lsu_mem_local_op_st_hit_rate.pct
            l1tex__t_sectors_pipe_tex_mem_texture.sum
            l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate.pct
            lts__t_sector_op_read_hit_rate.pct
            lts__t_sector_op_write_hit_rate.pct
            lts__t_sector_hit_rate.pct
            l1tex__t_sectors_pipe_lsu_mem_global_op_red.sum
            l1tex__t_sectors_pipe_lsu_mem_global_op_atom.sum
            l1tex__t_sector_pipe_lsu_mem_global_op_red_hit_rate.pct
            l1tex__t_sector_pipe_lsu_mem_global_op_atom_hit_rate.pct
            lts__t_sector_op_red_hit_rate.pct
            lts__t_sector_op_atom_hit_rate.pct
            l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld.sum
            sm__sass_inst_executed_op_shared_ld.sum
            sm__sass_inst_executed_op_shared_st.sum
            sm__sass_inst_executed_op_local_ld.sum
            sm__sass_inst_executed_op_local_st.sum
            sm__sass_inst_executed_op_global_ld.sum
            sm__sass_inst_executed_op_global_st.sum
            sm__sass_inst_executed_op_texture.sum
            smsp__sass_inst_executed.sum
            smsp__inst_executed_op_local_ld.sum
            smsp__inst_executed_op_local_st.sum
            l1tex__t_sector_hit_rate.pct
            lts__t_sectors_op_read.sum
            lts__t_sectors_op_write.sum
            lts__t_sectors_op_atom.sum
            lts__t_sectors_op_red.sum
            smsp__inst_executed_op_global_ld.sum
            memory_l2_theoretical_sectors_global
            memory_l2_theoretical_sectors_global_ideal
            memory_l1_wavefronts_shared
            memory_l1_wavefronts_shared_ideal

            # "misc" JSON serialization currently includes these
            sm__warps_active.avg.pct_of_peak_sustained_active
            smsp__warps_active.sum
            smsp__warp_issue_stalled_barrier_per_warp_active.pct
            smsp__warp_issue_stalled_membar_per_warp_active.pct
            smsp__warp_issue_stalled_short_scoreboard_per_warp_active.pct
            smsp__warp_issue_stalled_wait_per_warp_active.pct
            smsp__warp_issue_stalled_imc_miss_per_warp_active.pct
            smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct
            smsp__warp_issue_stalled_lg_throttle_per_warp_active.pct
            smsp__warp_issue_stalled_mio_throttle_per_warp_active.pct
            smsp__warp_issue_stalled_tex_throttle_per_warp_active.pct
        )

# for analysis in "${enabled_analyses[@]}"; do
#     case "$analysis" in
#         register_spilling)   _add_metrics "${_metrics_register_spilling[@]}" ;;
#         use_restrict)        _add_metrics "${_metrics_use_restrict[@]}" ;;
#         vectorization)       _add_metrics "${_metrics_vectorization[@]}" ;;
#         global_atomics)      _add_metrics "${_metrics_global_atomics[@]}" ;;
#         warp_divergence)     _add_metrics "${_metrics_warp_divergence[@]}" ;;
#         use_texture)         _add_metrics "${_metrics_use_texture[@]}" ;;
#         use_shared)          _add_metrics "${_metrics_use_shared[@]}" ;;
#         datatype_conversion) _add_metrics "${_metrics_datatype_conversion[@]}" ;;
#         deadlock_detection)  : ;;
#         *)
#             echo "ERROR: Unknown analysis name in enabled_analyses: $analysis"
#             exit 1
#             ;;
#     esac
# done
# Refactor the above to avoid code duplication and allow easier addition of new analyses in the future.
for analysis in "${enabled_analyses[@]}"; do
    if [ "${analysis}" = "deadlock_detection" ]; then
        continue
    fi

    if ! array_contains "${analysis}" "${valid_analyses[@]}"; then
        echo "ERROR: Unknown analysis name in enabled_analyses: $analysis"
        exit 1
    fi

    array_name="_metrics_${analysis}"
    declare -n current_metrics="${array_name}"

    _add_metrics "${current_metrics[@]}"
done

if [ "$json" = true ]; then
    _add_metrics "${_metrics_json_export[@]}"
fi

metrics_csv="$(_metrics_csv)" # This is the final comma-separated metric list to pass to NCU. It is de-duplicated and includes all metrics needed by the selected analyses and JSON export (if enabled).
ncu_metrics_required=false
if [ -n "${metrics_csv}" ]; then
    ncu_metrics_required=true
fi

if [ "$dry_run" = false ]; then
    if [ "$ncu_metrics_required" = true ]; then
        echo "Collecting NCU metrics . . . . . . . . . . . . . . . "
        
        # record the time for NCU metrics collection
        start_metrics=$(date +%s.%N)
        metrics_out="${run_prefix}_metrics_list"

        echo "NCU mode: one launch per selected kernel"

        rm -f "${metrics_out}"

        for kernel in "${ncu_collection_kernels[@]}"; do
            echo "Profiling NCU metrics for kernel pattern: ${kernel}"
            tmp_csv="$(mktemp)"

            # skip warmup iterations to get more accurate metrics for the steady-state execution of the kernel
            # launch_count=1 to run only one instance of the kernel and get per-kernel metrics 
            ncu -f --csv --log-file "${tmp_csv}" --print-units base --print-kernel-base mangled \
                "${ncu_kernel_base_args[@]}" \
                --kernel-name "${kernel}" \
                --launch-count 1 \
                --metrics "${metrics_csv}" \
                ${executable} ${args}
            # ncu -f --csv --log-file "${tmp_csv}" --print-units base --print-kernel-base mangled \
            #     "${ncu_kernel_base_args[@]}" \
            #     --kernel-name "${kernel}" -s 5 --launch-count 1 \
            #     --metrics "${metrics_csv}" \
            #     ${executable} ${args}
            append_ncu_csv_rows "${tmp_csv}" "${metrics_out}"
        done

        if [ ! -f "${metrics_out}" ]; then
            echo "WARNING: NCU did not produce any CSV rows for the selected kernels. Continuing without NCU metrics."
            # Emit a minimal valid CSV header so downstream parsers can proceed cleanly.
            printf '"ID","Process ID","Process Name","Host Name","Kernel Name","Kernel Time","Context","Stream","Section Name","Metric Name","Metric Unit","Metric Value"\n' > "${metrics_out}"
        fi

        mv "${metrics_out}" "${gpuscout_tmp_dir}/${metrics_out}"
        end_metrics=$(date +%s.%N)
        metrics_time=$(awk "BEGIN {print $end_metrics - $start_metrics}")
    else
        echo "Skipping NCU metrics collection: selected analyses do not require NCU metrics."
        metrics_time=0
    fi
fi


cd ${gpuscout_dir}/analysis_nvidia

hpc_sass="${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt"
exe_sass="${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt"
exe_ptx="${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt"
sampling="${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt"
metrics="${gpuscout_tmp_dir}/${run_prefix}_metrics_list"
reg_exe="${gpuscout_tmp_dir}/nvdisasm-registers-executable-${executable_filename}-sass.txt"
reg_hpc="${gpuscout_tmp_dir}/nvdisasm-registers-hpctoolkit-${executable_filename}-sass.txt"

args_register_spilling=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$reg_exe" "$json" "$gpuscout_output_dir" "$sms" "${kernel_filter_csv}")
args_use_restrict=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$reg_hpc" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")
args_vectorization=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$reg_hpc" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")
args_global_atomics=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")
args_warp_divergence=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")
args_use_texture=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")
args_use_shared=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")
args_datatype_conversion=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")
args_deadlock_detection=("$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir" "${kernel_filter_csv}")

start_analysis=$(date +%s.%N)

if [ "$performance_mode" = false ]; then
    # Time a command (wall clock) and print duration.
    # Usage: timed_run "<label>" <command> [args...]
   
    # Run only the analyses selected in `enabled_analyses` above.
    # for analysis in "${enabled_analyses[@]}"; do
    #     case "$analysis" in
    #         register_spilling)
    #             echo "======================================================================================================"
    #             echo "Combining above results for register spilling analysis . . . . . . . . . . . . . . . "
    #             # Run only the analyses selected in `enabled_analyses` above.
    #             #g++ -std=c++17 ../merge_analysis_register_spilling.cpp -o merge_analysis_register_spilling
    #             # nvcc --generate-line-info merge_analysis_register_spilling.cpp -o merge_analysis_register_spilling -lcuda -l:libcufilt.a
    #             timed_run "register spilling analysis" ./merge_analysis_register_spilling "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${reg_exe}" "${json}" "${gpuscout_output_dir}" "${sms}" "${kernel_filter_csv}"
    #             ;;
    #         use_restrict)
    #             echo "======================================================================================================"
    #             echo "Combining above results for using __restrict__ analysis . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_use_restrict.cpp -o merge_analysis_use_restrict
    #             timed_run "using __restrict__ analysis" ./merge_analysis_use_restrict "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${reg_hpc}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         vectorization)
    #             echo "======================================================================================================"
    #             echo "Combining above results for vectorization analysis . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_vectorization.cpp -o merge_analysis_vectorization
    #             timed_run "vectorization analysis" ./merge_analysis_vectorization "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${reg_hpc}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         global_atomics)
    #             echo "======================================================================================================"
    #             echo "Combining above results for global atomics analysis . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_global_atomics.cpp -o merge_analysis_global_atomics
    #             timed_run "global atomics analysis" ./merge_analysis_global_atomics "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         warp_divergence)
    #             echo "======================================================================================================"
    #             echo "Combining above results for warp divergence analysis . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_warp_divergence.cpp -o merge_analysis_warp_divergence
    #             timed_run "warp divergence analysis" ./merge_analysis_warp_divergence "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         use_texture)
    #             echo "Combining above results for using texture memory analysis . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_use_texture.cpp -o merge_analysis_use_texture
    #             timed_run "use texture memory analysis" ./merge_analysis_use_texture "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         use_shared)
    #             echo "======================================================================================================"
    #             echo "Combining above results for using shared memory analysis . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_use_shared.cpp -o merge_analysis_use_shared
    #             timed_run "use shared memory analysis" ./merge_analysis_use_shared "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         datatype_conversion)
    #             echo "======================================================================================================"
    #             echo "Combining above results for datatype conversion analysis . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_datatype_conversion.cpp -o merge_analysis_datatype_conversion
    #             timed_run "datatype conversion analysis" ./merge_analysis_datatype_conversion "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         deadlock_detection)
    #             echo "======================================================================================================"
    #             echo "Combining above results for deadlock detection . . . . . . . . . . . . . . . "
    #             #g++ -std=c++17 ../merge_analysis_deadlock_detection.cpp -o merge_analysis_deadlock_detection
    #             timed_run "deadlock detection" ./merge_analysis_deadlock_detection "${hpc_sass}" "${exe_sass}" "${exe_ptx}" "${sampling}" "${metrics}" "${json}" "${gpuscout_output_dir}" "${kernel_filter_csv}"
    #             ;;
    #         *)
    #             echo "ERROR: Unknown analysis name in enabled_analyses (merge stage): $analysis"
    #             exit 1
    #             ;;
    #     esac
    # done

    ## Refactor the above to avoid code duplication and allow easier addition of new analyses in the future.
    timed_run () {
        local label="$1"
        shift
        
        echo "======================================================================================================"
        if [[ "$label" == "use_restrict" ]]; then
                local readable_label="using __restrict__"
        elif [[ "$label" == "use_texture" ]]; then
            local readable_label="using texture memory"
        elif [[ "$label" == "use_shared" ]]; then
            local readable_label="using shared memory"
        else
            local readable_label=$(echo "$label" | tr '_' ' ')
        fi
        echo "Combining above results for ${readable_label} analysis . . . . . . . . . . . . . . . "
        local t0 t1 dt
        t0=$(date +%s.%N)
        "$@"
        t1=$(date +%s.%N)
        dt=$(awk "BEGIN {print $t1 - $t0}")
        echo "Time for ${readable_label}: ${dt}s"
    }
    # Run only the analyses selected in `enabled_analyses` above.
    for analysis in "${enabled_analyses[@]}"; do
        binary="./merge_analysis_${analysis}"
        if [ ! -x "${binary}" ]; then
            echo "ERROR:  Unknown analysis name in enabled_analyses (merge stage): $analysis"
            exit 1
        fi
        array_name="args_${analysis}"
        declare -n current_args="$array_name"

        timed_run "${analysis}" "${binary}" "${current_args[@]}"
    done
else
    # Use Multi-Threading for faster analysis -> each analysis within its own thread
    analysis_logs_dir="${gpuscout_tmp_dir}/analysis_tmp_outputs"
    mkdir -p "$analysis_logs_dir"
    declare -a names=() pids=()

    timed_run() {
        local name="$1"; shift
        names+=("$name")

        # Create a private log file for this analysis
        local private_log="${analysis_logs_dir}/${name}.log"
        : >"$private_log"

        (
            echo "======================================================================================================"
            if [[ "$name" == "use_restrict" ]]; then
                local readable_label="using __restrict__"
            elif [[ "$name" == "use_texture" ]]; then
                local readable_label="using texture memory"
            elif [[ "$name" == "use_shared" ]]; then
                local readable_label="using shared memory"
            else
                local readable_label=$(echo "$name" | tr '_' ' ')
            fi
            echo "Combining above results for ${readable_label} analysis . . . . . . . . . . . . . . . "
            
            local t0 t1 dt
            t0=$(date +%s.%N)

           "$@" # Execute the passed command
            rc=$? # Captures the exit code

            t1=$(date +%s.%N) 
            dt=$(awk "BEGIN {print $t1 - $t0}") 

            echo "Time for ${readable_label} analysis: ${dt}s" 
            exit "$rc"
        ) >"$private_log" 2>&1 &  # Run in background and redirect output to private log
        pids+=("$!")
    }

    echo "Launching NVIDIA SASS static analyses in parallel..."

    # run register_spilling   ./merge_analysis_register_spilling   "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$reg_exe" "$json" "$gpuscout_output_dir" "$sms"
    # run use_restrict        ./merge_analysis_use_restrict        "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$reg_hpc" "$json" "$gpuscout_output_dir"
    # run vectorization       ./merge_analysis_vectorization       "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$reg_hpc" "$json" "$gpuscout_output_dir"
    # run global_atomics      ./merge_analysis_global_atomics      "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir"
    # run warp_divergence     ./merge_analysis_warp_divergence     "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir"
    # run use_texture         ./merge_analysis_use_texture         "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir"
    # run use_shared          ./merge_analysis_use_shared          "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir"
    # run datatype_conversion ./merge_analysis_datatype_conversion "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir"
    # run deadlock_detection  ./merge_analysis_deadlock_detection  "$hpc_sass" "$exe_sass" "$exe_ptx" "$sampling" "$metrics" "$json" "$gpuscout_output_dir"
    # Launch all analyses in parallel
    for analysis in "${enabled_analyses[@]}"; do
        binary="./merge_analysis_${analysis}"
        if [ ! -x "${binary}" ]; then
            echo "ERROR:  Unknown analysis name in enabled_analyses (merge stage): $analysis"
            exit 1
        fi
        array_name="args_${analysis}"
        declare -n current_args="$array_name"

        timed_run "${analysis}" "${binary}" "${current_args[@]}"
    done

    # Wait for all analyses to complete and capture their exit codes
    declare -a rc=()
    for i in "${!pids[@]}"; do
        if wait "${pids[$i]}"; then
            rc[$i]=0
        else
            rc[$i]=$?
        fi
    done

    # print outputs of all analyses in order and check for any failures
    for name in "${names[@]}"; do
        cat "${analysis_logs_dir}/${name}.log"
    done
fi
echo "All NVIDIA static analyses completed!"

# Merge all individual JSON files

if [ "$json" = true ]; then

echo "======================================================================================================"
echo "Generating JSON output . . . . . . . . . . . . . . . "

./save_to_json ${gpuscout_output_dir} ${gpuscout_tmp_dir}/result-${run_prefix} ${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt ${gpuscout_tmp_dir}/nvdisasm-registers-executable-${executable_filename}-sass.txt ${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt ${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt ${gpuscout_tmp_dir}/${run_prefix}_metrics_list ${sms} "${kernel_filter_csv}"

fi

end_analysis=$(date +%s.%N)
analysis_time=$(awk "BEGIN {print $end_analysis - $start_analysis}")

echo "======================================================================================================"
echo "Time for Static Code Analysis: ${static_time}s"
if [ "$dry_run" = false ]; then
    echo "Time for PC Sampling:          ${pcsampling_time}s"
    echo "Time for Metrics Collection:   ${metrics_time}s"
fi
echo "Time for Merging Analysis:     ${analysis_time}s"
echo "======================================================================================================"

cd ..
