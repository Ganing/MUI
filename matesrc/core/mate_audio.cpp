#define NOMINMAX
#include <windows.h>

#include "mate_audio.h"
#include "mate_core.h"

// 只保留解码 + 设备输出；Windows 上走 WinMM，避免额外的 WASAPI/COM 链接依赖
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_WASAPI
#define MA_NO_DSOUND
#include <miniaudio/miniaudio.h>

#include <atomic>
#include <cstring>
#include <wchar.h>

namespace mate {
namespace {

constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kSampleRate = 48000;

std::wstring utf8ToWide(const std::string& s) {
    std::wstring out;
    if (s.empty()) return out;
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return out;
    out.resize((size_t)len);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

} // namespace

struct AudioLoop::Impl {
    ma_device device{};
    ma_decoder decoder{};
    bool decoderReady = false;
    bool deviceReady = false;
    bool running = false;
    std::atomic<float> volume{ 1.0f };
    std::atomic<bool> paused{ false };
    std::string error;

    static void dataCallback(ma_device* device, void* output, const void* input,
                             ma_uint32 frameCount) {
        (void)input;
        Impl* self = static_cast<Impl*>(device->pUserData);
        float* out = static_cast<float*>(output);
        const size_t stride = (size_t)device->playback.channels;
        const size_t total = (size_t)frameCount * stride;

        if (!self || !self->decoderReady || self->paused.load(std::memory_order_relaxed)) {
            std::memset(out, 0, total * sizeof(float));
            return;
        }

        ma_uint64 written = 0;
        while (written < frameCount) {
            ma_uint64 want = (ma_uint64)frameCount - written;
            ma_uint64 got = 0;
            ma_decoder_read_pcm_frames(&self->decoder,
                                       out + (size_t)written * stride, want, &got);
            if (got == 0) {
                // 到末尾：回到开头继续，做到无缝循环
                if (ma_decoder_seek_to_pcm_frame(&self->decoder, 0) != MA_SUCCESS) break;
                ma_decoder_read_pcm_frames(&self->decoder,
                                           out + (size_t)written * stride, want, &got);
                if (got == 0) break;
            }
            written += got;
        }

        const float v = self->volume.load(std::memory_order_relaxed);
        if (v != 1.0f) {
            for (size_t i = 0; i < (size_t)written * stride; ++i) out[i] *= v;
        }
        if (written < frameCount) {
            std::memset(out + (size_t)written * stride, 0,
                        ((size_t)frameCount - (size_t)written) * stride * sizeof(float));
        }
    }
};

AudioLoop::AudioLoop() = default;

AudioLoop::~AudioLoop() {
    stop();
    delete impl_;
    impl_ = nullptr;
}

bool AudioLoop::start(const std::string& path, float volume) {
    stop();
    if (!impl_) impl_ = new Impl();
    impl_->volume.store(volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume));
    impl_->paused.store(false);
    impl_->error.clear();

    const std::wstring wide = utf8ToWide(path);
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
    if (ma_decoder_init_file_w(wide.c_str(), &decoderConfig, &impl_->decoder) != MA_SUCCESS) {
        impl_->error = "无法打开音频文件: " + path;
        logf("[音频] %s", impl_->error.c_str());
        return false;
    }
    impl_->decoderReady = true;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = kChannels;
    deviceConfig.sampleRate = kSampleRate;
    deviceConfig.dataCallback = Impl::dataCallback;
    deviceConfig.pUserData = impl_;

    if (ma_device_init(nullptr, &deviceConfig, &impl_->device) != MA_SUCCESS) {
        impl_->error = "音频设备初始化失败";
        logf("[音频] %s", impl_->error.c_str());
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoderReady = false;
        return false;
    }
    impl_->deviceReady = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        impl_->error = "音频设备启动失败";
        logf("[音频] %s", impl_->error.c_str());
        ma_device_uninit(&impl_->device);
        impl_->deviceReady = false;
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoderReady = false;
        return false;
    }

    impl_->running = true;
    logf("[音频] 循环播放: %s", path.c_str());
    return true;
}

void AudioLoop::stop() {
    if (!impl_ || !impl_->running) return;
    if (impl_->deviceReady) {
        ma_device_uninit(&impl_->device);   // 会等待回调退出
        impl_->deviceReady = false;
        impl_->device = ma_device{};
    }
    if (impl_->decoderReady) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoderReady = false;
        impl_->decoder = ma_decoder{};
    }
    impl_->running = false;
}

void AudioLoop::setVolume(float volume) {
    if (!impl_) return;
    impl_->volume.store(volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume));
}

void AudioLoop::setPaused(bool paused) {
    if (!impl_) return;
    impl_->paused.store(paused);
}

bool AudioLoop::active() const { return impl_ && impl_->running; }

const char* AudioLoop::error() const { return impl_ ? impl_->error.c_str() : ""; }

} // namespace mate
