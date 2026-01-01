
/****************************************************************************
 * 标题: 媒体文件信息分析器
 * 文件：A00 显示媒体文件相关信息.cpp
 * 版本：0.1
 * 作者: AEGLOVE
 * 日期: 2026-01-01
 * 功能: 使用FFmpeg分析并显示媒体文件（视频/音频/字幕）流信息和元数据
 * 依赖: FFmpeg（libavformat、libavcodec、libavutil）、Windows API
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/

#if 1
#include <iostream>  
#include <string>  
#include <cstdint>  
#include <cwchar>  
#include <fcntl.h>  
#include <io.h>  
#include <windows.h>  

// FFmpeg静态库链接  
#pragma comment(lib, "avformat.lib")  
#pragma comment(lib, "avcodec.lib")   
#pragma comment(lib, "avutil.lib")  

// 控制台输出宏  
#define ODD(...) wprintf(__VA_ARGS__)  

// 控制台颜色设置    
void setConsoleColor(int32_t color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}

// 控制台颜色常量  
#define COLOR_DEFAULT 7  
#define COLOR_TITLE 11  
#define COLOR_INFO 10  
#define COLOR_SUCCESS 10  
#define COLOR_WARNING 14  
#define COLOR_ERROR 12  

extern "C" {
#include <libavformat/avformat.h>  
#include <libavcodec/avcodec.h>  
#include <libavutil/avutil.h>  
#include <libavutil/pixdesc.h>  
#include <libavutil/channel_layout.h>  
}

// 视频流信息结构体  
typedef struct VideoStreamInfo {
    int index;                    // 流索引  
    int width;                    // 视频宽度  
    int height;                   // 视频高度  
    int codedWidth;               // 编码宽度  
    int codedHeight;              // 编码高度  
    AVRational frameRate;         // 帧率  
    AVRational sampleAspectRatio; // 采样宽高比  
    AVRational displayAspectRatio; // 显示宽高比  
    AVPixelFormat pixelFormat;    // 像素格式  
    int64_t bitRate;              // 比特率  
    int hasBFrames;               // 是否有B帧  
    AVColorSpace colorSpace;      // 色彩空间  
    AVColorRange colorRange;      // 色彩范围  
    const char* codecName;        // 编解码器名称  
    const char* codecLongName;    // 编解码器长名称  
} VideoStreamInfo;

// 音频流信息结构体  
typedef struct AudioStreamInfo {
    int index;                    // 流索引  
    int sampleRate;               // 采样率  
    int channels;                 // 声道数  
    AVChannelLayout channelLayout; // 声道布局  
    AVSampleFormat sampleFormat;  // 采样格式  
    int64_t bitRate;              // 比特率  
    int bitsPerSample;            // 每样本位数  
    int initialPadding;           // 初始填充  
    const char* codecName;        // 编解码器名称  
    const char* codecLongName;    // 编解码器长名称  
} AudioStreamInfo;

// 媒体文件信息结构体  
typedef struct MediaFileInfo {
    char filename[256];           // 文件名  
    char formatName[64];          // 格式名称  
    char formatLongName[128];     // 格式长名称  
    int64_t duration;             // 时长(微秒)  
    int64_t size;                 // 文件大小(字节)  
    int64_t bitRate;              // 总比特率  
    int videoStreamCount;         // 视频流数量  
    int audioStreamCount;         // 音频流数量  
    int subtitleStreamCount;      // 字幕流数量  
    VideoStreamInfo* videoStreams; // 视频流数组  
    AudioStreamInfo* audioStreams; // 音频流数组  
} MediaFileInfo;

class MediaInfoAnalyzer {
private:
    MediaFileInfo mediaInfo;

    // 清理媒体信息  
    void cleanupMediaInfo() {
        if (mediaInfo.videoStreams) {
            free(mediaInfo.videoStreams);
            mediaInfo.videoStreams = nullptr;
        }
        if (mediaInfo.audioStreams) {
            free(mediaInfo.audioStreams);
            mediaInfo.audioStreams = nullptr;
        }
    }

    // 获取颜色空间名称  
    const char* getColorSpaceName(AVColorSpace colorSpace) {
        switch (colorSpace) {
        case AVCOL_SPC_BT709: return "BT.709";
        case AVCOL_SPC_BT470BG: return "BT.601";
        case AVCOL_SPC_BT2020_NCL: return "BT.2020";
        case AVCOL_SPC_SMPTE170M: return "SMPTE170M";
        default: return av_color_space_name(colorSpace);
        }
    }

    // 获取色彩范围名称  
    const char* getColorRangeName(AVColorRange colorRange) {
        switch (colorRange) {
        case AVCOL_RANGE_JPEG: return "JPEG";
        case AVCOL_RANGE_MPEG: return "MPEG";
        default: return av_color_range_name(colorRange);
        }
    }

public:
    MediaInfoAnalyzer() {
        memset(&mediaInfo, 0, sizeof(mediaInfo));
    }

    ~MediaInfoAnalyzer() {
        cleanupMediaInfo();
    }

    // 分析媒体文件  
    bool analyzeMediaFile(const char* filename) {
        AVFormatContext* formatCtx = nullptr;
        AVCodecContext* codecCtx = nullptr;

        ODD(L"正在分析媒体文件: %hs\n", filename);
        setConsoleColor(COLOR_INFO);

        // 打开输入文件  
        if (avformat_open_input(&formatCtx, filename, nullptr, nullptr) < 0) {
            setConsoleColor(COLOR_ERROR);
            ODD(L"❌ 无法打开文件: %hs\n", filename);
            return false;
        }

        // 查找流信息  
        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            setConsoleColor(COLOR_ERROR);
            ODD(L"❌ 无法查找流信息\n");
            avformat_close_input(&formatCtx);
            return false;
        }

        // 清理之前的信息  
        cleanupMediaInfo();

        // 填充基本信息  
        strncpy(mediaInfo.filename, filename, sizeof(mediaInfo.filename) - 1);
        strncpy(mediaInfo.formatName, formatCtx->iformat->name, sizeof(mediaInfo.formatName) - 1);
        strncpy(mediaInfo.formatLongName, formatCtx->iformat->long_name, sizeof(mediaInfo.formatLongName) - 1);
        mediaInfo.duration = formatCtx->duration;
        mediaInfo.size = avio_size(formatCtx->pb);
        mediaInfo.bitRate = formatCtx->bit_rate;

        // 统计流数量  
        for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
            AVStream* stream = formatCtx->streams[i];
            switch (stream->codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                mediaInfo.videoStreamCount++;
                break;
            case AVMEDIA_TYPE_AUDIO:
                mediaInfo.audioStreamCount++;
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                mediaInfo.subtitleStreamCount++;
                break;
            }
        }

        // 分配数组  
        if (mediaInfo.videoStreamCount > 0) {
            mediaInfo.videoStreams = (VideoStreamInfo*)calloc(mediaInfo.videoStreamCount, sizeof(VideoStreamInfo));
        }
        if (mediaInfo.audioStreamCount > 0) {
            mediaInfo.audioStreams = (AudioStreamInfo*)calloc(mediaInfo.audioStreamCount, sizeof(AudioStreamInfo));
        }

        // 填充流信息  
        int videoIndex = 0, audioIndex = 0;
        for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
            AVStream* stream = formatCtx->streams[i];
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            const AVCodecDescriptor* codecDesc = avcodec_descriptor_get(stream->codecpar->codec_id);

            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                VideoStreamInfo* videoInfo = &mediaInfo.videoStreams[videoIndex];

                // 创建解码器上下文获取额外信息  
                codecCtx = avcodec_alloc_context3(codec);
                avcodec_parameters_to_context(codecCtx, stream->codecpar);
                avcodec_open2(codecCtx, codec, nullptr);

                videoInfo->index = stream->index;
                videoInfo->width = stream->codecpar->width;
                videoInfo->height = stream->codecpar->height;
                videoInfo->codedWidth = codecCtx->coded_width;
                videoInfo->codedHeight = codecCtx->coded_height;
                videoInfo->frameRate = stream->avg_frame_rate;
                videoInfo->sampleAspectRatio = stream->sample_aspect_ratio;
                videoInfo->pixelFormat = (AVPixelFormat)stream->codecpar->format;
                videoInfo->bitRate = stream->codecpar->bit_rate;
                videoInfo->hasBFrames = stream->codecpar->video_delay;
                videoInfo->colorSpace = stream->codecpar->color_space;
                videoInfo->colorRange = stream->codecpar->color_range;
                videoInfo->codecName = codecDesc ? codecDesc->name : "unknown";
                videoInfo->codecLongName = codecDesc ? codecDesc->long_name : "unknown";

                // 计算显示宽高比  
                if (videoInfo->sampleAspectRatio.num) {
                    av_reduce(&videoInfo->displayAspectRatio.num, &videoInfo->displayAspectRatio.den,
                        videoInfo->width * videoInfo->sampleAspectRatio.num,
                        videoInfo->height * videoInfo->sampleAspectRatio.den, 1024 * 1024);
                }

                avcodec_free_context(&codecCtx);
                videoIndex++;

            }
            else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                AudioStreamInfo* audioInfo = &mediaInfo.audioStreams[audioIndex];

                audioInfo->index = stream->index;
                audioInfo->sampleRate = stream->codecpar->sample_rate;
                audioInfo->channels = stream->codecpar->ch_layout.nb_channels;
                audioInfo->channelLayout = stream->codecpar->ch_layout;
                audioInfo->sampleFormat = (AVSampleFormat)stream->codecpar->format;
                audioInfo->bitRate = stream->codecpar->bit_rate;
                audioInfo->bitsPerSample = av_get_bits_per_sample(stream->codecpar->codec_id);
                audioInfo->initialPadding = stream->codecpar->initial_padding;
                audioInfo->codecName = codecDesc ? codecDesc->name : "unknown";
                audioInfo->codecLongName = codecDesc ? codecDesc->long_name : "unknown";

                audioIndex++;
            }
        }

        avformat_close_input(&formatCtx);

        setConsoleColor(COLOR_SUCCESS);
        ODD(L"✅ 媒体文件分析完成!\n");
        return true;
    }

    // 打印媒体信息  
    void printMediaInfo() {
        setConsoleColor(COLOR_TITLE);
        ODD(L"\n媒体文件信息分析报告\n");
        ODD(L"═══════════════════════════════════════\n\n");

        // 基本信息  
        setConsoleColor(COLOR_INFO);
        ODD(L"文件信息:\n");
        ODD(L"   文件名: %hs\n", mediaInfo.filename);
        ODD(L"   格式: %hs (%hs)\n", mediaInfo.formatName, mediaInfo.formatLongName);
        ODD(L"   大小: %.2f MB\n", mediaInfo.size / (1024.0 * 1024.0));
        ODD(L"   时长: %.2f 秒\n", mediaInfo.duration / 1000000.0);
        if (mediaInfo.bitRate > 0) {
            ODD(L"   总比特率: %lld kbps\n", mediaInfo.bitRate / 1000);
        }
        ODD(L"\n");

        // 流统计  
        ODD(L"流统计:\n");
        ODD(L"   视频流: %d 个\n", mediaInfo.videoStreamCount);
        ODD(L"   音频流: %d 个\n", mediaInfo.audioStreamCount);
        ODD(L"   字幕流: %d 个\n", mediaInfo.subtitleStreamCount);
        ODD(L"\n");

        // 视频流信息  
        if (mediaInfo.videoStreamCount > 0) {
            setConsoleColor(COLOR_TITLE);
            ODD(L"视频流信息:\n");

            for (int i = 0; i < mediaInfo.videoStreamCount; i++) {
                VideoStreamInfo* video = &mediaInfo.videoStreams[i];
                setConsoleColor(COLOR_INFO);
                ODD(L"   流 #%d:\n", video->index);
                ODD(L"     编解码器: %hs (%hs)\n", video->codecName, video->codecLongName);
                ODD(L"     分辨率: %dx%d", video->width, video->height);
                if (video->codedWidth != video->width || video->codedHeight != video->height) {
                    ODD(L" (编码: %dx%d)", video->codedWidth, video->codedHeight);
                }
                ODD(L"\n");
                ODD(L"     帧率: %.2f fps\n", av_q2d(video->frameRate));
                if (video->sampleAspectRatio.num) {
                    ODD(L"     采样宽高比: %d:%d\n", video->sampleAspectRatio.num, video->sampleAspectRatio.den);
                }
                if (video->displayAspectRatio.num) {
                    ODD(L"     显示宽高比: %d:%d\n", video->displayAspectRatio.num, video->displayAspectRatio.den);
                }
                ODD(L"     像素格式: %hs\n", av_get_pix_fmt_name(video->pixelFormat));
                if (video->bitRate > 0) {
                    ODD(L"     比特率: %lld kbps\n", video->bitRate / 1000);
                }
                if (video->hasBFrames > 0) {
                    ODD(L"     B帧: %d\n", video->hasBFrames);
                }
                if (video->colorSpace != AVCOL_SPC_UNSPECIFIED) {
                    ODD(L"     色彩空间: %hs\n", getColorSpaceName(video->colorSpace));
                }
                if (video->colorRange != AVCOL_RANGE_UNSPECIFIED) {
                    ODD(L"     色彩范围: %hs\n", getColorRangeName(video->colorRange));
                }
                ODD(L"\n");
            }
        }

        // 音频流信息  
        if (mediaInfo.audioStreamCount > 0) {
            setConsoleColor(COLOR_TITLE);
            ODD(L"音频流信息:\n");

            for (int i = 0; i < mediaInfo.audioStreamCount; i++) {
                AudioStreamInfo* audio = &mediaInfo.audioStreams[i];
                setConsoleColor(COLOR_INFO);
                ODD(L"   流 #%d:\n", audio->index);
                ODD(L"     编解码器: %hs (%hs)\n", audio->codecName, audio->codecLongName);
                ODD(L"     采样率: %d Hz\n", audio->sampleRate);
                ODD(L"     声道数: %d\n", audio->channels);
                if (audio->channelLayout.order != AV_CHANNEL_ORDER_UNSPEC) {
                    char layoutStr[128];
                    av_channel_layout_describe(&audio->channelLayout, layoutStr, sizeof(layoutStr));
                    ODD(L"     声道布局: %hs\n", layoutStr);
                }
                ODD(L"     采样格式: %hs\n", av_get_sample_fmt_name(audio->sampleFormat));
                if (audio->bitRate > 0) {
                    ODD(L"     比特率: %lld kbps\n", audio->bitRate / 1000);
                }
                ODD(L"     每样本位数: %d bits\n", audio->bitsPerSample);
                if (audio->initialPadding > 0) {
                    ODD(L"     初始填充: %d\n", audio->initialPadding);
                }
                ODD(L"\n");
            }
        }

        setConsoleColor(COLOR_DEFAULT);
        ODD(L"═══════════════════════════════════════\n");
    }
};

int main(int argc, char* argv[]) {
    // 设置控制台输出编码为UTF-16  
    _setmode(_fileno(stdout), _O_U16TEXT);

    // 清屏  
    system("cls");

    setConsoleColor(COLOR_TITLE);
    ODD(L"媒体文件信息分析器\n");
    ODD(L"═══════════════════════════════════════\n\n");

    setConsoleColor(COLOR_INFO);
    ODD(L"请输入要分析的媒体文件路径: ");

    char filename[256];
    std::wstring input;
    std::wcin >> input;

    // 转换宽字符到多字节字符  
    wcstombs(filename, input.c_str(), sizeof(filename));

    MediaInfoAnalyzer analyzer;

    if (!analyzer.analyzeMediaFile(filename)) {
        setConsoleColor(COLOR_ERROR);
        ODD(L"\n❌ 分析失败! 请检查文件路径是否正确。\n");
        ODD(L"按任意键退出...");
        getchar();
        getchar();
        return -1;
    }

    analyzer.printMediaInfo();

    setConsoleColor(COLOR_SUCCESS);
    ODD(L"\n✅ 分析完成! 按任意键退出...\n");
    getchar();
    getchar();

    return 0;
}
#endif // 1
