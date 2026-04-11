#include "backend_service.hpp"
#include "duplicate_finder/logger.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int) {
    g_running = 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8080;
    std::filesystem::path config_path = duplicate_library::ConfigManager::default_config_path();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const auto config = duplicate_library::ConfigManager::load(config_path);
    duplicate_library::Logger::instance().initialize(config.paths.log_file_path);
    duplicate_library::log_info("backend process starting", {
        {"host", host},
        {"port", std::to_string(port)},
        {"config_path", config_path.lexically_normal().string()}
    });

    duplicate_app::BackendService service(config);
    if (!service.start(host, port)) {
        duplicate_library::log_error("backend process failed to start", {
            {"host", host},
            {"port", std::to_string(port)}
        });
        return 1;
    }

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    service.stop();
    duplicate_library::log_info("backend process exiting");
    return 0;
}
