#pragma once

#include "duplicate_finder/types.hpp"

#include <filesystem>
#include <functional>
#include <vector>

namespace duplicate_library {

class FileScanner {
public:
    using CancelCallback = std::function<bool()>;

    std::vector<FileEntry> scan(const std::filesystem::path& root) const;
    std::vector<FileEntry> scan(
        const std::filesystem::path& root,
        const CancelCallback& should_cancel) const;
};

}  // namespace duplicate_library
