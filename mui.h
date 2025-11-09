

/****************************************************************************
 * 标题: MUI 轻量级 UI 框架 - 单头文件版本
 * 文件: mui.h
 * 版本: 0.5
 * 作者: AEGLOVE
 * 更新日期: 2025-11-08
 *
 * 简介:
 *   MUI 是基于 ThorVG(GlCanvas) + OpenGL 的即时模式 UI 小型框架（单头文件）。
 *   本文件在原始 0.3 版本基础上扩展了播放列表、封面管理、歌词视图与媒体播放支持，
 *   适合作为桌面音乐播放器、轻量级工具或嵌入式 UI 的基础库使用。
 *
 * 主要功能（此处列出当前实现的要点）:
 *   - 基于 ThorVG(GlCanvas) + OpenGL 的 2D 矢量即时渲染。
 *   - 简单 3D 渲染器 (Renderer3D) 用于示例/混合渲染。
 *   - 丰富的 UI 组件集合：UIManager, UIElement, UIButton, UILabel,
 *     UITextInput, UISlider, UIProgressBar, UITooltip, UIFrame 等。
 *   - 两种列表控件：ListPanel（通用列表）与 PlayList（支持封面缩略图、缓存、交互）。
 *   - CoverImage：大封面显示 + 全局 LRU 缓存机制，支持从磁盘以宽字符路径读取图片。
 *   - LyricView：LRC 解析、时间驱动滚动与卡拉 OK 高亮效果。
 *   - MPlayer：基于 miniaudio 的播放封装，支持宽字符路径播放、播放控制、音量、
 *     随机顺序生成等。
 *   - 使用 TagLib 提取音频元数据与嵌入封面（extractCover、getSongInfo、scanMusic）。
 *   - 图片加载使用 stb_image/stb_image_write（支持从宽路径读取文件流以避免路径编码问题）。
 *   - UTF-8/UTF-16 工具（wideToUtf8 / utf8ToWide）与 UTF-8 字符处理辅助函数。
 *   - 封面提取、默认封面生成、封面缓存（thumbnailCache / coverCache）与预加载逻辑。
 *   - 从 RCDATA 加载字体到 ThorVG（tvg::Text::load 支持内存字体）。
 *   - 日志输出封装 ODD（默认为 wprintf），并在代码中注意了 UTF-8 -> 宽字符转换以避免乱码。
 *
 * 设计与实现注意事项:
 *   - Windows 专用实现（使用 Win32 API、wchar 宽字符串、CreateWindow/WGL、RT_RCDATA）。
 *   - 建议在 Release 发布时将链接子系统设为 Windows (/SUBSYSTEM:WINDOWS) 以去掉控制台。
 *     开发与调试时可临时 AllocConsole 或在 main/WinMain 中重定向 stdout/stderr 并设置
 *     _setmode(_fileno(stdout), _O_U16TEXT) 以获得宽字符控制台输出。
 *   - 使用 ThorVG 时必须调用 tvg::Initializer::init(...) / tvg::Initializer::term()。
 *   - 图片路径包含中文/特殊字符时，框架通过宽字符文件 API + stbi_load_from_file 减少失败情况。
 *   - 对第三方库返回的窄字符串（如 glGetString）要先按 UTF-8 转宽字符再用宽输出打印。
 *   - 封面缓存与位图内存由框架管理，请在程序退出或需释放时调用 CoverImage::clearCache()。
 *
 * 依赖:
 *   - Windows SDK (Win32)
 *   - OpenGL + glad
 *   - ThorVG (GlCanvas)
 *   - GLM
 *   - miniaudio (静态)
 *   - TagLib (静态)
 *   - stb_image / stb_image_write
 *
 * 环境:
 *   - Windows 10/11 x64, Visual Studio 2022, C++17, Unicode (UTF-16) 字符集
 *
 * 构建/发布提示:
 *   - 调试时可以保留控制台以便输出日志；发布时建议移除控制台并使用 Windows 子系统。
 *   - 确保链接 libthorvg、tag.lib、opengl32.lib 等依赖库。
 *   - 若出现字体/图片加载失败，优先检查路径编码（使用宽路径）与文件是否存在。
 *
 * 版权与许可:
 *   - 框架代码由作者 AEGLOVE 编写。请在遵守原作者许可规定下使用、修改与发布。
 *
 ****************************************************************************/

#pragma once  

#define NOMINMAX
#include <windows.h>        // Win32 API: CreateWindow/ShowWindow/UpdateWindow, GetModuleFileNameW, LoadResource, FindResourceW, HDC/HWND/HGLRC 等
#include <windowsx.h>      // Win32 帮助宏: GET_X_LPARAM/GET_Y_LPARAM 等消息参数辅助宏
#include <set>             // std::set 容器（用于有序集合）
#include <thread>          // std::thread, std::this_thread, std::thread::hardware_concurrency
#include <string>          // std::string / std::wstring 字符串类型与操作
#include <cwchar>          // 宽字符 C API：std::wprintf, swprintf_s, _wfopen 等
#include <vector>          // std::vector 动态数组容器
#include <random>          // 随机数：std::random_device, std::mt19937, std::shuffle
#include <chrono>          // 时间/计时：std::chrono::steady_clock, duration, Sleep 时间计算
#include <memory>          // 智能指针：std::unique_ptr, std::shared_ptr
#include <cstdint>         // 固定宽度整数类型：int32_t, uint32_t 等
#include <fstream>         // 文件流 I/O: std::ifstream / std::ofstream（二进制封面写入）
#include <iostream>        // 控制台 I/O: std::wcout / std::cout（调试输出）
#include <algorithm>       // 算法：std::clamp, std::find, std::transform, std::replace, std::shuffle 等
#include <filesystem>      // 文件系统操作：std::filesystem::path, directory_iterator, exists, create_directories
#include <functional>      // std::function 回调包装
#include <unordered_map>   // 哈希表容器（如需快速映射时可用）


#include "resource.h"  
#include <glad/glad.h>  

#define TVG_STATIC  
#include <thorvg/thorvg.h>  

#include <glm/glm.hpp>  
#include <glm/gtc/matrix_transform.hpp>  
#include <glm/gtc/type_ptr.hpp>

 // miniaudio静态库
 #include "miniaudio.h" 
 // TagLib 静态库  
 #define TAGLIB_STATIC    
 #include "tag.h"    
 #include "fileref.h"  
 #include "flacfile.h"    
 #include "tpropertymap.h"  
 #pragma comment(lib, "tag.lib")
// stb静态库
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma comment(lib, "user32.lib")  
#pragma comment(lib, "winmm.lib")  
#pragma comment(lib, "opengl32.lib")  
#pragma comment(lib, "libthorvg.lib")  

#define ODD(...) wprintf(__VA_ARGS__)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 (M_PI/2.0)
#endif

namespace fs = std::filesystem;


 // 全局结构、函数、静态常量  
namespace MUI {

    // 颜色结构  
    struct Color {
        uint8_t r, g, b, a;

        Color(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255)
            : r(r), g(g), b(b), a(a) {
        }
    };

    // 矩形结构  
    struct Rect {
        float x, y, w, h;

        Rect(float x = 0.0f, float y = 0.0f, float w = 0.0f, float h = 0.0f)
            : x(x), y(y), w(w), h(h) {
        }

        bool contains(float px, float py) const {
            return px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    typedef struct OBitmap_t {
        void* data;             // 图像原始数据
        int width;              // 原始宽度
        int height;             // 原始宽度
        int bpp;				// 每个像素需要多少个比特bits
        int pitch;				//(((8*img.width) + 31) >> 5) << 2; 如果4字节对齐，每行需要的字节byte数目
        int pixSize;			// 像素数据 大小=pitch*height
        int tag;               // 字体渲染中的top度量，或者纹理的Mipmap levels, 1 by default
        int format;             // 像素格式，和OGL的对齐。
    }OBitmap;

    // UTF-8 转 UTF-16  
    std::wstring utf8ToWide(const std::string& utf8str) {
        if (utf8str.empty()) return std::wstring();

        std::wstring result;
        result.reserve(utf8str.length());

        size_t i = 0;
        while (i < utf8str.length()) {
            uint32_t codepoint = 0;
            unsigned char c = utf8str[i];

            if (c <= 0x7F) {
                codepoint = c;
                i += 1;
            }
            else if ((c & 0xE0) == 0xC0) {
                if (i + 1 >= utf8str.length()) break;
                codepoint = ((c & 0x1F) << 6) | (utf8str[i + 1] & 0x3F);
                i += 2;
            }
            else if ((c & 0xF0) == 0xE0) {
                if (i + 2 >= utf8str.length()) break;
                codepoint = ((c & 0x0F) << 12) |
                    ((utf8str[i + 1] & 0x3F) << 6) |
                    (utf8str[i + 2] & 0x3F);
                i += 3;
            }
            else if ((c & 0xF8) == 0xF0) {
                if (i + 3 >= utf8str.length()) break;
                codepoint = ((c & 0x07) << 18) |
                    ((utf8str[i + 1] & 0x3F) << 12) |
                    ((utf8str[i + 2] & 0x3F) << 6) |
                    (utf8str[i + 3] & 0x3F);
                i += 4;

                if (codepoint > 0xFFFF) {
                    codepoint -= 0x10000;
                    result.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
                    result.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
                    continue;
                }
            }
            else {
                i += 1;
                continue;
            }

            result.push_back(static_cast<wchar_t>(codepoint));
        }

        return result;
    }

    // UTF-16 转 UTF-8  
    std::string wideToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();

        // ✅ 使用 Windows API 进行正确的转换  
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
            (int)wstr.length(), NULL, 0, NULL, NULL);
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(),
            &result[0], size_needed, NULL, NULL);
        return result;
    }

    // 从文件加载图片到 OBitmap  
    OBitmap loadImageToBitmap(const std::wstring& filePath) {
        ODD(L"开始加载图片: %ls\n", filePath.c_str());
        OBitmap bitmap = {};

        // 直接使用宽字符API打开文件，避免编码转换问题
        FILE* file = _wfopen(filePath.c_str(), L"rb");
        if (!file) {
            ODD(L"无法打开文件: %ls\n", filePath.c_str());
            return bitmap;
        }

        int width, height, channels;
        // 从文件流加载图片，避免路径编码问题
        unsigned char* data = stbi_load_from_file(file, &width, &height, &channels, 4);
        fclose(file);

        if (!data) {
            ODD(L"图片加载失败: %ls\n", filePath.c_str());
            ODD(L"错误信息: %s\n", stbi_failure_reason());
            return bitmap;  // 加载失败,返回空结构  
        }

        ODD(L"图片加载成功: %dx%d, 通道数: %d\n", width, height, channels);

        bitmap.width = width;
        bitmap.height = height;
        bitmap.bpp = 32;  // RGBA = 32 bits per pixel  
        bitmap.pitch = width * 4;  // 每行字节数 (4字节对齐已满足)  
        bitmap.pixSize = bitmap.pitch * height;
        bitmap.tag = 1;  // Mipmap level 1  
        bitmap.format = 0x1908;  // GL_RGBA  

        ODD(L"位图信息: 宽度=%d, 高度=%d, BPP=%d, 每行字节数=%d, 总大小=%d\n",
            bitmap.width, bitmap.height, bitmap.bpp, bitmap.pitch, bitmap.pixSize);

        // 分配内存并复制数据  
        bitmap.data = malloc(bitmap.pixSize);
        if (bitmap.data) {
            memcpy(bitmap.data, data, bitmap.pixSize);
            ODD(L"位图数据分配成功，大小: %d 字节\n", bitmap.pixSize);
        }
        else {
            ODD(L"位图数据分配失败: 无法分配 %d 字节内存\n", bitmap.pixSize);
            stbi_image_free(data);
            return bitmap;  // 返回空结构
        }

        stbi_image_free(data);  // 释放 stb_image 分配的内存  
        ODD(L"图片加载完成: %ls\n", filePath.c_str());

        return bitmap;
    }

    // 释放 OBitmap 资源  
    void freeBitmap(OBitmap& bitmap) {
        if (bitmap.data) {
            free(bitmap.data);
            bitmap.data = nullptr;
        }
    }

    // UTF-8 字符串处理工具
    namespace UTF8 {
        // 获取 UTF-8 字符的字节长度  
        inline size_t charLength(const char* str) {
            unsigned char c = static_cast<unsigned char>(*str);
            if (c < 0x80) return 1;
            if ((c & 0xE0) == 0xC0) return 2;
            if ((c & 0xF0) == 0xE0) return 3;
            if ((c & 0xF8) == 0xF0) return 4;
            return 1;  // 无效字符,当作单字节  
        }

        // 计算 UTF-8 字符串中的字符数量  
        inline size_t charCount(const std::string& str) {
            size_t count = 0;
            const char* p = str.c_str();
            while (*p) {
                p += charLength(p);
                count++;
            }
            return count;
        }

        // 获取字节偏移对应的字符索引  
        inline size_t byteToCharIndex(const std::string& str, size_t byteOffset) {
            size_t charIndex = 0;
            size_t currentByte = 0;
            const char* p = str.c_str();

            while (*p && currentByte < byteOffset) {
                size_t len = charLength(p);
                currentByte += len;
                p += len;
                charIndex++;
            }
            return charIndex;
        }

        // 获取字符索引对应的字节偏移  
        inline size_t charToByteIndex(const std::string& str, size_t charIndex) {
            size_t byteOffset = 0;
            size_t currentChar = 0;
            const char* p = str.c_str();

            while (*p && currentChar < charIndex) {
                size_t len = charLength(p);
                byteOffset += len;
                p += len;
                currentChar++;
            }
            return byteOffset;
        }
    }


} // namespace MUI

// 定时器类
namespace MUI {
    class Timer {
    private:
        std::chrono::steady_clock::time_point lastTime;
        float interval;  // 秒  
        float elapsed;
        bool running;
        std::function<void()> callback;

    public:
        Timer(float intervalSeconds = 0.5f)
            : interval(intervalSeconds), elapsed(0.0f), running(false) {
            lastTime = std::chrono::steady_clock::now();
        }

        void setInterval(float seconds) {
            interval = seconds;
        }

        void setCallback(std::function<void()> cb) {
            callback = cb;
        }

        void start() {
            running = true;
            elapsed = 0.0f;
            lastTime = std::chrono::steady_clock::now();
        }

        void stop() {
            running = false;
        }

        void reset() {
            elapsed = 0.0f;
            lastTime = std::chrono::steady_clock::now();
        }

        void update(float deltaTime) {
            if (!running) return;

            elapsed += deltaTime;
            if (elapsed >= interval) {
                elapsed -= interval;
                if (callback) callback();
            }
        }

        bool isRunning() const { return running; }
    };
}

// Song 结构与工具函数  
namespace MUI {
    // 歌曲结构(宽字符版本)  
    struct Song {
        std::wstring title;                   // 歌曲标题(显示用)  
        std::wstring artist;                  // 艺术家/演唱者  
        std::wstring album;                   // 专辑名  
        std::wstring filePath;                // 播放用完整路径  
        float        duration = 0.0f;         // 秒:歌曲时长(0表示未知或未读取)  

        int32_t      bitrate = 0;             // kb/s:比特率(0表示未知)  
        int32_t      year = 0;                // 年份(0表示未知)  
        int32_t      track = 0;               // 曲目编号(0表示未知)  

        std::vector<std::wstring> coverPaths; // 提取到的封面临时文件路径列表(可有多张)  
        int32_t embeddedCoverCount = 0;       // 嵌入在音频文件内部的封面数量  
        size_t coverCount() const { return coverPaths.size(); }
    };

    

    // 提取封面  
    std::wstring extractCover(const std::wstring& songPath, const std::wstring& toPath = L"", int32_t idx = 0) {
        TagLib::FileRef file(songPath.c_str());

        if (file.isNull() || !file.file()) {
            return L"";
        }

        auto pictures = file.complexProperties("PICTURE");
        if (pictures.isEmpty() || idx >= static_cast<int32_t>(pictures.size())) {
            return L"";
        }

        auto picture = pictures[idx];
        if (!picture.contains("data")) {
            return L"";
        }

        auto imageData = picture.value("data").value<TagLib::ByteVector>();
        if (imageData.isEmpty()) {
            return L"";
        }

        // 确定保存目录  
        fs::path savePath;
        if (toPath.empty()) {
            savePath = fs::path(songPath).parent_path();
        }
        else {
            savePath = fs::path(toPath);
            try {
                if (!fs::exists(savePath)) {
                    fs::create_directories(savePath);
                }
            }
            catch (...) {
                return L"";
            }
        }

        std::wstring songName = fs::path(songPath).stem().wstring();

        // 确定扩展名  
        std::wstring extension = L".jpg";
        if (picture.contains("mimeType")) {
            std::string mimeType = picture.value("mimeType").value<TagLib::String>().to8Bit(true);
            if (mimeType.find("png") != std::string::npos) {
                extension = L".png";
            }
            else if (mimeType.find("gif") != std::string::npos) {
                extension = L".gif";
            }
            else if (mimeType.find("bmp") != std::string::npos) {
                extension = L".bmp";
            }
        }

        std::wstring fileName = songName + L"_" + std::to_wstring(idx) + extension;
        fs::path fullPath = savePath / fileName;

        std::ofstream outFile(fullPath, std::ios::binary);
        if (!outFile) {
            return L"";
        }

        outFile.write(imageData.data(), imageData.size());
        outFile.close();

        // 统一路径分隔符后再返回  
        std::wstring result = fullPath.wstring();
        std::replace(result.begin(), result.end(), L'\\', L'/');
        return result;
    }

    Song getSongInfo(const std::wstring& songPath) {
        Song s;
        // 统一路径分隔符为正斜杠  
        s.filePath = songPath;
        std::replace(s.filePath.begin(), s.filePath.end(), L'\\', L'/');


        TagLib::FileRef f(songPath.c_str());

        if (f.isNull()) {
            return s;
        }

        if (f.tag()) {
            TagLib::Tag* tag = f.tag();

            try {
                // 使用 toCWString() 直接获取宽字符字符串  
                s.title = tag->title().toCWString();
            }
            catch (...) {
                s.title = L"[Error]";
            }

            try {
                s.artist = tag->artist().toCWString();
            }
            catch (...) {
                s.artist = L"[Error]";
            }

            try {
                s.album = tag->album().toCWString();
                if (s.album.length() > 500) {
                    s.album = s.album.substr(0, 500) + L"...";
                }
                // 空值替换为"未知"  
                if (s.album.empty()) {
                    s.album = L"未知";
                }
            }
            catch (...) {
                s.album = L"[Error]";
            }

            s.year = tag->year();
            s.track = tag->track();
        }

        if (s.title.empty()) {
            s.title = fs::path(songPath).filename().wstring();
        }

        if (f.audioProperties()) {
            TagLib::AudioProperties* props = f.audioProperties();
            if (props) {
                s.duration = static_cast<float>(props->lengthInSeconds());
                s.bitrate = props->bitrate();
            }
        }

        TagLib::StringList keys = f.complexPropertyKeys();
        if (keys.contains("PICTURE")) {
            auto pics = f.complexProperties("PICTURE");
            s.embeddedCoverCount = static_cast<int32_t>(pics.size());
        }

        return s;
    }

    // 格式化时长  
    std::wstring formatDuration(float seconds) {
        if (seconds <= 0.0f) return L"0:00";
        int32_t s = static_cast<int32_t>(seconds);
        int32_t mm = s / 60;
        int32_t ss = s % 60;
        wchar_t buf[32];
        swprintf_s(buf, 32, L"%d:%02d", mm, ss);
        return std::wstring(buf);
    }

    // 显示歌曲信息  
    void showSongInfo(const Song& s) {
        std::wcout << L"----- Song Info -----" << std::endl;
        std::wcout << L"Title   : " << s.title << std::endl;
        std::wcout << L"Artist  : " << s.artist << std::endl;
        std::wcout << L"Album   : " << s.album << std::endl;
        std::wcout << L"Path    : " << s.filePath << std::endl;
        std::wcout << L"Duration: " << formatDuration(s.duration)
            << L" (" << static_cast<int32_t>(s.duration) << L" s)" << std::endl;
        std::wcout << L"Bitrate : " << s.bitrate << L" kb/s" << std::endl;
        std::wcout << L"Year    : " << s.year << std::endl;
        std::wcout << L"Track   : " << s.track << std::endl;
        std::wcout << L"Covers  : " << s.embeddedCoverCount << std::endl;
        std::wcout << L"Extracted: " << s.coverPaths.size() << std::endl;

        for (size_t i = 0; i < s.coverPaths.size(); ++i) {
            std::wcout << L"  [" << (i + 1) << L"] " << s.coverPaths[i] << std::endl;
        }
        std::wcout << L"---------------------" << std::endl;
    }
}

// 封面相关
namespace MUI
{
    // 获取当前可执行文件所在的目录路径
    static std::wstring getExecutableDirectory() {
        wchar_t buf[MAX_PATH] = { 0 };  // 缓冲区，用于存储完整路径
        // 获取当前可执行文件的完整路径（包括文件名）
        DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (len == 0) return L"";  // 获取失败时返回空字符串

        // 将路径字符串转换为filesystem路径对象
        std::filesystem::path p(buf);
        // 返回父目录（即去掉文件名后的路径）
        return p.parent_path().wstring();
    }

    /**
     * @brief 创建数据目录结构（data/cover, data/image, data/text）
     * @param basePath 基础路径，通常为当前工作目录
     * @return 创建成功返回true，失败返回false
     */
    bool createDataSubdirs(const std::wstring& basePath) {
        try {
            // 构建各级目录路径
            std::filesystem::path dataDir = std::filesystem::path(basePath) / L"data";
            std::filesystem::path coverDir = dataDir / L"cover";
            std::filesystem::path imageDir = dataDir / L"image";
            std::filesystem::path textDir = dataDir / L"text";

            // 创建所有目录（如果已存在则不会重复创建）
            bool dataCreated = std::filesystem::create_directories(dataDir);
            bool coverCreated = std::filesystem::create_directories(coverDir);
            bool imageCreated = std::filesystem::create_directories(imageDir);
            bool textCreated = std::filesystem::create_directories(textDir);

            // 检查所有目录是否创建成功或已存在
            bool dataExists = std::filesystem::exists(dataDir) && std::filesystem::is_directory(dataDir);
            bool coverExists = std::filesystem::exists(coverDir) && std::filesystem::is_directory(coverDir);
            bool imageExists = std::filesystem::exists(imageDir) && std::filesystem::is_directory(imageDir);
            bool textExists = std::filesystem::exists(textDir) && std::filesystem::is_directory(textDir);

            return dataExists && coverExists && imageExists && textExists;
        }
        catch (const std::filesystem::filesystem_error& ex) {
            ODD(L"创建数据子目录失败: %s\n", ex.what());
            return false;
        }
        catch (const std::exception& ex) {
            ODD(L"创建数据子目录时发生异常: %Ts\n", ex.what());
            return false;
        }
    }

    /**
 * @brief 从歌曲文件名生成对应的封面文件名（通过检查实际文件确定扩展名）
 * @param coverDir 封面目录
 * @param songPath 歌曲文件路径
 * @param index 封面索引（用于多封面情况）
 * @return 封面文件路径（如果存在），否则返回空字符串
 */
    std::wstring findCoverFile(const std::filesystem::path& coverDir, const std::wstring& songPath, int index = 0) {
        std::filesystem::path path(songPath);
        std::wstring stem = path.stem().wstring();

        // 可能的图片扩展名
        std::vector<std::wstring> extensions = { L".jpg", L".jpeg", L".png", L".gif", L".bmp" };

        // 生成可能的文件名
        std::wstring baseName = (index == 0) ? stem : stem + L"_" + std::to_wstring(index);

        // 检查每个可能的扩展名
        for (const auto& ext : extensions) {
            std::wstring filename = baseName + ext;
            std::filesystem::path coverPath = coverDir / filename;

            if (std::filesystem::exists(coverPath)) {
                std::wstring result = coverPath.wstring();
                std::replace(result.begin(), result.end(), L'\\', L'/');
                return result;
            }
        }

        return L""; // 没有找到对应的封面文件
    }

    /**
     * @brief 创建默认封面图片
     * @param coverDir 封面目录路径
     * @return 成功返回true，失败返回false
     */
    bool createDefaultCover(const std::filesystem::path& coverDir) {
        std::filesystem::path defaultCoverPath = coverDir / L"defaultcover.png";

        // 如果默认封面已存在，直接返回成功
        if (std::filesystem::exists(defaultCoverPath)) {
            return true;
        }

        // 使用stb_image_write创建默认封面
        // 这里创建一个简单的128x128蓝色渐变图片作为默认封面
        const int width = 128;
        const int height = 128;
        const int channels = 3; // RGB

        // 分配图片数据内存
        unsigned char* image_data = new unsigned char[width * height * channels];

        // 生成蓝色渐变图片
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = (y * width + x) * channels;

                // 简单的蓝色渐变
                float fx = static_cast<float>(x) / width;
                float fy = static_cast<float>(y) / height;

                image_data[index] = static_cast<unsigned char>(fx * 100);     // R
                image_data[index + 1] = static_cast<unsigned char>(fy * 100); // G
                image_data[index + 2] = static_cast<unsigned char>(200);       // B
            }
        }

        // 使用stb_image_write保存为PNG
        // 注意：stb_image_write需要窄字符串路径
        std::string narrowPath = defaultCoverPath.string();
        int result = stbi_write_png(narrowPath.c_str(), width, height, channels, image_data, width * channels);

        // 释放内存
        delete[] image_data;

        if (result == 0) {
            ODD(L"创建默认封面失败: %ls\n", defaultCoverPath.wstring().c_str());
            return false;
        }

        ODD(L"创建默认封面成功: %ls\n", defaultCoverPath.wstring().c_str());
        return true;
    }

    /**
     * @brief 为歌曲列表提取和管理封面图片
     * @param songs 歌曲列表
     * @param dataParentPath data目录的父路径
     * @return 成功提取的封面数量
     */
    int extractAndManageCovers(std::vector<MUI::Song>& songs, const std::wstring& dataParentPath)
    {
        // 创建目录结构
        if (!createDataSubdirs(dataParentPath)) {
            ODD(L"创建数据目录失败，无法提取封面\n");
            return 0;
        }

        std::filesystem::path coverDir = std::filesystem::path(dataParentPath) / L"data" / L"cover";

        // 创建默认封面
        if (!createDefaultCover(coverDir)) {
            ODD(L"警告: 创建默认封面失败\n");
        }

        int totalCoversExtracted = 0;

        for (auto& song : songs) {
            // 如果歌曲已经有封面路径，跳过处理
            if (!song.coverPaths.empty()) {
                ODD(L"歌曲已有封面，跳过处理: %ls\n", song.filePath.c_str());
                continue;
            }

            // 首先检查data/cover目录下是否已有该歌曲的封面
            bool foundExistingCovers = false;

            // 根据embeddedCoverCount检查封面
            for (int i = 0; i < song.embeddedCoverCount; i++) {
                std::wstring existingCoverPath = findCoverFile(coverDir, song.filePath, i);

                if (!existingCoverPath.empty()) {
                    song.coverPaths.push_back(existingCoverPath);
                    foundExistingCovers = true;
                    ODD(L"找到已存在的封面: %ls\n", existingCoverPath.c_str());
                }
            }

            // 如果data/cover目录下没有封面，尝试从音频文件提取
            if (!foundExistingCovers && song.embeddedCoverCount > 0) {
                ODD(L"尝试从音频文件提取封面: %ls (数量: %d)\n",
                    song.filePath.c_str(), song.embeddedCoverCount);

                // 根据embeddedCoverCount提取封面
                for (int i = 0; i < song.embeddedCoverCount; i++) {
                    std::wstring extractedPath = MUI::extractCover(song.filePath, coverDir.wstring(), i);
                    if (!extractedPath.empty()) {
                        song.coverPaths.push_back(extractedPath);
                        totalCoversExtracted++;
                        ODD(L"成功提取封面: %ls\n", extractedPath.c_str());
                    }
                    else {
                        ODD(L"提取封面失败 (索引 %d): %ls\n", i, song.filePath.c_str());
                        // 即使某个索引失败，也继续尝试其他索引
                    }
                }
            }

            // 如果没有找到任何封面，使用默认封面
            if (song.coverPaths.empty()) {
                std::filesystem::path defaultCoverPath = coverDir / L"defaultcover.png";
                if (std::filesystem::exists(defaultCoverPath)) {
                    std::wstring defaultCover = defaultCoverPath.wstring();
                    std::replace(defaultCover.begin(), defaultCover.end(), L'\\', L'/');
                    song.coverPaths.push_back(defaultCover);
                    ODD(L"使用默认封面: %ls\n", defaultCover.c_str());
                }
            }
        }

        ODD(L"封面处理完成，共提取 %d 个新封面\n", totalCoversExtracted);
        return totalCoversExtracted;
    }

    /**
 * @brief 扫描音乐文件夹并提取封面
 * @param musicFolder 要扫描的音乐文件夹路径
 * @param saveFolder 封面保存的文件夹路径
 * @return 包含歌曲信息的向量
 */
    std::vector<MUI::Song> scanMusic(const std::wstring& musicFolder, const std::wstring& saveFolder) {
        std::vector<MUI::Song> songs;

        // 创建保存目录结构
        if (!createDataSubdirs(saveFolder)) {
            ODD(L"创建数据目录失败，无法扫描音乐\n");
            return songs;
        }

        std::filesystem::path coverDir = std::filesystem::path(saveFolder) / L"data" / L"cover";

        // 创建默认封面
        if (!createDefaultCover(coverDir)) {
            ODD(L"警告: 创建默认封面失败\n");
        }

        ODD(L"开始扫描音乐文件夹: %ls\n", musicFolder.c_str());

        try {
            // 扫描音乐文件夹
            for (const auto& entry : std::filesystem::directory_iterator(musicFolder)) {
                if (entry.is_regular_file()) {
                    std::wstring extension = entry.path().extension().wstring();
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                    if (extension == L".mp3" || extension == L".flac" || extension == L".wav") {
                        std::wstring filePath = entry.path().wstring();

                        // 获取歌曲信息
                        MUI::Song song = MUI::getSongInfo(filePath);

                        // 如果歌曲已经有封面路径，跳过处理
                        if (!song.coverPaths.empty()) {
                            ODD(L"歌曲已有封面，跳过处理: %ls\n", filePath.c_str());
                            songs.push_back(song);
                            continue;
                        }

                        // 首先检查保存目录下是否已有该歌曲的封面
                        bool foundExistingCovers = false;

                        // 根据embeddedCoverCount检查封面
                        for (int i = 0; i < song.embeddedCoverCount; i++) {
                            std::wstring existingCoverPath = findCoverFile(coverDir, filePath, i);

                            if (!existingCoverPath.empty()) {
                                song.coverPaths.push_back(existingCoverPath);
                                foundExistingCovers = true;
                                ODD(L"找到已存在的封面: %ls\n", existingCoverPath.c_str());
                            }
                        }

                        // 如果保存目录下没有封面，尝试从音频文件提取
                        if (!foundExistingCovers && song.embeddedCoverCount > 0) {
                            ODD(L"尝试从音频文件提取封面: %ls (数量: %d)\n",
                                filePath.c_str(), song.embeddedCoverCount);

                            // 根据embeddedCoverCount提取封面
                            for (int i = 0; i < song.embeddedCoverCount; i++) {
                                std::wstring extractedPath = MUI::extractCover(filePath, coverDir.wstring(), i);
                                if (!extractedPath.empty()) {
                                    song.coverPaths.push_back(extractedPath);
                                    ODD(L"成功提取封面: %ls\n", extractedPath.c_str());
                                }
                                else {
                                    ODD(L"提取封面失败 (索引 %d): %ls\n", i, filePath.c_str());
                                }
                            }
                        }

                        // 如果没有找到任何封面，使用默认封面
                        if (song.coverPaths.empty()) {
                            std::filesystem::path defaultCoverPath = coverDir / L"defaultcover.png";
                            if (std::filesystem::exists(defaultCoverPath)) {
                                std::wstring defaultCover = defaultCoverPath.wstring();
                                std::replace(defaultCover.begin(), defaultCover.end(), L'\\', L'/');
                                song.coverPaths.push_back(defaultCover);
                                ODD(L"使用默认封面: %ls\n", defaultCover.c_str());
                            }
                        }

                        songs.push_back(song);
                        ODD(L"添加歌曲: %ls - %ls\n", song.title.c_str(), song.artist.c_str());
                    }
                }
            }

            ODD(L"扫描完成，共找到 %zu 首歌曲\n", songs.size());
        }
        catch (const std::filesystem::filesystem_error& e) {
            ODD(L"扫描音乐文件夹失败: %Ts\n", e.what());
        }

        return songs;
    }

}

// MPlayer 类与实现  
namespace MUI {
    // 播放器状态  
    enum class PlaybackState { Stopped, Playing, Paused };
    enum class PlayMode { Normal, Repeat, Shuffle };

    // 播放器数据  
    struct PlayerData {
        PlaybackState state = PlaybackState::Stopped;
        PlayMode mode = PlayMode::Normal;
        int32_t currentSongIndex = 0;
        float currentPosition = 0.0f;
        float currentDuration = 0.0f;
        int32_t volume = 80;
        bool isDraggingProgress = false;
        std::vector<Song> songLibrary;
        std::vector<int32_t> shuffleOrder;
    };

    class MPlayer {
    public:
        MPlayer();
        ~MPlayer();

        // 基本控制  
        bool init();
        void cleanup();
        void playAudio(const std::wstring& filePath);
        void pauseAudio();
        void resumeAudio();
        void stopAudio();
        void seekAudio(float position);
        void updateAudioPosition();

        // 歌曲库管理  
        void initSongLibrary(const std::wstring& MUSIC_FOLDER = L"E:/xmusic");
        void preSong();
        void nextSong();
        void playSongByIndex(int32_t index);

        // 获取器  
        PlaybackState getPlaybackState() const { return playerData.state; }
        PlayMode getPlayMode() const { return playerData.mode; }
        int32_t getCurrentSongIndex() const { return playerData.currentSongIndex; }
        const std::vector<Song>& getSongLibrary() const  { return playerData.songLibrary; }
        float getCurrentPosition() const { return playerData.currentPosition; }
        float getCurrentDuration() const { return playerData.currentDuration; }
        int32_t getVolume() const { return playerData.volume; }
        const Song* getCurrentSong() const {
            if (playerData.currentSongIndex >= 0 &&
                playerData.currentSongIndex < static_cast<int32_t>(playerData.songLibrary.size())) {
                return &playerData.songLibrary[playerData.currentSongIndex];
            }
            return nullptr;
        }
        void setSongLibrary(const std::vector<Song>& songs);

        // 设置器  
        void setVolume(float volume) {
            playerData.volume = static_cast<int32_t>(std::clamp(volume, 0.0f, 100.0f));
            if (soundInitialized) {
                ma_sound_set_volume(&currentSound, playerData.volume / 100.0f);
            }
        }

        void setPlayMode(PlayMode mode) {
            if (playerData.mode != mode) {
                playerData.mode = mode;
                if (mode == PlayMode::Shuffle) {
                    generateShuffleOrder();
                }
            }
        }

        // 检查是否播放结束  
        bool isPlaybackFinished() const {
            if (!soundInitialized || playerData.state != PlaybackState::Playing) {
                return false;
            }
            return !ma_sound_is_playing(&currentSound);
        }

    private:
        ma_engine engine;
        ma_sound currentSound;
        PlayerData playerData;
        bool engineInitialized = false;
        bool soundInitialized = false;

        void generateShuffleOrder();
        int32_t getNextSongIndex() const;
        int32_t getPrevSongIndex() const;
    };

    // 实现部分  
    MPlayer::MPlayer() {
        // 构造函数保持简单  
    }

    MPlayer::~MPlayer() {
        cleanup();
    }

    bool MPlayer::init() {
        ma_result result = ma_engine_init(NULL, &engine);
        if (result != MA_SUCCESS) {
            std::wcout << L"Failed to initialize audio engine" << std::endl;
            return false;
        }
        engineInitialized = true;
        return true;
    }

    void MPlayer::cleanup() {
        if (soundInitialized) {
            ma_sound_uninit(&currentSound);
            soundInitialized = false;
        }
        if (engineInitialized) {
            ma_engine_uninit(&engine);
            engineInitialized = false;
        }
    }

    void MPlayer::playAudio(const std::wstring& filePath) {
        if (soundInitialized) {
            ma_sound_stop(&currentSound);
            ma_sound_uninit(&currentSound);
            soundInitialized = false;
        }

        // 统一路径分隔符为正斜杠  
        std::wstring normalizedPath = filePath;
        std::replace(normalizedPath.begin(), normalizedPath.end(), L'\\', L'/');

        ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC |
            MA_SOUND_FLAG_WAIT_INIT | MA_SOUND_FLAG_NO_SPATIALIZATION;

        // 直接使用宽字符版本的函数  
        ma_result result = ma_sound_init_from_file_w(&engine, normalizedPath.c_str(), flags, NULL, NULL, &currentSound);

        if (result == MA_SUCCESS) {
            soundInitialized = true;
            ma_sound_get_length_in_seconds(&currentSound, &playerData.currentDuration);
            ma_sound_set_volume(&currentSound, playerData.volume / 100.0f);
            ma_sound_start(&currentSound);
            playerData.state = PlaybackState::Playing;
            playerData.currentPosition = 0.0f;
        }
        else {
            playerData.state = PlaybackState::Stopped;
            std::wcout << L"Failed to load audio file: " << normalizedPath << std::endl;
        }
    }

    void MPlayer::pauseAudio() {
        if (soundInitialized && playerData.state == PlaybackState::Playing) {
            updateAudioPosition();
            ma_sound_stop(&currentSound);
            playerData.state = PlaybackState::Paused;
        }
    }

    void MPlayer::resumeAudio() {
        if (soundInitialized && playerData.state == PlaybackState::Paused) {
            ma_sound_seek_to_pcm_frame(&currentSound,
                static_cast<ma_uint64>(playerData.currentPosition * ma_engine_get_sample_rate(&engine)));
            ma_sound_start(&currentSound);
            playerData.state = PlaybackState::Playing;
        }
    }

    void MPlayer::stopAudio() {
        if (soundInitialized) {
            ma_sound_stop(&currentSound);
            ma_sound_uninit(&currentSound);
            soundInitialized = false;
        }
        playerData.state = PlaybackState::Stopped;
        playerData.currentPosition = 0.0f;
        playerData.currentDuration = 0.0f;
    }

    void MPlayer::seekAudio(float position) {
        if (soundInitialized &&
            (playerData.state == PlaybackState::Playing || playerData.state == PlaybackState::Paused)) {
            ma_uint64 targetFrame = static_cast<ma_uint64>(position * ma_engine_get_sample_rate(&engine));
            ma_sound_seek_to_pcm_frame(&currentSound, targetFrame);
            playerData.currentPosition = position;
        }
    }

    void MPlayer::updateAudioPosition() {
        if (soundInitialized && playerData.state == PlaybackState::Playing) {
            ma_uint64 cursor;
            if (ma_sound_get_cursor_in_pcm_frames(&currentSound, &cursor) == MA_SUCCESS) {
                playerData.currentPosition = static_cast<float>(cursor) / ma_engine_get_sample_rate(&engine);
            }
        }
    }

    void MPlayer::initSongLibrary(const std::wstring& MUSIC_FOLDER) {
        try {
            playerData.songLibrary.clear();
            std::wstring exeDir = MUI::getExecutableDirectory();

            for (const auto& entry : fs::directory_iterator(MUSIC_FOLDER)) {
                if (entry.is_regular_file()) {
                    std::wstring extension = entry.path().extension().wstring();
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                    if (extension == L".mp3" || extension == L".flac" || extension == L".wav") {
                        Song song = getSongInfo(entry.path().wstring());

                        // *** 关键修改: 在这里直接提取封面 ***  
                        for (int i = 0; i < song.embeddedCoverCount; ++i) {
                            std::wstring coverPath = MUI::extractCover(song.filePath, exeDir + L"/data/cover", i);
                            if (!coverPath.empty()) {
                                song.coverPaths.push_back(coverPath);
                            }
                        }

                        playerData.songLibrary.push_back(song);
                    }
                }
            }

            generateShuffleOrder();
        }
        catch (const fs::filesystem_error& e) {
            // 错误处理  
        }
    }

    /**
 * @brief 设置歌曲库并更新相关状态
 * @param songs 新的歌曲库
 */
    void MPlayer::setSongLibrary(const std::vector<Song>& songs) {
        // 停止当前播放
        if (soundInitialized) {
            stopAudio();
        }

        // 更新歌曲库
        playerData.songLibrary = songs;

        // 重置当前播放状态
        playerData.currentSongIndex = 0;
        playerData.currentPosition = 0.0f;
        playerData.currentDuration = 0.0f;

        // 如果新歌库不为空，更新当前歌曲的时长信息
        if (!playerData.songLibrary.empty()) {
            const Song* currentSong = getCurrentSong();
            if (currentSong) {
                playerData.currentDuration = currentSong->duration;
            }
        }

        // 重新生成随机播放顺序（如果需要）
        if (playerData.mode == PlayMode::Shuffle) {
            generateShuffleOrder();
        }
        else {
            // 清空随机播放顺序
            playerData.shuffleOrder.clear();
        }

        ODD(L"已设置歌曲库，共 %zu 首歌曲\n", playerData.songLibrary.size());
    }

    void MPlayer::playSongByIndex(int32_t index) {
        if (index >= 0 && index < static_cast<int32_t>(playerData.songLibrary.size())) {
            playerData.currentSongIndex = index;
            playAudio(playerData.songLibrary[index].filePath);
        }
    }

    void MPlayer::generateShuffleOrder() {
        playerData.shuffleOrder.clear();
        if (playerData.songLibrary.empty()) return;

        for (int32_t i = 0; i < static_cast<int32_t>(playerData.songLibrary.size()); ++i) {
            playerData.shuffleOrder.push_back(i);
        }

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(playerData.shuffleOrder.begin(), playerData.shuffleOrder.end(), g);

        if (playerData.mode == PlayMode::Shuffle) {
            auto it = std::find(playerData.shuffleOrder.begin(), playerData.shuffleOrder.end(), playerData.currentSongIndex);
            if (it != playerData.shuffleOrder.end()) {
                std::swap(playerData.shuffleOrder[0], *it);
            }
        }
    }

    int32_t MPlayer::getNextSongIndex() const {
        if (playerData.songLibrary.empty()) return -1;

        switch (playerData.mode) {
        case PlayMode::Normal:
            return (playerData.currentSongIndex + 1 < static_cast<int32_t>(playerData.songLibrary.size()))
                ? playerData.currentSongIndex + 1 : -1;

        case PlayMode::Repeat:
            return (playerData.currentSongIndex + 1) % static_cast<int32_t>(playerData.songLibrary.size());

        case PlayMode::Shuffle:
            if (playerData.shuffleOrder.empty()) return -1;
            auto it = std::find(playerData.shuffleOrder.begin(), playerData.shuffleOrder.end(), playerData.currentSongIndex);
            if (it == playerData.shuffleOrder.end()) return playerData.shuffleOrder[0];

            int32_t nextIndex = static_cast<int32_t>(std::distance(playerData.shuffleOrder.begin(), it)) + 1;
            return (nextIndex < static_cast<int32_t>(playerData.shuffleOrder.size()))
                ? playerData.shuffleOrder[nextIndex] : playerData.shuffleOrder[0];
        }
        return -1;
    }

    int32_t MPlayer::getPrevSongIndex() const {
        if (playerData.songLibrary.empty()) return -1;

        switch (playerData.mode) {
        case PlayMode::Normal:
            return (playerData.currentSongIndex > 0) ? playerData.currentSongIndex - 1 : -1;

        case PlayMode::Repeat:
            return (playerData.currentSongIndex - 1 + static_cast<int32_t>(playerData.songLibrary.size())) % static_cast<int32_t>(playerData.songLibrary.size());

        case PlayMode::Shuffle:
            if (playerData.shuffleOrder.empty()) return -1;
            auto it = std::find(playerData.shuffleOrder.begin(), playerData.shuffleOrder.end(), playerData.currentSongIndex);
            if (it == playerData.shuffleOrder.end()) return playerData.shuffleOrder[0];

            int32_t prevIndex = static_cast<int32_t>(std::distance(playerData.shuffleOrder.begin(), it)) - 1;
            return (prevIndex >= 0) ? playerData.shuffleOrder[prevIndex] : playerData.shuffleOrder.back();
        }
        return -1;
    }

    void MPlayer::preSong() {
        int32_t prevIndex = getPrevSongIndex();
        if (prevIndex >= 0) {
            playSongByIndex(prevIndex);
        }
        else {
            stopAudio();
        }
    }

    void MPlayer::nextSong() {
        int32_t nextIndex = getNextSongIndex();
        if (nextIndex >= 0) {
            playSongByIndex(nextIndex);
            if (playerData.mode == PlayMode::Shuffle && nextIndex == playerData.shuffleOrder[0]) {
                generateShuffleOrder();
            }
        }
        else {
            stopAudio();
        }
    }
}

// UIElement 类  
namespace MUI {

    class UIElement {
    public:
        Rect rect{ 0.0f, 0.0f, 100.0f, 30.0f };
        bool visible = true;
        bool enabled = true;

		std::string fontName = u8"siyuan.ttf";// 默认字体
        float fontSize = 12.0f;

        virtual ~UIElement() = default;

        // 渲染接口(即时模式)  
        virtual void render(tvg::Scene* parent) = 0;

        // 事件处理(简化命名)  
        virtual bool hitTest(float px, float py) {
            return rect.contains(px, py);
        }
        virtual void onMDown(float px, float py) {}
        virtual void onMUp(float px, float py) {}
        virtual void onMMove(float px, float py) {}
        virtual void onMWheel(float px, float py, int delta) {}
        virtual void onKDown(int keyCode) {}
        virtual void onKUp(int keyCode) {}
        virtual void onChar(wchar_t character) {}
        virtual void onFocus(bool focused) {}
        virtual void onSize(int w, int h) {}

        // 动画/定时器更新  
        virtual void update(float deltaTime) {}
    };

    // UIElement 实现(无需额外实现,全部为虚函数)  

} // namespace MUI  

// UIManager 类  
namespace MUI {

    class UIManager {
    private:
        void* glctx = nullptr;
        std::unique_ptr<tvg::GlCanvas> canvas;  // 只支持 GlCanvas  
        tvg::Scene* scene = nullptr;       
        std::vector<std::unique_ptr<UIElement>> elements;

        uint32_t width = 0;
        uint32_t height = 0;

        UIElement* hoveredElement = nullptr;
        UIElement* pressedElement = nullptr;
        UIElement* focusedElement = nullptr;

    public:
        UIManager();
        ~UIManager();

        bool init(void* glContext, int w, int h);
        void update(float deltaTime);
        void render();      
        void resize(int w, int h);
        void clear();

        void addElement(std::unique_ptr<UIElement> element);
        void removeElement(UIElement* element);
        void setFocus(UIElement* element);
        UIElement* getFocus() const { return focusedElement; }

        void handleMDown(int x, int y);
        void handleMUp(int x, int y);
        void handleMMove(int x, int y);
        void handleMWheel(int x, int y, int delta);
        void handleKDown(int keyCode);
        void handleKUp(int keyCode);
        void handleChar(wchar_t character);
        void handleSize(int w, int h);

        tvg::GlCanvas* getCanvas() const { return canvas.get(); }
    };

    // UIManager 实现  
    UIManager::UIManager() {}

    UIManager::~UIManager() {
        if (scene) scene->unref();
    }

    bool UIManager::init(void* glContext, int w, int h) {
        width = w;
        height = h;
        glctx = glContext;

        canvas = std::unique_ptr<tvg::GlCanvas>(tvg::GlCanvas::gen());
        if (!canvas) {
            ODD(L"GlCanvas 创建失败\n");
            return false;
        }

        if (canvas->target(glContext, 0, w, h, tvg::ColorSpace::ABGR8888S)
            != tvg::Result::Success) {
            ODD(L"GlCanvas target 设置失败\n");
            return false;
        }

        scene = tvg::Scene::gen();
        if (!scene) {
            ODD(L"Scene 创建失败\n");
            return false;
        }

        scene->ref();
        canvas->push(scene);

        ODD(L"UIManager 初始化成功: %dx%d\n", w, h);
        return true;
    }

    void UIManager::update(float deltaTime) {
        for (auto& element : elements) {
            if (element->visible) {
                element->update(deltaTime);
            }
        }
    }

    void UIManager::render() {
        if (!scene || !canvas) return;

        scene->remove(nullptr);

        for (auto& element : elements) {
            if (element->visible) {
                element->render(scene);
            }
        }

        canvas->update();
        canvas->draw(false);
        canvas->sync();
    }

    void UIManager::resize(int w, int h) {
        width = w;
        height = h;

        // ✅ 同步 ThorVG 画布尺寸
        if (canvas && glctx && w > 0 && h > 0) {
            auto r = canvas->target(glctx, 0, (uint32_t)w, (uint32_t)h, tvg::ColorSpace::ABGR8888S);
            if (r != tvg::Result::Success) {
                ODD(L"GlCanvas target 重设失败(resize): %d\n", (int)r);
            }
        }

        for (auto& element : elements) {
            element->onSize(w, h);
        }
    }

    void UIManager::clear() {
        elements.clear();
        hoveredElement = nullptr;
        pressedElement = nullptr;
        focusedElement = nullptr;
    }

    void UIManager::addElement(std::unique_ptr<UIElement> element) {
        elements.push_back(std::move(element));
    }

    void UIManager::removeElement(UIElement* element) {
        elements.erase(
            std::remove_if(elements.begin(), elements.end(),
                [element](const std::unique_ptr<UIElement>& e) {
                    return e.get() == element;
                }),
            elements.end()
        );
    }

    void UIManager::setFocus(UIElement* element) {
        if (focusedElement == element) return;

        if (focusedElement) {
            focusedElement->onFocus(false);
        }

        focusedElement = element;

        if (focusedElement) {
            focusedElement->onFocus(true);
        }
    }

    void UIManager::handleMDown(int x, int y) {
        UIElement* clickedElement = nullptr;

        for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
            if ((*it)->visible && (*it)->enabled && (*it)->hitTest((float)x, (float)y)) {
                clickedElement = it->get();
                pressedElement = clickedElement;
                clickedElement->onMDown((float)x, (float)y);
                break;
            }
        }

        setFocus(clickedElement);
    }

    void UIManager::handleMUp(int x, int y) {
        if (pressedElement) {
            pressedElement->onMUp((float)x, (float)y);
            pressedElement = nullptr;
        }
    }

    void UIManager::handleMMove(int x, int y) {
        UIElement* newHovered = nullptr;

        for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
            if ((*it)->visible && (*it)->enabled && (*it)->hitTest((float)x, (float)y)) {
                newHovered = it->get();
                break;
            }
        }

        if (newHovered != hoveredElement) {
            hoveredElement = newHovered;
        }

        for (auto& element : elements) {
            if (element->visible) {
                element->onMMove((float)x, (float)y);
            }
        }
    }

    void UIManager::handleMWheel(int x, int y, int delta) {
        for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
            if ((*it)->visible && (*it)->enabled && (*it)->hitTest((float)x, (float)y)) {
                (*it)->onMWheel((float)x, (float)y, delta);
                break;
            }
        }
    }

    void UIManager::handleKDown(int keyCode) {
        if (focusedElement) {
            focusedElement->onKDown(keyCode);
        }
    }

    void UIManager::handleKUp(int keyCode) {
        if (focusedElement) {
            focusedElement->onKUp(keyCode);
        }
    }

    void UIManager::handleChar(wchar_t character) {
        if (focusedElement) {
            focusedElement->onChar(character);
        }
    }

    void UIManager::handleSize(int w, int h) {
        width = w;
        height = h;

        // ✅ 同步 ThorVG 画布尺寸
        if (canvas && glctx && w > 0 && h > 0) {
            auto r = canvas->target(glctx, 0, (uint32_t)w, (uint32_t)h, tvg::ColorSpace::ABGR8888S);
            if (r != tvg::Result::Success) {
                ODD(L"GlCanvas target 重设失败(handleSize): %d\n", (int)r);
            }
        }
        for (auto& element : elements) {
            element->onSize(w, h);
        }
    }

} // namespace MUI  

// Renderer3D 类  
namespace MUI {

    class Renderer3D {
    private:
        int width = 800;
        int height = 600;
        glm::vec4 clearColor = glm::vec4(0.0f, 0.5f, 0.5f, 1.0f);

        GLuint program = 0;
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;

        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 model = glm::mat4(1.0f);

        glm::vec3 camPos = glm::vec3(0.0f, 0.0f, 10.0f);
        glm::vec3 camTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 camUp = glm::vec3(0.0f, 1.0f, 0.0f);

        float fov = 45.0f;
        float angle = 0.0f;

        static const char* vs_src;
        static const char* fs_src;

        static GLuint compileShader(GLenum type, const char* src);
        bool createShaders();
        void createCube();

    public:
        Renderer3D();
        ~Renderer3D();

        bool initialize(int width, int height);
        void resize(int width, int height);
        void update(float deltaTime);
        void render();
        void shutdown();

        void setClearColor(const glm::vec4& c) { clearColor = c; }
        void setCamera(const glm::vec3& pos, const glm::vec3& target, const glm::vec3& up);

        glm::mat4 getView() const { return view; }
        glm::mat4 getProjection() const { return projection; }
    };

    // Renderer3D 静态成员  
    const char* Renderer3D::vs_src = R"(  
#version 460 core  
layout(location = 0) in vec3 aPos;  
layout(location = 1) in vec3 aColor;  
uniform mat4 uMVP;  
out vec3 vColor;  
void main() {  
    vColor  = aColor;  
    gl_Position = uMVP * vec4(aPos, 1.0);  
}  
)";

    const char* Renderer3D::fs_src = R"(  
#version 460 core  
in vec3 vColor;  
out vec4 FragColor;  
void main() {  
    FragColor = vec4(vColor, 1.0);  
}  
)";

    Renderer3D::Renderer3D() {}

    Renderer3D::~Renderer3D() {
        shutdown();
    }

    bool Renderer3D::initialize(int w, int h) {
        width = w > 0 ? w : 800;
        height = h > 0 ? h : 600;

        if (!createShaders()) return false;
        createCube();

        projection = glm::perspective(glm::radians(fov),
            (float)width / (float)height,
            0.1f, 100.0f);
        view = glm::lookAt(camPos, camTarget, camUp);

        return true;
    }

    void Renderer3D::resize(int w, int h) {
        width = w;
        height = h;
        projection = glm::perspective(glm::radians(fov),
            (float)width / (float)height,
            0.1f, 100.0f);
    }

    void Renderer3D::update(float deltaTime) {
        angle += deltaTime * 45.0f;
        if (angle > 360.0f) angle -= 360.0f;
    }

    void Renderer3D::render() {
        if (!program || !vao) return;

        glUseProgram(program);

        model = glm::mat4(1.0f);
        model = glm::rotate(model, glm::radians(angle), glm::vec3(0.5f, 1.0f, 0.0f));

        glm::mat4 mvp = projection * view * model;
        GLint loc = glGetUniformLocation(program, "uMVP");
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void Renderer3D::shutdown() {
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (ebo) { glDeleteBuffers(1, &ebo); ebo = 0; }
        if (program) { glDeleteProgram(program); program = 0; }
    }

    void Renderer3D::setCamera(const glm::vec3& pos, const glm::vec3& target, const glm::vec3& up) {
        camPos = pos;
        camTarget = target;
        camUp = up;
        view = glm::lookAt(camPos, camTarget, camUp);
    }

    GLuint Renderer3D::compileShader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);

        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetShaderInfoLog(s, 512, nullptr, buf);
            ODD(L"Shader compile error: %s\n", buf);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    bool Renderer3D::createShaders() {
        GLuint vs = compileShader(GL_VERTEX_SHADER, vs_src);
        if (!vs) return false;

        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);
        if (!fs) {
            glDeleteShader(vs);
            return false;
        }

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char buf[512];
            glGetProgramInfoLog(program, 512, nullptr, buf);
            ODD(L"Shader link error: %s\n", buf);
            glDeleteProgram(program);
            program = 0;
            return false;
        }

        return true;
    }

    void Renderer3D::createCube() {
        struct V { float x, y, z; float r, g, b; };
        V verts[] = {
            {-1,-1,-1, 1,0,0}, {1,-1,-1, 0,1,0}, {1,1,-1, 0,0,1}, {-1,1,-1, 1,1,0},
            {-1,-1,1,  1,0,1}, {1,-1,1,  0,1,1}, {1,1,1,  1,1,1}, {-1,1,1,  0,0,0}
        };

        unsigned int idx[] = {
            0,1,2, 2,3,0,
            4,5,6, 6,7,4,
            0,4,7, 7,3,0,
            1,5,6, 6,2,1,
            3,2,6, 6,7,3,
            0,1,5, 5,4,0
        };

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(3 * sizeof(float)));

        glBindVertexArray(0);
    }

} // namespace MUI

// Application 类  
namespace MUI {

    class Application {
    private:
        HWND hwnd = nullptr;
        HDC hdc = nullptr;
        HGLRC hglrc = nullptr;

        uint32_t width = 800;
        uint32_t height = 600;
        bool running = false;

        std::unique_ptr<UIManager> uiManager;
        std::unique_ptr<Renderer3D> renderer3D;

        static Application* instance;

        static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        bool createWindow(const wchar_t* title);
        bool createGLContext();

    public:
        Application();
        ~Application();

        bool init(const wchar_t* title, int w, int h, int canvasType = 1);
        bool loadFontFromResource(int resourceId, const char* fontName);

        void run();
        void quit();

        HWND getHwnd() const { return hwnd; }
        UIManager* getUIManager() const { return uiManager.get(); }
        Renderer3D* getRenderer3D() const { return renderer3D.get(); }
    };

    Application* Application::instance = nullptr;

    Application::Application() {
        instance = this;
    }

    Application::~Application() {
        if (hglrc) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc);
        }
        if (hdc) ReleaseDC(hwnd, hdc);
        if (hwnd) DestroyWindow(hwnd);

        instance = nullptr;
    }

    bool Application::init(const wchar_t* title, int w, int h, int canvasType) {
        width = w;
        height = h;

        if (!createWindow(title)) return false;
        if (!createGLContext()) return false;

        auto threads = std::thread::hardware_concurrency();
        if (threads > 0) --threads;

        if (tvg::Initializer::init(threads) != tvg::Result::Success) {
            ODD(L"ThorVG 初始化失败\n");
            return false;
        }

        uiManager = std::make_unique<UIManager>();

        // ✅ 获取 OpenGL 上下文并传递  
        void* glContext = wglGetCurrentContext();
        if (!glContext) {
            ODD(L"无法获取 OpenGL 上下文\n");
            return false;
        }

        if (!uiManager->init(glContext, w, h)) {
            ODD(L"UIManager 初始化失败\n");
            return false;
        }

        if (renderer3D) {
            if (!renderer3D->initialize(w, h)) {
                ODD(L"Renderer3D 初始化失败\n");
                return false;
            }
        }

        ODD(L"Application 初始化成功\n");
        return true;
    }

    bool Application::loadFontFromResource(int resourceId, const char* fontName) {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        if (!hInst) {
            ODD(L"loadFontFromResource: GetModuleHandleW 返回 NULL\n");
            return false;
        }

        // ✅ 使用 RT_RCDATA 而不是 L"RCDATA"  
        HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCE(resourceId), RT_RCDATA);
        if (!hRes) {
            ODD(L"字体资源未找到: %d\n", resourceId);
            return false;
        }

        HGLOBAL hData = LoadResource(hInst, hRes);
        if (!hData) {
            ODD(L"LoadResource 失败: %d\n", resourceId);
            return false;
        }

        void* pData = LockResource(hData);
        DWORD size = SizeofResource(hInst, hRes);

        if (!pData || size == 0) {
            ODD(L"资源为空: %d\n", resourceId);
            return false;
        }

        // ✅ 使用 "font/ttf" 作为 MIME 类型,copy=true  
        auto result = tvg::Text::load(fontName,
            reinterpret_cast<const char*>(pData),
            static_cast<uint32_t>(size),
            "font/ttf",  // 不是 "ttf"  
            true);       // copy=true  

        std::wstring fontNameW = MUI::utf8ToWide(std::string(fontName ? fontName : ""));
        if (result == tvg::Result::Success) {
            ODD(L"字体加载成功: %ls (%u 字节)\n", fontNameW.c_str(), size);
            return true;
        }
        else {
            ODD(L"字体加载失败: %ls, 错误码: %d\n", fontNameW.c_str(), (int)result);
            return false;
        }
    }

    void Application::run() {
        running = true;

        auto lastTime = std::chrono::steady_clock::now();

        MSG msg = {};
        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (!running) break;

            // 计算 deltaTime  
            auto currentTime = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            // 更新  
            if (uiManager) uiManager->update(deltaTime);
            if (renderer3D) renderer3D->update(deltaTime);

            // 渲染  
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // 先渲染 3D  
            glEnable(GL_DEPTH_TEST);
            if (renderer3D) renderer3D->render();

            // 再渲染 2D UI  
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            if (uiManager) uiManager->render();

            SwapBuffers(hdc);

            Sleep(1);
        }

        tvg::Initializer::term();
    }

    void Application::quit() {
        running = false;
    }

    bool Application::createWindow(const wchar_t* title) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"MUIWindowClass";

        if (!RegisterClassExW(&wc)) {
            ODD(L"窗口类注册失败\n");
            return false;
        }

        // 计算需要的外部窗口尺寸以保证客户区为 width x height
        // 注意：CreateWindowEx 的 width/height 参数指定的是窗口总外框尺寸（包括标题栏/边框）
        //       如果直接使用客户区尺寸去创建窗口，则最终的客户区会比期望的小，导致
        //       OpenGL/ThorVG canvas 初始化尺寸与窗口消息中的鼠标坐标不一致（偏移问题）。
        //       使用 AdjustWindowRectEx 可以把期望的客户区尺寸转换为合适的窗口外框尺寸。
        // 计算需要的外部窗口尺寸以保证客户区为 width x height
        RECT rc = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        DWORD style = WS_OVERLAPPEDWINDOW;
        AdjustWindowRectEx(&rc, style, FALSE, 0);
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;

        hwnd = CreateWindowExW(
            0,
            L"MUIWindowClass",
            title,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            winW, winH,
            nullptr, nullptr,
            wc.hInstance,
            nullptr
        );

        if (!hwnd) {
            ODD(L"窗口创建失败\n");
            return false;
        }

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        return true;
    }

    bool Application::createGLContext() {
        hdc = GetDC(hwnd);
        if (!hdc) return false;

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;

        int pixelFormat = ChoosePixelFormat(hdc, &pfd);
        if (!pixelFormat) return false;

        if (!SetPixelFormat(hdc, pixelFormat, &pfd)) return false;

        hglrc = wglCreateContext(hdc);
        if (!hglrc) return false;

        if (!wglMakeCurrent(hdc, hglrc)) return false;

        // 加载 OpenGL 函数  
        if (!gladLoadGL()) {
            ODD(L"GLAD 初始化失败\n");
            return false;
        }

        // ✅ 初始化视口为当前客户区大小
        // ✅ 初始化视口为当前客户区大小（夹紧为 >=1）
        glViewport(0, 0, (GLsizei)std::max<uint32_t>(1, width),(GLsizei)std::max<uint32_t>(1, height));

        // ✅ 初始化视口为当前客户区大小（夹紧为 >=1）
        glViewport(0, 0, (GLsizei)std::max<uint32_t>(1, width), (GLsizei)std::max<uint32_t>(1, height));

        // 安全打印 OpenGL 版本（先转为窄字节，再转为宽字符串）
        const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if (ver && ver[0]) {
            std::wstring verW = MUI::utf8ToWide(std::string(ver));
            ODD(L"OpenGL 版本: %ls\n", verW.c_str());
        }
        else {
            ODD(L"OpenGL 版本: (unknown)\n");
        }
        return true;
    }

    LRESULT Application::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (instance) {
            return instance->WndProc(hwnd, msg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            // 防御：instance 可能为空（理论极少见）
            if (!instance) return 0;

            instance->width = (uint32_t)w;
            instance->height = (uint32_t)h;

            // 同步 OpenGL 视口（仅在尺寸有效且当前有上下文时）
            if (w > 0 && h > 0 && instance->hglrc && wglGetCurrentContext()) {
                if (w > 0 && h > 0) {
                    glViewport(0, 0, w, h);
                }
            }


            // 3D 渲染器
            if (instance->renderer3D) {
                instance->renderer3D->resize(w, h);
            }

            // 2D UI（ThorVG）
            // 交由 UIManager 统一处理：内部已判空并安全重设 GlCanvas target
            if (instance->uiManager) {
                instance->uiManager->handleSize(w, h);
            }

            // 如果你更偏好在 Application 层直接重设 GlCanvas target（可选），
    // 可启用下面几行，但请避免与 handleSize 重复调用：
    /*
    if (instance->uiManager) {
        if (auto canvas = instance->uiManager->getCanvas()) {
            // 注意：target 的第一个参数是 GL 上下文指针；这里传 hglrc
            canvas->target(instance->hglrc, 0, (uint32_t)w, (uint32_t)h, tvg::ColorSpace::ABGR8888S);
        }
    }
    */

            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (uiManager) uiManager->handleMMove(x, y);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (uiManager) uiManager->handleMDown(x, y);
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (uiManager) uiManager->handleMUp(x, y);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (uiManager) uiManager->handleMWheel(pt.x, pt.y, delta);
            return 0;
        }

        case WM_KEYDOWN: {
            if (uiManager) uiManager->handleKDown((int)wParam);
            return 0;
        }

        case WM_KEYUP: {
            if (uiManager) uiManager->handleKUp((int)wParam);
            return 0;
        }

        case WM_CHAR: {
            if (uiManager) uiManager->handleChar((wchar_t)wParam);
            return 0;
        }

        case WM_DESTROY:
            running = false;
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

} // namespace MUI  

// UIButton 类  
namespace MUI {

    class UIButton : public UIElement {
    private:
        std::string label;
        Color bgColor = Color(70, 130, 180);
        Color hoverColor = Color(100, 160, 210);
        Color pressColor = Color(50, 110, 160);
        bool isHovered = false;
        bool isPressed = false;

    public:
        std::function<void()> onClick;

        UIButton(const char* text = "") : label(text ? text : "") {
            rect.h = 40.0f;
        }

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            // ✅ 创建背景  
            auto bgShape = tvg::Shape::gen();
            bgShape->appendRect(rect.x, rect.y, rect.w, rect.h, 5, 5);
            Color c = isPressed ? pressColor : (isHovered ? hoverColor : bgColor);
            bgShape->fill(c.r, c.g, c.b, c.a);
            parent->push(std::move(bgShape));

            // ✅ 创建文本  
            if (!label.empty()) {
                auto labelText = tvg::Text::gen();
                labelText->font(fontName.c_str());
                labelText->size(fontSize);
                labelText->text(label.c_str());
                labelText->layout(rect.w, rect.h);
                labelText->align(0.5f, 0.5f);
                labelText->wrap(tvg::TextWrap::Ellipsis);
                labelText->fill(255, 255, 255);
                labelText->translate(rect.x, rect.y);
                parent->push(std::move(labelText));
            }
        }

        void onMDown(float px, float py) override {
            isPressed = true;
        }

        void onMUp(float px, float py) override {
            if (isPressed && hitTest(px, py)) {
                if (onClick) onClick();
            }
            isPressed = false;
        }

        void onMMove(float px, float py) override {
            isHovered = hitTest(px, py);
        }

        void setLabel(const char* text) {
            label = text ? text : "";
        }

        const std::string& getLabel() const {
            return label;
        }
    };

} // namespace MUI

// UITextInput 类
namespace MUI {

    class UITextInput : public UIElement {
    private:
        std::string text;
        std::vector<tvg::Text::TextGlyphInfo> glyphInfo;
        std::vector<float> caretPositions;

        Color bgColor = Color(255, 255, 255);
        Color textColor = Color(0, 0, 0);
        Color borderColor = Color(200, 200, 200);
        Color focusBorderColor = Color(70, 130, 180);

        bool hasFocus = false;
        size_t caretIndex = 0;
        float scrollX = 0.0f;
        float textWidth = 0.0f;

        float paddingL = 8.0f;
        float paddingR = 8.0f;
        float fontSize = 14.0f;

        Timer cursorBlinkTimer;
        bool isCursorVisible = true;
        bool layoutDirty = true;

    public:
        std::function<void(const std::string&)> onTextChanged;

        UITextInput() {
            rect.w = 200.0f;
            rect.h = 30.0f;

            cursorBlinkTimer.setInterval(0.5f);
            cursorBlinkTimer.setCallback([this]() {
                isCursorVisible = !isCursorVisible;
                });
        }

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            if (layoutDirty) {
                updateLayout();
                layoutDirty = false;
            }

            // 背景  
            auto bgShape = tvg::Shape::gen();
            bgShape->appendRect(rect.x, rect.y, rect.w, rect.h, 3, 3);
            bgShape->fill(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            parent->push(std::move(bgShape));

            // 边框  
            auto borderShape = tvg::Shape::gen();
            borderShape->appendRect(rect.x, rect.y, rect.w, rect.h, 3, 3);
            Color& bc = hasFocus ? focusBorderColor : borderColor;
            borderShape->strokeFill(bc.r, bc.g, bc.b, bc.a);
            borderShape->strokeWidth(hasFocus ? 2.0f : 1.0f);
            parent->push(std::move(borderShape));

            // 文本  
            if (!text.empty()) {
                auto textObj = tvg::Text::gen();
                textObj->font("siyuan.ttf");
                textObj->text(text.c_str());
                textObj->size(fontSize);
                textObj->fill(textColor.r, textColor.g, textColor.b);

                float textX = rect.x + paddingL - scrollX;
                float textY = rect.y + rect.h * 0.5f;
                textObj->translate(textX, textY);
                textObj->align(0.0f, 0.5f);

                parent->push(std::move(textObj));
            }

            // 光标  
            if (hasFocus && isCursorVisible) {
                float caretX = getCaretVisualX();
                float caretY1 = rect.y + 5.0f;
                float caretY2 = rect.y + rect.h - 5.0f;

                auto cursorShape = tvg::Shape::gen();
                cursorShape->moveTo(caretX, caretY1);
                cursorShape->lineTo(caretX, caretY2);
                cursorShape->strokeFill(textColor.r, textColor.g, textColor.b);
                cursorShape->strokeWidth(1.5f);
                parent->push(std::move(cursorShape));
            }
        }

        void update(float deltaTime) override {
            if (hasFocus) {
                cursorBlinkTimer.update(deltaTime);
            }
        }

        void onMDown(float px, float py) override {
            float localX = px - (rect.x + paddingL) + scrollX;
            caretIndex = getCaretIndexFromX(localX);

            isCursorVisible = true;
            cursorBlinkTimer.reset();
            ensureCaretVisible();
        }

        void onKDown(int keyCode) override {
            bool textChanged = false;

            switch (keyCode) {
            case VK_LEFT:
                if (caretIndex > 0) {
                    caretIndex--;
                    ensureCaretVisible();
                }
                break;

            case VK_RIGHT:
                if (caretIndex < caretPositions.size() - 1) {
                    caretIndex++;
                    ensureCaretVisible();
                }
                break;

            case VK_HOME:
                caretIndex = 0;
                ensureCaretVisible();
                break;

            case VK_END:
                caretIndex = caretPositions.size() - 1;
                ensureCaretVisible();
                break;

            case VK_BACK:
                if (caretIndex > 0 && !text.empty()) {
                    size_t byteIndex = UTF8::charToByteIndex(text, caretIndex - 1);
                    size_t charLen = UTF8::charLength(text.c_str() + byteIndex);
                    text.erase(byteIndex, charLen);
                    caretIndex--;
                    textChanged = true;
                }
                break;

            case VK_DELETE:
                if (caretIndex < caretPositions.size() - 1 && !text.empty()) {
                    size_t byteIndex = UTF8::charToByteIndex(text, caretIndex);
                    size_t charLen = UTF8::charLength(text.c_str() + byteIndex);
                    text.erase(byteIndex, charLen);
                    textChanged = true;
                }
                break;
            }

            if (textChanged) {
                layoutDirty = true;
                if (onTextChanged) onTextChanged(text);
            }

            isCursorVisible = true;
            cursorBlinkTimer.reset();
        }

        void onChar(wchar_t character) override {
            if (character < 32 || character == 127) return;

            std::wstring wstr(1, character);
            std::string utf8 = wideToUtf8(wstr);

            size_t byteIndex = UTF8::charToByteIndex(text, caretIndex);
            text.insert(byteIndex, utf8);
            caretIndex++;

            layoutDirty = true;
            if (onTextChanged) onTextChanged(text);

            isCursorVisible = true;
            cursorBlinkTimer.reset();
            ensureCaretVisible();
        }

        void onFocus(bool focused) override {
            hasFocus = focused;
            if (focused) {
                cursorBlinkTimer.start();
                isCursorVisible = true;
            }
            else {
                cursorBlinkTimer.stop();
            }
        }

        void setText(const std::string& str) {
            text = str;
            caretIndex = UTF8::charCount(text);
            layoutDirty = true;
            ensureCaretVisible();
        }

        const std::string& getText() const {
            return text;
        }

private:
    void updateLayout() {
        if (text.empty()) {
            glyphInfo.clear();
            caretPositions.clear();
            caretPositions.push_back(0.0f);
            textWidth = 0.0f;
            return;
        }

        auto tempText = tvg::Text::gen();
        tempText->font(fontName.c_str());
        tempText->text(text.c_str());
        tempText->size(fontSize);

        auto result = tempText->getGlyphInfo(glyphInfo);
        if (result == tvg::Result::Success && !glyphInfo.empty()) {
            caretPositions.clear();
            caretPositions.push_back(0.0f);

            for (const auto& glyph : glyphInfo) {
                float nextPos = caretPositions.back() + glyph.advance;
                caretPositions.push_back(nextPos);
            }

            textWidth = caretPositions.back();
        }
        else {
            const float FIXED_CHAR_WIDTH = 8.0f;
            size_t charCount = UTF8::charCount(text);

            caretPositions.clear();
            for (size_t i = 0; i <= charCount; ++i) {
                caretPositions.push_back(i * FIXED_CHAR_WIDTH);
            }
            textWidth = charCount * FIXED_CHAR_WIDTH;
        }
    }

    void ensureCaretVisible() {
        if (caretPositions.empty()) return;

        float caretX = caretPositions[std::min(caretIndex, caretPositions.size() - 1)];
        float visibleWidth = rect.w - paddingL - paddingR;

        if (caretX < scrollX) {
            scrollX = caretX;
        }
        else if (caretX > scrollX + visibleWidth) {
            scrollX = caretX - visibleWidth;
        }

        scrollX = std::max(0.0f, scrollX);
        if (textWidth <= visibleWidth) {
            scrollX = 0.0f;
        }
    }

    float getCaretVisualX() const {
        if (caretPositions.empty()) return rect.x + paddingL;

        float caretX = caretPositions[std::min(caretIndex, caretPositions.size() - 1)];
        return rect.x + paddingL + caretX - scrollX;
    }

    size_t getCaretIndexFromX(float localX) const {
        if (caretPositions.empty()) return 0;

        size_t bestIndex = 0;
        float minDistance = std::abs(localX - caretPositions[0]);

        for (size_t i = 1; i < caretPositions.size(); ++i) {
            float distance = std::abs(localX - caretPositions[i]);
            if (distance < minDistance) {
                minDistance = distance;
                bestIndex = i;
            }
        }

        return bestIndex;
    }
};

} // namespace MUI

// UILabel - 纯文本标签
namespace MUI {

    class UILabel : public UIElement {
    private:
        std::string text;
        Color textColor = Color(40, 40, 40, 255);
        float hAlign = 0.0f;   // 0..1 : 左中右
        float vAlign = 0.5f;   // 0..1 : 上中下
        bool hovered = false;

    public:
        // 进入/离开悬停的回调，参数为(hovering, px, py)
        std::function<void(bool, float, float)> onHoverChanged;

        UILabel(const char* txt = "") : text(txt ? txt : "") {
            rect.w = 120.0f;
            rect.h = 24.0f;
            fontSize = 14.0f;
        }

        void setText(const char* txt) { text = txt ? txt : ""; }
        const std::string& getText() const { return text; }

        void setTextColor(Color c) { textColor = c; }
        void setAlign(float h, float v) {
            hAlign = std::clamp(h, 0.0f, 1.0f);
            vAlign = std::clamp(v, 0.0f, 1.0f);
        }

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            if (!text.empty()) {
                auto t = tvg::Text::gen();
                t->font(fontName.c_str());
                t->size(fontSize);
                t->text(text.c_str());
                t->layout(rect.w, rect.h);
                t->wrap(tvg::TextWrap::Ellipsis);
                t->align(hAlign, vAlign);
                t->fill(textColor.r, textColor.g, textColor.b);
                t->translate(rect.x, rect.y);
                parent->push(std::move(t));
            }
        }

        void onMMove(float px, float py) override {
            bool now = hitTest(px, py);
            if (now != hovered) {
                hovered = now;
                if (onHoverChanged) onHoverChanged(hovered, px, py);
            }
        }
    };

} // namespace MUI

// UITooltip - 简单提示气泡（非交互）
namespace MUI {

    class UITooltip : public UIElement {
    private:
        std::string text;
        Color bgColor = Color(30, 30, 30, 230);
        Color borderColor = Color(0, 0, 0, 60);
        Color textColor = Color(255, 255, 255, 255);
        float paddingX = 8.0f;
        float paddingY = 6.0f;
        float corner = 4.0f;

        // 依据当前字体测量单行宽度
        float measureTextWidth(const std::string& s) const {
            if (s.empty()) return 0.0f;
            std::vector<tvg::Text::TextGlyphInfo> glyphs;
            auto tmp = tvg::Text::gen();
            tmp->font(fontName.c_str());
            tmp->size(fontSize);
            tmp->text(s.c_str());
            if (tmp->getGlyphInfo(glyphs) == tvg::Result::Success && !glyphs.empty()) {
                float w = 0.0f;
                for (auto& g : glyphs) w += g.advance;
                return w;
            }
            // 退化估算
            return std::max(10.0f, (float)UTF8::charCount(s) * (fontSize * 0.6f));
        }

        void updateSizeByText() {
            float w = measureTextWidth(text);
            float h = fontSize + 6.0f;
            rect.w = std::max(24.0f, w + paddingX * 2.0f);
            rect.h = std::max(16.0f, h + paddingY * 2.0f);
        }

    public:
        UITooltip() {
            fontSize = 12.0f;
            visible = false; // 默认隐藏
            rect.w = 80.0f;
            rect.h = 28.0f;
        }

        // 显示到指定屏幕坐标（左上角）
        void showAt(float x, float y) {
            rect.x = x;
            rect.y = y;
            visible = true;
        }

        void hide() { visible = false; }

        void setText(const char* s) {
            text = s ? s : "";
            updateSizeByText();
        }

        void setColors(Color bg, Color txt, Color border) {
            bgColor = bg; textColor = txt; borderColor = border;
        }

        bool hitTest(float, float) override { return false; } // 非交互，穿透

        void render(tvg::Scene* parent) override {
            if (!visible || text.empty()) return;

            // 背景
            auto bg = tvg::Shape::gen();
            bg->appendRect(rect.x, rect.y, rect.w, rect.h, corner, corner);
            bg->fill(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            // 边
            bg->strokeFill(borderColor.r, borderColor.g, borderColor.b, borderColor.a);
            bg->strokeWidth(1.0f);
            parent->push(std::move(bg));

            // 文本
            auto t = tvg::Text::gen();
            t->font(fontName.c_str());
            t->size(fontSize);
            t->text(text.c_str());
            t->wrap(tvg::TextWrap::Ellipsis);
            t->layout(rect.w - paddingX * 2.0f, rect.h - paddingY * 2.0f);
            t->align(0.0f, 0.5f);
            t->fill(textColor.r, textColor.g, textColor.b);
            t->translate(rect.x + paddingX, rect.y + rect.h * 0.5f);
            parent->push(std::move(t));
        }
    };

} // namespace MUI

// UISlider - 水平滑块
namespace MUI {

    class UISlider : public UIElement {
    private:
        float minV = 0.0f;
        float maxV = 100.0f;
        float value = 50.0f;

        float padding = 10.0f;  // 两端内边距
        float trackH = 4.0f;    // 轨道高度
        float thumbR = 8.0f;    // 拇指半径

        Color trackBg = Color(200, 200, 200, 180);
        Color trackFill = Color(70, 130, 180, 220);
        Color thumbColor = Color(255, 255, 255, 255);
        Color thumbBorder = Color(0, 0, 0, 50);

        bool dragging = false;

        float clampValue(float v) const { return std::clamp(v, minV, maxV); }
        float frac() const {
            if (maxV <= minV) return 0.0f;
            return (value - minV) / (maxV - minV);
        }

        float usableWidth() const { return std::max(0.0f, rect.w - padding * 2.0f); }

        float valueFromPos(float px) const {
            float fx = (px - (rect.x + padding)) / std::max(1.0f, usableWidth());
            return clampValue(minV + std::clamp(fx, 0.0f, 1.0f) * (maxV - minV));
        }

    public:
        std::function<void(float)> onValueChanged;

        UISlider() {
            rect.w = 200.0f;
            rect.h = 30.0f;
        }

        void setRange(float minVal, float maxVal) {
            minV = minVal; maxV = maxVal;
            setValue(value); // 重新夹紧
        }

        void setValue(float v) {
            float nv = clampValue(v);
            if (std::abs(nv - value) > 1e-6f) {
                value = nv;
                if (onValueChanged) onValueChanged(value);
            }
        }

        float getValue() const { return value; }

        // 便于外部在演示中查询拇指坐标
        void getThumbCenter(float& cx, float& cy) const {
            cx = rect.x + padding + frac() * usableWidth();
            cy = rect.y + rect.h * 0.5f;
        }

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            float cx, cy;
            getThumbCenter(cx, cy);

            // 轨道背景
            float ty = rect.y + rect.h * 0.5f - trackH * 0.5f;
            auto track = tvg::Shape::gen();
            track->appendRect(rect.x + padding, ty, usableWidth(), trackH, trackH * 0.5f, trackH * 0.5f);
            track->fill(trackBg.r, trackBg.g, trackBg.b, trackBg.a);
            parent->push(std::move(track));

            // 轨道填充
            auto fill = tvg::Shape::gen();
            float fillW = frac() * usableWidth();
            fill->appendRect(rect.x + padding, ty, fillW, trackH, trackH * 0.5f, trackH * 0.5f);
            fill->fill(trackFill.r, trackFill.g, trackFill.b, trackFill.a);
            parent->push(std::move(fill));

            // 拇指
            auto thumb = tvg::Shape::gen();
            thumb->appendCircle(cx, cy, thumbR, thumbR);
            thumb->fill(thumbColor.r, thumbColor.g, thumbColor.b, thumbColor.a);
            thumb->strokeFill(thumbBorder.r, thumbBorder.g, thumbBorder.b, thumbBorder.a);
            thumb->strokeWidth(1.0f);
            parent->push(std::move(thumb));
        }

        void onMDown(float px, float py) override {
            if (!hitTest(px, py)) return; // 只在控件内响应
            // 无论点轨道还是拇指，都直接跳转并进入拖动
            setValue(valueFromPos(px));
            dragging = true;
        }

        void onMMove(float px, float py) override {
            if (!visible) return;
            if (dragging) {
                setValue(valueFromPos(px));
            }
        }

        void onMUp(float, float) override {
            dragging = false;
        }
    };

} // namespace MUI

// UIProgressBar - 进度条
namespace MUI {

    class UIProgressBar : public UIElement {
    private:
        float minV = 0.0f;
        float maxV = 100.0f;
        float value = 0.0f;

        Color bg = Color(230, 230, 230, 255);
        Color fg = Color(70, 130, 180, 220);
        Color border = Color(0, 0, 0, 30);
        float corner = 3.0f;

        float frac() const {
            if (maxV <= minV) return 0.0f;
            return std::clamp((value - minV) / (maxV - minV), 0.0f, 1.0f);
        }

    public:
        UIProgressBar() {
            rect.w = 200.0f;
            rect.h = 10.0f;
        }

        void setRange(float minVal, float maxVal) {
            minV = minVal; maxV = maxVal;
            setValue(value);
        }

        void setValue(float v) {
            value = std::clamp(v, minV, maxV);
        }

        float getValue() const { return value; }

        void setColors(Color background, Color foreground, Color borderColor) {
            bg = background; fg = foreground; border = borderColor;
        }

        bool hitTest(float, float) override { return false; } // 非交互

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            // 背景
            auto b = tvg::Shape::gen();
            b->appendRect(rect.x, rect.y, rect.w, rect.h, corner, corner);
            b->fill(bg.r, bg.g, bg.b, bg.a);
            b->strokeFill(border.r, border.g, border.b, border.a);
            b->strokeWidth(1.0f);
            parent->push(std::move(b));

            // 前景填充
            float fw = std::max(0.0f, frac() * rect.w);
            if (fw > 0.0f) {
                auto f = tvg::Shape::gen();
                f->appendRect(rect.x, rect.y, fw, rect.h, corner, corner);
                f->fill(fg.r, fg.g, fg.b, fg.a);
                parent->push(std::move(f));
            }
        }
    };

} // namespace MUI

// PlayList 控件 - 带缓存的完整实现  
namespace MUI {

    
    // PlayListItem - 播放列表条目配置  
    
    struct PlayListItem {
        const Song* song = nullptr;

        struct Style {
            Color strokeColor = Color(200, 200, 200, 255);
            Color fillColor = Color(235, 235, 235, 255);
            Color hoverColor = Color(220, 230, 240, 255);
            Color selectedColor = Color(70, 130, 180, 200);
            float strokeWidth = 1.0f;
            bool enableStroke = false;
            bool enableFill = true;
            float itemW = 290.0f;
            float itemH = 50.0f;
        } style;

        struct Layout {
            struct { float x = 12, y = 9, w = 32, h = 32; } cover;
            struct { float x = 52, y = 9, w = 186, h = 16; float fontSize = 14; } title;
            struct { float x = 52, y = 30, w = 186, h = 12; float fontSize = 10; } artist;
            struct { float x = 248, y = 9, w = 32, h = 32; } favorite;
        } layout;

        bool isHovered = false;
        bool isSelected = false;
        bool isFavorite = false;

        PlayListItem(const Song* s = nullptr) : song(s) {}
    };

    
    // PlayList - 带缓存的播放列表容器  
    
    class PlayList : public UIElement {
    private:
        std::vector<Song> songs;
        std::vector<PlayListItem> items;

        // 简化的滚动状态  
        int firstVisibleIndex = 0;
        const int VISIBLE_COUNT = 8;

        // 布局参数  
        float leftPadding = 10.0f;
        float topPadding = 18.0f;
        float vspacing = 12.0f;

        int selectedIndex = -1;
        int hoveredIndex = -1;

        // *** 缓存机制 ***  
        // 封面缓存: 文件路径 -> OBitmap  
        std::map<std::wstring, OBitmap> thumbnailCache;

        // 容器样式  
        struct ContainerStyle {
            Color fillColor = Color(245, 245, 245, 255);
            Color borderColor = Color(200, 200, 200, 255);
            float opacity = 1.0f;
            float cornerRadius = 0.0f;
        } containerStyle;

    public:
        std::string fontName = "siyuan.ttf";
        std::function<void(int)> onSelect;
        std::function<void(int)> onFavoriteToggle;

        PlayList() {
            rect = { 950, 80, 310, 520 };
        }

        ~PlayList() noexcept {
            for (auto& pair : thumbnailCache) {
                freeBitmap(pair.second);
            }
        }

        void ensureVisible(int index) {
            if (index < 0 || index >= static_cast<int>(items.size())) return;

            // 如果索引在当前可见范围内,无需滚动  
            if (index >= firstVisibleIndex && index < firstVisibleIndex + VISIBLE_COUNT) {
                return;
            }

            // 如果索引在可见范围之前,滚动到该索引  
            if (index < firstVisibleIndex) {
                firstVisibleIndex = index;
            }
            // 如果索引在可见范围之后,滚动使其成为最后一个可见项  
            else {
                firstVisibleIndex = index - VISIBLE_COUNT + 1;
            }

            // 确保边界正确  
            int maxFirstIndex = std::max(0, static_cast<int>(items.size()) - VISIBLE_COUNT);
            firstVisibleIndex = std::clamp(firstVisibleIndex, 0, maxFirstIndex);
        }

        // ========================================================================  
        // 数据管理  
        // ========================================================================  

        void setSongs(const std::vector<Song>& s) {
            songs = s;
            items.clear();
            for (const auto& song : songs) {
                items.emplace_back(&song);
            }
            firstVisibleIndex = 0;
        }

        void setItems(const std::vector<Song>& s) {
            setSongs(s);
        }

        const std::vector<Song>& getSongs() const {
            return songs;
        }

        void setSelectedIndex(int index) {
            if (index >= 0 && index < static_cast<int>(items.size())) {
                selectedIndex = index;
                ensureVisible(index);
            }
        }

        int getSelectedIndex() const {
            return selectedIndex;
        }

        void setFavorite(int index, bool favorite) {
            if (index >= 0 && index < static_cast<int>(items.size())) {
                items[index].isFavorite = favorite;
            }
        }

        void setPadding(float left, float top) {
            leftPadding = left;
            topPadding = top;
        }

        void setSpacing(float vertical) {
            vspacing = vertical;
        }

        void setContainerOpacity(float opacity) {
            containerStyle.opacity = std::clamp(opacity, 0.0f, 1.0f);
        }

        void setContainerFillColor(const Color& color) {
            containerStyle.fillColor = color;
        }

        // ========================================================================  
        // 缓存管理  
        // ========================================================================  

        void preloadVisibleCovers() {
            int lastVisibleIndex = std::min(firstVisibleIndex + VISIBLE_COUNT,
                static_cast<int>(items.size()));

            for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
                const auto* song = items[i].song;
                if (!song || song->coverPaths.empty()) continue;

                const auto& coverPath = song->coverPaths[0];

                // 如果已缓存,跳过  
                if (thumbnailCache.count(coverPath)) continue;

                // 加载图片到 OBitmap  
                OBitmap bitmap = loadImageToBitmap(coverPath);
                if (bitmap.data) {
                    thumbnailCache[coverPath] = bitmap;
                }
            }
        }

        void clearCache() {
            for (auto& pair : thumbnailCache) {
                freeBitmap(pair.second);
            }
            thumbnailCache.clear();
        }

        // ========================================================================  
        // 渲染  
        // ========================================================================  

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            // 预加载可见范围的封面  
            preloadVisibleCovers();

            // 渲染容器背景  
            renderContainer(parent);

            // 计算可见范围  
            int lastVisibleIndex = std::min(firstVisibleIndex + VISIBLE_COUNT,
                static_cast<int>(items.size()));

            float contentX = rect.x + leftPadding;
            float contentY = rect.y + topPadding;

            // 渲染可见条目  
            for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
                int relativeIndex = i - firstVisibleIndex;
                float itemY = contentY + relativeIndex * (items[i].style.itemH + vspacing);

                renderItem(parent, i, contentX, itemY);
            }

            // 渲染滚动条  
            renderScrollbar(parent);
        }

    private:
        void renderContainer(tvg::Scene* parent) {
            auto bg = tvg::Shape::gen();
            bg->appendRect(rect.x, rect.y, rect.w, rect.h,
                containerStyle.cornerRadius, containerStyle.cornerRadius);
            bg->fill(containerStyle.fillColor.r, containerStyle.fillColor.g,
                containerStyle.fillColor.b, containerStyle.fillColor.a);
            bg->opacity(static_cast<uint8_t>(containerStyle.opacity * 255));
            parent->push(std::move(bg));
        }

        void renderItem(tvg::Scene* parent, int index, float x, float y) {
            const auto& item = items[index];

            // 渲染背景  
            renderItemBackground(parent, item, index, x, y);

            // 渲染封面  
            const auto& layout = item.layout;
            renderCover(parent, item, x + layout.cover.x, y + layout.cover.y,
                layout.cover.w, layout.cover.h);

            // 渲染标题  
            if (item.song) {
                renderTitle(parent, item, x + layout.title.x, y + layout.title.y,
                    layout.title.w, layout.title.h, layout.title.fontSize);
            }

            // 渲染艺术家  
            if (item.song) {
                renderArtist(parent, item, x + layout.artist.x, y + layout.artist.y,
                    layout.artist.w, layout.artist.h, layout.artist.fontSize);
            }

            // 渲染收藏按钮  
            renderFavorite(parent, item, x + layout.favorite.x, y + layout.favorite.y,
                layout.favorite.w, layout.favorite.h);
        }

        void renderItemBackground(tvg::Scene* parent, const PlayListItem& item,
            int index, float x, float y) {
            auto bg = tvg::Shape::gen();
            bg->appendRect(x, y, item.style.itemW, item.style.itemH, 3, 3);

            Color bgColor = item.style.fillColor;
            if (index == selectedIndex) {
                bgColor = item.style.selectedColor;
            }
            else if (index == hoveredIndex) {
                bgColor = item.style.hoverColor;
            }

            bg->fill(bgColor.r, bgColor.g, bgColor.b, bgColor.a);

            if (item.style.enableStroke) {
                bg->strokeFill(item.style.strokeColor.r, item.style.strokeColor.g,
                    item.style.strokeColor.b, item.style.strokeColor.a);
                bg->strokeWidth(item.style.strokeWidth);
            }

            parent->push(std::move(bg));
        }

        void renderCover(tvg::Scene* parent, const PlayListItem& item,
            float x, float y, float w, float h) {
            if (!item.song || item.song->coverPaths.empty()) {
                renderPlaceholder(parent, x, y, w, h);
                return;
            }

            const auto& coverPath = item.song->coverPaths[0];
            auto it = thumbnailCache.find(coverPath);

            if (it != thumbnailCache.end() && it->second.data) {
                const auto& bitmap = it->second;

                // 使用 ThorVG 从内存加载  
                auto pic = tvg::Picture::gen();
                auto result = pic->load(
                    reinterpret_cast<const uint32_t*>(bitmap.data),
                    bitmap.width,
                    bitmap.height,
                    tvg::ColorSpace::ABGR8888S,
                    false
                );

                if (result == tvg::Result::Success) {
                    pic->size(w, h);
                    pic->translate(x, y);
                    parent->push(pic);
                    return;
                }
            }

            renderPlaceholder(parent, x, y, w, h);
        }

        void renderPlaceholder(tvg::Scene* parent, float x, float y, float w, float h) {
            auto placeholder = tvg::Shape::gen();
            placeholder->appendRect(x, y, w, h, 2, 2);
            placeholder->fill(200, 200, 200, 255);
            parent->push(std::move(placeholder));
        }

        void renderTitle(tvg::Scene* parent, const PlayListItem& item,
            float x, float y, float w, float h, float fontSize) {
            if (!item.song) return;

            std::string title = wideToUtf8(item.song->title);

            auto text = tvg::Text::gen();
            text->font(fontName.c_str());
            text->size(fontSize);
            text->text(title.c_str());
            text->wrap(tvg::TextWrap::Ellipsis);
            text->layout(w, h);

            Color textColor = (selectedIndex == hoveredIndex) ?
                Color(255, 255, 255, 255) : Color(40, 40, 40, 255);
            text->fill(textColor.r, textColor.g, textColor.b);
            text->align(0.0f, 0.0f);
            text->translate(x, y);
            parent->push(std::move(text));
        }

        void renderArtist(tvg::Scene* parent, const PlayListItem& item,
            float x, float y, float w, float h, float fontSize) {
            if (!item.song) return;

            std::string artist = wideToUtf8(item.song->artist);

            auto text = tvg::Text::gen();
            text->font(fontName.c_str());
            text->size(fontSize);
            text->text(artist.c_str());
            text->wrap(tvg::TextWrap::Ellipsis);
            text->layout(w, h);
            text->fill(120, 120, 120);
            text->align(0.0f, 0.0f);
            text->translate(x, y);
            parent->push(std::move(text));
        }

        void renderFavorite(tvg::Scene* parent, const PlayListItem& item,
            float x, float y, float w, float h) {
            auto heart = tvg::Shape::gen();
            heart->appendRect(x, y, w, h, 2, 2);

            if (item.isFavorite) {
                heart->fill(255, 100, 100, 255);
            }
            else {
                heart->fill(200, 200, 200, 255);
            }

            parent->push(std::move(heart));
        }

        void renderScrollbar(tvg::Scene* parent) {
            if (items.size() <= VISIBLE_COUNT) return;

            float trackX = rect.x + rect.w - 8.0f;
            float trackW = 6.0f;
            float trackY = rect.y + 4.0f;
            float trackH = rect.h - 8.0f;

            float thumbH = std::max(20.0f, trackH * VISIBLE_COUNT / items.size());
            float thumbSpan = trackH - thumbH;

            int maxFirstIndex = items.size() - VISIBLE_COUNT;
            float thumbY = trackY + (firstVisibleIndex / (float)maxFirstIndex) * thumbSpan;

            auto scrollbar = tvg::Shape::gen();
            scrollbar->appendRect(trackX, thumbY, trackW, thumbH, 3, 3);
            scrollbar->fill(100, 100, 100, 200);
            parent->push(std::move(scrollbar));
        }

        // ========================================================================  
        // 交互处理  
        // ========================================================================  

    public:
        bool hitTest(float px, float py) override {
            return rect.contains(px, py);
        }
        void onMMove(float px, float py) override {
            if (!visible || !hitTest(px, py)) {
                hoveredIndex = -1;
                return;
            }

            hoveredIndex = getItemIndexAt(px, py);
        }

        void onMDown(float px, float py) override {
            if (!visible || !hitTest(px, py)) return;

            int idx = getItemIndexAt(px, py);
            if (idx >= 0) {
                // 检查是否点击了收藏按钮  
                if (isClickOnFavorite(px, py, idx)) {
                    items[idx].isFavorite = !items[idx].isFavorite;
                    if (onFavoriteToggle) onFavoriteToggle(idx);
                    return;
                }

                // 选中条目  
                selectedIndex = idx;
                if (onSelect) onSelect(idx);
            }
        }

        void onMWheel(float px, float py, int delta) override {
            if (!visible || !hitTest(px, py)) return;
            if (items.empty()) return;

            // 每次滚动2行  
            int step = (delta > 0 ? -2 : 2);
            firstVisibleIndex += step;

            // 关键:同时保证两个边界条件  
            int maxFirstIndex = std::max(0, static_cast<int>(items.size()) - VISIBLE_COUNT);
            firstVisibleIndex = std::clamp(firstVisibleIndex, 0, maxFirstIndex);
        }

private:
    int getItemIndexAt(float px, float py) const {
        if (items.empty()) return -1;

        float localY = py - rect.y - topPadding;
        if (localY < 0) return -1;

        float itemTotalHeight = items[0].style.itemH + vspacing;
        int relativeIndex = static_cast<int>(localY / itemTotalHeight);
        int absoluteIndex = firstVisibleIndex + relativeIndex;

        if (absoluteIndex >= firstVisibleIndex &&
            absoluteIndex < firstVisibleIndex + VISIBLE_COUNT &&
            absoluteIndex < static_cast<int>(items.size())) {

            float itemOffset = localY - relativeIndex * itemTotalHeight;
            if (itemOffset < items[0].style.itemH) {
                return absoluteIndex;
            }
        }

        return -1;
    }

    bool isClickOnFavorite(float px, float py, int itemIndex) const {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return false;

        float contentX = rect.x + leftPadding;
        float contentY = rect.y + topPadding;

        int relativeIndex = itemIndex - firstVisibleIndex;
        float itemY = contentY + relativeIndex * (items[itemIndex].style.itemH + vspacing);

        const auto& layout = items[itemIndex].layout;
        float favX = contentX + layout.favorite.x;
        float favY = itemY + layout.favorite.y;

        return px >= favX && px <= favX + layout.favorite.w &&
            py >= favY && py <= favY + layout.favorite.h;
    }

};

} // namespace MUI

// CoverImage 控件 - 带缓存的大封面显示  
namespace MUI {

    class CoverImage : public UIElement {
    private:
        std::wstring currentImagePath;
        OBitmap currentBitmap;  // 当前图片的位图数据  

        // 样式参数  
        float opacity = 1.0f;
        float cornerRadius = 5.0f;
        Color fillColor = Color(255, 255, 255, 255);
        bool enableFill = false;
        Color strokeColor = Color(147, 188, 234, 255);  // #93BCEA  
        float strokeWidth = 1.0f;
        bool enableStroke = true;

        // 静态 LRU 缓存 (所有 CoverImage 实例共享)  
        struct CacheEntry {
            OBitmap bitmap;
            std::chrono::steady_clock::time_point lastAccess;
        };
        static std::map<std::wstring, CacheEntry> coverCache;
        static const size_t MAX_CACHE_SIZE = 10;

    public:
        CoverImage() {
            rect = { 20, 80, 360, 360 };
            memset(&currentBitmap, 0, sizeof(OBitmap));
        }

        ~CoverImage() noexcept {
            // 不释放 currentBitmap,因为它指向缓存中的数据  
        }

        void setImageFromFile(const std::wstring& path) {
            if (path.empty()) {
                currentImagePath.clear();
                memset(&currentBitmap, 0, sizeof(OBitmap));
                return;
            }

            // 如果已经是同一张图片,无需重新加载  
            if (currentImagePath == path && currentBitmap.data) {
                return;
            }

            currentImagePath = path;

            // 检查缓存  
            auto it = coverCache.find(path);
            if (it != coverCache.end()) {
                currentBitmap = it->second.bitmap;
                it->second.lastAccess = std::chrono::steady_clock::now();
                return;
            }

            // 加载新图片  
            OBitmap bitmap = loadImageToBitmap(path);
            if (bitmap.data) {
                // LRU 缓存管理  
                if (coverCache.size() >= MAX_CACHE_SIZE) {
                    // 找到最久未使用的条目  
                    auto oldest = coverCache.begin();
                    for (auto it = coverCache.begin(); it != coverCache.end(); ++it) {
                        if (it->second.lastAccess < oldest->second.lastAccess) {
                            oldest = it;
                        }
                    }
                    freeBitmap(oldest->second.bitmap);
                    coverCache.erase(oldest);
                }

                // 添加到缓存  
                CacheEntry entry;
                entry.bitmap = bitmap;
                entry.lastAccess = std::chrono::steady_clock::now();
                coverCache[path] = entry;

                currentBitmap = bitmap;
            }
        }

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            // 渲染封面图片  
            if (currentBitmap.data && currentBitmap.width > 0 && currentBitmap.height > 0) {
                auto pic = tvg::Picture::gen();

                // 使用 std::min 确保图片完全包含在容器内  
                float scaleX = rect.w / currentBitmap.width;
                float scaleY = rect.h / currentBitmap.height;
                float scale = std::min(scaleX, scaleY);

                float scaledWidth = currentBitmap.width * scale;
                float scaledHeight = currentBitmap.height * scale;

                // 居中对齐  
                float offsetX = rect.x + (rect.w - scaledWidth) / 2.0f;
                float offsetY = rect.y + (rect.h - scaledHeight) / 2.0f;

                // 从内存加载 (BGRA 格式)  
                auto result = pic->load(
                    reinterpret_cast<const uint32_t*>(currentBitmap.data),
                    currentBitmap.width,
                    currentBitmap.height,
                    tvg::ColorSpace::ABGR8888S,
                    false  // 不复制,直接使用缓存数据  
                );

                if (result == tvg::Result::Success) {
                    pic->size(scaledWidth, scaledHeight);
                    pic->translate(offsetX, offsetY);
                    pic->opacity(static_cast<uint8_t>(opacity * 255));
                    parent->push(pic);
                }
            }

            // 渲染遮罩 (描边矩形)  
            auto mask = tvg::Shape::gen();
            mask->appendRect(rect.x, rect.y, rect.w, rect.h, cornerRadius, cornerRadius);

            // 填充  
            if (enableFill) {
                mask->fill(fillColor.r, fillColor.g, fillColor.b, fillColor.a);
            }
            else {
                mask->fill(0, 0, 0, 0);  // 透明填充  
            }

            // 描边  
            if (enableStroke) {
                mask->strokeFill(strokeColor.r, strokeColor.g, strokeColor.b, strokeColor.a);
                mask->strokeWidth(strokeWidth);
            }

            parent->push(std::move(mask));
        }

        bool hitTest(float px, float py) override {
            return rect.contains(px, py);
        }

        // 清理静态缓存 (在程序退出前调用)  
        static void clearCache() {
            for (auto& pair : coverCache) {
                freeBitmap(pair.second.bitmap);
            }
            coverCache.clear();
        }
    };

    // 静态成员初始化  
    std::map<std::wstring, CoverImage::CacheEntry> CoverImage::coverCache;

} // namespace MUI

// LyricView 控件 
namespace MUI {

    struct LyricLine {
        float t = 0.0f;
        std::string text;
    };

    class LyricView : public UIElement {
    private:
        std::function<float()> timeProvider;
        std::vector<LyricLine> lyrics;

        float currentTime = 0.0f;
        int curIndex = -1;
        int prevIndex = -1;

        float baseIndexF = 0.0f;
        float animStart = 0.0f, animEnd = 0.0f;
        float animTime = 0.0f;
        float animDur = 0.28f;

        float fontSize = 13.0f;
        Color curColor{ 255, 255, 255, 255 };
        Color otherColor{ 180, 180, 180, 200 };
        const char* fontName = nullptr;

    public:
        void setTimeProvider(std::function<float()> fn) {
            timeProvider = std::move(fn);
        }

        void setLyrics(std::vector<LyricLine> lrc) {
            if (lrc.empty()) {
                lrc.push_back({ 0.0f, std::string(u8"(无歌词)") });
            }
            lyrics = std::move(lrc);
            curIndex = -1;
            prevIndex = -1;
            baseIndexF = 0.0f;
            animTime = animDur;
        }

        void setStyle(float fs, Color cur, Color other) {
            fontSize = fs;
            curColor = cur;
            otherColor = other;
        }

        void setFontName(const char* name) {
            fontName = name;
        }

        void update(float dt) override {
            UIElement::update(dt);
            if (!timeProvider || lyrics.empty()) return;

            currentTime = timeProvider();

            // 二分查找当前时间对应的歌词索引  
            int lo = 0, hi = (int)lyrics.size() - 1, ans = -1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (lyrics[mid].t <= currentTime) {
                    ans = mid;
                    lo = mid + 1;
                }
                else hi = mid - 1;
            }
            curIndex = ans;

            // 触发滚动动画  
            if (curIndex != prevIndex) {
                animStart = baseIndexF;
                animEnd = (curIndex < 0 ? 0.0f : (float)curIndex);
                animTime = 0.0f;
                prevIndex = curIndex;
            }

            // 更新动画进度  
            if (animTime < animDur) {
                animTime += std::max(0.0f, dt);
                float t = std::min(1.0f, animTime / animDur);
                float k = 1.0f - std::pow(1.0f - t, 3.0f);  // 缓动函数  
                baseIndexF = animStart + (animEnd - animStart) * k;
            }
            else {
                baseIndexF = animEnd;
            }
        }

        void render(tvg::Scene* parent) override {
            if (!visible || lyrics.empty()) return;

            const float lineH = std::max(12.0f, fontSize + 8.0f);
            const int showCount = 7;
            const int half = showCount / 2;
            const float centerY = rect.y + rect.h * 0.5f;

            int idx = std::clamp(curIndex, 0, (int)lyrics.size() - 1);

            // 计算卡拉OK进度  
            float t0 = (idx >= 0 ? lyrics[idx].t : 0.0f);
            float t1 = (idx + 1 < (int)lyrics.size() ? lyrics[idx + 1].t : t0 + 3.0f);
            float seg = std::max(0.05f, t1 - t0);
            float karaokeP = currentTime >= t0 ?
                std::clamp((currentTime - t0) / seg, 0.0f, 1.0f) : 0.0f;

            int start = std::max(0, (int)std::floor(baseIndexF) - half);
            int end = std::min((int)lyrics.size() - 1, (int)std::floor(baseIndexF) + half);

            // 渲染可见歌词行  
            for (int i = start; i <= end; ++i) {
                const float y = centerY + ((float)i - baseIndexF) * lineH;
                float dist = std::fabs((float)i - baseIndexF);
                float fall = std::exp(-0.9f * dist);  // 距离衰减  

                bool isCur = (i == idx);

                // 背景文本(未高亮部分)  
                auto backText = tvg::Text::gen();
                if (fontName) backText->font(fontName);
                backText->size(fontSize);
                backText->text(lyrics[i].text.c_str());
                backText->translate(rect.x + 8.0f, y);
                backText->layout(std::max(0.0f, rect.w - 16.0f), lineH);
                backText->align(0.5f, 0.5f);

                Color base = isCur ? otherColor : otherColor;
                uint8_t a = (uint8_t)std::clamp((int)std::round(base.a * fall), 0, 255);
                backText->fill(base.r, base.g, base.b);
                backText->opacity(a);
                parent->push(std::move(backText));

                // 卡拉OK高亮部分  
                if (isCur && karaokeP > 0.01f) {
                    auto frontText = tvg::Text::gen();
                    if (fontName) frontText->font(fontName);
                    frontText->size(fontSize);
                    frontText->text(lyrics[i].text.c_str());
                    frontText->translate(rect.x + 8.0f, y);
                    frontText->layout(std::max(0.0f, rect.w - 16.0f), lineH);
                    frontText->align(0.5f, 0.5f);

                    // 矩形裁剪实现进度效果  
                    auto clipShape = tvg::Shape::gen();
                    float clipW = (rect.w - 16.0f) * karaokeP;
                    clipShape->appendRect(rect.x + 8.0f, y - lineH * 0.5f, clipW, lineH, 0, 0);
                    frontText->clip(std::move(clipShape));

                    uint8_t fa = (uint8_t)std::clamp((int)std::round(curColor.a * fall), 0, 255);
                    frontText->fill(curColor.r, curColor.g, curColor.b);
                    frontText->opacity(fa);
                    parent->push(std::move(frontText));
                }
            }

            // 顶部/底部渐变遮罩  
            auto topRect = tvg::Shape::gen();
            topRect->appendRect(rect.x, rect.y, rect.w, lineH * 0.9f, 0, 0);
            topRect->fill(24, 24, 24, 140);
            parent->push(std::move(topRect));

            auto botRect = tvg::Shape::gen();
            botRect->appendRect(rect.x, rect.y + rect.h - lineH * 0.9f, rect.w, lineH * 0.9f, 0, 0);
            botRect->fill(24, 24, 24, 140);
            parent->push(std::move(botRect));
        }

        bool hitTest(float px, float py) override {
            return rect.contains(px, py);
        }
    };

} // namespace MUI

// misc
namespace MUI {

    // 辅助函数:字符串转整数  
    static inline int _toInt(const std::wstring& s, size_t i, size_t len) {
        int v = 0;
        for (size_t k = 0; k < len && i + k < s.size(); ++k) {
            wchar_t c = s[i + k];
            if (c < L'0' || c > L'9') return v;
            v = v * 10 + (int)(c - L'0');
        }
        return v;
    }

    // 解析单行 LRC  
    static void parseLrcLine(const std::wstring& wline, std::vector<LyricLine>& out) {
        size_t i = 0;
        std::vector<float> times;

        // 提取所有时间标签  
        while (i < wline.size()) {
            if (wline[i] != L'[') break;
            size_t j = wline.find(L']', i + 1);
            if (j == std::wstring::npos) break;

            size_t colon = wline.find(L':', i + 1);
            if (colon != std::wstring::npos && colon < j) {
                int mm = _toInt(wline, i + 1, colon - (i + 1));
                size_t secStart = colon + 1;
                size_t dot = wline.find(L'.', secStart);
                int ss = 0, cs = 0;

                if (dot != std::wstring::npos && dot < j) {
                    ss = _toInt(wline, secStart, dot - secStart);
                    int frac = _toInt(wline, dot + 1, j - (dot + 1));
                    if (j - (dot + 1) >= 3) cs = (int)std::round(frac / 10.0);
                    else cs = frac;
                }
                else {
                    ss = _toInt(wline, secStart, j - secStart);
                }

                float t = (float)mm * 60.0f + (float)ss + (float)cs / 100.0f;
                times.push_back(t);
            }
            i = j + 1;
        }

        // 提取文本  
        std::wstring wtext;
        if (!times.empty()) {
            size_t last = wline.rfind(L']');
            if (last != std::wstring::npos && last + 1 < wline.size())
                wtext = wline.substr(last + 1);
        }
        else {
            wtext = wline;
        }

        std::string text = wideToUtf8(wtext);
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
            text.back() == ' ' || text.back() == '\t'))
            text.pop_back();

        if (times.empty()) {
            if (!text.empty()) out.push_back({ 0.0f, text });
            return;
        }

        for (float t : times) {
            out.push_back({ t, text });
        }
    }

    // 解析完整 LRC 文件  
    static std::vector<LyricLine> parseLrc(const std::wstring& content) {
        std::vector<LyricLine> lines;
        size_t start = 0;

        while (start < content.size()) {
            size_t end = content.find_first_of(L"\r\n", start);
            std::wstring line = (end == std::wstring::npos) ?
                content.substr(start) : content.substr(start, end - start);
            parseLrcLine(line, lines);

            if (end == std::wstring::npos) break;
            start = end + 1;
            if (start < content.size() && content[start] == L'\n' && content[end] == L'\r')
                ++start;
        }

        std::sort(lines.begin(), lines.end(),
            [](const LyricLine& a, const LyricLine& b) { return a.t < b.t; });
        return lines;
    }

    // 从音频文件读取嵌入歌词  
    static std::vector<LyricLine> loadLyricsFromFile(const std::wstring& wpath) {
        std::vector<LyricLine> none;
        TagLib::FileRef f(wpath.c_str());
        if (f.isNull()) return none;

        TagLib::PropertyMap props = f.properties();
        if (props.contains("LYRICS")) {
            auto w = props["LYRICS"].toString("\n").toWString();
            return parseLrc(w);
        }
        if (props.contains("UNSYNCEDLYRICS")) {
            auto w = props["UNSYNCEDLYRICS"].toString("\n").toWString();
            return parseLrc(w);
        }
        return none;
    }

} // namespace MUI

// UIFrame 控件 
namespace MUI {

    class UIFrame : public UIElement {
    private:
        // 子元素列表  
        std::vector<std::unique_ptr<UIElement>> children;

        // 布局属性  
        struct {
            float top = 0, right = 0, bottom = 0, left = 0;
        } padding;

        float gap = 0.0f;  // 子元素间距  

        enum class Direction { Horizontal, Vertical } direction = Direction::Vertical;
        enum class Alignment { Start, Center, End, Stretch } alignment = Alignment::Start;

        // 视觉样式  
        Color fillColor = Color(255, 255, 255, 255);
        bool enableFill = true;
        Color strokeColor = Color(200, 200, 200, 255);
        float strokeWidth = 1.0f;
        bool enableStroke = false;
        float cornerRadius = 0.0f;
        float opacity = 1.0f;

        // 裁剪和滚动  
        bool clipsContent = false;
        float scrollOffsetX = 0.0f;
        float scrollOffsetY = 0.0f;

    public:
        UIFrame() {
            rect = { 0, 0, 100, 100 };
        }

        ~UIFrame() noexcept override {
            children.clear();
        }

        // 添加子元素  
        void addChild(std::unique_ptr<UIElement> child) {
            children.push_back(std::move(child));
            updateLayout();
        }

        // 移除子元素  
        void removeChild(UIElement* child) {
            children.erase(
                std::remove_if(children.begin(), children.end(),
                    [child](const auto& c) { return c.get() == child; }),
                children.end()
            );
            updateLayout();
        }

        // 设置布局方向  
        void setDirection(Direction dir) {
            direction = dir;
            updateLayout();
        }

        // 设置对齐方式  
        void setAlignment(Alignment align) {
            alignment = align;
            updateLayout();
        }

        // 设置内边距  
        void setPadding(float top, float right, float bottom, float left) {
            padding.top = top;
            padding.right = right;
            padding.bottom = bottom;
            padding.left = left;
            updateLayout();
        }

        // 设置间距  
        void setGap(float g) {
            gap = g;
            updateLayout();
        }

        // 设置样式  
        void setStyle(Color fill, Color stroke, float strokeW, float corner) {
            fillColor = fill;
            strokeColor = stroke;
            strokeWidth = strokeW;
            cornerRadius = corner;
        }

        // 启用/禁用裁剪  
        void setClipsContent(bool clips) {
            clipsContent = clips;
        }

        // 更新布局  
        void updateLayout() {
            if (children.empty()) return;

            float contentX = rect.x + padding.left;
            float contentY = rect.y + padding.top;
            float availableW = rect.w - padding.left - padding.right;
            float availableH = rect.h - padding.top - padding.bottom;

            if (direction == Direction::Horizontal) {
                // 水平布局  
                float currentX = contentX;
                for (auto& child : children) {
                    if (!child->visible) continue;

                    child->rect.x = currentX;
                    child->rect.y = contentY;

                    // 根据对齐方式调整 Y 坐标  
                    if (alignment == Alignment::Center) {
                        child->rect.y += (availableH - child->rect.h) / 2.0f;
                    }
                    else if (alignment == Alignment::End) {
                        child->rect.y += availableH - child->rect.h;
                    }
                    else if (alignment == Alignment::Stretch) {
                        child->rect.h = availableH;
                    }

                    currentX += child->rect.w + gap;
                }
            }
            else {
                // 垂直布局  
                float currentY = contentY;
                for (auto& child : children) {
                    if (!child->visible) continue;

                    child->rect.x = contentX;
                    child->rect.y = currentY;

                    // 根据对齐方式调整 X 坐标  
                    if (alignment == Alignment::Center) {
                        child->rect.x += (availableW - child->rect.w) / 2.0f;
                    }
                    else if (alignment == Alignment::End) {
                        child->rect.x += availableW - child->rect.w;
                    }
                    else if (alignment == Alignment::Stretch) {
                        child->rect.w = availableW;
                    }

                    currentY += child->rect.h + gap;
                }
            }
        }

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            // 渲染背景  
            if (enableFill || enableStroke) {
                auto bg = tvg::Shape::gen();
                bg->appendRect(rect.x, rect.y, rect.w, rect.h, cornerRadius, cornerRadius);

                if (enableFill) {
                    bg->fill(fillColor.r, fillColor.g, fillColor.b, fillColor.a);
                }

                if (enableStroke) {
                    bg->strokeFill(strokeColor.r, strokeColor.g, strokeColor.b, strokeColor.a);
                    bg->strokeWidth(strokeWidth);
                }

                bg->opacity(static_cast<uint8_t>(opacity * 255));
                parent->push(std::move(bg));
            }

            // 渲染子元素    
            if (clipsContent) {
                // 创建裁剪场景 (返回原始指针)  
                auto clipScene = tvg::Scene::gen();

                // 创建裁剪遮罩 (返回原始指针)  
                auto clipMask = tvg::Shape::gen();
                clipMask->appendRect(rect.x, rect.y, rect.w, rect.h, cornerRadius, cornerRadius);

                // 使用 clip() 方法,传递原始指针  
                clipScene->clip(clipMask);

                // 添加子元素 (clipScene 已经是原始指针,直接使用)  
                for (auto& child : children) {
                    if (child->visible) {
                        child->render(clipScene);  // 不需要 .get()  
                    }
                }

                // 将裁剪场景添加到父场景 (传递原始指针)  
                parent->push(clipScene);  // 不需要 std::move()  
            }
            else {
                for (auto& child : children) {
                    if (child->visible) {
                        child->render(parent);
                    }
                }
            }
        }

        bool hitTest(float px, float py) override {
            return rect.contains(px, py);
        }

        void onMMove(float px, float py) override {
            for (auto& child : children) {
                if (child->hitTest(px, py)) {
                    child->onMMove(px, py);
                }
            }
        }

        void onMDown(float px, float py) override {
            for (auto& child : children) {
                if (child->hitTest(px, py)) {
                    child->onMDown(px, py);
                }
            }
        }

        void onMWheel(float px, float py, int delta) override {
            for (auto& child : children) {
                if (child->hitTest(px, py)) {
                    child->onMWheel(px, py, delta);
                }
            }
        }
    };

} // namespace MUI
