#include "backend_service.hpp"
#include "duplicate_finder/logger.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>

namespace duplicate_app {
namespace {

constexpr int kListenBacklog = 32;
constexpr std::size_t kReadChunkSize = 4096;

bool send_all(int fd, const std::string& response) {
    std::size_t offset = 0;
    while (offset < response.size()) {
        const auto sent = ::send(
            fd,
            response.data() + offset,
            response.size() - offset,
            0);
        if (sent <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

}  // namespace

BackendService::BackendService(std::size_t worker_count)
    : job_manager_(worker_count) {}

BackendService::BackendService(const duplicate_library::AppConfig& config)
    : job_manager_(config) {}

BackendService::~BackendService() {
    stop();
}

bool BackendService::start(const std::string& host, std::uint16_t port) {
    if (host != "127.0.0.1") {
        duplicate_library::log_error("backend refused non-loopback bind", {
            {"host", host},
            {"port", std::to_string(port)}
        });
        return false;
    }

    if (running_) {
        return true;
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, kListenBacklog) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&BackendService::accept_loop, this);
    duplicate_library::log_info("backend service started", {
        {"host", host},
        {"port", std::to_string(port)}
    });
    return true;
}

void BackendService::stop() {
    const bool was_running = running_.exchange(false);
    if (!was_running) {
        return;
    }

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    duplicate_library::log_info("backend service stopped");
}

void BackendService::accept_loop() {
    while (running_) {
        sockaddr_in client_address{};
        socklen_t client_length = sizeof(client_address);
        const int client_fd = ::accept(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_address),
            &client_length);

        if (client_fd < 0) {
            if (running_ && errno == EINTR) {
                continue;
            }
            break;
        }

        std::thread(&BackendService::handle_client, this, client_fd).detach();
    }
}

void BackendService::handle_client(int client_fd) {
    std::string request;

    while (true) {
        char buffer[kReadChunkSize];
        const auto bytes_read = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            break;
        }

        request.append(buffer, static_cast<std::size_t>(bytes_read));

        const auto header_end = request.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            continue;
        }

        const auto body = read_request_body(request);
        const auto content_length_pos = request.find("Content-Length:");
        std::size_t content_length = 0;
        if (content_length_pos != std::string::npos) {
            const auto value_start = content_length_pos + std::strlen("Content-Length:");
            const auto value_end = request.find("\r\n", value_start);
            content_length = static_cast<std::size_t>(
                std::stoul(request.substr(value_start, value_end - value_start)));
        }

        if (body.size() >= content_length) {
            break;
        }
    }

    const auto header_end = request.find("\r\n\r\n");
    const auto header_block = header_end == std::string::npos ? request : request.substr(0, header_end);
    const auto body = read_request_body(request);

    std::istringstream request_stream(header_block);
    std::string method;
    std::string target;
    std::string version;
    request_stream >> method >> target >> version;

    std::string response;
    if (method == "POST" && target == "/scan") {
        response = handle_start_scan(body);
    } else if (method == "GET" && target.rfind("/status/", 0) == 0) {
        response = handle_get_status(target);
    } else if (method == "GET" && target.rfind("/results/", 0) == 0) {
        response = handle_get_results(target);
    } else if (method == "POST" && target.rfind("/cancel/", 0) == 0) {
        response = handle_cancel(target);
    } else {
        response = build_json_response(
            404,
            reason_for_status(404),
            "{\"error\":\"Unknown endpoint\"}");
    }

    duplicate_library::log_info("backend request handled", {
        {"method", method},
        {"target", target}
    });
    send_all(client_fd, response);
    ::shutdown(client_fd, SHUT_RDWR);
    ::close(client_fd);
}

std::string BackendService::handle_start_scan(const std::string& body) {
    const auto paths = extract_json_string_array(body, "paths");
    if (paths.empty()) {
        return build_json_response(
            400,
            reason_for_status(400),
            "{\"error\":\"Missing or empty paths array\"}");
    }

    const auto id = job_manager_.submitScan(paths);
    duplicate_library::log_info("scan endpoint accepted job", {
        {"job_id", id},
        {"paths_count", std::to_string(paths.size())}
    });
    return build_json_response(
        202,
        reason_for_status(202),
        "{\"id\":\"" + json_escape(id) + "\",\"status\":\"queued\"}");
}

std::string BackendService::handle_get_status(const std::string& target) const {
    const auto id = extract_path_parameter(target, "/status/");
    if (id.empty()) {
        return build_json_response(
            400,
            reason_for_status(400),
            "{\"error\":\"Missing job id\"}");
    }

    const auto job = job_manager_.getJob(id);
    if (!job) {
        return build_json_response(
            404,
            reason_for_status(404),
            "{\"error\":\"Unknown job\"}");
    }

    std::ostringstream json;
    json << "{"
         << "\"id\":\"" << json_escape(job->id) << "\","
         << "\"status\":\"" << job_status_to_string(job->status) << "\","
         << "\"progress\":" << job->progress
         << "}";
    return build_json_response(200, reason_for_status(200), json.str());
}

std::string BackendService::handle_get_results(const std::string& target) const {
    const auto id = extract_path_parameter(target, "/results/");
    if (id.empty()) {
        return build_json_response(
            400,
            reason_for_status(400),
            "{\"error\":\"Missing job id\"}");
    }

    const auto job = job_manager_.getJob(id);
    if (!job) {
        return build_json_response(
            404,
            reason_for_status(404),
            "{\"error\":\"Unknown job\"}");
    }

    if (job->status != duplicate_library::JobStatus::kDone) {
        return build_json_response(
            409,
            "Conflict",
            "{\"error\":\"Results not ready\",\"status\":\"" + job_status_to_string(job->status) + "\"}");
    }

    std::ostringstream json;
    json << "{"
         << "\"id\":\"" << json_escape(job->id) << "\","
         << "\"status\":\"done\","
         << "\"result\":" << duplicate_groups_to_json(job->result)
         << "}";
    return build_json_response(200, reason_for_status(200), json.str());
}

std::string BackendService::handle_cancel(const std::string& target) {
    const auto id = extract_path_parameter(target, "/cancel/");
    if (id.empty()) {
        return build_json_response(
            400,
            reason_for_status(400),
            "{\"error\":\"Missing job id\"}");
    }

    const bool cancelled = job_manager_.cancelJob(id);
    if (!cancelled) {
        const auto job = job_manager_.getJob(id);
        if (!job) {
            return build_json_response(
                404,
                reason_for_status(404),
                "{\"error\":\"Unknown job\"}");
        }

        return build_json_response(
            409,
            "Conflict",
            "{\"error\":\"Job cannot be cancelled\",\"status\":\"" + job_status_to_string(job->status) + "\"}");
    }

    duplicate_library::log_warn("scan endpoint cancelled job", {
        {"job_id", id}
    });
    return build_json_response(
        202,
        reason_for_status(202),
        "{\"id\":\"" + json_escape(id) + "\",\"status\":\"cancelling\"}");
}

std::string BackendService::read_request_body(const std::string& request) {
    const auto header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return {};
    }
    return request.substr(header_end + 4);
}

std::string BackendService::build_json_response(
    int status_code,
    const std::string& status_text,
    const std::string& json_body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << ' ' << status_text << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << json_body.size() << "\r\n"
             << "Connection: close\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "\r\n"
             << json_body;
    return response.str();
}

std::string BackendService::reason_for_status(int code) {
    switch (code) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        default:
            return "OK";
    }
}

std::string BackendService::extract_json_string(const std::string& body, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const auto key_pos = body.find(pattern);
    if (key_pos == std::string::npos) {
        return {};
    }

    const auto colon_pos = body.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return {};
    }

    const auto value_start = body.find('"', colon_pos + 1);
    if (value_start == std::string::npos) {
        return {};
    }

    std::string result;
    bool escaped = false;
    for (std::size_t i = value_start + 1; i < body.size(); ++i) {
        const char current = body[i];
        if (escaped) {
            result.push_back(current);
            escaped = false;
            continue;
        }
        if (current == '\\') {
            escaped = true;
            continue;
        }
        if (current == '"') {
            return result;
        }
        result.push_back(current);
    }

    return {};
}

std::vector<std::string> BackendService::extract_json_string_array(const std::string& body, const std::string& key) {
    const std::string pattern = "\"" + key + "\":";
    const auto key_pos = body.find(pattern);
    if (key_pos == std::string::npos) {
        return {};
    }

    const auto array_start = body.find('[', key_pos + pattern.size());
    if (array_start == std::string::npos) {
        return {};
    }

    const auto array_end = body.find(']', array_start);
    if (array_end == std::string::npos) {
        return {};
    }

    std::vector<std::string> result;
    bool in_string = false;
    bool escaped = false;
    std::string current;

    for (std::size_t i = array_start + 1; i < array_end; ++i) {
        const char c = body[i];
        if (escaped) {
            current.push_back(c);
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == '"') {
            if (in_string) {
                result.push_back(current);
                current.clear();
            }
            in_string = !in_string;
            continue;
        }

        if (in_string) {
            current.push_back(c);
        }
    }

    return result;
}

std::string BackendService::extract_path_parameter(
    const std::string& target,
    const std::string& prefix) {
    if (target.rfind(prefix, 0) != 0) {
        return {};
    }

    auto value = target.substr(prefix.size());
    const auto query_pos = value.find('?');
    if (query_pos != std::string::npos) {
        value.resize(query_pos);
    }
    return value;
}

std::string BackendService::json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char current : value) {
        switch (current) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(current);
                break;
        }
    }
    return escaped;
}

std::string BackendService::job_status_to_string(duplicate_library::JobStatus status) {
    switch (status) {
        case duplicate_library::JobStatus::kQueued:
            return "queued";
        case duplicate_library::JobStatus::kRunning:
            return "running";
        case duplicate_library::JobStatus::kDone:
            return "done";
        case duplicate_library::JobStatus::kCancelled:
            return "cancelled";
        case duplicate_library::JobStatus::kFailed:
            return "failed";
    }

    return "queued";
}

std::string BackendService::duplicate_groups_to_json(
    const std::vector<duplicate_library::DuplicateGroup>& groups) {
    std::ostringstream json;
    json << "[";
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (i > 0) {
            json << ",";
        }

        json << "{"
             << "\"type\":\"" << json_escape(groups[i].type) << "\","
             << "\"files\":[";

        for (std::size_t file_index = 0; file_index < groups[i].files.size(); ++file_index) {
            if (file_index > 0) {
                json << ",";
            }
            json << "\"" << json_escape(groups[i].files[file_index]) << "\"";
        }

        json << "]}";
    }
    json << "]";
    return json.str();
}

}  // namespace duplicate_app
