#include "duplicate_finder/file_scanner.hpp"

#include "duplicate_finder/hash_engine.hpp"

#include <algorithm>
#include <system_error>

namespace duplicate_library {

std::vector<FileEntry> FileScanner::scan(const std::filesystem::path& root) const {
    return scan(root, {});
}

std::vector<FileEntry> FileScanner::scan(
    const std::filesystem::path& root,
    const CancelCallback& should_cancel) const {
    std::vector<FileEntry> files;
    HashEngine hash_engine;

    std::error_code error;
    if (!std::filesystem::exists(root, error) ||
        !std::filesystem::is_directory(root, error)) {
        return files;
    }

    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    std::filesystem::recursive_directory_iterator end;

    while (it != end) {
        if (should_cancel && should_cancel()) {
            break;
        }

        if (!error) {
            std::error_code file_error;
            if (it->is_regular_file(file_error)) {
                const auto path = it->path();
                const auto size = it->file_size(file_error);
                if (!file_error) {
                    files.push_back(FileEntry{
                        path,
                        size,
                        hash_engine.classify(path)});
                }
            }
            it.increment(error);
        } else {
            error.clear();
            it.increment(error);
        }
    }

    std::sort(files.begin(), files.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
        return lhs.path.lexically_normal().string() < rhs.path.lexically_normal().string();
    });

    return files;
}

}  // namespace duplicate_library
