#include "duplicate_finder/hash_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

#include <opencv2/opencv.hpp>
#include <chromaprint.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace duplicate_library {
namespace {

constexpr std::size_t kReadBufferSize = 64 * 1024;
constexpr std::size_t kSampleChunkSize = 64 * 1024;

constexpr std::uint64_t kPrime1 = 11400714785074694791ull;
constexpr std::uint64_t kPrime2 = 14029467366897019727ull;
constexpr std::uint64_t kPrime3 = 1609587929392839161ull;
constexpr std::uint64_t kPrime4 = 9650029242287828579ull;
constexpr std::uint64_t kPrime5 = 2870177450012600261ull;

std::uint64_t rotate_left(std::uint64_t value, int shift) {
    return (value << shift) | (value >> (64 - shift));
}

std::uint64_t read64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(input[i]) << (8 * i);
    }
    return value;
}

std::uint32_t read32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(input[i]) << (8 * i);
    }
    return value;
}

std::array<std::uint8_t, 8> offset_marker_bytes(std::uintmax_t offset) {
    std::array<std::uint8_t, 8> marker{};
    auto value = static_cast<std::uint64_t>(offset);
    for (std::size_t i = 0; i < marker.size(); ++i) {
        marker[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu);
    }
    return marker;
}

std::uint64_t round_accumulator(std::uint64_t acc, std::uint64_t lane) {
    acc += lane * kPrime2;
    acc = rotate_left(acc, 31);
    acc *= kPrime1;
    return acc;
}

std::uint64_t merge_round(std::uint64_t acc, std::uint64_t value) {
    acc ^= round_accumulator(0, value);
    acc = acc * kPrime1 + kPrime4;
    return acc;
}

std::uint64_t avalanche(std::uint64_t value) {
    value ^= value >> 33;
    value *= kPrime2;
    value ^= value >> 29;
    value *= kPrime3;
    value ^= value >> 32;
    return value;
}

class Xxh64 {
public:
    void update(const std::uint8_t* data, std::size_t length) {
        total_length_ += length;

        if (buffer_size_ + length < buffer_.size()) {
            std::copy(data, data + length, buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_));
            buffer_size_ += length;
            return;
        }

        std::size_t offset = 0;

        if (buffer_size_ > 0) {
            const auto needed = buffer_.size() - buffer_size_;
            std::copy(data, data + needed, buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_));
            process_block(buffer_.data());
            buffer_size_ = 0;
            offset += needed;
        }

        while (offset + buffer_.size() <= length) {
            process_block(data + offset);
            offset += buffer_.size();
        }

        const auto remaining = length - offset;
        std::copy(data + offset, data + length, buffer_.begin());
        buffer_size_ = remaining;
    }

    std::uint64_t digest() const {
        std::uint64_t hash = 0;

        if (total_length_ >= buffer_.size()) {
            hash = rotate_left(v1_, 1) +
                   rotate_left(v2_, 7) +
                   rotate_left(v3_, 12) +
                   rotate_left(v4_, 18);
            hash = merge_round(hash, v1_);
            hash = merge_round(hash, v2_);
            hash = merge_round(hash, v3_);
            hash = merge_round(hash, v4_);
        } else {
            hash = kPrime5;
        }

        hash += total_length_;

        const auto* data = buffer_.data();
        std::size_t offset = 0;

        while (offset + 8 <= buffer_size_) {
            const auto lane = read64(data + offset);
            hash ^= round_accumulator(0, lane);
            hash = rotate_left(hash, 27) * kPrime1 + kPrime4;
            offset += 8;
        }

        if (offset + 4 <= buffer_size_) {
            hash ^= static_cast<std::uint64_t>(read32(data + offset)) * kPrime1;
            hash = rotate_left(hash, 23) * kPrime2 + kPrime3;
            offset += 4;
        }

        while (offset < buffer_size_) {
            hash ^= static_cast<std::uint64_t>(data[offset]) * kPrime5;
            hash = rotate_left(hash, 11) * kPrime1;
            ++offset;
        }

        return avalanche(hash);
    }

private:
    void process_block(const std::uint8_t* block) {
        v1_ = round_accumulator(v1_, read64(block + 0));
        v2_ = round_accumulator(v2_, read64(block + 8));
        v3_ = round_accumulator(v3_, read64(block + 16));
        v4_ = round_accumulator(v4_, read64(block + 24));
    }

    std::uint64_t total_length_ = 0;
    std::uint64_t v1_ = kPrime1 + kPrime2;
    std::uint64_t v2_ = kPrime2;
    std::uint64_t v3_ = 0;
    std::uint64_t v4_ = 0 - kPrime1;
    std::array<std::uint8_t, 32> buffer_{};
    std::size_t buffer_size_ = 0;
};

std::string to_hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

std::string hash_bytes_to_hex(const std::uint8_t* data, std::size_t size) {
    Xxh64 state;
    if (size > 0) {
        state.update(data, size);
    }
    return to_hex(state.digest());
}

std::string normalized_extension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

bool is_video_extension(const std::string& ext) {
    static const std::set<std::string> kVideoExts = {
        ".mp4", ".mkv", ".avi", ".mov", ".flv", ".wmv", ".webm"};
    return kVideoExts.count(ext) > 0;
}

}  // namespace

FileKind HashEngine::classify(const std::filesystem::path& path) const {
    const auto ext = normalized_extension(path);

    static const std::set<std::string> kImageExts = {
        ".jpg", ".jpeg", ".png", ".bmp", ".webp", ".gif", ".tiff"};
    static const std::set<std::string> kVideoExts = {
        ".mp4", ".mkv", ".avi", ".mov", ".flv", ".wmv", ".webm"};
    static const std::set<std::string> kAudioExts = {
        ".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg", ".wma"};

    if (kImageExts.count(ext) > 0) {
        return FileKind::kImage;
    }
    if (kVideoExts.count(ext) > 0) {
        return FileKind::kVideo;
    }
    if (kAudioExts.count(ext) > 0) {
        return FileKind::kAudio;
    }
    return FileKind::kBinary;
}

std::string HashEngine::compute_xxhash(const std::filesystem::path& path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    Xxh64 state;
    std::vector<std::uint8_t> buffer(kReadBufferSize);

    while (input.good()) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            state.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }

    if (input.bad()) {
        return {};
    }

    return to_hex(state.digest());
}

std::string HashEngine::compute_sample_xxhash(
    const std::filesystem::path& path,
    std::uintmax_t file_size) const {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::vector<std::uint8_t> buffer(kSampleChunkSize);
    std::vector<std::uint8_t> sample_bytes;
    std::vector<std::uintmax_t> offsets;
    offsets.push_back(0);

    if (file_size > kSampleChunkSize) {
        offsets.push_back((file_size / 2) > (kSampleChunkSize / 2)
            ? (file_size / 2) - (kSampleChunkSize / 2)
            : 0);
    }

    if (file_size > (kSampleChunkSize * 2)) {
        offsets.push_back(file_size - kSampleChunkSize);
    }

    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    sample_bytes.reserve(offsets.size() * (buffer.size() + 8));

    for (const auto offset : offsets) {
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!input.good()) {
            continue;
        }

        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        const auto marker = offset_marker_bytes(offset);
        sample_bytes.insert(sample_bytes.end(), marker.begin(), marker.end());
        if (count > 0) {
            sample_bytes.insert(
                sample_bytes.end(),
                buffer.begin(),
                buffer.begin() + static_cast<std::size_t>(count));
        }
    }

    if (input.bad()) {
        return {};
    }

    return hash_bytes_to_hex(sample_bytes.data(), sample_bytes.size());
}

std::string HashEngine::compute_visual_hash(const std::filesystem::path& path) const {
    cv::Mat frame;
    const auto ext = normalized_extension(path);

    if (is_video_extension(ext)) {
        cv::VideoCapture capture(path.string());
        if (!capture.isOpened()) {
            return {};
        }

        const int frame_count = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
        if (frame_count > 0) {
            capture.set(cv::CAP_PROP_POS_FRAMES, frame_count / 2);
        }

        if (!capture.read(frame)) {
            return {};
        }
    } else {
        frame = cv::imread(path.string(), cv::IMREAD_COLOR);
    }

    if (frame.empty()) {
        return {};
    }

    cv::Mat gray;
    cv::Mat resized;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, resized, cv::Size(9, 8));

    std::uint64_t value = 0;
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            if (resized.at<unsigned char>(row, col) > resized.at<unsigned char>(row, col + 1)) {
                value |= (1ull << (row * 8 + col));
            }
        }
    }

    return to_hex(value);
}

std::string HashEngine::compute_audio_hash(const std::filesystem::path& path) const {
    av_log_set_level(AV_LOG_QUIET);

    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
    SwrContext* resample_context = nullptr;
    ChromaprintContext* fingerprint_context = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;

    std::string result;

    if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) != 0) {
        return {};
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        avformat_close_input(&format_context);
        return {};
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
        return {};
    }

    AVCodecParameters* codec_parameters = format_context->streams[stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codec_parameters->codec_id);
    if (codec == nullptr) {
        avformat_close_input(&format_context);
        return {};
    }

    codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
        avformat_close_input(&format_context);
        return {};
    }

    avcodec_parameters_to_context(codec_context, codec_parameters);
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return {};
    }

    fingerprint_context = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
    if (fingerprint_context == nullptr || chromaprint_start(fingerprint_context, 44100, 1) == 0) {
        if (fingerprint_context != nullptr) {
            chromaprint_free(fingerprint_context);
        }
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return {};
    }

    resample_context = swr_alloc();
    av_opt_set_chlayout(resample_context, "in_chlayout", &codec_context->ch_layout, 0);
    av_opt_set_int(resample_context, "in_sample_rate", codec_context->sample_rate, 0);
    av_opt_set_sample_fmt(resample_context, "in_sample_fmt", codec_context->sample_fmt, 0);
    AVChannelLayout mono_layout;
    av_channel_layout_default(&mono_layout, 1);
    av_opt_set_chlayout(resample_context, "out_chlayout", &mono_layout, 0);
    av_opt_set_int(resample_context, "out_sample_rate", 44100, 0);
    av_opt_set_sample_fmt(resample_context, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_channel_layout_uninit(&mono_layout);

    if (swr_init(resample_context) < 0) {
        swr_free(&resample_context);
        chromaprint_free(fingerprint_context);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return {};
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();

    constexpr int kMaxSamples = 44100 * 120;
    int processed = 0;

    while (av_read_frame(format_context, packet) >= 0 && processed < kMaxSamples) {
        if (packet->stream_index == stream_index) {
            if (avcodec_send_packet(codec_context, packet) == 0) {
                while (avcodec_receive_frame(codec_context, frame) == 0 && processed < kMaxSamples) {
                    uint8_t* output = nullptr;

                    const int output_samples = av_rescale_rnd(
                        frame->nb_samples,
                        44100,
                        codec_context->sample_rate,
                        AV_ROUND_UP);

                    if (av_samples_alloc(&output, nullptr, 1, output_samples, AV_SAMPLE_FMT_S16, 0) >= 0) {
                        const int converted = swr_convert(
                            resample_context,
                            &output,
                            output_samples,
                            const_cast<const uint8_t**>(frame->data),
                            frame->nb_samples);

                        if (converted > 0) {
                            chromaprint_feed(
                                fingerprint_context,
                                reinterpret_cast<int16_t*>(output),
                                converted);
                            processed += converted;
                        }

                        av_freep(&output);
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    chromaprint_finish(fingerprint_context);

    char* fingerprint = nullptr;
    if (chromaprint_get_fingerprint(fingerprint_context, &fingerprint) != 0 && fingerprint != nullptr) {
        result = fingerprint;
        chromaprint_dealloc(fingerprint);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&resample_context);
    chromaprint_free(fingerprint_context);
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);

    return result;
}

HashResult HashEngine::compute_hashes(const FileEntry& file) const {
    HashResult result;
    result.strict_hash = compute_xxhash(file.path);

    switch (file.kind) {
        case FileKind::kImage:
        case FileKind::kVideo:
            result.visual_hash = compute_visual_hash(file.path);
            break;
        case FileKind::kAudio:
            result.audio_hash = compute_audio_hash(file.path);
            break;
        case FileKind::kBinary:
            break;
    }

    return result;
}

}  // namespace duplicate_library
