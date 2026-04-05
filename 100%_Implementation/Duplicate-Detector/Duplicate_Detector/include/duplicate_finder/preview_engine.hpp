#pragma once

#include "duplicate_finder/config.hpp"
#include "duplicate_finder/types.hpp"

#include <filesystem>
#include <string>

namespace duplicate_library {

enum class PreviewKind {
    kImageThumbnail,
    kVideoFrame,
    kAudioWaveform,
    kUnsupported
};

struct PreviewResult {
    std::filesystem::path cache_path;
    PreviewKind kind = PreviewKind::kUnsupported;
    bool from_cache = false;
};

class PreviewEngine {
public:
    PreviewEngine();
    explicit PreviewEngine(const AppConfig& config);

    PreviewResult generate(const FileEntry& file) const;
    const std::filesystem::path& cache_directory() const;

private:
    std::filesystem::path cache_directory_;
};

}  // namespace duplicate_library
