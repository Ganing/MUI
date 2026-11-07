/****************************************************************************
 * 标题: mate - Mate 最终产品形态（引擎底座）
 * 来源: 一比一复刻自 matex0.cpp（引擎骨架，不含任何场景业务代码）
 * 功能: Win32 窗口 + 自建 OpenGL 4.5 上下文 + MateCanvas（复刻 threepp::Canvas
 *       的事件源语义）+ threepp GLRenderer(size) + GLTF 模型 + OrbitControls
 *       + WorkerW 壁纸挂载 + ImGui 参数面板 / 日志面板
 * 说明:
 *       - GL 上下文按 4.5 core -> 4.5 compat -> 3.3 core -> legacy 依次回退
 *       - 不再链接 GLFW，用 MateCanvas 直接封装 Win32/WGL，保留 Canvas 常用接口
 *       - 不内嵌字体与默认模型，字体/模型都从磁盘读取（matex.rc 暂只提供窗口图标）
 *       - 没有控制台，日志写进环形队列并显示在窗口内的日志面板
 *       - 相机交互使用 threepp::OrbitControls（左键旋转 / 右键平移 / 滚轮 dolly）
 *       - 场景内容（静水 / 劳力士 / 短途之旅）后续按 §十 迁出为 MScene 场景包
 *
 * 模式:
 *   调试模式   : 普通顶层窗口，标题栏可拖动，面板可交互
 *   壁纸模式   : 作为子窗口挂到 WorkerW 下，位于桌面图标下方，点击穿透、无面板
 *   编译期由 BUILD_WALLPAPER 决定启动模式，运行期按 F5 随时切换
 *
 * 快捷键:
 *   F5  调试模式 <-> 壁纸模式 切换
 *   F6  参数面板 显示 / 隐藏（仅调试模式）
 *   F7  暂停 / 继续
 *   F8  壁纸模式下切换鼠标交互（低级钩子）
 *   F9  视角复位
 *   ESC 退出（仅调试模式下可收到按键）
 ****************************************************************************/

#define NOMINMAX

#include <windows.h>
#include <mmsystem.h>
#include <dwmapi.h>

#include <glad/glad.h>   // threepp 自带的 glad（已升级为 GL 4.5 compatibility 生成）

#include "threepp/animation/AnimationMixer.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"              // 只为下面的 initWindow 空实现
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/controls/OrbitControls.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/input/IOCapture.hpp"
#include "threepp/input/PeripheralsEventSource.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"
#include "threepp/utils/LoadGlad.hpp"

#include "../core/mate_core.h"   // MScene 场景层共用能力（VFS / 纹理 / GLSL / IScene）
#include "../../../momo/fxcode.h"   // P4：复用 MX 现成的 Shadertoy 特效源码

// GLRenderer.obj 里同时带着 GLRenderer(Canvas&) 那个构造函数，它引用
// Canvas::initWindow；上游靠"总是编译 Canvas.cpp"满足这个符号，而 Mate 不编译
// Canvas.cpp（那是 GLFW 路径）。Mate 自己建窗口，永远不会走那个构造函数，
// 所以这里给个空实现让链接器满意即可。
namespace threepp {
    void Canvas::initWindow(GraphicsAPI) {}
}// namespace threepp

#include "imgui.h"
#include "imgui_freetype.h"   // P8：FreeType 字体后端
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"

// imgui 1.91 起后端不再在头文件里导出窗口过程，需要自己前置声明
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

// 只给了 glad.h（不含 GL/gl.h），WGL 这几个入口要自己声明
extern "C" {
__declspec(dllimport) HGLRC WINAPI wglCreateContext(HDC);
__declspec(dllimport) BOOL  WINAPI wglMakeCurrent(HDC, HGLRC);
__declspec(dllimport) BOOL  WINAPI wglDeleteContext(HGLRC);
__declspec(dllimport) PROC  WINAPI wglGetProcAddress(LPCSTR);
}

// P7：垂直同步交给 SwapBuffers 自己节流，不再用 DwmFlush。
// 实测 Explorer 退出后 DwmFlush 会稳定阻塞两个刷新周期（31ms），帧率直接砍半
typedef BOOL(WINAPI* PFN_wglSwapIntervalEXT)(int);
static PFN_wglSwapIntervalEXT wglSwapInterval = nullptr;

#include <algorithm>
#include <cctype>
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
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")

/*==================== 配置 ====================*/
#define MATE_WINDOW_CLASS  L"MateWallpaperClass"
#define TARGET_FPS         60
#define HOTKEY_ID_TOGGLE   1
#define HOTKEY_ID_PANEL    2
#define HOTKEY_ID_PAUSE    3
#define HOTKEY_ID_INTERACT 4
#define HOTKEY_ID_RESET    5
#define HOTKEY_VK_TOGGLE   VK_F5
#define HOTKEY_VK_PANEL    VK_F6
#define HOTKEY_VK_PAUSE    VK_F7
#define HOTKEY_VK_INTERACT VK_F8
#define HOTKEY_VK_RESET    VK_F9

// P8：低功耗（自动降帧）。暂停时画面不动，1fps 足够；前台有全屏应用/演示模式时
// 壁纸根本看不见，降到 5fps 省电
#define LOWPOWER_PAUSED_FPS    1
#define LOWPOWER_FULLSCREEN_FPS 5

// x0：不内嵌资源，matex.rc 只提供窗口图标（与 mate.rc 的 ID 保持一致）
#define IDI_MATE_APP       103
#define IDI_MATE_SMALL     104

// x0：窗口内日志面板保留的最大行数
#define LOG_MAX_LINES      512

// 模型目录：启动时扫描其中所有 .glb / .gltf，面板下拉切换。
// 优先用 exe 同级的 models（拷走 exe + models 目录即可独立分发），
// 没有就退回开发机上的绝对路径。
static std::string resolveModelDir() {
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        std::filesystem::path dir = std::filesystem::path(exePath).parent_path() / "models";
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec)) return dir.generic_string();
    }
    return "e:/MX/Data/models";
}

// 中文字体候选路径：优先 MX 现成的思源黑体，找不到再退回系统雅黑
static const char* kFontCandidates[] = {
    "e:/MX/Data/siyuan.ttf",
    "./siyuan.ttf",
    "c:/windows/fonts/msyh.ttc",
};

#ifdef BUILD_WALLPAPER
constexpr bool kStartInWallpaper = true;
#else
constexpr bool kStartInWallpaper = false;
#endif

/*==================== 桌面结构类型 ====================*/
enum class DesktopStructure {
    Unknown,    // 未知：兜底挂到 Progman
    Legacy,     // Win7/8/早期 Win10: 带 DefView 的 WorkerW + 同级壁纸 WorkerW
    Modern      // Win10 22H2+/Win11: Progman 直接持有 SHELLDLL_DefView（无壁纸层）
};

static const char* structureName(DesktopStructure s) {
    switch (s) {
    case DesktopStructure::Legacy: return "Legacy";
    case DesktopStructure::Modern: return "Modern";
    default:                       return "Unknown";
    }
}

/*==================== 应用状态 ====================*/
class MateCanvas;   // 定义在窗口过程之前，这里只存指针

struct AppState {
    // x0：窗口/DC/GL 上下文的真实所有者是 MateCanvas（见下面 MateCanvas 类）。
    // 这三个字段只是镜像，方便沿用 v3 已验证的壁纸挂载代码。
    HWND  hwnd = nullptr;
    HDC   hdc  = nullptr;
    HGLRC hglrc = nullptr;

    std::unique_ptr<MateCanvas> canvas;

    int   width  = 0;
    int   height = 0;

    bool  wallpaper = false;     // 当前是否壁纸模式
    bool  running   = true;
    bool  recreating = false;    // 主动销毁窗口时置位，避免 WM_DESTROY 退出循环
    bool  toggleMode = false;    // F5 请求切换模式
    bool  remount    = false;    // Explorer 重启等，需要重新挂载
    bool  rebuildWindow = false; // P7：窗口被 Explorer 连带销毁，主循环里整窗重建
    bool  fullscreenFallback = false;   // P7：挂不上壁纸层，退化成全屏顶层窗口
    double lastWindowCheck = 0.0;       // P7：窗口存活性体检时间戳
    bool  shellReady = false;           // P7：兜底期间记录桌面 shell 是否已恢复
    bool  showPanel  = true;     // F6 控制；壁纸模式下强制不显示
    bool  showLog    = true;     // 面板内的日志区域开关

    // P8：壁纸区域的鼠标交互（F8 切换）。NCHITTEST 恒定穿透，交互靠 WH_MOUSE_LL
    // 钩子只在"光标落在模型投影内"时拦截：左键旋转 / 右键平移 / 滚轮 dolly
    bool  interactive   = false;
    bool  dragging      = false;
    bool  rmbSwallowed  = false; // 右键按下被吞后，抬起也要吞，否则桌面仍会弹出右键菜单

    // P8：低功耗。前台有全屏应用/演示模式时自动降帧
    bool   lowPower       = false;
    double lastPowerCheck = 0.0;

    DesktopStructure structure = DesktopStructure::Unknown;
    HWND  wallpaperParent = nullptr;

    // threepp 场景
    std::unique_ptr<threepp::GLRenderer>        renderer;
    std::shared_ptr<threepp::Scene>             scene;
    std::shared_ptr<threepp::PerspectiveCamera> camera;
    std::shared_ptr<threepp::Group>             modelRoot;   // GLTF 根节点
    std::shared_ptr<threepp::Group>             modelPivot;  // 模型外层容器（相机轨道旋转）
    int   appliedW = 0;          // 已同步给渲染器的尺寸
    int   appliedH = 0;
    float modelRadius = 1.0f;

    // x0：相机交互交给 threepp::OrbitControls（左键旋转 / 右键平移 / 滚轮 dolly）
    std::unique_ptr<threepp::OrbitControls> controls;

    // x0：ImGui 输入捕获，面板悬停时不让 OrbitControls 误收事件
    threepp::IOCapture ioCapture;

    // 扫描到的模型列表：全部来自磁盘目录（x0 不再内嵌默认模型）
    struct ModelEntry {
        std::string name;   // 面板显示名
        std::string path;   // 磁盘路径
    };
    std::vector<ModelEntry> models;
    int  modelIndex = 0;                   // 当前选中项
    bool modelDirty = false;               // 面板改了选择，下一帧加载

    // 模型自带动画：GLTF 里的片段列表 + 播放控制
    std::vector<std::shared_ptr<threepp::AnimationClip>> clips;
    std::unique_ptr<threepp::AnimationMixer>             mixer;
    int  clipIndex   = 0;       // 当前选中的片段
    bool animPlaying = false;   // 是否播放（暂停/继续只影响 action 调度）
    bool clipDirty   = false;   // 面板换了片段或播放状态，下一帧重挂 action

    // P4：Shadertoy 全屏特效层（场景最底层）
    std::shared_ptr<threepp::Mesh>           fxMesh;
    std::shared_ptr<threepp::ShaderMaterial> fxMaterial;
    std::shared_ptr<threepp::Texture>        fxPlaceholderTex;   // sceneTex/lyricsTex 的 1x1 占位
    int   fxIndex   = 0;         // 0 = 关闭，1..N 对应 kFxPresets
    bool  fxEnabled = false;
    bool  fxDirty   = false;     // 面板改了预设，下一帧换着色器
    float fxTime    = 0.0f;

    // ImGui
    bool   imguiReady = false;
    bool   fontLoaded = false;

    // 面板可调参数
    float  rotateSpeed = 0.6f;
    int    fpsCap      = TARGET_FPS;
    bool   showDemo    = false;

    // P5：调参（相机 / 光照 / 特效强度）与观测
    bool  paused      = false;
    float cameraFov   = 45.0f;
    float cameraZoom  = 1.0f;          // 相对自动摆位距离的倍率（与 OrbitControls 距离同步）
    bool  zoomDirty   = false;         // 面板改了距离倍率，下一帧缩放到目标距离
    float cameraBaseDistance = 5.0f;   // loadModel 按包围盒算出来的基准距离
    float ambientIntensity = 1.1f;
    float keyIntensity     = 2.4f;
    float fillIntensity    = 0.8f;
    float fxGain      = 1.0f;

    GLuint gpuQueries[2] = { 0, 0 };
    int    gpuQueryIndex = 0;
    int    gpuFrames     = 0;   // 前两帧的 query 还没结果，先不取
    float  gpuMs         = 0.0f;

    std::shared_ptr<threepp::AmbientLight>     ambientLight;
    std::shared_ptr<threepp::DirectionalLight> keyLight;
    std::shared_ptr<threepp::DirectionalLight> fillLight;

    float  animTime = 0.0f;
    float  lastDt   = 0.0f;
    double fpsAvg   = 0.0;

    // MScene：第一个场景（静水）。sceneEnabled 为 false 时回到 threepp 模型演示
    std::unique_ptr<mate::IScene> stillwater;
    bool  sceneEnabled   = true;
    bool  scenePointerDown = false;

    // P7：SwapBuffers 耗时（vsync 由它节流，这个数就是等垂直同步的时间）
    double swapAcc = 0.0;
    int    swapFrames = 0;
};

static AppState g;

// P7：Explorer 重启后会广播 "TaskbarCreated"，用它来重新挂载壁纸层
static UINT g_taskbarCreatedMsg = 0;

/*==================== 日志 ====================*/
// x0 是 GUI 子系统（wWinMain，无控制台），日志写进环形队列，
// 由参数面板里的日志区域显示；mateLog 也会在主循环的帧率统计里被调用
static std::deque<std::string> g_logLines;
static std::mutex g_logMutex;

static void mateLog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(g_logMutex);
    if ((int)g_logLines.size() >= LOG_MAX_LINES) g_logLines.pop_front();
    g_logLines.emplace_back(buf);
}

/*==================== 计时 ====================*/
static double nowSeconds() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

/*==================== 显示器 ====================*/
// P7：所有显示器拼成的整块区域。壁纸要铺满它，不能只按主屏算
struct ScreenRect {
    int x = 0, y = 0, w = 0, h = 0;
};

static ScreenRect virtualScreenRect() {
    ScreenRect r;
    r.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (r.w <= 0 || r.h <= 0) {   // 远程桌面/驱动异常时退回主屏
        r = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }
    return r;
}

// 顶层 WorkerW 个数：用来判断 0x052C 到底有没有让 Explorer 分裂出壁纸层
static int countTopLevelWorkerW() {
    int n = 0;
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr) ++n;
    return n;
}

// P7：桌面 shell 是否已经跑起来了（Progman + 图标层 SHELLDLL_DefView）。
// Explorer 重启期间两者都会消失，重新出现才说明可以挂壁纸层了
static bool desktopShellReady() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return false;
    if (FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) return true;

    // Legacy 结构：图标层挂在某个顶层 WorkerW 下
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr) {
        if (FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr)) return true;
    }
    return false;
}

/*==================== 低功耗 ====================*/
// P8：前台是否有全屏应用/演示模式。SHQueryUserNotificationState 在 shell32 里，
// 这里动态取（只在 Win7+ 有，动态取顺便省掉 shlobj 头文件与枚举声明）
static bool fullscreenAppForeground() {
    typedef HRESULT(WINAPI* PFN_QueryState)(int*);
    static PFN_QueryState fn = []() -> PFN_QueryState {
        HMODULE h = GetModuleHandleW(L"shell32.dll");
        if (!h) h = LoadLibraryW(L"shell32.dll");
        return h ? (PFN_QueryState)GetProcAddress(h, "SHQueryUserNotificationState") : nullptr;
    }();
    if (!fn) return false;

    int state = 0;
    if (FAILED(fn(&state))) return false;

    // QUNS_RUNNING_D3D_FULL_SCREEN = 3，QUNS_PRESENTATION_MODE = 4
    return state == 3 || state == 4;
}

// 实际生效的帧率上限：暂停 1fps，全屏应用前台 5fps，其余用面板设定值
static int effectiveFpsCap() {
    if (g.paused)   return LOWPOWER_PAUSED_FPS;
    if (g.lowPower) return LOWPOWER_FULLSCREEN_FPS;
    return g.fpsCap;
}

/*==================== 壁纸交互 ====================*/
// P8/x0：模型包围球在屏幕上的投影半径（像素）。相机始终看向 OrbitControls 的
// target（模型中心在原点），因此按距离与视野直接换算即可
static float modelScreenRadius() {
    const float dist = std::max(
            g.controls ? g.controls->getDistance() : g.cameraBaseDistance, 0.001f);
    const float halfH = dist * std::tan(g.cameraFov * 0.5f * 3.14159265f / 180.0f);
    if (halfH <= 0.0f || g.height <= 0) return 0.0f;
    const float pxPerWorld = (float)g.height * 0.5f / halfH;
    // 1.25 倍余量：包围球是保守估计，模型顶点的投影通常比球略大
    return g.modelRadius * pxPerWorld * 1.25f;
}

static bool cursorOverModel(POINT client) {
    const float r = modelScreenRadius();
    if (r <= 0.0f) return false;
    const float dx = (float)client.x - (float)g.width  * 0.5f;
    const float dy = (float)client.y - (float)g.height * 0.5f;
    return dx * dx + dy * dy <= r * r;
}

// 壁纸窗口在 SHELLDLL_DefView 之下，窗口消息根本到不了（点击都被图标层吃掉），
// 所以交互只能走低级鼠标钩子：只在光标落在模型投影内时拦截，其余事件原样透传
static HHOOK g_mouseHook = nullptr;

// 前向声明：实现放在 MateCanvas 定义之后（钩子要调用 canvas 的 feed* 接口）
static LRESULT CALLBACK MateMouseHook(int nCode, WPARAM wParam, LPARAM lParam);

static void setInteractive(bool on) {
    g.interactive = on;
    g.dragging = false;
    g.rmbSwallowed = false;
    if (on && !g_mouseHook) {
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MateMouseHook, GetModuleHandleW(nullptr), 0);
        if (!g_mouseHook) mateLog("[交互] 低级鼠标钩子安装失败: %lu", GetLastError());
    } else if (!on && g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

static void logMonitors() {
    const ScreenRect vs = virtualScreenRect();
    mateLog("[显示器] %d 个，虚拟桌面 %dx%d @ (%d,%d)",
            (int)GetSystemMetrics(SM_CMONITORS), vs.w, vs.h, vs.x, vs.y);

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM) -> BOOL {
            MONITORINFO mi = { sizeof(MONITORINFO) };
            if (GetMonitorInfoW(mon, &mi)) {
                const RECT& rc = mi.rcMonitor;
                mateLog("[显示器]  %dx%d @ (%d,%d)%s",
                        rc.right - rc.left, rc.bottom - rc.top, rc.left, rc.top,
                        (mi.dwFlags & MONITORINFOF_PRIMARY) ? " 主屏" : "");
            }
            return TRUE;
        }, 0);
}

/*==================== 桌面结构探测 ====================*/
// 思路与 MX/MO/oapp.h 的 detectDesktopStructure / findDesktopLayers 一致：
// 先向 Progman 发 0x052C 触发系统分裂出 WorkerW，再按优先级收集可挂载的父窗口。
// Win11 24H2 上结构差异很大（Progman 直挂 SHELLDLL_DefView，且存在多个顶层 WorkerW），
// 因此不假设唯一父窗口，而是逐个尝试，直到 SetParent 成功。
static void collectWallpaperParents(std::vector<HWND>& outParents, DesktopStructure* outStructure) {
    outParents.clear();
    *outStructure = DesktopStructure::Unknown;

    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        mateLog("[桌面] 未找到 Progman 窗口");
        return;
    }

    // 触发 WorkerW 创建（Win11 24H2 上可能无效，靠下面的候选列表兜底）
    const int workerBefore = countTopLevelWorkerW();
    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    Sleep(120);
    const int workerAfter = countTopLevelWorkerW();
    mateLog("[桌面] 0x052C: 顶层 WorkerW %d -> %d（%s）",
            workerBefore, workerAfter,
            workerAfter > workerBefore ? "已分裂出壁纸层" : "未生效，走 Progman 直挂");

    bool progmanOwnsDefView =
        FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
    *outStructure = progmanOwnsDefView ? DesktopStructure::Modern : DesktopStructure::Legacy;

    // Modern（Win11 24H2）：壁纸由 Progman 自己绘制，且 SHELLDLL_DefView 直接挂在 Progman 下。
    // 实测 Wallpaper Engine 的层级是 Progman → 全屏 WorkerW → 壁纸窗口（WPEVideoWallpaper），
    // 那个 WorkerW 排在 DefView 之后，z 序由父子层级天然保证，比"每次相对 DefView 插队"更抗
    // Explorer 折腾，所以优先复用这个容器；找不到再退回直接挂 Progman。
    if (progmanOwnsDefView) {
        // P7：按虚拟桌面（所有显示器拼起来）判断，副屏也要算进"够大"
        const ScreenRect vs = virtualScreenRect();

        bool seenDefView = false;
        HWND child = nullptr;
        while ((child = FindWindowExW(progman, child, nullptr, nullptr)) != nullptr) {
            wchar_t cls[64] = {0};
            GetClassNameW(child, cls, 64);

            if (wcscmp(cls, L"SHELLDLL_DefView") == 0) {
                seenDefView = true;
                continue;   // 之后出现的兄弟窗口都在图标层下方
            }
            if (!seenDefView || wcscmp(cls, L"WorkerW") != 0) continue;
            if (!IsWindowVisible(child)) continue;

            RECT rc = { 0, 0, 0, 0 };
            GetWindowRect(child, &rc);
            if (rc.right - rc.left < vs.w / 2 || rc.bottom - rc.top < vs.h / 2) continue;

            outParents.push_back(child);
            break;
        }
        outParents.push_back(progman);   // 退路：直接挂在 Progman 下，插到 DefView 之后
    }

    // 候选 2：不带 SHELLDLL_DefView 的顶层 WorkerW；同时记录图标层
    HWND iconsLayer = nullptr;
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr) {
        if (FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr)) {
            iconsLayer = w;
            continue;
        }
        if (std::find(outParents.begin(), outParents.end(), w) == outParents.end()) {
            outParents.push_back(w);
        }
    }

    // 候选 3：经典结构下图标层之后的同级 WorkerW，优先级最高
    if (iconsLayer) {
        if (HWND wallpaperLayer = FindWindowExW(nullptr, iconsLayer, L"WorkerW", nullptr)) {
            outParents.insert(outParents.begin(), wallpaperLayer);
        }
    }

    // 兜底：Progman 本身
    if (std::find(outParents.begin(), outParents.end(), progman) == outParents.end()) {
        outParents.push_back(progman);
    }
}

/*==================== MateCanvas：Win32 版 Canvas ====================*/
// x0：用 Win32 + WGL 复刻 threepp::Canvas 的常用接口与事件语义，但不链接 GLFW。
// 继承 PeripheralsEventSource 借它的键鼠分发（OrbitControls 要挂在一个事件源上）；
// 渲染仍交给 GLRenderer(size)，因为 GLRenderer(Canvas&) 绑定的是真正的 Canvas 类型。
static bool registerWindowClass();

// VK -> threepp::Key。threepp 的 Key 枚举与 GLFW 完全同序，字母/数字/F 键可直接算
static threepp::Key toThreeppKey(WPARAM vk) {
    using threepp::Key;
    if (vk >= 'A' && vk <= 'Z') return (Key)((int)Key::A + (int)(vk - 'A'));
    if (vk >= '0' && vk <= '9') return (Key)((int)Key::NUM_0 + (int)(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (Key)((int)Key::F1 + (int)(vk - VK_F1));

    switch (vk) {
    case VK_SPACE:      return Key::SPACE;
    case VK_OEM_1:      return Key::SEMICOLON;
    case VK_OEM_PLUS:   return Key::EQUAL;
    case VK_OEM_COMMA:  return Key::COMMA;
    case VK_OEM_MINUS:  return Key::MINUS;
    case VK_OEM_PERIOD: return Key::PERIOD;
    case VK_OEM_2:      return Key::SLASH;
    case VK_OEM_3:      return Key::GRAVE_ACCENT;
    case VK_OEM_4:      return Key::LEFT_BRACKET;
    case VK_OEM_5:      return Key::BACKSLASH;
    case VK_OEM_6:      return Key::RIGHT_BRACKET;
    case VK_OEM_7:      return Key::APOSTROPHE;
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
    case VK_CAPITAL:    return Key::CAPS_LOCK;
    case VK_SCROLL:     return Key::SCROLL_LOCK;
    case VK_NUMLOCK:    return Key::NUM_LOCK;
    case VK_SNAPSHOT:   return Key::PRINT_SCREEN;
    case VK_PAUSE:      return Key::PAUSE;
    case VK_LSHIFT:
    case VK_SHIFT:      return Key::LEFT_SHIFT;
    case VK_LCONTROL:
    case VK_CONTROL:    return Key::LEFT_CONTROL;
    case VK_LMENU:
    case VK_MENU:       return Key::LEFT_ALT;
    default:            return Key::UNKNOWN;
    }
}

// 与 GLFW 的 mods 位定义一致，OrbitControls 的按键判断依赖它
static int toThreeppMods() {
    int mods = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= 0x0001;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= 0x0002;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= 0x0004;
    return mods;
}

class MateCanvas : public threepp::PeripheralsEventSource {
public:
    struct Parameters {
        std::wstring title = L"Mate";
        int  width  = 1280;
        int  height = 720;
        bool vsync  = true;
        bool exitOnKeyEscape = true;
        bool wallpaper = false;   // 壁纸模式：先建普通顶层窗口，再由 mountWallpaper 挂载
    };

    MateCanvas() = default;
    ~MateCanvas() override;

    bool create(const Parameters& params);
    void destroy();

    // ---- 与 threepp::Canvas 对齐的常用接口 ----
    [[nodiscard]] threepp::WindowSize size() const override {
        return { g.width, g.height };
    }
    [[nodiscard]] float aspect() const {
        return g.height > 0 ? (float)g.width / (float)g.height : 1.0f;
    }
    void setSize(threepp::WindowSize size);
    void onWindowResize(std::function<void(threepp::WindowSize)> f) {
        resizeCallback_ = std::move(f);
    }
    [[nodiscard]] bool hasResizeCallback() const { return (bool)resizeCallback_; }
    void notifyResize(threepp::WindowSize s) {
        if (resizeCallback_) resizeCallback_(s);
    }
    void animate(const std::function<void()>& f);
    bool animateOnce(const std::function<void()>& f);
    [[nodiscard]] bool isOpen() const { return hwnd_ != nullptr && IsWindow(hwnd_); }
    void close() {
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
    [[nodiscard]] void* windowPtr() const { return hwnd_; }
    void exitOnKeyEscape(bool value) { exitOnEscape_ = value; }
    [[nodiscard]] bool exitOnKeyEscape() const { return exitOnEscape_; }
    [[nodiscard]] bool vsync() const { return vsync_; }

    // ---- Win32 句柄 / 帧操作 ----
    [[nodiscard]] HWND  hwnd() const { return hwnd_; }
    [[nodiscard]] HDC   hdc() const { return hdc_; }
    [[nodiscard]] HGLRC hglrc() const { return hglrc_; }
    void swapBuffers() const { if (hdc_) SwapBuffers(hdc_); }
    bool pumpEvents();

    // ---- 键鼠事件入口：窗口消息与低级鼠标钩子共用 ----
    void handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void feedMouseDown(int button, float x, float y);
    void feedMouseUp(int button, float x, float y);
    void feedMouseMove(float x, float y);
    void feedMouseWheel(float deltaY);

    void onExternalDestroy();   // Explorer 连带销毁窗口
    [[nodiscard]] bool destroying() const { return destroying_; }
    [[nodiscard]] const char* profileName() const { return profileName_.c_str(); }

private:
    bool initGL();
    void syncMirror();
    void clearMirror();

    HWND  hwnd_  = nullptr;
    HDC   hdc_   = nullptr;
    HGLRC hglrc_ = nullptr;
    bool  exitOnEscape_ = true;
    bool  vsync_ = true;
    bool  destroying_ = false;
    bool  mouseDown_[3] = { false, false, false };
    std::string profileName_ = "none";
    std::function<void(threepp::WindowSize)> resizeCallback_;
};

MateCanvas::~MateCanvas() {
    destroy();
}

bool MateCanvas::create(const Parameters& params) {
    if (!registerWindowClass()) return false;

    exitOnEscape_ = params.exitOnKeyEscape;
    vsync_ = params.vsync;

    RECT rc = { 0, 0, params.width, params.height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(
            0, MATE_WINDOW_CLASS, params.title.c_str(), WS_OVERLAPPEDWINDOW,
            (screenW - winW) / 2, (screenH - winH) / 2, winW, winH,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        mateLog("[窗口] CreateWindowEx 失败: %lu", GetLastError());
        return false;
    }

    ShowWindow(hwnd_, SW_SHOW);
    if (!params.wallpaper) SetForegroundWindow(hwnd_);

    syncMirror();   // 先镜像，initGL 里出错时 teardown 也能拿到 hwnd
    if (!initGL()) return false;
    return true;
}

void MateCanvas::destroy() {
    // 调用方（teardownWindow）此时已释放 GL 对象，这里只负责上下文与窗口
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
        if (IsWindow(hwnd_)) {
            destroying_ = true;
            DestroyWindow(hwnd_);
            destroying_ = false;
        }
        hwnd_ = nullptr;
    }
    mouseDown_[0] = mouseDown_[1] = mouseDown_[2] = false;
    clearMirror();
}

void MateCanvas::syncMirror() {
    g.hwnd  = hwnd_;
    g.hdc   = hdc_;
    g.hglrc = hglrc_;
}

void MateCanvas::clearMirror() {
    g.hwnd  = nullptr;
    g.hdc   = nullptr;
    g.hglrc = nullptr;
}

void MateCanvas::onExternalDestroy() {
    // 窗口被 Explorer 连带销毁：DC 立刻失效，但 GL 上下文对象还在，留给 destroy() 删除
    hwnd_ = nullptr;
    hdc_  = nullptr;
    g.hwnd = nullptr;
    g.hdc  = nullptr;
}

bool MateCanvas::initGL() {
    hdc_ = GetDC(hwnd_);
    if (!hdc_) {
        mateLog("[GL] GetDC 失败");
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
        mateLog("[GL] 像素格式设置失败");
        return false;
    }

    // 先用临时上下文拿到 wglCreateContextAttribsARB（wglGetProcAddress 需要 current 上下文）
    HGLRC tempCtx = wglCreateContext(hdc_);
    if (!tempCtx) {
        mateLog("[GL] wglCreateContext 失败");
        return false;
    }
    if (!wglMakeCurrent(hdc_, tempCtx)) {
        mateLog("[GL] 临时上下文 current 失败");
        wglDeleteContext(tempCtx);
        return false;
    }

    typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
    auto createContextAttribs =
            (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");

    // x0：4.5 core -> 4.5 compatibility -> 3.3 core -> legacy
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
            mateLog("[GL] %s 上下文不可用，尝试下一档", a.name);
        }
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (!ctx) {
        mateLog("[GL] 未能创建 3.3+ 上下文，回退 legacy（特效/模型可能不可用）");
        ctx = wglCreateContext(hdc_);
    }
    if (!ctx) {
        mateLog("[GL] 无法创建 OpenGL 上下文");
        return false;
    }

    hglrc_ = ctx;
    profileName_ = chosen;
    if (!wglMakeCurrent(hdc_, hglrc_)) {
        mateLog("[GL] 正式上下文 current 失败");
        return false;
    }
    syncMirror();

    // threepp 的 glad 是全进程一次加载，必须在最终上下文 current 之后调用
    try {
        threepp::loadGlad();
    } catch (const std::exception& e) {
        mateLog("[GL] glad 加载失败: %s", e.what());
        return false;
    }

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    mateLog("[GL] 申请档位: %s，实际版本: %d.%d", profileName_.c_str(), (int)major, (int)minor);
    mateLog("[GL] 版本: %s", (const char*)glGetString(GL_VERSION));
    mateLog("[GL] 设备: %s", (const char*)glGetString(GL_RENDERER));
    mateLog("[GL] GLSL: %s", (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));

    wglSwapInterval = (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapInterval) {
        wglSwapInterval(vsync_ ? 1 : 0);
        mateLog("[GL] 垂直同步: %s", vsync_ ? "开" : "关");
    } else {
        mateLog("[GL] wglSwapIntervalEXT 不可用，只靠帧率上限节流");
    }

    RECT rc;
    GetClientRect(hwnd_, &rc);
    g.width  = rc.right - rc.left;
    g.height = rc.bottom - rc.top;
    if (g.width <= 0)  g.width  = 1280;
    if (g.height <= 0) g.height = 720;
    glViewport(0, 0, g.width, g.height);
    return true;
}

void MateCanvas::setSize(threepp::WindowSize size) {
    if (!hwnd_) return;
    RECT rc = { 0, 0, size.width(), size.height() };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool MateCanvas::pumpEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void MateCanvas::animate(const std::function<void()>& f) {
    while (isOpen()) {
        if (!pumpEvents()) break;
        f();
        swapBuffers();
    }
}

bool MateCanvas::animateOnce(const std::function<void()>& f) {
    if (!pumpEvents()) return false;
    f();
    swapBuffers();
    return isOpen();
}

void MateCanvas::feedMouseDown(int button, float x, float y) {
    if (button < 0 || button > 2) return;
    mouseDown_[button] = true;
    onMousePressedEvent(button, threepp::Vector2(x, y), MouseAction::PRESS);
}

void MateCanvas::feedMouseUp(int button, float x, float y) {
    if (button < 0 || button > 2) return;
    mouseDown_[button] = false;
    onMousePressedEvent(button, threepp::Vector2(x, y), MouseAction::RELEASE);
}

void MateCanvas::feedMouseMove(float x, float y) {
    onMouseMoveEvent(threepp::Vector2(x, y));
}

void MateCanvas::feedMouseWheel(float deltaY) {
    onMouseWheelEvent(threepp::Vector2(0.0f, deltaY));
}

void MateCanvas::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
            SetCapture(hwnd);
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

// F9 / 面板按钮：相机回到自动摆位距离，target 归零
static void resetView() {
    if (!g.controls || !g.camera) return;
    g.controls->target.set(0.0f, 0.0f, 0.0f);
    g.camera->position.set(0.0f, 0.0f, g.cameraBaseDistance);
    g.camera->lookAt(g.controls->target);
    g.camera->updateProjectionMatrix();
    g.controls->update();
    g.cameraZoom = 1.0f;
    g.zoomDirty = false;
    mateLog("[视角] 已复位（距离 %.3f）", g.cameraBaseDistance);
}

// 低级鼠标钩子实现（声明在壁纸交互段）。壁纸模式下窗口收不到消息，
// 事件直接注入 MateCanvas -> PeripheralsEventSource -> OrbitControls
static LRESULT CALLBACK MateMouseHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g.wallpaper && g.interactive && g.hwnd && IsWindow(g.hwnd) &&
        g.canvas) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        POINT pt = ms->pt;
        ScreenToClient(g.hwnd, &pt);
        const float x = (float)pt.x;
        const float y = (float)pt.y;

        switch (wParam) {
        case WM_LBUTTONDOWN:
            if (cursorOverModel(pt)) {
                g.dragging = true;
                g.canvas->feedMouseDown(0, x, y);
                return 1;   // 吃掉，避免桌面同时开始框选
            }
            break;

        case WM_MOUSEMOVE:
            if (g.dragging) {
                g.canvas->feedMouseMove(x, y);
                // 注意不能 return 1：吞掉 MOUSEMOVE 会让光标原地不动（实测拖拽位移恒为 0）
            }
            break;

        case WM_LBUTTONUP:
            if (g.dragging) {
                g.dragging = false;
                g.canvas->feedMouseUp(0, x, y);
                return 1;
            }
            break;

        case WM_RBUTTONDOWN:
            if (cursorOverModel(pt)) {
                g.dragging = true;       // 右键平移也走同一套拖拽状态
                g.rmbSwallowed = true;
                g.canvas->feedMouseDown(1, x, y);
                return 1;
            }
            break;

        case WM_RBUTTONUP:
            // 右键菜单是 Explorer 在"抬起"时才弹的，只吞按下等于没吞（实测桌面菜单照弹）
            if (g.rmbSwallowed) {
                g.rmbSwallowed = false;
                g.dragging = false;
                g.canvas->feedMouseUp(1, x, y);
                return 1;
            }
            break;

        case WM_MOUSEWHEEL:
            if (cursorOverModel(pt)) {
                // 与 GLFW 的 yoffset 约定一致：向上滚 = +1
                const int delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData);
                g.canvas->feedMouseWheel(delta > 0 ? 1.0f : -1.0f);
                return 1;
            }
            break;
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

/*==================== 窗口过程 ====================*/
static LRESULT CALLBACK MateWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 键鼠消息优先交给 ImGui（面板拖拽 / 输入）；ESC 与热键保留给下面的逻辑
    if (g.imguiReady && msg != WM_HOTKEY &&
        !(msg == WM_KEYDOWN && wParam == VK_ESCAPE)) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
            return 0;
        }
    }

    // P7：Explorer 重启会广播 TaskbarCreated，壁纸层已重建，请求重新挂载
    if (g_taskbarCreatedMsg && msg == g_taskbarCreatedMsg) {
        mateLog("[壁纸] 收到 TaskbarCreated（Explorer 已重启）");
        if (g.wallpaper) g.remount = true;
        return 0;
    }

    // x0：未被 ImGui 吃掉的键鼠消息转给 MateCanvas（OrbitControls 监听它）
    if (g.canvas) g.canvas->handleMessage(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID_TOGGLE) {
            g.toggleMode = true;
        } else if (wParam == HOTKEY_ID_PANEL) {
            g.showPanel = !g.showPanel;
        } else if (wParam == HOTKEY_ID_PAUSE) {
            g.paused = !g.paused;
            mateLog("[渲染] %s（上限 %d fps）", g.paused ? "暂停" : "继续", effectiveFpsCap());
        } else if (wParam == HOTKEY_ID_INTERACT) {
            // P8：壁纸模式下切换"接收鼠标/穿透"。调试模式下窗口本来就能交互，忽略
            if (g.wallpaper) {
                setInteractive(!g.interactive);
                mateLog("[交互] %s（%s）", g.interactive ? "接收鼠标" : "穿透",
                        g.interactive ? "光标移到模型上：左键旋转 / 右键平移 / 滚轮缩放"
                                      : "鼠标事件交还桌面");
            }
        } else if (wParam == HOTKEY_ID_RESET) {
            resetView();
        }
        return 0;

    // P8：壁纸窗口在 SHELLDLL_DefView 之下，窗口本身收不到鼠标（点击都被图标层吃掉），
    // 所以交互模式不在窗口消息里做，而是用低级鼠标钩子按"光标是否落在模型投影内"拦截。
    // 这里保留 WM_NCHITTEST 的穿透逻辑不变，桌面图标的操作完全不受影响
    case WM_NCHITTEST:
        // 壁纸模式不接收鼠标：HTTRANSPARENT 让命中测试直接穿到下层窗口。
        // 注意不能用 WS_EX_TRANSPARENT 扩展样式，实测那会让窗口在桌面完全不呈现。
        if (g.wallpaper) {
            return HTTRANSPARENT;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_MOUSEACTIVATE:
        // 壁纸模式下即使被点到也不要抢焦点
        if (g.wallpaper) {
            return MA_NOACTIVATE;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (!g.canvas || g.canvas->exitOnKeyEscape()) DestroyWindow(hwnd);
        } else if (wParam == VK_F7) {
            g.paused = !g.paused;
            mateLog("[渲染] %s", g.paused ? "暂停" : "继续");
        } else if (wParam == VK_F9) {
            resetView();
        } else if ((wParam == VK_SPACE || wParam == VK_RETURN) && g.stillwater && g.sceneEnabled) {
            // 与原作一致：回车/空格在水面中心投下一颗涟漪
            g.stillwater->pointerDown(0.48f, 0.52f);
        }
        return 0;

    case WM_SIZE:
        if (g.hdc) {
            g.width  = LOWORD(lParam);
            g.height = HIWORD(lParam);
            glViewport(0, 0, g.width, g.height);
            if (g.canvas && g.canvas->hasResizeCallback()) {
                g.canvas->notifyResize(threepp::WindowSize(g.width, g.height));
            }
        }
        return 0;

    case WM_DISPLAYCHANGE:
        // 分辨率变化：壁纸模式下跟随重挂
        g.remount = true;
        return 0;

    case WM_SETTINGCHANGE:
        // Explorer 重启、壁纸变更等：重新挂载
        g.remount = true;
        return 0;

    case WM_ERASEBKGND:
        return 1;   // 全部由 OpenGL 绘制，避免闪白

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g.recreating) return 0;
        if (g.canvas && g.canvas->destroying()) return 0;   // 主动 destroy() 触发的析构
        if (g.wallpaper) {
            // P7：Explorer 重启会连带销毁作为子窗口的我们。此时不能退出，
            // 记下来交给主循环整窗重建（GL 上下文已随窗口失效，必须重来）。
            mateLog("[壁纸] 窗口被连带销毁（Explorer 重启？），准备重建");
            if (g.canvas) g.canvas->onExternalDestroy();   // 同步清空 hwnd/hdc 镜像
            g.rebuildWindow = true;
            return 0;
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

/*==================== threepp 场景 ====================*/
// x0：模型列表完全来自磁盘目录（不再内嵌默认模型）
// 递归扫描：目录里既有顶层 .glb，也有 little_pond__fish/scene.gltf 这类子目录场景，
// 显示名用相对路径，避免出现一堆同名的 scene.gltf
static bool scanModels() {
    g.models.clear();

    // exe 路径运行期不会变，目录只解析一次
    static const std::string modelDir = resolveModelDir();
    const std::filesystem::path root(modelDir);

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        mateLog("[模型] 目录不可用: %s（%s）", modelDir.c_str(), ec.message().c_str());
        return false;
    }

    std::vector<std::pair<std::string, std::string>> found;   // (显示名, 全路径)
    for (const auto& entry : it) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (ext != ".glb" && ext != ".gltf") continue;

        std::error_code relEc;
        const std::filesystem::path rel = std::filesystem::relative(entry.path(), root, relEc);
        const std::string name = relEc ? entry.path().filename().string()
                                       : rel.generic_string();
        found.emplace_back(name, entry.path().generic_string());
    }

    // 忽略大小写排序：按 ASCII 会把 bob_xxx 这类小写开头的排到最后
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        const size_t n = (std::min)(a.first.size(), b.first.size());
        for (size_t i = 0; i < n; ++i) {
            const unsigned char ca = (unsigned char)std::tolower((unsigned char)a.first[i]);
            const unsigned char cb = (unsigned char)std::tolower((unsigned char)b.first[i]);
            if (ca != cb) return ca < cb;
        }
        return a.first.size() < b.first.size();
    });
    for (const auto& p : found) {
        g.models.push_back({ p.first, p.second });
    }

    mateLog("[模型] 目录 %s，共 %d 个", modelDir.c_str(), (int)g.models.size());
    return !g.models.empty();
}

// 重挂当前片段：先停掉全部 action，再按播放状态播当前片段
static void applyClip() {
    if (!g.mixer) return;
    g.mixer->stopAllAction();
    if (g.clipIndex < 0 || g.clipIndex >= (int)g.clips.size()) return;
    if (!g.animPlaying) return;

    auto* action = g.mixer->clipAction(g.clips[g.clipIndex]);
    if (action) action->reset().play();
}

// 加载第 index 个模型：先加载成功再卸掉旧的，避免加载失败时场景变空；
// 相机按包围盒自动摆位，换任何 glb 都不用逐个调
static bool loadModel(int index) {
    if (index < 0 || index >= (int)g.models.size()) return false;

    const AppState::ModelEntry& entry = g.models[index];
    mateLog("[模型] 加载 %d/%d: %s", index + 1, (int)g.models.size(), entry.name.c_str());

    threepp::GLTFLoader loader;
    auto result = loader.load(std::filesystem::path(entry.path));
    if (!result || !result->scene) {
        mateLog("[模型] 加载失败: %s", entry.name.c_str());
        return false;
    }

    // 旧模型的动画要先停掉：mixer 持有旧根节点引用
    g.mixer.reset();
    g.clips.clear();

    if (g.modelPivot) {
        g.scene->remove(*g.modelPivot);
        g.modelPivot.reset();
    }
    g.modelRoot.reset();

    g.modelRoot = result->scene;

    // 按包围盒把模型摆到原点，并把相机放到能完整看见它的距离上
    threepp::Box3 box;
    box.setFromObject(*g.modelRoot);
    threepp::Vector3 center = box.getCenter();
    threepp::Vector3 size   = box.getSize();

    float radius = 0.5f * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    if (radius <= 0.0f) radius = 1.0f;
    g.modelRadius = radius;

    g.modelRoot->position.set(-center.x, -center.y, -center.z);

    g.modelPivot = threepp::Group::create();
    g.modelPivot->add(g.modelRoot);
    g.scene->add(g.modelPivot);

    // 裁剪面跟着模型尺寸走：目录里既有半径 0.01 的小模型也有 30MB 的大模型
    // （near/far 在渲染循环里按当前距离倍率每帧重算）
    const float fovRad = g.camera->fov * 3.14159265358979f / 180.0f;
    const float distance = radius / std::sin(fovRad * 0.5f) * 1.25f;
    g.cameraBaseDistance = distance;
    g.cameraZoom = 1.0f;
    g.zoomDirty = false;

    g.camera->position.set(0.0f, 0.0f, distance);
    g.camera->lookAt(threepp::Vector3(0.0f, 0.0f, 0.0f));
    g.camera->updateProjectionMatrix();

    // x0：OrbitControls 以模型中心为轨道中心，距离范围随模型尺寸更新
    if (g.controls) {
        g.controls->target.set(0.0f, 0.0f, 0.0f);
        g.controls->minDistance = distance * 0.3f;
        g.controls->maxDistance = distance * 3.0f;
        g.controls->update();
    }

    mateLog("[模型] 已就位: %s（半径 %.3f，相机距离 %.3f）",
            entry.name.c_str(), radius, distance);

    // x0：GLTF 自带动画。列出片段并默认播放第一段
    g.clips       = result->animations;
    g.clipIndex   = 0;
    g.clipDirty   = false;
    g.animPlaying = !g.clips.empty();
    if (!g.clips.empty()) {
        g.mixer = std::make_unique<threepp::AnimationMixer>(*g.modelRoot);
        applyClip();
        std::string names;
        for (size_t i = 0; i < g.clips.size(); ++i) {
            if (i) names += " / ";
            names += g.clips[i]->name();
        }
        mateLog("[动画] 共 %d 段：%s", (int)g.clips.size(), names.c_str());
    } else {
        mateLog("[动画] 该模型不含动画");
    }
    return true;
}

/*==================== P4 特效层 ====================*/
struct FxPreset {
    const char* name;
    const char* source;
};

// fxcode.h 里 4 个 Shadertoy 风格特效；0/2 额外采样 sceneTex/lyricsTex，
// 当前还没有画面捕获与歌词层，先绑 1x1 黑纹理占位
static const FxPreset kFxPresets[] = {
    { "动态矩形", FXCODE::g_fxSource0 },
    { "梵高日落", FXCODE::g_fxSource1 },
    { "细雨",     FXCODE::g_fxSource2 },
    { "启动画面", FXCODE::g_fxSource3 },
};
static constexpr int kFxPresetCount = (int)(sizeof(kFxPresets) / sizeof(kFxPresets[0]));

// 顶点着色器直接输出裁剪坐标：这个四边形不参与相机变换，永远铺满屏幕
static const char* kFxVertexShader = R"GLSL(
void main() {
    gl_Position = vec4(position.xy, 0.9999, 1.0);
}
)GLSL";

// fxcode.h 每个特效自带 main()，先把它改名，再包一层统一乘上强度系数
static std::string buildFxFragment(const char* body) {
    std::string s = body;
    const std::string entry = "void main()";
    const size_t pos = s.find(entry);
    if (pos == std::string::npos) return s;   // 兜底：认不出就直接用原源码

    s.replace(pos, entry.size(), "void fxEntry()");
    s += "\nuniform float fxGain;\n"
         "void main() {\n"
         "    fxEntry();\n"
         "    gl_FragColor.rgb *= fxGain;\n"
         "}\n";
    return s;
}

static void applyFxPreset(int index) {
    if (!g.fxMaterial) return;

    g.fxEnabled = (index > 0 && index <= kFxPresetCount);
    g.fxMesh->visible = g.fxEnabled;
    if (!g.fxEnabled) {
        mateLog("[特效] 关闭");
        return;
    }

    g.fxMaterial->fragmentShader = buildFxFragment(kFxPresets[index - 1].source);
    g.fxMaterial->needsUpdate();   // 触发重新编译程序
    mateLog("[特效] %s", kFxPresets[index - 1].name);
}

static void initFxLayer() {
    // 1x1 黑纹理：给 sceneTex / lyricsTex 占位，避免采样到未绑定的纹理单元
    g.fxPlaceholderTex = threepp::Texture::create(
            threepp::Image(std::vector<unsigned char>{ 0, 0, 0, 255 }, 1, 1));

    g.fxMaterial = threepp::ShaderMaterial::create();
    g.fxMaterial->vertexShader   = kFxVertexShader;
    g.fxMaterial->fragmentShader = buildFxFragment(kFxPresets[0].source);
    // 底图层：不测深度也不写深度，先画它，3D 模型总能盖在上面
    g.fxMaterial->depthTest  = false;
    g.fxMaterial->depthWrite = false;

    auto& u = g.fxMaterial->uniforms;
    u["iTime"]       = threepp::Uniform(0.0f);
    u["iResolution"] = threepp::Uniform(threepp::Vector2(1.f, 1.f));
    u["iMouse"]      = threepp::Uniform(threepp::Vector4(0.f, 0.f, 0.f, 0.f));
    u["iMouseXY"]    = threepp::Uniform(threepp::Vector2(0.f, 0.f));
    u["iMouseZW"]    = threepp::Uniform(threepp::Vector2(0.f, 0.f));
    u["texSize"]     = threepp::Uniform(threepp::Vector2(1.f, 1.f));
    u["sceneTex"]    = threepp::Uniform(static_cast<threepp::Texture*>(g.fxPlaceholderTex.get()));
    u["lyricsTex"]   = threepp::Uniform(static_cast<threepp::Texture*>(g.fxPlaceholderTex.get()));
    u["fxGain"]      = threepp::Uniform(1.0f);

    g.fxMesh = threepp::Mesh::create(threepp::PlaneGeometry::create(2.0f, 2.0f), g.fxMaterial);
    g.fxMesh->frustumCulled = false;   // 顶点着色器绕过了相机，包围盒算出来没意义
    g.fxMesh->renderOrder   = -1000;   // 不透明物体按 renderOrder 升序，保证它第一个画
    g.fxMesh->visible       = false;
    g.scene->add(g.fxMesh);

    mateLog("[特效] 已就绪（%d 个预设，当前关闭）", kFxPresetCount);
}

// 每帧刷新 Shadertoy 的 iTime / iResolution / iMouse
static void updateFxUniforms() {
    if (!g.fxEnabled || !g.fxMaterial) return;

    g.fxTime += g.lastDt;

    // 鼠标坐标转成 gl_FragCoord 的左下原点，点击状态按 momo 的约定塞进 zw
    float mx = 0.0f, my = 0.0f;
    bool  down = false;
    if (g.imguiReady) {
        ImGuiIO& io = ImGui::GetIO();
        mx   = io.MousePos.x;
        my   = (float)g.height - 1.0f - io.MousePos.y;
        down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    }

    auto& u = g.fxMaterial->uniforms;
    u["iTime"].value<float>() = g.fxTime;
    u["iResolution"].value<threepp::Vector2>().set((float)g.width, (float)g.height);

    auto& mouse = u["iMouse"].value<threepp::Vector4>();
    const float z = down ? std::abs(mx) : -std::abs(mx);
    const float w = down ? std::abs(my) : -std::abs(my);
    mouse.set(mx, my, z, w);
    u["iMouseXY"].value<threepp::Vector2>().set(mx, my);
    u["iMouseZW"].value<threepp::Vector2>().set(z, w);
    u["fxGain"].value<float>() = g.fxGain;
}

static bool initThreepp() {
    try {
        g.renderer = std::make_unique<threepp::GLRenderer>(
            std::pair<int, int>{ g.width, g.height });
    } catch (const std::exception& e) {
        mateLog("[threepp] 渲染器创建失败: %s", e.what());
        return false;
    }
    g.appliedW = g.width;
    g.appliedH = g.height;
    g.renderer->setClearColor(threepp::Color(0x0d1017));

    g.scene = threepp::Scene::create();
    g.scene->background = threepp::Color(0x0d1017);

    g.camera = std::make_shared<threepp::PerspectiveCamera>(
        g.cameraFov, (float)g.width / (float)g.height, 0.01f, 1000.0f);

    // x0：相机交互交给 threepp::OrbitControls。先给相机一个合法初始位置，
    // 因为 OrbitControls 构造时会立即 update()（位置在原点会退化）
    g.camera->position.set(0.0f, 0.0f, g.cameraBaseDistance);
    g.camera->lookAt(threepp::Vector3(0.0f, 0.0f, 0.0f));
    if (g.canvas) {
        g.controls = std::make_unique<threepp::OrbitControls>(*g.camera, *g.canvas);
        g.controls->target.set(0.0f, 0.0f, 0.0f);
        g.controls->autoRotate      = true;
        g.controls->autoRotateSpeed = g.rotateSpeed * 10.0f;
        g.controls->minDistance     = 0.001f;
        g.controls->maxDistance     = 1.0e6f;
        mateLog("[相机] OrbitControls 已挂到 MateCanvas（左键旋转 / 右键平移 / 滚轮 dolly）");
    }

    // 灯光留着引用，面板改强度时直接改
    g.ambientLight = threepp::AmbientLight::create(0xffffff, g.ambientIntensity);
    g.scene->add(g.ambientLight);

    g.keyLight = threepp::DirectionalLight::create(0xffffff, g.keyIntensity);
    g.keyLight->position.set(2.4f, 3.6f, 3.2f);
    g.scene->add(g.keyLight);

    g.fillLight = threepp::DirectionalLight::create(0x8899ff, g.fillIntensity);
    g.fillLight->position.set(-3.0f, -1.5f, -2.0f);
    g.scene->add(g.fillLight);

    initFxLayer();   // P4：全屏特效层（默认关闭）

    // x0：不内嵌默认模型，模型全部来自目录扫描
    if (!scanModels()) {
        mateLog("[threepp] 模型目录里没有可用的 .glb / .gltf，场景留空");
        return true;
    }
    loadModel(g.modelIndex);   // 失败也继续跑，只是没有模型
    return true;
}

/*==================== ImGui ====================*/
static void initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // 不落盘 imgui.ini，保持项目目录干净

    // 中文字体：按候选路径逐个尝试，命中即用
    // GetGlyphRangesChineseSimplifiedCommon 只含 2500 常用字，像 "帧" 这种面板上要用的字
    // 不在其中，会显示成问号，所以把面板实际用到的字整体补进范围。
    static ImVector<ImWchar> glyphRanges;
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddText("分辨率帧率模式字体中文已加载默认模型半径上下限旋转速度切换壁纸退出示例窗口参数面板无可用的目录扫描特效关闭动态矩形梵高日落细雨启动画面暂停继续视野距离倍率光照环境主补强度不限内嵌交互重置视角低功耗前台全屏应用降到实际拖拽右键穿透接收鼠标桌面事件还日志轨道相机复位");
        builder.BuildRanges(&glyphRanges);
    }

    // P8：字体栅格化换成 FreeType。stb_truetype 关掉抗锯齿/网格对齐后中文边缘发糊，
    // FreeType 的 LightHinting 只做纵向对齐，最接近微软自身的渲染观感
    io.Fonts->FontBuilderIO    = ImGuiFreeType::GetBuilderForFreeType();
    io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;

    // x0：字体不内嵌，全部走磁盘候选（见 kFontCandidates）
    for (const char* path : kFontCandidates) {
        if (g.fontLoaded) break;
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH  = true;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            path, 18.0f, &cfg, glyphRanges.Data);
        if (font) {
            g.fontLoaded = true;
            mateLog("[ImGui] 字体: %s + FreeType", path);
            break;
        }
    }
    if (!g.fontLoaded) {
        io.Fonts->AddFontDefault();
        mateLog("[ImGui] 字体: 内置默认字体（中文会显示为方块）");
    }

    ImGui_ImplWin32_Init(g.hwnd);
    ImGui_ImplOpenGL3_Init("#version 450");   // x0：4.5 基线

    // x0：面板悬停/输入时把键鼠事件从 MateCanvas 手里截走，避免 OrbitControls 误转
    g.ioCapture.preventMouseEvent = [] { return g.imguiReady && ImGui::GetIO().WantCaptureMouse; };
    g.ioCapture.preventScrollEvent = [] { return g.imguiReady && ImGui::GetIO().WantCaptureMouse; };
    g.ioCapture.preventKeyboardEvent = [] { return g.imguiReady && ImGui::GetIO().WantCaptureKeyboard; };
    if (g.canvas) g.canvas->setIOCapture(&g.ioCapture);

    g.imguiReady = true;
}

static void shutdownImGui() {
    if (!g.imguiReady) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g.imguiReady = false;
}

/*==================== MScene 场景 ====================*/
// 场景目录：Data/MScene/<场景名>；目前只有一个内置场景（静水）
static void initStillWater() {
    g.stillwater.reset(mate::createStillWaterScene());
    const std::string sceneDir = mate::joinPath(mate::dataDir(), "MScene/Stillwater");
    if (!g.stillwater->load(sceneDir)) {
        mateLog("[静水] 场景加载失败，回退到 threepp 演示场景");
        g.stillwater.reset();
        g.sceneEnabled = false;
        return;
    }
    g.stillwater->resize(g.width, g.height);
    g.sceneEnabled = true;
    mateLog("[静水] 场景已就绪（%dx%d）", g.width, g.height);
}

static void drawPanel() {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    // 不用 AlwaysAutoResize：滑块的标签画在控件右侧，自动宽度不计入标签，会被裁掉
    ImGui::Begin("Mate 参数面板", &g.showPanel);

    ImGui::Text("分辨率  %d x %d", g.width, g.height);
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text("帧率    %.1f fps（%.2f ms）", fps, fps > 0.01f ? 1000.0f / fps : 0.0f);
    ImGui::Text("GPU     %.2f ms", g.gpuMs);
    ImGui::Text("模式    %s", g.wallpaper ? "壁纸" : "调试");
    ImGui::Text("字体    %s", g.fontLoaded ? "中文已加载" : "默认字体");

    // MScene：静水场景开关与场景自己的参数面板
    ImGui::Separator();
    ImGui::Checkbox("静水场景", &g.sceneEnabled);
    if (g.stillwater && g.sceneEnabled) {
        g.stillwater->drawUI();
    }
    ImGui::Separator();

    // 模型下拉：全部来自启动时的目录扫描（x0 不内嵌默认模型）
    const char* current = g.models.empty() ? "（无可用的模型）" : g.models[g.modelIndex].name.c_str();
    if (ImGui::BeginCombo("模型", current)) {
        for (int i = 0; i < (int)g.models.size(); ++i) {
            const bool selected = (i == g.modelIndex);
            if (ImGui::Selectable(g.models[i].name.c_str(), selected) && i != g.modelIndex) {
                g.modelIndex = i;
                g.modelDirty = true;   // 下一帧在渲染循环里加载
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Text("半径    %.2f（%d / %d）", g.modelRadius,
                g.modelIndex + 1, (int)g.models.size());

    // 模型动画：片段选择 + 播放开关（片段来自 GLTF animations）
    if (g.clips.empty()) {
        ImGui::Text("动画    无");
    } else {
        const char* clipCur = g.clips[g.clipIndex]->name().c_str();
        if (ImGui::BeginCombo("动画", clipCur)) {
            for (int i = 0; i < (int)g.clips.size(); ++i) {
                const bool selected = (i == g.clipIndex);
                if (ImGui::Selectable(g.clips[i]->name().c_str(), selected) && !selected) {
                    g.clipIndex = i;
                    g.clipDirty = true;   // 下一帧重挂 action
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(g.animPlaying ? "暂停" : "播放")) {
            g.animPlaying = !g.animPlaying;
            g.clipDirty = true;
        }
        ImGui::Text("片段    %d 段（%.2f 秒）", (int)g.clips.size(),
                    g.clips[g.clipIndex]->getDuration());
    }

    // 特效预设下拉：0 = 关闭，其余来自 fxcode.h
    const char* fxCur = (g.fxIndex == 0) ? "关闭" : kFxPresets[g.fxIndex - 1].name;
    if (ImGui::BeginCombo("特效", fxCur)) {
        if (ImGui::Selectable("关闭", g.fxIndex == 0) && g.fxIndex != 0) {
            g.fxIndex = 0;
            g.fxDirty = true;
        }
        for (int i = 0; i < kFxPresetCount; ++i) {
            const bool selected = (g.fxIndex == i + 1);
            if (ImGui::Selectable(kFxPresets[i].name, selected) && !selected) {
                g.fxIndex = i + 1;
                g.fxDirty = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SliderFloat("特效强度", &g.fxGain, 0.0f, 3.0f, "%.2f");
    ImGui::Separator();

    ImGui::Text("相机");
    ImGui::Text("交互    左键旋转 / 右键平移 / 滚轮 dolly");
    ImGui::SliderFloat("视野",     &g.cameraFov, 20.0f, 90.0f, "%.0f");
    float zoom = g.cameraZoom;
    if (ImGui::SliderFloat("距离倍率", &zoom, 0.3f, 3.0f, "%.2f")) {
        g.cameraZoom = zoom;
        g.zoomDirty  = true;   // 渲染循环里按目标倍率缩放相机距离
    }

    ImGui::Text("光照");
    ImGui::SliderFloat("环境光", &g.ambientIntensity, 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("主光",   &g.keyIntensity, 0.0f, 8.0f, "%.2f");
    ImGui::SliderFloat("补光",   &g.fillIntensity, 0.0f, 8.0f, "%.2f");
    ImGui::Separator();

    ImGui::SliderInt("帧率上限", &g.fpsCap, 15, 240);
    // 快捷档位单独一行：和滑块同一行时 120 会被面板宽度裁掉
    if (ImGui::SmallButton("30 fps"))  g.fpsCap = 30;
    ImGui::SameLine(); if (ImGui::SmallButton("60 fps"))  g.fpsCap = 60;
    ImGui::SameLine(); if (ImGui::SmallButton("120 fps")) g.fpsCap = 120;
    ImGui::SameLine(); if (ImGui::SmallButton("不限"))    g.fpsCap = 240;
    ImGui::SliderFloat("旋转速度", &g.rotateSpeed, 0.0f, 2.0f, "%.2f");
    if (ImGui::Button("重置视角 (F9)")) {
        resetView();
    }

    if (ImGui::Button(g.paused ? "继续 (F7)" : "暂停 (F7)")) {
        g.paused = !g.paused;
    }

    // P8：低功耗状态。上限是面板设定值，实际是暂停/全屏应用降频后的生效值
    ImGui::Text("帧率   上限 %d / 实际 %d", g.fpsCap, effectiveFpsCap());
    if (g.paused) {
        ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "低功耗：已暂停，降到 %d fps",
                           LOWPOWER_PAUSED_FPS);
    } else if (g.lowPower) {
        ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "低功耗：前台全屏应用，降到 %d fps",
                           LOWPOWER_FULLSCREEN_FPS);
    }
    ImGui::Separator();

    if (ImGui::Button("切换壁纸模式 (F5)")) {
        g.toggleMode = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("退出")) {
        g.running = false;
    }
    ImGui::Checkbox("ImGui 示例窗口", &g.showDemo);
    ImGui::Checkbox("调试日志", &g.showLog);

    ImGui::End();
}

// x0：日志面板。与参数面板分开，方便折叠；内容来自 mateLog 的环形队列
static void drawLogPanel() {
    // x0：日志面板停靠在主窗口右侧，宽度收窄，窗口缩放时重新贴合右边缘；
    // 首次启动默认折叠，点标题栏展开
    const float logW = 320.0f;
    const float logH = (std::min)(460.0f, (std::max)(240.0f, (float)g.height * 0.6f));
    const float logX = (std::max)(12.0f, (float)g.width - logW - 12.0f);

    ImGui::SetNextWindowPos(ImVec2(logX, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(logW, logH), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Mate 日志", &g.showLog,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }

    if (ImGui::SmallButton("清空")) {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logLines.clear();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("（F6 收起面板 / F9 复位）");
    ImGui::Separator();

    // 取消水平滚动条：长日志按面板宽度自动换行
    ImGui::BeginChild("##logscroll", ImVec2(0, 0), false);
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        ImGui::PushTextWrapPos(0.0f);   // 0 = 换行到窗口右边缘
        for (const auto& line : g_logLines) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopTextWrapPos();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// 一帧 ImGui：壁纸模式没有输入也没有可见面板，直接跳过绘制
static void renderUI() {
    if (!g.imguiReady) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (!g.wallpaper) {
        if (g.showPanel) drawPanel();
        if (g.showLog)   drawLogPanel();
        if (g.showDemo)  ImGui::ShowDemoWindow(&g.showDemo);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/*==================== 窗口创建 / 销毁 ====================*/
static bool registerWindowClass() {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = MateWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = MATE_WINDOW_CLASS;
    // x0：图标不内嵌在 cpp 里，而是由 matex.rc 以资源 ID 103/104 提供
    wc.hIcon         = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_MATE_APP));
    wc.hIconSm       = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_MATE_SMALL));
    if (!wc.hIcon)   mateLog("[窗口] 大图标资源 %d 缺失（检查 matex.rc）", IDI_MATE_APP);
    if (!wc.hIconSm) mateLog("[窗口] 小图标资源 %d 缺失（检查 matex.rc）", IDI_MATE_SMALL);

    if (!RegisterClassExW(&wc)) {
        mateLog("[窗口] 注册窗口类失败: %lu", GetLastError());
        return false;
    }
    registered = true;
    return true;
}

// 把"已经创建好、GL 上下文已就绪"的顶层窗口挂到桌面壁纸层。
// 顺序必须与 MX/MO/oapp.h 一致：先有正常顶层窗口并建好 GL 上下文，再改样式 + SetParent。
static void renderFrame(float dt);

// 用当前客户区刷新尺寸与视口（挂载/摘除壁纸层后尺寸都会变）
static void syncClientSize() {
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(g.hwnd, &rc);
    g.width  = rc.right - rc.left;
    g.height = rc.bottom - rc.top;
    if (g.width <= 0 || g.height <= 0) {
        const ScreenRect vs = virtualScreenRect();
        g.width  = vs.w;
        g.height = vs.h;
    }
    glViewport(0, 0, g.width, g.height);
}

// P7 兜底：所有父窗口都挂不上时（Win11 24H2 上 0x052C 无效、Progman 也拒绝 SetParent），
// 退化成铺满虚拟桌面的无边框顶层窗口。代价是桌面图标会被画面盖住，只能保证壁纸可见；
// 点击穿透仍由 WM_NCHITTEST 负责，鼠标不会被困住。
static bool mountFallbackFullscreen() {
    const ScreenRect vs = virtualScreenRect();

    LONG style = GetWindowLongW(g.hwnd, GWL_STYLE);
    style |= WS_POPUP | WS_VISIBLE;
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_CHILD);
    SetWindowLongW(g.hwnd, GWL_STYLE, style);

    LONG exStyle = GetWindowLongW(g.hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_NOACTIVATE;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLongW(g.hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(g.hwnd, HWND_TOP, vs.x, vs.y, vs.w, vs.h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

    g.wallpaperParent = nullptr;
    g.fullscreenFallback = true;
    g.shellReady = desktopShellReady();   // 基线：兜底瞬间 shell 是否已回来
    syncClientSize();
    mateLog("[壁纸] 兜底：全屏顶层窗口 %dx%d @ (%d,%d)（桌面图标会被覆盖）",
            g.width, g.height, vs.x, vs.y);
    return true;
}

static bool mountWallpaper() {
    const ScreenRect vs = virtualScreenRect();

    std::vector<HWND> candidates;
    collectWallpaperParents(candidates, &g.structure);
    if (candidates.empty()) {
        mateLog("[壁纸] 未找到可挂载的父窗口，转全屏兜底");
        return mountFallbackFullscreen();
    }
    mateLog("[壁纸] 桌面结构: %s，候选父窗口 %zu 个",
            structureName(g.structure), candidates.size());

    for (int idx = 0; idx < (int)candidates.size(); idx++) {
        HWND parent = candidates[idx];

        RECT pr = { 0, 0, 0, 0 };
        GetWindowRect(parent, &pr);
        int pw = pr.right - pr.left;
        int ph = pr.bottom - pr.top;

        mateLog("[壁纸] 候选 #%d: %p 可见=%d 尺寸=%dx%d 父=%p",
                idx, (void*)parent, (int)IsWindowVisible(parent), pw, ph,
                (void*)GetParent(parent));

        // 跳过隐藏或尺寸异常的窗口，避免挂到不可见的图层上
        if (!IsWindowVisible(parent) || pw < vs.w / 2 || ph < vs.h / 2) {
            continue;
        }

        // 跨进程 SetParent 要求双方 DPI 感知级别一致，否则返回 ERROR_INVALID_PARAMETER(87)
        DPI_AWARENESS_CONTEXT parentCtx = GetWindowDpiAwarenessContext(parent);
        if (!parentCtx) parentCtx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
        DPI_AWARENESS_CONTEXT prevCtx = SetThreadDpiAwarenessContext(parentCtx);

        // 与 MX 的窗口样式逐项对齐：只加 WS_CHILD | WS_VISIBLE 和 WS_EX_LAYERED。
        // 实测附加 WS_EX_TRANSPARENT / NOACTIVATE / TOOLWINDOW 会导致窗口在桌面完全不呈现。
        LONG style = GetWindowLongW(g.hwnd, GWL_STYLE);
        style |= WS_CHILD | WS_VISIBLE;
        style &= ~WS_CAPTION;
        style &= ~WS_THICKFRAME;
        style &= ~WS_POPUP;   // 从全屏兜底切回来时要去掉 WS_POPUP
        SetWindowLongW(g.hwnd, GWL_STYLE, style);

        LONG exStyle = GetWindowLongW(g.hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_LAYERED;
        exStyle &= ~WS_EX_APPWINDOW;
        SetWindowLongW(g.hwnd, GWL_EXSTYLE, exStyle);
        SetLayeredWindowAttributes(g.hwnd, 0, 0xFF, LWA_ALPHA);

        // SetParent 返回原父窗口，顶层窗返回 NULL 属正常，需配合 GetLastError 判断
        SetLastError(0);
        HWND oldParent = SetParent(g.hwnd, parent);
        DWORD parentErr = GetLastError();
        SetThreadDpiAwarenessContext(prevCtx);

        if (!oldParent && parentErr != 0) {
            mateLog("[壁纸] 父窗口 %p 挂载失败: %lu", (void*)parent, parentErr);
            continue;
        }

        g.wallpaperParent = parent;

        RECT pr2 = { 0, 0, 0, 0 };
        GetClientRect(parent, &pr2);
        int parentW = pr2.right - pr2.left;
        int parentH = pr2.bottom - pr2.top;
        if (parentW <= 0 || parentH <= 0) { parentW = vs.w; parentH = vs.h; }

        SetWindowPos(g.hwnd, nullptr, 0, 0, parentW, parentH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

        // z 序：插到桌面图标层（SHELLDLL_DefView）下方，图标在我们之上
        HWND defView = FindWindowExW(parent, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) {
            SetWindowPos(g.hwnd, defView, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            mateLog("[壁纸] z 序已插入 SHELLDLL_DefView(%p) 下方", (void*)defView);
        }

        ShowWindow(parent, SW_SHOW);
        // 强制父窗口连同子窗口一起重绘，否则桌面合成可能一直停留在旧壁纸上
        RedrawWindow(parent, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        mateLog("[壁纸] 已挂载到 %p（%dx%d）", (void*)parent, parentW, parentH);
        return true;
    }

    mateLog("[壁纸] 所有候选父窗口均挂载失败，转全屏兜底");
    return mountFallbackFullscreen();
}

// 从壁纸层摘回普通顶层窗口：只改样式与父子关系，GL 上下文和 ImGui 上下文全部保留。
// 这样 F5 切换不用重建窗口、不重建 threepp 场景，也就没有掉帧。
static bool unmountWallpaper() {
    if (!g.hwnd || !IsWindow(g.hwnd)) return false;

    HWND parent = GetParent(g.hwnd);
    if (parent) {
        // 跨进程 SetParent 要求双方 DPI 感知级别一致
        DPI_AWARENESS_CONTEXT parentCtx = GetWindowDpiAwarenessContext(parent);
        if (!parentCtx) parentCtx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
        DPI_AWARENESS_CONTEXT prevCtx = SetThreadDpiAwarenessContext(parentCtx);
        SetParent(g.hwnd, nullptr);
        SetThreadDpiAwarenessContext(prevCtx);
    }
    g.wallpaperParent = nullptr;

    // 先脱离父窗口再改样式：WS_CHILD 只能在无父窗口时安全移除
    LONG exStyle = GetWindowLongW(g.hwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_LAYERED;
    exStyle &= ~WS_EX_NOACTIVATE;   // 全屏兜底时加的，回调试模式要去掉
    SetWindowLongW(g.hwnd, GWL_EXSTYLE, exStyle);

    LONG style = GetWindowLongW(g.hwnd, GWL_STYLE);
    style |= WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    style &= ~WS_CHILD;
    style &= ~WS_POPUP;             // 全屏兜底时加的
    SetWindowLongW(g.hwnd, GWL_STYLE, style);

    g.fullscreenFallback = false;

    RECT rc = { 0, 0, 1280, 720 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(g.hwnd, HWND_TOP,
                 (screenW - winW) / 2, (screenH - winH) / 2, winW, winH,
                 SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    SetForegroundWindow(g.hwnd);
    RedrawWindow(g.hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW);
    syncClientSize();
    mateLog("[壁纸] 已脱离壁纸层，回到调试窗口 %dx%d", g.width, g.height);
    return true;
}

static bool buildWindow(bool wallpaperMode) {
    g.wallpaper = wallpaperMode;

    // x0：窗口 + WGL 上下文全部交给 MateCanvas（复刻 threepp::Canvas 的创建流程）
    MateCanvas::Parameters params;
    params.width  = 1280;
    params.height = 720;
    params.vsync  = true;          // 与 v3 一致：SwapBuffers 节流
    params.exitOnKeyEscape = true;
    params.wallpaper = wallpaperMode;
    params.title = wallpaperMode
            ? L"Mate Wallpaper"
            : L"Mate - Win32 + OpenGL 4.5（F5 切壁纸 / F6 面板 / F9 复位 / ESC 退出）";

    g.canvas = std::make_unique<MateCanvas>();
    if (!g.canvas->create(params)) {
        mateLog("[窗口] MateCanvas 创建失败");
        g.canvas.reset();
        return false;
    }
    if (!wallpaperMode) {
        mateLog("[调试] 普通顶层窗口已创建（F5 切壁纸 / F6 面板 / F9 复位）");
    }

    if (!initThreepp()) {
        return false;
    }
    initImGui();   // 需要 GL 上下文已 current
    initStillWater();   // MScene：静水场景（GL 已就绪）

    if (wallpaperMode) {
        // 挂载前先以普通顶层窗口形态跑一段时间（渲染 + 泵消息），
        // 让 DWM 完成对该窗口的首次合成。MX 是用户手动按 F4 才挂载，
        // 窗口早已正常合成过；如果启动即挂载，之后 DWM 就不再合成这个窗口
        // （表现为桌面看不到画面，但 GetPixel 能读到 GL 内容）。
        for (int i = 0; i < 4; i++) {
            renderFrame(0.0f);
            Sleep(16);
        }
        double warmup = nowSeconds();
        while (nowSeconds() - warmup < 2.0) {
            MSG wmsg;
            while (PeekMessageW(&wmsg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&wmsg);
                DispatchMessageW(&wmsg);
            }
            renderFrame(0.0f);
            Sleep(10);
        }
        if (!mountWallpaper()) {
            return false;
        }
        syncClientSize();
        mateLog("[壁纸] 挂载后客户区 %dx%d", g.width, g.height);
    }

    // F5 / F6 / F7 / F8 全局热键：壁纸模式下窗口没有焦点，也必须能响应
    if (!RegisterHotKey(g.hwnd, HOTKEY_ID_TOGGLE, MOD_NOREPEAT, HOTKEY_VK_TOGGLE)) {
        mateLog("[热键] F5 注册失败: %lu（不影响运行）", GetLastError());
    }
    if (!RegisterHotKey(g.hwnd, HOTKEY_ID_PANEL, MOD_NOREPEAT, HOTKEY_VK_PANEL)) {
        mateLog("[热键] F6 注册失败: %lu（不影响运行）", GetLastError());
    }
    if (!RegisterHotKey(g.hwnd, HOTKEY_ID_PAUSE, MOD_NOREPEAT, HOTKEY_VK_PAUSE)) {
        mateLog("[热键] F7 注册失败: %lu（不影响运行）", GetLastError());
    }
    if (!RegisterHotKey(g.hwnd, HOTKEY_ID_INTERACT, MOD_NOREPEAT, HOTKEY_VK_INTERACT)) {
        mateLog("[热键] F8 注册失败: %lu（不影响运行）", GetLastError());
    }
    if (!RegisterHotKey(g.hwnd, HOTKEY_ID_RESET, MOD_NOREPEAT, HOTKEY_VK_RESET)) {
        mateLog("[热键] F9 注册失败: %lu（不影响运行）", GetLastError());
    }

    g.animTime = 0.0f;
    return true;
}

static void teardownWindow() {
    // 删除 GL 资源需要上下文为当前；窗口已被 Explorer 连带销毁时 DC/上下文已失效，
    // 此时跳过 makeCurrent 直接释放对象（GL 调用会被驱动忽略），重建时再一整套重来
    if (g.hglrc && g.hdc && g.hwnd && IsWindow(g.hwnd)) {
        wglMakeCurrent(g.hdc, g.hglrc);
    }

    shutdownImGui();   // 必须在 GL 上下文销毁前释放 ImGui 后端资源
    g.stillwater.reset();   // 场景先释放自己的 GL 资源（上下文仍是 current）
    g.controls.reset();   // OrbitControls 持有 canvas 的监听器，必须先于 canvas 销毁
    g.renderer.reset();   // 先让 threepp 释放它持有的 GL 对象
    if (g.gpuQueries[0]) {
        glDeleteQueries(2, g.gpuQueries);
        g.gpuQueries[0] = g.gpuQueries[1] = 0;
        g.gpuFrames = 0;
    }
    g.fxMesh.reset();
    g.fxMaterial.reset();
    g.fxPlaceholderTex.reset();
    g.ambientLight.reset();
    g.keyLight.reset();
    g.fillLight.reset();
    // 动画对象持有模型节点引用，必须先于场景图释放
    g.mixer.reset();
    g.clips.clear();
    g.modelPivot.reset();
    g.modelRoot.reset();
    g.scene.reset();
    g.camera.reset();

    if (g.hwnd) {
        UnregisterHotKey(g.hwnd, HOTKEY_ID_TOGGLE);
        UnregisterHotKey(g.hwnd, HOTKEY_ID_PANEL);
        UnregisterHotKey(g.hwnd, HOTKEY_ID_PAUSE);
        UnregisterHotKey(g.hwnd, HOTKEY_ID_INTERACT);
        UnregisterHotKey(g.hwnd, HOTKEY_ID_RESET);
    }

    setInteractive(false);   // P8：钩子必须在窗口销毁前摘掉

    // x0：GL 上下文 / DC / 窗口统一由 MateCanvas 释放（同时清空 g.hwnd/g.hdc 镜像）
    if (g.canvas) {
        g.canvas->destroy();
        g.canvas.reset();
    }

    g.wallpaperParent = nullptr;
    g.fullscreenFallback = false;
    g.width = g.height = 0;
    g.appliedW = g.appliedH = 0;
}

/*==================== 重新挂载（Explorer 重启 / 分辨率变化） ====================*/
static void remountWallpaper() {
    g.remount = false;
    if (!g.wallpaper) return;

    // 父窗口被销毁时子窗口也会一起消失，此时整窗重建
    if (!IsWindow(g.hwnd)) {
        mateLog("[壁纸] 窗口已失效，重建中...");
        teardownWindow();
        if (!buildWindow(true)) {
            mateLog("[壁纸] 重建失败，退出");
            g.running = false;
        }
        return;
    }

    // 当前是全屏兜底：整窗重建后再挂。实测"全屏顶层窗口直接 SetParent 进 WorkerW"
    // 之后 DWM 就不再合成这个窗口（画面停在旧壁纸上、帧率掉到 30 上下），
    // 只有重新创建窗口走正常路径才恢复
    if (g.fullscreenFallback) {
        mateLog("[壁纸] 当前是全屏兜底，整窗重建后重新挂载");
        teardownWindow();
        if (!buildWindow(true)) {
            mateLog("[壁纸] 重建失败，退出");
            g.running = false;
        }
        return;
    }

    std::vector<HWND> candidates;
    DesktopStructure st = DesktopStructure::Unknown;
    collectWallpaperParents(candidates, &st);
    if (candidates.empty()) {
        mateLog("[壁纸] 重新挂载失败：找不到父窗口，转全屏兜底");
        mountFallbackFullscreen();
        return;
    }

    HWND parent = candidates.front();
    g.structure = st;
    g.wallpaperParent = parent;
    g.fullscreenFallback = false;

    if (GetParent(g.hwnd) != parent) {
        DPI_AWARENESS_CONTEXT parentCtx = GetWindowDpiAwarenessContext(parent);
        if (!parentCtx) parentCtx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
        DPI_AWARENESS_CONTEXT prevCtx = SetThreadDpiAwarenessContext(parentCtx);
        SetParent(g.hwnd, parent);
        SetThreadDpiAwarenessContext(prevCtx);
    }

    // 尺寸按父窗口客户区算：Explorer 的壁纸容器本身就跨所有显示器
    RECT pr = { 0, 0, 0, 0 };
    GetClientRect(parent, &pr);
    int pw = pr.right - pr.left;
    int ph = pr.bottom - pr.top;
    if (pw <= 0 || ph <= 0) {
        const ScreenRect vs = virtualScreenRect();
        pw = vs.w;
        ph = vs.h;
    }
    SetWindowPos(g.hwnd, HWND_TOP, 0, 0, pw, ph,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g.width = pw;
    g.height = ph;
    glViewport(0, 0, g.width, g.height);

    mateLog("[壁纸] 已重新挂载（结构: %s，%dx%d）", structureName(g.structure), pw, ph);
}

/*==================== 渲染 ====================*/
/*==================== GPU 计时 ====================*/
// 两个 GL_TIME_ELAPSED query 乒乓：本帧开始计时，同时取上一帧的结果，
// 不会因为等 GPU 而把帧率拖下来
static void gpuTimerBegin() {
    if (!g.gpuQueries[0]) glGenQueries(2, g.gpuQueries);
    glBeginQuery(GL_TIME_ELAPSED, g.gpuQueries[g.gpuQueryIndex]);
}

static void gpuTimerEnd() {
    glEndQuery(GL_TIME_ELAPSED);

    // 第一轮的 query 还没提交完，直接取会拿到 GL_INVALID_OPERATION
    if (g.gpuFrames++ >= 2) {
        GLuint& prev = g.gpuQueries[g.gpuQueryIndex ^ 1];
        GLint available = 0;
        glGetQueryObjectiv(prev, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(prev, GL_QUERY_RESULT, &ns);
            g.gpuMs = (float)((double)ns / 1e6);
        }
    }
    g.gpuQueryIndex ^= 1;
}

/*==================== 观察式输入 ====================*/
// 静水用"看见"而不是"抢走"点击：读全局光标/按键状态，不改变命中测试，
// 壁纸层保持穿透，桌面图标照常可用。
static void pollSceneInput() {
    if (!g.stillwater || !g.sceneEnabled || g.paused) {
        g.scenePointerDown = false;
        return;
    }
    if (!g.wallpaper && g.imguiReady && ImGui::GetIO().WantCaptureMouse) {
        g.scenePointerDown = false;
        return;
    }
    if (!g.hwnd || !IsWindow(g.hwnd) || g.width <= 0 || g.height <= 0) return;

    POINT pt = {};
    if (!GetCursorPos(&pt)) return;
    ScreenToClient(g.hwnd, &pt);
    if (pt.x < 0 || pt.y < 0 || pt.x >= g.width || pt.y >= g.height) {
        g.scenePointerDown = false;
        return;
    }
    const float nx = (float)pt.x / (float)g.width;
    const float ny = (float)pt.y / (float)g.height;
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (down && !g.scenePointerDown) {
        g.stillwater->pointerDown(nx, ny);
    } else if (down) {
        g.stillwater->pointerMove(nx, ny);   // 场景内部按 100ms 节流
    }
    g.scenePointerDown = down;
}

static void renderFrame(float dt) {
    if (g.paused) dt = 0.0f;   // 暂停：动画与特效时间都停住，画面仍持续刷新
    g.animTime += dt;
    g.lastDt = dt;

    // 面板切了模型：在帧首加载，避免在 ImGui 绘制过程中动场景图
    if (g.modelDirty) {
        g.modelDirty = false;
        loadModel(g.modelIndex);
    }
    // 面板换了动画片段 / 切换播放状态：重挂 action
    if (g.clipDirty) {
        g.clipDirty = false;
        applyClip();
    }
    // 骨骼动画推进：暂停时 dt 已经是 0，画面自然定住
    if (g.mixer && g.animPlaying) g.mixer->update(dt);
    if (g.fxDirty) {
        g.fxDirty = false;
        applyFxPreset(g.fxIndex);
    }
    updateFxUniforms();

    // 面板调参：相机视野、轨道距离、三盏灯强度
    if (g.camera) {
        g.camera->fov = g.cameraFov;

        // 距离倍率改动：沿当前视线方向把相机缩放到目标距离（等价于 OrbitControls dolly）
        if (g.zoomDirty && g.controls) {
            g.zoomDirty = false;
            threepp::Vector3 offset = g.camera->position;
            offset.sub(g.controls->target);
            const float cur = offset.length();
            if (cur > 1e-6f) {
                const float want = g.cameraBaseDistance * g.cameraZoom;
                offset.multiplyScalar(want / cur);
                g.camera->position.copy(g.controls->target).add(offset);
            }
        }

        // x0：自动旋转交给 OrbitControls.autoRotate（暂停时不再 update，画面就定住）
        if (g.controls) {
            g.controls->autoRotate      = g.rotateSpeed > 0.001f;
            g.controls->autoRotateSpeed = g.rotateSpeed * 10.0f;   // 面板 0.6 ≈ 6.0，约 10 秒一圈
            if (!g.paused) g.controls->update();
        }

        // 用真实距离更新裁剪面，并让面板的"距离倍率"跟随滚轮/拖拽后的实际距离
        const float dist = g.controls ? (std::max)(g.controls->getDistance(), 0.001f)
                                      : (std::max)(g.camera->position.length(), 0.001f);
        g.cameraZoom = dist / (std::max)(g.cameraBaseDistance, 0.001f);
        // 拉远时远裁剪面要跟着走，否则大模型在距离倍率拉满时会被裁掉
        g.camera->nearPlane = (std::max)(g.modelRadius * 0.01f, 0.001f);
        g.camera->farPlane  = dist + g.modelRadius * 10.0f;
        g.camera->updateProjectionMatrix();
    }
    if (g.ambientLight) g.ambientLight->intensity = g.ambientIntensity;
    if (g.keyLight)     g.keyLight->intensity     = g.keyIntensity;
    if (g.fillLight)    g.fillLight->intensity    = g.fillIntensity;

    if (g.width != g.appliedW || g.height != g.appliedH) {
        g.appliedW = g.width;
        g.appliedH = g.height;
        if (g.renderer) g.renderer->setSize(std::pair<int, int>{ g.width, g.height });
        if (g.camera) {
            g.camera->aspect = (float)g.width / (float)g.height;
            g.camera->updateProjectionMatrix();
        }
        if (g.stillwater) g.stillwater->resize(g.width, g.height);
        glViewport(0, 0, g.width, g.height);
    }

    gpuTimerBegin();
    if (g.stillwater && g.sceneEnabled) {
        // MScene：场景完全接管画面（含清屏），threepp 只作为备用演示
        pollSceneInput();
        mate::FrameContext fc;
        fc.width  = g.width;
        fc.height = g.height;
        fc.dt     = dt;
        fc.time   = g.animTime;
        g.stillwater->update(fc);
        g.stillwater->render(fc);
    } else if (g.renderer && g.scene && g.camera) {
        g.renderer->render(*g.scene, *g.camera);
    } else {
        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    gpuTimerEnd();

    renderUI();   // 面板叠在 3D 画面之上

    const double swapT0 = nowSeconds();
    SwapBuffers(g.hdc);
    const double swapT1 = nowSeconds();

    g.swapAcc += swapT1 - swapT0;
    g.swapFrames++;
}

/*==================== 消息泵 ====================*/
// 泵一轮消息，返回 false 表示收到 WM_QUIT。
// P8：低级鼠标钩子是在本线程的消息循环里回调的，暂停时那 1s 的 Sleep 会把
// 交互和热键一起卡住，所以限帧等待期间也要继续泵
static bool pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

/*==================== 入口 ====================*/
// x0：GUI 子系统，没有控制台（日志在窗口内面板里看）
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    (void)hInstance;

    // 与 Explorer 使用同一 DPI 感知级别，保证跨进程 SetParent 可用
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
    timeBeginPeriod(1);   // 提高 Sleep 精度，帧率更贴近 60fps

    // MScene 场景层统一走这条日志桥，输出到窗口内日志面板
    mate::setLogSink([](const char* line) { mateLog("%s", line); });

    mateLog("========================================");
    mateLog(" Mate - Win32 + OpenGL 4.5 + threepp（无 GLFW）");
    mateLog(" 不内嵌资源：字体走磁盘候选，模型扫描目录");
    mateLog(" 启动模式: %s", kStartInWallpaper ? "壁纸" : "调试");
    mateLog(" F5 切换模式 / F6 面板 / F7 暂停 / F8 壁纸交互 / F9 复位 / ESC 退出");
    mateLog("========================================");

    // P7：Explorer 重启广播，提前注册好消息号（0 表示注册失败，忽略即可）
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    logMonitors();

    bool wallpaperMode = kStartInWallpaper;
    if (!buildWindow(wallpaperMode)) {
        mateLog("启动失败，退出");
        teardownWindow();
        timeEndPeriod(1);
        return 1;
    }

    double lastTime = nowSeconds();
    double fpsTimer = lastTime;
    int    fpsFrames = 0;

    while (g.running) {
        if (!pumpMessages()) {
            g.running = false;
            break;
        }

        // 模式切换：原地改样式 + 换父窗口，窗口 / GL 上下文 / threepp 场景全部保留，
        // 不重建任何东西，所以切过去第一帧就是满帧。
        if (g.toggleMode) {
            g.toggleMode = false;

            if (!g.wallpaper) {
                mateLog("[模式] 切换为 壁纸 模式");
                if (mountWallpaper()) {
                    g.wallpaper = true;
                    syncClientSize();
                    mateLog("[壁纸] 挂载后客户区 %dx%d", g.width, g.height);
                    // 壁纸层窗口在 SHELLDLL_DefView 之下，收不到任何鼠标消息，
                    // 进壁纸就直接挂低级钩子；F8 仍可切回"完全穿透桌面"
                    setInteractive(true);
                    mateLog("[交互] 壁纸鼠标交互已开启（F8 可切穿透）");
                } else {
                    mateLog("[模式] 挂载失败，保持调试模式");
                }
            } else {
                mateLog("[模式] 切换为 调试 模式");
                g.wallpaper = false;   // 立刻按调试模式命中测试，避免残留穿透
                setInteractive(false); // P8：交互模式只对壁纸层有意义
                if (!unmountWallpaper()) {
                    // 父窗口被销毁时会连带销毁子窗口（Explorer 重启等），只能整窗重建
                    mateLog("[壁纸] 窗口已失效，整窗重建");
                    teardownWindow();
                    if (!buildWindow(false)) {
                        mateLog("[模式] 切换失败，退出");
                        break;
                    }
                }
            }

            // 重置计时与帧率统计，否则切换耗时会被算进下一个采样窗口
            lastTime = nowSeconds();
            fpsTimer = lastTime;
            fpsFrames = 0;
            continue;
        }

        // P7：壁纸窗口被 Explorer 连带销毁后，整窗重建（GL 上下文必须重来）
        if (g.rebuildWindow) {
            g.rebuildWindow = false;
            mateLog("[壁纸] 整窗重建中...");
            teardownWindow();
            if (!buildWindow(g.wallpaper)) {
                mateLog("[壁纸] 重建失败，退出");
                g.running = false;
                break;
            }
            lastTime = nowSeconds();
            fpsTimer = lastTime;
            fpsFrames = 0;
            continue;
        }

        // P7：壁纸窗口挂在 Explorer 的 WorkerW 下，Explorer 退出时窗口会被连带销毁，
        // 但实测收不到 WM_DESTROY（也不会收到广播消息，子窗口不在广播范围内），
        // 只能定时体检句柄；失效就走下面的重新挂载（内部会整窗重建）
        if (g.wallpaper && !g.remount) {
            const double checkNow = nowSeconds();
            if (checkNow - g.lastWindowCheck > 0.5) {
                g.lastWindowCheck = checkNow;
                if (!g.canvas || !g.canvas->isOpen()) {
                    mateLog("[壁纸] 窗口已失效（未收到 WM_DESTROY），走重建");
                    if (g.canvas) g.canvas->onExternalDestroy();
                    g.remount = true;
                } else if (g.fullscreenFallback) {
                    // P7：兜底期间轮询桌面 shell（Progman + SHELLDLL_DefView）。
                    // TaskbarCreated 广播实测不可靠，只能自己看它什么时候回来，
                    // 回来就重挂回壁纸层（只在 false->true 跳变时触发，避免反复重建）
                    const bool ready = desktopShellReady();
                    if (ready && !g.shellReady) {
                        mateLog("[壁纸] 桌面 shell 已恢复，准备回挂");
                        g.remount = true;
                    }
                    g.shellReady = ready;
                }
            }
        }

        // 重新挂载
        if (g.remount) {
            remountWallpaper();
            if (!g.running) break;
        }

        // P8：低功耗检测（每 2s）。前台有全屏应用/演示模式时壁纸根本看不见，降到 5fps
        {
            const double powerNow = nowSeconds();
            if (powerNow - g.lastPowerCheck > 2.0) {
                g.lastPowerCheck = powerNow;
                const bool fs = fullscreenAppForeground();
                if (fs != g.lowPower) {
                    g.lowPower = fs;
                    mateLog("[低功耗] %s，上限 %d fps",
                            fs ? "前台有全屏应用/演示模式" : "前台全屏应用已退出", effectiveFpsCap());
                }
            }
        }

        double frameStart = nowSeconds();
        float dt = (float)(frameStart - lastTime);
        lastTime = frameStart;
        if (dt > 0.1f) dt = 0.1f;   // 切窗口/睡眠后避免大跳变

        renderFrame(dt);

        // 帧率统计（每 2 秒一次）
        fpsFrames++;
        if (frameStart - fpsTimer >= 2.0) {
            const double swapMs = g.swapFrames ? g.swapAcc / g.swapFrames * 1000.0 : 0.0;
            mateLog("[帧率] %.1f fps（%dx%d） swap %.1fms",
                    fpsFrames / (frameStart - fpsTimer), g.width, g.height, swapMs);
            fpsTimer = frameStart;
            fpsFrames = 0;
            g.swapAcc = 0.0;
            g.swapFrames = 0;
        }

        // 限帧等待。P8：低功耗档（暂停 1fps / 全屏应用 5fps）低于 15，不能被钳制抬回去；
        // 等待期间按 4ms 分片泵消息，保证低级鼠标钩子与热键不被 Sleep 卡住
        int cap = effectiveFpsCap();
        if (!g.paused && !g.lowPower) {
            cap = cap < 15 ? 15 : (cap > 240 ? 240 : cap);
        }
        if (cap < 1) cap = 1;
        const double deadline = frameStart + 1.0 / (double)cap;
        while (g.running) {
            const double left = deadline - nowSeconds();
            if (left <= 0.0) break;
            // 一次睡到只剩 4ms，再按 1ms 收尾；分成多个 4ms 小睡会把帧率压到 40~50
            if (left > 0.006) {
                Sleep((DWORD)((left - 0.004) * 1000.0));
            } else if (left > 0.0015) {
                Sleep(1);
            } else {
                Sleep(0);
            }
            if (!pumpMessages()) {
                g.running = false;
                break;
            }
        }
    }

    teardownWindow();
    timeEndPeriod(1);
    mateLog("已退出");
    return 0;
}
