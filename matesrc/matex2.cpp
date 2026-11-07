/****************************************************************************
 * 标题: matex2 - threepp 学习沙箱
 * 功能: 复刻 ThreeppVSTest 的 01_intro/11_texture2d 示例：
 *       TextureLoader 贴图 + Sphere/Box/Plane + Reflector 实时镜面 + OrbitControls
 * 说明: 与 matex0 一样不链接 GLFW，用 Win32 + WGL 自建 OpenGL 4.5 上下文；
 *       渲染交给 threepp::GLRenderer(size)，事件转给 PeripheralsEventSource。
 *       与 matex0 的差别：这是个人学习用的最小外壳，没有壁纸挂载与 ImGui 面板，
 *       走 console 子系统，日志直接 printf 到控制台。
 *
 * 依赖: ../../3rd/lib/threepp_mt.lib（/MT）、WS2_32、matex.rc（仅窗口图标）
 * 资源: Data/textures/{checker.png, crate.gif, brick_bump.jpg}
 *
 * 操作: 左键旋转 / 右键平移 / 滚轮缩放 / ESC 退出
 ****************************************************************************/

#define NOMINMAX

#include <windows.h>
#include <mmsystem.h>

#include <glad/glad.h>

#include "threepp/cameras/OrthographicCamera.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"   // 只为下面的 initWindow 空实现
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/controls/OrbitControls.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/input/PeripheralsEventSource.hpp"
#include "threepp/core/Uniform.hpp"
#include "threepp/loaders/TextureLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/MathUtils.hpp"
#include "threepp/math/Vector4.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/Reflector.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"
#include "threepp/utils/LoadGlad.hpp"

// ImGui：切视频用的最小面板（Win32 + OpenGL3 后端，无 freetype）
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

#include "../momo/fxcode.h"   // 复用 MX 现成的 Shadertoy 特效源码（g_fxSource0..3）
#include "src/core/mate_video.h"   // 引擎无关视频帧源（MO/xav.h 解码 + 独立线程）
#include "src/core/mate_audio3d.h" // 视频音轨 -> miniaudio 3D 空间音源

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// GLRenderer.obj 里引用 Canvas::initWindow（GLFW 路径）。Mate 不编译 Canvas.cpp，
// 自己建窗口也不会走 GLRenderer(Canvas&) 构造函数，给链接器一个空实现即可。
namespace threepp {
    void Canvas::initWindow(GraphicsAPI) {}
}// namespace threepp

// 只给了 glad.h（不含 GL/gl.h），WGL 这几个入口要自己声明
extern "C" {
__declspec(dllimport) HGLRC WINAPI wglCreateContext(HDC);
__declspec(dllimport) BOOL  WINAPI wglMakeCurrent(HDC, HGLRC);
__declspec(dllimport) BOOL  WINAPI wglDeleteContext(HGLRC);
__declspec(dllimport) PROC  WINAPI wglGetProcAddress(LPCSTR);
}

typedef BOOL(WINAPI* PFN_wglSwapIntervalEXT)(int);

static const wchar_t* kWindowClass = L"MateX2Window";
static const int kDefaultWidth  = 1280;
static const int kDefaultHeight = 720;

static class Win32Canvas* g_canvas = nullptr;

// VK -> threepp::Key（与 GLFW 的 Key 枚举同序）
static threepp::Key toThreeppKey(WPARAM vk) {
    using threepp::Key;
    if (vk >= 'A' && vk <= 'Z') return (Key)((int)Key::A + (int)(vk - 'A'));
    if (vk >= '0' && vk <= '9') return (Key)((int)Key::NUM_0 + (int)(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (Key)((int)Key::F1 + (int)(vk - VK_F1));

    switch (vk) {
    case VK_SPACE:      return Key::SPACE;
    case VK_ESCAPE:     return Key::ESCAPE;
    case VK_RETURN:     return Key::ENTER;
    case VK_TAB:        return Key::TAB;
    case VK_BACK:       return Key::BACKSPACE;
    case VK_INSERT:     return Key::INSERT;
    case VK_DELETE:     return Key::DEL;
    case VK_RIGHT:      return Key::RIGHT;
    case VK_LEFT:       return Key::LEFT;
    case VK_DOWN:       return Key::DOWN;
    case VK_UP:         return Key::UP;
    case VK_PRIOR:      return Key::PAGE_UP;
    case VK_NEXT:       return Key::PAGE_DOWN;
    case VK_HOME:       return Key::HOME;
    case VK_END:        return Key::END;
    case VK_LSHIFT:
    case VK_SHIFT:      return Key::LEFT_SHIFT;
    case VK_LCONTROL:
    case VK_CONTROL:    return Key::LEFT_CONTROL;
    case VK_LMENU:
    case VK_MENU:       return Key::LEFT_ALT;
    default:            return Key::UNKNOWN;
    }
}

// 与 GLFW 的 mods 位定义一致（OrbitControls 依赖它）
static int toThreeppMods() {
    int mods = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= 0x0001;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= 0x0002;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= 0x0004;
    return mods;
}

/*==================== Win32Canvas：最小 Win32 版 Canvas ====================*/
class Win32Canvas : public threepp::PeripheralsEventSource {
public:
    ~Win32Canvas() override { destroy(); }

    bool create(const std::wstring& title, int width, int height, bool vsync);
    void destroy();

    [[nodiscard]] threepp::WindowSize size() const override { return { width_, height_ }; }
    [[nodiscard]] float aspect() const {
        return height_ > 0 ? (float)width_ / (float)height_ : 1.0f;
    }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] HWND hwnd() const { return hwnd_; }

    void onWindowResize(std::function<void(threepp::WindowSize)> f) {
        resizeCallback_ = std::move(f);
    }
    void resize(int width, int height);
    void handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    bool pumpEvents();
    void swapBuffers() const { if (hdc_) SwapBuffers(hdc_); }

private:
    bool initGL(bool vsync);

    HWND  hwnd_  = nullptr;
    HDC   hdc_   = nullptr;
    HGLRC hglrc_ = nullptr;
    int   width_  = kDefaultWidth;
    int   height_ = kDefaultHeight;
    bool  vsync_  = true;
    bool  mouseDown_[3] = { false, false, false };
    std::function<void(threepp::WindowSize)> resizeCallback_;
};

static LRESULT CALLBACK X2WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 键鼠消息优先交给 ImGui（面板拖拽/点击）；ESC 保留给退出
    if (ImGui::GetCurrentContext() != nullptr &&
        !(msg == WM_KEYDOWN && wParam == VK_ESCAPE)) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 0;
    }

    // 面板正在吃鼠标/键盘时，不把事件传给 OrbitControls，避免误转相机
    const bool mouseMsg = (msg >= WM_MOUSEMOVE && msg <= WM_MOUSEWHEEL);
    const bool keyMsg   = (msg >= WM_KEYDOWN && msg <= WM_SYSKEYUP);
    const bool skipForImGui = ImGui::GetCurrentContext() != nullptr &&
            ((mouseMsg && ImGui::GetIO().WantCaptureMouse) ||
             (keyMsg && ImGui::GetIO().WantCaptureKeyboard));
    if (g_canvas && !skipForImGui) g_canvas->handleMessage(msg, wParam, lParam);

    switch (msg) {
    case WM_SIZE:
        if (g_canvas) g_canvas->resize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool registerWindowClass() {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = X2WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kWindowClass;
    // 图标由 matex.rc 提供，资源 ID 与 matex0 保持一致
    wc.hIcon         = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(103));
    wc.hIconSm       = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(104));

    if (!RegisterClassExW(&wc)) {
        std::printf("[窗口] 注册窗口类失败: %lu\n", GetLastError());
        return false;
    }
    registered = true;
    return true;
}

bool Win32Canvas::create(const std::wstring& title, int width, int height, bool vsync) {
    if (!registerWindowClass()) return false;

    width_ = width;
    height_ = height;
    vsync_ = vsync;

    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(
            0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
            (screenW - winW) / 2, (screenH - winH) / 2, winW, winH,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        std::printf("[窗口] CreateWindowEx 失败: %lu\n", GetLastError());
        return false;
    }

    g_canvas = this;
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);

    if (!initGL(vsync)) return false;

    RECT client = { 0, 0, 0, 0 };
    GetClientRect(hwnd_, &client);
    width_  = client.right - client.left;
    height_ = client.bottom - client.top;
    if (width_ <= 0)  width_  = kDefaultWidth;
    if (height_ <= 0) height_ = kDefaultHeight;
    return true;
}

void Win32Canvas::destroy() {
    if (hglrc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc_);
        hglrc_ = nullptr;
    }
    if (hdc_ && hwnd_) {
        ReleaseDC(hwnd_, hdc_);
        hdc_ = nullptr;
    }
    if (hwnd_) {
        if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (g_canvas == this) g_canvas = nullptr;
    mouseDown_[0] = mouseDown_[1] = mouseDown_[2] = false;
}

bool Win32Canvas::initGL(bool vsync) {
    hdc_ = GetDC(hwnd_);
    if (!hdc_) {
        std::printf("[GL] GetDC 失败\n");
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32,
        0, 0, 0, 0, 0, 0,
        0, 0,
        0, 0, 0, 0, 0,
        24, 8,
        0,
        PFD_MAIN_PLANE,
        0, 0, 0, 0
    };

    const int pf = ChoosePixelFormat(hdc_, &pfd);
    if (!pf || !SetPixelFormat(hdc_, pf, &pfd)) {
        std::printf("[GL] 像素格式设置失败\n");
        return false;
    }

    // 先建临时上下文，才能拿到 wglCreateContextAttribsARB
    HGLRC tempCtx = wglCreateContext(hdc_);
    if (!tempCtx) {
        std::printf("[GL] wglCreateContext 失败\n");
        return false;
    }
    if (!wglMakeCurrent(hdc_, tempCtx)) {
        std::printf("[GL] 临时上下文 current 失败\n");
        wglDeleteContext(tempCtx);
        return false;
    }

    typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
    auto createContextAttribs =
            (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");

    struct Attempt {
        int major, minor, profile;
        const char* name;
    };
    static const Attempt attempts[] = {
        { 4, 5, 0x00000002, "4.5 core" },
        { 4, 5, 0x00000001, "4.5 compatibility" },
        { 3, 3, 0x00000002, "3.3 core" },
    };

    HGLRC ctx = nullptr;
    std::string chosen = "legacy";
    if (createContextAttribs) {
        for (const auto& a : attempts) {
            const int attribs[] = {
                0x2091, a.major,   // WGL_CONTEXT_MAJOR_VERSION_ARB
                0x2092, a.minor,   // WGL_CONTEXT_MINOR_VERSION_ARB
                0x2094, a.profile, // WGL_CONTEXT_PROFILE_MASK_ARB
                0
            };
            ctx = createContextAttribs(hdc_, nullptr, attribs);
            if (ctx) {
                chosen = a.name;
                break;
            }
            std::printf("[GL] %s 上下文不可用，尝试下一档\n", a.name);
        }
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (!ctx) {
        std::printf("[GL] 未能创建 3.3+ 上下文，回退 legacy\n");
        ctx = wglCreateContext(hdc_);
    }
    if (!ctx) {
        std::printf("[GL] 无法创建 OpenGL 上下文\n");
        return false;
    }

    hglrc_ = ctx;
    if (!wglMakeCurrent(hdc_, hglrc_)) {
        std::printf("[GL] 正式上下文 current 失败\n");
        return false;
    }

    // threepp 的 glad 全进程只加载一次，必须在最终上下文 current 之后
    try {
        threepp::loadGlad();
    } catch (const std::exception& e) {
        std::printf("[GL] glad 加载失败: %s\n", e.what());
        return false;
    }

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    std::printf("[GL] %s -> %d.%d | %s | %s\n", chosen.c_str(), (int)major, (int)minor,
                (const char*)glGetString(GL_RENDERER),
                (const char*)glGetString(GL_VERSION));

    PFN_wglSwapIntervalEXT swapInterval =
            (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
    if (swapInterval) {
        swapInterval(vsync ? 1 : 0);
        std::printf("[GL] 垂直同步: %s\n", vsync ? "开" : "关");
    }

    RECT client = { 0, 0, 0, 0 };
    GetClientRect(hwnd_, &client);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    glViewport(0, 0, w > 0 ? w : kDefaultWidth, h > 0 ? h : kDefaultHeight);
    return true;
}

void Win32Canvas::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;   // 最小化
    width_ = width;
    height_ = height;
    if (resizeCallback_) resizeCallback_({ width, height });
}

bool Win32Canvas::pumpEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void Win32Canvas::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEMOVE:
        onMouseMoveEvent(threepp::Vector2((float)(short)LOWORD(lParam),
                                         (float)(short)HIWORD(lParam)));
        break;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
        const int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? 0
                         : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) ? 1 : 2;
        const bool press = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                            msg == WM_MBUTTONDOWN);
        mouseDown_[button] = press;
        if (press) {
            SetCapture(hwnd_);
        } else if (!mouseDown_[0] && !mouseDown_[1] && !mouseDown_[2]) {
            ReleaseCapture();
        }
        onMousePressedEvent(button,
                            threepp::Vector2((float)(short)LOWORD(lParam),
                                             (float)(short)HIWORD(lParam)),
                            press ? MouseAction::PRESS : MouseAction::RELEASE);
        break;
    }

    case WM_MOUSEWHEEL: {
        // 与 GLFW 的 yoffset 一致：一格 = 1.0
        const float dy = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
        onMouseWheelEvent(threepp::Vector2(0.0f, dy));
        break;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const bool press  = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        const bool repeat = press && (lParam & (1 << 30)) != 0;
        const int  scancode = (int)((lParam >> 16) & 0xFF);
        threepp::KeyEvent evt(toThreeppKey(wParam), scancode, toThreeppMods());
        if (!press)      onKeyEvent(evt, KeyAction::RELEASE);
        else if (repeat) onKeyEvent(evt, KeyAction::REPEAT);
        else             onKeyEvent(evt, KeyAction::PRESS);
        break;
    }

    default:
        break;
    }
}

/*==================== 资源路径 ====================*/
static std::filesystem::path exeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

/*==================== Shadertoy 特效面片 ====================*/
// fxcode.h 里的特效自带 main()，并用 gl_FragCoord.xy（屏幕像素坐标）取 fragCoord。
// 贴到面片上时把 fragCoord 换成面片 uv（PlaneGeometry 的 uv 与 Shadertoy 一样 v=1 在上），
// 图案就跟着面片走，而不是钉死在屏幕上；入口 main() 改名后再包一层强度系数。
static std::string buildFxPlaneFragment(const char* body) {
    std::string s = body;

    // Shadertoy 的 fragCoord 来自屏幕；这里换成面片 uv
    const std::string from = "gl_FragCoord.xy";
    const std::string to   = "(vFxUv * iResolution)";
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }

    const std::string entry = "void main()";
    const size_t pos = s.find(entry);
    if (pos == std::string::npos) return s;   // 兜底：认不出就直接用原源码

    s.replace(pos, entry.size(), "void fxEntry()");
    s = "varying vec2 vFxUv;\n" + s;   // GLSL 要求先声明后使用
    s += "\nuniform float fxGain;\n"
         "uniform float fxOpacity;\n"
         "void main() {\n"
         "    fxEntry();\n"
         "    gl_FragColor.rgb *= fxGain;\n"
         "    gl_FragColor.a   *= fxOpacity;   // 图层整体透明度，供画布叠加\n"
         "}\n";
    return s;
}

// 普通 MVP 变换 + 把 uv 传给片元；不参与任何光照。图层四边形和可见面片共用这个顶点着色器
static const char* kFxPlaneVertexShader = R"GLSL(
varying vec2 vFxUv;
void main() {
    vFxUv = uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)GLSL";

// 图片图层 / 画布拷贝 / 面片贴图共用：按 uv 采样一张纹理，乘以图层透明度
static const char* kFxImageFragmentShader = R"GLSL(
varying vec2 vFxUv;
uniform sampler2D map;
uniform float fxOpacity;
void main() {
    vec4 c = texture(map, vFxUv);
    gl_FragColor = vec4(c.rgb, c.a * fxOpacity);
}
)GLSL";

// 视频 YUV420P：三个 R8 平面 + GPU 端 YUV→RGB
// BT.709 限制范围（Y 16-235 / CbCr 16-240）：
//   R = 1.164(Y-16) + 1.793(Cr-128)
//   G = 1.164(Y-16) - 0.213(Cb-128) - 0.533(Cr-128)
//   B = 1.164(Y-16) + 2.112(Cb-128)
static const char* kYuvVertexShader = R"GLSL(
varying vec2 vUv;
void main() {
    vUv = uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)GLSL";

static const char* kYuvFragmentShader = R"GLSL(
varying vec2 vUv;
uniform sampler2D yTex;
uniform sampler2D uTex;
uniform sampler2D vTex;
void main() {
    float y = texture(yTex, vUv).r * 255.0;
    float u = texture(uTex, vUv).r * 255.0 - 128.0;   // Cb
    float v = texture(vTex, vUv).r * 255.0 - 128.0;   // Cr
    y = (y - 16.0) * 1.164;
    float r = y + 1.793 * v;
    float g = y - 0.213 * u - 0.533 * v;
    float b = y + 2.112 * u;
    // 上面是 0..255 刻度；除以 255 再 clamp，否则亮部全部被截成白色
    gl_FragColor = vec4(clamp(vec3(r, g, b) / 255.0, 0.0, 1.0), 1.0);
}
)GLSL";

/*==================== FX 画布：图层栈合成 ====================*/
// 一张画布 = 一条图层栈，所有图层按 order 从下往上依次画进同一块离屏纹理：
//   - image 层：图片铺 rect
//   - fx 层   ：Shadertoy 源码，输出带 alpha 的叠加结果（走 Normal 混合）
//
// 排序规则：
//   1. order 小的在下、大的在上（决定前后关系）；
//   2. order 相同时按添加顺序，后加的在上（std::stable_sort 保证插入顺序）；
//   3. 同一层的位置用 rect(x, y, w, h)——画布归一化坐标、左下角为原点（与 Shadertoy
//      的 fragCoord 方向一致）。层与层可以完全重叠、也可以各自占一块区域，互不遮挡；
//      重叠部分才由 order 决定谁压谁。
//
// 需要读取"下面已经画好的画面"的 fx 层（如雨滴）把 samplesPrevious 置 true：
//   内部用 front/back 两块 RT 乒乓，先把 front 整体拷到 back，再在 back 上叠本层并采样 front。
//
// 用法：
//     FxCanvas canvas(640, 360);
//     canvas.addImage(tex, {0, 0, 1, 1}, 1.0f, 0);                  // 底层图片
//     canvas.addFx(FXCODE::g_fxSource1, {0, 0, 1, 1}, 1.0f, 1);     // 中层特效
//     canvas.addFx(FXCODE::g_fxSource2, {0, 0, 1, 1}, 1.0f, 2, true);  // 顶层后处理
//     canvas.update(dt);
//     canvas.render(*renderer);     // 必须在主场景 render 之前
//     canvas.texture()              // 最终画面，给可见面片的 map
class FxCanvas {

public:
    FxCanvas(int width, int height) : size(float(width), float(height)) {

        front = createTarget(width, height);
        back  = createTarget(width, height);

        camera = threepp::OrthographicCamera::create();
        camera->position.z = 1.0f;   // 正交相机只看 xy，z 只用来保证在近平面内

        // 拷贝 pass 用：把 front 整体搬到 back
        copyMaterial = threepp::ShaderMaterial::create();
        copyMaterial->vertexShader   = kFxPlaneVertexShader;
        copyMaterial->fragmentShader = kFxImageFragmentShader;
        copyMaterial->uniforms["map"]       = threepp::Uniform(static_cast<threepp::Texture*>(front->texture.get()));
        copyMaterial->uniforms["fxOpacity"] = threepp::Uniform(1.0f);
        copyScene = threepp::Scene::create();
        auto copyQuad = threepp::Mesh::create(threepp::PlaneGeometry::create(2.0f, 2.0f), copyMaterial);
        copyQuad->frustumCulled = false;   // 正交铺满 NDC，别被视锥剔除
        copyScene->add(copyQuad);

        // 后处理型特效的通用输入占位：1x1 全透明（雨滴会做 mix(col, lyrics.rgb, lyrics.a)，
        // 若绑成不透明黑，整屏会被乘成黑的）
        transparentTex = threepp::Texture::create(
                threepp::Image(std::vector<unsigned char>{ 0, 0, 0, 0 }, 1, 1));
    }

    // 图片层
    void addImage(const std::shared_ptr<threepp::Texture>& texture,
                  const threepp::Vector4& rect, float opacity, int order) {

        auto material = threepp::ShaderMaterial::create();
        material->vertexShader   = kFxPlaneVertexShader;
        material->fragmentShader = kFxImageFragmentShader;
        material->uniforms["map"]       = threepp::Uniform(static_cast<threepp::Texture*>(texture.get()));
        material->uniforms["fxOpacity"] = threepp::Uniform(opacity);
        material->transparent = true;    // 参与 Normal 混合，透明区域露出下层
        material->depthTest   = false;   // 层叠顺序靠绘制顺序，不靠深度
        material->depthWrite  = false;

        addLayer(material, rect, order, false);
    }

    // FX 层；samplesPrevious = true 表示需要把下层已合成画面当 sceneTex 输入
    void addFx(const char* body, const threepp::Vector4& rect, float opacity, int order,
               bool samplesPrevious = false) {

        auto material = threepp::ShaderMaterial::create();
        material->vertexShader   = kFxPlaneVertexShader;
        material->fragmentShader = buildFxPlaneFragment(body);
        material->uniforms["iTime"]       = threepp::Uniform(0.0f);
        material->uniforms["iResolution"] = threepp::Uniform(
                threepp::Vector2(rect.z * size.x, rect.w * size.y));   // 本层自己的像素尺寸
        material->uniforms["fxGain"]      = threepp::Uniform(1.0f);
        material->uniforms["fxOpacity"]   = threepp::Uniform(opacity);
        if (samplesPrevious) {
            // Shadertoy 后处理常见输入；不需要的 uniform 设了也无害
            material->uniforms["sceneTex"]  = threepp::Uniform(static_cast<threepp::Texture*>(front->texture.get()));
            material->uniforms["texSize"]   = threepp::Uniform(size);
            material->uniforms["iMouseXY"]  = threepp::Uniform(threepp::Vector2(0.0f, 0.0f));
            material->uniforms["iMouseZW"]  = threepp::Uniform(threepp::Vector2(0.0f, 0.0f));
            material->uniforms["lyricsTex"] = threepp::Uniform(static_cast<threepp::Texture*>(transparentTex.get()));
        }
        material->transparent = true;
        material->depthTest   = false;
        material->depthWrite  = false;

        addLayer(material, rect, order, samplesPrevious);
    }

    // 推进所有 fx 层的 iTime；自定义 uniform 改动后要显式标记上传
    void update(float dt) {
        time += dt;
        for (auto& layer : layers) {
            if (layer.material->uniforms.count("iTime")) {
                layer.material->uniforms["iTime"].value<float>() = time;
                layer.material->uniformsNeedUpdate = true;
            }
        }
    }

    // 按 order 从上到下合成；调用后自动回到屏幕
    void render(threepp::GLRenderer& renderer) {

        std::stable_sort(layers.begin(), layers.end(),
                         [](const Layer& a, const Layer& b) { return a.order < b.order; });

        // 画布底色：渲染器只认一个 clear color，这里临时借用作画布底色，结束时恢复
        threepp::Color prevClear;
        renderer.getClearColor(prevClear);
        const float prevAlpha = renderer.getClearAlpha();
        renderer.setClearColor(threepp::Color(0x000000), 1.0f);

        bool first = true;
        for (auto& layer : layers) {

            if (layer.samplesPrevious) {
                if (first) {
                    // 首层就是后处理：没有下层可拷，直接清屏后画
                    renderer.setRenderTarget(front.get());
                    renderer.clear();
                } else {
                    // 1) 已合成画面整体拷到另一块 RT
                    copyMaterial->uniforms["map"] =
                            threepp::Uniform(static_cast<threepp::Texture*>(front->texture.get()));
                    copyMaterial->uniformsNeedUpdate = true;
                    renderer.setRenderTarget(back.get());
                    renderer.clear();
                    renderer.render(*copyScene, *camera);

                    // 2) 本层叠在 back 上，sceneTex 读 front
                    layer.material->uniforms["sceneTex"] =
                            threepp::Uniform(static_cast<threepp::Texture*>(front->texture.get()));
                    layer.material->uniforms["texSize"] = threepp::Uniform(size);
                    layer.material->uniformsNeedUpdate = true;
                }
                renderer.autoClear = false;
                renderer.render(*layer.scene, *camera);
                renderer.autoClear = true;

                if (!first) std::swap(front, back);
            } else {
                renderer.setRenderTarget(front.get());
                if (first) renderer.clear();
                renderer.autoClear = false;
                renderer.render(*layer.scene, *camera);
                renderer.autoClear = true;
            }
            first = false;
        }

        renderer.setRenderTarget(nullptr);
        renderer.setClearColor(prevClear, prevAlpha);   // 恢复主场景底色（按屏幕重新编码）
    }

    [[nodiscard]] threepp::Texture* texture() const { return front->texture.get(); }
    [[nodiscard]] const threepp::Vector2& sizeVec() const { return size; }

private:
    struct Layer {
        int order = 0;
        bool samplesPrevious = false;
        std::shared_ptr<threepp::ShaderMaterial> material;
        std::shared_ptr<threepp::Scene> scene;
    };

    void addLayer(const std::shared_ptr<threepp::ShaderMaterial>& material,
                  const threepp::Vector4& rect, int order, bool samplesPrevious) {

        // 图层四边形：rect(归一化) → NDC 坐标 + 缩放
        auto mesh = threepp::Mesh::create(threepp::PlaneGeometry::create(1.0f, 1.0f), material);
        mesh->scale.set(2.0f * rect.z, 2.0f * rect.w, 1.0f);
        mesh->position.set(2.0f * (rect.x + 0.5f * rect.z) - 1.0f,
                           2.0f * (rect.y + 0.5f * rect.w) - 1.0f,
                           0.0f);
        mesh->frustumCulled = false;

        auto scene = threepp::Scene::create();
        scene->add(mesh);

        layers.push_back(Layer{ order, samplesPrevious, material, scene });
    }

    static std::shared_ptr<threepp::RenderTarget> createTarget(int width, int height) {
        threepp::RenderTarget::Options options;
        options.format    = threepp::Format::RGBA;
        options.type      = threepp::Type::UnsignedByte;
        options.minFilter = threepp::Filter::Linear;
        options.magFilter = threepp::Filter::Linear;
        options.depthBuffer = false;
        return threepp::RenderTarget::create(width, height, options);
    }

    std::vector<Layer> layers;   // 保持添加顺序，渲染时按 order 稳定排序
    std::shared_ptr<threepp::RenderTarget> front;   // 当前累积画面
    std::shared_ptr<threepp::RenderTarget> back;    // 拷贝/乒乓缓冲
    std::shared_ptr<threepp::OrthographicCamera> camera;
    std::shared_ptr<threepp::ShaderMaterial> copyMaterial;
    std::shared_ptr<threepp::Scene> copyScene;
    std::shared_ptr<threepp::Texture> transparentTex;
    threepp::Vector2 size;
    float time = 0.0f;
};

/*==================== 入口 ====================*/
int main() {    // 源码字符串是 UTF-8（/utf-8），控制台默认 GBK 代码页会把中文显示成乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
    timeBeginPeriod(1);

    std::printf("========================================\n");
    std::printf(" matex2 - threepp 11_texture2d 学习沙箱\n");
    std::printf(" 左键旋转 / 右键平移 / 滚轮缩放 / ESC 退出\n");
    std::printf("========================================\n");

    Win32Canvas canvas;
    if (!canvas.create(L"Mate x2 - threepp 11_texture2d", kDefaultWidth, kDefaultHeight, true)) {
        std::printf("[启动] 窗口/GL 初始化失败\n");
        timeEndPeriod(1);
        return 1;
    }

    std::unique_ptr<threepp::GLRenderer> renderer;
    try {
        renderer = std::make_unique<threepp::GLRenderer>(
                std::pair<int, int>{ canvas.width(), canvas.height() });
    } catch (const std::exception& e) {
        std::printf("[threepp] 渲染器创建失败: %s\n", e.what());
        canvas.destroy();
        timeEndPeriod(1);
        return 1;
    }
    renderer->setClearColor(threepp::Color(threepp::Color::aliceblue));

    // ImGui：只放一个切视频的小面板
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& imguiIo = ImGui::GetIO();
    imguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imguiIo.IniFilename = nullptr;                 // 不生成 imgui.ini
    ImGui::StyleColorsDark();
    if (std::filesystem::exists("C:/Windows/Fonts/msyh.ttc")) {
        imguiIo.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 18.0f, nullptr,
                                          imguiIo.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    } else {
        imguiIo.Fonts->AddFontDefault();
    }
    ImGui_ImplWin32_Init(canvas.hwnd());
    ImGui_ImplOpenGL3_Init("#version 450");

    auto scene = threepp::Scene::create();
    scene->background = threepp::Color(threepp::Color::aliceblue);

    auto camera = std::make_shared<threepp::PerspectiveCamera>(
            75.0f, canvas.aspect(), 0.1f, 1000.0f);
    camera->position.set(0.0f, 0.0f, 5.0f);

    // 先摆好相机再建 controls：OrbitControls 构造时会立即 update()
    std::unique_ptr<threepp::OrbitControls> controls;
    try {
        controls = std::make_unique<threepp::OrbitControls>(*camera, canvas);
    } catch (const std::exception& e) {
        std::printf("[相机] OrbitControls 创建失败: %s\n", e.what());
        canvas.destroy();
        timeEndPeriod(1);
        return 1;
    }

    std::filesystem::path texDir = exeDir() / "Data" / "textures";
    threepp::TextureLoader loader;
    std::shared_ptr<threepp::Texture> checkerTex, crateTex, brickTex;
    try {
        checkerTex = loader.load(texDir / "checker.png", threepp::ColorSpace::sRGB);
        crateTex   = loader.load(texDir / "crate.gif", threepp::ColorSpace::sRGB);
        brickTex   = loader.load(texDir / "brick_bump.jpg", threepp::ColorSpace::sRGB);
        std::printf("[纹理] checker.png / crate.gif / brick_bump.jpg 已加载\n");
    } catch (const std::exception& e) {
        std::printf("[纹理] 加载失败: %s（检查 %s）\n", e.what(), texDir.string().c_str());
        canvas.destroy();
        timeEndPeriod(1);
        return 1;
    }

    // sphere：checker 贴图
    auto sphereGeometry = threepp::SphereGeometry::create(0.5f, 16, 16);
    auto sphereMaterial = threepp::MeshBasicMaterial::create(
            threepp::MeshBasicMaterial::Params{}.map(checkerTex));
    auto sphere = threepp::Mesh::create(sphereGeometry, sphereMaterial);
    sphere->position.set(1.0f, 0.0f, 0.0f);
    scene->add(sphere);

    // box：crate 贴图
    auto boxGeometry = threepp::BoxGeometry::create();
    auto boxMaterial = threepp::MeshBasicMaterial::create(
            threepp::MeshBasicMaterial::Params{}.map(crateTex));
    auto box = threepp::Mesh::create(boxGeometry, boxMaterial);
    box->position.set(-1.0f, 0.0f, 0.0f);
    scene->add(box);

    // plane：brick 贴图，双面
    auto planeGeometry = threepp::PlaneGeometry::create(5, 5);
    auto planeMaterial = threepp::MeshBasicMaterial::create(
            threepp::MeshBasicMaterial::Params{}.map(brickTex));
    planeMaterial->side = threepp::Side::Double;   // 本版 Params 没有 side()，直接改字段
    auto plane = threepp::Mesh::create(planeGeometry, planeMaterial);
    plane->position.set(0.0f, 0.0f, -1.0f);
    scene->add(plane);

    // ---------- 一张画布 = 图片层 + 若干 FX 层 ----------
    std::shared_ptr<threepp::Texture> levTex;
    try {
        levTex = loader.load(std::filesystem::path("E:/MX/Data/lev.jpg"), threepp::ColorSpace::sRGB);
        std::printf("[纹理] lev.jpg 已加载\n");
    } catch (const std::exception& e) {
        std::printf("[纹理] lev.jpg 加载失败: %s\n", e.what());
        canvas.destroy();
        timeEndPeriod(1);
        return 1;
    }

    FxCanvas fxCanvas(1024, 576);

    // order 0：底图，铺满整张画布
    fxCanvas.addImage(levTex, threepp::Vector4(0.0f, 0.0f, 1.0f, 1.0f), 1.0f, 0);

    // order 1：梵高特效只占右半边——同一层的“位置”由 rect 决定，与前后顺序无关
    fxCanvas.addFx(FXCODE::g_fxSource1, threepp::Vector4(0.5f, 0.0f, 0.5f, 1.0f), 1.0f, 1);

    // order 2：局部小图叠在左下角，和 order 1 不重叠，互不遮挡
    fxCanvas.addImage(checkerTex, threepp::Vector4(0.03f, 0.04f, 0.16f, 0.28f), 1.0f, 2);

    // order 3：雨滴是后处理型，需要读下层已合成好的画面（samplesPrevious）
    fxCanvas.addFx(FXCODE::g_fxSource2, threepp::Vector4(0.0f, 0.0f, 1.0f, 1.0f), 1.0f, 3, true);

    // 可见面片：map 指向画布的最终纹理；后处理层每帧会交换 front/back，
    // 所以每帧都要重绑一次 map，不能只在启动时绑一次
    auto fxPlaneMaterial = threepp::ShaderMaterial::create();
    fxPlaneMaterial->vertexShader   = kFxPlaneVertexShader;
    fxPlaneMaterial->fragmentShader = kFxImageFragmentShader;
    fxPlaneMaterial->uniforms["map"]       = threepp::Uniform(fxCanvas.texture());
    fxPlaneMaterial->uniforms["fxOpacity"] = threepp::Uniform(1.0f);

    auto fxPlane = threepp::Mesh::create(threepp::PlaneGeometry::create(3.2f, 1.8f), fxPlaneMaterial);
    fxPlane->position.set(0.0f, 2.5f, 0.0f);
    scene->add(fxPlane);

    // ---------- 自转立方体：其中两个面贴视频 ----------
    // 面板里可在两个测试视频间切换；MATE_VIDEO 覆盖第一个路径；
    // MATE_VIDEO_SYNC=1 -> 解码留在渲染线程；MATE_VIDEO_WIDTH=<宽> 指定输出宽度（默认 1280）
    const char* envVideo = std::getenv("MATE_VIDEO");
    std::vector<std::string> videoPaths = {
        envVideo ? envVideo : "E:/MX/Data/tvb1.mp4",//zutian
        "E:/MX/Data/zutian.mp4",
    };
    int videoIndex = 0;
    const int videoWidth = std::getenv("MATE_VIDEO_WIDTH")
            ? std::atoi(std::getenv("MATE_VIDEO_WIDTH"))
            : 1280;
    mate::VideoSource video;
    const mate::VideoSource::Mode videoMode = std::getenv("MATE_VIDEO_SYNC")
            ? mate::VideoSource::Mode::Sync
            : mate::VideoSource::Mode::Threaded;

    // 3D 音源：音源位置 = 立方体，听者位置/朝向 = 相机；引擎起不来就静音继续
    mate::SpatialVideoAudio audio3d;
    float audioVolume  = 0.8f;
    bool  audioSpatial = true;
    if (audio3d.init()) {
        audio3d.setVolume(audioVolume);
        audio3d.setSpatialized(audioSpatial);
    } else {
        std::printf("[音频] 引擎初始化失败：%s（本次静音）\n", audio3d.error());
    }

    // 三个 R8 平面纹理（Y 全尺寸，U/V 各 1/2），上传量只有 RGBA 的 3/8
    auto makePlaneTex = [](int w, int h) {
        auto tex = threepp::Texture::create(threepp::Image(
                std::vector<unsigned char>((size_t)w * h, 0), (unsigned)w, (unsigned)h));
        tex->format = threepp::Format::Red;
        tex->type = threepp::Type::UnsignedByte;
        tex->unpackAlignment = 1;            // R8 行宽不一定 4 字节对齐
        tex->generateMipmaps = false;        // 每帧都变，mipmap 没有意义
        tex->minFilter = threepp::Filter::Linear;
        tex->magFilter = threepp::Filter::Linear;
        return tex;
    };

    auto faceVideo = threepp::ShaderMaterial::create();
    faceVideo->vertexShader   = kYuvVertexShader;
    faceVideo->fragmentShader = kYuvFragmentShader;

    // 两个视频分辨率可能不同，切换时按新平面尺寸重建纹理并重绑 uniform
    std::shared_ptr<threepp::Texture> videoPlanes[3];
    auto applyVideo = [&](int index) {
        const std::string& path = videoPaths[index];
        audio3d.detach();   // 先摘音源，避免音频线程读正在关闭的队列
        video.close();
        for (int p = 0; p < 3; ++p) {
            videoPlanes[p] = makePlaneTex(1, 1);
            videoPlanes[p]->image().data<unsigned char>()[0] = 128;   // 灰占位
        }
        if (!video.open(path, videoMode, videoWidth)) {
            std::printf("[视频] %s 打开失败，视频面回退成 YUV 占位\n", path.c_str());
        } else {
            std::printf("[视频] %s 已打开，输出 %dx%d（%s），模式=%s\n", path.c_str(),
                        video.width(), video.height(),
                        video.isYuv() ? "YUV420P" : "RGBA",
                        videoMode == mate::VideoSource::Mode::Sync ? "同步" : "独立线程");
            for (int p = 0; p < 3; ++p) {
                videoPlanes[p] = makePlaneTex(video.planeWidth(p), video.planeHeight(p));
            }
            if (!audio3d.attach(video)) {
                std::printf("[音频] 挂载音轨失败：%s\n", audio3d.error());
            }
        }
        faceVideo->uniforms["yTex"] = threepp::Uniform(videoPlanes[0].get());
        faceVideo->uniforms["uTex"] = threepp::Uniform(videoPlanes[1].get());
        faceVideo->uniforms["vTex"] = threepp::Uniform(videoPlanes[2].get());
        faceVideo->uniformsNeedUpdate = true;
    };
    applyVideo(videoIndex);

    auto facePlain = threepp::MeshBasicMaterial::create();
    facePlain->map = crateTex;

    // BoxGeometry 带 6 个 group，materialIndex 顺序固定为 +x,-x,+y,-y,+z,-z，
    // 所以材质数组的第 4、5 项就是 +z / -z 两个面 —— 只有这两面贴视频，
    // 其余四个面贴 crate。同一个材质实例可以同时给多个 group 用。
    std::vector<std::shared_ptr<threepp::Material>> cubeMats = {
        facePlain, facePlain, facePlain, facePlain, faceVideo, faceVideo
    };

    auto videoCube = threepp::Mesh::create(
            threepp::BoxGeometry::create(1.0f, 1.0f, 1.0f), cubeMats);
    videoCube->position.set(2.4f, 0.2f, 0.4f);
    scene->add(videoCube);

    // Reflector：2048x2048 反射纹理，水平铺在 y = -2.5
    threepp::Reflector::Options opt;
    opt.clipBias = 0.003f;
    opt.color = 0x777777;
    opt.textureWidth = 1024 * 2;
    opt.textureHeight = 1024 * 2;
    auto reflector = threepp::Reflector::create(threepp::PlaneGeometry::create(10, 10), opt);
    reflector->rotateX(threepp::math::degToRad(-90));
    reflector->position.set(0.0f, -2.5f, 0.0f);
    scene->add(reflector);

    canvas.onWindowResize([&](threepp::WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(std::pair<int, int>{ size.width(), size.height() });
        glViewport(0, 0, size.width(), size.height());
        std::printf("[窗口] 尺寸 %dx%d\n", size.width(), size.height());
    });

    auto last = std::chrono::steady_clock::now();
    double fpsTimer = 0.0;
    int    fpsFrames = 0;
    float  fpsWorstDt = 0.0f;   // 一个统计窗口里最慢的一帧（衡量卡顿，比平均 fps 更真实）

    while (canvas.pumpEvents()) {
        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;   // 拖动/卡顿后避免大跳变

        box->rotation.y += 0.5f * dt;
        sphere->rotation.x += 0.5f * dt;

        controls->update();

        // 立方体绕自身中心自转（BoxGeometry 以原点为中心，position 就是它的中心）
        videoCube->rotation.y += 0.7f * dt;

        // 音源 = 立方体中心，听者 = 相机：转视角、拉远拉近都能听出 3D 变化
        threepp::Vector3 camDir;
        camera->getWorldDirection(camDir);
        audio3d.setListener(camera->position.x, camera->position.y, camera->position.z,
                            camDir.x, camDir.y, camDir.z);
        audio3d.setSourcePosition(videoCube->position.x, videoCube->position.y,
                                  videoCube->position.z);
        audio3d.update();
        if (video.advance(dt)) {
            // 帧源给的是 GL 行序、按平面紧凑排列，逐平面拷进 threepp 纹理再标脏
            const int planes = video.isYuv() ? 3 : 1;
            for (int p = 0; p < planes; ++p) {
                auto& buf = videoPlanes[p]->image().data<unsigned char>();
                std::memcpy(buf.data(), video.plane(p), buf.size());
                videoPlanes[p]->needsUpdate();
            }
        }

        // 画布合成必须在主场景之前；render 内部结束时会自己切回屏幕
        fxCanvas.update(dt);
        fxCanvas.render(*renderer);
        fxPlaneMaterial->uniforms["map"] = threepp::Uniform(fxCanvas.texture());
        fxPlaneMaterial->uniformsNeedUpdate = true;

        // ImGui 面板：切换立方体上的测试视频
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("matex2 视频 / 音频");
        for (int i = 0; i < (int)videoPaths.size(); ++i) {
            if (ImGui::RadioButton(videoPaths[i].c_str(), videoIndex == i) && videoIndex != i) {
                videoIndex = i;
                applyVideo(videoIndex);
            }
        }
        ImGui::Separator();
        if (video.isOpen()) {
            const mate::VideoSource::Stats vs = video.stats();
            ImGui::Text("输出 %dx%d  %s", video.width(), video.height(),
                        video.isYuv() ? "YUV420P" : "RGBA");
            ImGui::Text("解码 %.2f ms  已解 %d 帧 / 已显示 %d 帧",
                        vs.decodeMs, vs.decoded, vs.presented);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "视频未打开（灰色占位）");
        }
        ImGui::Separator();
        ImGui::Text("音频（miniaudio 3D）");
        if (ImGui::SliderFloat("音量", &audioVolume, 0.0f, 1.0f, "%.2f")) {
            audio3d.setVolume(audioVolume);
        }
        if (ImGui::Checkbox("3D 空间化", &audioSpatial)) {
            audio3d.setSpatialized(audioSpatial);
        }
        if (audio3d.active()) {
            ImGui::Text("听者—音源 %.2f 单位", audio3d.distance());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "音轨未挂载%s%s",
                               audio3d.error()[0] ? "：" : "", audio3d.error());
        }
        ImGui::End();
        ImGui::Render();

        renderer->clear();
        renderer->render(*scene, *camera);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        canvas.swapBuffers();

        fpsFrames++;
        fpsTimer += dt;
        if (dt > fpsWorstDt) fpsWorstDt = dt;
        if (fpsTimer >= 2.0) {
            const mate::VideoSource::Stats vs = video.stats();
            std::printf("[帧率] %.1f fps（%dx%d）最慢帧 %.2f ms  视频 %dx%d 解码 %.1f ms / 已解 %d 帧 / 已显示 %d 帧\n",
                        fpsFrames / fpsTimer, canvas.width(), canvas.height(),
                        fpsWorstDt * 1000.0f, video.width(), video.height(),
                        vs.decodeMs, vs.decoded, vs.presented);
            fpsTimer = 0.0;
            fpsFrames = 0;
            fpsWorstDt = 0.0f;
        }
    }

    audio3d.shutdown();   // 先停音频，再拆视频/纹理

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    canvas.destroy();
    timeEndPeriod(1);
    std::printf("[退出] matex2 正常结束\n");
    return 0;
}
