/****************************************************************************
 * 标题: A02 FLAC+WASAPI+OSound
 * 文件：A02 FLAC+WASAPI+OSound.cpp
 * 版本：2.0
 * 作者: AEGLOVE
 * 日期: 2025-01-14
 * 功能: 无临时文件，FLAC → OSound → WASAPI 播放
 * 依赖: Windows SDK, libFLAC
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/

#if 1

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <conio.h>

extern "C" {
#define FLAC__NO_DLL
#include "share/compat.h"
#include "FLAC/stream_decoder.h"
}

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "flac.lib")

#define REFTIMES_PER_SEC        10000000
#define REFTIMES_PER_MILLISEC   10000

/* 用户给出的通用音频数据结构 */
typedef struct OSound {
    uint32_t channels;
    uint32_t sampleRate;
    uint32_t bitsPerSample;
    uint64_t numSamples;
    size_t   dataSize;
    uint8_t* data;
    double   duration;
    uint32_t byteRate;
    uint32_t playStartMs;
    uint32_t formatTag;
    uint8_t  isFloat;
    uint8_t  isInterleaved;
    uint16_t reserved;
} OSound;

/* 简单的内存缓冲写入器 */
struct MemWriter {
    std::vector<uint8_t> buf;
    void write(const void* src, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(src);
        buf.insert(buf.end(), p, p + n);
    }
};

class FlacMemDecoder {
public:
    /* 解码 flac 到 OSound（内部用 MemWriter 收集 PCM）*/
    bool decode(const std::string& filePath, OSound& outSound) {
        MemWriter writer;
        DecCtx ctx{ &writer, true, 0, 16, 2, 44100 };

        auto writeCB = [](const FLAC__StreamDecoder* dec,
            const FLAC__Frame* frame,
            const FLAC__int32* const buffer[],
            void* client) -> FLAC__StreamDecoderWriteStatus {
                auto* c = static_cast<DecCtx*>(client);
                const uint32_t block = frame->header.blocksize;
                const uint32_t ch = frame->header.channels;
                const uint32_t bps = frame->header.bits_per_sample;

                /* 第一次进来时记录基本信息 */
                if (c->first) {
                    c->first = false;
                    c->channels = ch;
                    c->sampleRate = frame->header.sample_rate;
                    c->bitsPerSample = bps;
                }

                /* 只支持 16/24 bit，按 little-endian 交织写入 */
                if (bps == 16) {
                    for (uint32_t i = 0; i < block; ++i)
                        for (uint32_t k = 0; k < ch; ++k) {
                            int16_t v = static_cast<int16_t>(buffer[k][i]);
                            c->wr->write(&v, 2);
                        }
                }
                else if (bps == 24) {
                    for (uint32_t i = 0; i < block; ++i)
                        for (uint32_t k = 0; k < ch; ++k) {
                            int32_t vv = buffer[k][i];
                            uint8_t tmp[3] = {
                                static_cast<uint8_t>(vv & 0xFF),
                                static_cast<uint8_t>((vv >> 8) & 0xFF),
                                static_cast<uint8_t>((vv >> 16) & 0xFF)
                            };
                            c->wr->write(tmp, 3);
                        }
                }
                else {
                    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                }
                c->totalSamples += block;
                return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
            };

        auto metaCB = [](const FLAC__StreamDecoder* dec,
            const FLAC__StreamMetadata* meta,
            void* client) {
                if (meta->type == FLAC__METADATA_TYPE_STREAMINFO) {
                    auto* c = static_cast<DecCtx*>(client);
                    c->totalSamples = meta->data.stream_info.total_samples;
                }
            };

        auto errCB = [](const FLAC__StreamDecoder* dec,
            FLAC__StreamDecoderErrorStatus st,
            void* client) {
                printf("FLAC 解码错误: %s\n", FLAC__StreamDecoderErrorStatusString[st]);
            };

        FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
        if (!dec) return false;
        FLAC__stream_decoder_set_md5_checking(dec, true);

        auto st = FLAC__stream_decoder_init_file(dec, filePath.c_str(),
            writeCB, metaCB, errCB, &ctx);
        if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            printf("FLAC 初始化失败: %s\n", FLAC__StreamDecoderInitStatusString[st]);
            FLAC__stream_decoder_delete(dec);
            return false;
        }

        bool ok = FLAC__stream_decoder_process_until_end_of_stream(dec);
        FLAC__stream_decoder_delete(dec);
        if (!ok) return false;

        /* 填充 OSound */
        outSound.channels = ctx.channels;
        outSound.sampleRate = ctx.sampleRate;
        outSound.bitsPerSample = ctx.bitsPerSample;
        outSound.numSamples = ctx.totalSamples;
        outSound.dataSize = writer.buf.size();
        outSound.data = new uint8_t[outSound.dataSize];
        memcpy(outSound.data, writer.buf.data(), outSound.dataSize);
        outSound.duration = static_cast<double>(outSound.numSamples) / outSound.sampleRate;
        outSound.byteRate = outSound.sampleRate * outSound.channels * (outSound.bitsPerSample / 8);
        outSound.formatTag = 1;   /* PCM */
        outSound.isFloat = 0;
        outSound.isInterleaved = 1;
        outSound.reserved = 0;

        printf("FLAC 解码完成: %u Hz, %u ch, %u bit, %llu samples, %.2f sec\n",
            outSound.sampleRate, outSound.channels, outSound.bitsPerSample,
            outSound.numSamples, outSound.duration);
        return true;
    }

private:
    struct DecCtx {
        MemWriter* wr;
        bool       first;
        uint64_t   totalSamples;
        uint32_t   bitsPerSample;
        uint32_t   channels;
        uint32_t   sampleRate;
    };
};

/* WASAPI 播放 OSound 的简单封装 */
class WasapiPlayer {
public:
    bool init(const OSound& snd) {
        HRESULT hr;
        IMMDeviceEnumerator* devEnum = nullptr;

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&devEnum);
        if (FAILED(hr)) return false;

        IMMDevice* device = nullptr;
        hr = devEnum->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        devEnum->Release();
        if (FAILED(hr)) return false;

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
        if (FAILED(hr)) { device->Release(); return false; }

        /* 根据 OSound 填充波形格式 */
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = snd.channels;
        wf.nSamplesPerSec = snd.sampleRate;
        wf.wBitsPerSample = snd.bitsPerSample;
        wf.nBlockAlign = wf.nChannels * (wf.wBitsPerSample / 8);
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
        wf.cbSize = 0;

        hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!hEvent) { device->Release(); return false; }

        /* 共享模式 + 事件驱动 */
        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
            0, 0, &wf, nullptr);
        if (FAILED(hr)) { device->Release(); return false; }

        hr = audioClient->SetEventHandle(hEvent);
        if (FAILED(hr)) { device->Release(); return false; }

        hr = audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
        if (FAILED(hr)) { device->Release(); return false; }

        device->Release();
        printf("WASAPI 初始化完成\n");
        return true;
    }

    void play(const OSound& snd) {
        if (!renderClient) return;

        const uint8_t* src = snd.data;
        uint64_t       remaining = snd.dataSize;
        uint32_t       frameSz = snd.channels * (snd.bitsPerSample / 8);

        HRESULT hr = audioClient->Start();
        if (FAILED(hr)) return;
        printf("开始播放...\n");

        while (remaining > 0 && !_kbhit()) {
            WaitForSingleObject(hEvent, INFINITE);

            UINT32 pad = 0, bufFrames = 0;
            audioClient->GetCurrentPadding(&pad);
            audioClient->GetBufferSize(&bufFrames);
            UINT32 avail = bufFrames - pad;
            if (avail == 0) continue;

            UINT32 want = (remaining / frameSz) > avail ? avail : static_cast<UINT32>(remaining / frameSz);
            BYTE* dst = nullptr;
            hr = renderClient->GetBuffer(want, &dst);
            if (FAILED(hr)) continue;

            memcpy(dst, src, want * frameSz);
            hr = renderClient->ReleaseBuffer(want, 0);
            if (FAILED(hr)) continue;

            src += want * frameSz;
            remaining -= want * frameSz;
        }

        Sleep(500);
        audioClient->Stop();
        printf("播放结束\n");
    }

    ~WasapiPlayer() { cleanup(); }

private:
    IAudioClient* audioClient = nullptr;
    IAudioRenderClient* renderClient = nullptr;
    HANDLE              hEvent = nullptr;

    void cleanup() {
        if (renderClient) { renderClient->Release(); renderClient = nullptr; }
        if (audioClient) { audioClient->Release();  audioClient = nullptr; }
        if (hEvent) { CloseHandle(hEvent);     hEvent = nullptr; }
    }
};

/* ------------- main ------------- */
int main(int argc, char** argv) {
    printf("A02 FLAC+WASAPI+OSound\n");
    printf("环境: Windows x64, VS2022, C++17, Unicode\n");
    printf("编码: UTF-8 with BOM\n\n");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;

    std::string inFile = (argc > 1) ? argv[1] : "test.flac";

    OSound sound = {};
    FlacMemDecoder decoder;
    if (!decoder.decode(inFile, sound)) {
        CoUninitialize();
        return 1;
    }

    WasapiPlayer player;
    if (!player.init(sound)) {
        delete[] sound.data;
        CoUninitialize();
        return 1;
    }

    player.play(sound);

    delete[] sound.data;
    CoUninitialize();
    return 0;
}

#endif