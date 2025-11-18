

/****************************************************************************
 * 标题: Whisper实时系统音频转录（回环捕获）
 * 文件: whisper_loopback_transcription.cpp
 * 版本: 1.3
 * 作者: AEGLOVE
 * 日期: 2025-11-16
 * 功能: 使用miniaudio捕获系统音频输出（回环设备），通过whisper.cpp实现实时转录
 * 依赖: whisper.cpp, miniaudio, CUDA
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
****************************************************************************/

#if 0  

 
#include "miniaudio.h"  
#include "whisper.h"  

#include <iostream>  
#include <fstream>  
#include <vector>  
#include <mutex>  
#include <cstdint>  
#include <string>  
#include <windows.h>  
#include <conio.h>  // 用于_kbhit()和_getch()  

#pragma comment(lib, "whisper.lib")  
#pragma comment(lib, "ggml.lib")  
#pragma comment(lib, "ggml-base.lib")  
#pragma comment(lib, "ggml-cpu.lib")  
#pragma comment(lib, "ggml-cuda.lib")  
#pragma comment(lib, "cudart_static.lib")  
#pragma comment(lib, "cublas.lib")  
#pragma comment(lib, "cublasLt.lib")  
#pragma comment(lib, "cuda.lib")  

// 音频缓冲区配置  
constexpr int32_t SAMPLE_RATE = 16000;      // Whisper要求16kHz采样率  
constexpr int32_t BUFFER_SIZE_MS = 5000;    // 5秒音频窗口  
constexpr int32_t STEP_SIZE_MS = 500;       // 每500ms处理一次  

// 全局文件输出流  
std::ofstream g_outputFile;
std::mutex g_fileMutex;

class WhisperRealtimeTranscriber {
private:
    whisper_context* ctx;
    std::vector<float> audioBuffer;
    std::mutex bufferMutex;
    ma_device device;
    bool isRunning;
    DWORD startTime;

    // miniaudio回调函数 - 捕获系统音频输出  
    static void audioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* transcriber = static_cast<WhisperRealtimeTranscriber*>(pDevice->pUserData);
        const float* input = static_cast<const float*>(pInput);

        if (input) {
            std::lock_guard<std::mutex> lock(transcriber->bufferMutex);
            transcriber->audioBuffer.insert(
                transcriber->audioBuffer.end(),
                input,
                input + frameCount
            );
        }

        (void)pOutput; // 不需要输出  
    }

public:
    WhisperRealtimeTranscriber(const char* modelPath) : ctx(nullptr), isRunning(false), startTime(0) {
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = true;

        ctx = whisper_init_from_file_with_params(modelPath, cparams);
        if (!ctx) {
            throw std::runtime_error("Failed to load whisper model");
        }

        std::cout << "Whisper模型加载成功" << std::endl;
    }

    ~WhisperRealtimeTranscriber() {
        stop();
        if (ctx) {
            whisper_free(ctx);
        }
    }

    bool start() {
        // 配置回环捕获设备（系统音频输出）  
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_loopback);
        deviceConfig.capture.format = ma_format_f32;      // 32位浮点格式  
        deviceConfig.capture.channels = 1;                  // 单声道（Whisper要求）  
        deviceConfig.sampleRate = SAMPLE_RATE;        // 16kHz采样率  
        deviceConfig.dataCallback = audioCallback;      // 设置回调函数  
        deviceConfig.pUserData = this;               // 传递this指针  
        deviceConfig.periodSizeInFrames = 1024;             // 缓冲区大小  

        // 初始化音频设备  
        ma_result result = ma_device_init(nullptr, &deviceConfig, &device);
        if (result != MA_SUCCESS) {
            std::cerr << "设备初始化失败: " << ma_result_description(result) << std::endl;
            std::cerr << "可能原因:" << std::endl;
            std::cerr << "1. 系统音频服务未运行" << std::endl;
            std::cerr << "2. 缺少音频输出设备" << std::endl;
            std::cerr << "3. 权限不足（需要管理员权限）" << std::endl;
            std::cerr << "4. 系统不支持回环捕获（需要Windows 10或更高版本）" << std::endl;
            return false;
        }

        // 启动音频设备  
        result = ma_device_start(&device);
        if (result != MA_SUCCESS) {
            std::cerr << "设备启动失败: " << ma_result_description(result) << std::endl;
            ma_device_uninit(&device);
            return false;
        }

        isRunning = true;
        startTime = GetTickCount();
        std::cout << "开始捕获系统音频输出..." << std::endl;
        return true;
    }

    void stop() {
        if (isRunning) {
            ma_device_uninit(&device);
            isRunning = false;
            std::cout << "停止捕获" << std::endl;
        }
    }

    void processAudio() {
        std::vector<float> processingBuffer;

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (audioBuffer.size() < SAMPLE_RATE * BUFFER_SIZE_MS / 1000) {
                return; // 缓冲区数据不足  
            }

            // 复制音频数据用于处理  
            size_t bufferSize = SAMPLE_RATE * BUFFER_SIZE_MS / 1000;
            processingBuffer.assign(
                audioBuffer.end() - bufferSize,
                audioBuffer.end()
            );

            // 保留最后一部分数据用于下次处理（重叠）  
            size_t keepSize = SAMPLE_RATE * STEP_SIZE_MS / 1000;
            audioBuffer.erase(
                audioBuffer.begin(),
                audioBuffer.end() - keepSize
            );
        }

        // 配置whisper参数  
        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.language = "zh";           // 中文识别  
        wparams.translate = false;         // 不翻译，仅转录  
        wparams.print_realtime = false;
        wparams.print_progress = false;
        wparams.print_timestamps = true;
        wparams.n_threads = 4;

        // 执行转录  
        if (whisper_full(ctx, wparams, processingBuffer.data(), processingBuffer.size()) != 0) {
            std::cerr << "转录失败" << std::endl;
            return;
        }

        // 输出转录结果  
        const int32_t n_segments = whisper_full_n_segments(ctx);
        for (int32_t i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            const int64_t t0 = whisper_full_get_segment_t0(ctx, i);
            const int64_t t1 = whisper_full_get_segment_t1(ctx, i);

            char buffer[4096];
            snprintf(buffer, sizeof(buffer), "[%6.2f -> %6.2f] %s\n",
                t0 / 100.0f, t1 / 100.0f, text);

            // 输出到控制台  
            printf("%s", buffer);

            // 同时写入文件  
            std::lock_guard<std::mutex> lock(g_fileMutex);
            if (g_outputFile.is_open()) {
                g_outputFile << buffer;
                g_outputFile.flush();
            }
        }
    }

    bool isActive() const { return isRunning; }

    // 显示录制状态  
    void displayStatus() {
        DWORD elapsed = GetTickCount() - startTime;
        float seconds = elapsed / 1000.0f;

        // 估算缓冲区大小（字节）  
        size_t bufferBytes;
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            bufferBytes = audioBuffer.size() * sizeof(float);
        }
        float bufferSizeMB = bufferBytes / (1024.0f * 1024.0f);

        printf("\r| %2d:%02d             | %.2f MB               |",
            (int)seconds / 60, (int)seconds % 60, bufferSizeMB);
    }
};

int main(int argc, char** argv) {
    // 设置控制台UTF-8编码  
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 打开输出文件  
    g_outputFile.open("loopback_transcription.txt", std::ios::out | std::ios::app);
    if (!g_outputFile.is_open()) {
        std::cerr << "无法打开输出文件" << std::endl;
        return 1;
    }

    // 写入文件头  
    g_outputFile << "=== 系统音频转录开始 ===" << std::endl;
    g_outputFile << "时间: " << __DATE__ << " " << __TIME__ << std::endl;
    g_outputFile << std::endl;

    try {
        WhisperRealtimeTranscriber transcriber("models/ggml-large-v3.bin");

        if (!transcriber.start()) {
            g_outputFile.close();
            return 1;
        }

        std::cout << "实时转录已启动（捕获系统音频输出）" << std::endl;
        std::cout << "结果将保存到: loopback_transcription.txt" << std::endl;
        std::cout << "按空格键停止录制..." << std::endl;
        std::cout << "+------------------+-------------------------+" << std::endl;
        std::cout << "| 录制时间          | 缓冲区大小              |" << std::endl;
        std::cout << "+------------------+-------------------------+" << std::endl;

        // 等待空格键停止  
        bool recording = true;
        while (recording && transcriber.isActive()) {
            // 检查按键  
            if (_kbhit()) {
                int ch = _getch();
                if (ch == ' ') {  // 空格键  
                    recording = false;
                }
            }

            // 处理音频  
            transcriber.processAudio();

            // 显示状态  
            transcriber.displayStatus();

            Sleep(100);
        }

        std::cout << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        g_outputFile << "错误: " << e.what() << std::endl;
        g_outputFile.close();
        return 1;
    }

    // 写入文件尾  
    g_outputFile << std::endl;
    g_outputFile << "=== 转录结束 ===" << std::endl;
    g_outputFile.close();

    std::cout << "转录结果已保存到文件" << std::endl;

    return 0;
}

#endif
