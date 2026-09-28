#!/bin/bash


#@(#) This is the measurements script that collects the Nsight Compute metrics and executes the SASS analysis code

echo "======================================================================================================"


# Parse a comma-separated list, validate list syntax, trim tokens, and deduplicate.
parse_csv_list () {
    local raw="$1"
    local option_name="$2"
    local normalized token trimmed
    local token_list=()

    parsed_csv_list=()

    validate_csv_list_syntax "${raw}" "${option_name}"
    normalized="$(normalize_csv_list "${raw}")"

    IFS=',' read -r -a token_list <<< "${normalized}"

    for token in "${token_list[@]}"; do
        trimmed="$(trim_whitespace "${token}")"
        if ! array_contains "${trimmed}" "${parsed_csv_list[@]}"; then
            parsed_csv_list+=("${trimmed}")
        fi
    done
}

# Check if an array contains a value
array_contains () {
    local needle="$1"
    shift
    local item
    for item in "$@"; do
        if [ "${item}" = "${needle}" ]; then
            return 0
        fi
    done
    return 1
}

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

########################################################################
# Static Analysis Selection
########################################################################
automatic_mode=false
start_static_detect=$(date +%s.%N)
selected_static_detect_time=0

if [ -z "${analysis_arg:-}" ]; then
    #### Automatic mode ####
    # Enable analyses only when their static bottleneck has been detected.
    automatic_mode=true
    enabled_analyses=()
    echo "NVIDIA analysis selection mode: automatic (static-triggered)"

    static_detect_analyses=(
        register_spilling
        use_restrict
        vectorization
        global_atomics
        warp_divergence
        use_texture
        use_shared
        datatype_conversion
        deadlock_detection
    )

    for analysis in "${static_detect_analyses[@]}"; do
        detector="${gpuscout_dir}/analysis_nvidia/parser_sass_${analysis}"
        detector_start=$(date +%s.%N)

        if "$detector" "${exe_sass}" "${exe_ptx}"; then
            detector_rc=0
        else
            detector_rc=$?
        fi

        detector_end=$(date +%s.%N)
        detector_time=$(awk "BEGIN {print $detector_end - $detector_start}")
        selected_static_detect_time=$(awk "BEGIN {print $selected_static_detect_time + $detector_time}")
        echo "Static detection time for $analysis: ${detector_time}s"

        if [ "$detector_rc" -eq 0 ]; then
            enabled_analyses+=("$analysis")
            echo "Detected bottleneck candidate: $analysis"
        elif [ "$detector_rc" -eq 1 ]; then
            echo "No bottleneck candidate detected: $analysis"
        else
            echo "ERROR: Static detection failed for $analysis"
            exit "$detector_rc"
        fi
    done
    echo "Static detection time of all selected analyses: ${selected_static_detect_time}s"
else
    #### Manual mode ####
    # Keep the existing CLI-based analysis selection.
    automatic_mode=false
    parse_csv_list "${analysis_arg}" "analysis"
    enabled_analyses=("${parsed_csv_list[@]}")
    echo "NVIDIA analysis selection mode: manual"

    # Always generate static JSON for manually selected analyses. Return code 1
    # means no candidate was found; it must not disable a user-selected analysis.
    for analysis in "${enabled_analyses[@]}"; do
        parser="${gpuscout_dir}/analysis_nvidia/parser_sass_${analysis}"
        parser_start=$(date +%s.%N)

        if "$parser" "${exe_sass}" "${exe_ptx}"; then
            parser_rc=0
        else
            parser_rc=$?
        fi

        parser_end=$(date +%s.%N)
        parser_time=$(awk "BEGIN {print $parser_end - $parser_start}")
        selected_static_detect_time=$(awk "BEGIN {print $selected_static_detect_time + $parser_time}")
        echo "Static detection time for $analysis: ${parser_time}s"

        if [ "$parser_rc" -gt 1 ]; then
            echo "ERROR: Static parser failed for $analysis."
            exit "$parser_rc"
        fi
    done
    echo "Static detection time of selected analyses: ${selected_static_detect_time}s"
fi
end_static_detect=$(date +%s.%N)
static_detect_time=$(awk "BEGIN {print $end_static_detect - $start_static_detect}")

########################################################################
# Build Candidate Kernel -> Analysis Mapping
########################################################################
candidate_kernels=()
declare -A kernel_demangled=()
declare -A kernel_analyses=()
if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq is required to read NVIDIA static analysis results." >&2
    exit 1
fi

for analysis in "${enabled_analyses[@]}"; do
    static_result_file="${gpuscout_tmp_dir}/static_results/${analysis}.json"
    if [ ! -s "${static_result_file}" ]; then
        echo "ERROR: Missing static result for analysis: ${analysis}" >&2
        exit 1
    fi

    while IFS=$'\t' read -r mangled demangled; do
        [ -z "${mangled}" ] && continue

        if [ -z "${kernel_analyses[$mangled]+x}" ]; then
            candidate_kernels+=("${mangled}")
            kernel_demangled["${mangled}"]="${demangled:-$mangled}"
            kernel_analyses["${mangled}"]="${analysis}"
        else
            kernel_analyses["${mangled}"]+=" ${analysis}"
        fi
    done < <(
        jq -r '.candidate_kernels[]? | [.name, .demangled] | @tsv' \
            "${static_result_file}"
    )
done

echo "==== Candidate kernel analysis mapping"
for mangled in "${candidate_kernels[@]}"; do
    echo "Kernel: ${kernel_demangled[$mangled]}"
    echo "        mangled: ${mangled}"
    echo "        analyses: ${kernel_analyses[$mangled]}"
done

########################################################################
# Select Candidate Kernels for NCU and Merge Analysis
########################################################################
ncu_collection_kernels=()
selected_candidate_kernels=()
ncu_kernel_base_args=()

if [ -n "${kernels_arg:-}" ]; then
    # --kernels accepts simple function names without namespace, template arguments, or function parameters.
    #
    # Examples:
    #   Candidate: foo(float*)             User input: foo
    #   Candidate: ns::foo(float*)         User input: foo
    #   Candidate: ns::foo<float>(float*)  User input: foo
    kernels_selection_mode="user_function"
    parse_csv_list "${kernels_arg}" "kernels"
    requested_kernels=("${parsed_csv_list[@]}")

    for requested_kernel in "${requested_kernels[@]}"; do
        request_matched=false
        for mangled in "${candidate_kernels[@]}"; do
            demangled="${kernel_demangled[$mangled]}"
            # Remove function parameters: ns::foo<float>(float*) -> ns::foo<float>
            function_without_parameters="${demangled%%(*}"
            # Remove namespace: ns::foo<float> -> foo<float>
            unqualified_function="${function_without_parameters##*::}"
            # Remove template arguments: foo<float> -> foo
            candidate_function="${unqualified_function%%<*}"
            # Remove a possible return type: void foo -> foo
            candidate_function="${candidate_function##* }"

            if [ "${requested_kernel}" = "${candidate_function}" ]; then
                request_matched=true
                if ! array_contains "${mangled}" "${selected_candidate_kernels[@]}"; then
                    selected_candidate_kernels+=("${mangled}")
                fi
            fi
        done

        if [ "${request_matched}" = false ]; then
            echo "No bottleneck candidate detected for user-selected kernel: ${requested_kernel}"
        fi
    done
elif [ "${#nsys_hotspot_kernels[@]}" -gt 0 ]; then
    # Hotspot kernels identified by nsys and then filtered by static parser candidate kernels
    kernels_selection_mode="nsys_demangled"

    for requested_kernel in "${nsys_hotspot_kernels[@]}"; do
        request_matched=false
        for mangled in "${candidate_kernels[@]}"; do
            demangled="${kernel_demangled[$mangled]}"
            if [ "${requested_kernel}" = "${demangled}" ]; then
                request_matched=true
                if ! array_contains "${mangled}" "${selected_candidate_kernels[@]}"; then
                    selected_candidate_kernels+=("${mangled}")
                fi
            fi
        done

        if [ "${request_matched}" = false ]; then
            echo "No bottleneck candidate detected for user-selected kernel: ${requested_kernel}"
        fi
    done
else
    # All statically detected candidate kernels
    kernels_selection_mode="static_candidates"
    selected_candidate_kernels=("${candidate_kernels[@]}")
fi

ncu_kernel_base_args=(--kernel-name-base mangled)
ncu_collection_kernels=("${selected_candidate_kernels[@]}")

echo "Selected kernels for NCU collection: $(join_by_comma "${ncu_collection_kernels[@]}")"
echo "Selected analyses: $(join_by_comma "${enabled_analyses[@]}")"
echo "Kernel selection mode: ${kernels_selection_mode}"

# Merge analysis binaries consumes mangled symbols. Use a non-matching sentinel when no requested kernel maps to a static candidate; an empty
# filter could otherwise be interpreted as "all kernels" downstream.
if [ "${automatic_mode}" = false ] && [ -z "${kernels_arg:-}" ] && [ "${#nsys_hotspot_kernels[@]}" -eq 0 ]; then
    # Empty filter means: merge all kernels from the static result.
    kernel_filter_csv=""
elif [ "${#selected_candidate_kernels[@]}" -gt 0 ]; then
    # Automatic mode, or an explicit kernel selection:
    # merge only the selected candidate kernels.
    kernel_filter_csv="$(join_by_comma "${selected_candidate_kernels[@]}")"
else
    # No selected candidate kernel. Prevent an empty filter from being interpreted as "all kernels" in automatic mode.
    kernel_filter_csv="__gpuscout_no_selected_kernel__"
fi

########################################################################
# Collect PC-Sampling Stall Information
########################################################################
# These analyses consume PC-sampling stall information during merge.
pc_sampling_analyses=(
    register_spilling
    use_restrict
    vectorization
    global_atomics
    warp_divergence
    use_texture
    use_shared
)

pc_sampling_required=false
for kernel in "${selected_candidate_kernels[@]}"; do
    for analysis in ${kernel_analyses[$kernel]:-}; do
        if array_contains "${analysis}" "${pc_sampling_analyses[@]}"; then
            pc_sampling_required=true
            break 2
        fi
    done
done

pcsampling_time=0
if [ "${dry_run}" = false ] && [ "${pc_sampling_required}" = true ]; then
    echo "Getting warp stall reasons . . . . . . . . . . . . . . . "
    start_pcsampling=$(date +%s.%N)
    source "${gpuscout_dir}/sampling_utilities/generate_sampling_stalls.sh"
    cp ${gpuscout_dir}/sampling_utilities/sampling_utility/pcsampling_${run_prefix}.txt ${gpuscout_tmp_dir}/pcsampling_${run_prefix}.txt
    end_pcsampling=$(date +%s.%N)
    pcsampling_time=$(awk "BEGIN {print $end_pcsampling - $start_pcsampling}")
else
    echo "Skipping PC sampling: no selected candidate analysis requires stall samples."
fi

########################################################################
# Build Metric Requirements
########################################################################
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
########################################################################
# Build Per-Kernel Metric Requirements
########################################################################
declare -A kernel_metrics=()

_add_kernel_metric()
{
    local kernel="$1"
    local metric="$2"
    local current_metrics="${kernel_metrics[$kernel]:-}"

    # Metric IDs contain no spaces, so a space-separated list is sufficient.
    case " ${current_metrics} " in
        *" ${metric} "*)
            ;;
        *)
            if [ -n "${current_metrics}" ]; then
                kernel_metrics["${kernel}"]+=" ${metric}"
            else
                kernel_metrics["${kernel}"]="${metric}"
            fi
            ;;
    esac
}

_add_analysis_metrics_to_kernel()
{
    local target_kernel="$1"
    local target_array_name="$2"
    local -n metrics_ref="${target_array_name}"
    local metric

    for metric in "${metrics_ref[@]}"; do
        _add_kernel_metric "${target_kernel}" "${metric}"
    done
}

ncu_metrics_required=false

for kernel in "${selected_candidate_kernels[@]}"; do
    for analysis in ${kernel_analyses[$kernel]:-}; do
        # deadlock_detection currently requires no NCU metrics.
        if [ "${analysis}" = "deadlock_detection" ]; then
            continue
        fi

        array_name="_metrics_${analysis}"

        if ! declare -p "${array_name}" >/dev/null 2>&1; then
            echo "ERROR: Missing metric definition for analysis: ${analysis}"
            exit 1
        fi

        _add_analysis_metrics_to_kernel "${kernel}" "${array_name}"
    done

    if [ "${json}" = true ]; then
        for metric in "${_metrics_json_export[@]}"; do
            _add_kernel_metric "${kernel}" "${metric}"
        done
    fi

    if [ -n "${kernel_metrics[$kernel]:-}" ]; then
        ncu_metrics_required=true
    fi
done
echo "==== NVIDIA kernel profiling plan"

for kernel in "${selected_candidate_kernels[@]}"; do
    echo "Kernel: ${kernel_demangled[$kernel]}"
    echo "  mangled : ${kernel}"
    echo "  analyses: ${kernel_analyses[$kernel]}"
    echo "  metrics : ${kernel_metrics[$kernel]:-(none)}"
done
########################################################################
# Metric Collection
########################################################################
if [ "$dry_run" = false ]; then
    if [ "$ncu_metrics_required" = true ]; then
        echo "Collecting NCU metrics . . . . . . . . . . . . . . . "
        
        # record the time for NCU metrics collection
        start_metrics=$(date +%s.%N)
        metrics_out="${run_prefix}_metrics_list"

        echo "NCU mode: one launch per selected kernel"

        rm -f "${metrics_out}"

        for kernel in "${ncu_collection_kernels[@]}"; do
        # Collect only the metrics required by the analyses detected for this kernel. 
            echo "Profiling NCU metrics for kernel pattern: ${kernel}"
            kernel_metrics_list="${kernel_metrics[$kernel]:-}"
            if [ -z "${kernel_metrics_list}" ]; then
                echo "Skipping kernel with no required NCU metrics: ${kernel}"
                continue
            fi
             # Convert the internal space-separated list to the comma-separated format expected by NCU.
            kernel_metrics_csv="${kernel_metrics_list// /,}"

            tmp_csv="$(mktemp)"

            # skip warmup iterations to get more accurate metrics for the steady-state execution of the kernel
            # launch_count=1 to run only one instance of the kernel and get per-kernel metrics 
            ncu -f --csv --log-file "${tmp_csv}" --print-units base --print-kernel-base mangled \
                "${ncu_kernel_base_args[@]}" \
                --kernel-name "${kernel}" \
                --launch-count 1 \
                --metrics "${kernel_metrics_csv}" \
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

########################################################################
# Merge Analysis
########################################################################
cd ${gpuscout_dir}/analysis_nvidia

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

    echo "All NVIDIA static analyses completed!"
fi

# Merge all individual JSON files

if [ "$json" = true ]; then

echo "======================================================================================================"
echo "Generating JSON output . . . . . . . . . . . . . . . "

./save_to_json ${gpuscout_output_dir} ${gpuscout_tmp_dir}/result-${run_prefix} ${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt ${gpuscout_tmp_dir}/nvdisasm-registers-executable-${executable_filename}-sass.txt ${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt ${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt ${gpuscout_tmp_dir}/${run_prefix}_metrics_list ${sms} "${kernel_filter_csv}"

fi

end_analysis=$(date +%s.%N)
analysis_time=$(awk "BEGIN {print $end_analysis - $start_analysis}")

echo "======================================================================================================"
echo "Time for Static Preparation:   ${static_prep_time}s"
echo "Time for Static Detection:     ${static_detect_time}s"
if [ "$dry_run" = false ]; then
    echo "Time for PC Sampling:          ${pcsampling_time}s"
    echo "Time for Metrics Collection:   ${metrics_time}s"
fi
echo "Time for Merging Analysis:     ${analysis_time}s"
echo "Total time:                    $(awk "BEGIN {print $static_prep_time + $static_detect_time + $pcsampling_time + $metrics_time + $analysis_time}")s"
echo "======================================================================================================"

cd ..
