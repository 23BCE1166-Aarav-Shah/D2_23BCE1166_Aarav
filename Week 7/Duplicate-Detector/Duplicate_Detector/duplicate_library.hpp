#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace duplicate_library {

struct DuplicateGroup {
    std::uintmax_t file_size = 0;
    std::string content_hash;
    std::vector<std::string> files;
};

class DuplicateDetector {
public:
    using ProgressCallback = std::function<void(int)>;
    using CancelCallback = std::function<bool()>;

    void set_progress_callback(ProgressCallback cb);
    void set_cancel_callback(CancelCallback cb);
    std::vector<DuplicateGroup> scan(const std::string& path);

private:
    ProgressCallback progress_callback_;
    CancelCallback cancel_callback_;
};

std::vector<DuplicateGroup> scan(const std::string& path);

}  // namespace duplicate_library
