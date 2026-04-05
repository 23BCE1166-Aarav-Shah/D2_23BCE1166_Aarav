#pragma once

#include <filesystem>
#include <string>

namespace duplicate_library {

class HashEngine {
public:
    std::string hash_file(const std::filesystem::path& path) const;
};

}  // namespace duplicate_library
