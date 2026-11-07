/****************************************************************************
 * 标题: matex2 - threepp 学习沙箱
 * 功能: 复刻 ThreeppVSTest 的 01_intro/11_texture2d 示例：
 *       TextureLoader 贴图 + Sphere/Box/Plane + Reflector 实时镜面 + OrbitControls；
 *       在此基础上按 matex0 的写法补了 glTF 模型（含动画片段）、三盏灯、
 *       桌面壁纸挂载（WorkerW/Progman + 全局鼠标钩子转发）与 ImGui 面板。
 * 说明: 与 matex0 一样不链接 GLFW，用 Win32 + WGL 自建 OpenGL 4.5 上下文；
 *       渲染交给 threepp::GLRenderer(size)，事件转给 PeripheralsEventSource。
 *       走 console 子系统，日志直接 printf 到控制台。
 *
 * 依赖: ../../3rd/lib/threepp_mt.lib（/MT）、WS2_32、matex.rc（仅窗口图标）
 * 资源: Data/textures/{checker.png, crate.gif, brick_bump.jpg}、Data/models/*.glb|gltf
 *
 * 操作: 左键旋转 / 右键平移 / 滚轮缩放 / ESC 退出
 *       F5 壁纸模式 / F6 面板 / F8 壁纸交互 / F9 复位视角 / F12 退出
 ****************************************************************************/

#define NOMINMAX

#include <windows.h>
#include <mmsystem.h>

#include <glad/glad.h>

#include "threepp/animation/AnimationMixer.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/objects/Group.hpp"
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
#include "threepp/math/Matrix4.hpp"
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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
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

/*==================== 日志（无控制台，写进环形队列给 ImGui 日志窗口显示） ====================*/
static constexpr size_t kLogMaxLines = 200;
static std::deque<std::string> g_logLines;
static bool g_showLog = true;

static void mateLog(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // 老代码里的格式串带 '\n'（给控制台用），转成 IMGUI 单行时去掉
    const size_t n = std::strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
    if (g_logLines.size() >= kLogMaxLines) g_logLines.pop_front();
    g_logLines.emplace_back(buf);
}

/*==================== 壁纸模式 / 交互状态 ====================*/
// 热键：F5 壁纸切换、F6 面板、F8 交互开关、F9 复位视角、F12 退出
enum HotkeyId {
    kHotkeyWallpaper = 1001,
    kHotkeyPanel     = 1002,
    kHotkeyInteract  = 1003,
    kHotkeyResetView = 1004,
    kHotkeyQuit      = 1005,
};

struct WallpaperState {
    bool wallpaper = false;           // 已挂到桌面壁纸层
    bool interactive = true;          // 壁纸模式下是否响应模型上的鼠标
    bool dragging = false;            // 正在拖拽模型区域（左键）
    bool rmbDown = false;             // 右键平移进行中
    bool wantToggle = false;          // 下面几个都由主循环消费，避免在 WndProc 里做重活
    bool wantRemount = false;
    bool wantResetView = false;
    bool wantQuit = false;
    bool showPanel = true;
    // 进壁纸模式前的窗口原始状态，退出时原样还原
    LONG_PTR savedStyle   = 0;
    LONG_PTR savedExStyle = 0;
    RECT     savedRect    = { 0, 0, 0, 0 };
};
static WallpaperState g_wp;

static threepp::PerspectiveCamera* g_camera = nullptr;   // 壁纸钩子用它算屏幕投影
static threepp::OrbitControls*    g_controls = nullptr;
static float g_cameraBaseDistance = 5.0f;
static threepp::Vector3 g_interactCenter{0.0f, 0.0f, 0.0f};
static float g_interactRadius = 1.0f;
static HHOOK g_mouseHook = nullptr;

/*==================== 模型拖动（普通窗口下的左键拖拽） ====================*/
static threepp::Group*  g_modelRoot = nullptr;   // 当前模型根节点，拖动直接改 position
static threepp::Vector3 g_modelAnchor(0.0f, 0.25f, 1.2f);   // 包围盒中心的世界坐标
static bool  g_modelDragEnabled = true;
static bool g_hotkeysRegistered = false;   // 注册失败时退回 WM_KEYDOWN（只在窗口有焦点时管用）
static bool  g_modelDragging = false;
static POINT g_modelDragStart{};
static threepp::Vector3 g_modelDragOrigin;

static bool handleModelDrag(UINT msg, LPARAM lParam);   // 实现在交互投影函数之后

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

    // 壁纸模式：全局鼠标钩子把事件注入 threepp 的监听器
    void feedMouseDown(int button, float x, float y);
    void feedMouseUp(int button, float x, float y);
    void feedMouseMove(float x, float y);
    void feedMouseWheel(float dy);
    void syncClientSize();

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
    if (g_canvas && !g_wp.wallpaper && !skipForImGui) {
        if (!handleModelDrag(msg, lParam)) g_canvas->handleMessage(msg, wParam, lParam);
    }

    switch (msg) {
    case WM_HOTKEY:
        switch (wParam) {
        case kHotkeyWallpaper: g_wp.wantToggle = true; break;
        case kHotkeyPanel:     g_wp.showPanel = !g_wp.showPanel; break;
        case kHotkeyInteract:
            g_wp.interactive = !g_wp.interactive;
            if (!g_wp.interactive) g_wp.dragging = false;
            break;
        case kHotkeyResetView: g_wp.wantResetView = true; break;
        case kHotkeyQuit:      g_wp.wantQuit = true; break;
        default: break;
        }
        return 0;

    case WM_NCHITTEST:
        // 壁纸模式：点击一律透传给桌面图标；模型区域的交互走全局鼠标钩子
        if (g_wp.wallpaper) return HTTRANSPARENT;
        break;

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (g_wp.wallpaper) g_wp.wantRemount = true;   // 分辨率/Explorer 变化后重挂
        break;

    case WM_SIZE:
        if (g_canvas) g_canvas->resize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
        }
        // 热键被别家占用（注册失败）时，窗口有焦点也能操作
        if (!g_hotkeysRegistered) {
            switch (wParam) {
            case VK_F5:  g_wp.wantToggle = true; return 0;
            case VK_F6:  g_wp.showPanel = !g_wp.showPanel; return 0;
            case VK_F8:  g_wp.interactive = !g_wp.interactive; g_wp.dragging = false; return 0;
            case VK_F9:  g_wp.wantResetView = true; return 0;
            case VK_F12: g_wp.wantQuit = true; return 0;
            default: break;
            }
        }
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
        mateLog("[窗口] 注册窗口类失败: %lu\n", GetLastError());
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
        mateLog("[窗口] CreateWindowEx 失败: %lu\n", GetLastError());
        return false;
    }

    g_canvas = this;
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);

    if (!initGL(vsync)) return false;

    // 全局热键：壁纸模式下窗口没有焦点/被图标层盖住，只能靠系统热键操作
    g_hotkeysRegistered = true;
    auto regHotkey = [&](int id, UINT vk, const char* name) {
        if (!RegisterHotKey(hwnd_, id, MOD_NOREPEAT, vk)) {
            g_hotkeysRegistered = false;
            mateLog("[热键] %s 注册失败（错误 %lu，可能被其他程序占用）", name, GetLastError());
        }
    };
    regHotkey(kHotkeyWallpaper, VK_F5,  "F5 壁纸");
    regHotkey(kHotkeyPanel,     VK_F6,  "F6 面板");
    regHotkey(kHotkeyInteract,  VK_F8,  "F8 交互");
    regHotkey(kHotkeyResetView, VK_F9,  "F9 复位视角");
    regHotkey(kHotkeyQuit,      VK_F12, "F12 退出");
    if (g_hotkeysRegistered) mateLog("[热键] F5/F6/F8/F9/F12 注册成功");

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
        mateLog("[GL] GetDC 失败\n");
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
        mateLog("[GL] 像素格式设置失败\n");
        return false;
    }

    // 先建临时上下文，才能拿到 wglCreateContextAttribsARB
    HGLRC tempCtx = wglCreateContext(hdc_);
    if (!tempCtx) {
        mateLog("[GL] wglCreateContext 失败\n");
        return false;
    }
    if (!wglMakeCurrent(hdc_, tempCtx)) {
        mateLog("[GL] 临时上下文 current 失败\n");
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
            mateLog("[GL] %s 上下文不可用，尝试下一档\n", a.name);
        }
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (!ctx) {
        mateLog("[GL] 未能创建 3.3+ 上下文，回退 legacy\n");
        ctx = wglCreateContext(hdc_);
    }
    if (!ctx) {
        mateLog("[GL] 无法创建 OpenGL 上下文\n");
        return false;
    }

    hglrc_ = ctx;
    if (!wglMakeCurrent(hdc_, hglrc_)) {
        mateLog("[GL] 正式上下文 current 失败\n");
        return false;
    }

    // threepp 的 glad 全进程只加载一次，必须在最终上下文 current 之后
    try {
        threepp::loadGlad();
    } catch (const std::exception& e) {
        mateLog("[GL] glad 加载失败: %s\n", e.what());
        return false;
    }

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    mateLog("[GL] %s -> %d.%d | %s | %s\n", chosen.c_str(), (int)major, (int)minor,
                (const char*)glGetString(GL_RENDERER),
                (const char*)glGetString(GL_VERSION));

    PFN_wglSwapIntervalEXT swapInterval =
            (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
    if (swapInterval) {
        swapInterval(vsync ? 1 : 0);
        mateLog("[GL] 垂直同步: %s\n", vsync ? "开" : "关");
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

/*==================== 壁纸模式：事件注入 ====================*/
void Win32Canvas::feedMouseDown(int button, float x, float y) {
    mouseDown_[button] = true;
    onMousePressedEvent(button, threepp::Vector2(x, y), MouseAction::PRESS);
}

void Win32Canvas::feedMouseUp(int button, float x, float y) {
    mouseDown_[button] = false;
    onMousePressedEvent(button, threepp::Vector2(x, y), MouseAction::RELEASE);
}

void Win32Canvas::feedMouseMove(float x, float y) {
    onMouseMoveEvent(threepp::Vector2(x, y));
}

void Win32Canvas::feedMouseWheel(float dy) {
    onMouseWheelEvent(threepp::Vector2(0.0f, dy));
}

void Win32Canvas::syncClientSize() {
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hwnd_, &rc);
    const int w = (rc.right - rc.left) > 1 ? (int)(rc.right - rc.left) : 1;
    const int h = (rc.bottom - rc.top) > 1 ? (int)(rc.bottom - rc.top) : 1;
    resize(w, h);   // 触发外部回调：相机 aspect / renderer->setSize / glViewport
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
/*==================== 模型：扫描目录 ====================*/
struct ModelEntry {
    std::filesystem::path path;
    std::string label;
};

static std::vector<ModelEntry> scanModels(const std::filesystem::path& root) {
    std::vector<ModelEntry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return out;

    for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (ext != ".glb" && ext != ".gltf") continue;

        ModelEntry e;
        e.path  = it->path();
        e.label = std::filesystem::relative(e.path, root, ec).generic_string();
        if (e.label.empty()) e.label = e.path.filename().string();
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const ModelEntry& a, const ModelEntry& b) { return a.label < b.label; });
    return out;
}

// 优先 exe 同级的 models（拷走 exe + 目录即可独立运行），没有就退回开发机绝对路径
static std::filesystem::path resolveModelDir() {
    std::error_code ec;
    std::filesystem::path beside = exeDir() / "models";
    if (std::filesystem::is_directory(beside, ec)) return beside;
    beside = exeDir() / "Data" / "models";
    if (std::filesystem::is_directory(beside, ec)) return beside;
    return "e:/MX/Data/models";
}

/*==================== 桌面壁纸挂载 ====================*/
struct ScreenRect { int x = 0, y = 0, w = 0, h = 0; };

static ScreenRect virtualScreenRect() {
    ScreenRect r;
    r.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (r.w <= 0 || r.h <= 0) {
        r.x = r.y = 0;
        r.w = GetSystemMetrics(SM_CXSCREEN);
        r.h = GetSystemMetrics(SM_CYSCREEN);
    }
    return r;
}

enum class DesktopStructure { Unknown, Legacy, Modern };

static const char* structureName(DesktopStructure s) {
    switch (s) {
    case DesktopStructure::Legacy: return "Legacy（DefView 在 WorkerW）";
    case DesktopStructure::Modern: return "Modern（DefView 在 Progman）";
    default: return "Unknown";
    }
}

static int countTopLevelWorkerW() {
    int n = 0;
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr) ++n;
    return n;
}

// 桌面壁纸层的两张牌：parent 是我们 SetParent 的目标，icons 是图标层（永远在最上面）
struct WallpaperLayers {
    HWND parent = nullptr;
    HWND icons = nullptr;
    DesktopStructure structure = DesktopStructure::Unknown;
    bool win11NoWallpaper = false;   // Win11：Progman 直接带 DefView，没有独立壁纸层
};

// 按 MX 主程序（MO/oapp.h enterWallpaperMode）的成功做法：
//   1) 先给 Progman 发 0x052C，让 Explorer 建出/露出壁纸层 WorkerW（并等它建好）；
//   2) 取壁纸层父窗口 + 图标层窗口，挂上以后把自己排在图标层后面，图标才不会被盖住。
static WallpaperLayers detectDesktopLayers() {
    WallpaperLayers L;
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return L;

    DWORD_PTR unused = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &unused);
    Sleep(100);

    if (HWND def = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
        // Win11 无壁纸模式：没有独立壁纸层，挂 Progman，排在 DefView 后面
        L.win11NoWallpaper = true;
        L.structure = DesktopStructure::Modern;
        L.parent = progman;
        L.icons = def;
        return L;
    }

    HWND icons = nullptr;
    HWND wallpaper = nullptr;
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr) {
        if (FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr)) {
            icons = w;
        } else if (!wallpaper && IsWindowVisible(w)) {
            wallpaper = w;   // 夹在一堆隐藏的 136x39 WorkerW 里，只认可见的那个
        }
    }
    L.icons = icons;
    if (wallpaper) {
        L.parent = wallpaper;
        L.structure = DesktopStructure::Modern;
    } else if (icons) {
        // Legacy：带 DefView 的 WorkerW 的下一个 WorkerW 才是壁纸层
        L.parent = FindWindowExW(nullptr, icons, L"WorkerW", nullptr);
        if (!L.parent) L.parent = icons;
        L.structure = DesktopStructure::Legacy;
    } else {
        L.parent = progman;   // 什么都没找到就挂 Progman 试试
    }
    return L;
}

static void restoreWindowFromWallpaper(HWND hwnd) {
    SetParent(hwnd, nullptr);
    SetWindowLongPtrW(hwnd, GWL_STYLE,
                      g_wp.savedStyle ? g_wp.savedStyle : (WS_OVERLAPPEDWINDOW | WS_VISIBLE));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, g_wp.savedExStyle);
    const RECT rc = g_wp.savedRect;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    SetWindowPos(hwnd, HWND_TOP,
                 w > 0 ? rc.left : 40, h > 0 ? rc.top : 40,
                 w > 0 ? w : kDefaultWidth, h > 0 ? h : kDefaultHeight,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_NORMAL);
}

static bool mountWallpaper() {
    if (!g_canvas || !IsWindow(g_canvas->hwnd())) return false;
    HWND hwnd = g_canvas->hwnd();
    WallpaperLayers L = detectDesktopLayers();
    if (!L.parent) {
        mateLog("[壁纸] 找不到 Progman / WorkerW（WorkerW 数=%d），保持窗口模式",
                countTopLevelWorkerW());
        return false;
    }

    // 记下原样式/位置，退出壁纸时原样还原（重挂时不能覆盖，否则丢掉原始值）
    if (!g_wp.wallpaper) {
        g_wp.savedStyle   = GetWindowLongPtrW(hwnd, GWL_STYLE);
        g_wp.savedExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &g_wp.savedRect);
    }

    LONG_PTR style = g_wp.savedStyle | WS_CHILD | WS_VISIBLE;
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_POPUP);
    LONG_PTR exStyle = (g_wp.savedExStyle | WS_EX_LAYERED) & ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    // 不透明（alpha=255），只是让桌面能合成这个分层子窗口
    SetLayeredWindowAttributes(hwnd, 0, 0xFF, LWA_ALPHA);

    SetLastError(0);
    if (!SetParent(hwnd, L.parent)) {
        mateLog("[壁纸] SetParent 失败（错误 %lu），保持窗口模式", GetLastError());
        restoreWindowFromWallpaper(hwnd);
        return false;
    }

    const ScreenRect r = virtualScreenRect();
    SetWindowPos(hwnd, nullptr, r.x, r.y, r.w, r.h, SWP_NOACTIVATE | SWP_FRAMECHANGED);
    // 排到图标层后面：桌面图标是不透明层，必须在它下面画才不会被盖住
    if (L.icons && L.icons != L.parent) {
        SetWindowPos(hwnd, L.icons, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    ShowWindow(L.parent, SW_SHOW);   // 0x052C 刚造出来的壁纸层可能还藏着

    g_wp.wallpaper = true;
    g_wp.dragging = false;
    g_wp.rmbDown = false;
    g_canvas->syncClientSize();
    mateLog("[壁纸] 已挂到 %s（WorkerW 数=%d，图标层=%p）", structureName(L.structure),
            countTopLevelWorkerW(), (void*)L.icons);
    return true;
}

static void unmountWallpaper() {
    if (!g_canvas) return;
    HWND hwnd = g_canvas->hwnd();
    if (!hwnd) return;

    restoreWindowFromWallpaper(hwnd);
    g_wp.wallpaper = false;
    g_wp.dragging = false;
    g_wp.rmbDown = false;
    g_canvas->syncClientSize();
    SetForegroundWindow(hwnd);   // 从桌面层回来时把窗口抬到前台，免得看着像"没反应"
    mateLog("[壁纸] 已退回普通窗口");
}

/*==================== 壁纸交互：屏幕投影判断 ====================*/
// 客户区坐标是否落在模型的投影圆内（屏幕坐标版本先 ScreenToClient 再调它）
static bool interactRegionContainsClient(const POINT& c) {
    if (!g_camera || !g_canvas) return false;
    HWND hwnd = g_canvas->hwnd();
    if (!hwnd) return false;

    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hwnd, &rc);
    const float cw = (float)(rc.right - rc.left);
    const float ch = (float)(rc.bottom - rc.top);
    if (cw <= 1.0f || ch <= 1.0f) return false;

    // 本版 Matrix4 没有 operator*，手动做齐次投影（elements 是列主序）
    threepp::Matrix4 viewProj;
    viewProj.multiplyMatrices(g_camera->projectionMatrix, g_camera->matrixWorldInverse);
    const float* m = viewProj.elements.data();
    const float x = g_interactCenter.x, y = g_interactCenter.y, z = g_interactCenter.z;
    const float clipX = m[0] * x + m[4] * y + m[8]  * z + m[12];
    const float clipY = m[1] * x + m[5] * y + m[9]  * z + m[13];
    const float clipW = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (clipW <= 0.0001f) return false;   // 在相机后面

    const float sx = (clipX / clipW * 0.5f + 0.5f) * cw;
    const float sy = (1.0f - (clipY / clipW * 0.5f + 0.5f)) * ch;

    const float dist = g_controls ? g_controls->getDistance() : g_cameraBaseDistance;
    const float halfFovTan = std::tan(threepp::math::degToRad(g_camera->fov * 0.5f));
    const float pxPerUnit = (ch * 0.5f) / std::max(0.001f, dist * halfFovTan);
    const float radiusPx = std::max(24.0f, g_interactRadius * pxPerUnit);

    const float dx = (float)c.x - sx;
    const float dy = (float)c.y - sy;
    return dx * dx + dy * dy <= radiusPx * radiusPx;
}

// 返回屏幕点是否落在模型的投影圆内，同时输出窗口客户区坐标（给 OrbitControls 用）
static bool cursorOverInteractRegion(const POINT& screenPt, float* outX, float* outY) {
    if (!g_camera || !g_canvas) return false;
    HWND hwnd = g_canvas->hwnd();
    if (!hwnd) return false;

    POINT c = screenPt;
    ScreenToClient(hwnd, &c);

    if (outX) *outX = (float)c.x;
    if (outY) *outY = (float)c.y;
    return interactRegionContainsClient(c);
}

// 普通窗口下的模型拖动：只在投影圆内按下左键时接管，其余事件仍给 OrbitControls
static bool handleModelDrag(UINT msg, LPARAM lParam) {
    if (!g_modelDragEnabled || !g_modelRoot || !g_camera || !g_canvas) return false;

    const POINT pt{ (LONG)(short)LOWORD(lParam), (LONG)(short)HIWORD(lParam) };
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (!interactRegionContainsClient(pt)) return false;
        g_modelDragging = true;
        g_modelDragStart = pt;
        g_modelDragOrigin.set(g_modelRoot->position.x, g_modelRoot->position.y,
                              g_modelRoot->position.z);
        return true;

    case WM_MOUSEMOVE: {
        if (!g_modelDragging) return false;
        const float dxPx = (float)(pt.x - g_modelDragStart.x);
        const float dyPx = (float)(pt.y - g_modelDragStart.y);

        // 屏幕像素 → 世界单位：把模型所在的深度当画布，fx/fy 就是相机右/上方向
        const float dist = g_controls ? g_controls->getDistance() : g_cameraBaseDistance;
        const float halfFovTan = std::tan(threepp::math::degToRad(g_camera->fov * 0.5f));
        const float pxPerUnit = (g_canvas->height() * 0.5f) / std::max(0.001f, dist * halfFovTan);

        threepp::Vector3 dir, right, up;
        g_camera->getWorldDirection(dir);
        right.crossVectors(dir, threepp::Vector3(0.0f, 1.0f, 0.0f)).normalize();
        up.crossVectors(right, dir).normalize();

        threepp::Vector3 pos = g_modelDragOrigin
                + right * (dxPx / pxPerUnit)
                + up * (-dyPx / pxPerUnit);
        g_modelRoot->position.set(pos.x, pos.y, pos.z);
        g_interactCenter.set(pos.x, pos.y, pos.z);
        g_modelAnchor.set(pos.x, pos.y, pos.z);
        return true;
    }

    case WM_LBUTTONUP:
        if (!g_modelDragging) return false;
        g_modelDragging = false;
        return true;

    default:
        return false;
    }
}

static LRESULT CALLBACK wallpaperMouseHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION || !g_wp.wallpaper || !g_wp.interactive || !g_canvas) {
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
    }

    const MSLLHOOKSTRUCT* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    float cx = 0.0f, cy = 0.0f;
    const bool over = cursorOverInteractRegion(ms->pt, &cx, &cy);

    switch (wParam) {
    case WM_MOUSEMOVE:
        // 一旦在模型上按下，拖动期间即使移出投影圆也继续接收
        if (g_wp.dragging || g_wp.rmbDown) {
            g_canvas->feedMouseMove(cx, cy);
            return 1;
        }
        break;

    case WM_LBUTTONDOWN:
        if (over) {
            g_wp.dragging = true;
            g_canvas->feedMouseMove(cx, cy);
            g_canvas->feedMouseDown(0, cx, cy);
            return 1;   // 吃掉，不让桌面图标被点到
        }
        break;

    case WM_LBUTTONUP:
        if (g_wp.dragging) {
            g_wp.dragging = false;
            g_canvas->feedMouseUp(0, cx, cy);
            return 1;
        }
        break;

    case WM_RBUTTONDOWN:
        if (over && !g_wp.dragging) {
            g_wp.rmbDown = true;
            g_canvas->feedMouseMove(cx, cy);
            g_canvas->feedMouseDown(1, cx, cy);
            return 1;
        }
        break;

    case WM_RBUTTONUP:
        if (g_wp.rmbDown) {
            g_wp.rmbDown = false;
            g_canvas->feedMouseUp(1, cx, cy);
            return 1;
        }
        break;

    case WM_MOUSEWHEEL:
        if (over || g_wp.dragging || g_wp.rmbDown) {
            const float dy = (float)((short)HIWORD(ms->mouseData)) / (float)WHEEL_DELTA;
            g_canvas->feedMouseWheel(dy);
            return 1;
        }
        break;

    default:
        break;
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

static void setWallpaperMouseHook(bool on) {
    if (on && !g_mouseHook) {
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, wallpaperMouseHook,
                                        GetModuleHandleW(nullptr), 0);
        if (!g_mouseHook) mateLog("[壁纸] 鼠标钩子安装失败: %lu\n", GetLastError());
    } else if (!on && g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

int main() {    // 源码字符串是 UTF-8（/utf-8），控制台默认 GBK 代码页会把中文显示成乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
    timeBeginPeriod(1);

    mateLog("========================================\n");
    mateLog(" matex2 - threepp 模型 / 动画 / 灯光 / 壁纸 学习沙箱\n");
    mateLog(" 左键旋转 / 右键平移 / 滚轮缩放 / ESC 退出\n");
    mateLog(" F5 壁纸 / F6 面板 / F8 交互 / F9 复位视角 / F12 退出\n");
    mateLog("========================================\n");

    Win32Canvas canvas;
    if (!canvas.create(L"Mate x2 - threepp 模型 / 视频", kDefaultWidth, kDefaultHeight, true)) {
        mateLog("[启动] 窗口/GL 初始化失败\n");
        timeEndPeriod(1);
        return 1;
    }

    std::unique_ptr<threepp::GLRenderer> renderer;
    try {
        renderer = std::make_unique<threepp::GLRenderer>(
                std::pair<int, int>{ canvas.width(), canvas.height() });
    } catch (const std::exception& e) {
        mateLog("[threepp] 渲染器创建失败: %s\n", e.what());
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
    // ChineseSimplifiedCommon 只含 2500 常用字，"频" / "帧" 这类面板必用的字不在其中，
    // 会渲染成方块；把面板实际用到的字补进范围
    static ImVector<ImWchar> glyphRanges;
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(imguiIo.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddText("模型动画灯光视频音频播放该不含环境主补未打开灰色占位输出解码已解帧"
                        "显示音量空间化听者源单位轨挂载壁纸桌面模式区域响应鼠标交互"
                        "复位退出面板拖动左键位置秒日志清空共行到可用的层退化全屏置底"
                        "普通窗口视角热键注册失败可能被其他程序占用错误场景只内置几何体"
                        "加载目录个就位半径纹理尺寸率最慢线程正常结束切换"
                        "缓冲欠载回调周期最大间隔后次累计");
        builder.BuildRanges(&glyphRanges);
    }
    if (std::filesystem::exists("C:/Windows/Fonts/msyh.ttc")) {
        imguiIo.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 18.0f, nullptr,
                                          glyphRanges.Data);
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
        mateLog("[相机] OrbitControls 创建失败: %s\n", e.what());
        canvas.destroy();
        timeEndPeriod(1);
        return 1;
    }

    // 壁纸钩子与交互判定要拿到相机/控件；模型换位时这两个全局量跟着更新
    g_camera  = camera.get();
    g_controls = controls.get();
    g_cameraBaseDistance = camera->position.z;

    // ---------- 灯光：glTF 的 PBR 材质没灯就是黑的，强度留给面板调 ----------
    float ambientIntensity = 0.45f;
    float keyIntensity     = 2.2f;
    float fillIntensity    = 0.8f;
    auto ambientLight = threepp::AmbientLight::create(0xffffff, ambientIntensity);
    scene->add(ambientLight);
    auto keyLight = threepp::DirectionalLight::create(0xffffff, keyIntensity);
    keyLight->position.set(2.4f, 3.6f, 3.2f);
    scene->add(keyLight);
    auto fillLight = threepp::DirectionalLight::create(0x8899ff, fillIntensity);
    fillLight->position.set(-3.0f, -1.5f, -2.0f);
    scene->add(fillLight);

    std::filesystem::path texDir = exeDir() / "Data" / "textures";
    threepp::TextureLoader loader;
    std::shared_ptr<threepp::Texture> checkerTex, crateTex, brickTex;
    try {
        checkerTex = loader.load(texDir / "checker.png", threepp::ColorSpace::sRGB);
        crateTex   = loader.load(texDir / "crate.gif", threepp::ColorSpace::sRGB);
        brickTex   = loader.load(texDir / "brick_bump.jpg", threepp::ColorSpace::sRGB);
        mateLog("[纹理] checker.png / crate.gif / brick_bump.jpg 已加载\n");
    } catch (const std::exception& e) {
        mateLog("[纹理] 加载失败: %s（检查 %s）\n", e.what(), texDir.string().c_str());
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
        mateLog("[纹理] lev.jpg 已加载\n");
    } catch (const std::exception& e) {
        mateLog("[纹理] lev.jpg 加载失败: %s\n", e.what());
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
        mateLog("[音频] 引擎初始化失败：%s（本次静音）\n", audio3d.error());
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
            mateLog("[视频] %s 打开失败，视频面回退成 YUV 占位\n", path.c_str());
        } else {
            mateLog("[视频] %s 已打开，输出 %dx%d（%s），模式=%s\n", path.c_str(),
                        video.width(), video.height(),
                        video.isYuv() ? "YUV420P" : "RGBA",
                        videoMode == mate::VideoSource::Mode::Sync ? "同步" : "独立线程");
            for (int p = 0; p < 3; ++p) {
                videoPlanes[p] = makePlaneTex(video.planeWidth(p), video.planeHeight(p));
            }
            if (!audio3d.attach(video)) {
                mateLog("[音频] 挂载音轨失败：%s\n", audio3d.error());
            } else {
                mateLog("[音频] 已挂载（后端 %s）", audio3d.backendName());
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

    /* ---------------- 模型 + 动画 ---------------- */
    const std::filesystem::path modelDir = resolveModelDir();
    std::vector<ModelEntry> models = scanModels(modelDir);
    mateLog("[模型] 目录 %s，共 %d 个\n", modelDir.string().c_str(), (int)models.size());

    std::shared_ptr<threepp::Group> modelRoot;
    std::unique_ptr<threepp::AnimationMixer> modelMixer;
    std::vector<std::shared_ptr<threepp::AnimationClip>> modelClips;
    int  modelIndex = 0;
    int  clipIndex  = 0;
    bool animPlaying = false;

    // 摆位点默认在视野正中（g_modelAnchor，面板可开关左键拖动来调整）
    // 换片段/开关播放都走这里：先停掉全部 action，再按当前状态重挂
    auto applyClip = [&] {
        if (!modelMixer) return;
        modelMixer->stopAllAction();
        if (clipIndex < 0 || clipIndex >= (int)modelClips.size() || !animPlaying) return;
        if (auto* action = modelMixer->clipAction(modelClips[clipIndex])) {
            action->setLoop(threepp::Loop::Repeat, -1);
            action->reset().play();
        }
    };

    // 加载第 index 个模型：先加载成功再卸旧的，加载失败时旧模型留在场景里
    auto applyModel = [&](int index) {
        if (index < 0 || index >= (int)models.size()) return;
        const ModelEntry& entry = models[index];
        mateLog("[模型] 加载 %d/%d：%s\n", index + 1, (int)models.size(),
                    entry.label.c_str());

        threepp::GLTFLoader gltf;
        auto result = gltf.load(entry.path);
        if (!result || !result->scene) {
            mateLog("[模型] 加载失败：%s\n", entry.label.c_str());
            return;
        }

        // mixer 持有旧根节点引用，必须先拆再释放模型
        modelMixer.reset();
        modelClips.clear();
        g_modelRoot = nullptr;   // 拖动逻辑在此之前不能再碰旧节点
        if (modelRoot) modelRoot->removeFromParent();
        modelRoot.reset();

        modelRoot = result->scene;

        // 按包围盒把最大边缩到约 2.2，并把包围盒中心摆到 g_modelAnchor
        threepp::Box3 bounds;
        bounds.setFromObject(*modelRoot);
        float radius = 1.0f;
        if (!bounds.isEmpty()) {
            const threepp::Vector3 center = bounds.getCenter();
            const threepp::Vector3 size   = bounds.getSize();
            const float maxEdge = std::max({ size.x, size.y, size.z });
            const float scale   = maxEdge > 1e-4f ? 2.2f / maxEdge : 1.0f;
            modelRoot->scale.set(scale, scale, scale);
            modelRoot->position.set(g_modelAnchor.x - center.x * scale,
                                    g_modelAnchor.y - center.y * scale,
                                    g_modelAnchor.z - center.z * scale);
            radius = 0.5f * maxEdge * scale;
        } else {
            modelRoot->position.set(g_modelAnchor.x, g_modelAnchor.y, g_modelAnchor.z);
        }
        g_interactCenter.set(g_modelAnchor.x, g_modelAnchor.y, g_modelAnchor.z);
        g_interactRadius = std::max(0.4f, radius);
        scene->add(modelRoot);
        g_modelRoot = modelRoot.get();
        modelIndex = index;

        modelClips  = result->animations;
        clipIndex   = 0;
        animPlaying = !modelClips.empty();
        if (animPlaying) {
            modelMixer = std::make_unique<threepp::AnimationMixer>(*modelRoot);
            applyClip();
            std::string names;
            for (size_t i = 0; i < modelClips.size(); ++i) {
                if (i) names += " / ";
                names += modelClips[i]->name();
            }
            mateLog("[动画] 共 %d 段：%s\n", (int)modelClips.size(), names.c_str());
        } else {
            mateLog("[动画] 该模型不含动画\n");
        }
        mateLog("[模型] 已就位：%s（交互半径 %.2f）\n", entry.label.c_str(),
                    g_interactRadius);
    };

    // 拖动后一键回正中（只挪模型位置，不动相机）
    auto resetModelPosition = [&] {
        const threepp::Vector3 target(0.0f, 0.25f, 1.2f);
        const threepp::Vector3 delta = target - g_interactCenter;
        if (modelRoot) {
            const threepp::Vector3 p = modelRoot->position + delta;
            modelRoot->position.set(p.x, p.y, p.z);
        }
        g_interactCenter.set(target.x, target.y, target.z);
        g_modelAnchor.set(target.x, target.y, target.z);
    };

    if (!models.empty()) {
        applyModel(0);
    } else {
        mateLog("[模型] 目录里没有 .glb / .gltf，场景只有内置几何体\n");
    }

    canvas.onWindowResize([&](threepp::WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(std::pair<int, int>{ size.width(), size.height() });
        glViewport(0, 0, size.width(), size.height());
        mateLog("[窗口] 尺寸 %dx%d\n", size.width(), size.height());
    });

    auto last = std::chrono::steady_clock::now();
    double fpsTimer = 0.0;
    int    fpsFrames = 0;
    float  fpsWorstDt = 0.0f;   // 一个统计窗口里最慢的一帧（衡量卡顿，比平均 fps 更真实）

    while (canvas.pumpEvents() && !g_wp.wantQuit) {
        // 热键请求统一在主循环消费：SetParent / 盖钩子这些重活不能放在 WndProc 里做
        if (g_wp.wantToggle) {
            g_wp.wantToggle = false;
            if (g_wp.wallpaper) {
                setWallpaperMouseHook(false);
                unmountWallpaper();
            } else if (mountWallpaper()) {
                setWallpaperMouseHook(true);   // 挂上了才开全局鼠标钩子
            }
        }
        if (g_wp.wantRemount) {
            g_wp.wantRemount = false;
            if (g_wp.wallpaper) mountWallpaper();   // 换分辨率/Explorer 重启后重挂
        }
        if (g_wp.wantResetView) {
            g_wp.wantResetView = false;
            camera->position.set(0.0f, 0.0f, g_cameraBaseDistance);
            controls->target.set(0.0f, 0.0f, 0.0f);
            controls->update();
            mateLog("[壁纸] 视角已复位\n");
        }

        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;   // 拖动/卡顿后避免大跳变

        box->rotation.y += 0.5f * dt;
        sphere->rotation.x += 0.5f * dt;

        // glTF 自带动画：时间轴按真实帧间隔推进，暂停时不动
        if (animPlaying && modelMixer) modelMixer->update(dt);

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

        // ImGui 面板：模型 / 动画 / 灯光 / 视频 / 音频 / 壁纸
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        // 壁纸模式下窗口已经是桌面的一部分，面板没有意义，直接跳过
        if (!g_wp.wallpaper && g_wp.showPanel) {
            ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("matex2 模型 / 视频 / 音频");
            ImGui::Text("模型：%d 个（%s）", (int)models.size(), modelDir.string().c_str());
            if (!models.empty()) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##model", models[modelIndex].label.c_str())) {
                    for (int i = 0; i < (int)models.size(); ++i) {
                        const bool selected = (i == modelIndex);
                        if (ImGui::Selectable(models[i].label.c_str(), selected)) applyModel(i);
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            if (!modelClips.empty()) {
                char preview[192];
                std::snprintf(preview, sizeof(preview), "%s（%.1f s）",
                              modelClips[clipIndex]->name().c_str(),
                              modelClips[clipIndex]->getDuration());
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##clip", preview)) {
                    for (int i = 0; i < (int)modelClips.size(); ++i) {
                        char item[192];
                        std::snprintf(item, sizeof(item), "%s（%.1f s）",
                                      modelClips[i]->name().c_str(),
                                      modelClips[i]->getDuration());
                        if (ImGui::Selectable(item, clipIndex == i)) {
                            clipIndex = i;
                            applyClip();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Checkbox("播放动画", &animPlaying)) applyClip();
            } else {
                ImGui::TextDisabled("该模型不含动画");
            }
            ImGui::Checkbox("拖动模型（左键按住模型）", &g_modelDragEnabled);
            if (ImGui::Button("模型居中")) resetModelPosition();

            ImGui::Separator();
            ImGui::Text("灯光");
            if (ImGui::SliderFloat("环境光", &ambientIntensity, 0.0f, 2.0f, "%.2f"))
                ambientLight->intensity = ambientIntensity;
            if (ImGui::SliderFloat("主光", &keyIntensity, 0.0f, 5.0f, "%.2f"))
                keyLight->intensity = keyIntensity;
            if (ImGui::SliderFloat("补光", &fillIntensity, 0.0f, 5.0f, "%.2f"))
                fillLight->intensity = fillIntensity;

            ImGui::Separator();
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
            ImGui::Text("音频（miniaudio 3D，%s）", audio3d.backendName());
            if (ImGui::SliderFloat("音量", &audioVolume, 0.0f, 1.0f, "%.2f")) {
                audio3d.setVolume(audioVolume);
            }
            if (ImGui::Checkbox("3D 空间化", &audioSpatial)) {
                audio3d.setSpatialized(audioSpatial);
            }
            if (audio3d.active()) {
                ImGui::Text("听者—音源 %.2f 单位", audio3d.distance());
                ImGui::Text("缓冲 %.2f s  欠载 %.0f ms", audio3d.bufferedSeconds(),
                            audio3d.underrunSeconds() * 1000.0);
                ImGui::Text("回调 周期 %.1f  最大 %.1f ms", audio3d.callbackPeriodMs(),
                            audio3d.callbackGapMaxMs());
                ImGui::Text("回调拖后 %llu 次", audio3d.callbackLateCount());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "音轨未挂载%s%s",
                                   audio3d.error()[0] ? "：" : "", audio3d.error());
            }

            ImGui::Separator();
            ImGui::Text("壁纸");
            bool wallpaperOn = g_wp.wallpaper;
            if (ImGui::Checkbox("桌面壁纸模式（F5）", &wallpaperOn)) g_wp.wantToggle = true;
            ImGui::Checkbox("模型区域响应鼠标（F8）", &g_wp.interactive);
            ImGui::Checkbox("显示日志窗口", &g_showLog);
            ImGui::TextDisabled("F5 壁纸 / F6 面板 / F8 交互 / F9 复位 / F12 退出");
            ImGui::End();
        }

        // 日志窗口：控制台已经去掉，mateLog 写的都在这看
        if (!g_wp.wallpaper && g_showLog) {
            ImGui::SetNextWindowPos(ImVec2((float)canvas.width() - 452.0f, 12.0f),
                                    ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(440.0f, 300.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("matex2 日志", &g_showLog)) {
                if (ImGui::Button("清空")) g_logLines.clear();
                ImGui::SameLine();
                ImGui::TextDisabled("共 %d 行", (int)g_logLines.size());
                ImGui::Separator();
                ImGui::BeginChild("##loglines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
                const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
                for (const auto& line : g_logLines) ImGui::TextUnformatted(line.c_str());
                if (atBottom) ImGui::SetScrollHereY(1.0f);   // 只在贴底时自动跟到最新
                ImGui::EndChild();
            }
            ImGui::End();
        }
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
            mateLog("[帧率] %.1f fps（%dx%d）最慢帧 %.2f ms  视频 %dx%d 解码 %.1f ms / 已解 %d 帧 / 已显示 %d 帧\n",
                        fpsFrames / fpsTimer, canvas.width(), canvas.height(),
                        fpsWorstDt * 1000.0f, video.width(), video.height(),
                        vs.decodeMs, vs.decoded, vs.presented);
            if (audio3d.active()) {
                static double s_lastUnderrunMs = 0.0;
                const double underMs = audio3d.underrunSeconds() * 1000.0;
                mateLog("[音频] 缓冲 %.2f s  欠载 +%.0f ms（累计 %.0f）",
                        audio3d.bufferedSeconds(), underMs - s_lastUnderrunMs, underMs);
                s_lastUnderrunMs = underMs;
                mateLog("[音频] 回调 周期 %.1f  最大 %.1f ms  拖后 %llu 次",
                        audio3d.callbackPeriodMs(), audio3d.callbackGapMaxMs(),
                        audio3d.callbackLateCount());
            }
            fpsTimer = 0.0;
            fpsFrames = 0;
            fpsWorstDt = 0.0f;
        }
    }

    // 先摘鼠标钩子、退出壁纸层，再拆音频/纹理
    setWallpaperMouseHook(false);
    if (g_wp.wallpaper) unmountWallpaper();
    if (modelMixer) modelMixer->stopAllAction();
    modelMixer.reset();   // mixer 持有模型根节点引用，先拆
    g_modelRoot = nullptr;
    modelRoot.reset();

    audio3d.shutdown();   // 先停音频，再拆视频/纹理

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    canvas.destroy();
    timeEndPeriod(1);
    mateLog("[退出] matex2 正常结束\n");
    return 0;
}
