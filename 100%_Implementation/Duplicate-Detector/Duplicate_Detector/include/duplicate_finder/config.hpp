#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace duplicate_library {

struct ScanSettings {
    std::size_t worker_count = 0;
    std::uintmax_t min_file_size_bytes = 1;
    std::uintmax_t max_file_size_bytes = 0;
    bool enable_cache = true;
};

struct ThresholdSettings {
    std::size_t minimum_group_size = 2;
};

struct PathSettings {
    std::filesystem::path cache_database_path;
    std::filesystem::path preview_cache_dir;
    std::filesystem::path log_file_path;
    std::filesystem::path default_scan_root;
};

struct AppConfig {
    ScanSettings scan;
    ThresholdSettings thresholds;
    PathSettings paths;
};

class ConfigManager {
public:
    static AppConfig load(const std::filesystem::path& path);
    static bool save(const std::filesystem::path& path, const AppConfig& config);
    static AppConfig defaults();
    static std::filesystem::path default_config_path();
};

}  // namespace duplicate_library
