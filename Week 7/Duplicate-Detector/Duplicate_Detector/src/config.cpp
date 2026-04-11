#include "duplicate_finder/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace duplicate_library {
namespace {

std::filesystem::path xdg_config_home() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return xdg;
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config";
    }

    return std::filesystem::temp_directory_path();
}

std::filesystem::path xdg_cache_home() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return xdg;
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".cache";
    }

    return std::filesystem::temp_directory_path();
}

std::filesystem::path xdg_state_home() {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return xdg;
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state";
    }

    return std::filesystem::temp_directory_path();
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string extract_string(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const auto key_pos = json.find(pattern);
    if (key_pos == std::string::npos) {
        return {};
    }

    const auto colon_pos = json.find(':', key_pos + pattern.size());
    const auto value_start = json.find('"', colon_pos + 1);
    if (colon_pos == std::string::npos || value_start == std::string::npos) {
        return {};
    }

    std::string value;
    bool escaped = false;
    for (std::size_t i = value_start + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            value.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }

    return {};
}

std::uintmax_t extract_uint(const std::string& json, const std::string& key, std::uintmax_t fallback) {
    const std::string pattern = "\"" + key + "\"";
    const auto key_pos = json.find(pattern);
    if (key_pos == std::string::npos) {
        return fallback;
    }

    const auto colon_pos = json.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return fallback;
    }

    const auto value_start = json.find_first_of("0123456789", colon_pos + 1);
    if (value_start == std::string::npos) {
        return fallback;
    }

    const auto value_end = json.find_first_not_of("0123456789", value_start);
    return static_cast<std::uintmax_t>(std::stoull(json.substr(value_start, value_end - value_start)));
}

bool extract_bool(const std::string& json, const std::string& key, bool fallback) {
    const std::string pattern = "\"" + key + "\"";
    const auto key_pos = json.find(pattern);
    if (key_pos == std::string::npos) {
        return fallback;
    }

    const auto colon_pos = json.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return fallback;
    }

    const auto value_start = json.find_first_not_of(" \t\r\n", colon_pos + 1);
    if (value_start == std::string::npos) {
        return fallback;
    }

    if (json.compare(value_start, 4, "true") == 0) {
        return true;
    }
    if (json.compare(value_start, 5, "false") == 0) {
        return false;
    }
    return fallback;
}

std::string escape_json(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

}  // namespace

AppConfig ConfigManager::defaults() {
    AppConfig config;
    config.paths.cache_database_path = xdg_cache_home() / "duplicate-finder" / "cache.sqlite3";
    config.paths.preview_cache_dir = xdg_cache_home() / "duplicate-finder" / "previews";
    config.paths.log_file_path = xdg_state_home() / "duplicate-finder" / "application.log";
    return config;
}

std::filesystem::path ConfigManager::default_config_path() {
    return xdg_config_home() / "duplicate-finder" / "config.json";
}

AppConfig ConfigManager::load(const std::filesystem::path& path) {
    AppConfig config = defaults();
    const std::string json = read_file(path);
    if (json.empty()) {
        return config;
    }

    config.scan.worker_count = static_cast<std::size_t>(
        extract_uint(json, "worker_count", config.scan.worker_count));
    config.scan.min_file_size_bytes = extract_uint(
        json,
        "min_file_size_bytes",
        config.scan.min_file_size_bytes);
    config.scan.max_file_size_bytes = extract_uint(
        json,
        "max_file_size_bytes",
        config.scan.max_file_size_bytes);
    config.scan.enable_cache = extract_bool(json, "enable_cache", config.scan.enable_cache);
    config.thresholds.minimum_group_size = static_cast<std::size_t>(
        extract_uint(json, "minimum_group_size", config.thresholds.minimum_group_size));

    const std::string cache_path = extract_string(json, "cache_database_path");
    if (!cache_path.empty()) {
        config.paths.cache_database_path = cache_path;
    }

    const std::string preview_path = extract_string(json, "preview_cache_dir");
    if (!preview_path.empty()) {
        config.paths.preview_cache_dir = preview_path;
    }

    const std::string log_path = extract_string(json, "log_file_path");
    if (!log_path.empty()) {
        config.paths.log_file_path = log_path;
    }

    const std::string scan_root = extract_string(json, "default_scan_root");
    if (!scan_root.empty()) {
        config.paths.default_scan_root = scan_root;
    }

    return config;
}

bool ConfigManager::save(const std::filesystem::path& path, const AppConfig& config) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }

    output
        << "{\n"
        << "  \"scan\": {\n"
        << "    \"worker_count\": " << config.scan.worker_count << ",\n"
        << "    \"min_file_size_bytes\": " << config.scan.min_file_size_bytes << ",\n"
        << "    \"max_file_size_bytes\": " << config.scan.max_file_size_bytes << ",\n"
        << "    \"enable_cache\": " << (config.scan.enable_cache ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"thresholds\": {\n"
        << "    \"minimum_group_size\": " << config.thresholds.minimum_group_size << "\n"
        << "  },\n"
        << "  \"paths\": {\n"
        << "    \"cache_database_path\": \"" << escape_json(config.paths.cache_database_path.string()) << "\",\n"
        << "    \"preview_cache_dir\": \"" << escape_json(config.paths.preview_cache_dir.string()) << "\",\n"
        << "    \"log_file_path\": \"" << escape_json(config.paths.log_file_path.string()) << "\",\n"
        << "    \"default_scan_root\": \"" << escape_json(config.paths.default_scan_root.string()) << "\"\n"
        << "  }\n"
        << "}\n";

    return output.good();
}

}  // namespace duplicate_library
