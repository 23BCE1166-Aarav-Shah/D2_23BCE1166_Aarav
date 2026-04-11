#pragma once

#include "duplicate_finder/config.hpp"
#include "duplicate_finder/duplicate_detector.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace duplicate_library {

struct DuplicateGroup {
    std::vector<std::string> files;
    std::string type;
};

class DuplicateEngine {
public:
    explicit DuplicateEngine(std::size_t worker_count = 0);
    explicit DuplicateEngine(const AppConfig& config);

    void setProgressCallback(std::function<void(int)> cb);
    void setCancelCallback(std::function<bool()> cb);
    void setConfig(const AppConfig& config);
    std::vector<DuplicateGroup> scan(const std::vector<std::string>& paths);

private:
    std::size_t worker_count_;
    AppConfig config_;
    std::function<void(int)> progress_callback_;
    std::function<bool()> cancel_callback_;
};

}  // namespace duplicate_library
