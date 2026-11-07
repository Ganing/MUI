/****************************************************************************
 * 标题: matex3 - threepp 最小学习沙箱（纯学习用）
 * 功能: 逐行复刻 ThreeppVSTest 的 01_intro/11_texture2d 示例：
 *       TextureLoader 三张贴图 + Sphere/Box/Plane + Reflector 实时镜面 + OrbitControls。
 *       只保留「窗口 + GL + threepp 场景」这一层，不再有任何产品化功能。
 * 说明: 与 matex0/matex2 一样不链接 GLFW，用 Win32 + WGL 自建 OpenGL 4.5 上下文；
 *       渲染交给 threepp::GLRenderer(size)，事件转给 PeripheralsEventSource。
 *       走 console 子系统，日志直接 printf 到控制台。
 *
 * 依赖: ../../3rd/lib/threepp_mt.lib（/MT）、WS2_32
 * 资源: Data/textures/{checker.png, crate.gif, brick_bump.jpg}（按 exe 目录拼接）
 *
 * 操作: 左键旋转 / 右键平移 / 滚轮缩放 / ESC 退出
 ****************************************************************************/

#define NOMINMAX

#include <windows.h>
#include <mmsystem.h>

#include <glad/glad.h>

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"   // 只为下面的 initWindow 空实现
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/controls/OrbitControls.hpp"
#include "threepp/core/Clock.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/input/PeripheralsEventSource.hpp"
#include "threepp/loaders/TextureLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/math/Color.hpp"
#include "threepp/math/MathUtils.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/Reflector.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"
#include "threepp/utils/LoadGlad.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

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

static const wchar_t* kWindowClass = L"MateX3Window";
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

static LRESULT CALLBACK X3WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 键鼠消息全部转给 threepp 的事件源（OrbitControls 挂在上面）
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (g_canvas) g_canvas->handleMessage(msg, wParam, lParam);
        break;
    default:
        break;
    }

    switch (msg) {
    case WM_SIZE:
        if (g_canvas) g_canvas->resize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
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
    wc.lpfnWndProc   = X3WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        printf("[窗口] 注册窗口类失败: %lu\n", GetLastError());
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
        printf("[窗口] CreateWindowEx 失败: %lu\n", GetLastError());
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
        printf("[GL] GetDC 失败\n");
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
        printf("[GL] 像素格式设置失败\n");
        return false;
    }

    // 先建临时上下文，才能拿到 wglCreateContextAttribsARB
    HGLRC tempCtx = wglCreateContext(hdc_);
    if (!tempCtx) {
        printf("[GL] wglCreateContext 失败\n");
        return false;
    }
    if (!wglMakeCurrent(hdc_, tempCtx)) {
        printf("[GL] 临时上下文 current 失败\n");
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
            printf("[GL] %s 上下文不可用，尝试下一档\n", a.name);
        }
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (!ctx) {
        printf("[GL] 未能创建 3.3+ 上下文，回退 legacy\n");
        ctx = wglCreateContext(hdc_);
    }
    if (!ctx) {
        printf("[GL] 无法创建 OpenGL 上下文\n");
        return false;
    }

    hglrc_ = ctx;
    if (!wglMakeCurrent(hdc_, hglrc_)) {
        printf("[GL] 正式上下文 current 失败\n");
        return false;
    }

    // threepp 的 glad 全进程只加载一次，必须在最终上下文 current 之后
    try {
        threepp::loadGlad();
    } catch (const std::exception& e) {
        printf("[GL] glad 加载失败: %s\n", e.what());
        return false;
    }

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    printf("[GL] %s -> %d.%d | %s | %s\n", chosen.c_str(), (int)major, (int)minor,
           (const char*)glGetString(GL_RENDERER),
           (const char*)glGetString(GL_VERSION));

    PFN_wglSwapIntervalEXT swapInterval =
            (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
    if (swapInterval) {
        swapInterval(vsync ? 1 : 0);
        printf("[GL] 垂直同步: %s\n", vsync ? "开" : "关");
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

/*==================== 主程序：11_texture2d ====================*/
int main() {
    // 源码字符串是 UTF-8（/utf-8），控制台默认 GBK 代码页会把中文显示成乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
    timeBeginPeriod(1);

    printf("========================================\n");
    printf(" matex3 - threepp 最小学习沙箱（11_texture2d）\n");
    printf(" 左键旋转 / 右键平移 / 滚轮缩放 / ESC 退出\n");
    printf("========================================\n");

    Win32Canvas canvas;
    if (!canvas.create(L"Mate x3 - threepp Texture2D", kDefaultWidth, kDefaultHeight, true)) {
        printf("[启动] 窗口/GL 初始化失败\n");
        timeEndPeriod(1);
        return 1;
    }

    std::unique_ptr<threepp::GLRenderer> renderer;
    try {
        renderer = std::make_unique<threepp::GLRenderer>(
                std::pair<int, int>{ canvas.width(), canvas.height() });
    } catch (const std::exception& e) {
        printf("[threepp] 渲染器创建失败: %s\n", e.what());
        canvas.destroy();
        timeEndPeriod(1);
        return 1;
    }
    renderer->setClearColor(threepp::Color(threepp::Color::aliceblue));

    auto scene = threepp::Scene::create();
    scene->background = threepp::Color(threepp::Color::aliceblue);

    threepp::PerspectiveCamera camera(75, canvas.aspect(), 0.1f, 1000);
    camera.position.z = 5;

    // 先摆好相机再建 controls：OrbitControls 构造时会立即 update()
    threepp::OrbitControls controls{camera, canvas};

    std::filesystem::path texDir = exeDir() / "Data" / "textures";
    threepp::TextureLoader loader;
    std::shared_ptr<threepp::Texture> checkerTex, crateTex, brickTex;
    try {
        checkerTex = loader.load(texDir / "checker.png", threepp::ColorSpace::sRGB);
        crateTex   = loader.load(texDir / "crate.gif", threepp::ColorSpace::sRGB);
        brickTex   = loader.load(texDir / "brick_bump.jpg", threepp::ColorSpace::sRGB);
        printf("[纹理] checker.png / crate.gif / brick_bump.jpg 已加载（%s）\n",
               texDir.string().c_str());
    } catch (const std::exception& e) {
        printf("[纹理] 加载失败: %s（检查 %s）\n", e.what(), texDir.string().c_str());
        canvas.destroy();
        timeEndPeriod(1);
        return 1;
    }

    // 球：checker.png
    const auto sphereGeometry = threepp::SphereGeometry::create(0.5f, 16, 16);
    const auto sphereMaterial = threepp::MeshBasicMaterial::create(
            threepp::MeshBasicMaterial::Params{}.map(checkerTex));
    auto sphere = threepp::Mesh::create(sphereGeometry, sphereMaterial);
    sphere->position.setX(1);
    scene->add(sphere);

    // 箱：crate.gif
    const auto boxGeometry = threepp::BoxGeometry::create();
    const auto boxMaterial = threepp::MeshBasicMaterial::create(
            threepp::MeshBasicMaterial::Params{}.map(crateTex));
    auto box = threepp::Mesh::create(boxGeometry, boxMaterial);
    box->position.setX(-1);
    scene->add(box);

    // 平面：brick_bump.jpg，双面（本版 Params 没有 side()，建完直接改成员）
    const auto planeGeometry = threepp::PlaneGeometry::create(5, 5);
    const auto planeMaterial = threepp::MeshBasicMaterial::create(
            threepp::MeshBasicMaterial::Params{}.map(brickTex));
    planeMaterial->side = threepp::Side::Double;
    auto plane = threepp::Mesh::create(planeGeometry, planeMaterial);
    plane->position.setZ(-1);
    scene->add(plane);

    // 实时镜面
    threepp::Reflector::Options opt;
    opt.clipBias = 0.003f;
    opt.color = 0x777777;
    opt.textureWidth = 2048;
    opt.textureHeight = 2048;
    auto reflector = threepp::Reflector::create(threepp::PlaneGeometry::create(10, 10), opt);
    reflector->rotateX(threepp::math::degToRad(-90));
    reflector->position.setY(-2.5f);
    scene->add(reflector);

    canvas.onWindowResize([&](threepp::WindowSize size) {
        camera.aspect = size.aspect();
        camera.updateProjectionMatrix();
        renderer->setSize(std::pair<int, int>{ size.width(), size.height() });
        glViewport(0, 0, size.width(), size.height());
    });

    threepp::Clock clock;
    while (canvas.pumpEvents()) {
        const auto dt = clock.getDelta();

        box->rotation.y += 0.5f * dt;
        sphere->rotation.x += 0.5f * dt;

        renderer->render(*scene, camera);
        canvas.swapBuffers();
    }

    canvas.destroy();
    timeEndPeriod(1);
    printf("[退出] matex3 正常结束\n");
    return 0;
}
