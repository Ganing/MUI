

/****************************************************************************
 * 标题: LLAMA对话窗口程序 (改进版)
 * 文件: llama_chat_window.cpp
 * 版本: 3.0
 * 作者: AEGLOVE
 * 日期: 2025-11-17
 * 功能: 使用WIN32 API创建对话窗口,支持实时显示生成过程
 * 依赖: llama.lib, ggml.lib, ggml-cuda.lib, CUDA Runtime
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
****************************************************************************/

#if 0  

#include <windows.h>  
#include <cstdint>  
#include <string>  
#include <vector>  
#include <thread>  
#include <atomic>  
#include <sstream>  

// llama.cpp头文件  
#include "llama.h"  

// 静态库链接  
#pragma comment(lib, "llama.lib")  

#pragma comment(lib, "ggml.lib")  
#pragma comment(lib, "ggml-base.lib")  
#pragma comment(lib, "ggml-cpu.lib")  
#pragma comment(lib, "ggml-cuda.lib")  
#pragma comment(lib, "cudart_static.lib")  
#pragma comment(lib, "cublas.lib")  
#pragma comment(lib, "cublasLt.lib")  
#pragma comment(lib, "cuda.lib")  

// 窗口控件ID  
constexpr int32_t ID_EDIT_INPUT = 101;
constexpr int32_t ID_EDIT_OUTPUT = 102;
constexpr int32_t ID_BUTTON_SEND = 103;
constexpr int32_t ID_BUTTON_CLEAR = 104;

// 自定义消息  
constexpr UINT WM_UPDATE_OUTPUT = WM_USER + 1;
constexpr UINT WM_GENERATION_DONE = WM_USER + 2;

// 全局变量  
HWND g_hwndInput = nullptr;
HWND g_hwndOutput = nullptr;
HWND g_hwndMainWindow = nullptr;
llama_model* g_model = nullptr;
llama_context* g_context = nullptr;
llama_sampler* g_sampler = nullptr;
std::atomic<bool> g_isGenerating(false);
std::wstring g_conversationHistory;

// 初始化llama模型  
bool initLlama(const char* modelPath) {
    llama_backend_init();

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = 99;

    g_model = llama_model_load_from_file(modelPath, modelParams);
    if (!g_model) {
        return false;
    }

    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx = 8192;  // 增加上下文长度以支持多轮对话  
    ctxParams.n_batch = 2048;

    g_context = llama_init_from_model(g_model, ctxParams);
    if (!g_context) {
        return false;
    }

    // 创建采样器链  
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    g_sampler = llama_sampler_chain_init(sparams);

    // 添加采样器 - 参考tools/run/run.cpp的配置  
    llama_sampler_chain_add(g_sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(g_sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(g_sampler, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(g_sampler, llama_sampler_init_dist(12345));

    // 添加重复惩罚采样器  
    llama_sampler_chain_add(g_sampler, llama_sampler_init_penalties(
        64,      // penalty_last_n  
        1.1f,    // penalty_repeat  
        0.0f,    // penalty_freq  
        0.0f     // penalty_present  
    ));

    return true;
}

// 追加文本到输出框  
void appendToOutput(const std::wstring& text) {
    int32_t len = GetWindowTextLengthW(g_hwndOutput);
    SendMessageW(g_hwndOutput, EM_SETSEL, len, len);
    SendMessageW(g_hwndOutput, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageW(g_hwndOutput, EM_SCROLLCARET, 0, 0);
}

// 生成回复 (在后台线程中运行)  
void generateResponseAsync(const std::string& prompt) {
    if (!g_context || g_isGenerating) return;

    g_isGenerating = true;

    const llama_vocab* vocab = llama_model_get_vocab(g_model);

    std::vector<llama_token> tokens;
    tokens.resize(prompt.length() + 1);

    int32_t nTokens = llama_tokenize(vocab, prompt.c_str(),
        prompt.length(), tokens.data(),
        tokens.size(), true, false);
    tokens.resize(nTokens);

    // 创建batch  
    llama_batch batch = llama_batch_init(512, 0, 1);

    // 添加tokens到batch  
    for (int32_t i = 0; i < nTokens; i++) {
        batch.token[i] = tokens[i];
        batch.pos[i] = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = false;
    }
    batch.n_tokens = nTokens;
    batch.logits[nTokens - 1] = true;

    // 处理prompt  
    if (llama_decode(g_context, batch) != 0) {
        llama_batch_free(batch);
        g_isGenerating = false;
        PostMessageW(g_hwndMainWindow, WM_GENERATION_DONE, 0, 0);
        return;
    }

    // 生成tokens - 参考tools/main/main.cpp的生成循环  
    const int32_t maxTokens = 512;  // 最大生成token数  
    for (int32_t i = 0; i < maxTokens; ++i) {
        // 采样  
        llama_token newToken = llama_sampler_sample(g_sampler, g_context, -1);

        // 检查是否为结束token  
        if (llama_vocab_is_eog(vocab, newToken)) {
            break;
        }

        // 接受token  
        llama_sampler_accept(g_sampler, newToken);

        // 转换为文本  
        char buf[128];
        int32_t len = llama_token_to_piece(vocab, newToken, buf, sizeof(buf), 0, false);
        std::string tokenStr(buf, len);

        // 转换为宽字符并发送到主线程更新UI  
        wchar_t wbuf[256];
        MultiByteToWideChar(CP_UTF8, 0, tokenStr.c_str(), -1, wbuf, 256);

        // 使用PostMessage异步更新UI  
        std::wstring* pText = new std::wstring(wbuf);
        PostMessageW(g_hwndMainWindow, WM_UPDATE_OUTPUT, 0, (LPARAM)pText);

        // 准备下一次解码  
        batch.n_tokens = 1;
        batch.token[0] = newToken;
        batch.pos[0] = nTokens + i;
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0] = 0;
        batch.logits[0] = true;

        if (llama_decode(g_context, batch) != 0) {
            break;
        }
    }

    llama_batch_free(batch);
    g_isGenerating = false;
    PostMessageW(g_hwndMainWindow, WM_GENERATION_DONE, 0, 0);
}

// 窗口过程  
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwndMainWindow = hwnd;

        // 创建输入框  
        g_hwndInput = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            10, 10, 560, 100, hwnd, (HMENU)ID_EDIT_INPUT,
            GetModuleHandle(nullptr), nullptr);

        // 创建输出框  
        g_hwndOutput = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            10, 120, 560, 300, hwnd, (HMENU)ID_EDIT_OUTPUT,
            GetModuleHandle(nullptr), nullptr);

        // 创建发送按钮  
        CreateWindowW(L"BUTTON", L"发送",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 430, 100, 30, hwnd, (HMENU)ID_BUTTON_SEND,
            GetModuleHandle(nullptr), nullptr);

        // 创建清空按钮  
        CreateWindowW(L"BUTTON", L"清空对话",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, 430, 100, 30, hwnd, (HMENU)ID_BUTTON_CLEAR,
            GetModuleHandle(nullptr), nullptr);

        // 设置字体  
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        SendMessageW(g_hwndInput, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hwndOutput, WM_SETFONT, (WPARAM)hFont, TRUE);
        break;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_BUTTON_SEND && !g_isGenerating) {
            wchar_t input[2048];
            GetWindowTextW(g_hwndInput, input, 2048);

            if (wcslen(input) == 0) break;

            // 显示用户问题  
            std::wstring userMsg = L"用户: ";
            userMsg += input;
            userMsg += L"\r\n\r\n";
            appendToOutput(userMsg);
            g_conversationHistory += userMsg;

            // 清空输入框  
            SetWindowTextW(g_hwndInput, L"");

            // 显示"助手正在思考..."  
            std::wstring thinkingMsg = L"助手: ";
            appendToOutput(thinkingMsg);

            // 转换为UTF-8  
            char inputUtf8[4096];
            WideCharToMultiByte(CP_UTF8, 0, input, -1, inputUtf8, 4096, nullptr, nullptr);

            // 在新线程中生成回复  
            std::thread([inputUtf8 = std::string(inputUtf8)]() {
                generateResponseAsync(inputUtf8);
                }).detach();

            // 禁用发送按钮  
            EnableWindow(GetDlgItem(hwnd, ID_BUTTON_SEND), FALSE);
        }
        else if (LOWORD(wParam) == ID_BUTTON_CLEAR) {
            SetWindowTextW(g_hwndInput, L"");
            SetWindowTextW(g_hwndOutput, L"");
            g_conversationHistory.clear();

            // 重置采样器状态  
            if (g_sampler) {
                llama_sampler_reset(g_sampler);
            }
        }
        break;
    }

    case WM_UPDATE_OUTPUT: {
        // 从lParam获取文本指针  
        std::wstring* pText = reinterpret_cast<std::wstring*>(lParam);
        if (pText) {
            appendToOutput(*pText);
            g_conversationHistory += *pText;
            delete pText;
        }
        break;
    }

    case WM_GENERATION_DONE: {
        // 添加换行  
        std::wstring endMsg = L"\r\n\r\n";
        appendToOutput(endMsg);
        g_conversationHistory += endMsg;

        // 重新启用发送按钮  
        EnableWindow(GetDlgItem(hwnd, ID_BUTTON_SEND), TRUE);
        break;
    }

    case WM_DESTROY:
        if (g_sampler) llama_sampler_free(g_sampler);
        if (g_context) llama_free(g_context);
        if (g_model) llama_model_free(g_model);
        llama_backend_free();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int main(int argc, char** argv) {
    // 初始化模型  
    const char* modelFilePath = "E:\\AI\\DeepSeek-R1-Distill-Qwen-14B-Q8_0.gguf";///"E:\\AI\\Qwen3VL-30B-A3B-Instruct-Q8_0.gguf";// "E:\\AI\\DeepSeek-R1-Distill-Qwen-14B-Q8_0.gguf";
    if (!initLlama(modelFilePath)) {
        MessageBoxW(nullptr, L"模型加载失败\n请检查模型文件路径是否正确", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 注册窗口类  
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"LlamaChatWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // 创建窗口  
    HWND hwnd = CreateWindowExW(
        0, L"LlamaChatWindow", L"LLAMA对话窗口 - DeepSeek-R1-Distill-Qwen-14B",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        600, 520, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!hwnd) {
        MessageBoxW(nullptr, L"窗口创建失败", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 消息循环  
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

#endif

