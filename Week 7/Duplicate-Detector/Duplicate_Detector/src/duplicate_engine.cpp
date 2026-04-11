#include "duplicate_finder/duplicate_engine.hpp"

#include "duplicate_finder/config.hpp"
#include "duplicate_finder/database_cache.hpp"
#include "duplicate_finder/file_scanner.hpp"
#include "duplicate_finder/hash_engine.hpp"
#include "src/thread_pool.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace duplicate_library {
namespace {

constexpr std::size_t kCompareBufferSize = 64 * 1024;

struct FileRecordWithHashes {
    FileEntry file;
    HashResult hashes;
    std::string sample_hash;
    std::int64_t mtime_ns = 0;
};

using RecordBuckets = std::map<std::string, std::vector<FileRecordWithHashes>>;
using IndexBuckets = std::unordered_map<std::string, std::vector<std::size_t>>;

std::string kind_to_string(FileKind kind) {
    switch (kind) {
        case FileKind::kBinary:
            return "binary";
        case FileKind::kImage:
            return "image";
        case FileKind::kVideo:
            return "video";
        case FileKind::kAudio:
            return "audio";
    }

    return "binary";
}

std::int64_t file_mtime_ns(const std::filesystem::path& path) {
    const auto file_time = std::filesystem::last_write_time(path);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(file_time.time_since_epoch()).count();
}

bool files_are_equal(const std::filesystem::path& lhs_path, const std::filesystem::path& rhs_path) {
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

bool parse_hex_u64(const std::string& value, std::uint64_t* out) {
    if (value.empty() || out == nullptr) {
        return false;
    }

    try {
        *out = std::stoull(value, nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

int hamming_distance(std::uint64_t lhs, std::uint64_t rhs) {
    return __builtin_popcountll(lhs ^ rhs);
}

void append_group(
    std::vector<DuplicateGroup>* groups,
    FileKind kind,
    std::vector<FileRecordWithHashes> group_records) {
    if (groups == nullptr || group_records.size() < 2) {
        return;
    }

    DuplicateGroup group;
    group.type = kind_to_string(kind);
    group.files.reserve(group_records.size());

    for (const auto& record : group_records) {
        group.files.push_back(record.file.path.lexically_normal().string());
    }

    std::sort(group.files.begin(), group.files.end());
    groups->push_back(std::move(group));
}

void update_progress(
    std::atomic<std::size_t>* processed,
    std::size_t total_files,
    const std::function<void(int)>& progress_callback) {
    if (processed == nullptr || !progress_callback || total_files == 0) {
        return;
    }

    const auto done = ++(*processed);
    progress_callback(static_cast<int>((done * 100) / total_files));
}

class ShardedIndexBuckets {
public:
    explicit ShardedIndexBuckets(std::size_t shard_count)
        : shards_(std::max<std::size_t>(1, shard_count)),
          mutexes_(shards_.size()) {}

    void add(const std::string& key, std::size_t value) {
        const auto shard_index = std::hash<std::string>{}(key) % shards_.size();
        std::lock_guard<std::mutex> lock(mutexes_[shard_index]);
        shards_[shard_index][key].push_back(value);
    }

    std::vector<std::vector<std::size_t>> collect_groups(std::size_t minimum_size) {
        std::vector<std::vector<std::size_t>> groups;

        for (std::size_t i = 0; i < shards_.size(); ++i) {
            std::lock_guard<std::mutex> lock(mutexes_[i]);
            for (auto& [key, values] : shards_[i]) {
                (void)key;
                if (values.size() >= minimum_size) {
                    groups.push_back(std::move(values));
                }
            }
        }

        return groups;
    }

private:
    std::vector<IndexBuckets> shards_;
    std::vector<std::mutex> mutexes_;
};

std::vector<std::vector<std::size_t>> partition_exact_binary_indices(
    const std::vector<std::size_t>& indices,
    const std::vector<FileRecordWithHashes>& records) {
    std::vector<std::vector<std::size_t>> partitions;
    std::vector<bool> used(indices.size(), false);

    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (used[i]) {
            continue;
        }

        std::vector<std::size_t> group{indices[i]};
        used[i] = true;

        for (std::size_t j = i + 1; j < indices.size(); ++j) {
            if (used[j]) {
                continue;
            }

            if (files_are_equal(records[indices[i]].file.path, records[indices[j]].file.path)) {
                used[j] = true;
                group.push_back(indices[j]);
            }
        }

        if (group.size() > 1) {
            std::sort(group.begin(), group.end(), [&](std::size_t lhs, std::size_t rhs) {
                return records[lhs].file.path.lexically_normal().string() <
                       records[rhs].file.path.lexically_normal().string();
            });
            partitions.push_back(std::move(group));
        }
    }

    return partitions;
}

void append_group_from_indices(
    std::vector<DuplicateGroup>* groups,
    const std::vector<FileRecordWithHashes>& records,
    FileKind kind,
    const std::vector<std::size_t>& indices) {
    if (groups == nullptr || indices.size() < 2) {
        return;
    }

    DuplicateGroup group;
    group.type = kind_to_string(kind);
    group.files.reserve(indices.size());

    for (const auto index : indices) {
        group.files.push_back(records[index].file.path.lexically_normal().string());
    }

    std::sort(group.files.begin(), group.files.end());
    groups->push_back(std::move(group));
}

std::vector<DuplicateGroup> build_media_duplicate_groups(const std::vector<FileRecordWithHashes>& records) {
    RecordBuckets audio_buckets;
    std::vector<FileRecordWithHashes> visual_records;

    for (const auto& record : records) {
        switch (record.file.kind) {
            case FileKind::kAudio: {
                const auto signature = record.hashes.audio_hash.empty()
                    ? record.hashes.strict_hash
                    : record.hashes.audio_hash;
                if (!signature.empty()) {
                    audio_buckets[signature].push_back(record);
                }
                break;
            }
            case FileKind::kImage:
            case FileKind::kVideo:
                visual_records.push_back(record);
                break;
            case FileKind::kBinary:
                break;
        }
    }

    std::vector<DuplicateGroup> groups;

    for (auto& [key, bucket] : audio_buckets) {
        (void)key;
        append_group(&groups, FileKind::kAudio, std::move(bucket));
    }

    std::vector<bool> visual_used(visual_records.size(), false);
    constexpr int kVisualHashDistanceThreshold = 10;

    for (std::size_t i = 0; i < visual_records.size(); ++i) {
        if (visual_used[i]) {
            continue;
        }

        std::uint64_t base_hash = 0;
        if (!parse_hex_u64(visual_records[i].hashes.visual_hash, &base_hash)) {
            continue;
        }

        std::vector<FileRecordWithHashes> group_records;
        group_records.push_back(visual_records[i]);
        visual_used[i] = true;

        for (std::size_t j = i + 1; j < visual_records.size(); ++j) {
            if (visual_used[j] || visual_records[j].file.kind != visual_records[i].file.kind) {
                continue;
            }

            std::uint64_t candidate_hash = 0;
            if (!parse_hex_u64(visual_records[j].hashes.visual_hash, &candidate_hash)) {
                continue;
            }

            if (hamming_distance(base_hash, candidate_hash) <= kVisualHashDistanceThreshold) {
                group_records.push_back(visual_records[j]);
                visual_used[j] = true;
            }
        }

        append_group(&groups, visual_records[i].file.kind, std::move(group_records));
    }

    std::sort(groups.begin(), groups.end(), [](const DuplicateGroup& lhs, const DuplicateGroup& rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type < rhs.type;
        }
        return lhs.files < rhs.files;
    });

    return groups;
}

std::vector<DuplicateGroup> build_binary_duplicate_groups(
    std::vector<FileRecordWithHashes>* records,
    const std::vector<std::size_t>& binary_indices,
    const HashEngine& hash_engine,
    DatabaseCache* cache,
    std::mutex* cache_mutex,
    bool enable_cache,
    std::size_t worker_count,
    const std::function<bool()>& cancel_callback,
    std::atomic<std::size_t>* processed,
    std::size_t total_files,
    const std::function<void(int)>& progress_callback) {
    std::vector<DuplicateGroup> groups;
    if (records == nullptr || binary_indices.empty()) {
        return groups;
    }

    std::map<std::uintmax_t, std::vector<std::size_t>> size_buckets;
    for (const auto index : binary_indices) {
        size_buckets[(*records)[index].file.size].push_back(index);
    }

    const auto io_worker_count = std::max<std::size_t>(1, std::min<std::size_t>(4, worker_count));
    const auto verify_worker_count = std::max<std::size_t>(1, worker_count > io_worker_count ? worker_count - io_worker_count : 1);

    ShardedIndexBuckets sample_buckets(io_worker_count * 4);
    ShardedIndexBuckets full_buckets(io_worker_count * 4);
    ThreadPool io_pool(io_worker_count);
    ThreadPool verify_pool(verify_worker_count);

    std::vector<std::size_t> direct_full_candidates;
    std::vector<std::size_t> sample_candidates;
    std::vector<std::size_t> unique_size_candidates;

    for (const auto& [size, indices] : size_buckets) {
        (void)size;
        if (indices.size() < 2) {
            unique_size_candidates.insert(unique_size_candidates.end(), indices.begin(), indices.end());
            continue;
        }

        bool has_cached_full = false;
        for (const auto index : indices) {
            if (!(*records)[index].hashes.strict_hash.empty()) {
                has_cached_full = true;
                break;
            }
        }

        for (const auto index : indices) {
            if (!(*records)[index].hashes.strict_hash.empty()) {
                const auto key = std::to_string((*records)[index].file.size) + "|" + (*records)[index].hashes.strict_hash;
                full_buckets.add(key, index);
            } else if (has_cached_full) {
                direct_full_candidates.push_back(index);
            } else {
                sample_candidates.push_back(index);
            }
        }
    }

    for (const auto index : unique_size_candidates) {
        update_progress(processed, total_files, progress_callback);
        (void)index;
    }

    std::vector<std::future<void>> sample_futures;
    sample_futures.reserve(sample_candidates.size());

    for (const auto index : sample_candidates) {
        sample_futures.push_back(io_pool.enqueue([&, index]() {
            if (cancel_callback && cancel_callback()) {
                return;
            }

            auto& record = (*records)[index];
            record.sample_hash = hash_engine.compute_sample_xxhash(record.file.path, record.file.size);
            if (!record.sample_hash.empty()) {
                const auto key = std::to_string(record.file.size) + "|" + record.sample_hash;
                sample_buckets.add(key, index);
            }
        }));
    }

    for (auto& future : sample_futures) {
        future.get();
    }

    if (cancel_callback && cancel_callback()) {
        return {};
    }

    std::vector<bool> promoted_to_full(records->size(), false);
    auto sample_groups = sample_buckets.collect_groups(2);
    for (const auto& group : sample_groups) {
        for (const auto index : group) {
            promoted_to_full[index] = true;
            direct_full_candidates.push_back(index);
        }
    }

    std::sort(direct_full_candidates.begin(), direct_full_candidates.end());
    direct_full_candidates.erase(
        std::unique(direct_full_candidates.begin(), direct_full_candidates.end()),
        direct_full_candidates.end());

    for (const auto index : sample_candidates) {
        if (!promoted_to_full[index]) {
            update_progress(processed, total_files, progress_callback);
        }
    }

    std::vector<std::future<void>> full_futures;
    full_futures.reserve(direct_full_candidates.size());

    for (const auto index : direct_full_candidates) {
        full_futures.push_back(io_pool.enqueue([&, index]() {
            if (cancel_callback && cancel_callback()) {
                return;
            }

            auto& record = (*records)[index];
            if (record.hashes.strict_hash.empty()) {
                record.hashes.strict_hash = hash_engine.compute_xxhash(record.file.path);
                if (enable_cache && cache != nullptr && cache_mutex != nullptr) {
                    std::lock_guard<std::mutex> lock(*cache_mutex);
                    cache->upsert(record.file, record.hashes, record.mtime_ns);
                }
            }

            if (!record.hashes.strict_hash.empty()) {
                const auto key = std::to_string(record.file.size) + "|" + record.hashes.strict_hash;
                full_buckets.add(key, index);
            }

            update_progress(processed, total_files, progress_callback);
        }));
    }

    for (auto& future : full_futures) {
        future.get();
    }

    if (cancel_callback && cancel_callback()) {
        return {};
    }

    auto full_groups = full_buckets.collect_groups(2);
    std::mutex groups_mutex;
    std::vector<std::future<void>> verify_futures;
    verify_futures.reserve(full_groups.size());

    for (auto& full_group : full_groups) {
        verify_futures.push_back(verify_pool.enqueue([&, full_group]() {
            auto partitions = partition_exact_binary_indices(full_group, *records);
            if (partitions.empty()) {
                return;
            }

            std::lock_guard<std::mutex> lock(groups_mutex);
            for (const auto& partition : partitions) {
                append_group_from_indices(&groups, *records, FileKind::kBinary, partition);
            }
        }));
    }

    for (auto& future : verify_futures) {
        future.get();
    }

    std::sort(groups.begin(), groups.end(), [](const DuplicateGroup& lhs, const DuplicateGroup& rhs) {
        return lhs.files < rhs.files;
    });
    return groups;
}

}  // namespace

DuplicateEngine::DuplicateEngine(std::size_t worker_count)
    : worker_count_(worker_count == 0 ? std::max<std::size_t>(1, std::thread::hardware_concurrency()) : worker_count),
      config_(ConfigManager::defaults()) {
    if (worker_count != 0) {
        config_.scan.worker_count = worker_count;
    }
}

DuplicateEngine::DuplicateEngine(const AppConfig& config)
    : worker_count_(config.scan.worker_count == 0
          ? std::max<std::size_t>(1, std::thread::hardware_concurrency())
          : config.scan.worker_count),
      config_(config) {}

void DuplicateEngine::setProgressCallback(std::function<void(int)> cb) {
    progress_callback_ = std::move(cb);
}

void DuplicateEngine::setCancelCallback(std::function<bool()> cb) {
    cancel_callback_ = std::move(cb);
}

void DuplicateEngine::setConfig(const AppConfig& config) {
    config_ = config;
    worker_count_ = config.scan.worker_count == 0
        ? std::max<std::size_t>(1, std::thread::hardware_concurrency())
        : config.scan.worker_count;
}

std::vector<DuplicateGroup> DuplicateEngine::scan(const std::vector<std::string>& paths) {
    FileScanner scanner;
    HashEngine hash_engine;
    DatabaseCache cache(config_.paths.cache_database_path);
    const bool cache_enabled = config_.scan.enable_cache && cache.is_available();

    std::vector<FileEntry> files;
    for (const auto& path : paths) {
        if (cancel_callback_ && cancel_callback_()) {
            return {};
        }
        auto path_files = scanner.scan(path, cancel_callback_);
        files.insert(files.end(), path_files.begin(), path_files.end());
    }

    files.erase(
        std::remove_if(files.begin(), files.end(), [&](const FileEntry& file) {
            if (file.size < config_.scan.min_file_size_bytes) {
                return true;
            }
            if (config_.scan.max_file_size_bytes != 0 && file.size > config_.scan.max_file_size_bytes) {
                return true;
            }
            return false;
        }),
        files.end());

    if (cache_enabled) {
        const auto cache_db = cache.database_path().lexically_normal().string();
        files.erase(
            std::remove_if(files.begin(), files.end(), [&](const FileEntry& file) {
                return file.path.lexically_normal().string() == cache_db;
            }),
            files.end());
    }

    const auto total_files = files.size();
    if (progress_callback_) {
        progress_callback_(total_files == 0 ? 100 : 0);
    }

    std::vector<FileRecordWithHashes> records(total_files);
    std::vector<std::size_t> pending_indices;
    std::vector<std::size_t> binary_indices;
    std::vector<std::size_t> media_indices;
    pending_indices.reserve(total_files);
    binary_indices.reserve(total_files);
    media_indices.reserve(total_files);
    std::atomic<std::size_t> processed{0};

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (cancel_callback_ && cancel_callback_()) {
            return {};
        }

        records[i].file = files[i];
        records[i].mtime_ns = file_mtime_ns(files[i].path);
        if (files[i].kind == FileKind::kBinary) {
            binary_indices.push_back(i);
        } else {
            media_indices.push_back(i);
        }

        const auto cached = cache_enabled ? cache.lookup(files[i].path) : std::nullopt;
        if (cached &&
            cached->size == files[i].size &&
            cached->mtime_ns == records[i].mtime_ns &&
            cached->kind == files[i].kind) {
            records[i].hashes = cached->hashes;
            update_progress(&processed, total_files, progress_callback_);
        } else {
            pending_indices.push_back(i);
        }
    }

    ThreadPool pool(worker_count_);
    std::vector<std::future<void>> futures;
    futures.reserve(media_indices.size());
    std::mutex cache_mutex;

    for (const auto index : pending_indices) {
        if (records[index].file.kind == FileKind::kBinary) {
            continue;
        }

        futures.push_back(pool.enqueue([&, index]() {
            if (cancel_callback_ && cancel_callback_()) {
                return;
            }

            records[index].hashes = hash_engine.compute_hashes(records[index].file);
            if (cache_enabled) {
                std::lock_guard<std::mutex> lock(cache_mutex);
                cache.upsert(records[index].file, records[index].hashes, records[index].mtime_ns);
            }

            update_progress(&processed, total_files, progress_callback_);
        }));
    }

    for (auto& future : futures) {
        future.get();
    }

    if (cancel_callback_ && cancel_callback_()) {
        return {};
    }

    auto groups = build_binary_duplicate_groups(
        &records,
        binary_indices,
        hash_engine,
        &cache,
        &cache_mutex,
        cache_enabled,
        worker_count_,
        cancel_callback_,
        &processed,
        total_files,
        progress_callback_);

    if (cancel_callback_ && cancel_callback_()) {
        return {};
    }

    std::vector<FileRecordWithHashes> media_records;
    media_records.reserve(media_indices.size());
    for (const auto index : media_indices) {
        media_records.push_back(records[index]);
    }

    auto media_groups = build_media_duplicate_groups(media_records);
    groups.insert(groups.end(), media_groups.begin(), media_groups.end());
    std::sort(groups.begin(), groups.end(), [](const DuplicateGroup& lhs, const DuplicateGroup& rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type < rhs.type;
        }
        return lhs.files < rhs.files;
    });
    groups.erase(
        std::remove_if(groups.begin(), groups.end(), [&](const DuplicateGroup& group) {
            return group.files.size() < config_.thresholds.minimum_group_size;
        }),
        groups.end());

    if (progress_callback_) {
        progress_callback_(100);
    }

    return groups;
}

}  // namespace duplicate_library
