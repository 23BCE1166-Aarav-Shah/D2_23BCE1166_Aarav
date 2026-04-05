#pragma once

#include "duplicate_finder/config.hpp"
#include "duplicate_finder/job_manager.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace duplicate_app {

class BackendService {
public:
    explicit BackendService(std::size_t worker_count = 1);
    explicit BackendService(const duplicate_library::AppConfig& config);
    ~BackendService();

    bool start(const std::string& host, std::uint16_t port);
    void stop();

private:
    void accept_loop();
    void handle_client(int client_fd);

    std::string handle_start_scan(const std::string& body);
    std::string handle_get_status(const std::string& target) const;
    std::string handle_get_results(const std::string& target) const;
    std::string handle_cancel(const std::string& target);

    static std::string read_request_body(const std::string& request);
    static std::string build_json_response(
        int status_code,
        const std::string& status_text,
        const std::string& json_body);
    static std::string reason_for_status(int code);
    static std::string extract_json_string(const std::string& body, const std::string& key);
    static std::vector<std::string> extract_json_string_array(const std::string& body, const std::string& key);
    static std::string extract_path_parameter(
        const std::string& target,
        const std::string& prefix);
    static std::string json_escape(const std::string& value);
    static std::string job_status_to_string(duplicate_library::JobStatus status);
    static std::string duplicate_groups_to_json(
        const std::vector<duplicate_library::DuplicateGroup>& groups);

    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    duplicate_library::JobManager job_manager_;
};

}  // namespace duplicate_app
