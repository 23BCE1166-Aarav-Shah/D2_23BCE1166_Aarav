#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <mpi.h>
#include <cstdint>
#include <cstring>

#include <opencv2/opencv.hpp>
#include <chromaprint.h>
#include "xxhash/xxhash.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
}

using namespace std;
namespace fs = filesystem;

enum FileType : uint32_t { BINARY_STRICT = 0, VISUAL_FUZZY = 1, AUDIO_FINGERPRINT = 2 };

const size_t FILE_BUFFER_SIZE = 65536;

/* -------------------------------------------------- */
/* FILE TYPE CLASSIFICATION                          */
/* -------------------------------------------------- */

FileType get_file_type(const fs::path& path) {
    string ext = path.extension().string();
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const set<string> visual_exts = {
        ".jpg",".jpeg",".png",".bmp",".webp",".gif",
        ".mp4",".mkv",".avi",".mov",".flv",".wmv"
    };

    static const set<string> audio_exts = {
        ".mp3",".wav",".flac",".m4a",".aac",".ogg",".wma"
    };

    if (visual_exts.count(ext)) return VISUAL_FUZZY;
    if (audio_exts.count(ext)) return AUDIO_FINGERPRINT;
    return BINARY_STRICT;
}

/* -------------------------------------------------- */
/* STRICT HASH (XXH3 128-bit)                        */
/* -------------------------------------------------- */

string compute_xxh3(const fs::path& path) {
    ifstream file(path, ios::binary);
    if (!file.is_open()) return "";

    XXH3_state_t* state = XXH3_createState();
    if (!state) return "";

    XXH3_128bits_reset(state);

    vector<char> buffer(FILE_BUFFER_SIZE);

    while (file) {
        file.read(buffer.data(), buffer.size());
        streamsize got = file.gcount();
        if (got > 0)
            XXH3_128bits_update(state, buffer.data(), got);
    }

    XXH128_hash_t hash = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    ostringstream ss;
    ss << hex << setfill('0')
       << setw(16) << hash.high64
       << setw(16) << hash.low64;

    return ss.str();
}

/* -------------------------------------------------- */
/* VISUAL HASH (dHash)                               */
/* -------------------------------------------------- */

string compute_visual_hash(const fs::path& path) {
    cv::Mat frame;

    string ext = path.extension().string();
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool is_video = (ext == ".mp4" || ext == ".mkv" || ext == ".avi" ||
                     ext == ".mov" || ext == ".flv" || ext == ".wmv");

    if (is_video) {
        cv::VideoCapture cap(path.string());
        if (!cap.isOpened()) return "";
        int total = cap.get(cv::CAP_PROP_FRAME_COUNT);
        if (total > 0)
            cap.set(cv::CAP_PROP_POS_FRAMES, total / 2);
        if (!cap.read(frame)) return "";
    } else {
        frame = cv::imread(path.string());
    }

    if (frame.empty()) return "";

    cv::Mat gray, resized;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, resized, cv::Size(9,8));

    uint64_t hash = 0;
    for (int r=0; r<8; r++)
        for (int c=0; c<8; c++)
            if (resized.at<unsigned char>(r,c) >
                resized.at<unsigned char>(r,c+1))
                hash |= (1ULL << (r*8+c));

    ostringstream ss;
    ss << hex << setw(16) << setfill('0') << hash;
    return ss.str();
}

/* -------------------------------------------------- */
/* AUDIO FINGERPRINT (Chromaprint)                   */
/* -------------------------------------------------- */

string compute_audio_fingerprint(const fs::path& path) {

    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwrContext* swrCtx = nullptr;
    ChromaprintContext* chromaCtx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;

    string result;

    av_log_set_level(AV_LOG_QUIET);

    if (avformat_open_input(&formatCtx, path.c_str(), nullptr, nullptr) != 0)
        return "";

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        avformat_close_input(&formatCtx);
        return "";
    }

    int streamIdx = -1;
    for (unsigned int i=0;i<formatCtx->nb_streams;i++)
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            streamIdx = i; break;
        }

    if (streamIdx == -1) {
        avformat_close_input(&formatCtx);
        return "";
    }

    AVCodecParameters* params = formatCtx->streams[streamIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        avformat_close_input(&formatCtx);
        return "";
    }

    codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, params);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return "";
    }

    chromaCtx = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
    chromaprint_start(chromaCtx, 44100, 1);

    swrCtx = swr_alloc();
    av_opt_set_int(swrCtx,"in_channel_layout",
                   codecCtx->ch_layout.u.mask,0);
    av_opt_set_int(swrCtx,"in_sample_rate",
                   codecCtx->sample_rate,0);
    av_opt_set_sample_fmt(swrCtx,"in_sample_fmt",
                          codecCtx->sample_fmt,0);
    av_opt_set_int(swrCtx,"out_channel_layout",
                   AV_CH_LAYOUT_MONO,0);
    av_opt_set_int(swrCtx,"out_sample_rate",44100,0);
    av_opt_set_sample_fmt(swrCtx,"out_sample_fmt",
                          AV_SAMPLE_FMT_S16,0);
    swr_init(swrCtx);

    frame = av_frame_alloc();
    packet = av_packet_alloc();

    const int max_samples = 44100 * 120;
    int processed = 0;

    while (av_read_frame(formatCtx, packet) >= 0 &&
           processed < max_samples) {

        if (packet->stream_index == streamIdx) {
            if (avcodec_send_packet(codecCtx, packet) == 0) {
                while (avcodec_receive_frame(codecCtx, frame) == 0) {

                    uint8_t* out_buf = nullptr;

                    int out_samples =
                        av_rescale_rnd(frame->nb_samples,
                                       44100,
                                       codecCtx->sample_rate,
                                       AV_ROUND_UP);

                    if (av_samples_alloc(&out_buf,nullptr,1,
                                         out_samples,
                                         AV_SAMPLE_FMT_S16,0) >= 0) {

                        int got = swr_convert(swrCtx,
                                              &out_buf,out_samples,
                                              (const uint8_t**)frame->data,
                                              frame->nb_samples);

                        if (got > 0) {
                            chromaprint_feed(
                                chromaCtx,
                                (int16_t*)out_buf,
                                got);
                            processed += got;
                        }

                        av_freep(&out_buf);
                    }

                    if (processed >= max_samples) break;
                }
            }
        }

        av_packet_unref(packet);
    }

    chromaprint_finish(chromaCtx);

    char* fp = nullptr;
    if (chromaprint_get_fingerprint(chromaCtx,&fp)) {
        result = string(fp);
        chromaprint_dealloc(fp);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&swrCtx);
    chromaprint_free(chromaCtx);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);

    return result;
}

/* -------------------------------------------------- */
/* FILE MOVE                                         */
/* -------------------------------------------------- */

bool move_file_to_folder(const string& file,
                         const string& dest) {

    try {
        fs::path src(file);
        fs::path dst = fs::path(dest) / src.filename();

        if (!fs::exists(dest))
            fs::create_directories(dest);

        try {
            fs::rename(src,dst);
        } catch (...) {
            fs::copy_file(src,dst,
                          fs::copy_options::overwrite_existing);
            fs::remove(src);
        }

        return true;
    } catch (...) {
        return false;
    }
}

/* -------------------------------------------------- */
/* MAIN                                              */
/* -------------------------------------------------- */

int main(int argc,char** argv) {

    MPI_Init(&argc,&argv);
    int rank,size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&size);

    string root;

    if (rank==0) {
        cout<<"Enter directory to scan: ";
        getline(cin,root);
    }

    uint32_t len = (rank==0)?root.size():0;
    MPI_Bcast(&len,1,MPI_UINT32_T,0,MPI_COMM_WORLD);
    if(rank!=0) root.resize(len);
    if(len>0)
        MPI_Bcast(root.data(),len,MPI_CHAR,0,MPI_COMM_WORLD);

    vector<string> local;
    int counter=0;

    for(auto& entry:
        fs::recursive_directory_iterator(
            root,
            fs::directory_options::skip_permission_denied)) {

        if(!fs::is_regular_file(entry)) continue;

        if(counter%size==rank)
            local.push_back(entry.path().string());

        counter++;
    }

    for(auto& p:local) {
        uint32_t l=p.size();
        MPI_Send(&l,1,MPI_UINT32_T,0,0,MPI_COMM_WORLD);
        MPI_Send(p.data(),l,MPI_CHAR,0,0,MPI_COMM_WORLD);
    }

    if(rank==0) {

        unordered_map<string,vector<string>> strict_ext;
        vector<string> visuals;
        vector<string> audios;

        for(int r=0;r<size;r++) {
            MPI_Status status;
            while(true) {
                int flag;
                MPI_Iprobe(r,0,MPI_COMM_WORLD,&flag,&status);
                if(!flag) break;

                uint32_t l;
                MPI_Recv(&l,1,MPI_UINT32_T,r,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
                string path(l,'\0');
                MPI_Recv(path.data(),l,MPI_CHAR,r,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

                FileType t = get_file_type(path);

                if(t==BINARY_STRICT) {
                    string ext = fs::path(path).extension().string();
                    transform(ext.begin(),ext.end(),ext.begin(),::tolower);
                    strict_ext[ext].push_back(path);
                }
                else if(t==VISUAL_FUZZY)
                    visuals.push_back(path);
                else
                    audios.push_back(path);
            }
        }

        string dest;
        cout<<"Enter destination folder: ";
        getline(cin,dest);

        /* STRICT */
        for(auto& [ext,files]:strict_ext) {

            if(files.size()<=1) continue;

            unordered_map<string,vector<string>> groups;
            for(auto& f:files)
                groups[compute_xxh3(f)].push_back(f);

            for(auto& [h,g]:groups)
                if(g.size()>1) {

                    cout << "\n--- STRICT DUPLICATES ---\n\n";
                    for(size_t i=0;i<g.size();i++)
                        cout<<i+1<<". "<<g[i]<<"\n";

                    cout<<"0. Skip\nSelect file to KEEP: ";
                    string in; getline(cin,in);
                    if(in=="0"||in.empty()) continue;
                    int keep=stoi(in);

                    for(size_t i=0;i<g.size();i++)
                        if((int)i!=keep-1)
                            move_file_to_folder(g[i],dest);
                }
        }

        /* VISUAL */
        vector<pair<string,uint64_t>> vhash;

        for(auto& f:visuals) {
            string h=compute_visual_hash(f);
            if(!h.empty())
                vhash.emplace_back(f,
                    stoull(h,nullptr,16));
        }

        vector<bool> visited(vhash.size(),false);

        for(size_t i=0;i<vhash.size();i++) {

            if(visited[i]) continue;
            vector<string> group{vhash[i].first};

            for(size_t j=i+1;j<vhash.size();j++) {
                if(visited[j]) continue;
                uint64_t x=vhash[i].second ^ vhash[j].second;
                if(__builtin_popcountll(x)<=3) {
                    group.push_back(vhash[j].first);
                    visited[j]=true;
                }
            }

            if(group.size()>1) {
                cout << "\n--- VISUAL DUPLICATES ---\n\n";
                for(size_t k=0;k<group.size();k++)
                    cout<<k+1<<". "<<group[k]<<"\n";

                cout<<"0. Skip\nSelect file to KEEP: ";
                string in; getline(cin,in);
                if(in=="0"||in.empty()) continue;
                int keep=stoi(in);

                for(size_t k=0;k<group.size();k++)
                    if((int)k!=keep-1)
                        move_file_to_folder(group[k],dest);
            }
        }

        /* AUDIO */
        unordered_map<string,vector<string>> agroups;
        for(auto& f:audios)
            agroups[compute_audio_fingerprint(f)].push_back(f);

        for(auto& [h,g]:agroups)
            if(g.size()>1) {

                cout << "\n--- AUDIO DUPLICATES ---\n\n";
                for(size_t i=0;i<g.size();i++)
                    cout<<i+1<<". "<<g[i]<<"\n";

                cout<<"0. Skip\nSelect file to KEEP: ";
                string in; getline(cin,in);
                if(in=="0"||in.empty()) continue;
                int keep=stoi(in);

                for(size_t i=0;i<g.size();i++)
                    if((int)i!=keep-1)
                        move_file_to_folder(g[i],dest);
            }

        cout<<"\nDone.\n";
    }

    MPI_Finalize();
    return 0;
}