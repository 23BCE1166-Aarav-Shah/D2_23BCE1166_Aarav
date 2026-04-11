#include "duplicate_detector.hpp"

#include "file_scanner.hpp"
#include "hash_engine.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <unordered_map>

namespace duplicate_library {

namespace {

using SizeBuckets = std::unordered_map<std::uintmax_t, std::vector<FileEntry>>;
using HashBuckets = std::unordered_map<std::string, std::vector<std::string>>;

constexpr std::size_t kCompareBufferSize = 64 * 1024;

bool files_are_equal(const std::string& lhs_path, const std::string& rhs_path) {
    std::ifstream lhs(lhs_path, std::ios::binary);
    std::ifstream rhs(rhs_path, std::ios::binary);
    if (!lhs.is_open() || !rhs.is_open()) {
        return false;
    }

    std::array<char, kCompareBufferSize> lhs_buffer{};
    std::array<char, kCompareBufferSize> rhs_buffer{};

    while (lhs.good() || rhs.good()) {
        lhs.read(lhs_buffer.data(), static_cast<std::streamsize>(lhs_buffer.size()));
        rhs.read(rhs_buffer.data(), static_cast<std::streamsize>(rhs_buffer.size()));

        const auto lhs_count = lhs.gcount();
        const auto rhs_count = rhs.gcount();
        if (lhs_count != rhs_count) {
            return false;
        }
        if (!std::equal(lhs_buffer.begin(), lhs_buffer.begin() + lhs_count, rhs_buffer.begin())) {
            return false;
        }
        if (lhs_count == 0) {
            break;
        }
    }

    return !lhs.bad() && !rhs.bad();
}

std::vector<std::vector<std::string>> partition_identical_files(std::vector<std::string> files) {
    std::vector<std::vector<std::string>> partitions;
    std::vector<bool> grouped(files.size(), false);

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (grouped[i]) {
            continue;
        }

        std::vector<std::string> group{files[i]};
        grouped[i] = true;

        for (std::size_t j = i + 1; j < files.size(); ++j) {
            if (grouped[j]) {
                continue;
            }
            if (files_are_equal(files[i], files[j])) {
                grouped[j] = true;
                group.push_back(files[j]);
            }
        }

        if (group.size() > 1) {
            std::sort(group.begin(), group.end());
            partitions.push_back(std::move(group));
        }
    }

    return partitions;
}

}  // namespace

DuplicateDetectorImpl::DuplicateDetectorImpl(FileScanner& scanner, HashEngine& hash_engine)
    : scanner_(scanner), hash_engine_(hash_engine) {}

void DuplicateDetectorImpl::set_progress_callback(DuplicateDetector::ProgressCallback cb) {
    progress_callback_ = std::move(cb);
}

void DuplicateDetectorImpl::set_cancel_callback(DuplicateDetector::CancelCallback cb) {
    cancel_callback_ = std::move(cb);
}

std::vector<DuplicateGroup> DuplicateDetectorImpl::scan(const std::string& path) {
    last_reported_progress_ = -1;

    const auto files = scanner_.scan(path, [this]() { return is_cancelled(); });
    const auto total_files = files.size();
    report_progress(0, total_files);

    SizeBuckets size_buckets;
    for (const auto& file : files) {
        if (is_cancelled()) {
            return {};
        }
        size_buckets[file.size].push_back(file);
    }

    std::vector<DuplicateGroup> duplicates;
    std::size_t processed = 0;

    std::vector<std::uintmax_t> ordered_sizes;
    ordered_sizes.reserve(size_buckets.size());
    for (const auto& bucket : size_buckets) {
        if (bucket.second.size() > 1) {
            ordered_sizes.push_back(bucket.first);
        }
    }
    std::sort(ordered_sizes.begin(), ordered_sizes.end());

    for (const auto size : ordered_sizes) {
        const auto& candidates = size_buckets[size];
        HashBuckets hash_buckets;

        for (const auto& candidate : candidates) {
            if (is_cancelled()) {
                return {};
            }
            const auto hash = hash_engine_.hash_file(candidate.path);
            ++processed;
            report_progress(processed, total_files);

            if (!hash.empty()) {
                hash_buckets[hash].push_back(candidate.path.lexically_normal().string());
            }
        }

        std::vector<std::string> ordered_hashes;
        ordered_hashes.reserve(hash_buckets.size());
        for (const auto& bucket : hash_buckets) {
            if (bucket.second.size() > 1) {
                ordered_hashes.push_back(bucket.first);
            }
        }
        std::sort(ordered_hashes.begin(), ordered_hashes.end());

        for (const auto& hash : ordered_hashes) {
            auto partitions = partition_identical_files(hash_buckets[hash]);
            for (auto& group_files : partitions) {
                duplicates.push_back(DuplicateGroup{size, hash, std::move(group_files)});
            }
        }
    }

    report_progress(total_files, total_files);

    std::sort(duplicates.begin(), duplicates.end(), [](const DuplicateGroup& lhs, const DuplicateGroup& rhs) {
        if (lhs.file_size != rhs.file_size) {
            return lhs.file_size < rhs.file_size;
        }
        if (lhs.content_hash != rhs.content_hash) {
            return lhs.content_hash < rhs.content_hash;
        }
        return lhs.files < rhs.files;
    });

    return duplicates;
}

bool DuplicateDetectorImpl::is_cancelled() const {
    return cancel_callback_ && cancel_callback_();
}

void DuplicateDetectorImpl::report_progress(std::size_t processed, std::size_t total) {
    if (!progress_callback_) {
        return;
    }

    const int percent =
        total == 0 ? 100 : static_cast<int>((processed * 100) / total);
    if (percent != last_reported_progress_) {
        last_reported_progress_ = percent;
        progress_callback_(percent);
    }
}

void DuplicateDetector::set_progress_callback(ProgressCallback cb) {
    progress_callback_ = std::move(cb);
}

void DuplicateDetector::set_cancel_callback(CancelCallback cb) {
    cancel_callback_ = std::move(cb);
}

std::vector<DuplicateGroup> DuplicateDetector::scan(const std::string& path) {
    FileScanner scanner;
    HashEngine hash_engine;
    DuplicateDetectorImpl detector(scanner, hash_engine);
    detector.set_progress_callback(progress_callback_);
    detector.set_cancel_callback(cancel_callback_);
    return detector.scan(path);
}

std::vector<DuplicateGroup> scan(const std::string& path) {
    DuplicateDetector detector;
    return detector.scan(path);
}

}  // namespace duplicate_library
