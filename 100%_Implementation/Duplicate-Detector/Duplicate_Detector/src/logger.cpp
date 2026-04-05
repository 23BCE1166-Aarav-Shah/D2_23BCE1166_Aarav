#include "duplicate_finder/logger.hpp"

#include <ctime>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>

namespace duplicate_library {
namespace {

std::mutex& logger_mutex() {
    static std::mutex mutex;
    return mutex;
}

int& log_file_descriptor() {
    static int fd = -1;
    return fd;
}

std::string iso8601_utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm utc_tm{};
    gmtime_r(&time, &utc_tm);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    return buffer;
}

void write_all(int fd, const std::string& record) {
    const char* data = record.data();
    std::size_t remaining = record.size();
    while (remaining > 0) {
        const auto written = ::write(fd, data, remaining);
        if (written <= 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

}  // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::initialize(const std::filesystem::path& log_path) {
    std::lock_guard<std::mutex> lock(logger_mutex());

    log_path_ = log_path.empty() ? default_log_path() : log_path;
    std::filesystem::create_directories(log_path_.parent_path());

    int& fd = log_file_descriptor();
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }

    fd = ::open(log_path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
}

void Logger::log(
    LogLevel level,
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    std::lock_guard<std::mutex> lock(logger_mutex());

    if (log_path_.empty()) {
        log_path_ = default_log_path();
        std::filesystem::create_directories(log_path_.parent_path());
        int& fd = log_file_descriptor();
        if (fd < 0) {
            fd = ::open(log_path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
        }
    }

    std::string record = format_record(level, message, fields);
    record.push_back('\n');

    write_all(STDERR_FILENO, record);

    const int fd = log_file_descriptor();
    if (fd >= 0) {
        write_all(fd, record);
    }
}

std::filesystem::path Logger::default_log_path() const {
    const char* state_home = std::getenv("XDG_STATE_HOME");
    if (state_home != nullptr && std::strlen(state_home) > 0) {
        return std::filesystem::path(state_home) / "duplicate-finder" / "application.log";
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && std::strlen(home) > 0) {
        return std::filesystem::path(home) / ".local" / "state" / "duplicate-finder" / "application.log";
    }

    return std::filesystem::temp_directory_path() / "duplicate-finder-application.log";
}

std::string Logger::format_record(
    LogLevel level,
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields) const {
    std::ostringstream out;
    out << "{"
        << "\"ts\":\"" << iso8601_utc_now() << "\","
        << "\"level\":\"" << level_to_string(level) << "\","
        << "\"message\":\"" << json_escape(message) << "\"";

    if (!fields.empty()) {
        out << ",\"fields\":{";
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i != 0) {
                out << ",";
            }
            out << "\"" << json_escape(fields[i].first) << "\":"
                << "\"" << json_escape(fields[i].second) << "\"";
        }
        out << "}";
    }

    out << "}";
    return out.str();
}

std::string Logger::json_escape(const std::string& value) {
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
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

const char* Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
    }
    return "INFO";
}

void log_info(
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    Logger::instance().log(LogLevel::kInfo, message, fields);
}

void log_warn(
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    Logger::instance().log(LogLevel::kWarn, message, fields);
}

void log_error(
    const std::string& message,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    Logger::instance().log(LogLevel::kError, message, fields);
}

}  // namespace duplicate_library
