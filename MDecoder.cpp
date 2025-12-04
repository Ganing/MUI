


#if 1




/****************************************************************************
 * 标题: Ogg多媒体解码器 (Theora+Vorbis)
 * 文件: MDecoder.cpp
 * 版本: 0.1
 * 作者: AEGLOVE
 * 日期: 2025-11-29
 * 功能: 基于Ogg容器的多线程解码器，支持Theora视频与Vorbis音频；
 *      提供`MDecoder`类，输出`VideoFrame`与`AudioPacket`链表结构；
 *      设计考虑线程安全、播放时间戳、搜索代次。
 * 依赖: Win32 API, libogg, libtheora, libvorbis
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/

// 统一使用C++17与Unicode
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>

#define NOMINMAX
#include <windows.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <theora/theoradec.h>

#pragma comment(lib, "libogg.lib")
#pragma comment(lib, "libtheora.lib")
#pragma comment(lib, "libvorbis.lib")
#pragma comment(lib, "winmm.lib")

//---------------------------------------------------------------------------
// 像素格式枚举（可根据项目扩展）
//---------------------------------------------------------------------------
enum class VideoFormat : std::uint32_t {
    Unknown = 0,
    ARGB32  = 1,    // 8:8:8:8
    YUV420P = 2
};

//---------------------------------------------------------------------------
// 优化后的数据结构
// - 使用<stdint.h>/<cstdint>中的固定宽度整数
// - 明确所有权：`pixels`和`samples`使用堆分配，调用者负责释放，或通过辅助函数释放
// - 增加`dataBytes`/`stride`便于渲染或传输
// - 保持链表接口以兼容现有设计
//---------------------------------------------------------------------------
typedef struct VideoFrame
{
    std::uint32_t seek_generation;  // 搜索代次，用于丢弃旧帧
    std::uint32_t playms;           // 预定播放时间戳(ms)
    double        fps;              // 帧率
    std::uint32_t width;            // 像素宽
    std::uint32_t height;           // 像素高
    VideoFormat   format;           // 像素格式
    std::uint32_t stride;           // 每行字节数(如ARGB32: width*4)
    std::uint32_t dataBytes;        // 像素数据总字节数
    std::uint8_t* pixels;           // 原始像素缓冲(堆分配)
    VideoFrame*   next;             // 链表下一项
} VideoFrame;

typedef struct AudioPacket
{
    std::uint32_t seek_generation;  // 搜索代次，用于丢弃旧音频包
    std::uint32_t playms;           // 预定播放起始时间(ms)
    std::int32_t  channels;         // 声道数
    std::int32_t  freq;             // 采样率(Hz)
    std::int32_t  frames;           // 本包包含的帧数量(每帧=channels个样本)
    std::uint32_t dataFloats;       // 样本总数=frames*channels
    float*        samples;          // 线性PCM(f32)，长度为dataFloats
    AudioPacket*  next;             // 链表下一项
} AudioPacket;

// 简单的辅助释放函数，确保易用性
static inline void freeVideoFrameChain(VideoFrame* head) {
    while (head) {
        VideoFrame* nxt = head->next;
        if (head->pixels) { free(head->pixels); head->pixels = nullptr; }
        free(head);
        head = nxt;
    }
}
static inline void freeAudioPacketChain(AudioPacket* head) {
    while (head) {
        AudioPacket* nxt = head->next;
        if (head->samples) { free(head->samples); head->samples = nullptr; }
        free(head);
        head = nxt;
    }
}

//---------------------------------------------------------------------------
// MDecoder: 多线程Ogg解码器 (Theora + Vorbis)
// - 读取Ogg文件，解析并在后台线程解码为VideoFrame/AudioPacket链表
// - 提供拉取接口popVideo/popAudio用于获取已解码的数据
// - 支持停止与代次标识
//---------------------------------------------------------------------------
class MDecoder {
public:
    MDecoder() = default;
    ~MDecoder() { stop(); }

    bool start(const std::wstring& path) {
        stop();
        // 打开文件
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) {
            std::fwprintf(stderr, L"[MDecoder] open failed: %ls\n", path.c_str());
            return false;
        }
        infile = f;
        seekGeneration.store(seekGeneration.load() + 1); // 递增代次
        quit.store(false);
        eof.store(false);
        // 启动线程
        worker = std::thread(&MDecoder::decodeThreadProc, this);
        return true;
    }

    void stop() {
        quit.store(true);
        if (worker.joinable()) worker.join();
        cleanup();
    }

    // 拉取一帧视频（返回链表头，调用者消费后负责释放）
    VideoFrame* popVideo() {
        std::lock_guard<std::mutex> lock(mtxVideo);
        VideoFrame* head = videoHead;
        videoHead = nullptr;
        videoTail = nullptr;
        return head;
    }

    // 拉取一段音频（返回链表头，调用者消费后负责释放）
    AudioPacket* popAudio() {
        std::lock_guard<std::mutex> lock(mtxAudio);
        AudioPacket* head = audioHead;
        audioHead = nullptr;
        audioTail = nullptr;
        return head;
    }

private:
    // 线程过程：参考SDLPlayer5.cpp的解码流程，输出链表
    void decodeThreadProc() {
        initCodecStates();
        if (!probeHeaders()) { cleanup(); return; }
        if (!initDecoders()) { cleanup(); return; }
        decodeLoop();
        cleanupCodecStates();
        eof.store(true);
    }

    // 初始化基础Ogg/Theora/Vorbis状态
    void initCodecStates() {
        ogg_sync_init(&oy);
        vorbis_info_init(&vi);
        vorbis_comment_init(&vc);
        th_info_init(&ti);
        th_comment_init(&tc);
    }
    void cleanupCodecStates() {
        if (hasVorbis) {
            ogg_stream_clear(&vo);
            vorbis_block_clear(&vb);
            vorbis_dsp_clear(&vd);
            vorbis_comment_clear(&vc);
            vorbis_info_clear(&vi);
        }
        if (hasTheora) {
            ogg_stream_clear(&to);
            if (td) th_decode_free(td);
            th_comment_clear(&tc);
            th_info_clear(&ti);
        }
        ogg_sync_clear(&oy);
    }

    bool probeHeaders() {
        ogg_packet op{};
        hasTheora = false; hasVorbis = false; theoraHeaders = 0; vorbisHeaders = 0;
        // 识别流
        while (!quit.load() && (!hasTheora || !hasVorbis)) {
            if (bufferData() == 0) { std::fprintf(stderr, "[MDecoder] EOF while probing.\n"); return false; }
            while (ogg_sync_pageout(&oy, &og) > 0) {
                ogg_stream_state test{};
                if (!ogg_page_bos(&og)) {
                    if (hasTheora) ogg_stream_pagein(&to, &og);
                    if (hasVorbis) ogg_stream_pagein(&vo, &og);
                    continue;
                }
                ogg_stream_init(&test, ogg_page_serialno(&og));
                ogg_stream_pagein(&test, &og);
                ogg_stream_packetout(&test, &op);
                if (!hasTheora && th_decode_headerin(&ti, &tc, &ts, &op) >= 0) {
                    std::memcpy(&to, &test, sizeof(test)); hasTheora = true; theoraHeaders = 1;
                } else if (!hasVorbis && vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {
                    std::memcpy(&vo, &test, sizeof(test)); hasVorbis = true; vorbisHeaders = 1;
                } else {
                    ogg_stream_clear(&test);
                }
            }
        }
        if (!hasTheora && !hasVorbis) return false;
        // 解析剩余头
        while ((hasTheora && theoraHeaders < 3) || (hasVorbis && vorbisHeaders < 3)) {
            while (hasTheora && theoraHeaders < 3 && ogg_stream_packetout(&to, &op)) {
                if (!th_decode_headerin(&ti, &tc, &ts, &op)) { std::fprintf(stderr, "[MDecoder] Theora header error.\n"); return false; }
                ++theoraHeaders;
            }
            while (hasVorbis && vorbisHeaders < 3 && ogg_stream_packetout(&vo, &op)) {
                if (vorbis_synthesis_headerin(&vi, &vc, &op)) { std::fprintf(stderr, "[MDecoder] Vorbis header error.\n"); return false; }
                ++vorbisHeaders;
            }
            if (ogg_sync_pageout(&oy, &og) > 0) {
                if (hasTheora) ogg_stream_pagein(&to, &og);
                if (hasVorbis) ogg_stream_pagein(&vo, &og);
            } else {
                int ret = bufferData(); if (ret == 0) { std::fprintf(stderr, "[MDecoder] EOF while reading headers.\n"); return false; }
            }
        }
        return true;
    }

    bool initDecoders() {
        if (hasTheora) {
            td = th_decode_alloc(&ti, ts);
            vWidth = ti.pic_width;
            vHeight = ti.pic_height;
            vFps = (ti.fps_denominator != 0) ? (double)ti.fps_numerator / (double)ti.fps_denominator : 30.0;
            if (vFps <= 0.0) vFps = 30.0;
        }
        if (hasVorbis) {
            if (vorbis_synthesis_init(&vd, &vi) != 0) return false;
            if (vorbis_block_init(&vd, &vb) != 0) return false;
            aFreq = vi.rate;
            aChannels = (vi.channels > 2) ? 2 : (vi.channels < 1 ? 1 : vi.channels);
        }
        startTicks = timeGetTime();
        return true;
    }

    int bufferData() {
        char* buffer = (char*)ogg_sync_buffer(&oy, 4096);
        int bytes = (int)std::fread(buffer, 1, 4096, infile);
        ogg_sync_wrote(&oy, bytes);
        return bytes;
    }

    // 简单的YUV420 -> ARGB32转换（BT.601近似）
    void yuv420ToARGB(const th_ycbcr_buffer& yuv, std::uint8_t* outPixels, std::uint32_t stride) {
        const int w = vWidth;
        const int h = vHeight;
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* Yp = (const std::uint8_t*)yuv[0].data + y * yuv[0].stride;
            const std::uint8_t* Up = (const std::uint8_t*)yuv[1].data + (y / 2) * yuv[1].stride;
            const std::uint8_t* Vp = (const std::uint8_t*)yuv[2].data + (y / 2) * yuv[2].stride;
            std::uint32_t* dst = (std::uint32_t*)(outPixels + (std::size_t)y * stride);
            for (int x = 0; x < w; ++x) {
                int Yv = Yp[x];
                int Uv = Up[x / 2];
                int Vv = Vp[x / 2];
                int C = Yv - 16;
                int D = Uv - 128;
                int E = Vv - 128;
                int R = (298 * C + 409 * E + 128) >> 8;
                int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
                int B = (298 * C + 516 * D + 128) >> 8;
                if (R < 0) R = 0; if (R > 255) R = 255;
                if (G < 0) G = 0; if (G > 255) G = 255;
                if (B < 0) B = 0; if (B > 255) B = 255;
                dst[x] = (0xFFu << 24) | (std::uint32_t(R) << 16) | (std::uint32_t(G) << 8) | (std::uint32_t)B;
            }
        }
    }

    void pushVideoFrame(VideoFrame* vf) {
        std::lock_guard<std::mutex> lock(mtxVideo);
        if (!videoHead) { videoHead = vf; videoTail = vf; }
        else { videoTail->next = vf; videoTail = vf; }
    }
    void pushAudioPacket(AudioPacket* ap) {
        std::lock_guard<std::mutex> lock(mtxAudio);
        if (!audioHead) { audioHead = ap; audioTail = ap; }
        else { audioTail->next = ap; audioTail = ap; }
    }

    void decodeLoop() {
        ogg_packet op{};
        th_ycbcr_buffer yuv{};
        bool alive = true;
        std::uint32_t gen = seekGeneration.load();
        while (!quit.load() && alive) {
            bool aPkt = false, vPkt = false;
            if (hasVorbis) {
                if (ogg_stream_packetout(&vo, &op) > 0) aPkt = true;
            }
            if (hasTheora && !aPkt) {
                if (ogg_stream_packetout(&to, &op) > 0) vPkt = true;
            }
            if (!aPkt && !vPkt) {
                if (bufferData() == 0) eof.store(true);
                while (ogg_sync_pageout(&oy, &og) > 0) {
                    if (hasTheora && ogg_page_serialno(&og) == to.serialno) ogg_stream_pagein(&to, &og);
                    if (hasVorbis && ogg_page_serialno(&og) == vo.serialno) ogg_stream_pagein(&vo, &og);
                }
                if (eof.load()) {
                    // 尝试最后再取一次包
                    if (hasVorbis && ogg_stream_packetout(&vo, &op) > 0) aPkt = true;
                    else if (hasTheora && ogg_stream_packetout(&to, &op) > 0) vPkt = true;
                    if (!aPkt && !vPkt) alive = false;
                }
            }
            // 音频
            if (aPkt) {
                if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
                float** pcm = nullptr; int samples = 0;
                while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                    // 输出为f32 interleaved
                    int outCh = aChannels;
                    std::size_t floats = (std::size_t)samples * (std::size_t)outCh;
                    float* buf = (float*)std::malloc(sizeof(float) * floats);
                    if (!buf) { vorbis_synthesis_read(&vd, samples); break; }
                    float* dst = buf;
                    for (int i = 0; i < samples; ++i) {
                        float L = 0.f, R = 0.f;
                        if (vi.channels == 1) { L = R = pcm[0][i]; }
                        else {
                            L = pcm[0][i]; R = pcm[1][i];
                            if (vi.channels > 2) {
                                for (int ch = 2; ch < vi.channels; ++ch) {
                                    float v = pcm[ch][i]; float scale = 0.5f / (vi.channels - 1);
                                    L += v * scale; R += v * scale;
                                }
                            }
                        }
                        if (outCh == 1) { *dst++ = L; }
                        else { *dst++ = L; *dst++ = R; }
                    }
                    // 计算播放时间戳(ms)
                    std::uint32_t nowms = (std::uint32_t)(timeGetTime() - startTicks);
                    AudioPacket* ap = (AudioPacket*)std::malloc(sizeof(AudioPacket));
                    if (!ap) { std::free(buf); vorbis_synthesis_read(&vd, samples); break; }
                    ap->seek_generation = gen;
                    ap->playms = nowms;
                    ap->channels = outCh;
                    ap->freq = aFreq;
                    ap->frames = samples;
                    ap->dataFloats = (std::uint32_t)floats;
                    ap->samples = buf;
                    ap->next = nullptr;
                    pushAudioPacket(ap);
                    vorbis_synthesis_read(&vd, samples);
                }
            }
            // 视频
            if (vPkt) {
                ogg_int64_t gran = -1;
                if (th_decode_packetin(td, &op, &gran) == 0) {
                    double vtime = th_granule_time(td, gran);
                    if (vtime < 0.0) vtime = (double)(timeGetTime() - startTicks) / 1000.0;
                    // 分配像素并转换
                    std::uint32_t stride = (std::uint32_t)vWidth * 4u;
                    std::uint32_t bytes = stride * (std::uint32_t)vHeight;
                    std::uint8_t* px = (std::uint8_t*)std::malloc(bytes);
                    if (!px) { /* OOM */ }
                    th_decode_ycbcr_out(td, yuv);
                    yuv420ToARGB(yuv, px, stride);
                    // 计算播放时间戳(ms)
                    std::uint32_t playms = (std::uint32_t)std::llround(vtime * 1000.0);
                    VideoFrame* vf = (VideoFrame*)std::malloc(sizeof(VideoFrame));
                    if (!vf) { std::free(px); }
                    else {
                        vf->seek_generation = gen;
                        vf->playms = playms;
                        vf->fps = vFps;
                        vf->width = (std::uint32_t)vWidth;
                        vf->height = (std::uint32_t)vHeight;
                        vf->format = VideoFormat::ARGB32;
                        vf->stride = stride;
                        vf->dataBytes = bytes;
                        vf->pixels = px;
                        vf->next = nullptr;
                        pushVideoFrame(vf);
                    }
                }
            }
        }
    }

    void cleanup() {
        if (infile) { std::fclose(infile); infile = nullptr; }
        // 清空队列
        {
            std::lock_guard<std::mutex> lock(mtxVideo);
            freeVideoFrameChain(videoHead); videoHead = nullptr; videoTail = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(mtxAudio);
            freeAudioPacketChain(audioHead); audioHead = nullptr; audioTail = nullptr;
        }
    }

private:
    // 文件与线程
    FILE* infile = nullptr;
    std::thread worker;
    std::atomic<bool> quit{ false };
    std::atomic<bool> eof{ false };
    std::atomic<std::uint32_t> seekGeneration{ 0 };

    // 输出队列（链表头尾）
    VideoFrame* videoHead = nullptr;
    VideoFrame* videoTail = nullptr;
    AudioPacket* audioHead = nullptr;
    AudioPacket* audioTail = nullptr;
    std::mutex mtxVideo;
    std::mutex mtxAudio;

    // Ogg/Theora/Vorbis状态
    ogg_sync_state   oy{};
    ogg_page         og{};

    ogg_stream_state to{}; // Theora
    th_info          ti{};
    th_comment       tc{};
    th_dec_ctx*      td = nullptr;
    th_setup_info*   ts = nullptr;

    ogg_stream_state vo{}; // Vorbis
    vorbis_info      vi{};
    vorbis_comment   vc{};
    vorbis_dsp_state vd{};
    vorbis_block     vb{};

    bool             hasTheora = false;
    bool             hasVorbis = false;
    int              theoraHeaders = 0;
    int              vorbisHeaders = 0;

    // 运行时信息
    int vWidth = 0, vHeight = 0;
    double vFps = 0.0;
    int aFreq = 0, aChannels = 0;
    DWORD startTicks = 0;
};


#endif // 0

//---------------------------------------------------------------------------
// 测试/示例代码（可折叠）
//---------------------------------------------------------------------------
#if 0

/****************************************************************************
 * 标题: MDecoder示例 (Theora+Vorbis链表输出)
 * 文件: MDecoder.cpp
 * 版本: 0.1
 * 作者: AEGLOVE
 * 日期: 2025-11-29
 * 功能: 展示如何使用`MDecoder`解码Ogg文件，并提取视频帧与音频包；
 *      控制台打印信息，并演示释放链表资源；
 * 依赖: Win32 API, libogg, libtheora, libvorbis
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/

#include <iostream>
#include <locale>
#include <codecvt>

static void printVideoFrames(VideoFrame* head) {
    std::size_t count = 0, bytes = 0;
    for (VideoFrame* p = head; p; p = p->next) { ++count; bytes += p->dataBytes; }
    std::cout << "[Demo] VideoFrames count=" << count << ", bytes=" << bytes << std::endl;
}
static void printAudioPackets(AudioPacket* head) {
    std::size_t count = 0, floats = 0;
    for (AudioPacket* p = head; p; p = p->next) { ++count; floats += p->dataFloats; }
    std::cout << "[Demo] AudioPackets count=" << count << ", floats=" << floats << std::endl;
}

int main(int argc, char** argv) {
    timeBeginPeriod(1);
    // C++17中std::wstring_convert已弃用。
    // 在Windows上，更现代的替代方法是使用MultiByteToWideChar(CP_UTF8, ...)。
    // 但为了保持代码简洁并解决直接的编译错误，我们暂时只包含必要的头文件。
    std::wstring path = (argc >= 2) ? std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(argv[1]) : L"test.ogg";

    MDecoder dec;
    if (!dec.start(path)) {
        std::wcerr << L"[Demo] Failed to start decoder for: " << path << std::endl;
        timeEndPeriod(1);
        return 1;
    }

    // 简单轮询，获取数据
    for (int i = 0; i < 50; ++i) { // 轮询约5秒
        Sleep(100);
        VideoFrame* vf = dec.popVideo();
        AudioPacket* ap = dec.popAudio();
        if (vf) { printVideoFrames(vf); freeVideoFrameChain(vf); }
        if (ap) { printAudioPackets(ap); freeAudioPacketChain(ap); }
    }

    dec.stop();
    timeEndPeriod(1);
    return 0;
}

#endif

//---------------------------------------------------------------------------
// 新示例：使用GDI+ + WaveOut 播放 Ogg(Theora+Vorbis)，并做A/V同步
//---------------------------------------------------------------------------
#if 0

#include <gdiplus.h>
#include <mmsystem.h>
#include <deque>
#include <algorithm>

#pragma comment(lib, "Gdiplus.lib")

namespace DemoPlayer {

struct SharedVideo {
    std::mutex mtx;
    VideoFrame* current = nullptr; // 持有当前用于绘制的帧
};

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void drawFrameToWindow(HWND hwnd, const VideoFrame* vf)
{
    if (!hwnd || !vf || !vf->pixels) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;

    HDC hdc = GetDC(hwnd);
    if (!hdc) return;

    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    const UINT w = vf->width;
    const UINT h = vf->height;
    const INT stride = (INT)vf->stride;

    Gdiplus::Bitmap bmp(w, h, stride, PixelFormat32bppARGB, (BYTE*)vf->pixels);

    // 按窗口大小等比缩放并居中
    double sx = (double)cw / (double)w;
    double sy = (double)ch / (double)h;
    double s = std::min(sx, sy);
    int dw = std::max(1, (int)std::llround(w * s));
    int dh = std::max(1, (int)std::llround(h * s));
    int dx = (cw - dw) / 2;
    int dy = (ch - dh) / 2;

    g.DrawImage(&bmp, Gdiplus::Rect(dx, dy, dw, dh));

    ReleaseDC(hwnd, hdc);
}

static void freeVideoFrame(VideoFrame*& vf)
{
    if (!vf) return;
    if (vf->pixels) { free(vf->pixels); vf->pixels = nullptr; }
    free(vf); vf = nullptr;
}

} // namespace DemoPlayer

int main(int argc, char** argv)
{
    using namespace DemoPlayer;

    timeBeginPeriod(1);

    std::wstring path = L"test.ogg";
    if (argc >= 2) {
        // 尝试将UTF-8的argv[1]转为UTF-16
        int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wbuf; wbuf.resize((size_t)wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wbuf.data(), wlen);
            if (!wbuf.empty()) path = wbuf;
        }
    }

    // 初始化GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        std::fprintf(stderr, "[Demo] GdiplusStartup failed.\n");
        return 2;
    }

    // 启动解码器
    MDecoder dec;
    if (!dec.start(path)) {
        std::fwprintf(stderr, L"[Demo] Failed to start decoder for: %ls\n", path.c_str());
        Gdiplus::GdiplusShutdown(gdiplusToken);
        timeEndPeriod(1);
        return 1;
    }

    // 创建窗口
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    const wchar_t* kCls = L"MDecoderPlayerWnd";
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = kCls; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    // 尝试获取第一帧来确定窗口大小
    int wndW = 800, wndH = 480;
    {
        VideoFrame* vfFirst = nullptr;
        for (int i = 0; i < 50 && !vfFirst; ++i) {
            Sleep(50);
            vfFirst = dec.popVideo();
        }
        if (vfFirst) {
            wndW = (int)vfFirst->width;
            wndH = (int)vfFirst->height;
        }
        // 我们会在之后绘制vfFirst或释放它（推回队列）
        if (vfFirst) {
            // 直接先用作第一帧显示
            // 暂时存下，后续作为current
        }
    }

    HWND hwnd = CreateWindowExW(0, kCls, L"MDecoder GDI+ + WaveOut Player",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, wndW + 16, wndH + 39, // 简单估计边框
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "[Demo] CreateWindow failed.\n");
        dec.stop();
        Gdiplus::GdiplusShutdown(gdiplusToken);
        timeEndPeriod(1);
        return 3;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 音频输出初始化（等到拿到第一包音频再初始化）
    HWAVEOUT hwo = nullptr;
    WAVEFORMATEX wfx{};
    std::vector<WAVEHDR*> activeHdrs; activeHdrs.reserve(64);

    DWORD audioSampleRate = 0;
    DWORD audioClockMs = 0;

    std::deque<VideoFrame*> vQueue;
    SharedVideo shared;

    // 主循环
    bool running = true;
    DWORD lastPaintTick = timeGetTime();

    while (running) {
        // 消息泵
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        // 回收已完成的音频缓冲
        if (hwo) {
            for (size_t i = 0; i < activeHdrs.size();) {
                WAVEHDR* hdr = activeHdrs[i];
                if (hdr->dwFlags & WHDR_DONE) {
                    waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
                    if (hdr->lpData) free(hdr->lpData);
                    free(hdr);
                    activeHdrs.erase(activeHdrs.begin() + i);
                } else {
                    ++i;
                }
            }
        }

        // 从解码器拉取数据
        if (VideoFrame* vfList = dec.popVideo()) {
            for (VideoFrame* p = vfList; p; ) {
                VideoFrame* nxt = p->next; p->next = nullptr; vQueue.push_back(p); p = nxt;
            }
        }
        if (AudioPacket* apList = dec.popAudio()) {
            for (AudioPacket* p = apList; p; ) {
                AudioPacket* nxt = p->next; p->next = nullptr;
                // 首次打开音频设备
                if (!hwo) {
                    audioSampleRate = (DWORD)p->freq;
                    wfx.wFormatTag = WAVE_FORMAT_PCM;
                    wfx.nChannels = (WORD)p->channels;
                    if (wfx.nChannels < 1) wfx.nChannels = 1; if (wfx.nChannels > 2) wfx.nChannels = 2;
                    wfx.nSamplesPerSec = audioSampleRate;
                    wfx.wBitsPerSample = 16;
                    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
                    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
                    MMRESULT mmr = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
                    if (mmr != MMSYSERR_NOERROR) {
                        std::fprintf(stderr, "[Demo] waveOutOpen failed: %u\n", (unsigned)mmr);
                        hwo = nullptr;
                    }
                }

                if (hwo) {
                    // float32 -> int16 转换并提交
                    size_t frames = (size_t)p->frames;
                    size_t channels = (size_t)wfx.nChannels;
                    size_t samples = frames * channels;
                    int16_t* pcm16 = (int16_t*)malloc(samples * sizeof(int16_t));
                    if (pcm16) {
                        for (size_t i = 0; i < samples; ++i) {
                            float v = p->samples[i];
                            if (v > 1.f) v = 1.f; else if (v < -1.f) v = -1.f;
                            int s = (int)std::lrintf(v * 32767.0f);
                            if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                            pcm16[i] = (int16_t)s;
                        }
                        WAVEHDR* hdr = (WAVEHDR*)malloc(sizeof(WAVEHDR));
                        if (hdr) {
                            std::memset(hdr, 0, sizeof(WAVEHDR));
                            hdr->lpData = (LPSTR)pcm16;
                            hdr->dwBufferLength = (DWORD)(samples * sizeof(int16_t));
                            if (waveOutPrepareHeader(hwo, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                                if (waveOutWrite(hwo, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                                    activeHdrs.push_back(hdr);
                                } else {
                                    waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
                                    free(pcm16); free(hdr);
                                }
                            } else {
                                free(pcm16); free(hdr);
                            }
                        } else {
                            free(pcm16);
                        }
                    }
                }

                // 释放AudioPacket本身
                if (p->samples) free(p->samples);
                free(p);
                p = nxt;
            }
        }

        // 获取音频时钟（毫秒）
        if (hwo && audioSampleRate > 0) {
            MMTIME mt{}; mt.wType = TIME_SAMPLES;
            if (waveOutGetPosition(hwo, &mt, sizeof(mt)) == MMSYSERR_NOERROR) {
                if (mt.wType == TIME_SAMPLES) {
                    audioClockMs = (DWORD)((mt.u.sample * 1000ull) / (audioSampleRate));
                } else if (mt.wType == TIME_MS) {
                    audioClockMs = mt.u.ms;
                } else if (mt.wType == TIME_BYTES) {
                    DWORD bytes = mt.u.cb;
                    DWORD samples = bytes / wfx.nBlockAlign; // frames
                    audioClockMs = (DWORD)((samples * 1000ull) / (audioSampleRate));
                }
            }
        }

        // 依据音频时钟显示视频
        const DWORD nowTick = timeGetTime();
        bool needPaint = false;
        while (!vQueue.empty()) {
            VideoFrame* vf = vQueue.front();
            // 若还未打开音频，则用系统时间推动
            DWORD refClock = (hwo ? audioClockMs : (nowTick));
            DWORD vms = vf->playms;
            // 允许一点提前/滞后
            if (!hwo) {
                // 当还没音频时，用当前时间近似判断
                needPaint = true;
            } else if (vms <= refClock + 10) {
                needPaint = true;
            } else {
                break;
            }

            if (needPaint) {
                // 交换到当前帧
                {
                    std::lock_guard<std::mutex> lk(shared.mtx);
                    if (shared.current) freeVideoFrame(shared.current);
                    shared.current = vf;
                }
                vQueue.pop_front();
            }
        }

        // 定期重绘
        if (nowTick - lastPaintTick >= 15) { // ~60fps刷新
            VideoFrame* cur = nullptr;
            {
                std::lock_guard<std::mutex> lk(shared.mtx);
                cur = shared.current;
            }
            if (cur) drawFrameToWindow(hwnd, cur);
            lastPaintTick = nowTick;
        }

        Sleep(1);
    }

    // 清理
    dec.stop();

    // 回收视频帧
    {
        std::lock_guard<std::mutex> lk(shared.mtx);
        if (shared.current) { freeVideoFrame(shared.current); }
    }
    for (VideoFrame* vf : vQueue) { freeVideoFrame(vf); }
    vQueue.clear();

    if (hwo) {
        waveOutReset(hwo);
        for (WAVEHDR* hdr : activeHdrs) {
            waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
            if (hdr->lpData) free(hdr->lpData);
            free(hdr);
        }
        activeHdrs.clear();
        waveOutClose(hwo);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    timeEndPeriod(1);
    return 0;
}

#endif
