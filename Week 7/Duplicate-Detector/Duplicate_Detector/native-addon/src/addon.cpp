#include <napi.h>

#include "duplicate_finder/duplicate_engine.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

std::atomic<int> g_progress{0};
std::atomic<bool> g_running{false};
std::mutex g_scan_mutex;

class ScanWorker final : public Napi::AsyncWorker {
public:
    ScanWorker(
        Napi::Env env,
        std::vector<std::string> paths,
        Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env),
          paths_(std::move(paths)),
          deferred_(deferred) {}

    void Execute() override {
        std::lock_guard<std::mutex> lock(g_scan_mutex);
        duplicate_library::DuplicateEngine engine;
        engine.setProgressCallback([](int progress) {
            g_progress.store(progress);
        });

        try {
            result_ = engine.scan(paths_);
        } catch (const std::exception& error) {
            SetError(error.what());
        } catch (...) {
            SetError("Unknown scan failure");
        }
    }

    void OnOK() override {
        g_progress.store(100);
        g_running.store(false);

        Napi::Array groups = Napi::Array::New(Env(), result_.size());
        for (std::size_t i = 0; i < result_.size(); ++i) {
            Napi::Object group = Napi::Object::New(Env());
            group.Set("type", Napi::String::New(Env(), result_[i].type));

            Napi::Array files = Napi::Array::New(Env(), result_[i].files.size());
            for (std::size_t j = 0; j < result_[i].files.size(); ++j) {
                files.Set(j, Napi::String::New(Env(), result_[i].files[j]));
            }

            group.Set("files", files);
            groups.Set(i, group);
        }

        deferred_.Resolve(groups);
    }

    void OnError(const Napi::Error& error) override {
        g_progress.store(0);
        g_running.store(false);
        deferred_.Reject(error.Value());
    }

private:
    std::vector<std::string> paths_;
    Napi::Promise::Deferred deferred_;
    std::vector<duplicate_library::DuplicateGroup> result_;
};

Napi::Value Scan(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() != 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "scan(paths) expects an array of string paths").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Array paths_array = info[0].As<Napi::Array>();
    std::vector<std::string> paths;
    paths.reserve(paths_array.Length());
    for (uint32_t i = 0; i < paths_array.Length(); ++i) {
        Napi::Value val = paths_array.Get(i);
        if (val.IsString()) {
            paths.push_back(val.As<Napi::String>().Utf8Value());
        }
    }

    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) {
        Napi::Error::New(env, "A scan is already in progress").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    g_progress.store(0);

    auto deferred = Napi::Promise::Deferred::New(env);

    auto* worker = new ScanWorker(env, std::move(paths), deferred);
    worker->Queue();

    return deferred.Promise();
}

Napi::Value GetProgress(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), g_progress.load());
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("scan", Napi::Function::New(env, Scan));
    exports.Set("getProgress", Napi::Function::New(env, GetProgress));
    return exports;
}

}  // namespace

NODE_API_MODULE(duplicate_engine_addon, Init)
