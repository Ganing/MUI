/****************************************************************************
 * 标题: TEST-Filament-Player - 音视频同步播放器
 * 功能: 使用 FFmpeg 解码 MP4 音视频，miniaudio 播放音频，Filament 渲染视频
 *       以音频时钟为主，实现音视频同步播放
 * 测试: E:/MX/Data/tvb1.mp4（有字幕，可验证同步）
 * 参考: MX/oav.h, TEST-Filament-Video.cpp
 * 依赖: ocore.h, oapp.h, oui.h, miniaudio, FFmpeg静态库, Filament
 ****************************************************************************/

#include "ocore.h"
#include "oapp.h"
#include "oui.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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

#define FILAMENT_LIB_PATH "../filament-v1.71.6-windows/lib/x86_64/mt/"
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
static constexpr const char* kScanDirs[]    = {"D:\\ffbin", "E:\\movie", "E:\\movie\\Movies\\TikTok"};
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
            displayMatrix = (const int32_t*)av_stream_get_side_data(vStream, AV_PKT_DATA_DISPLAYMATRIX, &dmSize);
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

    // --- Playback State ---
    bool isPlaying = false;
    bool isPaused = false;
    bool hasFrame = false;
    PlayMode playMode = PlayMode::SingleLoop;
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
    float volume = 1.0f;
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
    std::vector<std::string> mediaFiles;
    int currentFileIndex = -1;
    char rightInfoText[256] = "";

    // --- Timing ---
    std::chrono::steady_clock::time_point lastFrameTime;
    float fps = 0.0f;
};

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
        if (st.camEntity) st.engine->destroy(st.camEntity);
        if (st.quadEntity) st.engine->destroy(st.quadEntity);
        if (st.mi) st.engine->destroy(st.mi);
        if (st.mat) st.engine->destroy(st.mat);
        if (st.videoTex) st.engine->destroy(st.videoTex);
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
                    files.push_back(OX::Core::wstrToUtf8(entry.path().wstring()));
                }
            }
        } catch (...) {}
    }
}

/*==================== 加载媒体文件 ====================*/
static bool loadMediaFile(AppState& st, const char* path, int winW, int winH) {
    st.decoder.close();
    if (!st.decoder.open(path)) return false;

    st.videoW = st.decoder.getWidth();
    st.videoH = st.decoder.getHeight();
    st.frameDuration = st.decoder.hasVideo() ? (1.0 / st.decoder.getFps()) : 0.0;
    if (st.decoder.hasVideo()) {
        snprintf(st.infoText, sizeof(st.infoText), "%dx%d @ %.1ffps", st.videoW, st.videoH, st.decoder.getFps());
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

    // 纯音频文件：上传一帧黑色像素作为默认画面
    if (!st.decoder.hasVideo()) {
        std::vector<uint8_t> black(texW * texH * 4, 0);
        uploadVideoFrame(st, black.data(), texW, texH);
    }

    TextureSampler smp(TextureSampler::MinFilter::LINEAR, TextureSampler::MagFilter::LINEAR);
    st.mi->setParameter("videoTex", st.videoTex, smp);

    if (st.quadEntity) {
        updateVideoTransform(st.engine, st.quadEntity, winW, winH, st.videoW, st.videoH);
    }

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

/*==================== MAIN ====================*/
int main() {
    AllocConsole();
    SetConsoleOutputCP(65001);
    OX::Core::initConsole();
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered for crash diagnostics
    timeBeginPeriod(1);

    AppState st;
    const char* videoPath = "e:/MX/Data/tvb1.mp4";

    // ---------- 1. Window ----------
    OX::WindowDesc desc;
    desc.title = L"TEST-Filament-Player";
    desc.width = 1280; desc.height = 720;
    desc.enableOpenGL = true; desc.stencilBits = 0;
    if (!st.app.create(&desc)) { OX_LOG("[MAIN] Window create failed\n"); return 1; }
    uint32_t width, height; st.app.getSize(&width, &height);

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

    // ---------- 6. Open video ----------
    if (!loadMediaFile(st, videoPath, (int)width, (int)height)) {
        OX_LOG("[MAIN] Failed to open video: %s\n", videoPath);
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }

    st.quadEntity = createFullscreenQuad(st.engine, st.scene, st.mi);
    updateVideoTransform(st.engine, st.quadEntity, (int)width, (int)height, st.videoW, st.videoH);

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

    float leftX = 20.0f, leftY = 20.0f, panelW = 280.0f;
    std::function<void()> rebuildUI;
    rebuildUI = [&]() {
        st.ui.clearElements();
        st.fpsLabelPtr = st.infoLabelPtr = st.timeLabelPtr = st.statusLabelPtr = st.syncLabelPtr = st.rightInfoLabelPtr = nullptr;
        st.playBtnPtr = st.loopBtnPtr = st.prevBtnPtr = st.nextBtnPtr = nullptr;
        st.seekBarPtr = st.volumeSliderPtr = nullptr;
        st.fileDropdownPtr = nullptr;
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

        // ---------- File Dropdown ----------
        if (!st.mediaFiles.empty()) {
            curY += 8.0f;
            auto dd = std::make_unique<OX::UIDropdown>("Select file...");
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
                    // Auto-play
                    st.isPlaying = true;
                    st.isPaused = false;
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
        }

        // ---------- Right info panel ----------
        updateRightInfo(st);
        auto infoR = std::make_unique<OX::UILabel>(st.rightInfoText);
        infoR->rect = OX::ORect((float)width - panelW - 20.0f, leftY, panelW, 240.0f);
        infoR->fontSize = 13.0f; infoR->fontName = fontName;
        infoR->textColor = OX::OColor(200, 220, 255, 255);
        infoR->setWrapMode(tvg::TextWrap::Word);
        st.rightInfoLabelPtr = infoR.get();
        st.ui.addElement(std::move(infoR));
    };
    rebuildUI();

    st.app.setSizeCallback([&](uint32_t w, uint32_t h) {
        width = w; height = h;
        if (st.view) st.view->setViewport({0, 0, w, h});
        if (st.quadEntity) {
            updateVideoTransform(st.engine, st.quadEntity, (int)w, (int)h, st.videoW, st.videoH);
        }
        if (st.ui.init(st.app.getGLRC(), (int)w, (int)h)) st.needRebuildUI = true;
    });

    // ---------- 9. Input ----------
    st.app.setMouseWheelCallback([&](int32_t x, int32_t y, int d) { st.ui.handleMWheel((int)x, (int)y, d); });
    st.app.setMouseMoveCallback([&](int32_t x, int32_t y) { st.ui.handleMMove(x, y); });
    st.app.setMouseButtonCallback([&](int32_t x, int32_t y, int32_t btn, bool pressed) {
        if (btn == 0) { if (pressed) st.ui.handleMDown(x, y); else st.ui.handleMUp(x, y); }
    });

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
    st.ui.destroy();
    tvg::Initializer::term();
    cleanup(st);
    st.app.destroy();
    timeEndPeriod(1);
    return 0;
}
