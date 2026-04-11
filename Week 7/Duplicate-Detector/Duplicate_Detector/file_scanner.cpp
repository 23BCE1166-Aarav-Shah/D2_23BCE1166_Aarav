#include "file_scanner.hpp"

#include <algorithm>
#include <functional>
#include <system_error>

namespace duplicate_library {

std::vector<FileEntry> FileScanner::scan(const std::filesystem::path& root) const {
    return scan(root, {});
}

std::vector<FileEntry> FileScanner::scan(
    const std::filesystem::path& root,
    const std::function<bool()>& should_cancel) const {
    std::vector<FileEntry> files;

    std::error_code status_error;
    if (!std::filesystem::exists(root, status_error) ||
        !std::filesystem::is_directory(root, status_error)) {
        return files;
    }

    std::error_code iter_error;
    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        iter_error);
    std::filesystem::recursive_directory_iterator end;

    while (it != end) {
        if (should_cancel && should_cancel()) {
            break;
        }

        if (!iter_error) {
            std::error_code file_error;
            if (it->is_regular_file(file_error)) {
                const auto size = it->file_size(file_error);
                if (!file_error) {
                    files.push_back(FileEntry{it->path(), size});
                }
            }
            it.increment(iter_error);
        } else {
            iter_error.clear();
            it.increment(iter_error);
        }
    }

    std::sort(files.begin(), files.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
        return lhs.path.lexically_normal().string() < rhs.path.lexically_normal().string();
    });

    return files;
}

}  // namespace duplicate_library
