#include "duplicate_finder/duplicate_detector.hpp"

#include "src/thread_pool.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <future>
#include <mutex>
#include <unordered_map>


namespace duplicate_library {
namespace {

constexpr std::size_t kCompareBufferSize = 64 * 1024;

using SizeBuckets = std::unordered_map<std::uintmax_t, std::vector<FileEntry>>;
using SignatureBuckets = std::unordered_map<std::string, std::vector<FileEntry>>;

bool files_are_equal(const std::filesystem::path& lhs_path, const std::filesystem::path& rhs_path) {
    std::ifstream lhs(lhs_path, std::ios::binary);
    std::ifstream rhs(rhs_path, std::ios::binary);
    if (!lhs.is_open() || !rhs.is_open()) return false;

    std::array<char, kCompareBufferSize> lhs_buffer{};
    std::array<char, kCompareBufferSize> rhs_buffer{};

    while (lhs.good() || rhs.good()) {
        lhs.read(lhs_buffer.data(), lhs_buffer.size());
        rhs.read(rhs_buffer.data(), rhs_buffer.size());

        if (lhs.gcount() != rhs.gcount()) return false;

        if (!std::equal(lhs_buffer.begin(), lhs_buffer.begin() + lhs.gcount(), rhs_buffer.begin()))
            return false;

        if (lhs.gcount() == 0) break;
    }

    return true;
}

std::vector<std::vector<FileEntry>> partition_exact_files(const std::vector<FileEntry>& files) {
    std::vector<std::vector<FileEntry>> partitions;
    std::vector<bool> used(files.size(), false);

    for (size_t i = 0; i < files.size(); i++) {
        if (used[i]) continue;

        std::vector<FileEntry> group{files[i]};
        used[i] = true;

        for (size_t j = i + 1; j < files.size(); j++) {
            if (used[j]) continue;

            if (files_are_equal(files[i].path, files[j].path)) {
                used[j] = true;
                group.push_back(files[j]);
            }
        }

        if (group.size() > 1) partitions.push_back(group);
    }

    return partitions;
}

void sort_groups(std::vector<DuplicateMatchGroup>& groups) {
    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
        return a.files.size() > b.files.size();
    });
}

}  // namespace

DuplicateDetector::DuplicateDetector(
    const FileScanner& scanner,
    const HashEngine& hash_engine,
    std::size_t worker_count)
    : scanner_(&scanner),
      hash_engine_(&hash_engine),
      worker_count_(worker_count ? worker_count : std::thread::hardware_concurrency()) {}

void DuplicateDetector::set_progress_callback(ProgressCallback cb) {
    progress_callback_ = std::move(cb);
}

void DuplicateDetector::set_cancel_callback(CancelCallback cb) {
    cancel_callback_ = std::move(cb);
}

std::vector<DuplicateMatchGroup> DuplicateDetector::scan(const std::filesystem::path& root) const {
    const auto files = scanner_->scan(root, cancel_callback_);

    std::unordered_map<FileKind, SizeBuckets> kind_buckets;
    for (const auto& file : files) {
        const auto bucket_key = file.kind == FileKind::kBinary ? file.size : 0;
        kind_buckets[file.kind][bucket_key].push_back(file);
    }

    ThreadPool pool(worker_count_);
    std::mutex group_mutex;
    std::vector<DuplicateMatchGroup> groups;
    std::vector<std::future<void>> futures;

    auto process_bucket = [&](FileKind kind, std::uintmax_t size, std::vector<FileEntry> candidates) {
        std::vector<DuplicateMatchGroup> local_groups;

        /* =======================
           🔥 FUZZY IMAGE MATCHING
           ======================= */
        if (kind == FileKind::kImage || kind == FileKind::kVideo) {

            std::vector<std::pair<FileEntry, uint64_t>> vhash;

            for (const auto& file : candidates) {
                const auto hashes = hash_engine_->compute_hashes(file);

                if (!hashes.visual_hash.empty()) {
                    try {
                        uint64_t h = std::stoull(hashes.visual_hash, nullptr, 16);
                        vhash.emplace_back(file, h);
                    } catch (...) {
                        continue;
                    }
                }
            }

            std::vector<bool> visited(vhash.size(), false);

            for (size_t i = 0; i < vhash.size(); i++) {

                if (visited[i]) continue;

                std::vector<FileEntry> group{vhash[i].first};
                visited[i] = true;

                for (size_t j = i + 1; j < vhash.size(); j++) {

                    if (visited[j]) continue;

                    uint64_t diff = vhash[i].second ^ vhash[j].second;
                    int dist = __builtin_popcountll(diff);

                    if (dist <= 10) {  // 🔥 threshold
                        group.push_back(vhash[j].first);
                        visited[j] = true;
                        
                    }
                }

                if (group.size() > 1) {
                    local_groups.push_back(
                        DuplicateMatchGroup{kind, size, "visual_fuzzy", std::move(group)}
                    );
                }
            }
        }

        /* =======================
           🔹 EXACT MATCH (BINARY)
           ======================= */
        else if (kind == FileKind::kBinary) {

            auto exact_groups = partition_exact_files(candidates);

            for (auto& g : exact_groups) {
                local_groups.push_back(
                    DuplicateMatchGroup{kind, size, "exact", std::move(g)}
                );
            }
        }

        /* =======================
           🔹 AUDIO MATCH
           ======================= */
        else if (kind == FileKind::kAudio) {

            SignatureBuckets buckets;

            for (const auto& file : candidates) {
                auto hashes = hash_engine_->compute_hashes(file);
                if (!hashes.audio_hash.empty()) {
                    buckets[hashes.audio_hash].push_back(file);
                }
            }

            for (auto& [_, g] : buckets) {
                if (g.size() > 1) {
                    local_groups.push_back(
                        DuplicateMatchGroup{kind, size, "audio", std::move(g)}
                    );
                }
            }
        }

        if (!local_groups.empty()) {
            std::lock_guard<std::mutex> lock(group_mutex);
            groups.insert(groups.end(), local_groups.begin(), local_groups.end());
        }
    };

    for (const auto& [kind, size_map] : kind_buckets) {
        for (const auto& [size, bucket] : size_map) {
            if (bucket.size() < 2) continue;

            futures.push_back(pool.enqueue([=, &process_bucket]() {
                process_bucket(kind, size, bucket);
            }));
        }
    }

    for (auto& f : futures) f.get();

    sort_groups(groups);
    return groups;
}

}  // namespace duplicate_library
