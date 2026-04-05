#pragma once

#include "duplicate_library.hpp"

namespace duplicate_library {

class FileScanner;
class HashEngine;

class DuplicateDetectorImpl {
public:
    DuplicateDetectorImpl(FileScanner& scanner, HashEngine& hash_engine);

    void set_progress_callback(DuplicateDetector::ProgressCallback cb);
    void set_cancel_callback(DuplicateDetector::CancelCallback cb);
    std::vector<DuplicateGroup> scan(const std::string& path);

private:
    void report_progress(std::size_t processed, std::size_t total);
    bool is_cancelled() const;

    FileScanner& scanner_;
    HashEngine& hash_engine_;
    DuplicateDetector::ProgressCallback progress_callback_;
    DuplicateDetector::CancelCallback cancel_callback_;
    int last_reported_progress_ = -1;
};

}  // namespace duplicate_library
