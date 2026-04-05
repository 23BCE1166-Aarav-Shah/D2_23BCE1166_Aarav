#pragma once

#include "duplicate_finder/file_scanner.hpp"
#include "duplicate_finder/hash_engine.hpp"
#include "duplicate_finder/types.hpp"

#include <functional>
#include <vector>

namespace duplicate_library {

class DuplicateDetector {
public:
    using ProgressCallback = std::function<void(int)>;
    using CancelCallback = std::function<bool()>;

    explicit DuplicateDetector(
        const FileScanner& scanner = FileScanner(),
        const HashEngine& hash_engine = HashEngine(),
        std::size_t worker_count = 0);

    void set_progress_callback(ProgressCallback cb);
    void set_cancel_callback(CancelCallback cb);

    std::vector<DuplicateMatchGroup> scan(const std::filesystem::path& root) const;

private:
    const FileScanner* scanner_;
    const HashEngine* hash_engine_;
    std::size_t worker_count_;
    ProgressCallback progress_callback_;
    CancelCallback cancel_callback_;
};

}  // namespace duplicate_library
