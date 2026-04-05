#pragma once

#include "duplicate_finder/job_manager.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace duplicate_library {

class WatcherService {
public:
    explicit WatcherService(JobManager& job_manager);
    ~WatcherService();

    bool startWatching(
        const std::filesystem::path& root,
        std::chrono::milliseconds debounce = std::chrono::milliseconds(1500));
    void stopWatching();

    bool isWatching() const;
    std::optional<std::string> lastTriggeredJobId() const;

private:
    void eventLoop();
    bool addRecursiveWatch(const std::filesystem::path& directory);
    bool addSingleWatch(const std::filesystem::path& directory);
    void clearWatches();
    void scheduleIncrementalScanLocked();

    JobManager& job_manager_;
    int inotify_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread watch_thread_;

    mutable std::mutex mutex_;
    std::filesystem::path root_;
    std::chrono::milliseconds debounce_{1500};
    std::unordered_map<int, std::filesystem::path> watch_descriptors_;
    std::chrono::steady_clock::time_point last_event_time_{};
    bool pending_scan_ = false;
    std::optional<std::string> last_triggered_job_id_;
};

}  // namespace duplicate_library
