#include "duplicate_finder/database_cache.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace duplicate_library {
namespace {

struct sqlite3;
struct sqlite3_stmt;

constexpr int kSqliteOk = 0;
constexpr int kSqliteRow = 100;
constexpr int kSqliteDone = 101;
constexpr int kSqliteOpenReadWrite = 0x00000002;
constexpr int kSqliteOpenCreate = 0x00000004;
constexpr int kSqliteOpenFullMutex = 0x00010000;

struct SqliteApi {
    int (*open_v2)(const char*, sqlite3**, int, const char*) = nullptr;
    int (*close)(sqlite3*) = nullptr;
    int (*exec)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**) = nullptr;
    void (*free_fn)(void*) = nullptr;
    const char* (*errmsg)(sqlite3*) = nullptr;
    int (*prepare_v2)(sqlite3*, const char*, int, sqlite3_stmt**, const char**) = nullptr;
    int (*bind_text)(sqlite3_stmt*, int, const char*, int, void (*)(void*)) = nullptr;
    int (*bind_int64)(sqlite3_stmt*, int, long long) = nullptr;
    int (*bind_int)(sqlite3_stmt*, int, int) = nullptr;
    int (*bind_null)(sqlite3_stmt*, int) = nullptr;
    int (*step)(sqlite3_stmt*) = nullptr;
    int (*finalize)(sqlite3_stmt*) = nullptr;
    const unsigned char* (*column_text)(sqlite3_stmt*, int) = nullptr;
    long long (*column_int64)(sqlite3_stmt*, int) = nullptr;
    int (*column_int)(sqlite3_stmt*, int) = nullptr;
    int (*reset)(sqlite3_stmt*) = nullptr;
    int (*clear_bindings)(sqlite3_stmt*) = nullptr;
};

class DynamicSqlite {
public:
    DynamicSqlite() {
        handle_ = dlopen("libsqlite3.so.0", RTLD_NOW | RTLD_LOCAL);
        if (handle_ == nullptr) {
            throw std::runtime_error("Unable to load libsqlite3.so.0");
        }

        load(api_.open_v2, "sqlite3_open_v2");
        load(api_.close, "sqlite3_close");
        load(api_.exec, "sqlite3_exec");
        load(api_.free_fn, "sqlite3_free");
        load(api_.errmsg, "sqlite3_errmsg");
        load(api_.prepare_v2, "sqlite3_prepare_v2");
        load(api_.bind_text, "sqlite3_bind_text");
        load(api_.bind_int64, "sqlite3_bind_int64");
        load(api_.bind_int, "sqlite3_bind_int");
        load(api_.bind_null, "sqlite3_bind_null");
        load(api_.step, "sqlite3_step");
        load(api_.finalize, "sqlite3_finalize");
        load(api_.column_text, "sqlite3_column_text");
        load(api_.column_int64, "sqlite3_column_int64");
        load(api_.column_int, "sqlite3_column_int");
        load(api_.reset, "sqlite3_reset");
        load(api_.clear_bindings, "sqlite3_clear_bindings");
    }

    ~DynamicSqlite() {
        if (handle_ != nullptr) {
            dlclose(handle_);
        }
    }

    const SqliteApi& api() const {
        return api_;
    }

private:
    template <typename FunctionPointer>
    void load(FunctionPointer& target, const char* symbol) {
        target = reinterpret_cast<FunctionPointer>(dlsym(handle_, symbol));
        if (target == nullptr) {
            throw std::runtime_error(std::string("Missing SQLite symbol: ") + symbol);
        }
    }

    void* handle_ = nullptr;
    SqliteApi api_{};
};

std::filesystem::path default_cache_path() {
    const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache_home != nullptr && std::strlen(xdg_cache_home) > 0) {
        return std::filesystem::path(xdg_cache_home) / "duplicate-finder" / "cache.sqlite3";
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && std::strlen(home) > 0) {
        return std::filesystem::path(home) / ".cache" / "duplicate-finder" / "cache.sqlite3";
    }

    return std::filesystem::temp_directory_path() / "duplicate-finder-cache.sqlite3";
}

std::string normalized_path_string(const std::filesystem::path& path) {
    return path.lexically_normal().string();
}

std::int64_t unix_time_now() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string text_column(const SqliteApi& api, sqlite3_stmt* statement, int column) {
    const auto* value = api.column_text(statement, column);
    if (value == nullptr) {
        return {};
    }
    return reinterpret_cast<const char*>(value);
}

}  // namespace

class DatabaseCache::Impl {
public:
    explicit Impl(std::filesystem::path database_path)
        : sqlite_(std::make_unique<DynamicSqlite>()),
          database_path_(database_path.empty() ? default_cache_path() : std::move(database_path)) {
        std::filesystem::create_directories(database_path_.parent_path());

        const auto open_result = sqlite_->api().open_v2(
            database_path_.string().c_str(),
            &db_,
            kSqliteOpenReadWrite | kSqliteOpenCreate | kSqliteOpenFullMutex,
            nullptr);
        if (open_result != kSqliteOk || db_ == nullptr) {
            throw std::runtime_error("Unable to open SQLite cache database");
        }

        execute(
            "CREATE TABLE IF NOT EXISTS file_cache ("
            "path TEXT PRIMARY KEY,"
            "size_bytes INTEGER NOT NULL,"
            "mtime_ns INTEGER NOT NULL,"
            "kind INTEGER NOT NULL,"
            "strict_hash TEXT,"
            "visual_hash TEXT,"
            "audio_hash TEXT,"
            "updated_at INTEGER NOT NULL"
            ");");
        execute(
            "CREATE INDEX IF NOT EXISTS idx_file_cache_mtime "
            "ON file_cache(mtime_ns);");
    }

    ~Impl() {
        if (db_ != nullptr) {
            sqlite_->api().close(db_);
        }
    }

    std::optional<CachedFileRecord> lookup(const std::filesystem::path& path) const {
        static constexpr char kSql[] =
            "SELECT path, size_bytes, mtime_ns, kind, strict_hash, visual_hash, audio_hash "
            "FROM file_cache WHERE path = ?1;";

        sqlite3_stmt* statement = nullptr;
        prepare(kSql, &statement);
        bind_text(statement, 1, normalized_path_string(path));

        const int step_result = sqlite_->api().step(statement);
        if (step_result != kSqliteRow) {
            sqlite_->api().finalize(statement);
            return std::nullopt;
        }

        CachedFileRecord record;
        record.path = text_column(sqlite_->api(), statement, 0);
        record.size = static_cast<std::uintmax_t>(sqlite_->api().column_int64(statement, 1));
        record.mtime_ns = static_cast<std::int64_t>(sqlite_->api().column_int64(statement, 2));
        record.kind = static_cast<FileKind>(sqlite_->api().column_int(statement, 3));
        record.hashes.strict_hash = text_column(sqlite_->api(), statement, 4);
        record.hashes.visual_hash = text_column(sqlite_->api(), statement, 5);
        record.hashes.audio_hash = text_column(sqlite_->api(), statement, 6);

        sqlite_->api().finalize(statement);
        return record;
    }

    bool upsert(const FileEntry& file, const HashResult& hashes, std::int64_t mtime_ns) {
        static constexpr char kSql[] =
            "INSERT INTO file_cache(path, size_bytes, mtime_ns, kind, strict_hash, visual_hash, audio_hash, updated_at) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
            "ON CONFLICT(path) DO UPDATE SET "
            "size_bytes=excluded.size_bytes, "
            "mtime_ns=excluded.mtime_ns, "
            "kind=excluded.kind, "
            "strict_hash=excluded.strict_hash, "
            "visual_hash=excluded.visual_hash, "
            "audio_hash=excluded.audio_hash, "
            "updated_at=excluded.updated_at;";

        sqlite3_stmt* statement = nullptr;
        prepare(kSql, &statement);

        bind_text(statement, 1, normalized_path_string(file.path));
        bind_int64(statement, 2, static_cast<std::int64_t>(file.size));
        bind_int64(statement, 3, mtime_ns);
        bind_int(statement, 4, static_cast<int>(file.kind));
        bind_optional_text(statement, 5, hashes.strict_hash);
        bind_optional_text(statement, 6, hashes.visual_hash);
        bind_optional_text(statement, 7, hashes.audio_hash);
        bind_int64(statement, 8, unix_time_now());

        const bool success = sqlite_->api().step(statement) == kSqliteDone;
        sqlite_->api().finalize(statement);
        return success;
    }

    const std::filesystem::path& database_path() const {
        return database_path_;
    }

private:
    void execute(const char* sql) {
        char* error_message = nullptr;
        const int result = sqlite_->api().exec(db_, sql, nullptr, nullptr, &error_message);
        if (result != kSqliteOk) {
            std::string message = error_message == nullptr ? "SQLite exec failed" : error_message;
            if (error_message != nullptr) {
                sqlite_->api().free_fn(error_message);
            }
            throw std::runtime_error(message);
        }
    }

    void prepare(const char* sql, sqlite3_stmt** statement) const {
        if (sqlite_->api().prepare_v2(db_, sql, -1, statement, nullptr) != kSqliteOk) {
            throw std::runtime_error(sqlite_->api().errmsg(db_));
        }
    }

    void bind_text(sqlite3_stmt* statement, int index, const std::string& value) const {
        if (sqlite_->api().bind_text(statement, index, value.c_str(), -1, nullptr) != kSqliteOk) {
            throw std::runtime_error(sqlite_->api().errmsg(db_));
        }
    }

    void bind_optional_text(sqlite3_stmt* statement, int index, const std::string& value) const {
        if (value.empty()) {
            if (sqlite_->api().bind_null(statement, index) != kSqliteOk) {
                throw std::runtime_error(sqlite_->api().errmsg(db_));
            }
            return;
        }
        bind_text(statement, index, value);
    }

    void bind_int64(sqlite3_stmt* statement, int index, std::int64_t value) const {
        if (sqlite_->api().bind_int64(statement, index, value) != kSqliteOk) {
            throw std::runtime_error(sqlite_->api().errmsg(db_));
        }
    }

    void bind_int(sqlite3_stmt* statement, int index, int value) const {
        if (sqlite_->api().bind_int(statement, index, value) != kSqliteOk) {
            throw std::runtime_error(sqlite_->api().errmsg(db_));
        }
    }

    std::unique_ptr<DynamicSqlite> sqlite_;
    sqlite3* db_ = nullptr;
    std::filesystem::path database_path_;
};

DatabaseCache::DatabaseCache()
    : impl_(nullptr) {
    try {
        impl_ = new Impl({});
    } catch (...) {
        impl_ = nullptr;
    }
}

DatabaseCache::DatabaseCache(const std::filesystem::path& database_path)
    : impl_(nullptr) {
    try {
        impl_ = new Impl(database_path);
    } catch (...) {
        impl_ = nullptr;
    }
}

DatabaseCache::~DatabaseCache() {
    delete impl_;
}

bool DatabaseCache::is_available() const {
    return impl_ != nullptr;
}

const std::filesystem::path& DatabaseCache::database_path() const {
    static const std::filesystem::path empty_path;
    return impl_ == nullptr ? empty_path : impl_->database_path();
}

std::optional<CachedFileRecord> DatabaseCache::lookup(const std::filesystem::path& path) const {
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    return impl_->lookup(path);
}

bool DatabaseCache::upsert(
    const FileEntry& file,
    const HashResult& hashes,
    std::int64_t mtime_ns) {
    if (impl_ == nullptr) {
        return false;
    }
    return impl_->upsert(file, hashes, mtime_ns);
}

}  // namespace duplicate_library
