#include "duplicate_finder/job_manager.hpp"
#include "duplicate_finder/logger.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace duplicate_library {
namespace {

std::size_t normalized_worker_count(std::size_t worker_count) {
    if (worker_count != 0) {
        return worker_count;
    }
    const auto hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 1u : 1u;
}

}  // namespace

JobManager::JobManager(std::size_t worker_count)
    : worker_count_(normalized_worker_count(worker_count)),
      config_(ConfigManager::defaults()) {
    if (worker_count != 0) {
        config_.scan.worker_count = worker_count;
    }
    workers_.reserve(worker_count_);
    for (std::size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

JobManager::JobManager(const AppConfig& config)
    : worker_count_(normalized_worker_count(config.scan.worker_count == 0 ? 1 : config.scan.worker_count)),
      config_(config) {
    workers_.reserve(worker_count_);
    for (std::size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

JobManager::~JobManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::string JobManager::submitScan(const std::vector<std::string>& paths) {
    auto job = std::make_shared<JobRecord>();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->id = std::to_string(next_id_++);
        job->paths = paths;
        job->status.store(JobStatus::kQueued);
        jobs_[job->id] = job;
        queued_jobs_.push_back(job);
    }

    log_info("scan job queued", {
        {"job_id", job->id},
        {"paths_count", std::to_string(paths.size())}
    });
    condition_.notify_one();
    return job->id;
}

bool JobManager::cancelJob(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) {
        return false;
    }

    auto& job = it->second;
    const auto status = job->status.load();
    if (status == JobStatus::kDone || status == JobStatus::kCancelled || status == JobStatus::kFailed) {
        return false;
    }

    job->cancel_requested.store(true);
    if (status == JobStatus::kQueued) {
        job->status.store(JobStatus::kCancelled);
        job->progress.store(0);
    }

    log_warn("scan job cancellation requested", {
        {"job_id", job->id}
    });
    condition_.notify_all();
    return true;
}

std::optional<ScanJobResult> JobManager::getJob(const std::string& id) const {
    std::shared_ptr<JobRecord> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return std::nullopt;
        }
        job = it->second;
    }

    ScanJobResult snapshot;
    snapshot.id = job->id;
    snapshot.status = job->status.load();
    snapshot.progress = job->progress.load();

    {
        std::lock_guard<std::mutex> result_lock(job->result_mutex);
        snapshot.result = job->result;
    }

    return snapshot;
}

void JobManager::workerLoop() {
    for (;;) {
        auto job = takeNextJob();
        if (!job) {
            return;
        }

        if (job->cancel_requested.load()) {
            job->status.store(JobStatus::kCancelled);
            log_warn("scan job cancelled before start", {
                {"job_id", job->id}
            });
            continue;
        }

        job->status.store(JobStatus::kRunning);
        job->progress.store(0);
        log_info("scan job started", {
            {"job_id", job->id},
            {"paths_count", std::to_string(job->paths.size())}
        });

        try {
            DuplicateEngine engine(config_);
            engine.setProgressCallback([job](int progress) {
                job->progress.store(progress);
            });
            engine.setCancelCallback([job]() {
                return job->cancel_requested.load();
            });

            auto result = engine.scan(job->paths);

            if (job->cancel_requested.load()) {
                job->status.store(JobStatus::kCancelled);
                log_warn("scan job cancelled during execution", {
                    {"job_id", job->id}
                });
                continue;
            }

            {
                std::lock_guard<std::mutex> result_lock(job->result_mutex);
                job->result = std::move(result);
            }

            job->progress.store(100);
            job->status.store(JobStatus::kDone);
            log_info("scan job completed", {
                {"job_id", job->id},
                {"groups", std::to_string(job->result.size())}
            });
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> result_lock(job->result_mutex);
            job->error_message = error.what();
            job->status.store(JobStatus::kFailed);
            log_error("scan job failed", {
                {"job_id", job->id},
                {"error", error.what()}
            });
        } catch (...) {
            std::lock_guard<std::mutex> result_lock(job->result_mutex);
            job->error_message = "Unknown scan failure";
            job->status.store(JobStatus::kFailed);
            log_error("scan job failed", {
                {"job_id", job->id},
                {"error", "Unknown scan failure"}
            });
        }
    }
}

std::shared_ptr<JobManager::JobRecord> JobManager::takeNextJob() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this]() { return stop_ || !queued_jobs_.empty(); });

    if (stop_) {
        return nullptr;
    }

    while (!queued_jobs_.empty()) {
        auto job = queued_jobs_.front();
        queued_jobs_.pop_front();
        if (job->status.load() == JobStatus::kCancelled) {
            continue;
        }
        return job;
    }

    return nullptr;
}

}  // namespace duplicate_library
