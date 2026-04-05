#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace duplicate_library {

struct FileEntry {
    std::filesystem::path path;
    std::uintmax_t size = 0;
};

class FileScanner {
public:
    std::vector<FileEntry> scan(const std::filesystem::path& root) const;
    std::vector<FileEntry> scan(
        const std::filesystem::path& root,
        const std::function<bool()>& should_cancel) const;
};

}  // namespace duplicate_library
