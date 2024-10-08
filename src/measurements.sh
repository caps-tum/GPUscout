#!/bin/bash


#@(#) This is the measurements script that collects the Nsight Compute metrics and executes the SASS analysis code

echo "======================================================================================================"
if [ "$dry_run" = false ]; then
    echo "Collecting NCU metrics . . . . . . . . . . . . . . . "

    executable_with_args="$executable"

    if [ ! -z "$args"]; then
        executable_with_args="$executable $args"
    fi

    # Extract all the metrics in one pass
    ncu -f --csv --log-file ${run_prefix}_metrics_list --print-units base --print-kernel-base mangled --metrics \
smsp__warps_active.sum,\
smsp__warp_issue_stalled_barrier_per_warp_active.pct,\
smsp__warp_issue_stalled_membar_per_warp_active.pct,\
smsp__warp_issue_stalled_short_scoreboard_per_warp_active.pct,\
smsp__warp_issue_stalled_wait_per_warp_active.pct,\
smsp__thread_inst_executed_per_inst_executed.ratio,\
sm__sass_branch_targets.avg,\
sm__sass_branch_targets_threads_divergent.avg,\
smsp__warp_issue_stalled_imc_miss_per_warp_active.pct,\
smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct,\
sm__warps_active.avg.pct_of_peak_sustained_active,\
smsp__warp_issue_stalled_lg_throttle_per_warp_active.pct,\
smsp__warp_issue_stalled_mio_throttle_per_warp_active.pct,\
smsp__warp_issue_stalled_tex_throttle_per_warp_active.pct,\
sm__sass_inst_executed_op_global_red.sum,\
sm__sass_inst_executed_op_shared_atom.sum,\
l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld.sum,\
sm__sass_inst_executed_op_shared_ld.sum,\
l1tex__data_pipe_lsu_wavefronts_mem_shared_op_st.sum,\
sm__sass_inst_executed_op_shared_st.sum,\
smsp__sass_average_data_bytes_per_wavefront_mem_shared.pct,\
l1tex__t_sector_hit_rate.pct,\
lts__t_sectors_op_atom.sum,\
lts__t_sectors_op_read.sum,\
lts__t_sectors_op_red.sum,\
lts__t_sectors_op_write.sum,\
smsp__inst_executed_op_local_ld.sum,\
smsp__inst_executed_op_local_st.sum,\
sm__sass_inst_executed_op_global_ld.sum,\
l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum,\
l1tex__t_sector_pipe_lsu_mem_global_op_ld_hit_rate.pct,\
lts__t_sector_op_read_hit_rate.pct,\
l1tex__t_sectors_pipe_lsu_mem_local_op_ld.sum,\
l1tex__t_sector_pipe_lsu_mem_local_op_ld_hit_rate.pct,\
l1tex__t_sectors_pipe_lsu_mem_global_op_red.sum,\
l1tex__t_sectors_pipe_lsu_mem_global_op_atom.sum,\
l1tex__t_sector_pipe_lsu_mem_global_op_red_hit_rate.pct,\
l1tex__t_sector_pipe_lsu_mem_global_op_atom_hit_rate.pct,\
lts__t_sector_op_red_hit_rate.pct,\
lts__t_sector_op_atom_hit_rate.pct,\
sm__sass_data_bytes_mem_shared_op_atom.sum,\
l1tex__m_xbar2l1tex_read_sectors_mem_lg_op_ld.sum.pct_of_peak_sustained_elapsed,\
l1tex__average_t_sectors_per_request_pipe_lsu_mem_global_op_ld.ratio,\
smsp__inst_executed_op_global_ld.sum,\
memory_l2_theoretical_sectors_global,\
memory_l2_theoretical_sectors_global_ideal,\
memory_l1_wavefronts_shared,\
memory_l1_wavefronts_shared_ideal,\
sm__sass_inst_executed_op_texture.sum,\
l1tex__t_sectors_pipe_tex_mem_texture.sum,\
l1tex__t_sector_pipe_tex_mem_texture_op_tex_hit_rate.pct,\
smsp__sass_average_data_bytes_per_wavefront_mem_shared_op_ld.pct \
\
\"${executable_with_args}\"

    mv ${run_prefix}_metrics_list ${gpuscout_tmp_dir}/${run_prefix}_metrics_list
fi


cd ${gpuscout_dir}/analysis

echo "======================================================================================================"
echo "Combining above results for register spilling analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_register_spilling.cpp -o merge_analysis_register_spilling
# nvcc --generate-line-info merge_analysis_register_spilling.cpp -o merge_analysis_register_spilling -lcuda -l:libcufilt.a
./merge_analysis_register_spilling \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${gpuscout_tmp_dir}/nvdisasm-registers-executable-${executable_filename}-sass.txt \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for using __restrict__ analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_use_restrict.cpp -o merge_analysis_use_restrict
./merge_analysis_use_restrict \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${gpuscout_tmp_dir}/nvdisasm-registers-hpctoolkit-${executable_filename}-sass.txt \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for vectorization analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_vectorization.cpp -o merge_analysis_vectorization
./merge_analysis_vectorization \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${gpuscout_tmp_dir}/nvdisasm-registers-hpctoolkit-${executable_filename}-sass.txt \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for global atomics analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_global_atomics.cpp -o merge_analysis_global_atomics
./merge_analysis_global_atomics \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for warp divergence analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_warp_divergence.cpp -o merge_analysis_warp_divergence
./merge_analysis_warp_divergence ${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for using texture memory analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_use_texture.cpp -o merge_analysis_use_texture
./merge_analysis_use_texture \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for using shared memory analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_use_shared.cpp -o merge_analysis_use_shared
./merge_analysis_use_shared \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for datatype conversion analysis . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_datatype_conversion.cpp -o merge_analysis_datatype_conversion
./merge_analysis_datatype_conversion \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${json} ${gpuscout_output_dir}

echo "======================================================================================================"
echo "Combining above results for deadlock detection . . . . . . . . . . . . . . . "
#g++ -std=c++17 ../merge_analysis_deadlock_detection.cpp -o merge_analysis_deadlock_detection
./merge_analysis_deadlock_detection \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
${gpuscout_tmp_dir}/${run_prefix}_metrics_list \
${json} ${gpuscout_output_dir}

# Merge all individual JSON files

if [ "$json" = true ]; then

echo "======================================================================================================"
echo "Generating JSON output . . . . . . . . . . . . . . . "

./save_to_json \
${gpuscout_output_dir} \
${gpuscout_tmp_dir}/result-${run_prefix}.json \
${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${executable_filename}-sass.txt \
${gpuscout_tmp_dir}/pcsampling_${executable_filename}.txt \
$copy_sources \
${gpuscout_tmp_dir}/nvdisasm-executable-${executable_filename}-ptx.txt \
${run_prefix} \
${gpuscout_tmp_dir}

fi

echo "======================================================================================================"

cd ..
