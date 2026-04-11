#include "duplicate_finder/preview_engine.hpp"

#include <opencv2/opencv.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace duplicate_library {
namespace {

constexpr int kThumbnailEdge = 256;
constexpr int kWaveformWidth = 800;
constexpr int kWaveformHeight = 220;
constexpr int kTargetAudioRate = 22050;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::filesystem::path default_preview_cache_dir() {
    const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache_home != nullptr && *xdg_cache_home != '\0') {
        return std::filesystem::path(xdg_cache_home) / "duplicate-finder" / "previews";
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".cache" / "duplicate-finder" / "previews";
    }

    return std::filesystem::temp_directory_path() / "duplicate-finder-previews";
}

std::string normalized_string(const std::filesystem::path& path) {
    return path.lexically_normal().string();
}

std::int64_t file_mtime_ns(const std::filesystem::path& path) {
    const auto file_time = std::filesystem::last_write_time(path);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(file_time.time_since_epoch()).count();
}

std::string deterministic_hex_key(const FileEntry& file) {
    const auto payload = normalized_string(file.path) + "|" +
                         std::to_string(file.size) + "|" +
                         std::to_string(file_mtime_ns(file.path)) + "|" +
                         std::to_string(static_cast<int>(file.kind));

    std::uint64_t hash = kFnvOffset;
    for (unsigned char byte : payload) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kFnvPrime;
    }

    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

cv::Mat resize_to_thumbnail(const cv::Mat& source) {
    if (source.empty()) {
        return {};
    }

    const int width = source.cols;
    const int height = source.rows;
    const double scale = static_cast<double>(kThumbnailEdge) /
                         static_cast<double>(std::max(width, height));
    const int target_width = std::max(1, static_cast<int>(width * scale));
    const int target_height = std::max(1, static_cast<int>(height * scale));

    cv::Mat resized;
    cv::resize(source, resized, cv::Size(target_width, target_height), 0.0, 0.0, cv::INTER_AREA);
    return resized;
}

bool save_png(const std::filesystem::path& output_path, const cv::Mat& image) {
    if (image.empty()) {
        return false;
    }
    std::filesystem::create_directories(output_path.parent_path());
    return cv::imwrite(output_path.string(), image);
}

PreviewKind preview_kind_for_file(FileKind kind) {
    switch (kind) {
        case FileKind::kImage:
            return PreviewKind::kImageThumbnail;
        case FileKind::kVideo:
            return PreviewKind::kVideoFrame;
        case FileKind::kAudio:
            return PreviewKind::kAudioWaveform;
        case FileKind::kBinary:
            return PreviewKind::kUnsupported;
    }

    return PreviewKind::kUnsupported;
}

bool generate_image_thumbnail(const std::filesystem::path& input, const std::filesystem::path& output) {
    const cv::Mat image = cv::imread(input.string(), cv::IMREAD_COLOR);
    return save_png(output, resize_to_thumbnail(image));
}

bool generate_video_thumbnail(const std::filesystem::path& input, const std::filesystem::path& output) {
    cv::VideoCapture capture(input.string());
    if (!capture.isOpened()) {
        return false;
    }

    const int frame_count = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
    if (frame_count > 0) {
        capture.set(cv::CAP_PROP_POS_FRAMES, frame_count / 2);
    }

    cv::Mat frame;
    if (!capture.read(frame)) {
        return false;
    }

    return save_png(output, resize_to_thumbnail(frame));
}

bool decode_audio_samples(const std::filesystem::path& input, std::vector<float>* samples) {
    av_log_set_level(AV_LOG_QUIET);

    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwrContext* swr = nullptr;
    bool success = false;

    if (avformat_open_input(&format_context, input.c_str(), nullptr, nullptr) != 0) {
        return false;
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        avformat_close_input(&format_context);
        return false;
    }

    int stream_index = -1;
    for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream_index = static_cast<int>(i);
            break;
        }
    }

    if (stream_index < 0) {
        avformat_close_input(&format_context);
        return false;
    }

    AVCodecParameters* codec_parameters = format_context->streams[stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codec_parameters->codec_id);
    if (codec == nullptr) {
        avformat_close_input(&format_context);
        return false;
    }

    codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
        avformat_close_input(&format_context);
        return false;
    }

    avcodec_parameters_to_context(codec_context, codec_parameters);
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    swr = swr_alloc();
    AVChannelLayout mono_layout;
    av_channel_layout_default(&mono_layout, 1);
    av_opt_set_chlayout(swr, "in_chlayout", &codec_context->ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", codec_context->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", codec_context->sample_fmt, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &mono_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", kTargetAudioRate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
    av_channel_layout_uninit(&mono_layout);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();

    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == stream_index) {
            if (avcodec_send_packet(codec_context, packet) == 0) {
                while (avcodec_receive_frame(codec_context, frame) == 0) {
                    const int output_samples = av_rescale_rnd(
                        frame->nb_samples,
                        kTargetAudioRate,
                        codec_context->sample_rate,
                        AV_ROUND_UP);

                    uint8_t* output = nullptr;
                    if (av_samples_alloc(&output, nullptr, 1, output_samples, AV_SAMPLE_FMT_FLT, 0) >= 0) {
                        const int converted = swr_convert(
                            swr,
                            &output,
                            output_samples,
                            const_cast<const uint8_t**>(frame->data),
                            frame->nb_samples);

                        if (converted > 0) {
                            auto* float_samples = reinterpret_cast<float*>(output);
                            samples->insert(samples->end(), float_samples, float_samples + converted);
                            success = true;
                        }

                        av_freep(&output);
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&swr);
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);

    return success;
}

bool generate_audio_waveform(const std::filesystem::path& input, const std::filesystem::path& output) {
    std::vector<float> samples;
    if (!decode_audio_samples(input, &samples) || samples.empty()) {
        return false;
    }

    cv::Mat canvas(kWaveformHeight, kWaveformWidth, CV_8UC3, cv::Scalar(11, 17, 26));
    cv::line(
        canvas,
        cv::Point(0, kWaveformHeight / 2),
        cv::Point(kWaveformWidth, kWaveformHeight / 2),
        cv::Scalar(42, 61, 88),
        1,
        cv::LINE_AA);

    const std::size_t bucket_size = std::max<std::size_t>(1, samples.size() / static_cast<std::size_t>(kWaveformWidth));

    for (int x = 0; x < kWaveformWidth; ++x) {
        const std::size_t start = static_cast<std::size_t>(x) * bucket_size;
        if (start >= samples.size()) {
            break;
        }

        const std::size_t end = std::min(samples.size(), start + bucket_size);
        float peak = 0.0f;
        for (std::size_t i = start; i < end; ++i) {
            peak = std::max(peak, std::abs(samples[i]));
        }

        const int amplitude = static_cast<int>(peak * (kWaveformHeight / 2 - 12));
        cv::line(
            canvas,
            cv::Point(x, kWaveformHeight / 2 - amplitude),
            cv::Point(x, kWaveformHeight / 2 + amplitude),
            cv::Scalar(84, 215, 255),
            1,
            cv::LINE_AA);
    }

    return save_png(output, canvas);
}

}  // namespace

PreviewEngine::PreviewEngine()
    : cache_directory_(default_preview_cache_dir()) {
    std::filesystem::create_directories(cache_directory_);
}

PreviewEngine::PreviewEngine(const AppConfig& config)
    : cache_directory_(config.paths.preview_cache_dir.empty()
          ? default_preview_cache_dir()
          : config.paths.preview_cache_dir) {
    std::filesystem::create_directories(cache_directory_);
}

PreviewResult PreviewEngine::generate(const FileEntry& file) const {
    const auto kind = preview_kind_for_file(file.kind);
    if (kind == PreviewKind::kUnsupported) {
        return { {}, PreviewKind::kUnsupported, false };
    }

    const auto cache_key = deterministic_hex_key(file);
    const auto output_path = cache_directory_ / (cache_key + ".png");
    if (std::filesystem::exists(output_path)) {
        return { output_path, kind, true };
    }

    bool generated = false;
    switch (kind) {
        case PreviewKind::kImageThumbnail:
            generated = generate_image_thumbnail(file.path, output_path);
            break;
        case PreviewKind::kVideoFrame:
            generated = generate_video_thumbnail(file.path, output_path);
            break;
        case PreviewKind::kAudioWaveform:
            generated = generate_audio_waveform(file.path, output_path);
            break;
        case PreviewKind::kUnsupported:
            break;
    }

    if (!generated) {
        return { {}, kind, false };
    }

    return { output_path, kind, false };
}

const std::filesystem::path& PreviewEngine::cache_directory() const {
    return cache_directory_;
}

}  // namespace duplicate_library
