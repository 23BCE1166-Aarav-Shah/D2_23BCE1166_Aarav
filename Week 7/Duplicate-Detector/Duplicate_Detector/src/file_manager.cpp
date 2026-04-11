#include "duplicate_finder/file_manager.hpp"

#include <system_error>

namespace duplicate_library {

bool FileManager::move_to_directory(
    const std::filesystem::path& source,
    const std::filesystem::path& destination_directory) const {
    std::error_code error;
    std::filesystem::create_directories(destination_directory, error);
    if (error) {
        return false;
    }

    const auto destination = destination_directory / source.filename();
    std::filesystem::rename(source, destination, error);
    if (!error) {
        return true;
    }

    error.clear();
    std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
        return false;
    }

    error.clear();
    std::filesystem::remove(source, error);
    return !error;
}

bool FileManager::remove_file(const std::filesystem::path& path) const {
    std::error_code error;
    return std::filesystem::remove(path, error) && !error;
}

}  // namespace duplicate_library
