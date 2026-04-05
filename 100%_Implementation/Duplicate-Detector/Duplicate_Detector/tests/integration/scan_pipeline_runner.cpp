#include "duplicate_finder/duplicate_engine.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string escape_field(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\\') {
            escaped += "\\\\";
        } else if (ch == '|') {
            escaped += "\\|";
        } else {
            escaped += ch;
        }
    }
    return escaped;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: duplicate_scan_integration_runner <path>...\n";
        return 2;
    }

    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        paths.push_back(argv[i]);
    }

    try {
        duplicate_library::DuplicateEngine engine;
        int last_progress = 0;
        engine.setProgressCallback([&](int progress) {
            last_progress = progress;
        });

        const auto started_at = std::chrono::steady_clock::now();
        auto groups = engine.scan(paths);
        const auto ended_at = std::chrono::steady_clock::now();

        for (auto& group : groups) {
            std::sort(group.files.begin(), group.files.end());
        }
        std::sort(groups.begin(), groups.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.type != rhs.type) {
                return lhs.type < rhs.type;
            }
            return lhs.files < rhs.files;
        });

        const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            ended_at - started_at);

        std::cout << "SUMMARY"
                  << " groups=" << groups.size()
                  << " duration_ms=" << duration_ms.count()
                  << " progress=" << last_progress
                  << "\n";

        for (const auto& group : groups) {
            std::cout << "GROUP"
                      << " type=" << group.type
                      << " count=" << group.files.size()
                      << " files=";
            for (std::size_t i = 0; i < group.files.size(); ++i) {
                if (i != 0) {
                    std::cout << "|";
                }
                std::cout << escape_field(group.files[i]);
            }
            std::cout << "\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "scan failed: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
