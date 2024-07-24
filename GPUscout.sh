#!/bin/bash

# Define the usage function
usage() {
    echo "Usage: $0 [-h] [--dry-run] [--verbose] -e executable [-c directory] [--args]"
    echo "  -h | --help : Display this help."
    echo "  --dry_run : performs only dry_run. A --dry_run will only analyse the SASS instructions. --dry_run will neither read warp stalls nor Nsight metrics "
    echo "  -v | --verbose : print more verbose output. "
    echo "  -e | --executable : Path to the executable (compiled with nvcc)."
    echo "  -c | --cubin : Path to the cubin file (compiled with nvcc, with -cubin). If left empty, the same path as executable and the name cubin-<executable> will be assumed."
    echo "  -a | --args : Arguments for running the binary. e.g. --args=\"64 2 2 temp_64 power_64 output_64.txt\""
    echo "  -j | --json : Save a JSON-formatted version of the output"
    exit 1
}

# Parse command-line options
options=$(getopt -o hve:c:a:j -l help,dry_run,verbose,executable:,cubin:,args:,json -- "$@")

if [ $? -ne 0 ]; then
    echo "Error: Invalid option."
    usage
fi

eval set -- "$options"

dry_run=false
verbose=false
json=false
executable=""
cubin=""
args=""
while true; do
    case "$1" in
        -h | --help)
            usage
            ;;
        -e | --executable)
            executable="$2"
            shift 2
            ;;
        -c | --cubin)
            cubin="$2"
            shift 2
            ;;
        -a | --args)
            args="$2"
            shift 2
            ;;
        --dry_run)
            dry_run=true
            shift
            ;;
        -v | --verbose)
            verbose=true
            shift
            ;;
         -j | --json)
            json=true
            shift
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Error: Internal error during parsing."
            exit 1
            ;;
    esac
done

#check the params
if [ -z "$executable" ]; then
    echo "No executable specified (-e ..)"
    usage
fi
executable_filename=$(basename "$executable")
executable_dir="$( cd "$( dirname "$executable" )" && pwd )"
executable="$executable_dir/$executable_filename"
run_prefix=$executable_filename
if [ ! -f "$executable" ]; then
    echo "Executable not found at: $executable"
    exit 1
fi

if [ -z "$cubin" ]; then
    cubin_filename="cubin-$executable_filename"
    cubin_dir=$executable_dir
else
    cubin_filename=$(basename "$cubin")
    cubin_dir="$( cd "$( dirname "$cubin" )" && pwd )"

fi
cubin="$cubin_dir/$cubin_filename"
if [ ! -f "$cubin" ]; then
    echo "Cubin file not found at: $cubin"
    exit 1
fi

gpuscout_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
gpuscout_tmp_dir="${gpuscout_dir}/tmp-gpuscout"
gpuscout_output_dir="${gpuscout_tmp_dir}/output"
# Save metrics in a seperate directory
echo "======================================================================================================"
echo "==== Creating GPUscout TMP directory for storing metrics: ${gpuscout_tmp_dir}"
mkdir -p ${gpuscout_tmp_dir}

if [ "$json" = true ]; then
    mkdir -p ${gpuscout_output_dir}
fi

# Note: when you compile the code with nvcc, create 2 executables
# 1. without -cubin flag: <executable name>
# 2. with -cubin flag: prefix the name of the executable with cubin-<executable name>

echo "==== Executable: $executable"
echo "==== Cubin:      $cubin"
echo "==== Arguments for the executable file: \"$args\""
echo "==== Dry-run: $dry_run"
echo "==== Verbose: $verbose"
echo "==== JSON Output: $json"
echo "======================================================================================================"


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

echo "Setting up profiling . . . . . . . . . . . . . . . "
# Remove and Create build directory
#rm -rf $PWD/build/ && mkdir -p $PWD/build/

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
echo -e "Generating binaries . . . . . . . . . . . . . . . . . . . ."
nvdisasm -g -c ${cubin} > ${gpuscout_tmp_dir}/nvdisasm-hpctoolkit-${run_prefix}-sass.txt #TODO this line necessary?
nvdisasm -g -c ${cubin} > ${gpuscout_tmp_dir}/nvdisasm-executable-${run_prefix}-sass.txt
cuobjdump -ptx ${executable} > ${gpuscout_tmp_dir}/nvdisasm-executable-${run_prefix}-ptx.txt
nvdisasm -g -c -lrm=count ${cubin} > ${gpuscout_tmp_dir}/nvdisasm-registers-hpctoolkit-${run_prefix}-sass.txt #TODO this line necessary?
nvdisasm -g -c -lrm=count ${cubin} > ${gpuscout_tmp_dir}/nvdisasm-registers-executable-${run_prefix}-sass.txt


# Run the generate_sampling_stalls script inside the sampling_utilities directory
if [ "$dry_run" = false ]; then
    echo "Getting warp stall reasons . . . . . . . . . . . . . . . "
    source ${gpuscout_dir}/sampling_utilities/generate_sampling_stalls.sh
    cp ${gpuscout_dir}/sampling_utilities/sampling_utility/pcsampling_${run_prefix}.txt ${gpuscout_tmp_dir}/pcsampling_${run_prefix}.txt
fi

# Get the measurements and analysis from the measurements script
echo "Measurements and Analysis . . . . . . . . . . . . . . . (${gpuscout_dir}/analysis/measurements.sh)"
source ${gpuscout_dir}/analysis/measurements.sh

# Exit
echo -e "Profiling complete! Starting cleanup . . . . . . . . . . . . . . . . . . . ."

# Remove the PC sampling files generate before release/production, else keep them for debug
# rm $PWD/sampling_utilities/sampling_continuous/*_pcsampling_*.dat
# rm $PWD/sampling_utilities/sampling_utility/pcsampling_$file_name.txt

exit 0
