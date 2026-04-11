#pragma once

#include "duplicate_finder/config.hpp"
#include "duplicate_finder/duplicate_engine.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace duplicate_library {

enum class JobStatus {
    kQueued,
    kRunning,
    kDone,
    kCancelled,
    kFailed
};

struct ScanJobResult {
    std::string id;
    JobStatus status = JobStatus::kQueued;
    int progress = 0;
    std::vector<DuplicateGroup> result;
};

class JobManager {
public:
    explicit JobManager(std::size_t worker_count = 1);
    explicit JobManager(const AppConfig& config);
    ~JobManager();

    std::string submitScan(const std::vector<std::string>& paths);
    bool cancelJob(const std::string& id);
    std::optional<ScanJobResult> getJob(const std::string& id) const;

private:
    struct JobRecord {
        std::string id;
        std::vector<std::string> paths;
        std::atomic<JobStatus> status{JobStatus::kQueued};
        std::atomic<int> progress{0};
        std::atomic<bool> cancel_requested{false};
        std::vector<DuplicateGroup> result;
        std::string error_message;
        mutable std::mutex result_mutex;
    };

    void workerLoop();
    std::shared_ptr<JobRecord> takeNextJob();

    std::size_t worker_count_;
    AppConfig config_;
    std::vector<std::thread> workers_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<std::string, std::shared_ptr<JobRecord>> jobs_;
    std::deque<std::shared_ptr<JobRecord>> queued_jobs_;
    bool stop_ = false;
    std::uint64_t next_id_ = 1;
};

}  // namespace duplicate_library
