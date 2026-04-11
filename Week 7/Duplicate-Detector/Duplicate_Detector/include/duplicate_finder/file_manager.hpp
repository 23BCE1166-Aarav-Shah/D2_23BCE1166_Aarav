#pragma once

#include <filesystem>

namespace duplicate_library {

class FileManager {
public:
    bool move_to_directory(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_directory) const;

    bool remove_file(const std::filesystem::path& path) const;
};

}  // namespace duplicate_library
