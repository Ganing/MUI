/****************************************************************************
 * 标题: TEST-Filament-Player - 音视频同步播放器
 * 功能: 使用 FFmpeg 解码 MP4 音视频，miniaudio 播放音频，Filament 渲染视频
 *       以音频时钟为主，实现音视频同步播放
 * 测试: E:/MX/Data/tvb1.mp4（有字幕，可验证同步）
 * 参考: MX/oav.h, TEST-Filament-Video.cpp
 * 依赖: ocore.h, oapp.h, oui.h, miniaudio, FFmpeg静态库, Filament
 ****************************************************************************/

// json.hpp 必须在 ocore.h 之前，避免与 Filament 的 assert_invariant/UTILS_VERY_LIKELY 宏冲突
#include "json.hpp"
using json = nlohmann::json;

#include "ocore.h"
#include "oapp.h"
#include "oui.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glad/glad.h>

#include <thread>
#include <chrono>

#include <filament/Engine.h>
#include <filament/Camera.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/VertexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/Viewport.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>

#include <utils/EntityManager.h>
#include <filamat/MaterialBuilder.h>

#include "shadertoy_effects.h"

struct FXEffect { const char* name; const char* code; };
static const FXEffect g_fxEffects[] = {
    {"DynamicRect",   FX::DynamicRect},
    {"VanGoghSunset", FX::VanGoghSunset},
    {"HeartfeltRain", FX::HeartfeltRain},
};
static const int g_fxEffectCount = sizeof(g_fxEffects) / sizeof(g_fxEffects[0]);

#include <windows.h>
#include <wingdi.h>
#include <mmsystem.h>
#include <backend/platforms/PlatformWGL.h>

#include <cmath>
#include <cstring>
#include <chrono>
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <ctime>

// FFmpeg headers
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// miniaudio (single header, define implementation here)
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#define HOTKEY_TOGGLE 1
#define HOTKEY_VK VK_F5

using namespace filament;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")

/*==================== 播放模式枚举 ====================*/
enum class PlayMode {
    SingleLoop,     // 单曲循环
    ListLoop,       // 列表循环
    Sequential,     // 顺序播放
    Random          // 随机播放
};

static const char* getPlayModeText(PlayMode mode) {
    switch (mode) {
        case PlayMode::SingleLoop:  return "单曲循环";
        case PlayMode::ListLoop:    return "列表循环";
        case PlayMode::Sequential:  return "顺序播放";
        case PlayMode::Random:      return "随机播放";
        default:                    return "未知";
    }
}

/*==================== 歌单/歌曲数据结构 ====================*/
struct SongInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string filePath;
    float duration = 0.0f;
    std::string addDate;
};

struct PlaylistInfo {
    std::string name;
    std::string createDate;
    std::string type = "builtin";    // "builtin" | "localDir"
    std::vector<SongInfo> songs;
    std::string directoryPath;
    bool autoScan = true;
    int scanInterval = 3600;
};

struct LrcLine {
    double timestamp;   // seconds
    std::string text;
};

#define FILAMENT_LIB_PATH "e:/MX/3rd/lib/filament/mt/"
#pragma comment(lib, FILAMENT_LIB_PATH "filament.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "backend.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluegl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filabridge.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filaflat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filamat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "utils.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "ibl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "geometry.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "shaders.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "stb.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "zstd.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluevk.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "smol-v.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "image.lib")

#pragma comment(lib, "e:/MX/3rd/lib/libthorvg_mt.lib")

// FFmpeg static libs
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libavformat.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libavcodec.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libavutil.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libswscale.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libswresample.a")

/*==================== Config constants ====================*/
static constexpr const char* kScanDirs[]    = {"E:/xmusic","D:\\ffbin", "E:\\movie\\Movies\\TikTok"};
static constexpr const char* kDefaultFlac   = "E:\\movie\\music\\goodbye happiness.flac";
static constexpr const char* kDefaultMp4    = "E:\\movie\\Movies\\TikTok\\looklook9766_2025-01-23-19-33-40_1737632020200.mp4";
static constexpr const char* kFontPath      = "e:/MX/Data/siyuan.ttf";
static constexpr const char* kFfmpegPath    = "D:\\ffbin\\ffmpeg.exe";

/*==================== 无 Swap 平台 ====================*/
class NoSwapWGLPlatform : public backend::PlatformWGL {
public:
    void commit(filament::backend::Platform::SwapChain* swapChain) noexcept override {
        (void)swapChain;
    }
};

/*==================== 全屏Quad ====================*/
static constexpr float2 kQuadPositions[6] = {
    {-1,-1}, { 1,-1}, { 1, 1},
    {-1,-1}, { 1, 1}, {-1, 1}
};
static constexpr float2 kQuadUVs[6] = {
    {0,0}, {1,0}, {1,1},
    {0,0}, {1,1}, {0,1}
};

static Entity createFullscreenQuad(Engine* engine, Scene* scene, MaterialInstance* mi) {
    using AT = VertexBuffer::AttributeType;
    VertexBuffer* vb = VertexBuffer::Builder()
        .vertexCount(6)
        .bufferCount(2)
        .attribute(VertexAttribute::POSITION, 0, AT::FLOAT2, 0, sizeof(float2))
        .attribute(VertexAttribute::UV0,       1, AT::FLOAT2, 0, sizeof(float2))
        .build(*engine);
    vb->setBufferAt(*engine, 0, {kQuadPositions, sizeof(kQuadPositions)});
    vb->setBufferAt(*engine, 1, {kQuadUVs,      sizeof(kQuadUVs)});

    Entity e = EntityManager::get().create();
    RenderableManager::Builder(1)
        .boundingBox({{-1,-1,-0.01f},{1,1,0.01f}})
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb)
        .material(0, mi)
        .culling(false)
        .castShadows(false).receiveShadows(false)
        .build(*engine, e);
    scene->addEntity(e);
    return e;
}

/*==================== Audio constants ====================*/
static constexpr int    kAudioSampleRate  = 48000;
static constexpr int    kAudioChannels    = 2;
static constexpr size_t kAudioBufFifth    = kAudioSampleRate * kAudioChannels / 5;
static constexpr size_t kAudioBufQuarter  = kAudioSampleRate * kAudioChannels / 4;
static constexpr size_t kAudioBufHalf     = kAudioSampleRate * kAudioChannels / 2;
static constexpr size_t kAudioBufEofThresh = kAudioSampleRate * kAudioChannels / 10;
static constexpr size_t kAudioBufCapacity = kAudioSampleRate * kAudioChannels * 2;

/*==================== 线程安全音频队列 ====================*/
struct AudioQueue {
    std::vector<float> buf;
    size_t wp = 0; // write position
    size_t rp = 0; // read position
    mutable std::mutex mtx;

    explicit AudioQueue(size_t capacityFrames = kAudioBufCapacity) { // 2 sec stereo @ 48k
        buf.resize(capacityFrames);
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        if (wp >= rp) return wp - rp;
        return buf.size() - rp + wp;
    }

    size_t space() const {
        std::lock_guard<std::mutex> lock(mtx);
        return buf.size() - 1 - ((wp >= rp) ? (wp - rp) : (buf.size() - rp + wp));
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        wp = rp = 0;
    }

    // write samples (interleaved float)
    size_t write(const float* data, size_t count) {
        std::lock_guard<std::mutex> lock(mtx);
        size_t avail = buf.size() - 1 - ((wp >= rp) ? (wp - rp) : (buf.size() - rp + wp));
        size_t toWrite = (count < avail) ? count : avail;
        for (size_t i = 0; i < toWrite; ++i) {
            buf[wp] = data[i];
            wp = (wp + 1) % buf.size();
        }
        return toWrite;
    }

    // read samples into out buffer
    size_t read(float* out, size_t count) {
        std::lock_guard<std::mutex> lock(mtx);
        size_t avail = (wp >= rp) ? (wp - rp) : (buf.size() - rp + wp);
        size_t toRead = (count < avail) ? count : avail;
        for (size_t i = 0; i < toRead; ++i) {
            out[i] = buf[rp];
            rp = (rp + 1) % buf.size();
        }
        return toRead;
    }
};

/*==================== FFmpeg 音视频解码器 ====================*/
class AVDecoder {
    // Format
    AVFormatContext* fmtCtx = nullptr;

    // Video
    int videoStreamIdx = -1;
    AVCodecContext* vCodecCtx = nullptr;
    AVFrame* vFrame = nullptr;
    AVFrame* rgbFrame = nullptr;
    SwsContext* swsCtx = nullptr;
    uint8_t* rgbBuffer = nullptr;
    int rgbBufferSize = 0;
    double vTimeBase = 0.0;
    double vFps = 30.0;

    // Audio
    int audioStreamIdx = -1;
    AVCodecContext* aCodecCtx = nullptr;
    AVFrame* aFrame = nullptr;
    SwrContext* swrCtx = nullptr;
    double aTimeBase = 0.0;
    int aSampleRate = 48000;
    int aChannels = 2;

    // Frame buffer
    struct DecodedFrame {
        std::vector<uint8_t> rgba;
        double pts = 0.0;
    };
    std::vector<DecodedFrame> frameBuffer;
    DecodedFrame currentFrame;
    AVPacket* pendingVPacket = nullptr;

    // Common
    double durationSec = 0.0;
    int videoW = 0;
    int videoH = 0;
    bool eof = false;
    bool firstFrameLogged = false;

    // Cover art (from attached_pic or embedded image)
    std::vector<uint8_t> coverArtRGBA;
    int coverArtW = 0;
    int coverArtH = 0;
    bool hasCoverArt = false;

public:
    bool open(const char* path) {
        if (avformat_open_input(&fmtCtx, path, nullptr, nullptr) < 0) {
            OX_LOG("[AV] avformat_open_input failed: %s\n", path);
            return false;
        }
        if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
            OX_LOG("[AV] avformat_find_stream_info failed\n");
            close();
            return false;
        }

        // Seek back to beginning: av_find_stream_info may consume packets
        av_seek_frame(fmtCtx, -1, 0, AVSEEK_FLAG_BACKWARD);

        // ---- 提取嵌入封面图（attached_pic） ----
        for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
            if (fmtCtx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket* pkt = &fmtCtx->streams[i]->attached_pic;
                if (pkt->size > 0) {
                    int w, h, comp;
                    uint8_t* img = stbi_load_from_memory(pkt->data, (int)pkt->size, &w, &h, &comp, 4);
                    if (img) {
                        coverArtRGBA.assign(img, img + w * h * 4);
                        coverArtW = w;
                        coverArtH = h;
                        hasCoverArt = true;
                        stbi_image_free(img);
                        OX_LOG("[AV] Extracted cover art %dx%d from stream %u (codec=%d)\n",
                               w, h, i, fmtCtx->streams[i]->codecpar->codec_id);
                    }
                }
            }
        }

        // Find video stream
        for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
            if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIdx < 0) {
                videoStreamIdx = (int)i;
            } else if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIdx < 0) {
                audioStreamIdx = (int)i;
            }
        }

        if (videoStreamIdx < 0 && audioStreamIdx < 0) {
            OX_LOG("[AV] No video or audio stream found\n");
            close();
            return false;
        }

        // Init video codec
        if (videoStreamIdx >= 0) {
            AVCodecParameters* vPar = fmtCtx->streams[videoStreamIdx]->codecpar;
            const AVCodec* vCodec = avcodec_find_decoder(vPar->codec_id);
            // Skip embedded images / cover art (MJPEG, PNG, BMP, etc.)
            bool isImageCodec = (vPar->codec_id == AV_CODEC_ID_MJPEG ||
                                 vPar->codec_id == AV_CODEC_ID_PNG ||
                                 vPar->codec_id == AV_CODEC_ID_BMP ||
                                 vPar->codec_id == AV_CODEC_ID_PAM ||
                                 vPar->codec_id == AV_CODEC_ID_PBM ||
                                 vPar->codec_id == AV_CODEC_ID_PGM ||
                                 vPar->codec_id == AV_CODEC_ID_PPM ||
                                 vPar->codec_id == AV_CODEC_ID_PGMYUV ||
                                 vPar->codec_id == AV_CODEC_ID_TIFF ||
                                 vPar->codec_id == AV_CODEC_ID_JPEGLS ||
                                 vPar->codec_id == AV_CODEC_ID_JPEG2000 ||
                                 vPar->codec_id == AV_CODEC_ID_WEBP ||
                                 vPar->codec_id == AV_CODEC_ID_WRAPPED_AVFRAME);
            if (isImageCodec) {
                OX_LOG("[AV] Skipping embedded image/cover-art stream (codec=%d)\n", vPar->codec_id);
                videoStreamIdx = -1;
                videoW = videoH = 0;
                goto audio_init; // skip video init entirely
            }
            if (vCodec) {
                vCodecCtx = avcodec_alloc_context3(vCodec);
                if (vCodecCtx) {
                    avcodec_parameters_to_context(vCodecCtx, vPar);
                    vCodecCtx->pkt_timebase = fmtCtx->streams[videoStreamIdx]->time_base;
                    AVDictionary* opts = nullptr;
                    av_dict_set(&opts, "threads", "auto", 0);
                    avcodec_open2(vCodecCtx, vCodec, &opts);
                    av_dict_free(&opts);
                    vTimeBase = av_q2d(fmtCtx->streams[videoStreamIdx]->time_base);
                    AVRational fr = fmtCtx->streams[videoStreamIdx]->avg_frame_rate;
                    if (fr.num == 0) fr = fmtCtx->streams[videoStreamIdx]->r_frame_rate;
                    vFps = av_q2d(fr);
                    if (vFps <= 0) vFps = 30.0;
                    videoW = vCodecCtx->width;
                    videoH = vCodecCtx->height;

                    vFrame = av_frame_alloc();
                    rgbFrame = av_frame_alloc();
                    rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, videoW, videoH, 1);
                    rgbBuffer = (uint8_t*)av_malloc(rgbBufferSize);
                    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer,
                                         AV_PIX_FMT_RGBA, videoW, videoH, 1);
                    swsCtx = sws_getContext(videoW, videoH, vCodecCtx->pix_fmt,
                                            videoW, videoH, AV_PIX_FMT_RGBA,
                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                }
            }
        }

audio_init:
        // Init audio codec
        if (audioStreamIdx >= 0) {
            AVCodecParameters* aPar = fmtCtx->streams[audioStreamIdx]->codecpar;
            const AVCodec* aCodec = avcodec_find_decoder(aPar->codec_id);
            if (aCodec) {
                aCodecCtx = avcodec_alloc_context3(aCodec);
                if (aCodecCtx) {
                    avcodec_parameters_to_context(aCodecCtx, aPar);
                    aCodecCtx->thread_count = 1;
                    avcodec_open2(aCodecCtx, aCodec, nullptr);
                    aTimeBase = av_q2d(fmtCtx->streams[audioStreamIdx]->time_base);
                    aSampleRate = aCodecCtx->sample_rate;
                    aChannels = aCodecCtx->ch_layout.nb_channels;
                    aFrame = av_frame_alloc();

                    AVChannelLayout outLayout;
                    av_channel_layout_default(&outLayout, 2);
                    swr_alloc_set_opts2(&swrCtx,
                        &outLayout, AV_SAMPLE_FMT_FLT, 48000,
                        &aCodecCtx->ch_layout, aCodecCtx->sample_fmt, aCodecCtx->sample_rate,
                        0, nullptr);
                    if (swrCtx) {
                        int swrRet = swr_init(swrCtx);
                        if (swrRet < 0) {
                            OX_LOG("[AV] swr_init failed: %d\n", swrRet);
                            swr_free(&swrCtx);
                        }
                    }
                    av_channel_layout_uninit(&outLayout);
                }
            }
        }

        durationSec = fmtCtx->duration > 0 ? (double)fmtCtx->duration / AV_TIME_BASE : 0.0;
        eof = false;
        firstFrameLogged = false;

        int rotation = 0;
        if (videoStreamIdx >= 0) {
            AVStream* vStream = fmtCtx->streams[videoStreamIdx];
            AVDictionaryEntry* rotateEntry = av_dict_get(vStream->metadata, "rotate", nullptr, 0);
            if (rotateEntry) rotation = atoi(rotateEntry->value);
            const int32_t* displayMatrix = nullptr;
            size_t dmSize = 0;
            // C4996: av_stream_get_side_data deprecated in FFmpeg 7.x, use av_packet_side_data_get
            const AVPacketSideData* sd = av_packet_side_data_get(
                vStream->codecpar->coded_side_data,
                vStream->codecpar->nb_coded_side_data,
                AV_PKT_DATA_DISPLAYMATRIX);
            if (sd) {
                displayMatrix = (const int32_t*)sd->data;
                dmSize = sd->size;
            }
            if (displayMatrix && dmSize >= 9 * sizeof(int32_t)) {
                double theta = -atan2((double)displayMatrix[1], (double)displayMatrix[0]) * 180.0 / 3.14159265358979323846;
                rotation = (int)std::lround(theta);
                while (rotation < 0) rotation += 360;
                while (rotation >= 360) rotation -= 360;
            }
        }
        OX_LOG("[AV] Opened %s (V:%dx%d@%.2ffps A:%dHz/%dch, %.2f sec rot=%d) vIdx=%d aIdx=%d\n",
               path, videoW, videoH, vFps, aSampleRate, aChannels, durationSec, rotation, videoStreamIdx, audioStreamIdx);
        return true;
    }

    void close() {
        frameBuffer.clear();
        if (pendingVPacket) { av_packet_free(&pendingVPacket); pendingVPacket = nullptr; }
        if (swsCtx) { sws_freeContext(swsCtx); swsCtx = nullptr; }
        if (rgbBuffer) { av_freep(&rgbBuffer); rgbBuffer = nullptr; }
        if (rgbFrame) { av_frame_free(&rgbFrame); }
        if (vFrame) { av_frame_free(&vFrame); }
        if (vCodecCtx) { avcodec_free_context(&vCodecCtx); }

        if (swrCtx) { swr_free(&swrCtx); }
        if (aFrame) { av_frame_free(&aFrame); }
        if (aCodecCtx) { avcodec_free_context(&aCodecCtx); }

        if (fmtCtx) { avformat_close_input(&fmtCtx); }
        videoStreamIdx = audioStreamIdx = -1;
        hasCoverArt = false;
        coverArtRGBA.clear();
        coverArtW = coverArtH = 0;
    }

    // Drain all decoded video frames from the codec into frameBuffer
    void drainVideoFrames() {
        if (!vCodecCtx) return;
        while (true) {
            int ret = avcodec_receive_frame(vCodecCtx, vFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;
            if (!firstFrameLogged) {
                firstFrameLogged = true;
                AVRational sar = vFrame->sample_aspect_ratio;
                OX_LOG("[AV] First decoded frame: %dx%d fmt=%d(ctx=%d) ls=%d/%d/%d SAR=%d/%d interlace=%d\n",
                       vFrame->width, vFrame->height, vFrame->format, vCodecCtx->pix_fmt,
                       vFrame->linesize[0], vFrame->linesize[1], vFrame->linesize[2],
                       sar.num, sar.den, vFrame->flags & AV_FRAME_FLAG_INTERLACED ? 1 : 0);
            }
            // 如果解码器输出格式与 swsCtx 创建时使用的格式不一致，重新创建 swsCtx
            if (vFrame->format != vCodecCtx->pix_fmt) {
                if (swsCtx) { sws_freeContext(swsCtx); swsCtx = nullptr; }
                swsCtx = sws_getContext(videoW, videoH, (AVPixelFormat)vFrame->format,
                                        videoW, videoH, AV_PIX_FMT_RGBA,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!swsCtx) {
                    OX_LOG("[AV] sws_getContext failed for fmt=%d\n", vFrame->format);
                    continue;
                }
            }
            DecodedFrame df;
            df.pts = (vFrame->pts != AV_NOPTS_VALUE) ? (vFrame->pts * vTimeBase) : 0.0;
            df.rgba.resize((size_t)videoW * videoH * 4);
            sws_scale(swsCtx, vFrame->data, vFrame->linesize, 0, videoH,
                      rgbFrame->data, rgbFrame->linesize);
            for (int y = 0; y < videoH; ++y) {
                memcpy(df.rgba.data() + y * videoW * 4,
                       rgbBuffer + y * rgbFrame->linesize[0],
                       (size_t)videoW * 4);
            }
            frameBuffer.push_back(std::move(df));
        }
    }

    bool hasVideo() const { return videoStreamIdx >= 0; }
    bool hasAudio() const { return audioStreamIdx >= 0; }
    int getWidth() const { return videoW; }
    int getHeight() const { return videoH; }
    bool hasCover() const { return hasCoverArt; }
    int getCoverWidth() const { return coverArtW; }
    int getCoverHeight() const { return coverArtH; }
    const uint8_t* getCoverData() const { return coverArtRGBA.empty() ? nullptr : coverArtRGBA.data(); }
    // 提取内嵌歌词（FLAC Vorbis Comment / MP3 ID3v2 USLT / MP4 ©lyr）
    std::string getEmbeddedLyrics() const {
        if (!fmtCtx) return "";
        // 尝试多种常见 metadata key
        const char* keys[] = {
            "LYRICS",       // FLAC/OGG Vorbis Comment
            "lyrics",       // 小写变体
            "UNSYNCEDLYRICS", "unsyncedlyrics",
            "lyrics-eng",   // ID3v2 USLT with language code
            "lyrics-XXX",   // fallback language
            "©lyr",         // MP4/M4A
            "lyr",          // alternative MP4
        };
        for (const char* key : keys) {
            AVDictionaryEntry* entry = av_dict_get(fmtCtx->metadata, key, nullptr, AV_DICT_IGNORE_SUFFIX);
            if (entry && entry->value && entry->value[0]) {
                OX_LOG("[AV] Found embedded lyrics via key '%s'\n", key);
                return std::string(entry->value);
            }
        }
        // Also check stream-level metadata
        for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
            for (const char* key : keys) {
                AVDictionaryEntry* entry = av_dict_get(fmtCtx->streams[i]->metadata, key, nullptr, AV_DICT_IGNORE_SUFFIX);
                if (entry && entry->value && entry->value[0]) {
                    OX_LOG("[AV] Found embedded lyrics via stream %u key '%s'\n", i, key);
                    return std::string(entry->value);
                }
            }
        }
        return "";
    }
    int getSampleRate() const { return aSampleRate; }
    const uint8_t* getCurrentFramePtr() const { return currentFrame.rgba.empty() ? nullptr : currentFrame.rgba.data(); }
    int getChannels() const { return aChannels; }
    double getDuration() const { return durationSec; }
    double getFps() const { return vFps; }
    bool isEof() const { return eof; }

    // Internal: decode and enqueue one audio packet (takes ownership, will unref)
    void decodeAudioPacket(AVPacket* packet, AudioQueue& q) {
        if (!aCodecCtx || !swrCtx) { av_packet_unref(packet); return; }
        int totalWritten = 0;
        int ret = avcodec_send_packet(aCodecCtx, packet);
        if (ret == AVERROR(EAGAIN)) {
            // Decoder full, drain first
            while (true) {
                ret = avcodec_receive_frame(aCodecCtx, aFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) { OX_LOG("[AV] audio receive_frame error: %d\n", ret); break; }
                int outSamples = swr_get_out_samples(swrCtx, aFrame->nb_samples);
                if (outSamples <= 0) continue;
                std::vector<float> tmp(outSamples * 2);
                uint8_t* outBuf[1] = { (uint8_t*)tmp.data() };
                int converted = swr_convert(swrCtx, outBuf, outSamples,
                                            (const uint8_t**)aFrame->data, aFrame->nb_samples);
                if (converted < 0) {
                    OX_LOG("[AV] swr_convert failed: %d (fmt=%d sr=%d ch=%d nb=%d)\n",
                           converted, aFrame->format, aFrame->sample_rate,
                           aFrame->ch_layout.nb_channels, aFrame->nb_samples);
                } else if (converted > 0) {
                    q.write(tmp.data(), converted * 2);
                    totalWritten += converted;
                } else if (converted == 0) {
                    static int zeroCount = 0;
                    if (++zeroCount <= 5) {
                        OX_LOG("[AV] swr_convert returned 0 (fmt=%d sr=%d ch=%d nb=%d)\n",
                               aFrame->format, aFrame->sample_rate,
                               aFrame->ch_layout.nb_channels, aFrame->nb_samples);
                    }
                }
            }
            ret = avcodec_send_packet(aCodecCtx, packet);
        }
        av_packet_unref(packet);
        if (ret < 0) { OX_LOG("[AV] audio send_packet error: %d\n", ret); return; }
        while (true) {
            ret = avcodec_receive_frame(aCodecCtx, aFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                static int recEagainCount = 0;
                if (++recEagainCount <= 5) {
                    OX_LOG("[AV] audio receive_frame ret=%d (EAGAIN=%d EOF=%d)\n", ret, AVERROR(EAGAIN), AVERROR_EOF);
                }
                break;
            }
            if (ret < 0) { OX_LOG("[AV] audio receive_frame error: %d\n", ret); break; }
            int outSamples = swr_get_out_samples(swrCtx, aFrame->nb_samples);
            if (outSamples <= 0) {
                static int outSamplesCount = 0;
                if (++outSamplesCount <= 5) {
                    OX_LOG("[AV] swr_get_out_samples returned %d (nb_samples=%d)\n", outSamples, aFrame->nb_samples);
                }
                continue;
            }
            std::vector<float> tmp(outSamples * 2);
            uint8_t* outBuf[1] = { (uint8_t*)tmp.data() };
            int converted = swr_convert(swrCtx, outBuf, outSamples,
                                        (const uint8_t**)aFrame->data, aFrame->nb_samples);
            if (converted < 0) {
                OX_LOG("[AV] swr_convert failed: %d (fmt=%d sr=%d ch=%d nb=%d)\n",
                       converted, aFrame->format, aFrame->sample_rate,
                       aFrame->ch_layout.nb_channels, aFrame->nb_samples);
            } else if (converted > 0) {
                q.write(tmp.data(), converted * 2);
                totalWritten += converted;
            } else if (converted == 0) {
                static int zeroCount2 = 0;
                if (++zeroCount2 <= 5) {
                    OX_LOG("[AV] swr_convert returned 0 (fmt=%d sr=%d ch=%d nb=%d)\n",
                           aFrame->format, aFrame->sample_rate,
                           aFrame->ch_layout.nb_channels, aFrame->nb_samples);
                }
            }
        }
        if (totalWritten == 0) {
            static int emptyCount = 0;
            if (++emptyCount <= 5) {
                OX_LOG("[AV] decodeAudioPacket wrote 0 samples\n");
            }
        }
    }

    // Decode next video frame, filling audio queue along the way.
    // Follows ffplay.c pattern: drain decoded frames first, then send next packet.
    const uint8_t* decodeNextFrame(double& outPts, AudioQueue& q) {
        if (!fmtCtx) return nullptr;

        if (!frameBuffer.empty()) {
            currentFrame = std::move(frameBuffer.front());
            frameBuffer.erase(frameBuffer.begin());
            outPts = currentFrame.pts;
            return currentFrame.rgba.data();
        }

        AVPacket* packet = av_packet_alloc();
        int audioPacketsInThisDecode = 0;
        const int MAX_AUDIO_PER_DECODE = 10; // limit audio-only packet processing
        while (true) {
            // 1. Drain all decoded video frames first (ffplay.c style)
            drainVideoFrames();
            if (!frameBuffer.empty()) {
                currentFrame = std::move(frameBuffer.front());
                frameBuffer.erase(frameBuffer.begin());
                outPts = currentFrame.pts;
                av_packet_free(&packet);
                return currentFrame.rgba.data();
            }

            // 2. Use pending packet if send_packet previously returned EAGAIN
            if (pendingVPacket && pendingVPacket->data) {
                av_packet_unref(packet);
                av_packet_ref(packet, pendingVPacket);
                av_packet_unref(pendingVPacket);
            } else {
                // Read next packet from file
                int ret = av_read_frame(fmtCtx, packet);
                if (ret < 0) {
                    av_packet_free(&packet);
                    eof = true;
                    return nullptr;
                }
            }

            if (packet->stream_index == videoStreamIdx) {
                int ret = avcodec_send_packet(vCodecCtx, packet);
                if (ret == AVERROR(EAGAIN)) {
                    // Save packet for retry (do NOT unref)
                    if (!pendingVPacket) pendingVPacket = av_packet_alloc();
                    av_packet_ref(pendingVPacket, packet);
                    av_packet_unref(packet);
                    drainVideoFrames();
                    continue;
                }
                av_packet_unref(packet);
                if (ret < 0) {
                    continue; // decoder error, try next packet
                }
                // Packet sent successfully, loop back to receive decoded frames
            } else if (packet->stream_index == audioStreamIdx) {
                audioPacketsInThisDecode++;
                if (audioPacketsInThisDecode >= MAX_AUDIO_PER_DECODE && !frameBuffer.empty()) {
                    // Don't consume all audio packets in one go; return what we have
                    if (frameBuffer.empty()) {
                        // Need at least one video frame to return, but if none, abort audio processing
                        av_packet_unref(packet);
                        av_packet_free(&packet);
                        return nullptr;
                    }
                    // Return current buffered video frame (already in frameBuffer from drainVideoFrames)
                    currentFrame = std::move(frameBuffer.front());
                    frameBuffer.erase(frameBuffer.begin());
                    outPts = currentFrame.pts;
                    av_packet_unref(packet);
                    av_packet_free(&packet);
                    return currentFrame.rgba.data();
                }
                size_t qSizeBefore = q.size();
                decodeAudioPacket(packet, q);
                static int gfPktCount = 0;
                if (++gfPktCount <= 10) {
                    OX_LOG("[AV] getFirstFrame decodeAudioPacket: qBefore=%zu qAfter=%zu\n", qSizeBefore, q.size());
                }
            } else {
                av_packet_unref(packet);
            }
        }
    }

    // Pre-fill audio queue without producing video frames
    void prefillAudioQueue(AudioQueue& q, size_t targetSamples) {
        if (!fmtCtx || !aCodecCtx) return;
        int packetsRead = 0;
        size_t qBefore = q.size();
        while (q.size() < targetSamples && !eof) {
            AVPacket* packet = av_packet_alloc();
            int ret = av_read_frame(fmtCtx, packet);
            static int readLogCount = 0;
            if (++readLogCount <= 3) {
                OX_LOG("[AV] prefillAudioQueue av_read_frame ret=%d packetsRead=%d\n", ret, packetsRead);
            }
            if (ret < 0) {
                static int prefillErrCount = 0;
                if (++prefillErrCount <= 3) {
                    char errbuf[256];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    OX_LOG("[AV] prefillAudioQueue EOF: %d (%s) totalRead=%d qSize=%zu\n", ret, errbuf, packetsRead, q.size());
                }
                eof = true; av_packet_free(&packet); break;
            }
            packetsRead++;
            if (packet->stream_index == audioStreamIdx) {
                decodeAudioPacket(packet, q);
            } else {
                static int skipCallCount = 0;
                if (++skipCallCount <= 3) {
                    OX_LOG("[AV] prefillAudioQueue skip pkt streamIdx=%d audioIdx=%d videoIdx=%d\n",
                           packet->stream_index, audioStreamIdx, videoStreamIdx);
                }
                av_packet_unref(packet);

            }
            av_packet_free(&packet);
        }
        // Drain any remaining decoded frames after EOF (some decoders like FLAC buffer internally)
        if (eof && aCodecCtx) {
            while (true) {
                AVFrame* frame = av_frame_alloc();
                int ret = avcodec_receive_frame(aCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) { av_frame_free(&frame); break; }
                if (ret < 0) { av_frame_free(&frame); break; }
                if (swrCtx) {
                    int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
                    if (outSamples > 0) {
                        std::vector<float> tmp(outSamples * 2);
                        uint8_t* outBuf[1] = { (uint8_t*)tmp.data() };
                        int converted = swr_convert(swrCtx, outBuf, outSamples,
                                                    (const uint8_t**)frame->data, frame->nb_samples);
                        if (converted > 0) {
                            q.write(tmp.data(), converted * 2);
                        }
                    }
                }
                av_frame_free(&frame);
            }
        }
        size_t filled = q.size() - qBefore;
        if (filled < targetSamples / 2 && packetsRead == 0 && q.size() < targetSamples) {
            OX_LOG("[AV] prefillAudioQueue warning: filled=%zu target=%zu packets=%d qsize=%zu\n", filled, targetSamples, packetsRead, q.size());
        }
    }

    bool seek(double seconds) {
        if (!fmtCtx) return false;
        // av_seek_frame with stream_index=-1 uses AV_TIME_BASE (microseconds)
        int64_t ts_avtb = (int64_t)(seconds * AV_TIME_BASE);
        int ret = av_seek_frame(fmtCtx, -1, ts_avtb, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            OX_LOG("[AV] seek failed: %d (%s)\n", ret, errbuf);
            return false;
        }
        if (vCodecCtx) avcodec_flush_buffers(vCodecCtx);
        if (aCodecCtx) avcodec_flush_buffers(aCodecCtx);
        if (swrCtx) {
            int swrRet = swr_init(swrCtx);
            if (swrRet < 0) {
                char errbuf[256];
                av_strerror(swrRet, errbuf, sizeof(errbuf));
                OX_LOG("[AV] swr_init failed in seek: %d (%s), recreating swrCtx\n", swrRet, errbuf);
                swr_free(&swrCtx);
                AVChannelLayout outLayout;
                av_channel_layout_default(&outLayout, 2);
                swr_alloc_set_opts2(&swrCtx,
                    &outLayout, AV_SAMPLE_FMT_FLT, 48000,
                    &aCodecCtx->ch_layout, aCodecCtx->sample_fmt, aCodecCtx->sample_rate,
                    0, nullptr);
                av_channel_layout_uninit(&outLayout);
                if (swrCtx) {
                    int swrRet2 = swr_init(swrCtx);
                    if (swrRet2 < 0) {
                        char errbuf2[256];
                        av_strerror(swrRet2, errbuf2, sizeof(errbuf2));
                        OX_LOG("[AV] swr_init failed again: %d (%s)\n", swrRet2, errbuf2);
                        swr_free(&swrCtx);
                    }
                }
            }
        }
        frameBuffer.clear();
        if (pendingVPacket) { av_packet_unref(pendingVPacket); pendingVPacket = nullptr; }
        eof = false;
        return true;
    }

    // --- Seek helpers ---
    double seekedPts = 0.0;
    std::vector<uint8_t> seekedFrame;
    bool hasSeekedFrame_ = false;

    double getSeekedPts() const { return seekedPts; }
    bool hasSeekedFrame() const { return hasSeekedFrame_; }
    const uint8_t* getSeekedFramePtr() const { return hasSeekedFrame_ ? seekedFrame.data() : nullptr; }

    // seek: av_seek_frame → raw drain find frame → re-seek → fill audio from frame PTS
     bool seekToTarget(AudioQueue& q, double targetSeconds) {
        hasSeekedFrame_ = false;
        seekedPts = targetSeconds;
        frameBuffer.clear();
        if (pendingVPacket) { av_packet_unref(pendingVPacket); pendingVPacket = nullptr; }
        if (!fmtCtx) return false;

        if (targetSeconds < 0) targetSeconds = 0;
        if (durationSec > 0 && targetSeconds > durationSec) targetSeconds = durationSec;

        // 1. FFmpeg seek to keyframe before target
        seek(targetSeconds);
        q.clear();

        // 2. Audio-only: fill and done
        if (!hasVideo() && hasAudio()) {
            prefillAudioQueue(q, kAudioBufHalf);
            return q.size() > 0;
        }

        // 3. Video: drain raw packets (skip audio) — find first video frame PTS >= target
        double foundPts = targetSeconds;
        const uint8_t* foundFrame = nullptr;
        {
            AVPacket* pkt = av_packet_alloc();
            const int MAX_PKTS = 2000;
            bool frameFound = false;
            for (int i = 0; i < MAX_PKTS && !frameFound; i++) {
                int ret = av_read_frame(fmtCtx, pkt);
                if (ret < 0) break;
                if (pkt->stream_index == videoStreamIdx) {
                    ret = avcodec_send_packet(vCodecCtx, pkt);
                    av_packet_unref(pkt);
                    if (ret >= 0) {
                        while (true) {
                            ret = avcodec_receive_frame(vCodecCtx, vFrame);
                            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                            if (ret < 0) continue;
                            double pts = (vFrame->pts != AV_NOPTS_VALUE) ? (vFrame->pts * vTimeBase) : 0.0;
                            if (pts >= targetSeconds) {
                                if (vFrame->format != vCodecCtx->pix_fmt) {
                                    if (swsCtx) { sws_freeContext(swsCtx); swsCtx = nullptr; }
                                    swsCtx = sws_getContext(videoW, videoH, (AVPixelFormat)vFrame->format,
                                                            videoW, videoH, AV_PIX_FMT_RGBA,
                                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                                }
                                if (swsCtx) {
                                    sws_scale(swsCtx, vFrame->data, vFrame->linesize, 0, videoH,
                                              rgbFrame->data, rgbFrame->linesize);
                                    foundFrame = rgbBuffer;
                                    foundPts = pts;
                                }
                                frameFound = true;
                                break;
                            }
                        }
                    }
                } else {
                    av_packet_unref(pkt);
                }
            }
            av_packet_free(&pkt);
        }

        // 4. Clean decoder state
        avcodec_flush_buffers(vCodecCtx);
        if (pendingVPacket) { av_packet_unref(pendingVPacket); pendingVPacket = nullptr; }
        eof = false;

        // 5. Re-seek to exact frame PTS
        seek(foundPts);
        q.clear();

        // 6. Fill audio queue — also feed video packets to decoder so keyframes aren't lost.
        //    Buffer decoded video frames with PTS >= foundPts so decodeNextFrame can use them.
        {
            AVPacket* apkt = av_packet_alloc();
            AVStream* aStream = audioStreamIdx >= 0 ? fmtCtx->streams[audioStreamIdx] : nullptr;
            double aTimeBase = aStream ? av_q2d(aStream->time_base) : 1.0;
            while (q.size() < kAudioBufHalf) {
                int ret = av_read_frame(fmtCtx, apkt);
                if (ret < 0) break;
                if (apkt->stream_index == audioStreamIdx) {
                    double apts = (apkt->pts != AV_NOPTS_VALUE) ? (apkt->pts * aTimeBase) : 0.0;
                    if (apts + 0.05 >= foundPts) {
                        decodeAudioPacket(apkt, q);
                    } else {
                        av_packet_unref(apkt);
                    }
                } else if (apkt->stream_index == videoStreamIdx) {
                    int vret = avcodec_send_packet(vCodecCtx, apkt);
                    av_packet_unref(apkt);
                    if (vret >= 0) {
                        while (true) {
                            int dret = avcodec_receive_frame(vCodecCtx, vFrame);
                            if (dret == AVERROR(EAGAIN) || dret == AVERROR_EOF) break;
                            if (dret < 0) break;
                            double pts = (vFrame->pts != AV_NOPTS_VALUE) ? (vFrame->pts * vTimeBase) : 0.0;
                            if (pts >= foundPts) {
                                if (vFrame->format != vCodecCtx->pix_fmt) {
                                    if (swsCtx) { sws_freeContext(swsCtx); swsCtx = nullptr; }
                                    swsCtx = sws_getContext(videoW, videoH, (AVPixelFormat)vFrame->format,
                                                            videoW, videoH, AV_PIX_FMT_RGBA,
                                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                                }
                                if (swsCtx) {
                                    sws_scale(swsCtx, vFrame->data, vFrame->linesize, 0, videoH,
                                              rgbFrame->data, rgbFrame->linesize);
                                    DecodedFrame df;
                                    df.pts = pts;
                                    df.rgba.resize((size_t)videoW * videoH * 4);
                                    for (int y = 0; y < videoH; ++y) {
                                        memcpy(df.rgba.data() + y * videoW * 4,
                                               rgbBuffer + y * rgbFrame->linesize[0],
                                               (size_t)videoW * 4);
                                    }
                                    frameBuffer.push_back(std::move(df));
                                }
                            }
                        }
                    }
                } else {
                    av_packet_unref(apkt);
                }
            }
            av_packet_free(&apkt);
        }

        // 7. Save found frame
        if (foundFrame) {
            hasSeekedFrame_ = true;
            seekedPts = foundPts;
            seekedFrame.resize((size_t)videoW * videoH * 4);
            for (int y = 0; y < videoH; y++) {
                memcpy(seekedFrame.data() + y * videoW * 4,
                       (const uint8_t*)foundFrame + y * rgbFrame->linesize[0],
                       (size_t)videoW * 4);
            }
        }

        return q.size() > 0;
     }

    const uint8_t* getFirstFrame(double& outPts, AudioQueue& q) {
        seek(0);
        frameBuffer.clear();
        return decodeNextFrame(outPts, q);
    }
};

/*==================== 构建视频材质 ====================*/
/*==================== Build Transparent Fill Material ====================*/
/*==================== Build Lyrics Overlay Material ====================*/
static Material* buildLyricsMaterial(Engine* engine) {
    // depthCulling(false) disables depth test so transparent overlay isn't culled
    // by the opaque background quad rendered at the same Z=0
    filamat::MaterialBuilder b;
    b.name("LyricsOverlay")
     .material(R"FILAMENT(
void material(inout MaterialInputs m) {
    prepareMaterial(m);
    vec2 uv = getUV0();
    uv.y = 1.0 - uv.y;  // ThorVG FBO → glReadPixels flips Y
    m.baseColor = texture(materialParams_lyricsTex, uv);
}
)FILAMENT")
     .parameter("lyricsTex", filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
     .require(filament::VertexAttribute::UV0)
     .shading(Shading::UNLIT)
     .blending(filamat::MaterialBuilder::BlendingMode::TRANSPARENT)
     .depthCulling(false)
     .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
     .platform(filamat::MaterialBuilder::Platform::DESKTOP);
    filamat::Package pkg = b.build(engine->getJobSystem());
    if (!pkg.isValid()) { OX_LOG("[Lyrics] Material compile FAILED\n"); return nullptr; }
    OX_LOG("[Lyrics] Material compiled OK\n");
    return Material::Builder().package(pkg.getData(), pkg.getSize()).build(*engine);
}

static Material* buildTransparentFillMaterial(Engine* engine) {
    filamat::MaterialBuilder b;
    b.name("TransparentFill")
     .material(R"FILAMENT(
void material(inout MaterialInputs m) {
    prepareMaterial(m);
    m.baseColor = vec4(0.0);
}
)FILAMENT")
     .shading(Shading::UNLIT)
     .blending(filamat::MaterialBuilder::BlendingMode::TRANSPARENT)
     .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
     .platform(filamat::MaterialBuilder::Platform::DESKTOP);
    filamat::Package pkg = b.build(engine->getJobSystem());
    if (!pkg.isValid()) { OX_LOG("[BG] TransparentFill material compile FAILED\n"); return nullptr; }
    return Material::Builder().package(pkg.getData(), pkg.getSize()).build(*engine);
}

/*==================== Build ShaderToy FX Material ====================*/
static Material* buildShaderToyFXMaterial(Engine* engine, const char* name, const char* code,
    filamat::MaterialBuilder::BlendingMode blend = filamat::MaterialBuilder::BlendingMode::TRANSPARENT) {
    filamat::MaterialBuilder b;
    b.name(name)
     .material(code)
     .parameter("iTime",       filamat::MaterialBuilder::UniformType::FLOAT)
     .parameter("iResolution", filamat::MaterialBuilder::UniformType::FLOAT2)
     .parameter("sceneTex",    filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
     .parameter("texSize",     filamat::MaterialBuilder::UniformType::FLOAT2)
     .parameter("iMouseXY",    filamat::MaterialBuilder::UniformType::FLOAT2)
     .parameter("iMouseZW",    filamat::MaterialBuilder::UniformType::FLOAT2)
     .parameter("lyricsTex",   filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
     .shading(Shading::UNLIT)
     .blending(blend)
     .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
     .platform(filamat::MaterialBuilder::Platform::DESKTOP);
    filamat::Package pkg = b.build(engine->getJobSystem());
    if (!pkg.isValid()) { OX_LOG("[FX] %s material compile FAILED\n", name); return nullptr; }
    return Material::Builder().package(pkg.getData(), pkg.getSize()).build(*engine);
}

static Material* buildVideoMaterial(Engine* engine) {
    filamat::MaterialBuilder b;
    b.name("VideoMaterial")
     .material(R"FILAMENT(
void material(inout MaterialInputs m) {
    prepareMaterial(m);
    m.baseColor = texture(materialParams_videoTex, getUV0());
}
)FILAMENT")
     .parameter("videoTex", filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
     .require(filament::VertexAttribute::UV0)
     .shading(Shading::UNLIT)
     .blending(filamat::MaterialBuilder::BlendingMode::OPAQUE)
     .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
     .platform(filamat::MaterialBuilder::Platform::DESKTOP);
    filamat::Package pkg = b.build(engine->getJobSystem());
    if (!pkg.isValid()) { OX_LOG("[Player] Material compile FAILED\n"); return nullptr; }
    return Material::Builder().package(pkg.getData(), pkg.getSize()).build(*engine);
}

static void updateVideoTransform(Engine* engine, Entity quadEntity, int winW, int winH, int vidW, int vidH) {
    if (vidW <= 0 || vidH <= 0) {
        auto& tcm = engine->getTransformManager();
        auto ti = tcm.getInstance(quadEntity);
        tcm.setTransform(ti, math::mat4f::scaling(float3{0, 0, 1.0f}));
        return;
    }
    float windowAspect = float(winW) / float(winH);
    float videoAspect  = float(vidW) / float(vidH);
    float sx = 1.0f, sy = 1.0f;
    if (windowAspect > videoAspect) {
        sx = videoAspect / windowAspect;
    } else {
        sy = windowAspect / videoAspect;
    }
    auto& tcm = engine->getTransformManager();
    auto ti = tcm.getInstance(quadEntity);
    tcm.setTransform(ti, math::mat4f::scaling(float3{sx, sy, 1.0f}));
    static int aspectLogCount = 0;
    if (++aspectLogCount <= 3) {
        OX_LOG("[Aspect] win=%dx%d vid=%dx%d winA=%.4f vidA=%.4f scale=(%.4f,%.4f)\n",
               winW, winH, vidW, vidH, windowAspect, videoAspect, sx, sy);
    }
}

/*==================== AppState ====================*/
struct AppState {
    // --- Engine / Rendering ---
    OX::Application app;
    OX::UIManager ui;
    Engine* engine = nullptr;
    SwapChain* swapChain = nullptr;
    Renderer* renderer = nullptr;
    Scene* scene = nullptr;
    View* view = nullptr;
    Camera* cam = nullptr;
    Entity camEntity;
    Material* mat = nullptr;
    MaterialInstance* mi = nullptr;
    Entity quadEntity;
    Texture* videoTex = nullptr;
    bool needRebuildUI = false;

    // --- ShaderToy FX ---
    static constexpr int MAX_FX = 8;
    bool fxEnabled = false;
    float fxTime = 0.0f;
    int fxEffectIndex = 0;                    // current effect
    int fxEffectCount = 0;                    // number successfully compiled
    Material* fxMat[MAX_FX] = {};             // one per effect
    MaterialInstance* fxMi[MAX_FX] = {};      // one per effect
    Entity fxEntity;
    OX::UIButton* fxBtnPtr = nullptr;
    OX::UIDropdown* fxDropdownPtr = nullptr;
    float4 fxMouse = float4{0, 0, 0, 0};     // iMouse: xy=pos, zw=click

    // --- 背景显示控制 ---
    bool showBackground = true;
    Material* bgMat = nullptr;
    MaterialInstance* bgMi = nullptr;
    Entity bgEntity;
    OX::UIButton* bgBtnPtr = nullptr;

    // --- 歌词独立渲染层 (between video and FX) ---
    Entity lyricsEntity;
    Material* lyricsMat = nullptr;
    MaterialInstance* lyricsMi = nullptr;

    // --- Playback State ---
    bool isPlaying = false;
    bool isPaused = false;
    bool hasFrame = false;
    PlayMode playMode = PlayMode::Sequential;
    bool audioStarted = false;
    double videoTime = 0.0;
    double currentVideoPts = 0.0;
    double frameDuration = DEFAULT_FRAME_DURATION_SEC;
    bool pendingSeek = false;
    double pendingSeekTarget = 0.0;

    // --- Audio ---
    ma_device audioDevice{};
    std::atomic<uint64_t> audioSamplesPlayed{0};
    AudioQueue audioQueue{kAudioBufCapacity};
	float volume = 0.5f;// 默认音量 50%
    static constexpr int AUDIO_SAMPLE_RATE = kAudioSampleRate;
    static constexpr int AUDIO_CHANNELS = kAudioChannels;
    static constexpr size_t AUDIO_BUF_FIFTH_SEC   = kAudioBufFifth;
    static constexpr size_t AUDIO_BUF_QUARTER_SEC  = kAudioBufQuarter;
    static constexpr size_t AUDIO_BUF_HALF_SEC     = kAudioBufHalf;
    static constexpr size_t AUDIO_BUF_EOF_THRESH   = kAudioBufEofThresh;
    static constexpr size_t AUDIO_BUF_CAPACITY     = kAudioBufCapacity;
    static constexpr double VIDEO_SYNC_THRESHOLD_SEC = 0.005;
    static constexpr double DEFAULT_FRAME_DURATION_SEC = 0.033;

    // --- Decoder ---
    AVDecoder decoder;
    int videoW = 0;
    int videoH = 0;

    // --- UI Data ---
    char fpsText[64] = "FPS: 0";
    char infoText[128] = "No video";
    char timeText[64] = "00:00 / 00:00";
    char statusText[32] = "Stopped";
    char syncText[64] = "Sync: --";
    char frameText[64] = "Frame: --";

    // --- UI Widgets ---
    OX::UILabel* fpsLabelPtr = nullptr;
    OX::UILabel* infoLabelPtr = nullptr;
    OX::UILabel* timeLabelPtr = nullptr;
    OX::UILabel* statusLabelPtr = nullptr;
    OX::UILabel* syncLabelPtr = nullptr;
    OX::UILabel* frameLabelPtr = nullptr;
    OX::UILabel* rightInfoLabelPtr = nullptr;
    OX::UIButton* playBtnPtr = nullptr;
    OX::UIButton* loopBtnPtr = nullptr;
    OX::UIButton* prevBtnPtr = nullptr;
    OX::UIButton* nextBtnPtr = nullptr;
    OX::UIDropdown* fileDropdownPtr = nullptr;
    OX::UISlider* seekBarPtr = nullptr;
    OX::UISlider* volumeSliderPtr = nullptr;
    bool seekBarDragging = false;

    // --- Playlist ---
    std::vector<std::string> mediaFiles;           // 当前播放列表(flat, 供 fileDropdown 用)
    int currentFileIndex = -1;
    char rightInfoText[256] = "";

    // --- 歌单系统 ---
    std::vector<PlaylistInfo> playlists;
    int currentPlaylistIndex = -1;

    // --- 创建歌单对话框 ---
    bool showCreatePlaylistDlg = false;
    char plDlgName[256] = "";
    int plDlgType = 0;             // 0=内置歌单, 1=本地目录
    char plDlgDir[512] = "";
    bool plDlgAutoScan = true;
    int plDlgScanInterval = 3600;

    // --- 添加歌曲对话框 ---
    bool showAddSongsDlg = false;
    int addSongSelIdx = -1;

    // --- 断点续播 ---
    int resumePlaylistIndex = -1;
    int resumeSongIndex = -1;
    double resumePosition = 0.0;
    char resumeFilePath[512] = "";

    // --- 歌单切换 Dropdown ---
    OX::UIDropdown* playlistDropdownPtr = nullptr;

    // --- Lyrics ---
    std::vector<LrcLine> lrcLines;
    bool showLyrics = true;
    OX::UIFrame* lrcPanelPtr = nullptr;
    OX::UILabel* lrcLabels[7] = {};
    OX::UIButton* lyricsBtnPtr = nullptr;
    int lrcCurrentIndex = -1;

    // --- Lyrics FBO (render-to-texture for FX refraction) ---
    GLuint lrcFboId = 0;
    GLuint lrcTexId = 0;
    tvg::GlCanvas* lrcGlCanvas = nullptr;   // separate GlCanvas targeting FBO
    tvg::Scene* lrcTvgScene = nullptr;      // ThorVG scene for lyrics
    int lrcFboW = 0, lrcFboH = 0;
    Texture* lrcFilamentTex = nullptr;      // Filament wrapper for GL lrcTexId
    bool lrcFboDirty = true;                // re-render only when lyrics change

    // --- Timing ---
    std::chrono::steady_clock::time_point lastFrameTime;
    float fps = 0.0f;
};

/*==================== 歌词更新 ====================*/
static void updateLyrics(AppState& st) {
    if (st.lrcLines.empty()) return;

    // 二分查找当前时间点对应的歌词行
    int idx = -1;
    int lo = 0, hi = (int)st.lrcLines.size() - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (st.lrcLines[mid].timestamp <= st.videoTime) {
            idx = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    // 如果没变，跳过 label 更新
    if (idx == st.lrcCurrentIndex) return;
    st.lrcCurrentIndex = idx;
    st.lrcFboDirty = true;    // lyrics line changed, re-render FBO next frame

    for (int i = 0; i < 7; i++) {
        if (!st.lrcLabels[i]) continue;
        int lineIdx = idx + (i - 3);  // offset from center line
        if (lineIdx >= 0 && lineIdx < (int)st.lrcLines.size()) {
            st.lrcLabels[i]->setText(st.lrcLines[lineIdx].text.c_str());
            // 高亮当前行 (index 3 = center)
            if (i == 3) {
                st.lrcLabels[i]->fontSize = 18.0f;
                st.lrcLabels[i]->textColor = OX::OColor(255, 255, 255, 255);
            } else if (i == 2 || i == 4) {
                st.lrcLabels[i]->fontSize = 14.0f;
                st.lrcLabels[i]->textColor = OX::OColor(180, 180, 200, 255);
            } else {
                st.lrcLabels[i]->fontSize = 12.0f;
                st.lrcLabels[i]->textColor = OX::OColor(120, 120, 140, 200);
            }
        } else {
            st.lrcLabels[i]->setText("");
        }
    }
}

/*==================== 歌词 FBO 渲染（离屏渲染→Filament 纹理→FX Shader 折射） ====================*/
static void destroyLrcFbo(AppState& st) {
    // Check if scene is in canvas before clearing (ThorVG ref-counting)
    bool sceneInCanvas = (st.lrcTvgScene && st.lrcTvgScene->refCnt() > 0);
    if (st.lrcGlCanvas) {
        st.lrcGlCanvas->remove(nullptr);  // Releases all paints; frees scene if it was pushed
        delete st.lrcGlCanvas;
        st.lrcGlCanvas = nullptr;
    }
    // If scene was never pushed to canvas, it's still alive and must be freed manually
    if (!sceneInCanvas && st.lrcTvgScene) {
        delete st.lrcTvgScene;
    }
    st.lrcTvgScene = nullptr;
    if (st.lrcTexId) { glDeleteTextures(1, &st.lrcTexId); st.lrcTexId = 0; }
    if (st.lrcFboId) { glDeleteFramebuffers(1, &st.lrcFboId); st.lrcFboId = 0; }
    st.lrcFboW = st.lrcFboH = 0;
}

static bool createLrcFbo(AppState& st, int w, int h) {
    destroyLrcFbo(st);
    if (w <= 0 || h <= 0) return false;

    HGLRC hglrc = wglGetCurrentContext();
    if (!hglrc) { OX_LOG("[LrcFbo] No GL context\n"); return false; }

    glGenFramebuffers(1, &st.lrcFboId);
    glBindFramebuffer(GL_FRAMEBUFFER, st.lrcFboId);

    glGenTextures(1, &st.lrcTexId);
    glBindTexture(GL_TEXTURE_2D, st.lrcTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, st.lrcTexId, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        OX_LOG("[LrcFbo] FBO incomplete: 0x%x\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroyLrcFbo(st);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    st.lrcGlCanvas = tvg::GlCanvas::gen();
    if (!st.lrcGlCanvas) { destroyLrcFbo(st); return false; }

    auto r = st.lrcGlCanvas->target(hglrc, st.lrcFboId, (uint32_t)w, (uint32_t)h, tvg::ColorSpace::ABGR8888S);
    if (r != tvg::Result::Success) {
        OX_LOG("[LrcFbo] GlCanvas::target failed\n");
        destroyLrcFbo(st);
        return false;
    }

    st.lrcTvgScene = tvg::Scene::gen();
    if (!st.lrcTvgScene) { destroyLrcFbo(st); return false; }

    st.lrcFboW = w;
    st.lrcFboH = h;
    st.lrcFboDirty = true;
    OX_LOG("[LrcFbo] Created %dx%d Fbo=%u Tex=%u\n", w, h, st.lrcFboId, st.lrcTexId);
    return true;
}

static void renderLyricsToFbo(AppState& st, float lrcX, float lrcY, float lrcW, float lrcH,
                               const char* fontName) {
    if (!st.lrcGlCanvas || st.lrcLines.empty()) return;

    // ThorVG: canvas->remove(nullptr) calls clearPaints(), which unrefs and frees
    // old paints properly. Do this BEFORE creating a new scene to avoid dangling ptrs.
    st.lrcGlCanvas->remove(nullptr);
    st.lrcTvgScene = tvg::Scene::gen();
    if (!st.lrcTvgScene) return;

    // 歌词面板背景
    auto bg = tvg::Shape::gen();
    bg->appendRect(lrcX, lrcY, lrcW, lrcH, 8.0f, 8.0f);
    bg->fill(20, 22, 30, 200);
    bg->strokeFill(60, 62, 80, 200);
    bg->strokeWidth(1.0f);
    st.lrcTvgScene->push(bg);

    // 7 行歌词
    float lineH = 40.0f;
    float startY = lrcY + (lrcH - 7.0f * lineH) / 2.0f;
    for (int i = 0; i < 7; i++) {
        int lineIdx = st.lrcCurrentIndex + (i - 3);
        if (lineIdx < 0 || lineIdx >= (int)st.lrcLines.size()) continue;
        auto txt = tvg::Text::gen();
        txt->text(st.lrcLines[lineIdx].text.c_str());
        txt->font(fontName);
        txt->translate(lrcX + lrcW / 2.0f, startY + i * lineH + lineH / 2.0f - 4.0f);
        txt->align(0.5f, 0.5f);
        if (i == 3) { // 当前行高亮
            txt->size(18.0f);
            txt->fill(255, 255, 255);
        } else if (i == 2 || i == 4) {
            txt->size(14.0f);
            txt->fill(180, 180, 200);
        } else {
            txt->size(12.0f);
            txt->fill(120, 120, 140);
        }
        st.lrcTvgScene->push(txt);
    }

    st.lrcGlCanvas->push(st.lrcTvgScene);
    st.lrcGlCanvas->draw(true);
    st.lrcGlCanvas->sync();
}

static void uploadLyricsToFilament(AppState& st) {
    if (!st.lrcTexId) { OX_LOG("[LRC] upload: lrcTexId=0, skip\n"); return; }

    // Lazily create Filament texture on first upload (FBO created after loadMediaFile)
    if (!st.lrcFilamentTex && st.engine) {
        OX_LOG("[LRC] Creating lrcFilamentTex %dx%d, lyricsMi=%p\n", st.lrcFboW, st.lrcFboH, (void*)st.lyricsMi);
        st.lrcFilamentTex = Texture::Builder()
            .width((uint32_t)st.lrcFboW).height((uint32_t)st.lrcFboH).levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .sampler(Texture::Sampler::SAMPLER_2D)
            .build(*st.engine);
        auto smp = TextureSampler(TextureSampler::MinFilter::LINEAR, TextureSampler::MagFilter::LINEAR);
        if (st.lyricsMi) {
            st.lyricsMi->setParameter("lyricsTex", st.lrcFilamentTex, smp);
            OX_LOG("[LRC] Bound tex to lyricsMi OK\n");
        } else {
            OX_LOG("[LRC] lyricsMi is NULL!\n");
        }
    }
    if (!st.lrcFilamentTex) { OX_LOG("[LRC] lrcFilamentTex still null\n"); return; }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, st.lrcFboId);
    size_t bufSize = (size_t)st.lrcFboW * st.lrcFboH * 4;
    uint8_t* pixels = (uint8_t*)malloc(bufSize);
    if (!pixels) { glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); OX_LOG("[LRC] malloc failed\n"); return; }
    glReadPixels(0, 0, st.lrcFboW, st.lrcFboH, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // PixelBufferDescriptor takes ownership; will call ::free() on pixels
    Texture::PixelBufferDescriptor pbd(
        pixels, bufSize,
        Texture::Format::RGBA, Texture::Type::UBYTE
    );
    st.lrcFilamentTex->setImage(*st.engine, 0, std::move(pbd));
    OX_LOG("[LRC] setImage done, %zu bytes\n", bufSize);
    st.lrcFboDirty = false;
}

/*==================== Layer visibility helper ====================*/
static void updateVisibleLayers(AppState& st) {
    uint8_t mask = 0;
    // 背景层: showBackground=true → 显示视频/封面(0x01), false → 显示透明填充(0x04)
    if (st.showBackground) {
        mask |= 0x01;  // 背景层 (priority 0, 先渲染)
    } else {
        mask |= 0x04;  // 透明填充 (priority 3, 覆盖在最上面, alpha=0 全透明)
    }
    // 歌词层 (priority 1, 在背景和FX之间)
    // Rain FX (index 2) handles lyrics internally; hide separate entity to avoid layering conflict
    bool rainActive = st.fxEnabled && st.fxEffectIndex == 2;
    if (st.showLyrics && !rainActive) {
        mask |= 0x08;
    }
    // FX层: 独立开关, 覆盖在背景之上 (priority 2)
    if (st.fxEnabled) {
        mask |= 0x02;  // FX层
    }
    OX_LOG("[LRC] updateVisibleLayers: mask=0x%02X, showBg=%d showLyrics=%d fx=%d\n",
           mask, st.showBackground, st.showLyrics, st.fxEnabled);
    st.view->setVisibleLayers(0xFF, mask);
}

/*==================== Helpers ====================*/
static void uploadVideoFrame(AppState& st, const uint8_t* data, int w, int h) {
    st.videoTex->setImage(*st.engine, 0,
        Texture::PixelBufferDescriptor(data, (size_t)w * h * 4,
            Texture::PixelBufferDescriptor::PixelDataFormat::RGBA,
            Texture::PixelBufferDescriptor::PixelDataType::UBYTE));
    st.engine->flushAndWait();
}

/*==================== miniaudio callback ====================*/
static void ma_data_callback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    AppState* st = (AppState*)pDevice->pUserData;
    if (!st) return;
    float* out = (float*)pOutput;
    size_t needed = frameCount * st->AUDIO_CHANNELS;
    size_t got = st->audioQueue.read(out, needed);
    if (got == 0) {
        static int underflowCount = 0;
        if (++underflowCount <= 5) {
            OX_LOG("[Audio] underflow! frameCount=%u needed=%zu queueSize=%zu\n",
                   frameCount, needed, st->audioQueue.size());
        }
    }
    if (got < needed) {
        memset(out + got, 0, (needed - got) * sizeof(float));
    }
    st->audioSamplesPlayed += frameCount;
}

/*==================== cleanup ====================*/
static void cleanup(AppState& st) {
    if (st.audioDevice.pUserData) {
        ma_device_uninit(&st.audioDevice);
    }
    if (st.engine) {
        // Destroy in reverse order: entity -> material instance -> material -> texture -> scene -> view -> renderer -> swapchain -> engine
        if (st.lyricsEntity) st.engine->destroy(st.lyricsEntity);
        if (st.lyricsMi) st.engine->destroy(st.lyricsMi);
        if (st.lyricsMat) st.engine->destroy(st.lyricsMat);
        if (st.fxEntity) st.engine->destroy(st.fxEntity);
        for (int i = 0; i < st.fxEffectCount; i++) {
            if (st.fxMi[i]) st.engine->destroy(st.fxMi[i]);
            if (st.fxMat[i]) st.engine->destroy(st.fxMat[i]);
        }
        if (st.bgEntity) st.engine->destroy(st.bgEntity);
        if (st.bgMi) st.engine->destroy(st.bgMi);
        if (st.bgMat) st.engine->destroy(st.bgMat);
        if (st.camEntity) st.engine->destroy(st.camEntity);
        if (st.quadEntity) st.engine->destroy(st.quadEntity);
        if (st.mi) st.engine->destroy(st.mi);
        if (st.mat) st.engine->destroy(st.mat);
        if (st.videoTex) st.engine->destroy(st.videoTex);
        if (st.lrcFilamentTex) st.engine->destroy(st.lrcFilamentTex);
        if (st.cam) st.engine->destroyCameraComponent(st.camEntity);
        if (st.scene) st.engine->destroy(st.scene);
        if (st.view) st.engine->destroy(st.view);
        if (st.renderer) st.engine->destroy(st.renderer);
        if (st.swapChain) st.engine->destroy(st.swapChain);
        Engine::destroy(&st.engine);
    }
    filamat::MaterialBuilder::shutdown();
    st.decoder.close();
}

/*==================== 扫描媒体文件 ====================*/
// 路径分隔符统一为 /
static std::string normalizePath(const std::string& path) {
    std::string s = path;
    for (char& c : s) { if (c == '\\') c = '/'; }
    return s;
}

static void scanMediaFiles(std::vector<std::string>& files, const std::vector<std::string>& dirs) {
    const std::vector<std::string> exts = {
        ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".ts",
        ".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma"
    };
    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) continue;
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = OX::Core::wstrToUtf8(entry.path().extension().wstring());
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
                if (std::find(exts.begin(), exts.end(), ext) != exts.end()) {
                    files.push_back(normalizePath(OX::Core::wstrToUtf8(entry.path().wstring())));
                }
            }
        } catch (...) {}
    }
}

/*==================== 加载媒体文件 ====================*/
// 前向声明
static void loadLyrics(AppState& st);

static bool loadMediaFile(AppState& st, const char* path, int winW, int winH) {
    st.decoder.close();
    if (!st.decoder.open(path)) return false;

    st.videoW = st.decoder.getWidth();
    st.videoH = st.decoder.getHeight();

    // ---- 纯音频：用封面图或默认 LOGO 覆盖尺寸 ----
    bool usingCover = false;
    unsigned char* logoData = nullptr;
    int logoW = 0, logoH = 0;

    if (!st.decoder.hasVideo()) {
        if (st.decoder.hasCover()) {
            st.videoW = st.decoder.getCoverWidth();
            st.videoH = st.decoder.getCoverHeight();
            usingCover = true;
            OX_LOG("[AV] Using embedded cover art %dx%d\n", st.videoW, st.videoH);
        } else {
            logoData = stbi_load("e:/MX/Projects/MPlayer/wp.png", &logoW, &logoH, nullptr, 4);
            if (logoData) {
                st.videoW = logoW;
                st.videoH = logoH;
                OX_LOG("[AV] Using fallback LOGO.jpg %dx%d\n", logoW, logoH);
            }
        }
    }

    st.frameDuration = st.decoder.hasVideo() ? (1.0 / st.decoder.getFps()) : 0.0;
    if (st.decoder.hasVideo()) {
        snprintf(st.infoText, sizeof(st.infoText), "%dx%d @ %.1ffps", st.videoW, st.videoH, st.decoder.getFps());
    } else if (usingCover) {
        snprintf(st.infoText, sizeof(st.infoText), "Audio only + Cover (%dx%d)", st.videoW, st.videoH);
    } else if (logoData) {
        snprintf(st.infoText, sizeof(st.infoText), "Audio only + Logo (%dx%d)", st.videoW, st.videoH);
    } else {
        snprintf(st.infoText, sizeof(st.infoText), "Audio only (%dHz/%dch)", st.decoder.getSampleRate(), st.decoder.getChannels());
    }

    if (st.videoTex) {
        st.engine->destroy(st.videoTex);
        st.videoTex = nullptr;
    }
    int texW = (st.videoW > 0) ? st.videoW : 1;
    int texH = (st.videoH > 0) ? st.videoH : 1;
    st.videoTex = Texture::Builder()
        .width(texW).height(texH).levels(1)
        .format(Texture::InternalFormat::RGBA8)
        .sampler(Texture::Sampler::SAMPLER_2D)
        .build(*st.engine);

    // 纯音频文件：上传封面或logo
    if (usingCover) {
        uploadVideoFrame(st, st.decoder.getCoverData(), st.videoW, st.videoH);
    } else if (logoData) {
        uploadVideoFrame(st, logoData, st.videoW, st.videoH);
        stbi_image_free(logoData);
    } else if (!st.decoder.hasVideo()) {
        // 均失败时回退到黑色
        std::vector<uint8_t> black(texW * texH * 4, 0);
        uploadVideoFrame(st, black.data(), texW, texH);
    }

    TextureSampler smp(TextureSampler::MinFilter::LINEAR, TextureSampler::MagFilter::LINEAR);
    st.mi->setParameter("videoTex", st.videoTex, smp);

    // Bind video/cover texture to all FX materials (used by Heartfelt for refraction)
    for (int i = 0; i < st.fxEffectCount; i++) {
        if (st.fxMi[i]) {
            st.fxMi[i]->setParameter("sceneTex", st.videoTex, smp);
            st.fxMi[i]->setParameter("texSize", float2{float(st.videoW), float(st.videoH)});
        }
    }

    // Bind lyrics FBO texture to HeartfeltRain FX material
    if (st.lrcTexId && st.fxEffectCount > 0) {
        // Create a regular Filament Texture (will upload from FBO via CPU bridge)
        if (st.lrcFilamentTex) { st.engine->destroy(st.lrcFilamentTex); st.lrcFilamentTex = nullptr; }
        st.lrcFilamentTex = Texture::Builder()
            .width((uint32_t)st.lrcFboW).height((uint32_t)st.lrcFboH).levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .sampler(Texture::Sampler::SAMPLER_2D)
            .build(*st.engine);
        // Bind lyricsTex to all FX materials (generic layer)
        for (int i = 0; i < st.fxEffectCount; i++) {
            if (st.fxMi[i]) {
                st.fxMi[i]->setParameter("lyricsTex", st.lrcFilamentTex, smp);
            }
        }
        // Also bind to independent lyrics layer
        if (st.lyricsMi) {
            st.lyricsMi->setParameter("lyricsTex", st.lrcFilamentTex, smp);
        }
    }

    if (st.quadEntity) {
        updateVideoTransform(st.engine, st.quadEntity, winW, winH, st.videoW, st.videoH);
    }

    // 加载歌词
    loadLyrics(st);

    st.videoTime = 0.0;
    st.hasFrame = false;
    st.isPlaying = false;
    st.isPaused = false;
    OX_LOG("[Audio] audioQueue.clear() from loadMediaFile\n");
    st.audioQueue.clear();
    st.audioSamplesPlayed = 0;
    if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }

    return true;
}

/*==================== 更新右侧面板信息 ====================*/
static void updateRightInfo(AppState& st) {
    int total = (int)st.mediaFiles.size();
    std::string name = "None";
    if (st.currentFileIndex >= 0 && st.currentFileIndex < total) {
        const std::string& fullPath = st.mediaFiles[st.currentFileIndex];
        size_t pos = fullPath.find_last_of("\\/");
        name = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
    }
    if (st.decoder.hasVideo()) {
        snprintf(st.rightInfoText, sizeof(st.rightInfoText),
            "Files: %d/%d\nName: %s\nRes: %dx%d\nFPS: %.1f\nDuration: %.1fs",
            st.currentFileIndex + 1, total, name.c_str(), st.videoW, st.videoH,
            st.decoder.getFps(), st.decoder.getDuration());
    } else {
        snprintf(st.rightInfoText, sizeof(st.rightInfoText),
            "Files: %d/%d\nName: %s\nRes: Audio only\nFPS: -\nDuration: %.1fs",
            st.currentFileIndex + 1, total, name.c_str(), st.decoder.getDuration());
    }
    if (st.rightInfoLabelPtr) st.rightInfoLabelPtr->setText(st.rightInfoText);
}

/*==================== LRC 歌词解析 ====================*/
static bool parseLRC(const char* lrcPath, std::vector<LrcLine>& lines) {
    FILE* f = nullptr;
    fopen_s(&f, lrcPath, "rb");
    if (!f) return false;

    lines.clear();
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        // 跳过空行和注释
        if (buf[0] == '\0' || buf[0] == '\n' || buf[0] == '\r') continue;

        // 解析 [mm:ss.xx] 或 [mm:ss.xxx] 时间戳
        char* p = buf;
        std::vector<double> timestamps;
        while (*p == '[') {
            int min = 0, sec_int = 0, frac = 0;
            if (sscanf_s(p, "[%d:%d.%d]", &min, &sec_int, &frac) == 3) {
                // 处理 .xx (百分秒) 和 .xxx (毫秒)
                double sec = sec_int;
                if (frac < 100)      sec += frac / 100.0;   // .xx = 百分秒
                else                 sec += frac / 1000.0;  // .xxx = 毫秒
                timestamps.push_back(min * 60.0 + sec);
            }
            // 跳到下一个 ]
            char* close = strchr(p, ']');
            if (!close) break;
            p = close + 1;
        }

        // 提取歌词文本（跳过前导空白）
        while (*p == ' ' || *p == '\t') p++;
        // 去除尾部换行
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';

        std::string text(p);
        if (text.empty()) text = "♪";

        for (double ts : timestamps) {
            lines.push_back({ts, text});
        }
    }
    fclose(f);

    // 按时间戳排序
    std::sort(lines.begin(), lines.end(),
        [](const LrcLine& a, const LrcLine& b) { return a.timestamp < b.timestamp; });

    OX_LOG("[Lyrics] Parsed %zu lines from %s\n", lines.size(), lrcPath);
    return !lines.empty();
}

// 将纯文本歌词（无时间戳）转换为 LrcLine 格式，按歌曲时长均匀分布时间
static void parsePlainLyrics(const std::string& text, std::vector<LrcLine>& lines, double duration) {
    lines.clear();
    if (text.empty()) return;

    // 按换行符分割
    std::istringstream iss(text);
    std::vector<std::string> rawLines;
    std::string line;
    while (std::getline(iss, line)) {
        // 去掉首尾空白和回车符
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) rawLines.push_back(line);
    }
    if (rawLines.empty()) return;

    // 按歌曲时长均匀分布时间戳
    double interval = duration / (double)(rawLines.size() + 1);
    for (size_t i = 0; i < rawLines.size(); i++) {
        lines.push_back({interval * (i + 1), rawLines[i]});
    }
    OX_LOG("[Lyrics] Parsed %zu plain text lines, interval=%.1fs\n", lines.size(), interval);
}

static void loadLyrics(AppState& st) {
    st.lrcLines.clear();
    st.lrcCurrentIndex = -1;

    // 1) 优先尝试提取内嵌歌词（FLAC/MP3/MP4 等元数据）
    std::string embedded = st.decoder.getEmbeddedLyrics();
    if (!embedded.empty()) {
        OX_LOG("[Lyrics] Embedded lyrics: %zu bytes\n", embedded.size());
        // 检查是否包含 LRC 时间戳 [mm:ss
        if (embedded.find("[") != std::string::npos &&
            embedded.find(":") != std::string::npos) {
            // 尝试将内嵌歌词写入临时文件然后用 parseLRC 解析
            // 因为内嵌歌词可能使用 \r\n 或 \n 分隔
            std::string tmpPath = "e:/MX/Data/_lrc_temp.lrc";
            std::ofstream ofs(tmpPath, std::ios::binary);
            if (ofs) {
                ofs << embedded;
                ofs.close();
                if (parseLRC(tmpPath.c_str(), st.lrcLines)) {
                    st.lrcCurrentIndex = 0;
                    st.lrcLines = st.lrcLines; // keep
                    OX_LOG("[Lyrics] Parsed embedded LRC lyrics: %zu lines\n", st.lrcLines.size());
                    std::remove(tmpPath.c_str());
                    return;
                }
                std::remove(tmpPath.c_str());
            }
        }
        // 纯文本内嵌歌词：按时长均匀分布
        double dur = st.decoder.getDuration();
        if (dur <= 0) dur = 240.0; // fallback 4 minutes
        parsePlainLyrics(embedded, st.lrcLines, dur);
        if (!st.lrcLines.empty()) {
            st.lrcCurrentIndex = 0;
            return;
        }
    }

    // 2) 回退到外部 .lrc 文件
    if (st.currentFileIndex < 0 || st.currentFileIndex >= (int)st.mediaFiles.size()) return;

    std::string audioPath = st.mediaFiles[st.currentFileIndex];
    size_t dotPos = audioPath.find_last_of('.');
    if (dotPos == std::string::npos) return;
    std::string lrcPath = audioPath.substr(0, dotPos) + ".lrc";

    std::ifstream test(lrcPath);
    if (!test.good()) {
        OX_LOG("[Lyrics] No .lrc file found: %s\n", lrcPath.c_str());
        return;
    }
    test.close();

    if (parseLRC(lrcPath.c_str(), st.lrcLines)) {
        st.lrcCurrentIndex = 0;
    }
}

/*==================== 格式化时间 ====================*/
static void formatTime(char* buf, size_t sz, double sec) {
    int m = (int)(sec / 60.0);
    int s = (int)sec % 60;
    snprintf(buf, sz, "%02d:%02d", m, s);
}

/*==================== 播放模式处理 ====================*/
static void handlePlayModeEOF(AppState& st) {
    switch (st.playMode) {
        case PlayMode::SingleLoop:
            // 单曲循环：seek到开头重新播放
            OX_LOG("[SingleLoop] Restarting current file\n");
            st.audioQueue.clear();
            st.audioSamplesPlayed = 0;
            st.videoTime = 0.0;
            st.decoder.seek(0);
            // 视频：获取第一帧并上传
            if (st.decoder.hasVideo()) {
                double pts;
                const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                if (f) {
                    st.currentVideoPts = pts;
                    st.hasFrame = true;
                    uploadVideoFrame(st, f, st.videoW, st.videoH);
                    OX_LOG("[SingleLoop] First frame uploaded, pts=%.3f\n", pts);
                } else {
                    OX_LOG("[SingleLoop] Failed to get first frame\n");
                }
            }
            st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_HALF_SEC);
            if (st.audioStarted) {
                ma_device_stop(&st.audioDevice);
                ma_result maRes = ma_device_start(&st.audioDevice);
                if (maRes != MA_SUCCESS) { OX_LOG("[Audio] restart failed: %d\n", maRes); }
            }
            break;

        case PlayMode::ListLoop:
            // 列表循环：播放下一首，到末尾则回到第一首
            if (!st.mediaFiles.empty()) {
                int nextIndex = st.currentFileIndex + 1;
                if (nextIndex >= (int)st.mediaFiles.size()) nextIndex = 0;
                uint32_t w, h; st.app.getSize(&w, &h);
                if (loadMediaFile(st, st.mediaFiles[nextIndex].c_str(), (int)w, (int)h)) {
                    st.currentFileIndex = nextIndex;
                    if (st.decoder.hasVideo()) {
                        double pts;
                        const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                        if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                    }
                    st.isPlaying = true; st.isPaused = false;
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                    if (!st.audioStarted) {
                        if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                    }
                    st.needRebuildUI = true;
                }
            }
            break;

        case PlayMode::Sequential:
            // 顺序播放：播放下一首，到末尾则停止
            if (!st.mediaFiles.empty() && st.currentFileIndex + 1 < (int)st.mediaFiles.size()) {
                int nextIndex = st.currentFileIndex + 1;
                uint32_t w, h; st.app.getSize(&w, &h);
                if (loadMediaFile(st, st.mediaFiles[nextIndex].c_str(), (int)w, (int)h)) {
                    st.currentFileIndex = nextIndex;
                    if (st.decoder.hasVideo()) {
                        double pts;
                        const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                        if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                    }
                    st.isPlaying = true; st.isPaused = false;
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                    if (!st.audioStarted) {
                        if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                    }
                    st.needRebuildUI = true;
                }
            } else {
                // 到末尾，停止播放
                st.isPlaying = false;
                st.isPaused = false;
                snprintf(st.statusText, sizeof(st.statusText), "Stopped");
                if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
                if (st.playBtnPtr) st.playBtnPtr->setText("Play");
                if (st.statusLabelPtr) st.statusLabelPtr->setText(st.statusText);
            }
            break;

        case PlayMode::Random:
            // 随机播放：随机选择一首（可能重复）
            if (!st.mediaFiles.empty()) {
                int nextIndex;
                if (st.mediaFiles.size() > 1) {
                    do { nextIndex = rand() % (int)st.mediaFiles.size(); } while (nextIndex == st.currentFileIndex);
                } else {
                    nextIndex = 0;
                }
                uint32_t w, h; st.app.getSize(&w, &h);
                if (loadMediaFile(st, st.mediaFiles[nextIndex].c_str(), (int)w, (int)h)) {
                    st.currentFileIndex = nextIndex;
                    if (st.decoder.hasVideo()) {
                        double pts;
                        const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                        if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                    }
                    st.isPlaying = true; st.isPaused = false;
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                    if (!st.audioStarted) {
                        if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                    }
                    st.needRebuildUI = true;
                }
            }
            break;
    }
}

/*==================== 文件切换辅助 ====================*/
static void switchFile(AppState& st, int delta) {
    if (st.mediaFiles.empty()) return;
    if (st.isPlaying) {
        st.isPlaying = false; st.isPaused = true;
        if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
    }
    st.currentFileIndex += delta;
    if (st.currentFileIndex < 0) st.currentFileIndex = (int)st.mediaFiles.size() - 1;
    if (st.currentFileIndex >= (int)st.mediaFiles.size()) st.currentFileIndex = 0;
    uint32_t w, h; st.app.getSize(&w, &h);
    if (loadMediaFile(st, st.mediaFiles[st.currentFileIndex].c_str(), (int)w, (int)h)) {
        if (st.decoder.hasVideo()) {
            double pts;
            const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
            if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
        }
        st.isPlaying = true; st.isPaused = false;
        snprintf(st.statusText, sizeof(st.statusText), "Playing");
        st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
        if (!st.audioStarted) {
            if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
        }
    }
    st.needRebuildUI = true;
}

/*==================== 自检: 确保 MData/config.json 存在 ====================*/
static void ensureConfig() {
    namespace fs = std::filesystem;

    // 获取 exe 所在目录
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path mdataDir = exeDir / "MData";
    fs::path configPath = mdataDir / "config.json";

    // 如果 MData 目录不存在则创建
    if (!fs::exists(mdataDir)) {
        OX_LOG("[Config] Creating MData directory: %s\n", mdataDir.string().c_str());
        std::error_code ec;
        if (!fs::create_directory(mdataDir, ec)) {
            OX_LOG("[Config] ERROR: Failed to create MData directory: %s\n", ec.message().c_str());
            return;
        }
    }

    // 如果 config.json 不存在则用默认模板创建
    if (!fs::exists(configPath)) {
        OX_LOG("[Config] Creating default config.json: %s\n", configPath.string().c_str());
        json cfg = R"({
            "player": {
                "volume": 80,
                "playMode": "listLoop",
                "equalizer": { "enable": false, "preset": "classical" },
                "crossfade": { "enable": false, "duration": 3 }
            },
            "playlists": [],
            "ui": {
                "theme": "dark",
                "language": "zh-CN",
                "showLyrics": true,
                "miniMode": false,
                "backgroundBlur": 30
            },
            "shortcuts": {
                "playPause": "Space",
                "nextTrack": "Ctrl+Right",
                "prevTrack": "Ctrl+Left",
                "volumeUp": "Ctrl+Up",
                "volumeDown": "Ctrl+Down"
            },
            "lastPlayed": {
                "playlistIndex": 0,
                "songIndex": 0,
                "position": 0.0
            }
        })"_json;

        std::ofstream ofs(configPath);
        if (ofs) {
            ofs << cfg.dump(4);
            OX_LOG("[Config] Default config.json created successfully\n");
        } else {
            OX_LOG("[Config] ERROR: Failed to write config.json\n");
        }
    } else {
        OX_LOG("[Config] config.json found: %s\n", configPath.string().c_str());
    }
}

/*==================== 歌单读写 ====================*/
// 将 ; 分隔的目录字符串拆分为 vector，并去掉每个路径两端的引号和空白
static std::vector<std::string> splitDirs(const std::string& raw) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t end = raw.find(';', start);
        if (end == std::string::npos) end = raw.size();
        std::string part = raw.substr(start, end - start);
        size_t s = 0, e = part.size();
        while (s < e && (part[s] == ' ' || part[s] == '\t' || part[s] == '"' || part[s] == '\'')) ++s;
        while (e > s && (part[e - 1] == ' ' || part[e - 1] == '\t' || part[e - 1] == '"' || part[e - 1] == '\'')) --e;
        if (s < e) result.push_back(normalizePath(part.substr(s, e - s)));
        start = end + 1;
    }
    return result;
}

static std::string getTodayDate() {
    time_t now = time(nullptr);
    tm t{};
    localtime_s(&t, &now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return buf;
}

static std::filesystem::path getConfigPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::filesystem::path(exePath).parent_path() / "MData" / "config.json";
}

static void loadPlaylists(AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        if (cfg.contains("playlists") && cfg["playlists"].is_array()) {
            st.playlists.clear();
            for (const auto& jpl : cfg["playlists"]) {
                PlaylistInfo pl;
                pl.name        = jpl.value("name", "");
                pl.createDate  = jpl.value("createDate", "");
                pl.type        = jpl.value("type", "builtin");
                pl.directoryPath = jpl.value("directoryPath", "");
                // 清理旧数据：去掉引号并统一分隔符
                {
                    auto dirs = splitDirs(pl.directoryPath);
                    pl.directoryPath = "";
                    for (size_t i = 0; i < dirs.size(); ++i) {
                        if (i > 0) pl.directoryPath += ";";
                        pl.directoryPath += dirs[i];
                    }
                }
                pl.autoScan    = jpl.value("autoScan", true);
                pl.scanInterval = jpl.value("scanInterval", 3600);
                if (jpl.contains("songs") && jpl["songs"].is_array()) {
                    for (const auto& js : jpl["songs"]) {
                        SongInfo s;
                        s.title    = js.value("title", "");
                        s.artist   = js.value("artist", "");
                        s.album    = js.value("album", "");
                        s.filePath = normalizePath(js.value("filePath", ""));
                        s.duration = js.value("duration", 0.0f);
                        s.addDate  = js.value("addDate", "");
                        pl.songs.push_back(s);
                    }
                }
                st.playlists.push_back(pl);
            }
            OX_LOG("[Playlist] Loaded %zu playlists\n", st.playlists.size());
        }
    } catch (const std::exception& e) {
        OX_LOG("[Playlist] loadPlaylists error: %s\n", e.what());
    }
}

static void savePlaylists(const AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        // 读取完整 config → 仅替换 playlists 段 → 写回
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        ifs.close();

        cfg["playlists"] = json::array();
        for (const auto& pl : st.playlists) {
            json jpl;
            jpl["name"]         = pl.name;
            jpl["createDate"]   = pl.createDate;
            jpl["type"]         = pl.type;
            jpl["autoScan"]     = pl.autoScan;
            jpl["scanInterval"] = pl.scanInterval;
            jpl["directoryPath"] = pl.directoryPath;
            jpl["songs"]        = json::array();
            for (const auto& s : pl.songs) {
                json js;
                js["title"]    = s.title;
                js["artist"]   = s.artist;
                js["album"]    = s.album;
                js["filePath"] = s.filePath;
                js["duration"] = s.duration;
                js["addDate"]  = s.addDate;
                jpl["songs"].push_back(js);
            }
            cfg["playlists"].push_back(jpl);
        }

        std::ofstream ofs(configPath);
        ofs << cfg.dump(4);
        OX_LOG("[Playlist] Saved %zu playlists\n", st.playlists.size());
    } catch (const std::exception& e) {
        OX_LOG("[Playlist] savePlaylists error: %s\n", e.what());
    }
}

// 保存歌词显示开关设置
static void saveShowLyrics(const AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        ifs.close();

        cfg["ui"]["showLyrics"] = st.showLyrics;

        std::ofstream ofs(configPath);
        ofs << cfg.dump(4);
    } catch (const std::exception& e) {
        OX_LOG("[Config] saveShowLyrics error: %s\n", e.what());
    }
}

// 保存断点续播信息
static void saveLastPlayed(const AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        ifs.close();

        double pos = st.videoTime;
        cfg["lastPlayed"] = {
            {"playlistIndex", st.currentPlaylistIndex},
            {"songIndex",     st.currentFileIndex},
            {"position",      pos}
        };

        std::ofstream ofs(configPath);
        ofs << cfg.dump(4);
        OX_LOG("[LastPlayed] Saved: playlist=%d song=%d pos=%.1fs\n",
               st.currentPlaylistIndex, st.currentFileIndex, pos);
    } catch (const std::exception& e) {
        OX_LOG("[LastPlayed] save error: %s\n", e.what());
    }
}

/*==================== 读取并应用 config.json 配置 ====================*/
static void applyConfig(AppState& st) {
    namespace fs = std::filesystem;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path configPath = fs::path(exePath).parent_path() / "MData" / "config.json";

    if (!fs::exists(configPath)) {
        OX_LOG("[Config] applyConfig: config.json not found\n");
        return;
    }

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);

        // ---- player.volume (0~100 int → 0.0~1.0 float) ----
        if (cfg.contains("player") && cfg["player"].contains("volume")) {
            int vol = cfg["player"]["volume"].get<int>();
            st.volume = (float)std::clamp(vol, 0, 100) / 100.0f;
            OX_LOG("[Config] volume set to %d → %.2f\n", vol, st.volume);
        }

        // ---- player.playMode (string → PlayMode enum) ----
        if (cfg.contains("player") && cfg["player"].contains("playMode")) {
            std::string mode = cfg["player"]["playMode"].get<std::string>();
            if (mode == "singleLoop")      st.playMode = PlayMode::SingleLoop;
            else if (mode == "listLoop")   st.playMode = PlayMode::ListLoop;
            else if (mode == "random")     st.playMode = PlayMode::Random;
            else                           st.playMode = PlayMode::Sequential;
            OX_LOG("[Config] playMode set to %s → %s\n", mode.c_str(), getPlayModeText(st.playMode));
        }

        // ---- 后续扩展其他配置项统一在此处理 ----

        // ---- UI 配置 ----
        if (cfg.contains("ui")) {
            if (cfg["ui"].contains("showLyrics")) {
                st.showLyrics = cfg["ui"]["showLyrics"].get<bool>();
                OX_LOG("[Config] showLyrics = %s\n", st.showLyrics ? "true" : "false");
            }
        }

        // ---- 加载歌单 ----
        loadPlaylists(st);

        // ---- 读取断点续播信息 ----
        if (cfg.contains("lastPlayed")) {
            st.resumePlaylistIndex = cfg["lastPlayed"].value("playlistIndex", -1);
            st.resumeSongIndex     = cfg["lastPlayed"].value("songIndex", -1);
            st.resumePosition      = cfg["lastPlayed"].value("position", 0.0);
            OX_LOG("[LastPlayed] Read: playlist=%d song=%d pos=%.1fs\n",
                   st.resumePlaylistIndex, st.resumeSongIndex, st.resumePosition);
        }

        OX_LOG("[Config] applyConfig done\n");
    } catch (const std::exception& e) {
        OX_LOG("[Config] applyConfig parse error: %s\n", e.what());
    }
}

/*==================== MAIN ====================*/
int main() {
    ensureConfig();
    AllocConsole();
    SetConsoleOutputCP(65001);
    OX::Core::initConsole();
    // 屏蔽 FFmpeg stderr 输出（h264 内部警告等），避免泄露库信息
    freopen("NUL", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered for crash diagnostics
    timeBeginPeriod(1);

    AppState st;
    applyConfig(st);
    const char* videoPath = "e:/MX/Data/tvb1.mp4";

    // ---------- 1. Window ----------
    OX::WindowDesc desc;
    desc.title = L"MPlayer";
    desc.width = 1920;
    desc.height = 1080;
	desc.style = OX::WindowStyle::Borderless;
    desc.enableOpenGL = true; 
    desc.stencilBits = 0;
    if (!st.app.create(&desc)) { 
        OX_LOG("[MAIN] Window create failed\n"); 
        return 1; 
    }
    uint32_t width, height; 
    st.app.getSize(&width, &height);

	// ---------- 1.5. VSync off ----------
    st.app.registerHotKey(HOTKEY_TOGGLE, 0, HOTKEY_VK);

    std::function<void()> toggleWallpaperMode = [&st]() {
        if (st.app.isWallpaperMode()) {
            if (st.app.exitWallpaperMode()) {
                st.app.setTitle(L"MPlayer - 窗口模式");
            }
        }
        else {
            if (st.app.enterWallpaperMode()) {
                st.app.setTitle(L"MPlayer - 壁纸模式");
            }
        }
    };

    st.app.setHotKeyCallback([&](int id) {
        if (id == HOTKEY_TOGGLE) {
            toggleWallpaperMode();
        }
    });


    // ---------- 2. Engine ----------
    auto* platform = new NoSwapWGLPlatform();
    st.engine = Engine::create(Engine::Backend::OPENGL, platform, nullptr);
    if (!st.engine) { OX_LOG("[MAIN] Engine failed\n"); st.app.destroy(); timeEndPeriod(1); return 1; }
    st.swapChain = st.engine->createSwapChain((void*)st.app.getHwnd());
    st.renderer = st.engine->createRenderer();
    st.renderer->setClearOptions({.clearColor={0,0,0,1}, .clear=true});

    // ---------- 3. Scene & View ----------
    st.scene = st.engine->createScene();
    st.view = st.engine->createView();
    st.view->setScene(st.scene);
    st.view->setViewport({0,0,width,height});
    st.view->setPostProcessingEnabled(false);

    st.camEntity = EntityManager::get().create();
    st.cam = st.engine->createCamera(st.camEntity);
    st.cam->setProjection(Camera::Projection::ORTHO, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);
    st.view->setCamera(st.cam);

    // ---------- 4. Scan media files ----------
    std::vector<std::string> scanDirs {kScanDirs[0], kScanDirs[1]};
    scanMediaFiles(st.mediaFiles, scanDirs);
    OX_LOG("[MAIN] Initial scan: %zu files from %s, %s\n", st.mediaFiles.size(), kScanDirs[0], kScanDirs[1]);
    // Insert test FLAC as first item
    if (std::filesystem::exists(kDefaultFlac)) {
        for (size_t i = 0; i < st.mediaFiles.size(); i++) {
            if (st.mediaFiles[i] == kDefaultFlac) {
                if (i > 0) { std::swap(st.mediaFiles[0], st.mediaFiles[i]); }
                break;
            }
        }
    }
    // Insert tvb1.mp4 as second item
    if (std::filesystem::exists(kDefaultMp4)) {
        for (size_t i = 0; i < st.mediaFiles.size(); i++) {
            if (st.mediaFiles[i] == kDefaultMp4 && i != 1) {
                std::swap(st.mediaFiles[1], st.mediaFiles[i]);
                break;
            }
        }
    }
    if (!st.mediaFiles.empty()) {
        st.currentFileIndex = 0;
        videoPath = st.mediaFiles[st.currentFileIndex].c_str();
    }

    // ---------- 5. Material ----------
    filamat::MaterialBuilder::init();
    st.mat = buildVideoMaterial(st.engine);
    if (!st.mat) {
        OX_LOG("[MAIN] Material build failed\n");
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    st.mi = st.mat->createInstance();

    // ---------- Build FX materials (all effects, unified) ----------
    int count = (g_fxEffectCount < AppState::MAX_FX) ? g_fxEffectCount : AppState::MAX_FX;
    for (int i = 0; i < count; i++) {
        // HeartfeltRain uses OPAQUE (renders in opaque pass, handles lyrics internally).
        // Other FX use TRANSPARENT (renders in transparent pass after lyrics entity).
        bool isRain = (g_fxEffects[i].code == FX::HeartfeltRain);
        auto blend = isRain ? filamat::MaterialBuilder::BlendingMode::OPAQUE
                            : filamat::MaterialBuilder::BlendingMode::TRANSPARENT;
        st.fxMat[i] = buildShaderToyFXMaterial(st.engine, g_fxEffects[i].name, g_fxEffects[i].code, blend);
        if (st.fxMat[i]) {
            st.fxMi[i] = st.fxMat[i]->createInstance();
            st.fxMi[i]->setParameter("iResolution", float2{float(width), float(height)});
            st.fxMi[i]->setParameter("iTime", 0.0f);
            st.fxMi[i]->setParameter("iMouseXY", float2{0, 0});
            st.fxMi[i]->setParameter("iMouseZW", float2{0, 0});
            st.fxEffectCount++;
        }
    }
    // Create single FX quad, bind first compiled effect's MI
    if (st.fxEffectCount > 0) {
        st.fxEntity = createFullscreenQuad(st.engine, st.scene, st.fxMi[0]);
        if (st.fxEntity) {
            auto& rcm = st.engine->getRenderableManager();
            auto fxi = rcm.getInstance(st.fxEntity);
            rcm.setLayerMask(fxi, 0xFF, 0x02);
            rcm.setPriority(fxi, 2);
        }
    }

    // ---------- 6. Open video ----------
    if (!loadMediaFile(st, videoPath, (int)width, (int)height)) {
        OX_LOG("[MAIN] Failed to open video: %s\n", videoPath);
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }

    st.quadEntity = createFullscreenQuad(st.engine, st.scene, st.mi);
    updateVideoTransform(st.engine, st.quadEntity, (int)width, (int)height, st.videoW, st.videoH);

    // Video quad: layer 0x01 (bit 0), priority 0 (renders first, 背景层)
    {
        auto& rcm = st.engine->getRenderableManager();
        auto vi = rcm.getInstance(st.quadEntity);
        rcm.setLayerMask(vi, 0xFF, 0x01);
        rcm.setPriority(vi, 0);
    }
    // Initially only show video layer (FX off)
    updateVisibleLayers(st);

    // ---------- Build transparent fill (for 关闭背景) ----------
    st.bgMat = buildTransparentFillMaterial(st.engine);
    if (st.bgMat) {
        st.bgMi = st.bgMat->createInstance();
        st.bgEntity = createFullscreenQuad(st.engine, st.scene, st.bgMi);
        // Transparent fill: layer 0x04 (bit 2), priority 3 (renders on top)
        if (st.bgEntity) {
            auto& rcm = st.engine->getRenderableManager();
            auto bgi = rcm.getInstance(st.bgEntity);
            rcm.setLayerMask(bgi, 0xFF, 0x04);
            rcm.setPriority(bgi, 3);
        }
    }

    // ---------- Build lyrics overlay (between video and FX) ----------
    st.lyricsMat = buildLyricsMaterial(st.engine);
    OX_LOG("[LRC] buildLyricsMaterial returned %p\n", (void*)st.lyricsMat);
    if (st.lyricsMat) {
        st.lyricsMi = st.lyricsMat->createInstance();
        auto smp = TextureSampler(TextureSampler::MinFilter::LINEAR, TextureSampler::MagFilter::LINEAR);
        if (st.lrcFilamentTex) st.lyricsMi->setParameter("lyricsTex", st.lrcFilamentTex, smp);
        st.lyricsEntity = createFullscreenQuad(st.engine, st.scene, st.lyricsMi);
        OX_LOG("[LRC] lyricsEntity=%u\n", (unsigned)st.lyricsEntity.getId());
        if (st.lyricsEntity) {
            auto& rcm = st.engine->getRenderableManager();
            auto li = rcm.getInstance(st.lyricsEntity);
            rcm.setLayerMask(li, 0xFF, 0x08);
            rcm.setPriority(li, 1);
        }
    }
    // Refresh layer visibility mask with all entities created
    updateVisibleLayers(st);

    // Diagnostic dump: all lyrics-related state after init
    OX_LOG("[LRC] === POST-INIT STATE DUMP ===\n");
    OX_LOG("[LRC]   lyricsMat=%p  lyricsMi=%p  lyricsEntity=%u\n",
           (void*)st.lyricsMat, (void*)st.lyricsMi, (unsigned)st.lyricsEntity.getId());
    OX_LOG("[LRC]   lrcTexId=%u  lrcFboId=%u  lrcFilamentTex=%p  lrcGlCanvas=%p\n",
           st.lrcTexId, st.lrcFboId, (void*)st.lrcFilamentTex, (void*)st.lrcGlCanvas);
    OX_LOG("[LRC]   lrcFboW=%d  lrcFboH=%d  lrcFboDirty=%d  showLyrics=%d  fxEffectCount=%d\n",
           st.lrcFboW, st.lrcFboH, st.lrcFboDirty, st.showLyrics, st.fxEffectCount);
    OX_LOG("[LRC]   fxEnabled=%d  fxEffectIndex=%d  lrcLines=%zu\n",
           st.fxEnabled, st.fxEffectIndex, st.lrcLines.size());

    // ---------- 7. Decode first frame ----------
    if (st.decoder.hasVideo()) {
        double firstPts = 0.0;
        const uint8_t* firstFrame = st.decoder.getFirstFrame(firstPts, st.audioQueue);
        if (firstFrame) {
            st.currentVideoPts = firstPts;
            st.hasFrame = true;
            uploadVideoFrame(st, firstFrame, st.videoW, st.videoH);
        } else {
            OX_LOG("[MAIN] Failed to decode first frame\n");
        }
    }

    // ---------- 8. Init miniaudio ----------
    ma_device_config aConfig = ma_device_config_init(ma_device_type_playback);
    aConfig.playback.format   = ma_format_f32;
    aConfig.playback.channels = st.AUDIO_CHANNELS;
    aConfig.sampleRate        = st.AUDIO_SAMPLE_RATE;
    aConfig.dataCallback      = ma_data_callback;
    aConfig.pUserData         = &st;
    if (ma_device_init(nullptr, &aConfig, &st.audioDevice) != MA_SUCCESS) {
        OX_LOG("[MAIN] miniaudio device init failed\n");
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    ma_device_set_master_volume(&st.audioDevice, st.volume);

    // ---------- 8. ThorVG / OUI ----------
    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        OX_LOG("[MAIN] ThorVG init failed\n");
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    bool fontLoaded = OX::UIManager::loadFont("siyuan", kFontPath);
    const char* fontName = fontLoaded ? "siyuan" : "Arial";
    if (!st.ui.init(st.app.getGLRC(), (int)width, (int)height)) {
        OX_LOG("[MAIN] OUI init failed\n");
        tvg::Initializer::term(); cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }

    // Create lyrics FBO (full-screen, same size as window)
    createLrcFbo(st, (int)width, (int)height);

    float leftX = 20.0f, leftY = 20.0f, panelW = 280.0f;
    std::function<void()> rebuildUI;
    rebuildUI = [&]() {
        st.ui.clearElements();
        st.fpsLabelPtr = st.infoLabelPtr = st.timeLabelPtr = st.statusLabelPtr = st.syncLabelPtr = st.rightInfoLabelPtr = nullptr;
        st.playBtnPtr = st.loopBtnPtr = st.prevBtnPtr = st.nextBtnPtr = nullptr;
        st.seekBarPtr = st.volumeSliderPtr = nullptr;
        st.fileDropdownPtr = nullptr;
        st.playlistDropdownPtr = nullptr;
        st.fxBtnPtr = nullptr;
        st.fxDropdownPtr = nullptr;
        st.bgBtnPtr = nullptr;
        st.lrcPanelPtr = nullptr; st.lyricsBtnPtr = nullptr;
        for (auto& p : st.lrcLabels) p = nullptr;
        float curY = leftY;

        auto addL = [&](const char* t, float h, OX::OColor c, OX::UILabel** o = nullptr) {
            auto l = std::make_unique<OX::UILabel>(t);
            l->rect = OX::ORect(leftX, curY, panelW, h);
            l->fontSize = 14.0f; l->fontName = fontName; l->textColor = c;
            if (o) *o = l.get(); st.ui.addElement(std::move(l)); curY += h + 6.0f;
        };
        auto addB = [&](const char* t, std::function<void()> cb, float h = 32.0f, OX::UIButton** o = nullptr) {
            auto b = std::make_unique<OX::UIButton>(t);
            b->rect = OX::ORect(leftX, curY, panelW, h);
            b->fontName = fontName; b->fontSize = 11.0f; b->onClick = cb;
            if (o) *o = b.get(); st.ui.addElement(std::move(b)); curY += h + 4.0f;
        };

        addL("TEST-Filament-Player", 28.0f, OX::OColor(100, 200, 255, 255));
        addL(st.infoText, 22.0f, OX::OColor(255, 255, 200, 255), &st.infoLabelPtr);
        addL(st.statusText, 22.0f, OX::OColor(255, 200, 100, 255), &st.statusLabelPtr);
        addL(st.timeText, 22.0f, OX::OColor(200, 255, 200, 255), &st.timeLabelPtr);
        addL(st.syncText, 22.0f, OX::OColor(200, 200, 255, 255), &st.syncLabelPtr);
        addL(st.frameText, 22.0f, OX::OColor(255, 255, 255, 255), &st.frameLabelPtr);

        auto playPauseCb = [&st, &rebuildUI]() {
            if (st.isPlaying) {
                st.isPlaying = false;
                st.isPaused = true;
                snprintf(st.statusText, sizeof(st.statusText), "Paused");
                if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
            } else {
                st.isPlaying = true;
                st.isPaused = false;
                snprintf(st.statusText, sizeof(st.statusText), "Playing");
                if (!st.hasFrame) {
                    st.decoder.seek(0);
                    OX_LOG("[Audio] audioQueue.clear() from playPauseCb\n");
                    st.audioQueue.clear();
                    st.audioSamplesPlayed = 0;
                    double pts;
                    const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                    if (f) {
                        st.currentVideoPts = pts;
                        st.hasFrame = true;
                        uploadVideoFrame(st, f, st.videoW, st.videoH);
                    }
                }
                st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                if (!st.audioStarted) {
                    ma_result maRes = ma_device_start(&st.audioDevice);
                    if (maRes != MA_SUCCESS) {
                        OX_LOG("[Audio] ma_device_start failed: %d\n", maRes);
                    } else {
                        st.audioStarted = true;
                    }
                }
            }
            if (st.playBtnPtr) st.playBtnPtr->setText(st.isPlaying ? "Pause" : "Play");
            if (st.statusLabelPtr) st.statusLabelPtr->setText(st.statusText);
        };
        // ---------- Play/Pause + Stop (dual column) ----------
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            // Pause button (left)
            auto pauseBtn = std::make_unique<OX::UIButton>(st.isPlaying ? "Pause" : "Play");
            pauseBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            pauseBtn->fontName = fontName; pauseBtn->fontSize = 14.0f; pauseBtn->onClick = playPauseCb;
            st.playBtnPtr = pauseBtn.get(); st.ui.addElement(std::move(pauseBtn));
            // Stop button (right)
            auto stopBtn = std::make_unique<OX::UIButton>("Stop");
            stopBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            stopBtn->fontName = fontName; stopBtn->fontSize = 14.0f;
            stopBtn->onClick = [&st]() {
            st.isPlaying = false;
            st.isPaused = false;
            st.videoTime = 0.0;
            snprintf(st.statusText, sizeof(st.statusText), "Stopped");
            if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
            OX_LOG("[Audio] audioQueue.clear() from StopBtn\n");
            st.audioQueue.clear();
            st.audioSamplesPlayed = 0;
            st.decoder.seek(0);
            double pts;
            const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
            if (f) {
                st.currentVideoPts = pts;
                st.hasFrame = true;
                uploadVideoFrame(st, f, st.videoW, st.videoH);
            }
            if (st.playBtnPtr) st.playBtnPtr->setText("Play");
            if (st.statusLabelPtr) st.statusLabelPtr->setText(st.statusText);
            };
            st.ui.addElement(std::move(stopBtn));
            curY += 32.0f + 4.0f;
        }

        // ---------- Prev / Next (dual column) ----------
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            auto prevCb = [&st]() { switchFile(st, -1); };
            auto nextCb = [&st]() { switchFile(st, +1); };
            auto prevBtn = std::make_unique<OX::UIButton>("< Prev");
            prevBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            prevBtn->fontName = fontName; prevBtn->fontSize = 11.0f; prevBtn->onClick = prevCb;
            st.prevBtnPtr = prevBtn.get(); st.ui.addElement(std::move(prevBtn));
            auto nextBtn = std::make_unique<OX::UIButton>("Next >");
            nextBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            nextBtn->fontName = fontName; nextBtn->fontSize = 11.0f; nextBtn->onClick = nextCb;
            st.nextBtnPtr = nextBtn.get(); st.ui.addElement(std::move(nextBtn));
            curY += 32.0f + 4.0f;
        }

        // ---------- Screenshot buttons ----------
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            auto winShotCb = [&st]() {
                uint32_t w, h;
                st.app.getSize(&w, &h);
                if (w == 0 || h == 0) return;
                std::vector<uint8_t> pixels(w * h * 4);
                glReadPixels(0, 0, (int)w, (int)h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                std::vector<uint8_t> flipped(w * h * 4);
                for (uint32_t y = 0; y < h; y++) {
                    memcpy(&flipped[y * w * 4], &pixels[(h - 1 - y) * w * 4], w * 4);
                }
                stbi_write_png("dump_window.png", (int)w, (int)h, 4, flipped.data(), (int)w * 4);
                OX_LOG("[Dump] Saved window screenshot: dump_window.png\n");
            };
            auto frameShotCb = [&st]() {
                if (!st.decoder.hasVideo() || !st.hasFrame) return;
                int frameNum = (int)(st.videoTime * st.decoder.getFps() + 0.5);
                char pngName[256];
                snprintf(pngName, sizeof(pngName), "dump_frame_%04d_%.3f.png", frameNum, st.videoTime);
                const uint8_t* framePtr = st.decoder.getCurrentFramePtr();
                if (framePtr) {
                    stbi_write_png(pngName, st.videoW, st.videoH, 4, framePtr, st.videoW * 4);
                    OX_LOG("[Dump] Saved frame screenshot: %s\n", pngName);
                }
                if (st.currentFileIndex >= 0 && st.currentFileIndex < (int)st.mediaFiles.size()) {
                    char ffmpegCmd[1024];
                    snprintf(ffmpegCmd, sizeof(ffmpegCmd),
                             "start /B \"\" \"%s\" -y -ss %.3f -i \"%s\" -vframes 1 -q:v 2 dump_ffmpeg_%04d_%.3f.png >nul 2>&1",
                             kFfmpegPath, st.videoTime, st.mediaFiles[st.currentFileIndex].c_str(), frameNum, st.videoTime);
                    std::thread([ffmpegCmd]() { system(ffmpegCmd); }).detach();
                    OX_LOG("[Dump] FFmpeg screenshot launched\n");
                }
            };
            auto winShotBtn = std::make_unique<OX::UIButton>("Window Shot");
            winShotBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            winShotBtn->fontName = fontName; winShotBtn->fontSize = 11.0f; winShotBtn->onClick = winShotCb;
            st.ui.addElement(std::move(winShotBtn));
            auto frameShotBtn = std::make_unique<OX::UIButton>("Frame Shot");
            frameShotBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            frameShotBtn->fontName = fontName; frameShotBtn->fontSize = 11.0f; frameShotBtn->onClick = frameShotCb;
            st.ui.addElement(std::move(frameShotBtn));
            curY += 32.0f + 4.0f;
        }

        // ---------- FX特效选择 ----------
        if (st.fxEffectCount > 0) {
            // Label
            auto fxLabel = std::make_unique<OX::UILabel>("FX特效");
            fxLabel->rect = OX::ORect(leftX, curY, panelW, 26.0f);
            fxLabel->fontSize = 14.0f; fxLabel->fontName = fontName;
            st.ui.addElement(std::move(fxLabel));
            curY += 26.0f + 2.0f;

            // Dropdown - gather successfully compiled effect names
            auto dd = std::make_unique<OX::UIDropdown>("选择特效...");
            dd->rect = OX::ORect(leftX, curY, 180.0f, 32.0f);
            dd->fontName = fontName; dd->fontSize = 12.0f;
            for (int i = 0; i < st.fxEffectCount; i++) {
                dd->options.push_back(g_fxEffects[i].name);
            }
            dd->selectedIndex = st.fxEffectIndex;
            dd->onSelectionChanged = [&st](int idx, const std::string&) {
                if (idx < 0 || idx >= st.fxEffectCount) return;
                st.fxEffectIndex = idx;
                st.fxTime = 0.0f;
                updateVisibleLayers(st);  // rain FX must hide separate lyrics entity
                if (st.fxEntity && st.fxMi[idx]) {
                    auto& rcm = st.engine->getRenderableManager();
                    auto fxi = rcm.getInstance(st.fxEntity);
                    rcm.setMaterialInstanceAt(fxi, 0, st.fxMi[idx]);
                }
            };
            st.fxDropdownPtr = dd.get();
            st.ui.addElement(std::move(dd));
            curY += 32.0f + 4.0f;

            // Toggle button
            addB(st.fxEnabled ? "关闭FX" : "打开FX", [&st]() {
                st.fxEnabled = !st.fxEnabled;
                updateVisibleLayers(st);
                if (st.fxBtnPtr) st.fxBtnPtr->setText(st.fxEnabled ? "关闭FX" : "打开FX");
            }, 32.0f, &st.fxBtnPtr);
        }

        // ---------- 显示/关闭背景 ----------
        if (st.bgMat) {
            addB(st.showBackground ? "关闭背景" : "显示背景", [&st]() {
                st.showBackground = !st.showBackground;
                updateVisibleLayers(st);
                if (st.bgBtnPtr) st.bgBtnPtr->setText(st.showBackground ? "关闭背景" : "显示背景");
            }, 32.0f, &st.bgBtnPtr);
        }

        // ---------- 显示/关闭歌词 ----------
        addB(st.showLyrics ? "关闭歌词" : "显示歌词", [&st]() {
            st.showLyrics = !st.showLyrics;
            st.lrcFboDirty = true;
            updateVisibleLayers(st);
            saveShowLyrics(st);
            if (st.lyricsBtnPtr) st.lyricsBtnPtr->setText(st.showLyrics ? "关闭歌词" : "显示歌词");
        }, 32.0f, &st.lyricsBtnPtr);

        addB(getPlayModeText(st.playMode), [&st]() {
            // 循环切换: 单曲循环 -> 列表循环 -> 顺序播放 -> 随机播放 -> 单曲循环
            switch (st.playMode) {
                case PlayMode::SingleLoop: st.playMode = PlayMode::ListLoop; break;
                case PlayMode::ListLoop:   st.playMode = PlayMode::Sequential; break;
                case PlayMode::Sequential: st.playMode = PlayMode::Random; break;
                case PlayMode::Random:     st.playMode = PlayMode::SingleLoop; break;
            }
            if (st.loopBtnPtr) st.loopBtnPtr->setText(getPlayModeText(st.playMode));
        }, 32.0f, &st.loopBtnPtr);

        // ---------- Progress bar ----------
        if (st.decoder.getDuration() > 0) {
            curY += 4.0f;
            auto seekBar = std::make_unique<OX::UISlider>();
            seekBar->rect = OX::ORect(leftX, curY, panelW, 24.0f);
            seekBar->minValue = 0.0f;
            seekBar->maxValue = (float)st.decoder.getDuration();
            seekBar->value = (float)st.videoTime;
            seekBar->onValueChanged = [&st](float val) {
                if (std::abs(val - (float)st.videoTime) > 1.0f) {
                    st.pendingSeek = true;
                    st.pendingSeekTarget = (double)val;
                }
            };
            st.seekBarPtr = seekBar.get();
            st.ui.addElement(std::move(seekBar));
            curY += 24.0f + 4.0f;
        }

        // ---------- Volume control ----------
        {
            curY += 4.0f;
            auto volSlider = std::make_unique<OX::UISlider>();
            volSlider->rect = OX::ORect(leftX, curY, panelW, 24.0f);
            volSlider->minValue = 0.0f;
            volSlider->maxValue = 1.0f;
            volSlider->value = st.volume;
            volSlider->onValueChanged = [&st](float val) {
                st.volume = val;
                ma_device_set_master_volume(&st.audioDevice, val);
            };
            st.volumeSliderPtr = volSlider.get();
            st.ui.addElement(std::move(volSlider));
            curY += 24.0f + 4.0f;
        }

        addL(st.fpsText, 22.0f, OX::OColor(0, 255, 0, 255), &st.fpsLabelPtr);

        // ====== 歌单切换 ======
        if (!st.playlists.empty()) {
            curY += 8.0f;
            auto plDD = std::make_unique<OX::UIDropdown>("选择歌单...");
            plDD->rect = OX::ORect(leftX, curY, panelW, 36.0f);
            plDD->fontName = fontName; plDD->fontSize = 12.0f;
            for (const auto& pl : st.playlists) {
                plDD->options.push_back(pl.name);
            }
            plDD->selectedIndex = st.currentPlaylistIndex;
            plDD->onSelectionChanged = [&st](int idx, const std::string&) {
                if (idx < 0 || idx >= (int)st.playlists.size()) return;
                st.currentPlaylistIndex = idx;
                auto& pl = st.playlists[idx];
                OX_LOG("[UI] Playlist switched to [%d] %s (type=%s)\n", idx, pl.name.c_str(), pl.type.c_str());
                // 填充 mediaFiles
                st.mediaFiles.clear();
                st.currentFileIndex = -1;
                if (pl.type == "builtin") {
                    for (const auto& s : pl.songs)
                        st.mediaFiles.push_back(s.filePath);
                    OX_LOG("[UI] builtin playlist: %zu songs loaded\n", st.mediaFiles.size());
                } else {
                    // localDir: 扫描目录 (支持 ; 分隔多目录)
                    std::vector<std::string> dirs = splitDirs(pl.directoryPath);
                    scanMediaFiles(st.mediaFiles, dirs);
                    OX_LOG("[UI] localDir scan %zu dirs: '%s', found %zu files\n",
                        dirs.size(), pl.directoryPath.c_str(), st.mediaFiles.size());
                }
                // 加载第一个文件
                if (!st.mediaFiles.empty()) {
                    OX_LOG("[UI] Auto-loading first file: %s\n", st.mediaFiles[0].c_str());
                    st.currentFileIndex = 0;
                    uint32_t w, h; st.app.getSize(&w, &h);
                    if (loadMediaFile(st, st.mediaFiles[0].c_str(), (int)w, (int)h)) {
                        if (st.decoder.hasVideo()) {
                            double pts;
                            const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                            if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                        }
                        st.isPlaying = true; st.isPaused = false;
                        snprintf(st.statusText, sizeof(st.statusText), "Playing");
                        st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                        if (!st.audioStarted) {
                            if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                        }
                    }
                }
                st.needRebuildUI = true;
            };
            st.playlistDropdownPtr = plDD.get();
            st.ui.addElement(std::move(plDD));
            curY += 36.0f + 4.0f;
        }

        // ====== 新建歌单 / 添加歌曲 按钮 ======
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            auto newPlBtn = std::make_unique<OX::UIButton>("+ 新建歌单");
            newPlBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            newPlBtn->fontName = fontName; newPlBtn->fontSize = 11.0f;
            newPlBtn->onClick = [&st]() {
                st.showCreatePlaylistDlg = true;
                st.plDlgName[0] = '\0';
                st.plDlgType = 0;
                st.plDlgDir[0] = '\0';
                st.plDlgAutoScan = true;
                st.plDlgScanInterval = 3600;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(newPlBtn));

            auto addSongsBtn = std::make_unique<OX::UIButton>("+ 添加歌曲");
            addSongsBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            addSongsBtn->fontName = fontName; addSongsBtn->fontSize = 11.0f;
            addSongsBtn->onClick = [&st]() {
                if (st.currentPlaylistIndex < 0 || st.currentPlaylistIndex >= (int)st.playlists.size()) return;
                if (st.playlists[st.currentPlaylistIndex].type != "builtin") return;
                st.showAddSongsDlg = true;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(addSongsBtn));
            curY += 32.0f + 4.0f;
        }

        // ====== File Dropdown (当前歌单的文件列表) ======
        if (!st.mediaFiles.empty()) {
            OX_LOG("[UI] rebuildUI: showing %zu files (playlistIdx=%d, fileIdx=%d)\n",
                st.mediaFiles.size(), st.currentPlaylistIndex, st.currentFileIndex);
            curY += 4.0f;
            auto dd = std::make_unique<OX::UIDropdown>("选择文件...");
            dd->rect = OX::ORect(leftX, curY, panelW, 36.0f);
            dd->fontName = fontName; dd->fontSize = 12.0f;
            for (const auto& fp : st.mediaFiles) {
                size_t pos = fp.find_last_of("\\/");
                dd->options.push_back((pos != std::string::npos) ? fp.substr(pos + 1) : fp);
            }
            dd->selectedIndex = st.currentFileIndex;
            dd->onSelectionChanged = [&st](int idx, const std::string&) {
                if (idx < 0 || idx >= (int)st.mediaFiles.size()) return;
                st.currentFileIndex = idx;
                uint32_t w, h; st.app.getSize(&w, &h);
                if (loadMediaFile(st, st.mediaFiles[idx].c_str(), (int)w, (int)h)) {
                    if (st.decoder.hasVideo()) {
                        double pts;
                        const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                        if (f) {
                            st.currentVideoPts = pts;
                            st.hasFrame = true;
                            uploadVideoFrame(st, f, st.videoW, st.videoH);
                        }
                    }
                    st.isPlaying = true; st.isPaused = false;
                    snprintf(st.statusText, sizeof(st.statusText), "Playing");
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                    if (!st.audioStarted) {
                        ma_result maRes = ma_device_start(&st.audioDevice);
                        if (maRes != MA_SUCCESS) {
                            OX_LOG("[Audio] ma_device_start failed: %d\n", maRes);
                        } else {
                            st.audioStarted = true;
                        }
                    }
                } else {
                    snprintf(st.statusText, sizeof(st.statusText), "Load failed");
                }
                st.needRebuildUI = true;
            };
            st.fileDropdownPtr = dd.get();
            st.ui.addElement(std::move(dd));
        } else {
            OX_LOG("[UI] rebuildUI: mediaFiles empty, no file dropdown (playlistIdx=%d)\n", st.currentPlaylistIndex);
        }

        // ====== Right info panel ======
        updateRightInfo(st);
        auto infoR = std::make_unique<OX::UILabel>(st.rightInfoText);
        infoR->rect = OX::ORect((float)width - panelW - 20.0f, leftY, panelW, 240.0f);
        infoR->fontSize = 13.0f; infoR->fontName = fontName;
        infoR->textColor = OX::OColor(200, 220, 255, 255);
        infoR->setWrapMode(tvg::TextWrap::Word);
        st.rightInfoLabelPtr = infoR.get();
        st.ui.addElement(std::move(infoR));

        // ====== 歌词面板 (Lyrics FBO for FX refraction) ======
        if (st.showLyrics) {
            if (!st.lrcLines.empty()) {
                float lrcW = panelW * 2.0f;
                float lrcX = ((float)width - lrcW) / 2.0f;
                float lrcY = leftY + 248.0f;
                float lrcH = 340.0f;
                renderLyricsToFbo(st, lrcX, lrcY, lrcW, lrcH, fontName);
            }
            st.lrcFboDirty = true;  // force re-render (or clear) next frame
        }

        // ====== 顶部拖动区域 ======
        auto dragRegion = std::make_unique<OX::UIDragRegion>(st.app.getHwnd());
        dragRegion->rect = OX::ORect(0, 0, static_cast<float>(desc.width), 48.0f);
        st.ui.addElement(std::move(dragRegion));

        // ================================================================
        // 创建歌单对话框 (遮罩层在前，卡片在后 → 逆序派发时卡片优先)
        // ================================================================
        if (st.showCreatePlaylistDlg) {
            float dlgW = 440.0f, dlgH = 320.0f;
            float dlgX = ((float)width - dlgW) / 2.0f;
            float dlgY = ((float)height - dlgH) / 2.0f;

            // 遮罩 (先添加 → 渲染在下层; enabled=false → 不拦截点击)
            auto backdrop = std::make_unique<OX::UIFrame>();
            backdrop->rect = OX::ORect(0, 0, (float)width, (float)height);
            backdrop->bgColor = OX::OColor(0, 0, 0, 180);
            backdrop->borderWidth = 0;
            backdrop->enabled = false;
            st.ui.addElement(std::move(backdrop));

            // 卡片 (enabled=false → 仅视觉背景，不拦截子元素点击)
            auto card = std::make_unique<OX::UIFrame>();
            card->rect = OX::ORect(dlgX, dlgY, dlgW, dlgH);
            card->bgColor = OX::OColor(40, 42, 54, 255);
            card->borderColor = OX::OColor(80, 82, 100, 255);
            card->borderWidth = 1.0f;
            card->cornerRadius = 8.0f;
            card->enabled = false;
            st.ui.addElement(std::move(card));

            // 标题
            auto dlgTitle = std::make_unique<OX::UILabel>("创建歌单");
            dlgTitle->rect = OX::ORect(dlgX + 20.0f, dlgY + 16.0f, dlgW - 40.0f, 28.0f);
            dlgTitle->fontName = fontName; dlgTitle->fontSize = 18.0f;
            dlgTitle->textColor = OX::OColor(255, 255, 255, 255);
            st.ui.addElement(std::move(dlgTitle));

            // 名称输入
            float dlgCurY = dlgY + 56.0f;
            auto nameLabel = std::make_unique<OX::UILabel>("名称:");
            nameLabel->rect = OX::ORect(dlgX + 20.0f, dlgCurY, 60.0f, 24.0f);
            nameLabel->fontName = fontName; nameLabel->fontSize = 14.0f;
            nameLabel->textColor = OX::OColor(200, 200, 200, 255);
            st.ui.addElement(std::move(nameLabel));

            auto nameInput = std::make_unique<OX::UITextInput>();
            nameInput->rect = OX::ORect(dlgX + 80.0f, dlgCurY, dlgW - 100.0f, 30.0f);
            nameInput->fontName = fontName;
            nameInput->setText(st.plDlgName);
            nameInput->onTextChanged = [&st](const std::string& text) {
                strncpy_s(st.plDlgName, text.c_str(), sizeof(st.plDlgName) - 1);
            };
            st.ui.addElement(std::move(nameInput));
            dlgCurY += 40.0f;

            // 类型选择
            auto typeLabel = std::make_unique<OX::UILabel>("类型:");
            typeLabel->rect = OX::ORect(dlgX + 20.0f, dlgCurY, 60.0f, 24.0f);
            typeLabel->fontName = fontName; typeLabel->fontSize = 14.0f;
            typeLabel->textColor = OX::OColor(200, 200, 200, 255);
            st.ui.addElement(std::move(typeLabel));

            auto typeDD = std::make_unique<OX::UIDropdown>("");
            typeDD->rect = OX::ORect(dlgX + 80.0f, dlgCurY, dlgW - 100.0f, 30.0f);
            typeDD->fontName = fontName; typeDD->fontSize = 13.0f;
            typeDD->options = {"内置歌单", "本地目录"};
            typeDD->selectedIndex = st.plDlgType;
            typeDD->onSelectionChanged = [&st](int idx, const std::string&) {
                st.plDlgType = idx;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(typeDD));
            dlgCurY += 40.0f;

            // 本地目录专属字段
            if (st.plDlgType == 1) {
                auto dirLabel = std::make_unique<OX::UILabel>("目录:");
                dirLabel->rect = OX::ORect(dlgX + 20.0f, dlgCurY, 60.0f, 24.0f);
                dirLabel->fontName = fontName; dirLabel->fontSize = 14.0f;
                dirLabel->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(dirLabel));

                auto dirInput = std::make_unique<OX::UITextInput>();
                dirInput->rect = OX::ORect(dlgX + 80.0f, dlgCurY, dlgW - 100.0f, 30.0f);
                dirInput->fontName = fontName;
                dirInput->setText(st.plDlgDir);
                dirInput->onTextChanged = [&st](const std::string& text) {
                    strncpy_s(st.plDlgDir, text.c_str(), sizeof(st.plDlgDir) - 1);
                };
                st.ui.addElement(std::move(dirInput));
                dlgCurY += 44.0f;

                // 自动扫描 checkbox
                auto autoScanCB = std::make_unique<OX::UICheckbox>();
                autoScanCB->rect = OX::ORect(dlgX + 80.0f, dlgCurY, 24.0f, 24.0f);
                autoScanCB->setChecked(st.plDlgAutoScan);
                autoScanCB->onStateChanged = [&st](bool checked) {
                    st.plDlgAutoScan = checked;
                };
                st.ui.addElement(std::move(autoScanCB));

                auto autoScanLbl = std::make_unique<OX::UILabel>("自动扫描新文件");
                autoScanLbl->rect = OX::ORect(dlgX + 110.0f, dlgCurY, 160.0f, 24.0f);
                autoScanLbl->fontName = fontName; autoScanLbl->fontSize = 13.0f;
                autoScanLbl->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(autoScanLbl));
                dlgCurY += 34.0f;

                // 扫描间隔 slider
                auto intervalLabel = std::make_unique<OX::UILabel>("扫描间隔(秒):");
                intervalLabel->rect = OX::ORect(dlgX + 80.0f, dlgCurY, 120.0f, 24.0f);
                intervalLabel->fontName = fontName; intervalLabel->fontSize = 13.0f;
                intervalLabel->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(intervalLabel));

                auto intervalSlider = std::make_unique<OX::UISlider>();
                intervalSlider->rect = OX::ORect(dlgX + 80.0f, dlgCurY + 22.0f, dlgW - 100.0f, 20.0f);
                intervalSlider->minValue = 300.0f;
                intervalSlider->maxValue = 7200.0f;
                intervalSlider->value = (float)st.plDlgScanInterval;
                intervalSlider->step = 300.0f;
                intervalSlider->onValueChanged = [&st](float val) {
                    st.plDlgScanInterval = (int)val;
                };
                st.ui.addElement(std::move(intervalSlider));
                dlgCurY += 50.0f;

                dlgH = dlgCurY - dlgY + 60.0f;
            }

            // 按钮行
            float btnY = dlgY + dlgH - 48.0f;
            auto cancelBtn = std::make_unique<OX::UIButton>("取消");
            cancelBtn->rect = OX::ORect(dlgX + dlgW - 200.0f, btnY, 80.0f, 32.0f);
            cancelBtn->fontName = fontName; cancelBtn->fontSize = 13.0f;
            cancelBtn->onClick = [&st]() {
                st.showCreatePlaylistDlg = false;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(cancelBtn));

            auto createBtn = std::make_unique<OX::UIButton>("创建");
            createBtn->rect = OX::ORect(dlgX + dlgW - 110.0f, btnY, 80.0f, 32.0f);
            createBtn->fontName = fontName; createBtn->fontSize = 13.0f;
            createBtn->bgColor = OX::OColor(100, 180, 100, 255);
            createBtn->onClick = [&st]() {
                if (strlen(st.plDlgName) == 0) {
                    OX_LOG("[Playlist] Create failed: name is empty\n");
                    return;
                }
                PlaylistInfo pl;
                pl.name = st.plDlgName;
                pl.createDate = getTodayDate();
                pl.type = (st.plDlgType == 1) ? "localDir" : "builtin";
                if (pl.type == "localDir") {
                    // 用 ; 分隔多目录，去掉每个路径的引号
                    auto dirs = splitDirs(st.plDlgDir);
                    pl.directoryPath = "";
                    for (size_t i = 0; i < dirs.size(); ++i) {
                        if (i > 0) pl.directoryPath += ";";
                        pl.directoryPath += dirs[i];
                    }
                    pl.autoScan = st.plDlgAutoScan;
                    pl.scanInterval = st.plDlgScanInterval;
                } else {
                    pl.directoryPath = "";
                    pl.autoScan = false;
                    pl.scanInterval = 0;
                }
                // 如果是内置歌单，先清空 songs；localDir 则 songs 为空
                pl.songs.clear();
                st.playlists.push_back(pl);
                savePlaylists(st);
                OX_LOG("[Playlist] Created: name='%s' type=%s dir='%s' autoScan=%d interval=%d\n",
                    pl.name.c_str(), pl.type.c_str(), pl.directoryPath.c_str(), pl.autoScan, pl.scanInterval);
                // 自动切换到新歌单
                st.currentPlaylistIndex = (int)st.playlists.size() - 1;
                st.mediaFiles.clear();
                st.currentFileIndex = -1;
                if (pl.type == "localDir") {
                    std::vector<std::string> dirs = splitDirs(pl.directoryPath);
                    scanMediaFiles(st.mediaFiles, dirs);
                    OX_LOG("[Playlist] Scanned %zu dirs: '%s', found %zu files\n",
                        dirs.size(), pl.directoryPath.c_str(), st.mediaFiles.size());
                }
                st.showCreatePlaylistDlg = false;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(createBtn));
        }

        // ================================================================
        // 添加歌曲对话框
        // ================================================================
        if (st.showAddSongsDlg && st.currentPlaylistIndex >= 0 && st.currentPlaylistIndex < (int)st.playlists.size()) {
            float dlgW = 460.0f, dlgH = 240.0f;
            float dlgX = ((float)width - dlgW) / 2.0f;
            float dlgY = ((float)height - dlgH) / 2.0f;

            // 遮罩 (先添加 → 渲染在下层; enabled=false → 不拦截点击)
            auto addBackdrop = std::make_unique<OX::UIFrame>();
            addBackdrop->rect = OX::ORect(0, 0, (float)width, (float)height);
            addBackdrop->bgColor = OX::OColor(0, 0, 0, 180);
            addBackdrop->borderWidth = 0;
            addBackdrop->enabled = false;
            st.ui.addElement(std::move(addBackdrop));

            // 卡片 (enabled=false → 仅视觉背景)
            auto addCard = std::make_unique<OX::UIFrame>();
            addCard->rect = OX::ORect(dlgX, dlgY, dlgW, dlgH);
            addCard->bgColor = OX::OColor(40, 42, 54, 255);
            addCard->borderColor = OX::OColor(80, 82, 100, 255);
            addCard->borderWidth = 1.0f;
            addCard->cornerRadius = 8.0f;
            addCard->enabled = false;
            st.ui.addElement(std::move(addCard));

            // 标题
            char addTitle[128];
            snprintf(addTitle, sizeof(addTitle), "添加歌曲到 %s", st.playlists[st.currentPlaylistIndex].name.c_str());
            auto addTitleLbl = std::make_unique<OX::UILabel>(addTitle);
            addTitleLbl->rect = OX::ORect(dlgX + 20.0f, dlgY + 16.0f, dlgW - 40.0f, 28.0f);
            addTitleLbl->fontName = fontName; addTitleLbl->fontSize = 16.0f;
            addTitleLbl->textColor = OX::OColor(255, 255, 255, 255);
            st.ui.addElement(std::move(addTitleLbl));

            // 从扫描池中筛选未在歌单中的文件
            auto& pl = st.playlists[st.currentPlaylistIndex];
            std::vector<std::string> pool;
            // 先用全局扫描填充 pool
            scanMediaFiles(pool, std::vector<std::string>{kScanDirs[0], kScanDirs[1]});
            // 移除已在歌单中的
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&pl](const std::string& fp) {
                for (const auto& s : pl.songs) {
                    if (s.filePath == fp) return true;
                }
                return false;
            }), pool.end());

            if (!pool.empty()) {
                st.addSongSelIdx = -1;  // 每次打开对话框重置选择
                auto addFileDD = std::make_unique<OX::UIDropdown>("选择要添加的文件...");
                addFileDD->rect = OX::ORect(dlgX + 20.0f, dlgY + 60.0f, dlgW - 40.0f, 36.0f);
                addFileDD->fontName = fontName; addFileDD->fontSize = 12.0f;
                addFileDD->options.clear();
                for (const auto& fp : pool) {
                    size_t pos = fp.find_last_of("\\/");
                    addFileDD->options.push_back((pos != std::string::npos) ? fp.substr(pos + 1) : fp);
                }
                addFileDD->selectedIndex = (st.addSongSelIdx >= 0 && st.addSongSelIdx < (int)pool.size()) ? st.addSongSelIdx : 0;
                addFileDD->onSelectionChanged = [&st](int idx, const std::string&) {
                    st.addSongSelIdx = idx;
                };
                st.ui.addElement(std::move(addFileDD));

                // 添加按钮
                auto addBtn = std::make_unique<OX::UIButton>("添加");
                addBtn->rect = OX::ORect(dlgX + dlgW - 210.0f, dlgY + dlgH - 48.0f, 80.0f, 32.0f);
                addBtn->fontName = fontName; addBtn->fontSize = 13.0f;
                addBtn->bgColor = OX::OColor(100, 180, 100, 255);
                addBtn->onClick = [&st, pool]() {
                    if (st.addSongSelIdx < 0 || st.addSongSelIdx >= (int)pool.size()) return;
                    const std::string& fp = pool[st.addSongSelIdx];
                    // 检查是否已在歌单中
                    auto& songs = st.playlists[st.currentPlaylistIndex].songs;
                    for (const auto& s : songs) {
                        if (s.filePath == fp) return;
                    }
                    SongInfo si;
                    size_t pos = fp.find_last_of("\\/");
                    si.title = (pos != std::string::npos) ? fp.substr(pos + 1) : fp;
                    si.filePath = normalizePath(fp);
                    si.addDate = getTodayDate();
                    songs.push_back(si);
                    // 同时加入 mediaFiles
                    st.mediaFiles.push_back(fp);
                    if (st.currentFileIndex < 0) st.currentFileIndex = 0;
                    savePlaylists(st);
                    OX_LOG("[Playlist] Added song: %s\n", fp.c_str());
                    st.showAddSongsDlg = false;
                    st.needRebuildUI = true;
                };
                st.ui.addElement(std::move(addBtn));
            } else {
                auto noFileLbl = std::make_unique<OX::UILabel>("没有可添加的新文件 (所有文件已在歌单中)");
                noFileLbl->rect = OX::ORect(dlgX + 20.0f, dlgY + 60.0f, dlgW - 40.0f, 24.0f);
                noFileLbl->fontName = fontName; noFileLbl->fontSize = 14.0f;
                noFileLbl->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(noFileLbl));
            }

            // 关闭按钮
            auto closeBtn = std::make_unique<OX::UIButton>("关闭");
            closeBtn->rect = OX::ORect(dlgX + dlgW - 110.0f, dlgY + dlgH - 48.0f, 80.0f, 32.0f);
            closeBtn->fontName = fontName; closeBtn->fontSize = 13.0f;
            closeBtn->onClick = [&st]() {
                st.showAddSongsDlg = false;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(closeBtn));
        }
    };
    rebuildUI();

    st.app.setSizeCallback([&](uint32_t w, uint32_t h) {
        width = w; height = h;
        if (st.view) st.view->setViewport({0, 0, w, h});
        if (st.quadEntity) {
            updateVideoTransform(st.engine, st.quadEntity, (int)w, (int)h, st.videoW, st.videoH);
        }
        // Update all FX material instances
        for (int i = 0; i < st.fxEffectCount; i++) {
            if (st.fxMi[i]) st.fxMi[i]->setParameter("iResolution", float2{float(w), float(h)});
        }
        // Recreate lyrics FBO with new window size
        createLrcFbo(st, (int)w, (int)h);
        // Recreate Filament texture for lyrics (new size)
        if (st.lrcTexId && st.engine) {
            if (st.lrcFilamentTex) { st.engine->destroy(st.lrcFilamentTex); st.lrcFilamentTex = nullptr; }
            st.lrcFilamentTex = Texture::Builder()
                .width((uint32_t)w).height((uint32_t)h).levels(1)
                .format(Texture::InternalFormat::RGBA8)
                .sampler(Texture::Sampler::SAMPLER_2D)
                .build(*st.engine);
            TextureSampler smp(TextureSampler::MinFilter::LINEAR, TextureSampler::MagFilter::LINEAR);
            for (int i = 0; i < st.fxEffectCount; i++) {
                if (st.fxMi[i])
                    st.fxMi[i]->setParameter("lyricsTex", st.lrcFilamentTex, smp);
            }
            if (st.lyricsMi)
                st.lyricsMi->setParameter("lyricsTex", st.lrcFilamentTex, smp);
        }
        if (st.ui.init(st.app.getGLRC(), (int)w, (int)h)) st.needRebuildUI = true;
    });

    // ---------- 9. Input ----------
    st.app.setMouseWheelCallback([&](int32_t x, int32_t y, int d) { st.ui.handleMWheel((int)x, (int)y, d); });
    st.app.setMouseMoveCallback([&](int32_t x, int32_t y) {
        st.ui.handleMMove(x, y);
        st.fxMouse.x = (float)x;
        uint32_t winH = 0; st.app.getSize(nullptr, &winH);
        st.fxMouse.y = (float)(winH - 1 - y); // ShaderToy origin at bottom-left
    });
    st.app.setMouseButtonCallback([&](int32_t x, int32_t y, int32_t btn, bool pressed) {
        if (btn == 0) {
            uint32_t winH = 0; st.app.getSize(nullptr, &winH);
            if (pressed) {
                st.ui.handleMDown(x, y);
                st.fxMouse.z = (float)x;
                st.fxMouse.w = (float)(winH - 1 - y);
            } else {
                st.ui.handleMUp(x, y);
                st.fxMouse.z = -abs(st.fxMouse.z);
                st.fxMouse.w = -abs(st.fxMouse.w);
            }
        }
    });
    st.app.setKeyDownCallback([&](int keyCode) {
        if (keyCode == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (OpenClipboard(nullptr)) {
                HANDLE h = GetClipboardData(CF_UNICODETEXT);
                if (h) {
                    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
                    if (wstr) {
                        for (int i = 0; wstr[i]; ++i) {
                            // 过滤掉常见的换行符，避免意外行为
                            if (wstr[i] != L'\r' && wstr[i] != L'\n')
                                st.ui.handleChar(wstr[i]);
                        }
                        GlobalUnlock(h);
                    }
                }
                CloseClipboard();
            }
        } else {
            st.ui.handleKDown(keyCode);
        }
    });
    st.app.setKeyUpCallback([&](int keyCode) { st.ui.handleKUp(keyCode); });
    st.app.setCharCallback([&](wchar_t ch) { st.ui.handleChar(ch); });

    st.lastFrameTime = std::chrono::steady_clock::now();
    int frameCount = 0; float fpsTimer = 0.0f;

    // ---------- 9.5 Auto-start playback ----------
    st.isPlaying = true;
    st.isPaused = false;
    snprintf(st.statusText, sizeof(st.statusText), "Playing");
    if (st.decoder.hasAudio()) {
        st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
    }
    if (!st.audioStarted) {
        ma_result maRes = ma_device_start(&st.audioDevice);
        if (maRes != MA_SUCCESS) {
            OX_LOG("[Audio] ma_device_start init failed: %d\n", maRes);
        } else {
            st.audioStarted = true;
            OX_LOG("[Audio] ma_device_start init OK\n");
        }
    }

    // ---------- 9.6 Resume last played ----------
    if (st.resumePlaylistIndex >= 0 && st.resumePlaylistIndex < (int)st.playlists.size() && st.resumePosition >= 0.0) {
        OX_LOG("[LastPlayed] Resuming: playlist=%d song=%d pos=%.1fs\n",
               st.resumePlaylistIndex, st.resumeSongIndex, st.resumePosition);

        // 切到断点歌单
        st.currentPlaylistIndex = st.resumePlaylistIndex;
        auto& pl = st.playlists[st.resumePlaylistIndex];

        // 填充 mediaFiles
        st.mediaFiles.clear();
        st.currentFileIndex = -1;
        if (pl.type == "builtin") {
            for (const auto& s : pl.songs)
                st.mediaFiles.push_back(s.filePath);
        } else {
            std::vector<std::string> dirs = splitDirs(pl.directoryPath);
            scanMediaFiles(st.mediaFiles, dirs);
        }
        OX_LOG("[LastPlayed] playlist '%s' has %zu files\n", pl.name.c_str(), st.mediaFiles.size());

        // 校验 songIndex 并设置
        if (!st.mediaFiles.empty()) {
            if (st.resumeSongIndex >= 0 && st.resumeSongIndex < (int)st.mediaFiles.size())
                st.currentFileIndex = st.resumeSongIndex;
            else
                st.currentFileIndex = 0;

            OX_LOG("[LastPlayed] Loading file: %s\n", st.mediaFiles[st.currentFileIndex].c_str());

            // 停止当前音频并关闭旧的解码器
            if (st.audioStarted) {
                ma_device_stop(&st.audioDevice);
                st.audioStarted = false;
            }
            st.decoder.close();

            // 加载断点文件
            uint32_t w2, h2; st.app.getSize(&w2, &h2);
            if (loadMediaFile(st, st.mediaFiles[st.currentFileIndex].c_str(), (int)w2, (int)h2)) {
                if (st.decoder.hasVideo()) {
                    double firstPts2;
                    const uint8_t* f2 = st.decoder.getFirstFrame(firstPts2, st.audioQueue);
                    if (f2) { st.currentVideoPts = firstPts2; st.hasFrame = true; uploadVideoFrame(st, f2, st.videoW, st.videoH); }
                }
                st.isPlaying = true; st.isPaused = false;
                snprintf(st.statusText, sizeof(st.statusText), "Playing");
                st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);

                // 启动音频
                if (ma_device_start(&st.audioDevice) == MA_SUCCESS) {
                    st.audioStarted = true;

                    // Seek 到断点位置
                    if (st.resumePosition > 0.5 && st.decoder.getDuration() > 0) {
                        st.pendingSeek = true;
                        st.pendingSeekTarget = st.resumePosition;
                        OX_LOG("[LastPlayed] Will seek to %.1fs (duration=%.1fs)\n",
                               st.resumePosition, st.decoder.getDuration());
                    }
                }
                OX_LOG("[LastPlayed] Resume OK\n");
            } else {
                OX_LOG("[LastPlayed] Failed to load resume file\n");
            }
        }
        st.needRebuildUI = true;
    }

    // ---------- 10. Main loop ----------
    while (!st.app.shouldClose()) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - st.lastFrameTime).count();
        st.lastFrameTime = now;
        st.app.pollEvents();

        // Pending seek
        if (st.pendingSeek) {
            st.pendingSeek = false;
            double seekTarget = st.pendingSeekTarget;
            if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
            st.audioQueue.clear();
            st.audioSamplesPlayed = 0;
            bool ok = st.decoder.seekToTarget(st.audioQueue, seekTarget);
            if (ok) {
                double actualPts = st.decoder.getSeekedPts();
                st.videoTime = actualPts;
                if (st.decoder.hasVideo() && st.decoder.hasSeekedFrame()) {
                    const uint8_t* f = st.decoder.getSeekedFramePtr();
                    if (f) {
                        st.currentVideoPts = actualPts;
                        st.hasFrame = true;
                        uploadVideoFrame(st, f, st.videoW, st.videoH);
                    }
                }
                st.audioSamplesPlayed.store((uint64_t)(actualPts * st.AUDIO_SAMPLE_RATE));
            } else {
                st.audioSamplesPlayed.store((uint64_t)(seekTarget * st.AUDIO_SAMPLE_RATE));
                st.videoTime = seekTarget;
            }
            ma_result maRes = ma_device_start(&st.audioDevice);
            st.audioStarted = (maRes == MA_SUCCESS);
        }

        // FPS
        frameCount++; fpsTimer += dt;
        if (fpsTimer >= 0.5f) {
            st.fps = frameCount / fpsTimer;
            snprintf(st.fpsText, sizeof(st.fpsText), "FPS: %.1f", st.fps);
            if (st.fpsLabelPtr) st.fpsLabelPtr->setText(st.fpsText);
            frameCount = 0; fpsTimer = 0.0f;
        }

        // Update time & sync text
        double audioClock = st.audioSamplesPlayed.load() / (double)st.AUDIO_SAMPLE_RATE;
        st.videoTime = audioClock; // use audio clock as master time
        updateLyrics(st);
        if (st.timeLabelPtr) {
            char cur[16], dur[16];
            formatTime(cur, sizeof(cur), st.videoTime);
            formatTime(dur, sizeof(dur), st.decoder.getDuration());
            snprintf(st.timeText, sizeof(st.timeText), "%s / %s", cur, dur);
            st.timeLabelPtr->setText(st.timeText);
        }
        if (st.seekBarPtr && !st.seekBarDragging && st.decoder.getDuration() > 0) {
            st.seekBarPtr->setValue((float)st.videoTime);
        }
        if (st.syncLabelPtr) {
            double diff = st.currentVideoPts - audioClock;
            snprintf(st.syncText, sizeof(st.syncText), "Sync: %+.3fs", diff);
            st.syncLabelPtr->setText(st.syncText);
        }
        if (st.frameLabelPtr) {
            if (st.decoder.hasVideo()) {
                int frameNum = (int)(st.videoTime * st.decoder.getFps() + 0.5);
                snprintf(st.frameText, sizeof(st.frameText), "Time: %.2fs Frame: %d", st.videoTime, frameNum);
            } else {
                snprintf(st.frameText, sizeof(st.frameText), "Time: %.2fs", st.videoTime);
            }
            st.frameLabelPtr->setText(st.frameText);
        }

        // Playback
        if (st.isPlaying) {
            // Keep audio queue fed (for both audio-only and video files)
            if (st.decoder.hasAudio()) {
                if (st.audioQueue.size() < (size_t)st.AUDIO_BUF_FIFTH_SEC) {
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_HALF_SEC);
                }
            }

            // For audio-only files: detect EOF and handle play mode
            if (!st.hasFrame && st.decoder.hasAudio() && st.decoder.isEof() && st.audioQueue.size() < (size_t)st.AUDIO_BUF_EOF_THRESH) {
                OX_LOG("[Audio] Audio-only EOF, playMode=%d\n", (int)st.playMode);
                handlePlayModeEOF(st);
            }

            if (st.hasFrame) {
                // Video sync: if current video frame is behind audio, decode next
                if (st.currentVideoPts <= audioClock + st.VIDEO_SYNC_THRESHOLD_SEC) {
                    double nextPts = 0.0;
                    const uint8_t* frameData = st.decoder.decodeNextFrame(nextPts, st.audioQueue);
                    if (frameData) {
                        st.currentVideoPts = nextPts;
                        uploadVideoFrame(st, frameData, st.videoW, st.videoH);
                    } else {
                        // EOF
                        OX_LOG("[Video] Video EOF, playMode=%d\n", (int)st.playMode);
                        handlePlayModeEOF(st);
                    }
                }
            }
        }

        // Update FX time + mouse
        if (st.fxEnabled && st.fxEffectCount > 0 && st.fxMi[st.fxEffectIndex]) {
            st.fxTime += dt;
            st.fxMi[st.fxEffectIndex]->setParameter("iTime", st.fxTime);
            st.fxMi[st.fxEffectIndex]->setParameter("iMouseXY", float2{st.fxMouse.x, st.fxMouse.y});
            st.fxMi[st.fxEffectIndex]->setParameter("iMouseZW", float2{st.fxMouse.z, st.fxMouse.w});
        }

        // One-time diagnostic dump (printed here so it's visible even with "start" console)
        {
            static bool dumped = false;
            if (!dumped) {
                dumped = true;
                OX_LOG("[LRC] === MAIN-LOOP STATE DUMP ===\n");
                OX_LOG("[LRC]   lyricsMat=%p  lyricsMi=%p  lyricsEntity=%u\n",
                       (void*)st.lyricsMat, (void*)st.lyricsMi, (unsigned)st.lyricsEntity.getId());
                OX_LOG("[LRC]   lrcTexId=%u  lrcFboId=%u  lrcFilamentTex=%p  lrcGlCanvas=%p\n",
                       st.lrcTexId, st.lrcFboId, (void*)st.lrcFilamentTex, (void*)st.lrcGlCanvas);
                OX_LOG("[LRC]   lrcFboW=%d  lrcFboH=%d  lrcFboDirty=%d  showLyrics=%d\n",
                       st.lrcFboW, st.lrcFboH, st.lrcFboDirty, st.showLyrics);
                OX_LOG("[LRC]   fxEnabled=%d  fxEffectCount=%d  fxEffectIndex=%d  lrcLines=%zu\n",
                       st.fxEnabled, st.fxEffectCount, st.fxEffectIndex, st.lrcLines.size());
            }
        }

        // Upload lyrics FBO to Filament (before Filament render, so FX shader can sample it)
        wglMakeCurrent(st.app.getDC(), st.app.getGLRC());
        if (st.lrcFboDirty) {
            OX_LOG("[LRC] Main loop: lrcFboDirty=true, showLyrics=%d, lrcLines=%zu, lrcTexId=%u\n",
                   st.showLyrics, st.lrcLines.size(), st.lrcTexId);
            if (st.showLyrics && !st.lrcLines.empty()) {
                float lrcW = 560.0f;
                float lrcX = ((float)desc.width - lrcW) / 2.0f;
                float lrcY = 20.0f + 248.0f;              // leftY + offset
                float lrcH = 340.0f;
                renderLyricsToFbo(st, lrcX, lrcY, lrcW, lrcH, "siyuan");
            } else if (st.lrcFboId && st.lrcGlCanvas) {
                // Clear FBO to transparent (hide lyrics / no lyrics)
                st.lrcGlCanvas->remove(nullptr);
                glBindFramebuffer(GL_FRAMEBUFFER, st.lrcFboId);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            uploadLyricsToFilament(st);
        }

        // Render
        if (st.renderer->beginFrame(st.swapChain)) {
            st.renderer->render(st.view);
            st.renderer->endFrame();
        }
        st.engine->flushAndWait();

        // ThorVG UI
        wglMakeCurrent(st.app.getDC(), st.app.getGLRC());
        if (st.needRebuildUI) { st.needRebuildUI = false; rebuildUI(); }
        st.ui.update(dt);
        st.ui.render();
        st.app.swapBuffers();
    }

    // ---------- 11. Cleanup ----------
    saveLastPlayed(st);
    st.ui.destroy();
    wglMakeCurrent(st.app.getDC(), st.app.getGLRC());
    destroyLrcFbo(st);
    wglMakeCurrent(nullptr, nullptr);
    tvg::Initializer::term();
    cleanup(st);
	st.app.unregisterHotKey(HOTKEY_TOGGLE);
    st.app.destroy();
    timeEndPeriod(1);
    return 0;
}
