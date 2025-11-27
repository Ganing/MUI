/****************************************************************************
 * 标题: Ogg Theora Win32 Player - GDI/WaveOut测试版
 * 文件: SDLPlayer4.cpp
 * 版本: 2.2
 * 作者: AEGLOVE
 * 日期: 2025-11-29
 * 功能: 使用Win32窗口+GDI渲染视频，waveOut播放音频；基于Theora/Vorbis解码
 *      - v2.2: 彻底修复音频末尾截断问题，优化渲染循环与时钟同步，提升流畅度
 * 依赖: Win32 API, GDI, winmm(waveOut), libogg, libtheora, libvorbis
 * 环境: Windows10/11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/

#if 1
#define NOMINMAX
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <csignal>
#include <vector>
#include <string>
#include <algorithm>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <mmsystem.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <theora/theoradec.h>

#pragma comment(lib, "libogg.lib")
#pragma comment(lib, "libtheora.lib")
#pragma comment(lib, "libvorbis.lib")
#pragma comment(lib, "winmm.lib")

// 全局状态
static ogg_sync_state   oy;
static ogg_page         og;
static ogg_stream_state vo;
static ogg_stream_state to;
static th_info      ti;
static th_comment   tc;
static th_dec_ctx       *td = nullptr;
static th_setup_info    *ts = nullptr;
static vorbis_info      vi;
static vorbis_dsp_state vd;
static vorbis_block     vb;
static vorbis_comment   vc;
static th_pixel_fmt     px_fmt;

static int              theora_p = 0;
static int              vorbis_p = 0;
static int              theora_headers = 0;
static int              vorbis_headers = 0;

// Win32/GDI
static HINSTANCE g_hInst = nullptr;
static HWND g_hWnd = nullptr;
static int g_width = 0, g_height = 0;
static std::vector<uint32_t> g_frameARGB; // ARGB32 framebuffer
static BITMAPINFO g_bmi = {};
// 视频帧率与节奏
static double g_videoFps = 0.0;
static DWORD g_frameIntervalMs = 33; // default ~30fps
static DWORD g_nextPresentMs = 0;
static bool g_haveFrame = false;
static double g_pendingVTime = -1.0; // seconds of pending frame
struct QueuedFrame { double vtime; std::vector<uint32_t> pixels; };
static std::deque<QueuedFrame> g_videoQueue; // ordered by decode
static double g_lastPresentedTime = -1.0;
static double VIDEO_DROP_LATE = 0.30;    // 基于FPS的容差（运行时调整）
static double VIDEO_DECODE_AHEAD = 2.00; // 动态：约 1-2 秒
static double VIDEO_RENDER_SLACK = 0.01; // 动态：约 1/2 帧
static size_t g_maxVideoQueueFrames = 120;   // 最大缓存帧数（约2秒@60fps）
static DWORD g_startTicks = 0;                 // master timeline start

// 线程与同步
static std::mutex g_queueMutex;
static std::condition_variable g_queueCond;
static std::thread g_decodeThread;
static std::atomic<bool> g_quitFlag(false);
static std::atomic<bool> g_eofFlag(false);

// waveOut
static HWAVEOUT g_hWave = nullptr;
static WAVEFORMATEX g_wfx = {};
static const int AUDIO_BUFFER_SAMPLES = 1024; // not used in callback mode
static const int AUDIO_BUFFERS = 0;           // not used in callback mode
static std::atomic<long> g_outstanding{0};
static std::atomic<uint64_t> g_bytesCompleted{0};
static std::atomic<uint64_t> g_bytesSubmitted{0};
static std::vector<char> g_audioAccum;
static DWORD g_targetAudioBytes = 0; // 目标一次提交的字节数（~80ms）

// 简单输入缓冲
int buffer_data(FILE* in, ogg_sync_state* oy) {
    char* buffer = ogg_sync_buffer(oy, 4096);
    int bytes = (int)fread(buffer, 1, 4096, in);
    ogg_sync_wrote(oy, bytes);
    return bytes;
}

static void render_frame() {
    if (!g_hWnd || g_frameARGB.empty()) return;
    HDC hdc = GetDC(g_hWnd);
    if (hdc) {
        SetDIBitsToDevice(hdc, 0, 0, g_width, g_height, 0, 0, 0, g_height, g_frameARGB.data(), &g_bmi, DIB_RGB_COLORS);
        ReleaseDC(g_hWnd, hdc);
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // 主动渲染，WM_PAINT只在必要时（如窗口恢复）重绘最后一帧
        if (!g_frameARGB.empty()) {
            SetDIBitsToDevice(hdc, 0, 0, g_width, g_height, 0, 0, 0, g_height, g_frameARGB.data(), &g_bmi, DIB_RGB_COLORS);
        }
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_DESTROY:
        g_quitFlag = true;
        g_queueCond.notify_all();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static void createWindowGDI(int w, int h) {
    g_width = w; g_height = h;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(IDC_ARROW));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"TheoraGDIPlayer";
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"Theora GDI Player",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w + 16, h + 39,
        NULL, NULL, g_hInst, NULL);
    
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = g_width;
    g_bmi.bmiHeader.biHeight = -g_height; // top-down
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    g_frameARGB.resize((size_t)w * (size_t)h);
}

static void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR /*dwInstance*/, DWORD_PTR dwParam1, DWORD_PTR /*dwParam2*/) {
    if (uMsg == WOM_DONE) {
        WAVEHDR* hdr = reinterpret_cast<WAVEHDR*>(dwParam1);
        if (hdr) {
            if (hdr->dwBufferLength) {
                g_bytesCompleted += hdr->dwBufferLength;
            }
            if (hdr->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
            if (hdr->lpData) delete[] hdr->lpData;
            delete hdr;
            --g_outstanding;
        }
    }
}

static void openWaveOut() {
    g_wfx.wFormatTag = WAVE_FORMAT_PCM;
    g_wfx.nChannels = (WORD)((vi.channels > 2) ? 2 : (vi.channels < 1 ? 1 : vi.channels)); // waveOut通常只支持1或2声道
    g_wfx.nSamplesPerSec = vi.rate;
    g_wfx.wBitsPerSample = 16;
    g_wfx.nBlockAlign = (g_wfx.nChannels * g_wfx.wBitsPerSample) / 8;
    g_wfx.nAvgBytesPerSec = g_wfx.nSamplesPerSec * g_wfx.nBlockAlign;

    MMRESULT mm = waveOutOpen(&g_hWave, WAVE_MAPPER, &g_wfx, (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION);
    if (mm != MMSYSERR_NOERROR) {
        fprintf(stderr, "waveOutOpen failed: %u\n", (unsigned)mm);
        exit(1);
    }
    fprintf(stderr, "Audio opened: %u Hz, %u ch, %u-bit\n", (unsigned)g_wfx.nSamplesPerSec, (unsigned)g_wfx.nChannels, (unsigned)g_wfx.wBitsPerSample);
    g_bytesCompleted = 0;
    g_bytesSubmitted = 0;
    g_outstanding = 0;
    // 目标缓冲大小：约80ms，夹在 4KB..128KB 之内
    DWORD bytesPerMs = (g_wfx.nAvgBytesPerSec > 0) ? (g_wfx.nAvgBytesPerSec / 1000) : 0;
    DWORD target = bytesPerMs ? bytesPerMs * 80u : 32768u;
    if (target < 4096u) target = 4096u; if (target > 131072u) target = 131072u;
    g_targetAudioBytes = target;
    g_audioAccum.clear(); g_audioAccum.reserve(g_targetAudioBytes);
}

static void closeWaveOut() {
    if (g_hWave) {
        // waveOutReset(g_hWave); // 过于粗暴，会丢弃缓冲区，导致音频截断。移除。
        // 等待所有缓冲回调完成
        while (g_outstanding.load() > 0) Sleep(10);
        waveOutClose(g_hWave);
        g_hWave = nullptr;
    }
}

static bool submitPcm(const char* data, DWORD len) {
    if (!g_hWave || len == 0) return true;
    WAVEHDR* hdr = new WAVEHDR{};
    char* buf = new char[len];
    memcpy(buf, data, len);
    hdr->lpData = buf;
    hdr->dwBufferLength = len;
    MMRESULT m1 = waveOutPrepareHeader(g_hWave, hdr, sizeof(WAVEHDR));
    if (m1 != MMSYSERR_NOERROR) {
        delete[] buf; delete hdr; return false;
    }
    MMRESULT m2 = waveOutWrite(g_hWave, hdr, sizeof(WAVEHDR));
    if (m2 != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(g_hWave, hdr, sizeof(WAVEHDR));
        delete[] buf; delete hdr; return false;
    }
    ++g_outstanding;
    g_bytesSubmitted.fetch_add(len);
    return true;
}


static inline uint64_t getAudioQueuedBytes() {
    uint64_t sub = g_bytesSubmitted.load();
    uint64_t comp = g_bytesCompleted.load();
    return (sub > comp) ? (sub - comp) : 0ull;
}

static double getAudioPlayedSec() {
    if (!g_hWave || g_wfx.nAvgBytesPerSec == 0) {
        // 如果没有音频，则使用系统时间作为主时钟
        if (g_startTicks == 0) g_startTicks = timeGetTime();
        return (double)(timeGetTime() - g_startTicks) / 1000.0;
    }
    double played = (double)g_bytesCompleted.load() / (double)g_wfx.nAvgBytesPerSec;
    return played;
}

static void yuv420_to_argb32(const th_ycbcr_buffer& yuv, std::vector<uint32_t>& argb_buffer) {
    // 简单的YUV420到ARGB32转码（BT.601近似）
    const int w = g_width;
    const int h = g_height;
    if (argb_buffer.size() != (size_t)w * h) {
        argb_buffer.resize((size_t)w * h);
    }

    for (int y = 0; y < h; ++y) {
        const uint8_t* Yp = (const uint8_t*)yuv[0].data + y * yuv[0].stride;
        const uint8_t* Up = (const uint8_t*)yuv[1].data + (y / 2) * yuv[1].stride;
        const uint8_t* Vp = (const uint8_t*)yuv[2].data + (y / 2) * yuv[2].stride;
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
            argb_buffer[(size_t)y * w + x] = (0xFFu << 24) | (uint32_t(R) << 16) | (uint32_t(G) << 8) | (uint32_t)B;
        }
    }
}

void decode_loop(FILE* infile) {
    ogg_packet op;
    th_ycbcr_buffer yuv;
    bool streams_alive = true;

    // 预缓冲音频 ~600ms，保证开播平滑
    if (vorbis_p) {
        const uint64_t preBytes = (uint64_t)g_wfx.nAvgBytesPerSec * 600ull / 1000ull;
        while (!g_quitFlag && getAudioQueuedBytes() < preBytes) {
            // 先消费已有音频包
            while (!g_quitFlag && ogg_stream_packetout(&vo, &op) > 0) {
                if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
                float** pcm = nullptr; int samples = 0;
                while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                    const int outCh = g_wfx.nChannels;
                    const size_t needed = (size_t)samples * (size_t)outCh * sizeof(int16_t);
                    size_t start = g_audioAccum.size();
                    g_audioAccum.resize(start + needed);
                    int16_t* dst = reinterpret_cast<int16_t*>(g_audioAccum.data() + start);
                    for (int i = 0; i < samples; ++i) {
                        float L = 0.f, R = 0.f;
                        if (vi.channels == 1) { L = R = pcm[0][i]; }
                        else { L = pcm[0][i]; R = pcm[1][i]; if (vi.channels > 2) { for (int ch = 2; ch < vi.channels; ++ch) { float v = pcm[ch][i]; float scale = 0.5f / (vi.channels - 1); L += v * scale; R += v * scale; } } }
                        int sL = (int)lround(L * 32767.0f); if (sL > 32767) sL = 32767; if (sL < -32768) sL = -32768;
                        if (outCh == 1) { *dst++ = (int16_t)sL; }
                        else { int sR = (int)lround(R * 32767.0f); if (sR > 32767) sR = 32767; if (sR < -32768) sR = -32768; *dst++ = (int16_t)sL; *dst++ = (int16_t)sR; }
                    }
                    vorbis_synthesis_read(&vd, samples);
                    if (g_audioAccum.size() >= g_targetAudioBytes) { (void)submitPcm(g_audioAccum.data(), (DWORD)g_audioAccum.size()); g_audioAccum.clear(); }
                }
            }
            if (g_quitFlag) break;
            // 没有可用包则拉更多页面
            if (ogg_sync_pageout(&oy, &og) > 0) {
                if (theora_p) ogg_stream_pagein(&to, &og);
                if (vorbis_p) ogg_stream_pagein(&vo, &og);
            } else {
                if (buffer_data(infile, &oy) == 0) { g_eofFlag = true; break; }
            }
        }
        if (!g_audioAccum.empty()) { (void)submitPcm(g_audioAccum.data(), (DWORD)g_audioAccum.size()); g_audioAccum.clear(); }
    }

    // 主解码循环
    while (!g_quitFlag && streams_alive) {
        bool a_pkt = false, v_pkt = false;

        // 如果队列满了，就等待
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCond.wait(lock, [] { return g_quitFlag || g_videoQueue.size() < g_maxVideoQueueFrames; });
        }
        if (g_quitFlag) break;

        // 优先处理已缓冲的数据包
        if (vorbis_p) {
            if (ogg_stream_packetout(&vo, &op) > 0) a_pkt = true;
        }
        if (theora_p && !a_pkt) { // 优先解码音频
            if (ogg_stream_packetout(&to, &op) > 0) v_pkt = true;
        }

        // 如果没有缓冲的数据包，从文件读取更多
        if (!a_pkt && !v_pkt) {
            if (buffer_data(infile, &oy) == 0) {
                g_eofFlag = true; // 文件结束
                // break; // 不要立即退出，需要把流里的剩余包处理完
            }
            while (ogg_sync_pageout(&oy, &og) > 0) {
                if (theora_p && ogg_page_serialno(&og) == to.serialno) ogg_stream_pagein(&to, &og);
                if (vorbis_p && ogg_page_serialno(&og) == vo.serialno) ogg_stream_pagein(&vo, &og);
            }
            // 再次尝试获取数据包
            if (vorbis_p) {
                if (ogg_stream_packetout(&vo, &op) > 0) a_pkt = true;
            }
            if (theora_p && !a_pkt) {
                if (ogg_stream_packetout(&to, &op) > 0) v_pkt = true;
            }
            // 如果文件结束且所有流都取不到包了，说明解码完成
            if (g_eofFlag && !a_pkt && !v_pkt) {
                streams_alive = false;
            }
        }

        // 音频解码
        if (a_pkt) {
            if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
            float** pcm = nullptr; int samples = 0;
            while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                const int outCh = g_wfx.nChannels;
                const size_t needed = (size_t)samples * (size_t)outCh * sizeof(int16_t);
                size_t start = g_audioAccum.size();
                g_audioAccum.resize(start + needed);
                int16_t* dst = reinterpret_cast<int16_t*>(g_audioAccum.data() + start);
                for (int i = 0; i < samples; ++i) {
                    float L = 0.f, R = 0.f;
                    if (vi.channels == 1) { L = R = pcm[0][i]; }
                    else { L = pcm[0][i]; R = pcm[1][i]; if (vi.channels > 2) { for (int ch = 2; ch < vi.channels; ++ch) { float v = pcm[ch][i]; float scale = 0.5f / (vi.channels - 1); L += v * scale; R += v * scale; } } }
                    int sL = (int)lround(L * 32767.0f); if (sL > 32767) sL = 32767; if (sL < -32768) sL = -32768;
                    if (outCh == 1) { *dst++ = (int16_t)sL; }
                    else { int sR = (int)lround(R * 32767.0f); if (sR > 32767) sR = 32767; if (sR < -32768) sR = -32768; *dst++ = (int16_t)sL; *dst++ = (int16_t)sR; }
                }
                vorbis_synthesis_read(&vd, samples);
                if (g_audioAccum.size() >= g_targetAudioBytes) { while (!g_quitFlag && g_outstanding.load() >= 16) Sleep(5); (void)submitPcm(g_audioAccum.data(), (DWORD)g_audioAccum.size()); g_audioAccum.clear(); }
            }
        }

        // 视频解码
        if (v_pkt) {
            ogg_int64_t gran = -1;
            if (th_decode_packetin(td, &op, &gran) == 0) {
                double vtime = th_granule_time(td, gran);
                
                QueuedFrame qf;
                qf.vtime = vtime;
                th_decode_ycbcr_out(td, yuv);
                yuv420_to_argb32(yuv, qf.pixels);

                {
                    std::lock_guard<std::mutex> lock(g_queueMutex);
                    if (g_videoQueue.size() >= g_maxVideoQueueFrames) {
                        g_videoQueue.pop_front(); // 丢弃最旧的帧
                    }
                    g_videoQueue.push_back(std::move(qf));
                }
                g_queueCond.notify_one();
            }
        }
    }

    // 解码循环结束，冲洗(flush)所有剩余的音频
    if (vorbis_p && !g_audioAccum.empty()) {
        (void)submitPcm(g_audioAccum.data(), (DWORD)g_audioAccum.size());
        g_audioAccum.clear();
    }
    g_eofFlag = true; // 再次确认文件结束标志
    g_queueCond.notify_all(); // 唤醒主线程，以防它在等待
}


int main(int argc, char** argv) {
    timeBeginPeriod(1);
    g_hInst = GetModuleHandle(NULL);
    const char* filename = (argc >= 2) ? argv[1] : "test.ogg";
    FILE* infile = fopen(filename, "rb");
    if (!infile) { fprintf(stderr, "Unable to open '%s' for playback.\n", filename); return 1; }

    ogg_sync_init(&oy);
    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    th_comment_init(&tc);
    th_info_init(&ti);

    ogg_packet op;

    // 识别流
    while (!theora_p || !vorbis_p) {
        int ret = buffer_data(infile, &oy);
        if (ret == 0) { fprintf(stderr, "End of file while searching for codec headers.\n"); return 1; }
        while (ogg_sync_pageout(&oy, &og) > 0) {
            ogg_stream_state test;
            if (!ogg_page_bos(&og)) {
                if (theora_p) ogg_stream_pagein(&to, &og);
                if (vorbis_p) ogg_stream_pagein(&vo, &og);
                continue;
            }
            ogg_stream_init(&test, ogg_page_serialno(&og));
            ogg_stream_pagein(&test, &og);
            ogg_stream_packetout(&test, &op);
            if (!theora_p && th_decode_headerin(&ti, &tc, &ts, &op) >= 0) {
                memcpy(&to, &test, sizeof(test)); theora_p = 1; theora_headers = 1;
            } else if (!vorbis_p && vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {
                memcpy(&vo, &test, sizeof(test)); vorbis_p = 1; vorbis_headers = 1;
            } else {
                ogg_stream_clear(&test);
            }
        }
    }

    // 解析剩余头
    while ((theora_p && theora_headers < 3) || (vorbis_p && vorbis_headers < 3)) {
        while (theora_p && theora_headers < 3 && ogg_stream_packetout(&to, &op)) {
            if (!th_decode_headerin(&ti, &tc, &ts, &op)) { fprintf(stderr, "Error parsing Theora headers.\n"); return 1; }
            theora_headers++;
        }
        while (vorbis_p && vorbis_headers < 3 && ogg_stream_packetout(&vo, &op)) {
            if (vorbis_synthesis_headerin(&vi, &vc, &op)) { fprintf(stderr, "Error parsing Vorbis headers.\n"); return 1; }
            vorbis_headers++;
            if (vorbis_headers == 3) break;
        }
        if (ogg_sync_pageout(&oy, &og) > 0) {
            if (theora_p) ogg_stream_pagein(&to, &og);
            if (vorbis_p) ogg_stream_pagein(&vo, &og);
        } else {
            int ret = buffer_data(infile, &oy);
            if (ret == 0) { fprintf(stderr, "End of file while searching for codec headers.\n"); return 1; }
        }
    }

    if (theora_p) {
        td = th_decode_alloc(&ti, ts);
        px_fmt = ti.pixel_fmt;
        int w = ti.pic_width;
        int h = ti.pic_height;
        createWindowGDI(w, h);
        if (ti.fps_denominator != 0) {
            g_videoFps = (double)ti.fps_numerator / (double)ti.fps_denominator;
        } else {
            g_videoFps = 30.0;
        }
        if (g_videoFps <= 1e-6) g_videoFps = 30.0;
        g_frameIntervalMs = (DWORD)std::max(1.0, floor(1000.0 / g_videoFps + 0.5));
        // 根据FPS调整容差
        double frameSec = 1000.0 / g_videoFps * 0.001; // 单帧秒
        VIDEO_RENDER_SLACK = std::max(0.005, frameSec * 0.4);   // 提前渲染容差
        VIDEO_DROP_LATE    = std::max(0.20, frameSec * 3.0);    // 落后超过约3帧丢帧
        VIDEO_DECODE_AHEAD = std::min(3.0, std::max(1.0, frameSec * 60.0)); // 约1-3秒预解码
        g_startTicks = timeGetTime();
        g_nextPresentMs = g_startTicks;
         printf("Theora video FPS: %.3f (frame interval %u ms)\n", g_videoFps, (unsigned)g_frameIntervalMs);
    }
    if (vorbis_p) {
        vorbis_synthesis_init(&vd, &vi);
        vorbis_block_init(&vd, &vb);
        openWaveOut();
    }

    // 启动解码线程
    g_decodeThread = std::thread(decode_loop, infile);

    // 主循环：渲染
    bool running = true;
    while (running) {
        // 处理消息
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        double atime_now = getAudioPlayedSec();
        QueuedFrame current_frame;
        bool have_frame = false;

        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            
            // 丢弃过时的帧
            while (!g_videoQueue.empty() && g_videoQueue.front().vtime + VIDEO_DROP_LATE < atime_now) {
                g_videoQueue.pop_front();
            }

            if (!g_videoQueue.empty()) {
                const QueuedFrame& head = g_videoQueue.front();
                // 如果到了呈现时间
                if (atime_now + VIDEO_RENDER_SLACK >= head.vtime) {
                    current_frame = std::move(g_videoQueue.front());
                    g_videoQueue.pop_front();
                    have_frame = true;
                }
            }
        }

        if (have_frame) {
            if (current_frame.pixels.size() == g_frameARGB.size()) {
                g_frameARGB.swap(current_frame.pixels);
                g_lastPresentedTime = current_frame.vtime;
                render_frame();
            }
        }

        // 如果队列为空且已到文件末尾，则检查是否可以退出
        if (g_eofFlag.load()) {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            // 确保视频和音频都播放完毕
            if (g_videoQueue.empty() && getAudioQueuedBytes() == 0 && g_outstanding.load() == 0) {
                running = false;
            }
        }

        // 智能休眠
        DWORD sleep_ms = 1;
        if (!have_frame) {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            if (!g_eofFlag && g_videoQueue.empty()) {
                // 队列是空的，等待解码线程填充
                g_queueCond.wait_for(lock, std::chrono::milliseconds(10));
                sleep_ms = 0;
            } else if (!g_videoQueue.empty()) {
                // 队列中有帧，但还没到呈现时间
                double next_vtime = g_videoQueue.front().vtime;
                double wait_sec = next_vtime - atime_now;
                if (wait_sec > 0.002) { // 如果等待时间大于2ms
                    sleep_ms = (DWORD)(wait_sec * 1000.0) - 1;
                } else {
                    // 时间太近，不值得休眠，直接进入下一轮循环检查
                    sleep_ms = 0;
                }
            }
        }
        
        if (sleep_ms > 0) {
            Sleep(std::min(sleep_ms, g_frameIntervalMs));
        }
    }

    // 确保解码线程已收到退出信号并等待其结束
    g_quitFlag = true;
    g_queueCond.notify_all();
    if (g_decodeThread.joinable()) {
        g_decodeThread.join();
    }
    
    // 清理
    closeWaveOut();

    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_videoQueue.clear();
    }

    if (g_hWnd) DestroyWindow(g_hWnd);
    timeEndPeriod(1);

    if (vorbis_p) {
        ogg_stream_clear(&vo);
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
    }
    if (theora_p) {
        ogg_stream_clear(&to);
        th_decode_free(td);
        th_comment_clear(&tc);
        th_info_clear(&ti);
    }
    ogg_sync_clear(&oy);

    if (infile) fclose(infile);
    return 0;
}

#endif // 1
