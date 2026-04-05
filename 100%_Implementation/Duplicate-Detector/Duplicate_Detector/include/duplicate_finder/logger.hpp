#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace duplicate_library {

enum class LogLevel {
    kInfo,
    kWarn,
    kError
};

class Logger {
public:
    static Logger& instance();

    void initialize(const std::filesystem::path& log_path = {});
    void log(
        LogLevel level,
        const std::string& message,
        const std::vector<std::pair<std::string, std::string>>& fields = {});

private:
    Logger() = default;

    std::filesystem::path default_log_path() const;
    std::string format_record(
        LogLevel level,
        const std::string& message,
        const std::vector<std::pair<std::string, std::string>>& fields) const;
    static std::string json_escape(const std::string& value);
    static const char* level_to_string(LogLevel level);

    std::filesystem::path log_path_;
};

void log_info(
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields = {});
void log_warn(
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields = {});
void log_error(
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields = {});

}  // namespace duplicate_library
