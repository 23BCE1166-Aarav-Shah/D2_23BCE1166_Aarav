#pragma once

#include "duplicate_finder/types.hpp"

#include <filesystem>
#include <string>

namespace duplicate_library {

class HashEngine {
public:
    FileKind classify(const std::filesystem::path& path) const;

    std::string compute_xxhash(const std::filesystem::path& path) const;
    std::string compute_sample_xxhash(
        const std::filesystem::path& path,
        std::uintmax_t file_size) const;
    std::string compute_visual_hash(const std::filesystem::path& path) const;
    std::string compute_audio_hash(const std::filesystem::path& path) const;

    HashResult compute_hashes(const FileEntry& file) const;
};

}  // namespace duplicate_library
