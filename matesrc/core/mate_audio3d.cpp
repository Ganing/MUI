/****************************************************************************
 * mate_audio3d.cpp - 视频音轨的空间音源实现
 *
 * 数据链：VideoSource(AVDecoder 解码, 48k/2ch float) -> AudioQueue
 *         -> 自定义 ma_data_source -> ma_sound（空间化）-> ma_engine 混音 -> 设备
 * 队列空时补静音（视频循环/切源的瞬间会有几十毫秒空隙），保证音源不会“结束”。
 ****************************************************************************/

#define NOMINMAX
#include <windows.h>

#include "mate_audio3d.h"
#include "mate_video.h"

// 引擎 + 空间化要保留 node graph；Windows 走 WinMM，避免 WASAPI/DSOUND 的额外依赖
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_WASAPI
#define MA_NO_DSOUND
#include <miniaudio/miniaudio.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace mate {
namespace {

// AudioQueue 里的 PCM 固定是 48k / 2ch / float32（xav.h 的 swr 输出配置）
constexpr ma_uint32 kRate     = (ma_uint32)kAudioSampleRate;
constexpr ma_uint32 kChannels = (ma_uint32)kAudioChannels;

struct QueueSource {
    ma_data_source_base base;
    AudioQueue* queue = nullptr;
};

ma_result queueRead(ma_data_source* pDS, void* pFramesOut, ma_uint64 frameCount,
                    ma_uint64* pFramesRead) {
    QueueSource* src = reinterpret_cast<QueueSource*>(pDS);
    const size_t want = (size_t)frameCount * kChannels;
    float* out = static_cast<float*>(pFramesOut);

    size_t got = 0;
    if (src->queue) got = src->queue->read(out, want);
    if (got < want) std::memset(out + got, 0, (want - got) * sizeof(float));   // 欠载补静音

    if (pFramesRead) *pFramesRead = frameCount;   // 永远报满，别让音源进入“已结束”
    return MA_SUCCESS;
}
ma_result queueSeek(ma_data_source*, ma_uint64) {
    return MA_NOT_IMPLEMENTED;
}

ma_result queueGetFormat(ma_data_source*, ma_format* pFormat, ma_uint32* pChannels,
                         ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap) {
    *pFormat = ma_format_f32;
    *pChannels = kChannels;
    *pSampleRate = kRate;
    if (pChannelMap && channelMapCap >= 2) {   // FL / FR：这版 miniaudio 没有 stereo 简写
        pChannelMap[0] = MA_CHANNEL_FRONT_LEFT;
        pChannelMap[1] = MA_CHANNEL_FRONT_RIGHT;
    }
    return MA_SUCCESS;
}

ma_result queueGetCursor(ma_data_source*, ma_uint64* pCursor) {
    if (pCursor) *pCursor = 0;
    return MA_NOT_IMPLEMENTED;
}

ma_result queueGetLength(ma_data_source*, ma_uint64* pLength) {
    if (pLength) *pLength = 0;   // 实时流，长度未知
    return MA_NOT_IMPLEMENTED;
}

const ma_data_source_vtable g_queueVtable = {
    queueRead, queueSeek, queueGetFormat, queueGetCursor, queueGetLength, nullptr, 0
};

}// namespace

struct SpatialVideoAudio::Impl {
    ma_engine engine{};
    bool      engineReady = false;

    QueueSource ds{};
    bool        dsReady = false;
    VideoSource* video = nullptr;   // 当前挂着的视频（detach 时要把音频时钟关掉）

    ma_sound sound{};
    bool     soundReady = false;
    bool     spatialApplied = true;

    std::atomic<float> volume{0.8f};
    std::atomic<bool>  spatial{true};
    std::atomic<bool>  paused{false};

    std::atomic<float> srcX{0.0f}, srcY{0.0f}, srcZ{0.0f};
    float lisX = 0.0f, lisY = 0.0f, lisZ = 0.0f;
    float lisDX = 0.0f, lisDY = 0.0f, lisDZ = -1.0f;

    std::string err;
};

SpatialVideoAudio::SpatialVideoAudio() : impl_(new Impl()) {}

SpatialVideoAudio::~SpatialVideoAudio() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool SpatialVideoAudio::init() {
    Impl& im = *impl_;
    if (im.engineReady) return true;

    ma_engine_config cfg = ma_engine_config_init();
    const ma_result r = ma_engine_init(&cfg, &im.engine);
    if (r != MA_SUCCESS) {
        im.err = std::string("ma_engine_init 失败: ") + ma_result_description(r);
        return false;
    }
    im.engineReady = true;

    ma_engine_listener_set_position(&im.engine, 0, 0.0f, 0.0f, 0.0f);
    ma_engine_listener_set_direction(&im.engine, 0, 0.0f, 0.0f, -1.0f);
    ma_engine_listener_set_world_up(&im.engine, 0, 0.0f, 1.0f, 0.0f);
    return true;
}

void SpatialVideoAudio::shutdown() {
    if (!impl_) return;
    Impl& im = *impl_;
    detach();
    if (im.engineReady) {
        ma_engine_uninit(&im.engine);
        im.engineReady = false;
    }
}

bool SpatialVideoAudio::attach(VideoSource& video) {
    Impl& im = *impl_;
    if (!im.engineReady) {
        im.err = "ma_engine 未初始化";
        return false;
    }
    detach();

    im.ds.queue = &video.audioQueue();
    // 有音轨才切到音频时钟：没有音轨时队列永远空，consumedSeconds 会一直停在 0，
    // 视频进度就再也不走了
    im.video = &video;
    video.setAudioClock(video.hasAudio());
    ma_data_source_config dsCfg = ma_data_source_config_init();
    dsCfg.vtable = &g_queueVtable;
    ma_result r = ma_data_source_init(&dsCfg, &im.ds.base);
    if (r != MA_SUCCESS) {
        im.err = std::string("ma_data_source_init 失败: ") + ma_result_description(r);
        im.ds.queue = nullptr;
        video.setAudioClock(false);
        im.video = nullptr;
        return false;
    }
    im.dsReady = true;

    r = ma_sound_init_from_data_source(&im.engine, &im.ds.base, 0, nullptr, &im.sound);
    if (r != MA_SUCCESS) {
        im.err = std::string("ma_sound_init_from_data_source 失败: ")
               + ma_result_description(r);
        ma_data_source_uninit(&im.ds.base);
        im.dsReady = false;
        im.ds.queue = nullptr;
        video.setAudioClock(false);
        im.video = nullptr;
        return false;
    }
    im.soundReady = true;
    im.spatialApplied = im.spatial.load();

    // 3D：反比衰减 + 近区半径；没有空间化时就当普通 2D 音源
    ma_sound_set_spatialization_enabled(&im.sound,
                                        im.spatialApplied ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(&im.sound, ma_attenuation_model_inverse);
    ma_sound_set_rolloff(&im.sound, 0.3f);
    ma_sound_set_min_distance(&im.sound, 2.0f);
    ma_sound_set_max_distance(&im.sound, 300.0f);
    ma_sound_set_position(&im.sound, im.srcX.load(), im.srcY.load(), im.srcZ.load());
    ma_sound_set_volume(&im.sound, im.volume.load());
    ma_sound_start(&im.sound);

    std::printf("[音频] 音轨已挂到 3D 音源（%u Hz / %u ch）\n", kRate, kChannels);
    return true;
}

void SpatialVideoAudio::detach() {
    Impl& im = *impl_;
    if (im.video) {
        im.video->setAudioClock(false);   // 退回墙钟，否则视频进度会停在原地
        im.video = nullptr;
    }
    if (im.soundReady) {
        ma_sound_stop(&im.sound);
        ma_sound_uninit(&im.sound);
        im.soundReady = false;
    }
    if (im.dsReady) {
        ma_data_source_uninit(&im.ds.base);
        im.dsReady = false;
    }
    im.ds.queue = nullptr;   // 音源已拆，别再让音频线程去读 VideoSource 的队列
}

bool SpatialVideoAudio::active() const {
    return impl_->soundReady;
}

void SpatialVideoAudio::update() {
    Impl& im = *impl_;
    if (!im.engineReady) return;

    ma_engine_listener_set_position(&im.engine, 0, im.lisX, im.lisY, im.lisZ);
    ma_engine_listener_set_direction(&im.engine, 0, im.lisDX, im.lisDY, im.lisDZ);

    if (!im.soundReady) return;
    ma_sound_set_position(&im.sound, im.srcX.load(), im.srcY.load(), im.srcZ.load());
    ma_sound_set_volume(&im.sound, im.volume.load());
    const bool sp = im.spatial.load();
    if (sp != im.spatialApplied) {
        ma_sound_set_spatialization_enabled(&im.sound, sp ? MA_TRUE : MA_FALSE);
        im.spatialApplied = sp;
    }
}

void SpatialVideoAudio::setListener(float px, float py, float pz,
                                    float dx, float dy, float dz) {
    Impl& im = *impl_;
    im.lisX = px; im.lisY = py; im.lisZ = pz;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) {
        im.lisDX = dx / len; im.lisDY = dy / len; im.lisDZ = dz / len;
    }
}

void SpatialVideoAudio::setSourcePosition(float x, float y, float z) {
    impl_->srcX.store(x);
    impl_->srcY.store(y);
    impl_->srcZ.store(z);
}

void SpatialVideoAudio::setVolume(float volume) {
    impl_->volume.store(volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume));
}

void SpatialVideoAudio::setSpatialized(bool on) {
    impl_->spatial.store(on);
}

float SpatialVideoAudio::distance() const {
    const Impl& im = *impl_;
    const float dx = im.srcX.load() - im.lisX;
    const float dy = im.srcY.load() - im.lisY;
    const float dz = im.srcZ.load() - im.lisZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

const char* SpatialVideoAudio::error() const {
    return impl_ ? impl_->err.c_str() : "";
}

}// namespace mate
