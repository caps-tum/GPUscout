// Contains helper functions used in both the nvidia and amd version

#ifndef GPUSCOUT_HELPER_HPP
#define GPUSCOUT_HELPER_HPP

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include "json.hpp"

// https://www.jeremymorgan.com/tutorials/c-programming/how-to-capture-the-output-of-a-linux-command-in-c/
std::string get_demangled_kernel(std::string kernel_name, std::string utility = "cu++filt") {
    std::string command = utility + " " + kernel_name;
    std::string result;
    FILE* stream;
    const int max_buffer = 256;
    char buffer[max_buffer];

    stream = popen(command.c_str(), "r");

    if (stream) {
        while (!feof(stream)) {
            if (fgets(buffer, max_buffer, stream) != NULL)
                result.append(buffer);
        }
        pclose(stream);
    }
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    return result;
}

inline bool save_static_result(const std::filesystem::path& filename, const nlohmann::json& result)
{
    std::filesystem::create_directories(filename.parent_path());
    std::ofstream file(filename);

    if (!file)
    {
        return false;
    }

    file << result.dump(4);
    return true;
}

inline bool load_static_result(const std::filesystem::path& filename, nlohmann::json& result)
{
    std::ifstream file(filename);

    if (!file)
    {
        return false;
    }

    try
    {
        file >> result;
        return true;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }
}

/*!
 * Return the path of the temporary static result.
 * Example: assembly file path: /tmp-gpuscout/kernel.s
 * Result: /tmp-gpuscout/static_results/register_spilling.json
 */
inline std::filesystem::path static_result_path(const std::string& assembly, std::string result_filename)
{
    return std::filesystem::path(assembly).parent_path()
           / "static_results"
           / (result_filename + ".json"); 
}


#endif //GPUSCOUT_HELPER_HPP