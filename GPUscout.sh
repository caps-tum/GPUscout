#!/bin/bash
gpuscout_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
gpuscout_tmp_dir="${gpuscout_dir}/tmp-gpuscout"
# Save metrics in a seperate directory
mkdir -p ${gpuscout_tmp_dir}
#@(#) This is the setup script that sets up HPCToolkit to generate the gpu binaries, disassembles them with nvdisasm, reads warp stalls and executes the measurement script


# Note: when you compile the code with nvcc, create 2 executables
# 1. without -cubin flag: <executable name>
# 2. with -cubin flag: prefix the name of the executable with cubin-<executable name>

echo "Taking name of the executable file"
# read file_name
file_name=cuda_test

echo "Taking arguments for the executable file"
# args="64 2 2 temp_64 power_64 output_64.txt"
# args="128 0.1"
# args=""

echo "Taking run type for the tool"
# Pass --dry_run as command-line argument to the script
# A --dry_run will only analyse the SASS instructions
# --dry_run will neither read warp stalls nor Nsight metrics 
runtype=$1
if [[ $runtype = "--dry_run" ]]; then
echo "Run type: dry_run selected . . . . . . . . . . . . . . . . . . . . "
fi

echo "Clearing previous files . . . . . . . . . . . . . . . . . . . . "
# DO NOT REMOVE sampling_utilities directory !!!!!!!
rm -rf hpctoolkit-*-measurements 2>/dev/null
rm -rf hpctoolkit-*-database 2>/dev/null
rm -rf metrics 2>/dev/null
rm nvdisasm-hpctoolkit-* 2>/dev/null
rm nvdisasm-executable-* 2>/dev/null
rm nvdisasm-registers-hpctoolkit-* 2>/dev/null
rm nvdisasm-registers-executable-* 2>/dev/null
rm parser_sass_datatype_conversion 2>/dev/null
rm parser_ptx 2>/dev/null
rm pcsampling_*.txt 2>/dev/null
rm parser_metrics 2>/dev/null
rm parser_sass_restrict 2>/dev/null
rm parser_sass_vectorized 2>/dev/null
rm parser_sass_divergence 2>/dev/null
rm parser_sass_register_spilling 2>/dev/null
rm parser_pcsampling 2>/dev/null
rm parser_sass_deadlock_detection 2>/dev/null
rm parser_sass_use_texture 2>/dev/null
rm parser_sass_use_shared 2>/dev/null
rm merge_analysis_register_spilling 2>/dev/null
rm merge_analysis_use_restrict 2>/dev/null
rm merge_analysis_vectorization 2>/dev/null
rm merge_analysis_global_atomics 2>/dev/null
rm merge_analysis_warp_divergence 2>/dev/null
rm merge_analysis_use_texture 2>/dev/null
rm merge_analysis_use_shared 2>/dev/null
rm merge_analysis_datatype_conversion 2>/dev/null

echo "======================================================================================================"
echo "Setting up profiling . . . . . . . . . . . . . . . "
# Remove and Create build directory
rm -rf $PWD/build/ && mkdir -p $PWD/build/

# ------------------------ WITH HPCTOOLKIT ------------------------
# # Save your executable in a directory named "executable"
# cd $PWD/build/
# echo "Loading spack . . . . . . . . . . . . . . . . . . . . "
# spack load hpctoolkit@2022.10.01
# # for ice1
# # module unload hpctoolkit
# # module load hpctoolkit/2022.01.15/gcc-11.2.0-module-y42vztd

# echo "Start profiling with hpctoolkit to generate the gpubin file . . . . . . . . . . . . . . . "
# hpcrun -e REALTIME -e gpu=nvidia,pc -t ./../executable/$file_name $args
# hpcstruct --gpucfg yes hpctoolkit-$file_name-measurements/
# hpcprof hpctoolkit-$file_name-measurements/

# # Disassemble to get the SASS and ptx and output it to txt file
# echo "SASS from hpctoolkit generated gpubin . . . . . . . . . . . . . . . "
# # remove the inlined information from nvdisasm
# # gpubins-used UNPREDICTABLE!
# # nvdisasm -g -c hpctoolkit-$file_name-measurements/gpubins-used/*.gpubin > nvdisasm-hpctoolkit-$file_name-sass.txt
# # for ice1
# nvdisasm -g -c hpctoolkit-$file_name-measurements/gpubins/*.gpubin > nvdisasm-hpctoolkit-$file_name-sass.txt

# echo "SASS directly from executable . . . . . . . . . . . . . . . "
# nvdisasm -g -c ./../executable/cubin-$file_name > nvdisasm-executable-$file_name-sass.txt

# echo "ptx directly from executable . . . . . . . . . . . . . . . "
# cuobjdump -ptx ./../executable/$file_name > nvdisasm-executable-$file_name-ptx.txt

# echo "SASS with live register info from hpctoolkit . . . . . . . . . . . . . . . "
# # nvdisasm -g -c -lrm=count hpctoolkit-$file_name-measurements/gpubins-used/*.gpubin > nvdisasm-registers-hpctoolkit-$file_name-sass.txt
# nvdisasm -g -c -lrm=count hpctoolkit-$file_name-measurements/gpubins/*.gpubin > nvdisasm-registers-hpctoolkit-$file_name-sass.txt

# echo "SASS with live register info from executable . . . . . . . . . . . . . . . "
# nvdisasm -g -c -lrm=count ./../executable/cubin-$file_name > nvdisasm-registers-executable-$file_name-sass.txt

# cd ..

# ------------------------ WITHOUT HPCTOOLKIT ------------------------
# Still using the name same as before (with hpctoolkit), just for ease
# Hence we are running the same commands below twice, once t generate the name with -hpctoolkit- and once to generate the name with -executable-
cd ${gpuscout_dir}
echo "1gpuscout_dir = ${gpuscout_dir}"
echo -e "Generating binaries . . . . . . . . . . . . . . . . . . . ."
nvdisasm -g -c ./../executable/cubin-${file_name} > ${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${file_name}-sass.txt
nvdisasm -g -c ./../executable/cubin-${file_name} > ${gpuscout_tmp_dir}/nvdisasm-executable-${file_name}-sass.txt
cuobjdump -ptx ./../executable/${file_name} > ${gpuscout_tmp_dir}/nvdisasm-executable-${file_name}-ptx.txt
nvdisasm -g -c -lrm=count ./../executable/cubin-${file_name} > ${gpuscout_tmp_dir}/nvdisasm-registers-hpctoolkit-${file_name}-sass.txt
nvdisasm -g -c -lrm=count ./../executable/cubin-${file_name} > ${gpuscout_tmp_dir}/nvdisasm-registers-executable-${file_name}-sass.txt

# Run the generate_sampling_stalls script inside the sampling_utilities directory
if [[ $runtype != "--dry_run" ]]; then
echo "Getting warp stall reasons . . . . . . . . . . . . . . . "
source ${gpuscout_dir}/sampling_utilities/generate_sampling_stalls.sh
cp ${gpuscout_dir}/sampling_utilities/sampling_utility/pcsampling_${file_name}.txt ${gpuscout_tmp_dir}/pcsampling_${file_name}.txt
fi

# Get the measurements and analysis from the measurements script
echo "Measurements and Analysis . . . . . . . . . . . . . . . "
source ${gpuscout_dir}/analysis/measurements.sh

# Exit
echo -e "Profiling complete! Starting cleanup . . . . . . . . . . . . . . . . . . . ."

# Remove the PC sampling files generate before release/production, else keep them for debug
# rm $PWD/sampling_utilities/sampling_continuous/*_pcsampling_*.dat
# rm $PWD/sampling_utilities/sampling_utility/pcsampling_$file_name.txt

exit 0
