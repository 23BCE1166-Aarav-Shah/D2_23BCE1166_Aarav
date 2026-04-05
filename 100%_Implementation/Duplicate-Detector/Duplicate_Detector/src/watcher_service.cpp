#include "duplicate_finder/watcher_service.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <array>
#include <system_error>

namespace duplicate_library {
namespace {

constexpr std::uint32_t kWatchMask =
    IN_CREATE |
    IN_CLOSE_WRITE |
    IN_MOVED_TO |
    IN_DELETE |
    IN_MOVED_FROM |
    IN_ATTRIB |
    IN_MODIFY |
    IN_DELETE_SELF |
    IN_MOVE_SELF;

constexpr std::size_t kEventBufferSize = 64 * 1024;

}  // namespace

WatcherService::WatcherService(JobManager& job_manager)
    : job_manager_(job_manager) {}

WatcherService::~WatcherService() {
    stopWatching();
}

bool WatcherService::startWatching(
    const std::filesystem::path& root,
    std::chrono::milliseconds debounce) {
    stopWatching();

    inotify_fd_ = ::inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        root_ = root.lexically_normal();
        debounce_ = debounce;
        pending_scan_ = false;
        last_triggered_job_id_.reset();
        last_event_time_ = std::chrono::steady_clock::now();
    }

    if (!addRecursiveWatch(root_)) {
        ::close(inotify_fd_);
        inotify_fd_ = -1;
        return false;
    }

    running_.store(true);
    watch_thread_ = std::thread(&WatcherService::eventLoop, this);
    return true;
}

void WatcherService::stopWatching() {
    const bool was_running = running_.exchange(false);
    if (!was_running && inotify_fd_ < 0) {
        return;
    }

    if (inotify_fd_ >= 0) {
        ::close(inotify_fd_);
        inotify_fd_ = -1;
    }

    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }

    clearWatches();
}

bool WatcherService::isWatching() const {
    return running_.load();
}

std::optional<std::string> WatcherService::lastTriggeredJobId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_triggered_job_id_;
}

void WatcherService::eventLoop() {
    std::array<char, kEventBufferSize> buffer{};

    while (running_.load()) {
        const auto bytes_read = ::read(inotify_fd_, buffer.data(), buffer.size());
        if (bytes_read > 0) {
            std::size_t offset = 0;
            while (offset < static_cast<std::size_t>(bytes_read)) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);

                std::filesystem::path watched_directory;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto found = watch_descriptors_.find(event->wd);
                    if (found != watch_descriptors_.end()) {
                        watched_directory = found->second;
                    }
                }

                if (!watched_directory.empty()) {
                    const auto name = event->len > 0 ? std::string(event->name) : std::string();
                    const auto event_path = name.empty() ? watched_directory : watched_directory / name;

                    if ((event->mask & IN_ISDIR) != 0 &&
                        ((event->mask & IN_CREATE) != 0 || (event->mask & IN_MOVED_TO) != 0)) {
                        addRecursiveWatch(event_path);
                    }

                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        pending_scan_ = true;
                        last_event_time_ = std::chrono::steady_clock::now();
                    }
                }

                offset += sizeof(inotify_event) + static_cast<std::size_t>(event->len);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_scan_ &&
                std::chrono::steady_clock::now() - last_event_time_ >= debounce_) {
                scheduleIncrementalScanLocked();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool WatcherService::addRecursiveWatch(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) ||
        !std::filesystem::is_directory(directory, error)) {
        return false;
    }

    if (!addSingleWatch(directory)) {
        return false;
    }

    std::filesystem::recursive_directory_iterator it(
        directory,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    std::filesystem::recursive_directory_iterator end;

    while (it != end) {
        if (!error) {
            std::error_code entry_error;
            if (it->is_directory(entry_error) && !entry_error) {
                addSingleWatch(it->path());
            }
            it.increment(error);
        } else {
            error.clear();
            it.increment(error);
        }
    }

    return true;
}

bool WatcherService::addSingleWatch(const std::filesystem::path& directory) {
    const int descriptor = ::inotify_add_watch(inotify_fd_, directory.c_str(), kWatchMask);
    if (descriptor < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    watch_descriptors_[descriptor] = directory.lexically_normal();
    return true;
}

void WatcherService::clearWatches() {
    std::lock_guard<std::mutex> lock(mutex_);
    watch_descriptors_.clear();
    pending_scan_ = false;
}

void WatcherService::scheduleIncrementalScanLocked() {
    pending_scan_ = false;
    last_triggered_job_id_ = job_manager_.submitScan({root_.string()});
}

}  // namespace duplicate_library
