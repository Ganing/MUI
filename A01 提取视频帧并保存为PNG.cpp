

/****************************************************************************
 * 标题: 视频帧提取并保存为PNG
 * 文件：A01 提取视频帧并保存为PNG.cpp
 * 版本：0.1
 * 作者: AEGLOVE
 * 日期: 2026-01-01
 * 功能: 使用FFmpeg提取视频帧并将指定帧保存为PNG图片
 * 依赖: FFmpeg（libavformat、libavcodec、libavutil、libswscale）、stb_image_write
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/


#if 0

#include <iostream>  
#include <string>  
#include <filesystem>  
#include <cstdint>  
#include <cstdio>  

// FFmpeg静态库链接  
#pragma comment(lib, "avformat.lib")  
#pragma comment(lib, "avcodec.lib")   
#pragma comment(lib, "avutil.lib")  
#pragma comment(lib, "swscale.lib")  

// stb_image_write.h定义  
#define STB_IMAGE_WRITE_IMPLEMENTATION  
#include "stb_image_write.h"  

extern "C" {
#include <libavformat/avformat.h>  
#include <libavcodec/avcodec.h>  
#include <libavutil/avutil.h>  
#include <libavutil/imgutils.h>  
#include <libswscale/swscale.h>  
}

// 您的OBitmap结构体  
typedef struct OBitmap_t {
    void* data;             // 图像原始数据  
    int width;              // 原始宽度  
    int height;             // 原始高度  
    int bpp;                // 每个像素需要多少个比特bits  
    int pitch;              // (((8*img.width) + 31) >> 5) << 2; 如果4字节对齐，每行需要的字节byte数目  
    int pixSize;            // 像素数据 大小=pitch*height  
    int tag;                // 字体渲染中的top度量，或者纹理的Mipmap levels, 1 by default  
    int format;             // 像素格式，和OGL的对齐。  
} OBitmap;

class VideoFrameExtractor {
private:
    AVFormatContext* formatCtx;     // 格式上下文  
    AVCodecContext* videoCodecCtx;  // 视频解码器上下文  
    int videoStreamIndex;           // 视频流索引  
    SwsContext* swsCtx;             // 像素格式转换上下文  
    std::string outputDir;          // 输出目录  

public:
    VideoFrameExtractor() : formatCtx(nullptr), videoCodecCtx(nullptr),
        videoStreamIndex(-1), swsCtx(nullptr) {
    }

    ~VideoFrameExtractor() {
        cleanup();
    }

    // 初始化FFmpeg并打开视频文件  
    bool initialize(const std::string& filename, const std::string& outputDir) {
        this->outputDir = outputDir;

        // 创建输出目录  
        if (!std::filesystem::exists(outputDir)) {
            std::filesystem::create_directories(outputDir);
            std::cout << "创建输出目录: " << outputDir << std::endl;
        }

        // 打开输入文件  
        if (avformat_open_input(&formatCtx, filename.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "无法打开文件: " << filename << std::endl;
            return false;
        }
        std::cout << "成功打开文件: " << filename << std::endl;

        // 查找流信息  
        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            std::cerr << "无法查找流信息" << std::endl;
            return false;
        }
        std::cout << "找到流信息" << std::endl;

        // 查找视频流  
        videoStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStreamIndex < 0) {
            std::cerr << "未找到视频流" << std::endl;
            return false;
        }
        std::cout << "找到视频流，索引: " << videoStreamIndex << std::endl;

        // 获取视频流参数  
        AVStream* videoStream = formatCtx->streams[videoStreamIndex];
        const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "未找到解码器" << std::endl;
            return false;
        }

        // 创建解码器上下文  
        videoCodecCtx = avcodec_alloc_context3(codec);
        if (!videoCodecCtx) {
            std::cerr << "无法分配解码器上下文" << std::endl;
            return false;
        }

        // 复制流参数到解码器上下文  
        if (avcodec_parameters_to_context(videoCodecCtx, videoStream->codecpar) < 0) {
            std::cerr << "无法复制流参数" << std::endl;
            return false;
        }

        // 打开解码器  
        if (avcodec_open2(videoCodecCtx, codec, nullptr) < 0) {
            std::cerr << "无法打开解码器" << std::endl;
            return false;
        }

        // 打印视频信息  
        std::cout << "视频信息:" << std::endl;
        std::cout << "  分辨率: " << videoCodecCtx->width << "x" << videoCodecCtx->height << std::endl;
        std::cout << "  像素格式: " << av_get_pix_fmt_name(videoCodecCtx->pix_fmt) << std::endl;
        std::cout << "  帧率: " << av_q2d(videoStream->avg_frame_rate) << " fps" << std::endl;

        // 初始化像素格式转换上下文 (转换为RGB24)  
        swsCtx = sws_getContext(
            videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt,
            videoCodecCtx->width, videoCodecCtx->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        return true;
    }

    // 提取帧并保存为PNG  
    bool extractFrames(int frameInterval, int maxFrames) {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVFrame* rgbFrame = av_frame_alloc();

        if (!packet || !frame || !rgbFrame) {
            std::cerr << "无法分配内存" << std::endl;
            return false;
        }

        // 为RGB帧分配缓冲区  
        int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
            videoCodecCtx->width, videoCodecCtx->height, 1);
        uint8_t* rgbBuffer = (uint8_t*)av_malloc(bufferSize);
        av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer,
            AV_PIX_FMT_RGB24, videoCodecCtx->width, videoCodecCtx->height, 1);

        int frameCount = 0;
        int savedFrameCount = 0;
        std::cout << "开始提取帧..." << std::endl;

        while (av_read_frame(formatCtx, packet) >= 0 && savedFrameCount < maxFrames) {
            if (packet->stream_index == videoStreamIndex) {
                // 发送数据包到解码器  
                int ret = avcodec_send_packet(videoCodecCtx, packet);
                if (ret < 0) {
                    std::cerr << "发送数据包失败" << std::endl;
                    break;
                }

                // 接收解码后的帧  
                while (ret >= 0 && savedFrameCount < maxFrames) {
                    ret = avcodec_receive_frame(videoCodecCtx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    else if (ret < 0) {
                        std::cerr << "解码帧失败" << std::endl;
                        break;
                    }

                    frameCount++;

                    // 每隔frameInterval帧保存一次  
                    if (frameCount % frameInterval == 0) {
                        // 转换像素格式  
                        sws_scale(swsCtx, frame->data, frame->linesize, 0,
                            videoCodecCtx->height, rgbFrame->data, rgbFrame->linesize);

                        // 保存为PNG  
                        char filename[256];
                        sprintf(filename, "%s/frame_%04d.png", outputDir.c_str(), savedFrameCount + 1);

                        if (stbi_write_png(filename, videoCodecCtx->width, videoCodecCtx->height,
                            3, rgbFrame->data[0], rgbFrame->linesize[0])) {
                            std::cout << "保存帧 " << (savedFrameCount + 1) << "/" << maxFrames
                                << " 到 " << filename << " (原始帧号: " << frameCount << ")" << std::endl;
                            savedFrameCount++;
                        }
                        else {
                            std::cerr << "保存PNG失败: " << filename << std::endl;
                        }
                    }
                }
            }
            av_packet_unref(packet);
        }

        std::cout << "提取完成! 总共处理了 " << frameCount << " 帧，保存了 "
            << savedFrameCount << " 帧" << std::endl;

        // 清理内存  
        av_free(rgbBuffer);
        av_frame_free(&rgbFrame);
        av_frame_free(&frame);
        av_packet_free(&packet);

        return savedFrameCount > 0;
    }

private:
    void cleanup() {
        if (swsCtx) {
            sws_freeContext(swsCtx);
            swsCtx = nullptr;
        }
        if (videoCodecCtx) {
            avcodec_free_context(&videoCodecCtx);
        }
        if (formatCtx) {
            avformat_close_input(&formatCtx);
        }
    }
};

int main(int argc, char* argv[]) {



    std::cout << "=== 视频帧提取器 ===" << std::endl;
    std::cout << "使用FFmpeg提取视频帧并保存为PNG" << std::endl;
    std::cout << "====================" << std::endl;

    const std::string inputFile = "D:\\ffbin\\fuyin.mp4";
    const std::string outputDir = "IMGSAVE";
    const int frameInterval = 10;  // 每隔10帧抽取一帧  
    const int maxFrames = 100;     // 最多保存100帧  

    VideoFrameExtractor extractor;

    if (!extractor.initialize(inputFile, outputDir)) {
        std::cerr << "初始化失败!" << std::endl;
        return -1;
    }

    if (!extractor.extractFrames(frameInterval, maxFrames)) {
        std::cerr << "提取帧失败!" << std::endl;
        return -1;
    }

    std::cout << "程序执行完成!" << std::endl;
    return 0;
}

#endif // 0