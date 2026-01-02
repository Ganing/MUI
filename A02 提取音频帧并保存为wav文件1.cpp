/****************************************************************************
 * 标题: 音频帧提取并保存为WAV
 * 文件：A02 提取音频帧并保存为wav文件1.cpp
 * 版本：0.1
 * 作者: AEGLOVE
 * 日期: 2026-01-02
 * 功能: 使用FFmpeg提取音频帧/片段并保存为标准WAV文件（支持位深转换与简单格式处理）
 * 依赖: FFmpeg（libavformat、libavcodec、libavutil、libswresample）、C++ 标准库
 * 环境: Windows11 x64, VS2022, C++17, Unicode 字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/
#if 0
#include <iostream>  
#include <string>  
#include <vector>  
#include <cstdint>  
#include <fstream>  
#include <algorithm>

 // WAV文件写入函数  
#define DR_WAV_IMPLEMENTATION  
#include "dr_wav.h"  

// FFmpeg静态库链接  
#pragma comment(lib, "avformat.lib")  
#pragma comment(lib, "avcodec.lib")   
#pragma comment(lib, "avutil.lib")  
#pragma comment(lib, "swresample.lib")  

extern "C" {
#include <libavformat/avformat.h>  
#include <libavcodec/avcodec.h>  
#include <libavutil/avutil.h>  
#include <libavutil/opt.h>  
#include <libswresample/swresample.h>  
}

// 您的OSound结构体  
typedef struct Sound {
    uint32_t channels;       // 声道数（1=单声道，2=立体声）  
    uint32_t sampleRate;     // 采样率（Hz，如44100）  
    uint32_t bitsPerSample;  // 采样位深（16/24/32位）  
    uint64_t numSamples;     // 总采样数（单声道计数）  
    size_t   dataSize;       // PCM数据字节数 = numSamples * channels * (bitsPerSample/8)  
    void* data;           // PCM数据指针（需根据位深转换访问）  

    // 计算字段  
    double   duration;       // 音频时长（秒）numSamples/(double)sampleRate  
    uint32_t byteRate;       // 字节速率 sampleRate * channels * (bitsPerSample/8)  
    uint32_t playms;        //  开始播放的时间  
} OSound;

class AudioFrameExtractor {
private:
    AVFormatContext* formatCtx;
    AVCodecContext* audioCodecCtx;
    int audioStreamIndex;
    SwrContext* swrCtx;

public:
    AudioFrameExtractor() : formatCtx(nullptr), audioCodecCtx(nullptr),
        audioStreamIndex(-1), swrCtx(nullptr) {
    }

    ~AudioFrameExtractor() {
        cleanup();
    }

    bool initialize(const std::string& filename) {
        // 打开输入文件  
        if (avformat_open_input(&formatCtx, filename.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "无法打开文件: " << filename << std::endl;
            return false;
        }

        // 查找流信息  
        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            std::cerr << "无法查找流信息" << std::endl;
            return false;
        }

        // 查找音频流  
        audioStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audioStreamIndex < 0) {
            std::cerr << "未找到音频流" << std::endl;
            return false;
        }

        // 设置音频解码器  
        AVStream* audioStream = formatCtx->streams[audioStreamIndex];
        const AVCodec* codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "未找到解码器" << std::endl;
            return false;
        }

        audioCodecCtx = avcodec_alloc_context3(codec);
        if (!audioCodecCtx) {
            std::cerr << "无法分配解码器上下文" << std::endl;
            return false;
        }

        if (avcodec_parameters_to_context(audioCodecCtx, audioStream->codecpar) < 0) {
            std::cerr << "无法复制流参数" << std::endl;
            return false;
        }

        if (avcodec_open2(audioCodecCtx, codec, nullptr) < 0) {
            std::cerr << "无法打开解码器" << std::endl;
            return false;
        }

        // 打印音频信息  
        std::cout << "音频信息:" << std::endl;
        std::cout << "  采样率: " << audioCodecCtx->sample_rate << " Hz" << std::endl;
        std::cout << "  声道数: " << audioCodecCtx->ch_layout.nb_channels << std::endl;
        std::cout << "  采样格式: " << av_get_sample_fmt_name(audioCodecCtx->sample_fmt) << std::endl;

        return true;
    }

    // 提取指定时长的音频数据  
    bool extractAudioSegment(double startTime, double duration, std::vector<OSound>& audioFrames) {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();

        // 计算目标采样位置  
        int64_t targetSample = (int64_t)(startTime * audioCodecCtx->sample_rate);
        int64_t totalSamples = (int64_t)(duration * audioCodecCtx->sample_rate);
        int64_t extractedSamples = 0;

        std::cout << "开始提取音频: 从 " << startTime << "s 开始，时长 " << duration << "s" << std::endl;

        while (av_read_frame(formatCtx, packet) >= 0 && extractedSamples < totalSamples) {
            if (packet->stream_index == audioStreamIndex) {
                // 发送到解码器  
                int ret = avcodec_send_packet(audioCodecCtx, packet);
                if (ret < 0) {
                    av_packet_unref(packet);
                    continue;
                }

                // 接收解码后的帧  
                while (ret >= 0 && extractedSamples < totalSamples) {
                    ret = avcodec_receive_frame(audioCodecCtx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    else if (ret < 0) {
                        std::cerr << "解码帧失败" << std::endl;
                        break;
                    }

                    // 检查是否到达目标位置  
                    int64_t currentSample = frame->pts * audioCodecCtx->sample_rate /
                        formatCtx->streams[audioStreamIndex]->time_base.den;

                    if (currentSample >= targetSample && extractedSamples < totalSamples) {
                        OSound sound;
                        convertAVFrameToOSound(frame, &sound);
                        audioFrames.push_back(sound);
                        extractedSamples += frame->nb_samples;

                        std::cout << "已提取 " << extractedSamples << " / " << totalSamples << " 采样" << std::endl;
                    }
                }
            }
            av_packet_unref(packet);
        }

        av_packet_free(&packet);
        av_frame_free(&frame);

        std::cout << "音频提取完成! 总共提取了 " << audioFrames.size() << " 帧" << std::endl;
        return !audioFrames.empty();
    }

private:

    void convertAVFrameToOSound(AVFrame* frame, OSound* sound) {
        sound->channels = frame->ch_layout.nb_channels;
        sound->sampleRate = frame->sample_rate;
        sound->bitsPerSample = av_get_bytes_per_sample((AVSampleFormat)frame->format) * 8;
        sound->numSamples = frame->nb_samples;

        // 计算数据大小  
        sound->dataSize = frame->nb_samples * sound->channels * (sound->bitsPerSample / 8);
        sound->duration = (double)frame->nb_samples / frame->sample_rate;
        sound->byteRate = sound->sampleRate * sound->channels * (sound->bitsPerSample / 8);

        // 分配内存  
        sound->data = malloc(sound->dataSize);

        AVSampleFormat fmt = (AVSampleFormat)frame->format;

        // 处理planar格式  
        if (av_sample_fmt_is_planar(fmt)) {
            convertPlanarToInterleaved(frame, sound, fmt);
        }
        else {
            // packed格式：直接复制  
            memcpy(sound->data, frame->data[0], sound->dataSize);
        }

        sound->playms = frame->pts * av_q2d(formatCtx->streams[audioStreamIndex]->time_base) * 1000;
    }

    void convertPlanarToInterleaved(AVFrame* frame, OSound* sound, AVSampleFormat fmt) {
        int channels = frame->ch_layout.nb_channels;
        int samples = frame->nb_samples;

        switch (fmt) {
        case AV_SAMPLE_FMT_FLTP:
        {
            float** channelData = (float**)frame->data;
            float* interleaved = (float*)sound->data;
            for (int i = 0; i < samples; ++i) {
                for (int ch = 0; ch < channels; ++ch) {
                    interleaved[i * channels + ch] = channelData[ch][i];
                }
            }
        }
        break;

        case AV_SAMPLE_FMT_S16P:
        {
            int16_t** channelData = (int16_t**)frame->data;
            int16_t* interleaved = (int16_t*)sound->data;
            for (int i = 0; i < samples; ++i) {
                for (int ch = 0; ch < channels; ++ch) {
                    interleaved[i * channels + ch] = channelData[ch][i];
                }
            }
        }
        break;

        case AV_SAMPLE_FMT_S32P:
        {
            int32_t** channelData = (int32_t**)frame->data;
            int32_t* interleaved = (int32_t*)sound->data;
            for (int i = 0; i < samples; ++i) {
                for (int ch = 0; ch < channels; ++ch) {
                    interleaved[i * channels + ch] = channelData[ch][i];
                }
            }
        }
        break;

        case AV_SAMPLE_FMT_U8P:
        {
            uint8_t** channelData = (uint8_t**)frame->data;
            uint8_t* interleaved = (uint8_t*)sound->data;
            for (int i = 0; i < samples; ++i) {
                for (int ch = 0; ch < channels; ++ch) {
                    interleaved[i * channels + ch] = channelData[ch][i];
                }
            }
        }
        break;

        case AV_SAMPLE_FMT_DBLP:
        {
            double** channelData = (double**)frame->data;
            double* interleaved = (double*)sound->data;
            for (int i = 0; i < samples; ++i) {
                for (int ch = 0; ch < channels; ++ch) {
                    interleaved[i * channels + ch] = channelData[ch][i];
                }
            }
        }
        break;

        default:
            std::cerr << "不支持的planar格式: " << av_get_sample_fmt_name(fmt) << std::endl;
            break;
        }
    }

    void cleanup() {
        if (swrCtx) {
            swr_free(&swrCtx);
        }
        if (audioCodecCtx) {
            avcodec_free_context(&audioCodecCtx);
        }
        if (formatCtx) {
            avformat_close_input(&formatCtx);
        }
    }
};


// 各种格式转换函数  
void convertU8ToS16(const OSound& s, std::vector<int16_t>& outPCM) {
    const uint8_t* src = reinterpret_cast<const uint8_t*>(s.data);
    size_t sampleCount = s.numSamples * s.channels;

    for (size_t i = 0; i < sampleCount; ++i) {
        int16_t sample = (int16_t)(src[i] - 128) * 256;
        outPCM.push_back(sample);
    }
}

void convertS16ToS16(const OSound& s, std::vector<int16_t>& outPCM) {
    const int16_t* src = reinterpret_cast<const int16_t*>(s.data);
    size_t sampleCount = s.numSamples * s.channels;
    outPCM.insert(outPCM.end(), src, src + sampleCount);
}

void convertS24ToS16(const OSound& s, std::vector<int16_t>& outPCM) {
    const uint8_t* src = reinterpret_cast<const uint8_t*>(s.data);
    size_t sampleCount = s.numSamples * s.channels;

    for (size_t i = 0; i < sampleCount; ++i) {
        int32_t sample24 = (src[i * 3] | (src[i * 3 + 1] << 8) | (src[i * 3 + 2] << 16));
        if (sample24 & 0x800000) {
            sample24 |= 0xFF000000;
        }
        int16_t sample16 = (int16_t)(sample24 >> 8);
        outPCM.push_back(sample16);
    }
}

void convertS32OrFloatToS16(const OSound& s, std::vector<int16_t>& outPCM) {
    // 根据数据大小判断是float还是int32  
    size_t expectedFloatBytes = s.numSamples * s.channels * sizeof(float);
    size_t expectedInt32Bytes = s.numSamples * s.channels * sizeof(int32_t);

    if (s.dataSize == expectedFloatBytes) {
        // 32-bit float  
        const float* src = reinterpret_cast<const float*>(s.data);
        size_t sampleCount = s.numSamples * s.channels;

        for (size_t i = 0; i < sampleCount; ++i) {
            float v = src[i];
            if (!std::isfinite(v)) v = 0.0f;
            v = std::clamp(v, -1.0f, 1.0f);
            int16_t sample = (int16_t)std::lrintf(v * 32767.0f);
            outPCM.push_back(sample);
        }
    }
    else if (s.dataSize == expectedInt32Bytes) {
        // 32-bit integer  
        const int32_t* src = reinterpret_cast<const int32_t*>(s.data);
        size_t sampleCount = s.numSamples * s.channels;

        for (size_t i = 0; i < sampleCount; ++i) {
            int64_t tmp = (int64_t)src[i] >> 16;
            tmp = std::clamp<int64_t>(tmp, -32768, 32767);
            outPCM.push_back((int16_t)tmp);
        }
    }
}

void convertDoubleToS16(const OSound& s, std::vector<int16_t>& outPCM) {
    const double* src = reinterpret_cast<const double*>(s.data);
    size_t sampleCount = s.numSamples * s.channels;

    for (size_t i = 0; i < sampleCount; ++i) {
        double v = src[i];
        if (!std::isfinite(v)) v = 0.0;
        v = std::clamp(v, -1.0, 1.0);
        int16_t sample = (int16_t)std::lrint(v * 32767.0);
        outPCM.push_back(sample);
    }
}

bool writeWAVFile(const std::string& filename, const std::vector<OSound>& audioFrames) {
    if (audioFrames.empty()) return false;

    const OSound& first = audioFrames[0];
    const uint32_t sampleRate = first.sampleRate;
    const uint16_t channels = (uint16_t)first.channels;

    // 计算总样本数  
    uint64_t totalSamples = 0;
    for (const auto& s : audioFrames) {
        totalSamples += s.numSamples;
    }

    std::vector<int16_t> outPCM;
    outPCM.reserve(totalSamples * channels);

    // 转换所有音频帧  
    for (const auto& s : audioFrames) {
        if (s.sampleRate != sampleRate || (uint16_t)s.channels != channels) {
            std::cerr << "音频参数不一致\n";
            return false;
        }

        if (!s.data || s.dataSize == 0 || s.numSamples == 0) {
            continue;
        }

        // 根据位深和格式进行转换  
        switch (s.bitsPerSample) {
        case 8:
            convertU8ToS16(s, outPCM);
            break;
        case 16:
            convertS16ToS16(s, outPCM);
            break;
        case 24:
            convertS24ToS16(s, outPCM);
            break;
        case 32:
            convertS32OrFloatToS16(s, outPCM);
            break;
        case 64:
            convertDoubleToS16(s, outPCM);
            break;
        default:
            std::cerr << "不支持的位深: " << s.bitsPerSample << "\n";
            return false;
        }
    }

    // 使用dr_wav写入文件  
    drwav wav;
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = channels;
    format.sampleRate = sampleRate;
    format.bitsPerSample = 16;

    if (!drwav_init_file_write(&wav, filename.c_str(), &format, nullptr)) {
        std::cerr << "无法创建WAV文件: " << filename << std::endl;
        return false;
    }

    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, outPCM.size() / channels, outPCM.data());
    drwav_uninit(&wav);

    if (framesWritten != outPCM.size() / channels) {
        std::cerr << "写入WAV文件数据不完整\n";
        return false;
    }

    std::cout << "WAV文件保存成功: " << filename
        << " (时长约 " << (double)outPCM.size() / channels / sampleRate << " 秒)" << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "=== 音频帧提取器 ===" << std::endl;
    std::cout << "使用FFmpeg提取音频数据并保存为WAV文件" << std::endl;
    std::cout << "====================" << std::endl;

    const std::string inputFile = "D:\\ffbin\\fuyin.mp4";
    const std::string outputFile = "extracted_audio.wav";
    const double startTime = 105.0;  // 从第5秒开始  
    const double duration = 20.0;  // 提取10秒音频  

    AudioFrameExtractor extractor;

    if (!extractor.initialize(inputFile)) {
        std::cerr << "初始化失败!" << std::endl;
        return -1;
    }

    std::vector<OSound> audioFrames;
    if (!extractor.extractAudioSegment(startTime, duration, audioFrames)) {
        std::cerr << "提取音频失败!" << std::endl;
        return -1;
    }

    if (!writeWAVFile(outputFile, audioFrames)) {
        std::cerr << "保存WAV文件失败!" << std::endl;
        return -1;
    }

    // 清理内存  
    for (auto& sound : audioFrames) {
        free(sound.data);
    }

    std::cout << "程序执行完成!" << std::endl;
    return 0;
}
#endif // 1
