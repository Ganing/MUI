/****************************************************************************
 * 标题: OGG Vorbis 音频播放器 (Win32 waveOut, 仅 libogg/libvorbis)
 * 文件: oga test.cpp
 * 版本: 3.0
 * 作者: AEGLOVE / GitHub Copilot
 * 日期: 2025-11-19
 * 功能: 手动解析 OGG 容器与 Vorbis 包, 分块解码并用 waveOut 播放 (流式边解码边播放)
 * 依赖: libogg, libvorbis
 * 环境: Windows 10/11, VS2022, C++17/20, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
****************************************************************************/
#if 0
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <vector>
#include <cstring>

// Ogg/Vorbis (不使用 vorbisfile)
#include <ogg/ogg.h>
#include <vorbis/codec.h>

// 静态库链接
#pragma comment(lib, "libogg.lib")
#pragma comment(lib, "libvorbis.lib")
#pragma comment(lib, "winmm.lib")

// 全局: 已提交但未播放完成的 waveOut 缓冲数量
static std::atomic<long> g_outstanding{ 0 };

// 全局: waveOut 设备句柄(用于在错误时简化清理)
static HWAVEOUT g_waveOut = nullptr;

// 回调: 每个缓冲播放完释放
static void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR /*dwInstance*/, DWORD_PTR dwParam1, DWORD_PTR /*dwParam2*/)
{
    if (uMsg == WOM_DONE) {
        WAVEHDR* hdr = reinterpret_cast<WAVEHDR*>(dwParam1);
        if (hdr) {
            waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
            delete[] hdr->lpData;
            delete hdr;
            --g_outstanding;
        }
    }
}

// 提交一块PCM数据到 waveOut
static bool submitPcm(const char* data, DWORD len)
{
    if (!g_waveOut || len == 0) return true;
    WAVEHDR* hdr = new WAVEHDR{};
    char* buf = new char[len];
    std::memcpy(buf, data, len);
    hdr->lpData = buf;
    hdr->dwBufferLength = len;
    MMRESULT m1 = waveOutPrepareHeader(g_waveOut, hdr, sizeof(WAVEHDR));
    if (m1 != MMSYSERR_NOERROR) {
        std::printf("waveOutPrepareHeader 失败: %u\n", (unsigned)m1);
        delete[] buf; delete hdr; return false;
    }
    MMRESULT m2 = waveOutWrite(g_waveOut, hdr, sizeof(WAVEHDR));
    if (m2 != MMSYSERR_NOERROR) {
        std::printf("waveOutWrite 失败: %u\n", (unsigned)m2);
        waveOutUnprepareHeader(g_waveOut, hdr, sizeof(WAVEHDR));
        delete[] buf; delete hdr; return false;
    }
    ++g_outstanding;
    return true;
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
        if (bytes <= 0) break; // EOF
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

// 初始化 Vorbis 解码状态 (vorbis_dsp_state / vorbis_block)
static bool initVorbisDecoder(vorbis_dsp_state& vd, vorbis_block& vb, vorbis_info& vi)
{
    if (vorbis_synthesis_init(&vd, &vi) != 0) {
        std::printf("vorbis_synthesis_init 失败\n");
        return false;
    }
    vorbis_block_init(&vd, &vb);
    return true;
}

// 初始化 waveOut 输出设备
static bool initWaveOut(const vorbis_info& vi)
{
    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)vi.channels;
    wfx.nSamplesPerSec = (DWORD)vi.rate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(wfx.nChannels * (wfx.wBitsPerSample / 8));
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    MMRESULT mmr = waveOutOpen(&g_waveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION);
    if (mmr != MMSYSERR_NOERROR) {
        std::printf("waveOutOpen 失败, MMRESULT=%u\n", (unsigned)mmr);
        g_waveOut = nullptr;
        return false;
    }
    return true;
}

// 将浮点 PCM 写入累积缓冲并按需刷新为 waveOut 块
static bool writePcmAndMaybeFlush(float** pcm, int samples, int channels,
    std::vector<char>& accum, DWORD bufBytes, long maxQueued)
{
    const size_t needed = (size_t)samples * (size_t)channels * sizeof(int16_t);
    size_t startSize = accum.size();
    accum.resize(startSize + needed);
    int16_t* dst = reinterpret_cast<int16_t*>(accum.data() + startSize);
    for (int i = 0; i < samples; ++i) {
        for (int c = 0; c < channels; ++c) {
            float f = pcm[c][i];
            if (f > 1.0f) f = 1.0f; if (f < -1.0f) f = -1.0f;
            int v = (int)(f * 32767.0f);
            if (v > 32767) v = 32767; if (v < -32768) v = -32768;
            *dst++ = (int16_t)v;
        }
    }
    // 刷新策略
    if (accum.size() >= bufBytes) {
        while (g_outstanding.load() >= maxQueued) Sleep(5);
        if (!submitPcm(accum.data(), (DWORD)accum.size())) return false;
        accum.clear();
    }
    return true;
}

// 解码并播放主循环(边解码边播放)
static void decodeAndPlay(FILE* fp, ogg_sync_state& oy, ogg_stream_state& vs,
    vorbis_dsp_state& vd, vorbis_block& vb, vorbis_info& vi)
{
    const DWORD target_ms = 80; // 目标缓冲时长
    const DWORD bytesPerMs = (DWORD)(vi.rate * vi.channels * 2) / 1000; // 16-bit
    const DWORD targetBytes = (bytesPerMs > 0) ? (bytesPerMs * target_ms) : 32768;
    const DWORD minBuf = 4 * 1024, maxBuf = 128 * 1024;
    DWORD bufBytes = targetBytes;
    if (bufBytes < minBuf) bufBytes = minBuf; if (bufBytes > maxBuf) bufBytes = maxBuf;
    const long maxQueued = 8;

    std::vector<char> accum; accum.reserve(bufBytes);
    bool eos = false;
    ogg_page og{}; ogg_packet op{};

    while (!eos) {
        // 先消费已在 vs 中的包
        while (ogg_stream_packetout(&vs, &op) > 0) {
            if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
            float** pcm = nullptr; int samples = 0;
            while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                if (!writePcmAndMaybeFlush(pcm, samples, vi.channels, accum, bufBytes, maxQueued)) {
                    eos = true; break; // 提交失败
                }
                vorbis_synthesis_read(&vd, samples);
            }
            if (eos) break;
        }
        if (eos) break;

        // 获取更多页面
        int pageout = ogg_sync_pageout(&oy, &og);
        if (pageout == 0) {
            char* buffer = ogg_sync_buffer(&oy, 4096);
            int bytes = (int)std::fread(buffer, 1, 4096, fp);
            if (bytes <= 0) eos = true; else ogg_sync_wrote(&oy, bytes);
            continue;
        }
        if (pageout < 0) continue; // 损坏页面
        if (ogg_page_serialno(&og) == vs.serialno) {
            ogg_stream_pagein(&vs, &og);
            if (ogg_page_eos(&og)) eos = true;
        }
    }

    // 拉完残留
    while (ogg_stream_packetout(&vs, &op) > 0) {
        if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
        float** pcm = nullptr; int samples = 0;
        while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
            if (!writePcmAndMaybeFlush(pcm, samples, vi.channels, accum, bufBytes, maxQueued)) break;
            vorbis_synthesis_read(&vd, samples);
        }
    }

    // 刷新剩余
    submitPcm(accum.data(), (DWORD)accum.size());
    accum.clear();
}

// 等待所有 waveOut 缓冲播放完成
static void waitAllBuffers()
{
    while (g_outstanding.load() > 0) Sleep(10);
}

// 释放所有与 Vorbis/Ogg 相关资源
static void cleanup(ogg_sync_state& oy, ogg_stream_state& vs,
    vorbis_dsp_state& vd, vorbis_block& vb,
    vorbis_comment& vc, vorbis_info& vi, FILE* fp)
{
    if (g_waveOut) { waveOutClose(g_waveOut); g_waveOut = nullptr; }
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    ogg_stream_clear(&vs);
    ogg_sync_clear(&oy);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    if (fp) std::fclose(fp);
}

// 程序入口: 组织整体流程
int main(int argc, char** argv)
{
    timeBeginPeriod(1);
    const char* filename = (argc >= 2) ? argv[1] : "test.ogg";

    FILE* fp = nullptr;
    ogg_sync_state oy; ogg_stream_state vs{};
    ogg_page og{}; // 可选: 用于后续
    vorbis_info vi; vorbis_info_init(&vi);
    vorbis_comment vc; vorbis_comment_init(&vc);
    vorbis_dsp_state vd{}; vorbis_block vb{};

    if (!openFileAndInitSync(filename, fp, oy)) { timeEndPeriod(1); return 1; }
    if (!parseVorbisHeaders(fp, oy, vs, vi, vc)) { cleanup(oy, vs, vd, vb, vc, vi, fp); timeEndPeriod(1); return 1; }
    if (!initVorbisDecoder(vd, vb, vi)) { cleanup(oy, vs, vd, vb, vc, vi, fp); timeEndPeriod(1); return 1; }
    if (!initWaveOut(vi)) { cleanup(oy, vs, vd, vb, vc, vi, fp); timeEndPeriod(1); return 1; }

    std::printf("播放: %s\n采样率: %ld Hz, 声道: %d\n", filename, vi.rate, vi.channels);

    decodeAndPlay(fp, oy, vs, vd, vb, vi); // 边解码边播放
    waitAllBuffers();

    cleanup(oy, vs, vd, vb, vc, vi, fp);
    timeEndPeriod(1);
    std::printf("播放完成\n");
    return 0;
}

#endif // 0

