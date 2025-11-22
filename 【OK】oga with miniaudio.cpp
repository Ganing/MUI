/****************************************************************************
 * 标题: OGG Vorbis 音频播放器 (libvorbis + miniaudio 输出)
 * 文件: oga_test_miniaudio.cpp
 * 版本: 4.0
 * 功能: 手动解析 OGG 容器与 Vorbis 包, 使用 miniaudio 播放
 * 依赖: libogg, libvorbis, miniaudio
 * 环境: Windows 10/11, VS2022, C++17/20
 ****************************************************************************/

#if 1

#include "miniaudio.h"  

#include <cstdio>  
#include <cstdint>  
#include <atomic>  
#include <vector>  
#include <cstring>  
#include <mutex>  
#include <thread>
#include <chrono>

 // Ogg/Vorbis  
#include <ogg/ogg.h>  
#include <vorbis/codec.h>  

// 静态库链接  
#pragma comment(lib, "libogg.lib")  
#pragma comment(lib, "libvorbis.lib")  

// 全局状态  
struct AudioState {
    std::vector<char> pcmBuffer;      // PCM 数据缓冲  
    std::mutex bufferMutex;           // 缓冲区互斥锁  
    std::atomic<bool> isPlaying{ true };
    std::atomic<bool> decodingFinished{ false };
    ma_device device;
    ma_uint32 channels;
    ma_uint32 sampleRate;
};

static AudioState g_audioState;

// miniaudio 数据回调函数  
static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput;

    if (!g_audioState.isPlaying.load()) {
        memset(pOutput, 0, frameCount * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels));
        return;
    }

    std::lock_guard<std::mutex> lock(g_audioState.bufferMutex);

    size_t bytesNeeded = frameCount * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels);
    size_t bytesAvailable = g_audioState.pcmBuffer.size();

    if (bytesAvailable >= bytesNeeded) {
        // 有足够数据  
        memcpy(pOutput, g_audioState.pcmBuffer.data(), bytesNeeded);
        g_audioState.pcmBuffer.erase(g_audioState.pcmBuffer.begin(),
            g_audioState.pcmBuffer.begin() + bytesNeeded);
    }
    else if (bytesAvailable > 0) {
        // 部分数据  
        memcpy(pOutput, g_audioState.pcmBuffer.data(), bytesAvailable);
        memset((char*)pOutput + bytesAvailable, 0, bytesNeeded - bytesAvailable);
        g_audioState.pcmBuffer.clear();

        if (g_audioState.decodingFinished.load()) {
            g_audioState.isPlaying.store(false);
        }
    }
    else {
        // 无数据  
        memset(pOutput, 0, bytesNeeded);
        if (g_audioState.decodingFinished.load()) {
            g_audioState.isPlaying.store(false);
        }
    }
}

// 提交 PCM 数据到缓冲区  
static bool submitPcm(const char* data, size_t len)
{
    if (len == 0) return true;

    // 限制缓冲区大小，避免内存过度增长  
    const size_t maxBufferSize = 1024 * 1024; // 1MB  

    // 在锁外等待缓冲区有空间  
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_audioState.bufferMutex);
            if (g_audioState.pcmBuffer.size() <= maxBufferSize) {
                // 有空间了，添加数据  
                g_audioState.pcmBuffer.insert(g_audioState.pcmBuffer.end(), data, data + len);
                return true;
            }
        }
        // 释放锁后再睡眠  
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// 打开文件并初始化 ogg_sync_state  
static bool openFileAndInitSync(const char* filename, FILE*& fp, ogg_sync_state& oy)
{
    fp = std::fopen(filename, "rb");
    if (!fp) {
        std::printf("无法打开文件: %s\n", filename);
        return false;
    }
    ogg_sync_init(&oy);
    return true;
}

// 解析 Vorbis 三个头并初始化 ogg_stream_state  
static bool parseVorbisHeaders(FILE* fp, ogg_sync_state& oy, ogg_stream_state& vs,
    vorbis_info& vi, vorbis_comment& vc)
{
    bool vorbisStreamInit = false;
    int vorbisHeaderCount = 0;
    ogg_page og{}; ogg_packet op{};

    while (vorbisHeaderCount < 3) {
        char* buffer = ogg_sync_buffer(&oy, 4096);
        int bytes = (int)std::fread(buffer, 1, 4096, fp);
        if (bytes <= 0) break;
        ogg_sync_wrote(&oy, bytes);

        while (ogg_sync_pageout(&oy, &og) > 0) {
            ogg_stream_state test;
            if (!vorbisStreamInit) {
                ogg_stream_init(&test, ogg_page_serialno(&og));
                ogg_stream_pagein(&test, &og);
                while (ogg_stream_packetout(&test, &op) > 0) {
                    if (vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {
                        std::memcpy(&vs, &test, sizeof(test));
                        vorbisStreamInit = true;
                        vorbisHeaderCount = 1;
                        break;
                    }
                }
                if (!vorbisStreamInit) ogg_stream_clear(&test);
            }
            else if (ogg_page_serialno(&og) == vs.serialno) {
                ogg_stream_pagein(&vs, &og);
                while (vorbisHeaderCount < 3 && ogg_stream_packetout(&vs, &op) > 0) {
                    if (vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {
                        vorbisHeaderCount++;
                    }
                }
            }
        }
    }
    if (!vorbisStreamInit || vorbisHeaderCount < 3) {
        std::printf("未找到完整的 Vorbis 音频头\n");
        return false;
    }
    return true;
}

// 初始化 Vorbis 解码状态  
static bool initVorbisDecoder(vorbis_dsp_state& vd, vorbis_block& vb, vorbis_info& vi)
{
    if (vorbis_synthesis_init(&vd, &vi) != 0) {
        std::printf("vorbis_synthesis_init 失败\n");
        return false;
    }
    vorbis_block_init(&vd, &vb);
    return true;
}

// 初始化 miniaudio 设备  
static bool initMiniaudioDevice(const vorbis_info& vi)
{
    g_audioState.channels = vi.channels;
    g_audioState.sampleRate = vi.rate;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_s16;
    deviceConfig.playback.channels = vi.channels;
    deviceConfig.sampleRate = vi.rate;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &g_audioState;

    ma_result result = ma_device_init(NULL, &deviceConfig, &g_audioState.device);
    if (result != MA_SUCCESS) {
        std::printf("ma_device_init 失败, 错误码=%d\n", result);
        return false;
    }

    result = ma_device_start(&g_audioState.device);
    if (result != MA_SUCCESS) {
        std::printf("ma_device_start 失败, 错误码=%d\n", result);
        ma_device_uninit(&g_audioState.device);
        return false;
    }

    return true;
}

// 将浮点 PCM 转换为 int16 并提交  
static bool writePcmAndMaybeFlush(float** pcm, int samples, int channels,
    std::vector<char>& accum, size_t bufBytes)
{
    const size_t needed = (size_t)samples * (size_t)channels * sizeof(int16_t);
    size_t startSize = accum.size();
    accum.resize(startSize + needed);
    int16_t* dst = reinterpret_cast<int16_t*>(accum.data() + startSize);

    for (int i = 0; i < samples; ++i) {
        for (int c = 0; c < channels; ++c) {
            float f = pcm[c][i];
            if (f > 1.0f) f = 1.0f;
            if (f < -1.0f) f = -1.0f;
            int v = (int)(f * 32767.0f);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            *dst++ = (int16_t)v;
        }
    }

    // 刷新策略  
    if (accum.size() >= bufBytes) {
        if (!submitPcm(accum.data(), accum.size())) return false;
        accum.clear();
    }
    return true;
}

// 解码并播放主循环  
static void decodeAndPlay(FILE* fp, ogg_sync_state& oy, ogg_stream_state& vs,
    vorbis_dsp_state& vd, vorbis_block& vb, vorbis_info& vi)
{
    const size_t target_ms = 80;
    const size_t bytesPerMs = (vi.rate * vi.channels * 2) / 1000;
    const size_t targetBytes = (bytesPerMs > 0) ? (bytesPerMs * target_ms) : 32768;
    const size_t minBuf = 4 * 1024, maxBuf = 128 * 1024;
    size_t bufBytes = targetBytes;
    if (bufBytes < minBuf) bufBytes = minBuf;
    if (bufBytes > maxBuf) bufBytes = maxBuf;

    std::vector<char> accum;
    accum.reserve(bufBytes);
    bool eos = false;
    ogg_page og{};
    ogg_packet op{};

    while (!eos) {
        while (ogg_stream_packetout(&vs, &op) > 0) {
            if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
            float** pcm = nullptr;
            int samples = 0;
            while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                if (!writePcmAndMaybeFlush(pcm, samples, vi.channels, accum, bufBytes)) {
                    eos = true; break;
                }
                vorbis_synthesis_read(&vd, samples);
            }
            if (eos) break;
        }
        if (eos) break;

        int pageout = ogg_sync_pageout(&oy, &og);
        if (pageout == 0) {
            char* buffer = ogg_sync_buffer(&oy, 4096);
            int bytes = (int)std::fread(buffer, 1, 4096, fp);
            if (bytes <= 0) eos = true;
            else ogg_sync_wrote(&oy, bytes);
            continue;
        }
        if (pageout < 0) continue;
        if (ogg_page_serialno(&og) == vs.serialno) {
            ogg_stream_pagein(&vs, &og);
            if (ogg_page_eos(&og)) eos = true;
        }
    }

    // 拉完残留  
    while (ogg_stream_packetout(&vs, &op) > 0) {
        if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
        float** pcm = nullptr;
        int samples = 0;
        while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
            if (!writePcmAndMaybeFlush(pcm, samples, vi.channels, accum, bufBytes)) break;
            vorbis_synthesis_read(&vd, samples);
        }
    }

    // 刷新剩余  
    submitPcm(accum.data(), accum.size());
    accum.clear();

    g_audioState.decodingFinished.store(true);
}

// 释放资源  
static void cleanup(ogg_sync_state& oy, ogg_stream_state& vs,
    vorbis_dsp_state& vd, vorbis_block& vb,
    vorbis_comment& vc, vorbis_info& vi, FILE* fp)
{
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    ogg_stream_clear(&vs);
    ogg_sync_clear(&oy);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    if (fp) std::fclose(fp);
}

// 程序入口  
int main(int argc, char** argv)
{
    const char* filename = (argc >= 2) ? argv[1] : "test.ogg";

    FILE* fp = nullptr;
    ogg_sync_state oy;
    ogg_stream_state vs{};
    vorbis_info vi; vorbis_info_init(&vi);
    vorbis_comment vc; vorbis_comment_init(&vc);
    vorbis_dsp_state vd{};
    vorbis_block vb{};

    if (!openFileAndInitSync(filename, fp, oy)) {
        return 1;
    }
    if (!parseVorbisHeaders(fp, oy, vs, vi, vc)) {
        cleanup(oy, vs, vd, vb, vc, vi, fp);
        return 1;
    }
    if (!initVorbisDecoder(vd, vb, vi)) {
        cleanup(oy, vs, vd, vb, vc, vi, fp);
        return 1;
    }
    if (!initMiniaudioDevice(vi)) {
        cleanup(oy, vs, vd, vb, vc, vi, fp);
        return 1;
    }

    std::printf("播放: %s\n采样率: %ld Hz, 声道: %d\n", filename, vi.rate, vi.channels);

    decodeAndPlay(fp, oy, vs, vd, vb, vi); // 边解码边播放  

    // 等待播放完成  
    while (g_audioState.isPlaying.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 清理资源  
    ma_device_uninit(&g_audioState.device);
    cleanup(oy, vs, vd, vb, vc, vi, fp);

    std::printf("播放完成\n");
    return 0;
}



#endif // 0
