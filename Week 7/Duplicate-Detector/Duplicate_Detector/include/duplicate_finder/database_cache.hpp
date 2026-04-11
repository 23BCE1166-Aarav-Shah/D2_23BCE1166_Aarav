#pragma once

#include "duplicate_finder/types.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>

namespace duplicate_library {

struct CachedFileRecord {
    std::string path;
    std::uintmax_t size = 0;
    std::int64_t mtime_ns = 0;
    FileKind kind = FileKind::kBinary;
    HashResult hashes;
};

class DatabaseCache {
public:
    DatabaseCache();
    explicit DatabaseCache(const std::filesystem::path& database_path);
    ~DatabaseCache();

    bool is_available() const;
    const std::filesystem::path& database_path() const;

    std::optional<CachedFileRecord> lookup(const std::filesystem::path& path) const;
    bool upsert(
        const FileEntry& file,
        const HashResult& hashes,
        std::int64_t mtime_ns);

private:
    class Impl;
    Impl* impl_;
};

}  // namespace duplicate_library
