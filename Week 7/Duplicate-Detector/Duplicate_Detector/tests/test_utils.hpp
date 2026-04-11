#pragma once

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <string>

namespace duplicate_library::test {

class ScopedTempDir {
public:
    ScopedTempDir() {
        const auto unique_suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = std::filesystem::temp_directory_path() /
                ("duplicate-finder-tests-" + unique_suffix);
        std::filesystem::create_directories(path_);
    }

    ~ScopedTempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

inline void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const std::string& value)
        : name_(name) {
        const char* current = std::getenv(name_);
        if (current != nullptr) {
            had_original_ = true;
            original_ = current;
        }

        setenv(name_, value.c_str(), 1);
    }

    ~ScopedEnvVar() {
        if (had_original_) {
            setenv(name_, original_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    bool had_original_ = false;
    std::string original_;
};

}  // namespace duplicate_library::test
