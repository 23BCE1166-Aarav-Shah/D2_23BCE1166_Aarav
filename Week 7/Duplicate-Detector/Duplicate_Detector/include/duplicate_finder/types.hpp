#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace duplicate_library {

enum class FileKind {
    kBinary,
    kImage,
    kVideo,
    kAudio
};

struct FileEntry {
    std::filesystem::path path;
    std::uintmax_t size = 0;
    FileKind kind = FileKind::kBinary;
};

struct HashResult {
    std::string strict_hash;
    std::string visual_hash;
    std::string audio_hash;
};

struct DuplicateMatchGroup {
    FileKind kind = FileKind::kBinary;
    std::uintmax_t file_size = 0;
    std::string signature;
    std::vector<FileEntry> files;
};

}  // namespace duplicate_library
