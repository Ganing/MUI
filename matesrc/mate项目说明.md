# Mate 项目说明

> Windows 壁纸引擎 —— Win32 + threepp + ImGui + FFmpeg + miniaudio 单 exe 桌面特效应用
> 建立日期：2026-10-25
> 当前版本：v5（P10 音视频打磨：局部上传 + 时间轴缩略图 + 播放列表拖拽/按目录建队列；P1~P10 全部实测通过）+ matex0（P12：OpenGL 4.5 / Win32 Canvas 分支，2026-10-28）+ matex1（Stillwater 程序化场景）+ matex2（P13：threepp 示例学习沙箱）+ P14 视频帧源独立成类 / 解码时直接缩放 / YUV420P 三平面 + 双 PBO / 接入 StillWater 场景层（2026-11-07）
> 场景系统设定（MScene / ma.bin / mate.json / 层与 Lua）见 §十；引擎底座已由 matex0 复刻并拆成 `src/` 多文件，产物 `mate.exe`（2026-11-06）

---

## 一、项目概述

Mate 是一个 **Windows 桌面壁纸应用程序**：在桌面图标下方渲染 3D 场景、全屏 Shadertoy 特效以及 UI 参数面板，把桌面背景变成可交互的实时图形程序。

**最终交付形态**：单个 exe，无需安装、无额外 DLL 依赖，双击即运行并接管系统壁纸。

### 与 MX 的定位差异

| 维度 | MX（现有） | Mate（本项目） |
|---|---|---|
| UI | ThorVG 矢量渲染（`oui.h` / `sui.h`） | Dear ImGui |
| 3D / 渲染封装 | 自研 `ogl.h`（直接写 GL） | threepp（C++ 版 Three.js，场景图） |
| 代码形态 | 单头文件模块库 `MO/*.h` + 项目 cpp | 按版本单文件 `matevN.cpp` |
| 目标 | 通用图形模块库 | 一体化壁纸应用 |

两者共用根目录 `e:\MX` 下的 `3rd/`（三方库）与 `Data/`（模型、字体、视频）资源。

---

## 二、技术栈

| 项 | 选型 | 说明 |
|---|---|---|
| 语言 | C++20 | |
| 窗口 | 纯 Win32 API | 不使用 GLFW |
| 图形 API | OpenGL 3.3 Core（matev1~v5）/ 4.5 Core（matex0） | `wglCreateContextAttribsARB` 建核心上下文并带回退链；matex0 已把 glad 升到 0.1.36（4.5 compatibility），见 §四 v11 |
| 3D 引擎 | threepp | mrdoob/threepp，仅用其渲染器/场景图/加载器，已剥离 GLFW（见 §四 v3、§六.3） |
| UI | Dear ImGui 1.91.5 | Win32 + OpenGL3 后端 |
| 字体 | FreeType（`imgui_freetype.cpp`） | P8 起替换 ImGui 内置 stb_truetype，`LightHinting` 栅格化（见 §四 v8） |
| 音视频解码 | FFmpeg（静态库） | 解码逻辑复用 `MO/xav.h`（header-only，从 MPlayer 抽出），A/V 同步以音频时钟为主（见 §四 v9） |
| 音频输出 | miniaudio | 单头文件（`MINIAUDIO_IMPLEMENTATION` 在本 TU 定义）：环境音/频谱走 `ma_device` 回调，视频音轨走 `ma_engine` + 3D 空间化（§10.9、§11.5） |
| 频谱 | 自写基 2 FFT | `fftRadix2` + Hann 窗，压成 64 柱带衰减，ImGui 前景层绘制 |
| CRT | MSVC /MT 静态链接 | 单 exe |
| 构建 | `cl` + `mate.bat` | threepp 由 `3rd/src/threepp/bld_threepp.bat` 预编为 `/MT` 静态库，ImGui 源码随 `mate.bat` 直接编译 |

---

## 三、目录结构

```
e:\MX\Projects\Mate\
├── mate项目说明.md      # 本文件
├── matev1.cpp           # P1 壁纸骨架（已完成）
├── matev2.cpp           # P2 ImGui 面板（已完成）
├── matev3.cpp           # P3~P6 threepp 渲染，无 GLFW（已完成）
├── matev4.cpp           # P9 音视频：视频壁纸 + 音频 + 频谱（已完成，可回退快照）
├── matev5.cpp           # P10 音视频打磨：局部上传 + 时间轴缩略图 + 播放列表（当前版本）
├── matex0.cpp           # P12 matex 分支：OpenGL 4.5 + Win32 Canvas + OrbitControls（实验版本）
├── matex1.cpp           # x1 分支：Stillwater 程序化场景（学习/素材参考，不做产品）
├── matex2.cpp           # P13 threepp 学习沙箱：01_intro/11_texture2d（TextureLoader/Reflector/OrbitControls）
├── matex.rc             # x0/x2 资源脚本：仅窗口图标(103/104)，不含字体/模型
├── matex0.bat           # x0 编译脚本：matex0.bat [debug|wallpaper] [run]
├── matex2.bat           # x2 编译脚本：matex2.bat [debug] [run]（console 子系统，无 ImGui）
├── mate.rc              # 资源脚本：内嵌字体(101) + 默认模型(102) + 窗口图标(103/104)
├── mate.bat             # 编译脚本：mate.bat [v1|v2|v3|v4|v5|x1|mate] [debug|wallpaper] [run]
├── mate.exe             # 产品引擎产物（mate.bat mate 生成）
├── src\                 # 产品源码（多文件，接替 mate.cpp 单文件）
│   ├── app\main.cpp     # 应用外壳：窗口/WorkerW/热键/主循环/输入分发
│   ├── core\mate_core.* # VFS/纹理/GLSL 程序/帧缓冲/IScene
│   ├── core\mate_audio.*# 无缝循环环境音（miniaudio）
│   ├── core\mate_audio3d.*# 视频音轨 -> miniaudio 3D 空间音源（ma_engine）
│   ├── core\mate_video.h# 视频帧源：解码线程 + 解码时直接缩放（不含图形 API）
│   └── scenes\stillwater\stillwater.cpp   # 首个 native 场景
├── Data\MScene\         # 场景包根目录（开发期直接用文件夹）
│   └── Stillwater\      # scene.json + layer/water.frag + data/*.png|wav|mp4
├── Data\textures\       # matex2 示例贴图：checker.png / crate.gif / brick_bump.jpg
├── matev4_resume.txt    # 运行期生成：断点续播（第 1 行 UTF-8 路径，第 2 行秒数；v5 沿用同名文件）
├── MData\               # 运行时配置/资源（待建）
└── backups\             # 版本快照（待建）
```

三方依赖统一放在 `e:\MX\3rd`：

| 依赖 | 位置 | 状态 |
|---|---|---|
| ImGui 1.91.5（含 Win32/OpenGL3 后端 + `misc/freetype`） | `3rd/include/imgui`（已从 `E:\XKU\imgui` 复制，已纳入 `mate.bat`） | ✅ 已就位（`imgui_freetype.*` 于 P8 从 jsdelivr 补入） |
| threepp | `3rd/include/threepp`（369 头）+ `3rd/src/threepp`（GL-only 源码/生成物）+ `3rd/lib/threepp_mt.lib` | ✅ 已就位（2026.10.11，GitHub 直连不通，改从 PyPI sdist 取） |
| glad（GL 加载器） | `3rd/include/glad` + `glad.c` | ✅ 已有 |
| stb / tinygltf / glm | `3rd/include` | ✅ 已有 |
| FreeType | `3rd/ffmpeg/FF/lib/freetype.lib`（/MT）+ `include/freetype2` + `3rd/include/imgui/imgui_freetype.{h,cpp}` | ✅ 已接入 |
| FFmpeg SDK | `3rd/ffmpeg/FF/{include,lib}`（`libavformat/libavcodec/libavutil/libswscale/libswresample.a` + 30 余个编解码静态库） | ✅ P9 接入，链接需 `/FORCE:MULTIPLE /IGNORE:4006,4088` |
| miniaudio | `3rd/include/miniaudio/miniaudio.h`（单头文件） | ✅ P9 接入 |
| 解码器封装 | [xav.h](file:///e:/MX/MO/xav.h)（`3rd` 之外，项目头） | ✅ 复用 MPlayer 验证过的实现 |

---

## 四、版本更新点

### v1 — 壁纸骨架（P1，已完成）

**目标**：验证"能被 Windows 认成壁纸"这件事，其余一切为空。

**实际实现（`matev1.cpp` + `mate.bat`）**：

- 注册窗口类 → 创建 OpenGL 3.3 上下文（临时窗口 `wglCreateContextAttribsARB` 建 3.3 Core，失败回退兼容上下文）
- 桌面结构识别：Modern（Progman 直接持有 `SHELLDLL_DefView`）/ Legacy 双分支
- 父窗口候选按优先级收集后逐个尝试 `SetParent`，跳过隐藏或小于半屏的窗口；Modern 结构下优先挂进 **Progman 直属的全屏 `WorkerW`**（与 Wallpaper Engine 同构），失败再退回直接挂 Progman
- 壁纸模式窗口形态：`WS_OVERLAPPEDWINDOW` 顶层窗口（1280x720 居中，带标题栏）→ 挂载时改 `WS_CHILD | WS_VISIBLE` + `WS_EX_LAYERED`（alpha 255）
- z 序：挂 `WorkerW` 时由父子层级天然保证在图标层之下；退回挂 Progman 时用 `SetWindowPos(hwnd, progmanDefView, ...)` 插到 `SHELLDLL_DefView` 正下方
- 空渲染循环：清屏 + 正弦色带渐变，锁 60fps（`timeBeginPeriod(1)` + 粗睡 + `Sleep(0)` 自旋补齐），壁纸模式下每帧 `DwmFlush`
- `BUILD_WALLPAPER` 宏决定启动模式，运行期 F5 全局热键双向切换（切模式 = 销毁窗口 + 全量重建）
- 点击穿透：`WM_NCHITTEST` 返回 `HTTRANSPARENT` + `WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATE`（仅在壁纸模式生效，调试模式仍可拖动）
- 控制台中文：`SetConsoleOutputCP(CP_UTF8)`，与 `/utf-8` 编译的源码一致（默认 GBK 代码页会乱码）
- Explorer 重启 / 分辨率变化：`WM_SETTINGCHANGE` / `WM_DISPLAYCHANGE` 触发重挂，窗口已失效则整窗重建

**踩坑结论与对照（本机 Win11 24H2，Progman 直挂 DefView）**：

1. Modern 结构下壁纸由 Progman 自己绘制 —— 挂在顶层 `WorkerW` 上会被 Progman 的壁纸盖住，壁纸窗口必须落在 **Progman 子树内**，并且 z 序在 `SHELLDLL_DefView` 之下（可直接挂 Progman 插到 DefView 后，也可挂 Progman 直属的全屏 `WorkerW`）。
2. 扩展样式只能加 `WS_EX_LAYERED`；实测再附加 `WS_EX_TRANSPARENT / NOACTIVATE / TOOLWINDOW` 会让窗口在桌面完全不呈现。
3. **窗口必须先在普通顶层窗口形态下被 DWM 合成过一次，再 `SetParent`。** 一创建就是全屏无边框窗口会跳过合成，导致挂载后桌面看不到画面（但 `GetPixel` 能读到 GL 内容）。所以启动即壁纸时也要先建 1280x720 普通窗口、渲染并泵消息 2 秒后再挂载。
4. **参照 Wallpaper Engine 实测层级**（本机 WE 1.x，`wallpaper64.exe`）：
   ```
   Progman
   ├─ [z0] SHELLDLL_DefView                       style=0x56010000  ex=0x80000
   └─ [z1] WorkerW              (0,0,2560,1440)   style=0x58000000  ex=0x80000A0
        └─ WPEVideoWallpaper   (0,0,2560,1440)   style=0x50000000  ex=0x8080000
                                   layered=TRUE alpha=255 LWA_ALPHA
   ```
   WE 把壁纸窗口放在 Progman 直属、z 序位于 DefView 之后的全屏 `WorkerW` 里，z 序由父子层级保证，不依赖每次 `SetWindowPos` 相对 DefView 插队；分层属性（`WS_EX_LAYERED` + `LWA_ALPHA 255`）与 Mate 完全一致。Mate 已改为同构挂载，失败才退回直接挂 Progman。

**实测验收（2026-10-25）**：壁纸模式 2560x1440 @60fps，屏幕像素随时间变化（确认为 Mate 着色器输出而非系统壁纸）；桌面图标正常显示；F5 双向切换正常；ESC 正常退出。

### v2 — ImGui 面板（P2，已完成）

**目标**：在 v1 壁纸骨架上接入 Dear ImGui，把渲染参数暴露成可实时调节的面板。

**实际实现（`matev2.cpp` + `mate.bat`）**：

- ImGui 上下文 + `imgui_impl_win32` / `imgui_impl_opengl3` 后端，初始化顺序固定为 `initOpenGL()` → `initImGui()`（后端需要 GL 上下文为 current）
- 中文字体按候选路径逐个尝试，命中即用：`e:/MX/Data/siyuan.ttf` → `./siyuan.ttf` → `c:/windows/fonts/msyh.ttc`，字符集 `GetGlyphRangesChineseSimplifiedCommon()`；全部失败才退回内置默认字体（日志会提示中文将显示为方块）
- `io.IniFilename = nullptr`，不落盘 `imgui.ini`，保持项目目录干净
- 面板内容：分辨率 / 实时帧率 / 当前模式 / 字体状态 + 帧率上限滑块（15~240）、动画速度滑块（0~4）、底色/色带A/色带B 三色编辑器、"切换壁纸模式 (F5)" 与"退出"按钮、ImGui 示例窗口开关
- 着色器由硬编码渐变改为 uniform 驱动（`uColor1/2/3`、`uSpeed`），面板改动直接生效
- 帧率上限改为运行时可变（`g.fpsCap` 在每帧循环内换算 `targetFrameTime`）
- 热键分工：**F5 = 调试↔壁纸切换，F6 = 面板显隐**；壁纸模式下不绘制面板（即使 `showPanel` 为真）
- 消息转发：`MateWndProc` 开头把键鼠消息交给 `ImGui_ImplWin32_WndProcHandler`，但排除 `WM_HOTKEY` 与 `WM_KEYDOWN/VK_ESCAPE`，否则热键和退出会被 ImGui 吞掉
- `shutdownImGui()` 在 `wglDeleteContext` **之前**调用（需 GL 上下文仍 current 才能释放后端 GL 资源）；`UnregisterHotKey` 在窗口销毁前执行
- `mate.bat` 支持版本选择：`mate.bat [v1|v2] [debug|wallpaper] [run]`，v2 额外编入 `3rd/include/imgui` 的 5 个核心源文件 + 2 个后端源文件

**踩坑结论**：

1. **imgui 1.91 起后端不再在头文件里导出窗口过程**，`imgui_impl_win32.h` 中该声明被 `#if 0` 包住，必须自己前置声明：
   `extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);`
   否则 `error C3861: 找不到标识符`。
2. **F5 切换改为原地换父窗口，不重建窗口。** 早期实现是"销毁窗口 + 全量重建"，每切一次都要重建 GL 上下文、重编着色器、重新加载中文字体并重新预热，切换耗时还会被算进下一个帧率采样窗口（表现为首帧掉到 ~18fps）。现在改为：`mountWallpaper()` / `unmountWallpaper()` 只改窗口样式与父子关系（`WS_CHILD` ⇄ `WS_OVERLAPPEDWINDOW`、`WS_EX_LAYERED` 加减、`SetParent`、`SetWindowPos`），窗口、GL 上下文、ImGui 上下文、字体图集全程保留，切换后首帧即 60fps。切到调试模式前先把 `g.wallpaper` 置 false，避免残留 `HTTRANSPARENT`；摘除时先 `SetParent(hwnd, nullptr)` 再移除 `WS_CHILD`。只有父窗口连带销毁（Explorer 重启）时才退回整窗重建。

**实测验收（2026-10-25）**：`mate.bat v2 debug` 编译 0 warning；面板中文字形正常（日志 `[ImGui] 字体: e:/MX/Data/siyuan.ttf`）；F6 隐藏/显示面板正常；F5 切壁纸模式后面板不再绘制、渐变换出、挂载到 `WorkerW 0x101A0`；连续多次 F5 切换稳定，切换后首个采样即 60.4fps（无重建、无字体重载日志）；调试态样式经检查为 `WS_CHILD=0 / WS_CAPTION=1 / WS_EX_LAYERED=0`、客户区 1280x720、`WM_NCHITTEST` 返回 `HTCLIENT`；壁纸态为 `WS_CHILD=1 / WS_EX_LAYERED=1`、`WM_NCHITTEST` 返回 `-1`、`WindowFromPoint` 命中桌面图标层（穿透有效）、桌面像素随时间变化；滑块调帧率上限即刻生效。

### v3 — threepp 接入（P3，已完成）

**目标**：把 threepp 当纯渲染器接进来，窗口与 GL 上下文仍由 Mate 自建，彻底不碰 GLFW。

**关键结论：不需要 `Win32Canvas`。** 上游 `GLRenderer` 有一个 size-only 构造函数
`GLRenderer(std::pair<int,int> size)`，官方注释明确写着「host application has already made a context current ... Rendering targets a framebuffer object, so no swapchain and no window is involved」。它内部只做 `if (!gladLoaded()) loadGlad()` + `glEnable(GL_PROGRAM_POINT_SIZE)`，渲染到 FBO、末尾自行 blit 回默认 framebuffer，`SwapBuffers` 仍由 Mate 负责。原先规划的实现 `Canvas` 契约方案被这条路径直接取代。

**threepp 静态库构建（`3rd/src/threepp/bld_threepp.bat` + `bld_threepp.ps1`）**：

- 来源：GitHub 直连与全部 gh-proxy 镜像均不可用，改从 PyPI sdist 取 `threepp-2026.10.11.tar.gz`；369 个头进 `3rd/include/threepp`，GL-only 源码/生成物进 `3rd/src/threepp`（两者 `*.hpp` 无重名）
- 源清单按上游 CMakeLists 的 GL-only 分支生成，剔除 `Canvas.cpp` / `GlfwWindow*` / `EglContext` 等窗口相关文件，共 **331 个 cpp**
- 生成源用 `gen_embed.ps1` 复刻上游 `embed.cmake`：把 141 个 `.glsl`（107 ShaderChunk + 34 ShaderLib）内嵌成 `ShaderChunk.cpp` 的头文件字符串、字体转 `EmbeddedFonts.cpp` 的 hex 数组
- 编译参数 `/O2 /EHsc /std:c++20 /MT /utf-8 /bigobj /MP`；`libwebp` 的 include 必须排最前（上游 CMake 用 `BEFORE`）
- 产物：`3rd/lib/threepp_mt.lib`，326.5 MB / 331 objects，0 error（仅 C4244/C4250/C4305 warning）；脚本支持增量，`bld_threepp.bat gen` 先重跑生成源再编

**`matev3.cpp`（约 1000 行）与 v2 的差异只在地基**：

- **glad 统一**：threepp 自带 glad（GL 4.1 compatibility）且全进程一次性加载，与 MX 的 3.3 core glad 同名冲突。v3 完全弃用 MX glad，只链接 `threepp_mt.lib` 里那份；`mate.bat` 的 v3 分支把 `GLAD_INC` / `GLAD_SRC` 清空
- **`loadGlad()` 时机**：临时上下文上**不**加载 glad，只在最终上下文 `wglMakeCurrent` 成功后调一次 `threepp::loadGlad()`
- **WGL 声明**：threepp 的 glad 不带 `GL/gl.h`，`wglCreateContext` / `wglMakeCurrent` / `wglDeleteContext` / `wglGetProcAddress` 需自行 `__declspec(dllimport)` 声明
- **`Canvas::initWindow` 空桩**：`GLRenderer.obj` 里同时含 `GLRenderer(Canvas&)` 构造函数，它引用 `Canvas::initWindow`；不编 `Canvas.cpp`（GLFW 路径）就会 LNK2019，故在 `matev3.cpp` 里给个空实现骗过链接器（永不调用）
- **场景**：`AmbientLight(0xffffff, 1.1)` + 主/补两盏 `DirectionalLight`；启动时扫描模型目录（`resolveModelDir()`：优先 exe 同级 `models/`，否则 `e:/MX/Data/models`）下所有 `.glb` / `.gltf`（忽略大小写排序），面板下拉框切换，一次渲染一个，选中项在下一帧帧首加载（先加载成功再卸旧的，失败不会清空场景）
- **相机自动摆位**：`Box3::setFromObject` 取包围盒 → 模型平移使中心归零 → 半径 `0.5*sqrt(x²+y²+z²)` → `distance = radius / sin(fov/2) * 1.25`，近/远裁剪面也按半径换算（目录里大模型 ~45 半径、小模型不到 1），换任意 glb 都不用调相机；模型挂在 `Group` 上做整体旋转
- **尺寸同步**：`renderFrame()` 比较 `g.width/g.height` 与已应用尺寸，变了才 `renderer->setSize()` + 更新 `camera->aspect` + `glViewport`
- **释放顺序**：`shutdownImGui()` → `renderer.reset()`（先让 threepp 释放自己的 GL 对象）→ 场景/相机/模型

**踩坑结论**：

1. **`.ps1` / `.bat` 必须纯 ASCII。** PowerShell 5.1 按 ANSI(GBK) 解码无 BOM 文件，行尾的中文注释会吞掉换行符，把下一条语句粘进注释里，报 `Unexpected token ')'`。两个脚本顶部都写了原因说明。
2. **cmd 单条命令行上限 8191 字符。** 331 个 obj 全路径必须走 `lib @objs.rsp`；同理编译走 `stale.rsp`，并要在编译前删掉旧 `compile.log`、清理遗留 rsp，否则会读到上一轮的产物假装「只编 1 个文件」。
3. **`cl /Fo"build\"` 的 `\"` 会被当成转义引号**，把后面的 rsp 路径整段吞进选项 → 必须写 `/Fobuild\`（不带引号）。
4. **`gen_embed.ps1` 的 `@THREEPP_SHADER_INCLUDES@` 必须同时含两组头。** 最初把 `$includes` 局部化，只填了 107 个 ShaderChunk 头，34 个 ShaderLib 头缺失 → `ShaderChunk.cpp` 报大量 `C2653: "shaderlib" 不是类或命名空间名称`。
5. **ImGui 中文字形**：`GetGlyphRangesChineseSimplifiedCommon()` 只有 2500 常用字，「帧」不在其中会显示成 `?`。用 `ImFontGlyphRangesBuilder::AddText(...)` 把面板实际用到的字补进范围。
6. **ImGui 面板宽度**：`AlwaysAutoResize` 不计入滑块右侧标签的宽度，「帧率上限」「旋转速度」被裁掉 → 去掉该 flag，窗口固定 420。

**实测验收（2026-10-25）**：

- `[GL] 版本: 3.3.0 NVIDIA 596.36` / `设备: NVIDIA GeForce RTX 4070`；`[threepp] 模型已就位（半径 1.645，相机距离 5.372）`；`[ImGui] 字体: e:/MX/Data/siyuan.ttf`
- 调试窗口 1280x720 稳定 60fps，DamagedHelmet PBR 正常，中文面板字形完整
- F5 切壁纸：挂到 `WorkerW 0x101A0`（2560x1440），**连续 7 次采样均 60.0fps**；F5 切回 1280x720 仍 60.0fps（沿用 v2 的原地换父窗口，不重建）
- 桌面壁纸层模型正常渲染、图标在其上方、点击穿透有效
- exe 内除启动横幅里的「无 GLFW」说明文字外，无任何 GLFW 字符串/依赖
- **附加发现**：v3 在 2560x1440 壁纸下是满帧 60fps，而 v2 同一场景曾实测掉到 ~42fps，threepp 路径反而没有该问题

### v4 — Shadertoy 全屏层（P4，已完成）

**实际实现（`matev3.cpp` 内，未另开 `matev4.cpp`）**：

- 直接 `#include "../momo/fxcode.h"`，复用 MX 现成的 4 个 Shadertoy 特效：动态矩形 / 梵高日落 / 细雨 / 启动画面。每个源码自带 `main()` 把 `gl_FragCoord` 转发给 `mainImage`，所以不需要再包一层
- 全屏层用 threepp 原生方式实现：`PlaneGeometry(2,2)` + `ShaderMaterial`，顶点着色器直接 `gl_Position = vec4(position.xy, 0.9999, 1.0)`（绕过相机变换，永远铺满屏幕），`frustumCulled = false`
- **关键：`renderOrder = -1000` + `depthTest/depthWrite = false`**。不透明物体按 `renderOrder` 升序绘制，特效层第一个画且不写深度，3D 模型随后自然盖在上面；不能改成「先自己用裸 GL 画一遍再让 threepp 渲染」，因为 `GLRenderer::render()` 末尾会把内部 FBO blit 回默认 framebuffer，覆盖掉之前画的内容
- uniform 每帧刷新：`iTime`（独立累加）、`iResolution`、`iMouse`（左下原点，`zw` 按 momo 约定在按下时为正、否则为负）、`iMouseXY` / `iMouseZW`（细雨用）、`texSize`
- 细雨 / 动态矩形额外采样 `sceneTex` / `lyricsTex`，当前没有画面捕获与歌词层，绑 1x1 黑纹理占位，避免采样未绑定单元
- 面板「特效」下拉切换预设（0 = 关闭），换 shader 走 `material->needsUpdate()` 触发热重编译

**实测验收（2026-10-25）**：梵高日落 / 启动画面 全屏铺满且 3D 模型正确叠在上层、面板在最上层；调试 1280x720 与壁纸 2560x1440 均 60fps；F5 双向切换正常；细雨因 `sceneTex` 为黑纹理只显示雨丝（等 P5 接入画面捕获后才有完整效果）；无 GL 报错。

### v5 — 三层合成与调参（P5，已完成）

**实际实现（`matev3.cpp`）**：

- **三层顺序**：Shadertoy 特效（`renderOrder = -1000`，`depthTest/Write = false`）→ 3D 模型（正常深度）→ ImGui（`renderUI()` 最后叠加）。三层共用一个 threepp 场景和一次 `render()` 调用
- **帧率锁**：滑块 15~240 + 快捷档 `30 / 60 / 120 / 不限`（独立一行，避免被面板宽度裁切）；限帧逻辑不变（粗睡 + `Sleep(0)` 自旋补齐）
- **暂停/恢复**：面板按钮 + F7。实现方式是 `dt = 0`（`animTime` 与 `fxTime` 同时冻结），渲染与面板照常刷新，不是停止渲染
- **GPU 占用观测**：两个 `GL_TIME_ELAPSED` query 乒乓，本帧开始计时同时取上一帧结果，不产生同步等待；前两帧 query 尚未提交，跳过读取以免 `GL_INVALID_OPERATION`。面板显示 `GPU xx.xx ms` 与 CPU 帧时间
- **相机**：`视野` 20~90°、`距离倍率` 0.3~3.0（相对包围盒自动摆位距离）。near/far 每帧按当前距离重算，拉远时不会被远裁剪面切掉
- **光照**：环境光 / 主光 / 补光强度实时可调（保留灯的 shared_ptr 直接改 `intensity`）
- **特效强度**：`buildFxFragment()` 把 fxcode 的 `void main()` 改名为 `fxEntry()`，再包一层 `main()` 统一乘 `fxGain`（0~3），从而在不改 MX 原有 shader 的前提下调节亮度

**实测验收（2026-10-25）**：60fps 时 GPU 0.07~0.08 ms、细雨开启后 1.18~2.78 ms（计时器响应正常）；点 `30 fps` 后日志稳定 30.0 fps、面板 33.3 ms；F7 暂停后动画停止、帧率保持；距离倍率 3.00 时模型完整可见无裁剪；F5 切壁纸 2560x1440 双向均 60fps。

### v6 — 单 exe 打包（P6，已完成）

**实际实现（`matev3.cpp` + `mate.rc`）**：

- `mate.rc` 把字体与默认模型编进 exe 自己的 RC 资源段（`IDR_FONT = 101`、`IDR_MODEL = 102`，都是 `RCDATA`）；`mate.bat` 在 `cl` 之前先执行 `rc /nologo /fo mate.res mate.rc`，再把 `mate.res` 作为输入交给链接器（仅 v3 定义 `RES_FILE`，v1/v2 不受影响）
- `loadEmbeddedResource(id, out)`：`FindResourceW` → `LoadResource` → `LockResource` → `SizeofResource`，按字节数拷进 `std::vector<unsigned char>`。数据常驻 exe 镜像，不需要释放
- **字体**：`io.Fonts->AddFontFromMemoryTTF(g.embeddedFont.data(), size, 18.0f, &cfg, ranges)`，其中 `cfg.FontDataOwnedByAtlas = false`（缓冲区由程序持有，不能让 ImGui 去 free 资源段内存）。内嵌命中就不再去试磁盘候选路径
- **模型**：`ModelEntry { name, path }`，`path` 为空表示走内存数据。列表第 0 项固定是 `DamagedHelmet.glb（内嵌）`，由 `GLTFLoader::load(const std::vector<uint8_t>&)` 解析（GLB 自包含，不需要 basePath）；目录里与内嵌同名的文件会被跳过，列表里不会出现两个
- **模型目录**：`resolveModelDir()` 优先取 exe 同级的 `models/`（拷走 exe + `models` 目录即可分发），没有才退回开发机的 `e:/MX/Data/models`；目录扫描、忽略大小写排序、面板下拉切换的行为都不变
- `/MT` 全静态链接，不依赖 MSVC 运行库 DLL

**实测验收（2026-10-25）**：

- exe 体积 3.25 MB → **15.45 MB**（16,198,656 字节，其中字体 8.9 MB + DamagedHelmet 3.6 MB）
- 目录齐全：`[P6] 内嵌模型已载入内存（3.6 MB）` / `[模型] 共 12 个（内嵌 1 个 + 目录 11 个）` / `[ImGui] 字体: 内嵌资源（8754 KB）`，1280x720 稳定 60fps
- **隔离验证**：把 `e:\MX\Data\models` 临时改名后启动，仍能出画面（`[模型] 加载 1/1: DamagedHelmet.glb（内嵌）`、中文面板正常、60fps），证明字体与默认模型确实来自资源段而非磁盘
- **分发验证**：把 exe 与只含 1 个 glb 的 `models/` 一起拷到 `%TEMP%`，进程序列出 `共 2 个（内嵌 1 个 + 目录 1 个）`，面板显示 `1/2`

### v7 — 健壮性（P7，已完成）

**实际实现（`matev3.cpp`）**：

- **多显示器（整块虚拟桌面，不做每屏独立窗口）**：新增 `ScreenRect virtualScreenRect()`，取 `SM_XVIRTUALSCREEN / SM_CXVIRTUALSCREEN`（远程桌面或驱动异常返回 0 时退回主屏 `SM_CXSCREEN`）。`mountWallpaper()` 的父窗口判据、尺寸兜底、`remountWallpaper()`、`mountFallbackFullscreen()`、`syncClientSize()` 全部改用虚拟桌面。启动时 `logMonitors()` 打一行汇总 + `EnumDisplayMonitors` 逐屏明细（分辨率、坐标、是否主屏）
- **0x052C 生效性可观测**：`collectWallpaperParents()` 在发 `0x052C` 前后各数一次顶层 `WorkerW`，日志打 `顶层 WorkerW N -> M` 并判定「已分裂出壁纸层 / 未生效，走 Progman 直挂」，不再靠猜
- **Win11 24H2 兜底**：`mountWallpaper()` 所有候选父窗口都挂不上时统一转 `mountFallbackFullscreen()` —— 去掉 `WS_CAPTION/WS_THICKFRAME/WS_CHILD`，加 `WS_POPUP | WS_EX_NOACTIVATE`，`SetWindowPos` 铺满虚拟桌面。代价是桌面图标被画面盖住（保证壁纸可见优先），点击穿透仍由 `WM_NCHITTEST` 负责，鼠标不会被困住。`unmountWallpaper()` 负责把这些样式清回去（`WS_POPUP`、`WS_EX_NOACTIVATE`）
- **Explorer 崩溃恢复（三级保险）**：
  1. 子窗口被连带销毁时**实测收不到 `WM_DESTROY`**，所以主循环每 0.5s 轮询 `IsWindow(g.hwnd)`；失效则置 `g.remount`，走「`teardownWindow()` + `buildWindow()` 整窗重建」（GL 上下文随窗口一起失效，没法原地救）；`WM_DESTROY` 分支保留为双保险
  2. 兜底期间每 0.5s 轮询 `desktopShellReady()`（Progman + `SHELLDLL_DefView` 是否回来），false→true 跳变时回挂（只在跳变时触发，避免反复重建）
  3. `TaskbarCreated` 广播仍注册并处理，但**实测不可靠**（兜底期间未送达），只当第三重保险
- **`remountWallpaper()` 在兜底态必须整窗重建**：从「全屏顶层窗口」直接 `SetParent` 进 WorkerW 后 DWM 会停止合成该窗口（画面停在旧壁纸、帧率 ~30），只有重建窗口走正常路径才恢复
- **`teardownWindow()` 容错**：`wglMakeCurrent` 前先判 `g.hglrc && g.hdc && g.hwnd && IsWindow(g.hwnd)`（窗口已死时 DC/上下文失效），但 `shutdownImGui()` / 各类 `reset()` 无条件执行
- **垂直同步改用 `wglSwapIntervalEXT(1)`，删掉每帧 `DwmFlush()`**：实测 Explorer 死后 `DwmFlush()` 固定阻塞 31.1ms（两个刷新周期），壁纸帧率从 60 掉到 ~30
- 帧率日志保留 `swap %.1fms`（SwapBuffers 耗时，即等垂直同步的时间），临时计时字段 `flushAcc` 已删除

**实测验收（2026-10-25，本机 Win11 2560x1440 单屏 / RTX 4070）**：

- 启动：`[显示器] 1 个，虚拟桌面 2560x1440 @ (0,0)` + 逐屏明细；副屏路径仅代码与日志层面验证（无第二块屏）
- F5 切壁纸：`[桌面] 0x052C: 顶层 WorkerW 19 -> 20（已分裂出壁纸层）` → `桌面结构: Modern，候选父窗口 22 个` → `已挂载到 ...（2560x1440）`，60.0 fps。⚠ 同一天重启过的 Explorer 上再试则稳定 `19 -> 19（未生效，走 Progman 直挂）`，印证 24H2 上该消息不稳定
- `taskkill /f /im explorer.exe`：0.5s 内 `[壁纸] 窗口已失效（未收到 WM_DESTROY），走重建` → `整窗重建中...` → `未找到 Progman 窗口` → `转全屏兜底` → `兜底：全屏顶层窗口 2560x1440 @ (0,0)`，画面保留、稳定 60fps（`swap 0.0ms`）
- `Start-Process explorer.exe`：`[壁纸] 桌面 shell 已恢复，准备回挂` → `当前是全屏兜底，整窗重建后重新挂载` → `收到 TaskbarCreated` → `已挂载到 WorkerW（2560x1440）` → 60.0 fps；截图确认**桌面图标回到画面之上**、Helmet 模型在壁纸层、任务栏正常
- 编译：`Build SUCCESS: matev3.exe [debug]`，无新增告警（仅 threepp 头文件既有的 C4244/C4250/C4305）

### v8 — 交互 / 字体 / 低功耗（P8，已完成）

**目标**：把 §九 长期清单剩下的三条一次性落地 —— 壁纸区域局部交互、FreeType 字体后端、暂停/全屏降帧。

**1. 壁纸区域交互（F8 切换「接收鼠标 / 穿透」）**

- **不能用窗口消息实现**：壁纸窗口是 `SHELLDLL_DefView` 的下层兄弟，点击先被图标层吃掉，`WM_LBUTTONDOWN` 之类**永远不会派发到我们身上**。因此改用 **`WH_MOUSE_LL` 低级鼠标钩子**（`SetWindowsHookExW`，仅在壁纸模式 + 交互态安装），按「光标是否落在模型屏幕投影内」决定拦不拦；`WM_NCHITTEST` 保持恒定 `HTTRANSPARENT`，桌面图标操作完全不受影响
- **模型投影半径不解投影矩阵**：相机看向原点、模型也居中在原点，`halfH = dist * tan(fov/2)` → `pxPerWorld = height/2 / halfH` → `radius = modelRadius * pxPerWorld * 1.25`（1.25 为包围球到顶点的余量），见 `modelScreenRadius()`
- **交互行为**：左键拖拽 → 偏航/俯仰（0.012 rad/px，俯仰夹在 ±1.2 rad，叠加在自动旋转之上，接入 `modelPivot->rotation.x/y`）；右键 → 视角复位（`dragYaw/dragPitch/cameraZoom` 归零）；滚轮 → 缩放（0.94 / 1.06 步进，夹在 0.3~3.0）。面板同步加了「重置视角」按钮
- **`WM_MOUSEMOVE` 绝不能吞**：`return 1` 会让光标原地不动（实测拖拽位移恒为 0），只有 `LBUTTONDOWN / LBUTTONUP / RBUTTONDOWN / RBUTTONUP / MOUSEWHEEL` 才吞
- **`WM_RBUTTONUP` 必须一起吞**：Explorer 的桌面右键菜单是在「抬起」时弹的，只吞按下等于没吞（实测菜单照弹），因此记录 `rmbSwallowed` 标记成对吞掉

**2. FreeType 字体后端**

- `3rd/include/imgui/imgui_freetype.{h,cpp}` 取自 imgui v1.91.5（GitHub raw 不通，走 `cdn.jsdelivr.net/gh/ocornut/imgui@v1.91.5/misc/freetype/...`）；`mate.bat` 的 `:uses_threepp` 段追加该源文件、`/I .../ffmpeg/FF/include/freetype2` 与 `freetype.lib`（`dumpbin /directives` 确认为 `/DEFAULTLIB:LIBCMT`，与 `/MT` 一致）
- 接入方式不用 `IMGUI_ENABLE_FREETYPE`（要改 imconfig），而是运行时指定 builder：`io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType()` + `FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting`
- **`OversampleH/V = 1` + `PixelSnapH = true`**：FreeType 自带栅格化，再超采样只会糊；`LightHinting` 只做纵向网格对齐，观感最接近微软自身渲染器

**3. 低功耗降帧**

- `effectiveFpsCap()` 统一出口：暂停 → 1fps，前台全屏应用/演示模式 → 5fps，其余用面板设定值；主循环限帧对低功耗档不再做 15fps 下限钳制
- 全屏检测走 `SHQueryUserNotificationState`（shell32，动态 `GetProcAddress`，Win7+ 才有），每 2s 查一次，`3 = QUNS_RUNNING_D3D_FULL_SCREEN` / `4 = QUNS_PRESENTATION_MODE` 视为「壁纸看不见」；状态跳变时打 `[低功耗]` 日志
- 面板显示「帧率 上限 N / 实际 M」并标出低功耗原因
- **限帧等待期间必须泵消息**：钩子回调跑在本线程消息循环里，暂停那 1s 的 `Sleep` 会把交互与热键一起卡死。等待循环改为「一次睡到只剩 4ms，再用 1ms / 0 收尾」，每轮 `pumpMessages()`
- **不能切成多个 4ms 小睡**：`Sleep(4)` 实际 5~8ms，累加会把 60fps 压到 42~51fps（实测）；改回单次长睡后恢复 59.8fps

**实测验收（2026-10-25，本机 Win11 2560x1440 单屏 / RTX 4070）**：

- 字体：`[ImGui] 字体: 内嵌资源（8754 KB）+ FreeType`，面板中文正常、无新增告警
- 交互（壁纸模式 + 暂停定格，截图对比）：拖拽前 → 拖拽后模型姿态明显改变（`[交互] 拖拽结束（偏航 3.60 rad / 俯仰 1.20 rad）`）→ 滚轮后模型放大 → 右键 `[交互] 视角已复位` **姿态与尺寸完全回到拖拽前，且桌面不再弹出右键菜单**；F8 关闭后 `[交互] 穿透（鼠标事件交还桌面）`
- 暂停：F7 → `[渲染] 暂停（上限 1 fps）` → `[帧率] 1.0 fps`；暂停期间拖拽仍响应（`偏航 1.44 rad`），证明泵消息生效；F7 恢复 → `59.8 fps`
- 低功耗 5fps 分支：本机无 D3D 独占全屏应用，`PresentationSettings.exe /start` 也没能把 `SHQueryUserNotificationState` 推到 4（始终 5 = `QUNS_ACCEPTS_NOTIFICATIONS`，与代码判据一致）；用**临时**把判据放宽到含 5 的方式验证管道：`[低功耗] 前台有全屏应用/演示模式，上限 5 fps` → `[帧率] 5.0 fps`，随后已改回正式判据重新编译
- 自动化测试踩坑：`SetCursorPos` 不产生低级鼠标钩子事件（拖拽位移恒 0），必须用 `mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE, nx, ny)` 注入；上一实例未退出会占住热键（`[热键] F5 注册失败: 1409`），测试前要先杀进程

### v9 — 音视频（P9，已完成）

**目标**：在前八版全部能力之上接 FFmpeg + miniaudio，让桌面壁纸能放视频、能放音乐，并把视频画面同时喂给 Shadertoy 特效当 `sceneTex`。

**1. 视频壁纸（面板「视频层」三档）**

| 档位 | 行为 |
|---|---|
| 关闭 | 隐藏 `videoMesh`，`sceneTex` 回退 1x1 占位纹理 |
| 底层直出 | 全屏四边形铺满窗口，3D 模型与特效叠在其上 |
| 作为特效输入 | 不直接显示，帧作为 `sceneTex` 交给特效（细雨 / 动态矩形）采样 |

- 视频层是 `PlaneGeometry(2,2)` + `ShaderMaterial`，顶点着色器直接写 NDC：`gl_Position = vec4(position.xy, 0.9999, 1.0)`（不接矩阵，`position` / `uv` 由 threepp 的 `GLProgram` 自动注入；片元里的 `gl_FragColor` / `texture2D` 也会被自动换成 330 core 的写法）；`uv` 纯透传 `vUv = uv`
- `renderOrder = -2000`、`depthTest/depthWrite = false`、`frustumCulled = false` —— 比特效层（`-1000`）更早画，永远压在最底
- **直出用 cover 而不是拉伸**：`uUvScale` 按窗口宽高比与本视频宽高比算缩放（`sa > va → sx = va/sa`，否则 `sy = sa/va`），落黑边也不变形
- **喂特效时必须同步 `texSize`**：细雨用 `texSize.x / texSize.y` 与屏幕宽高比算 `inBounds`（决定哪些像素算「画内」），留成初始的 1x1 会把可采样区域压成中间一条、四周全黑；`updateVideoUniforms()` 每帧写入真实视频宽高
- 档位切换集中在 `applyVideoLayer()`：同一条函数里决定 mesh 可见性与 `sceneTex` 绑的是视频纹理还是占位纹理，并打日志 `[视频] 视频层 -> ...（hasVideo=… sceneTex=… videoTex=…）` 便于核对绑定

**2. 视频帧 → threepp 纹理**

threepp 没有 `VideoTexture`，自建链路：

```
AVDecoder::decodeNextFrame() → RGBA → memcpy 进 Texture::image().data<unsigned char>() → tex->needsUpdate()
```

- `ImageData` 是 `std::variant<vector<unsigned char> / vector<float> / vector<uint16_t>>`，`data<T>()` 即 `std::get<vector<T>>`，直接原地改写，不重建 `Image`
- 上传条件是 `!glInit || version != texture.version()`，所以每帧 `needsUpdate()` 都会走一次**全量 `glTexImage2D`**（threepp 没有 `texSubImage2D` 快路径），4K 素材代价明显（见 §八）
- `generateMipmaps = false`（每帧都变，mipmap 纯白烧），`Linear/Linear`
- **行序在这里一次翻好**：FFmpeg 自上而下，GL 纹理第 0 行在 v=0（下），`uploadVideoFrame()` 逐行倒序拷贝 → 直出与 `sceneTex` 两个消费方都按 GL 常规约定采样，不必各自翻转
- 分辨率变化（换片 / 同片切轨）由 `ensureVideoTexture()` 重建纹理并**同步改两个采样点**（`videoMaterial["videoTex"]` 与 `fxMaterial["sceneTex"]`）

**3. 音频输出（miniaudio）**

- `ma_device_type_playback` + `ma_format_f32` + 2ch + 48000 Hz，回调里从 `xav.h` 的 `AudioQueue::read()` 取，不足补零；同一份数据再混成单声道写进 `AudioVisRingBuffer`（16K 样本）供 FFT 取窗
- 音量走 `ma_device_set_master_volume`，不碰解码侧
- 倍速在取数据阶段做重采样（`speedCursor` + `speedTail` 残留清空），切速度时清尾巴避免旧音频污染
- `ma_device_init` 失败只打日志降级为静音播放，不影响视频

**4. 频谱**

自写迭代式基 2 FFT（`fftRadix2`）+ Hann 窗，取 `fftWindowSize` 个单声道样本 → 功率谱 → 按 `barCount`（64）分组取均值 → `log10` 压成 0~1 并带衰减（`decayRate`），用 `ImGui::GetForegroundDrawList()` 在屏幕底部画柱状图，不参与 threepp 场景。

**5. 媒体库能力**

- **播放列表**：`scanPlaylist()` 优先扫 `e:/MX/Data/media`，没有就扫 `e:/MX/Data`；扩展名白名单 `.mp4/.mkv/.webm/.avi/.mov/.ogv/.mp3/.flac/.wav/.m4a/.ogg`
- **播放模式**（4 档）：单曲循环 / 列表循环 / 顺序播放 / 随机播放，EOF 由 `handleMediaEof()` 分派，`playNext(±1, wrap)` 统一处理上一首 / 下一首；随机档下 EOF 走 `playNext(0, true)`（最多重试 8 次 `rand() % size` 直到不等于当前曲），**上一首 / 下一首按钮仍是顺序 ±1**
- **字幕**：`loadSubtitles()` 三级回退 —— 同名 `.srt` → 同名 `.lrc` → 文件内嵌歌词（`AVDecoder::getEmbeddedLyrics()`，查 `LYRICS / lyrics / UNSYNCEDLYRICS / lyrics-eng / ©lyr`）；`.lrc` 缺结束时间时用下一句起点补（末句 +8s）；字幕画在 ImGui 前景层，黑底白字居中
- **封面**：`buildCoverTexture()` 取 `xav.h` 抽出的 attached_pic（或音频流的图片帧）建 threepp 纹理，面板用 `ImGui::Image((ImTextureID)coverTex, …)` 显示
- **断点续播**：`matev4_resume.txt` 存「UTF-8 路径 + 秒数」，关窗时写、打开媒体时读；启动时**先试开上次那个文件**（接着播），失败才退回播放列表第一项
- **面板**：进度条（拖动期间不被时钟覆盖，`IsItemDeactivatedAfterEdit` 才 seek）、播放/暂停/停止、上一首/下一首、打开文件（`GetOpenFileNameW` + `WideCharToMultiByte(CP_UTF8)`）、频谱勾选、音量、倍速 6 档、播放模式 4 档、视频层 3 档、断点续播开关、封面、播放列表
- **F9** 播放/暂停与 **F7** 渲染暂停是两件事：F7 只停画面（上限 1fps），F9 只停媒体

**踩坑（都已修）**

1. **`prefillAudioQueue()` 会丢视频包** —— 顺序必须是「先 `decodeOneFrame()`，再 `prefillAudioQueue()`」，反了会出现「音频在走、画面不动」
2. **占位纹理的 alpha 必须是 0** —— 细雨的收尾是 `col = mix(col, lrc.rgb, lrc.a)`，`lyricsTex` 若给不透明黑（`{0,0,0,255}`）会把整屏盖成全黑；`fxPlaceholderTex` 改 `{0,0,0,0}`
3. **UTF-8 路径不能先过 ANSI API 判存在性** —— 项目内部路径是 UTF-8，`GetFileAttributesA` 按 GBK 解释，含中文的路径（如 `雏田-穿搭轮换.mp4`）会被误判「不存在」而跳过续播；改成直接 `openMedia()`，失败再退回列表
4. **Unicode 字符集下的字形缺口** —— `GetGlyphRangesChineseSimplifiedCommon()` 只有 2500 常用字，「媒」「频」「循」「谱」等不在表内，会渲染成问号；`initImGui()` 里追加 `builder.AddText(...)` 把这些字补进图集

**实测验收（2026-10-25，本机 Win11 2560x1440 单屏 / RTX 4070）**：

- 视频直出：`[视频] 纹理 1920x1080` → `底层直出` 全屏铺满，3D 头盔叠加其上、频谱 64 柱实时跳动，稳定 59.7~59.9 fps（1280x720 窗口）；与 `ffmpeg -ss 9.06 -frames:v 1` 抽出的参考帧比对，画面正立无翻转
- 视频喂特效：`作为特效输入` + 特效「细雨」→ 画面透过雨滴折射呈现，日志 `sceneTex` 与 `videoTex` 指针一致
- 本地音乐：`mate_music.flac` 打开成功，`60.0 s / 音频 / 48000 Hz`，封面显示、内嵌歌词解析成 5 句字幕（`[字幕] 载入 5 条（内嵌歌词）`），频谱正常；1.25x 倍速下进度从 27.98 s 推进到 47.61 s
- 播放列表 / 上一首 / 下一首 / 视频层三档菜单：UI 点击全部生效
- F9 `[媒体] 暂停（76.67 s）`；F7 `[渲染] 暂停（上限 1 fps）` → `[帧率] 1.0 fps`；F5 进壁纸 `[壁纸] 已挂载到 …（2560x1440）`，44~52 fps（4K 素材）
- 断点续播：关窗写入 `matev4_resume.txt`，重启 `[媒体] 断点续播 9.7 s` → `[媒体] 播放（9.72 s）`
- 4K 素材（3840x2160）：正确识别 `335.0 s / 3840x2160 @ 59 fps`，但窗口内 fps 掉到 42~43（见 §八）

**未验**：外部 `.srt` / `.lrc` 文件（只测了内嵌歌词）、顺序/随机播放的实际切曲、壁纸模式下的面板与 Explorer 重启后的续播、倍速的听感（只看了进度推进）。

---

### v10 — 音视频打磨（P10，已完成）

**目标**：把 v9 遗留的四类问题收拾掉 —— 4K 每帧全量上传、播放器缺时间轴预览、播放列表不能调整、竖屏视频被横向拉宽。代码落在新文件 `matev5.cpp`（v4 原样保留可回退），`mate.bat` 增加 `v5` 分支。

**1. 视频帧改走 `glTexSubImage2D`**

threepp 只有 `glTexImage2D` 全量上传的快路径（`GLTextures::uploadTexture` / `GLState::texImage2D` 都是直通），所以自己接管上传：

```cpp
// 首帧还拿不到 GL id：纹理要等 threepp 第一次采样时才 initTexture
if (!g.videoGlTex) {
    auto id = g.renderer->getGlTextureId(*g.videoTex);
    if (!id) { g.videoTex->needsUpdate(); return; }   // 这一帧仍走 threepp
    g.videoGlTex = *id;
}
bindGlTexture(g.videoGlTex);
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
```

- `ensureVideoTexture()` 重建纹理时把 `videoGlTex` 清零，重新取 id
- **绑定必须走 `renderer->state().activeTexture() / bindTexture()`**，不能裸 `glBindTexture`：`GLState::bindTexture` 在「记录的当前绑定 == 目标」时直接 early-out 不调 GL，缓存一旦与实际不一致，后面 threepp 采样自己的纹理会跳过重绑、采到错图（封面纹理与缩略图建纹理统一改走同一个 `bindGlTexture()`）

**2. 时间轴缩略图（真解码抽帧）**

- 常量：12 张、每张塞进 160x90 框内
- 后台线程用**独立 `AVDecoder`**（不碰播放中的那个），逐点 `seekToTarget(dummy, dur * i / 12)` → `getSeekedFramePtr()` → `downscaleRgba()` 盒式均值降采样 → 上锁写进 `g.thumbs[i]`
- **工作线程只产出 CPU 像素**：GL 上下文不在线程里，`glGenTextures` / `glTexImage2D` 全部由主线程在 `pumpThumbnails()` 里做（渲染循环每帧调用，建完纹理就 `px.clear()` 释放内存）
- 切片 / 关媒体先 `stopThumbnails()`（置 `thumbCancel` + `thumbGen++` + `join()` + 删纹理），避免线程握着已释放的解码器；`thumbGen` 保证旧的抽帧结果不会被新一轮采纳
- 进度条 hover 时按 **`|it.t - t|` 找最近的已完成帧**（抽帧是并行完成的，不能按下标推算），tooltip 里同时显示时间点与缩略图

**3. 播放列表拖拽重排 + 按目录建队列**

- 重排：`BeginDragDropSource` 里 `SetDragDropPayload("MATE_PLAYLIST", &i, sizeof(int))`，目标行 `AcceptDragDropPayload` 后 `if (from < i) --to`（先摘掉原位、后面下标整体前移），挪完再按 `g.mediaPath` 找回 `playIndex`
- 按目录建队列：面板勾选 `按目录建队列` 后，`openMedia()` 里只在「文件所在目录 ≠ `playlistDir`」时才重扫，切曲不会反复扫盘
- 列表项要 `PushID(i)`：`Selectable` 的标签可能重名，不隔离 ID 会导致拖拽/点击串到别的行

**4. 修 cover 裁剪轴（v9 遗留 bug）**

原实现 `if (sa > va) sx = va/sa; else sy = sa/va;` 把两条轴写反了：窗口比视频宽时按宽度铺满、高度溢出，应当裁**上下**（`sy = va/sa`）。v9 验收用的是 16:9 视频 + 16:9 窗口（`sa == va`），这条路根本没被触发，竖屏素材（720x1280）一放就横向拉宽。现在 `sa > va → sy = va/sa`，窗口更窄时 `sx = sa/va`。

**实测验收（2026-10-25，Win11 2560x1440 / RTX 4070）**

| 项 | 结果 |
|---|---|
| 局部上传 | `[视频] 局部上传已启用（GL 纹理 3，3840x2160）`；720x1280 与 3840x2160 均走通 |
| 缩略图 | `[缩略图] 抽帧结束 12/12`；进度条 hover 显示 `27.3 s` 对应小图 |
| 拖拽重排 | 拖动第 2 行到第 4 行 → `[媒体] 播放列表重排：1 -> 2`，列表顺序随之变化 |
| 按目录建队列 | 打开 `%TEMP%\mate4k\test4k.mp4` → `[媒体] 按目录建队列 C:/Users/…/mate4k（1 项）` |
| 竖屏 cover | 720x1280 素材在 1280x720 窗口铺满宽度、裁掉上下，人脸比例正常（与 `ffmpeg -ss` 抽帧比对） |
| 4K 直出 | v5 稳定 59.7~59.8 fps（v4 同样 59.6~59.8）→ **上传不是本机瓶颈**，见 §八.8 |
| 壁纸模式 4K | 2560x1440 挂载后 59.8 fps，F9 生效（`[媒体] 暂停（317.02 s）`）；壁纸模式下面板按设计不画，频谱/字幕仍画 |
| 外部字幕 | `.srt` → `[字幕] 载入 4 条（SRT）`，15.23 s 显示第 3 条；`.lrc` → `[字幕] 载入 3 条（LRC）` |
| EOF | 20 s 素材播完 → `[媒体] 播放结束（列表循环）` → 自动切下一首 |
| Explorer 重启续播 | 壁纸模式下杀 `explorer.exe` → `[壁纸] 窗口已失效（未收到 WM_DESTROY），走重建` → Explorer 起来后 `[壁纸] 收到 TaskbarCreated` → `[壁纸] 已重新挂载`；`test.mp4` 进度 199.60 s → 240.05 s，43 s 墙钟只丢 ~2.5 s（重建 + 重挂），没有从 0 重来。两轮复现一致 |
| 倍速 | 1.5x：10.4 s 墙钟推进 15.61 s 媒体时间（折算 1.50x）；1.25x：10.4 s 推进 13.04 s（折算 1.25x）；画面照常出帧、无卡顿 |
| 随机播放 | 切到「随机播放」后逐首跑 EOF，`[媒体] 播放结束（随机播放）` → 自动开下一首，**每次都不是当前这首**（8 次重试兜底未被用尽）。三次冷启动各自连跑 2~3 次，序列各不相同（`ogv→tvb1→music→mp4` / `ogv→mp4→tvb1→music` / `mp4→ogv`），播种生效 |

**随机模式下的「上一首 / 下一首」仍按顺序 ±1**（只有 EOF 自动切曲走随机），这是刻意保留的行为：手动翻页要的是"上一首/下一首"，不是"随便来一首"。

**新发现**：

- v5 打开 4K 素材时启动帧率掉到 9.3 fps（v4 同一文件是 33.9）—— 差值是缩略图线程在 4K 上做了 12 次 seek+解码。属于可接受的启动开销，若要优化可以按分辨率降张数或延后启动。
- 本机 Explorer 退出时窗口是**直接失效**、收不到 `WM_DESTROY`，靠 P7 的窗口存活检查兜底才走到重建（`WM_DESTROY` 那条路没被触发）。
- 壁纸模式重建出来的窗口标题是 `Mate Wallpaper`；F5 切回调试模式不重建窗口，标题也就停在 `Mate Wallpaper`。已修：标题提成 `kTitleDebug` / `kTitleWallpaper` 两个常量，F5 切回调试模式且窗口未重建时 `SetWindowTextW` 改回；实测 F5 往返后标题恢复成 `Mate v5 - …`。
- `rand()` 原先从未播种（CRT 默认种子 1），所以两次冷启动从同一首出发会选到同一首（实测连续两次都是 `test.ogv → test.mp4`）。已修：`wWinMain()` 里 `srand((unsigned)time(nullptr) ^ (unsigned)GetCurrentProcessId())`，时间和 PID 异或，保证同一秒内多次启动也不同；修后三次冷启动序列互不相同。注意全局 `rand()` 与第三方解码库共享状态，不能假设「调了几次 rand」可控。

### v5 后续打磨（P11，已完成）

**目标**：去掉控制台，调试信息进窗口内；补窗口图标。

- **入口改成 `wWinMain` + `/subsystem:windows`**：不再弹控制台窗口。`mate.bat` 按版本切 `%SUBSYSTEM%`（只有 v5 是 windows，v1~v4 仍是 console，`main` 不动）；`SetConsoleOutputCP` 那两行随之删除
- **日志改走 ImGui**：`mateLog()` 不再 `printf`，改成格式化进 512 行的内存环形队列（`g_logLines` + `g_logMutex`，跨线程安全）。布局是「左参数面板、右调试日志」，两块都拉到窗口高度（客户区高 − 32）：参数面板 420 宽贴左，日志面板 480 宽贴右（窗口太窄时退到参数面板右侧 8px），逐行 `TextUnformatted`，默认跟随最新一行、用户往上翻时不打断
- **默认窗口 1920x1080**：`defaultWindowSize()` 统一算窗口尺寸（客户区 1920x1080 再加标题栏和边框），屏幕工作区装不下就退回工作区大小；调试窗口创建、壁纸模式预建窗口、F5 摘除壁纸恢复尺寸三处共用
- **窗口图标**：`mate.rc` 增加 `IDI_MATE_APP(103)` / `IDI_MATE_SMALL(104)`，直接引用 MX 现成的 `e:/MX/Data/app.ico` 与 `small.ico`（与 `e:/MX/Data/app.rc` 同源）；`registerWindowClass()` 填 `wc.hIcon` / `wc.hIconSm`
- 壁纸模式下面板本来就不画，日志面板跟着一起不画（壁纸是最终形态，不需要日志）

**实测**：启动无控制台窗口；窗口客户区正好 1920x1080（2560x1440 屏上居中，1936x1119 含边框）；左侧参数面板 + 右侧调试日志都拉满高度，日志里有 `[媒体] 打开 …`、`[视频] 局部上传已启用`、`[帧率] 59.8 fps` 等，中文正常；标题栏图标显示为 `app.ico` 的蓝色 "M"。

### v11 — matex0：OpenGL 4.5 + Win32 Canvas（P12，已完成）

**目标**：以 `matev3.cpp` 为蓝本另起 `matex0.cpp`，把渲染基线从 GL 3.3 抬到 4.5，用 Win32 消息循环复刻 threepp 的 `Canvas`（彻底摆脱 GLFW），同时保留壁纸挂载 / 热键 / 低级钩子，并补上 OrbitControls、资源外置、窗口内日志与模型动画。

**1. OpenGL 4.5 与 glad 升级**

- **只换 glad 不够，库也要跟着重打**：`3rd/src/threepp/external/glad` 换成 glad 0.1.36 生成的 **GL 4.5 compatibility** 版本（旧 4.1 版备份为同目录 `glad_4_1_bak`）；`threepp_mt.lib` 不需要全量重编，只重编 `glad.obj` 后重新打包（326.5 MB → **342.4 MB**），其余 obj 原样复用
- **上下文回退链**：4.5 core → 4.5 compatibility → 3.3 core → legacy `wglCreateContext`；`wglCreateContextAttribsARB` 的属性用 `0x2091/0x2092` 传主次版本、`0x2094` 传 profile mask
- 实测申请 4.5 core 直接拿到 `4.5.0 NVIDIA 596.36`、`GLSL 4.50 NVIDIA`（RTX 4070），没有走回退
- **ImGui 后端版本串必须跟着改**：`ImGui_ImplOpenGL3_Init("#version 450")`，否则 4.5 core 上下文会让后端默认的 130/150 版着色器编译失败

**2. MateCanvas：Win32 复刻 Canvas**

- `class MateCanvas : public threepp::PeripheralsEventSource`，**刻意不继承 `threepp::Canvas`**：后者的实现落在 `Canvas.cpp`，会顺带把 GLFW 拖进链接；渲染仍走 `GLRenderer(size)`
- 对齐官方示例用到的接口：`size()/aspect()/setSize(WindowSize)/onWindowResize/notifyResize/animate/animateOnce/isOpen/close/windowPtr/vsync/swapBuffers/pumpEvents`
- 消息分发：`MateWndProc` 先给 ImGui，再 `canvas->handleMessage()`；`WM_LBUTTONDOWN/UP`、`WM_MOUSEMOVE`、`WM_MOUSEWHEEL` 分别送 `feedMouseDown/Up/Move/Wheel`
- 约定对齐 GLFW：鼠标按钮 0=左 / 1=右 / 2=中；滚轮 120 刻度归一成 ±1 且**上滚 = +1**（与 GLFW yoffset 同向）；`WM_MOUSEWHEEL` 的 lParam 是屏幕坐标，`WM_MOUSEMOVE` 是客户区坐标，两者不能混
- `threepp::Key` 与 GLFW 键值同序，VK → Key 用算术映射即可，不用逐键表
- `IOCapture` 三个回调直接接 `ImGui::GetIO().WantCaptureMouse / WantCaptureKeyboard`：在面板上拖滑块不会带动相机，鼠标移到面板上滚轮也不会缩放模型
- ESC 退出由 `exitOnKeyEscape()` 决定；`WM_DESTROY` 用 `destroying()` 区分「自己关」与「Explorer 连带销毁」，后者要整窗重建并重挂

**3. 资源外置（去内嵌）**

- 删掉 `IDR_FONT(101)` / `IDR_MODEL(102)` 与整段 `loadEmbeddedResource()`；字体走 `e:/MX/Data/siyuan.ttf`（失败再退 `./siyuan.ttf`、`msyh.ttc`、ImGui 默认字体），模型目录 `e:/MX/Data/models`
- **窗口 ICO 必须保留**：`matex.rc` 只留 `IDI_MATE_APP(103)/IDI_MATE_SMALL(104)`，`registerWindowClass()` 填 `wc.hIcon/wc.hIconSm`
- 少了 12.5 MB 的字体/模型 RC，`matex0.exe` 只有 **4.22 MB**（v5 单 exe 15.45 MB）；入口保持 `wWinMain` + `/subsystem:windows`，日志只进窗口面板

**4. 构建脚本 `matex0.bat`**

- 用法 `matex0.bat [debug|wallpaper] [run]`；VS2022 / VS2026 各列三条候选路径，命中后打印 `VS path: ...`
- 单 TU（2176 行）；`/MT /std:c++20 /utf-8 /IGNORE:4006`，链接 `threepp_mt.lib freetype.lib zlib.lib ws2_32.lib user32 gdi32 kernel32 opengl32 winmm dwmapi`
- 路径一律相对 `../../3rd/...`，与 `mate.bat` 的约定保持一致

**5. 相机控制：OrbitControls**

- 相机 `(0,0,cameraBaseDistance)`、`target=(0,0,0)`；左键旋转 / 右键平移 / 滚轮 dolly；`autoRotate` 与面板「旋转速度」联动（`rotateSpeed*10`）
- F9 复位：`target` 归零、距离回 `cameraBaseDistance`，日志 `[视角] 已复位（距离 %.3f）`；面板「距离倍率」= `controls->getDistance() / cameraBaseDistance`
- `minDistance/maxDistance` 按模型包围球设成 `0.3×/3×`，换任何 glb 都不会钻进去或飞出视野

**6. 模型扫描与动画**

- `scanModels()` 改**递归**扫描：`.glb` + `.gltf`，显示名用相对路径（子目录里一堆 `scene.gltf` 不再同名）；本机 12 个顶层 glb + 10 个子目录场景 = **22 个**
- 动画取自 `GLTFLoader` 的 `LoadResult::animations`：`AnimationMixer` 挂在模型根节点，加载后打印 `[动画] 共 N 段：名称 / 名称 …`，并默认自动播放第一段
- 面板新增「动画」下拉 + 暂停/播放 + `片段 N 段（X 秒）`；换片段或切播放状态只置 `clipDirty`，下一帧帧首执行 `stopAllAction()` → `clipAction()->reset().play()`，绝不在 ImGui 绘制过程中改场景图
- **顺序约束**：换模型 / 销毁窗口时先 `mixer.reset()` 再卸模型，mixer 持有节点引用，反过来会 UAF

**7. 日志面板**

- **停靠右边缘**：每帧按客户区算 `x = 宽 - 320 - 12`，`ImGuiCond_Always` + `NoMove|NoResize`，窗口缩放时自动贴住右边；高度 `min(460, 高 × 0.6)`
- **宽度 320 + 自动换行**：`PushTextWrapPos(0.0f)`（0 = 换到窗口右缘），去掉横向滚动条；仍保持「在底部时跟随最新行、用户上翻时不打断」
- **默认折叠**：`SetNextWindowCollapsed(true, FirstUseEver)`，启动只露标题条，点标题栏展开；F6 收起参数面板时日志面板保留

**8. 壁纸交互：进层即开钩子**

- 壁纸窗口在 `SHELLDLL_DefView` 之下，**收不到任何鼠标消息**，交互只能走 `WH_MOUSE_LL`（§六.6）。此前钩子默认关闭、要按 F8 才装，实际使用起来就是「壁纸状态下鼠标没反应」
- 现在 `mountWallpaper()` 成功后直接 `setInteractive(true)` 并打 `[交互] 壁纸鼠标交互已开启（F8 可切穿透）`；退出壁纸自动 `setInteractive(false)`，F8 仍可手动切「接收 / 穿透」
- 拦截规则不变：光标落在模型投影圆内才吞（左键旋转 / 右键平移 / 滚轮缩放），圆外全部透传；`WM_MOUSEMOVE` 不吞、`WM_RBUTTONUP` 与按下成对吞，避免桌面框选与右键菜单

**实测验收（2026-10-28，Win11 2560x1440 单屏 / RTX 4070）**

| 项 | 结果 |
|---|---|
| GL | `[GL] 申请档位: 4.5 core，实际版本: 4.5`；`4.5.0 NVIDIA 596.36`；`GLSL 4.50 NVIDIA` |
| 资源 | 字体 `e:/MX/Data/siyuan.ttf + FreeType`；模型 22 个；`matex0.exe` 4.22 MB；标题栏图标正常 |
| 动画 | `SambaDancing.glb` 1 段 18.20 s 自动播放；切到 `InterpolationTest.glb` 显示 9 段、当前片段 1.71 s，方块插值运动 |
| 交互 | 滚轮 3 格距离倍率 1.00 → 0.86（0.95³）；右键拖拽平移后位置保留；F9 回到 7.384 |
| 壁纸 | 挂 WorkerW 后客户区 2560x1440，模型渲染在图标层之下；F5 往返 `GetParent` 由 WorkerW 回到 0 |
| 钩子 | 进壁纸不按 F8，滚轮 3 格 → 切回调试模式倍率 1.00 → 1.17；`SendInput` 右键拖拽驱动 OrbitControls 平移 |
| 其他 | F6 收面板 / F7 暂停降到 1.0 fps / ESC 干净退出；单实例下 F5~F9 无 1409 |

**踩坑**

- 两个实例同时运行会互抢 F5~F9（`[热键] F5 注册失败: 1409`），表现为「面板看起来不响应」，测试前必须先杀旧进程
- 自动化点击必须 DPI 感知（`SetThreadDpiAwarenessContext(-4)`），否则 `ClientToScreen` 与实际物理坐标差一截，点不到 ImGui 控件
- 低级钩子的注入测试只能用 `SendInput` / `mouse_event`（`SetCursorPos` 不产生钩子事件），与 v8 结论一致

---

### v12 — matex2：threepp 示例学习沙箱（P13，已完成）

**目标**：把 `ThreeppVSTest` 的 `01_intro/11_texture2d.cpp` 搬进 Mate 的依赖体系，做成单 TU 的 threepp 模块试验田。以后换示例只改场景构建，窗口 / GL / 输入外壳不再重写。

**1. 外壳**

- 直接复用 matex0 的结论：`Win32Canvas : threepp::PeripheralsEventSource`，**不继承 / 不链接 `threepp::Canvas`**（否则把 GLFW 拖进来）；渲染仍走 `GLRenderer(width, height)`，只给 `Canvas::initWindow(GraphicsAPI)` 补空实现
- WGL 上下文回退链与 matex0 一致：4.5 core → 4.5 compatibility → 3.3 core → legacy `wglCreateContext`；`wglSwapIntervalEXT` 开垂直同步
- 与 matex0 的刻意差异：**没有 ImGui / 壁纸挂载 / 热键 / 低功耗**，用 `/subsystem:console` + `printf` 日志，纯粹为了快速观察 threepp API 行为
- 事件对齐 GLFW：鼠标按钮 0=左 / 1=右 / 2=中；滚轮 120 刻度归一成 ±1（上滚 = +1）；`threepp::Key` 与 GLFW 同序；`WM_SIZE` 同步 `camera.aspect`、`renderer->setSize()`、`glViewport()`

**2. 示例内容（11_texture2d）**

- `SphereGeometry + checker.png`、`BoxGeometry + crate.gif`、`PlaneGeometry + brick_bump.jpg`（双面），球 / 箱自转
- `Reflector`：2048×2048 反射纹理、`clipBias=0.003`、`color=0x777777`，水平铺在 `y=-2.5`
- `OrbitControls`：先摆相机 `(0,0,5)` 再构造（构造函数里会立即 `update()`）；左键旋转 / 右键平移 / 滚轮缩放 / ESC 退出
- 本版 threepp 的 `MeshBasicMaterial::Params` 没有 `.side()`：平面材质先 `create(Params{}.map(tex))`，再直接改 `material->side = Side::Double`

**3. 资源与构建**

- 贴图从 `E:\MX\Projects\ThreeppTest\build\data\textures` 复制到 `Data/textures/`，路径按 **exe 目录**拼（`GetModuleFileNameW` → `Data/textures/*`），不依赖 `DATA_FOLDER` 宏或当前工作目录
- `matex2.bat debug [run]`：VS2022 / VS2026 各三条候选路径，命中后打印 `VS path: ...`；`/MT /std:c++20 /utf-8 /IGNORE:4006`，链接 `threepp_mt.lib + ws2_32 + opengl32/winmm/user32/gdi32`，不需要 ImGui / FreeType / zlib
- 产物 `matex2.exe` 约 **2.2 MB**（matex0 4.22 MB，mate 产品另计）

**实测（2026-11-07，Win11 2560x1440 / RTX 4070）**

| 项 | 结果 |
|---|---|
| GL | `[GL] 4.5 core -> 4.5 | NVIDIA GeForce RTX 4070 | 4.5.0 NVIDIA 596.36` |
| 贴图 | `checker.png / crate.gif / brick_bump.jpg` 三张全部加载，无异常 |
| 场景 | 球 / 箱自转；砖墙 + 地面镜面可见倒影，两侧反射 aliceblue 背景 |
| 交互 | 左键旋转、右键平移、滚轮缩放正常；ESC / 关窗干净退出 |
| 构建 | `Build SUCCESS: matex2.exe`，无 LNK4006/4088 |

**踩坑**

- `glad` 是进程级一次性加载，必须在**最终上下文** current 之后调用 `threepp::loadGlad()`；在临时上下文里加载会让后续 `GLRenderer` 拿不到函数指针
- `WM_MOUSEWHEEL` 的 `lParam` 是屏幕坐标，`WM_MOUSEMOVE` 是客户区坐标，两者不要混用；滚轮事件只取 `wParam` 的 delta
- 窗口最小化时 `WM_SIZE` 会给出 0×0，必须跳过，否则 `renderer->setSize` 与 `glViewport(0,0,0,0)` 会把下一帧画没
- 源码按 `/utf-8` 编译时，console 默认 GBK(936) 代码页会把中文日志显示成乱码；`main()` 开头要先 `SetConsoleOutputCP(CP_UTF8)` + `SetConsoleCP(CP_UTF8)`

---

### v13 — 视频帧源独立成类 / 解码时直接缩放 / 接入场景层（P14，已完成）

**目标**：把视频播放从 `matex2.cpp` 里的临时代码收敛成一个可复用的类，按收益先做「解码时直接缩放」，再把它接到 `mate.exe` 的 StillWater 场景层。

**1. 分层：一个引擎无关的类 + 两个薄适配**

| 层 | 位置 | 职责 |
|---|---|---|
| 帧源（不含图形 API） | [mate_video.h](file:///e:/MX/Projects/Mate/src/core/mate_video.h) | 解码、独立线程、时钟/循环、行序翻转、`Stats` |
| 产品适配 | [stillwater.cpp](file:///e:/MX/Projects/Mate/src/scenes/stillwater/stillwater.cpp) | `mate::Texture::uploadSub` 传 720p 帧，画在 `order 50` 的矩形里 |
| threepp 适配 | [matex2.cpp](file:///e:/MX/Projects/Mate/matex2.cpp) | 把帧拷进 `threepp::Texture` 的 `Image` 缓冲 + `needsUpdate()` |

- `open(path, mode, targetWidth)` / `advance(dt)`（返回是否有新帧）/ `data()|width()|height()`；像素是 RGBA，**第 0 行 = 画面底部**。
- 行序翻转放在**解码线程**里，`advance()` 只 `std::move`，渲染线程零拷贝；`Sync` 模式保留用于单线程对照。
- 原 threepp 专用 `VideoSource.h` 已删除，新头文件不再引用 `threepp/...`。

**2. 解码时直接缩放**

`MO/xav.h` 的 `AVDecoder` 新增 `setOutputWidth(w)`（0 = 原尺寸，且不放大），全链路改成输出尺寸：

| 改动点 | 内容 |
|---|---|
| sws 上下文 ×3（`open` / 格式变化 / `seekToTarget`） | 目标宽高改为 `getWidth()/getHeight()` |
| 输出缓冲 | `av_image_get_buffer_size` / `av_image_fill_arrays` 用输出尺寸 |
| 帧拷贝 | `DecodedFrame::rgba`、`seekedFrame`、逐行拷贝全按输出尺寸 |
| 读取接口 | `getWidth()/getHeight()` 返回**帧尺寸**；未设输出时与原行为一致，matev4/v5 无感知 |

**实测**（`zutian.mp4` 3840×2160、165Hz vsync、独立线程）：

| 输出尺寸 | 解码耗时 | 最慢帧 | 单帧上传量 |
|---|---|---|---|
| 3840×2160（原尺寸） | 10.6–10.8 ms | 10.6–11.6 ms | 33 MB |
| 1280×720（默认 1280 宽） | 7.0–7.3 ms | 6.2–7.3 ms | 3.7 MB |

两档都能跑满 165 fps，但缩放后最慢帧几乎减半，上传量降到约 1/9。`targetWidth=1280` 时用默认的 `yuv=true`，`false` 可回退打包 RGBA。

**补充（同日）：YUV420P + PBO**

- 解码直接出 Y/U/V 三平面（`setOutputYuv420p(true)`，`getFramePlane(i)` / `planeWidth(i)` / `planeHeight(i)` 暴露给消费方），颜色换算放到片元着色器（BT.709 限制范围）
- 720p 单帧 1.38MB（RGBA 3.69MB）；`matex2` 实测解码从 7.0–7.3 ms 降到 **3.3–3.8 ms**（只做 YUV→YUV 缩放，不再做 YUV→RGB）
- StillWater 侧上传走 **双 PBO 轮转**：`glBufferStorage` 建不可变缓冲（`WRITE|PERSISTENT|COHERENT`），起点 `glMapBufferRange` 一次映射后长期复用，每帧把三平面 `memcpy` 进映射指针，再 `glTexSubImage2D` 带偏移取数；每块 PBO 配一个 `GLsync` fence，复用前 `glClientWaitSync` 等 GPU 读完，避免 CPU 写缓冲与 GPU 读缓冲撞在一起。`glMapBufferRange` 失败时自动回退 `glBufferSubData`
- YUV420 的色度平面是 2×2 下采样，输出宽高会强制取偶；R8 纹理上传时 `UNPACK_ALIGNMENT` 临时改 1

**3. 接入 mate 场景层**

- `scene.json`：新增 `assets.video`、`layers` 里的 `video`（`type=native` / `order=50` / `rect=[0.06,0.08,0.26,0.30]` / `blend=alpha`），参数 `video`、`videoWidth`（默认 1280）、`videoAlpha`
- `stillwater.cpp`：`load()` 按 `videoWidth` 打开并建 3 个 `GL_R8` 纹理（首帧用 `uploadSub`）；同时建两块双 PBO（`glBufferStorage` + 持久映射）；`update()` 里 `advance` 有新帧才上传；`render()` 画一个保持视频比例的矩形；面板给开关与解码耗时
- 层序必须是 50：水面（0）之后，不会被水着色器的折射/高光覆盖；落叶（100）之前，保持前后关系
- 测试素材 `Data/MScene/Stillwater/data/video.mp4`（由 `E:\MX\Data\tvb1.mp4` 复制，5.4 MB）

**踩坑 / 注意点**

- `getWidth()/getHeight()` 的语义已从「视频原始尺寸」改为「帧尺寸」：只要没调用 `setOutputWidth` 就完全等价；新调用方必须按这两个值算缓冲区大小。
- 目标宽 ≥ 原宽时不放大，直接回落原尺寸——否则 1080p 素材设 1280 宽会被无意义放大并多占内存。
- `xav.h` 里的 `stbi_*` 只有声明，实现由 `threepp_mt.lib` 提供；`mate` 产品版同样靠它解析符号，任何新 TU 都不能再定义 `STB_IMAGE_IMPLEMENTATION`。

---

## 五、四层渲染架构

```
┌─────────────────────────────────────┐
│  UI 层      ImGui 参数面板/频谱/字幕 │  最上层（可隐藏）
├─────────────────────────────────────┤
│  3D 层      threepp 场景图 / glTF    │
├─────────────────────────────────────┤
│  特效层     Shadertoy 全屏片元       │  renderOrder = -1000
├─────────────────────────────────────┤
│  视频层     全屏四边形 + 视频纹理    │  renderOrder = -2000（最底）
└─────────────────────────────────────┘
```

绘制顺序由下至上，均在同一 GL 上下文内完成，最终 `SwapBuffers` 一次输出。视频层与特效层都关深度写入；视频层仅在「底层直出」档可见，切「作为特效输入」时改为向特效层提供 `sceneTex`（此时画面由特效层输出，视频层自身不画）。

---

## 六、关键技术要点

### 1. WorkerW 挂载与桌面结构识别

核心难点不是挂载，而是 **Win11 的桌面结构与老版本不同**。可直接复用 MX 中已验证的实现思路：

- [oapp.h](file:///e:/MX/MO/oapp.h#L131-L171)：`detectDesktopStructure()` 判定 `Legacy` / `Modern`（Progman 是否直接持有 `SHELLDLL_DefView`）
- [oapp.h](file:///e:/MX/MO/oapp.h#L174-L210)：`findDesktopLayers()` 分别定位壁纸层与图标层 WorkerW

Mate 侧需额外补充：`WM_SETTINGCHANGE` 后重新探测、以及 24H2 的降级策略。

### 2. WGL 上下文

`GetDC(hwnd)` → `ChoosePixelFormat`(24 位深度 + 8 位模板) → `SetPixelFormat` → 临时 `wglCreateContext` 拿到 `wglCreateContextAttribsARB` → 销毁临时上下文 → 用 3.3 Core Profile 建正式上下文（失败回退兼容上下文）→ `wglMakeCurrent`。v3 起上下文 current 之后再 `threepp::loadGlad()`。

### 3. threepp 去 GLFW（已落地，结论见 §四 v3）

上游 `GLRenderer` 提供了 size-only 构造 `GLRenderer(std::pair<int,int> size)`，专给「宿主已建好并 current 的 GL 上下文」用：不建窗口、不碰 GLFW、渲染目标是 FBO，末尾自己 blit 回默认 framebuffer。因此**不需要实现 `Canvas` 契约**，实际做法是：

1. 只编译 GL-only 源列表（331 个 cpp），排除 `Canvas.cpp` / `GlfwWindow*` / `EglContext`；
2. Mate 自己 `wglCreateContextAttribsARB` 建 3.3 Core 上下文并 `wglMakeCurrent`，再 `threepp::loadGlad()`（threepp 的 glad 全进程 one-shot，必须在最终上下文 current 之后调用）；
3. 只给 `Canvas::initWindow` 提供一个空实现骗过链接器（`GLRenderer(Canvas&)` 那个构造函数在 obj 里引用它，但永不调用）；
4. threepp 需要的 glm / stb / glad / KHR 头全部用 `3rd/include` 版本，不引入 submodule；glad 只用 threepp 自带的那份（GL 4.1 compatibility），MX 的 3.3 core glad 在 v3 中弃用。

`setSize()` + `camera->aspect` + `glViewport` 负责 resize，`SwapBuffers` 由 Mate 控制，垂直同步用 `wglSwapIntervalEXT(1)` 让 `SwapBuffers` 自己节流（v7 起不再每帧 `DwmFlush`，原因见 §四 v7）。

### 4. 中文字体

P6 起优先用 exe 资源段里的内嵌字体（`IDR_FONT`，8.7 MB），脱离 `Data` 目录也能显示中文；P8 起栅格化交给 FreeType：

```cpp
io.Fonts->FontBuilderIO    = ImGuiFreeType::GetBuilderForFreeType();
io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
// AddFontFromMemoryTTF 的 ImFontConfig：OversampleH/V = 1、PixelSnapH = true、FontDataOwnedByAtlas = false
```

stb_truetype 对小字号中文边缘发糊，`LightHinting` 只做纵向网格对齐，最接近系统渲染观感（细节与踩坑见 §四 v8）。

### 5. 调试 / 发布双模式

`BUILD_WALLPAPER` 宏切换：调试 → 普通顶层窗口，面板常显；发布 → 挂 WorkerW，面板默认隐藏，快捷键唤出。

### 6. 壁纸层的鼠标交互只能走低级钩子

壁纸窗口挂在 `SHELLDLL_DefView` 的下层，窗口消息全被图标层吃掉，`WM_LBUTTONDOWN` 等**永远收不到**。因此：

- 交互用 `WH_MOUSE_LL` 全局钩子，按「光标是否落在模型屏幕投影圆内」决定是否 `return 1`；
- `WM_NCHITTEST` 恒定返回 `HTTRANSPARENT`，桌面图标的点击/框选不受影响；
- 钩子回调跑在本线程消息循环里，**任何 `Sleep` 都会连带卡住钩子**，限帧等待必须边睡边 `PeekMessage`；
- 吞事件时要注意成对性：`MOUSEMOVE` 一律不吞（否则光标不动），右键的 `DOWN`/`UP` 要一起吞（Explorer 在 `UP` 时弹菜单）。

### 7. 低功耗降帧

`SHQueryUserNotificationState`（动态取，Win7+）每 2s 查一次，前台是 D3D 独占全屏（3）或演示模式（4）时，壁纸根本看不见，降到 5fps；暂停时降到 1fps。所有档位收敛到 `effectiveFpsCap()` 一个出口，面板同步显示「上限 / 实际」。注意低功耗档低于主循环原本的 15fps 下限钳制，需要显式放行。

### 8. 视频帧 → threepp Texture（没有 VideoTexture）

> P14 起解码与线程统一收敛到 `src/core/mate_video.h` 的 `VideoSource`（见 §四 v13、§11.4）；本节只保留 threepp 消费侧的做法。

threepp 只认静态 `Image`，视频只能靠「原地改写像素缓冲 + 每帧标脏」：

```cpp
auto& buf = videoTex->image().data<unsigned char>();   // std::get<vector<unsigned char>>
std::memcpy(buf.data(), video.data(), buf.size());     // 帧源已翻成 GL 行序（第 0 行=画面底部）
videoTex->needsUpdate();                               // version++，触发下次采样时重新上传
```

- 帧源给的是**已经翻好行序**的 RGBA（翻转在解码线程里做），消费侧不要再 `1.0 - uv.y`
- 纹理在 `open()` 之后按 `video.width()/height()` 建一次；`targetWidth` 只影响输出尺寸，不改变宽高比
- `needsUpdate()` 走的是 threepp 的慢路径：`GLTextures::setTexture2D` 判到 `version` 变化就 `uploadTexture`（`glTexImage2D` 全量重传），没有 `texSubImage2D`。matev5 起改用自己拿 GL id + `glTexSubImage2D`（见 §六.11）；产品场景 `stillwater` 则用 3 个 `mate::Texture`（GL_R8）配双 PBO 上传
- P14 起帧源默认输出 YUV420P 三平面，`matex2` 用 3 个 `Format::Red` 纹理 + `ShaderMaterial` 做 BT.709 → RGB；RGBA 路径仍保留（`open(..., yuv=false)`）
- 行序**只在一个地方翻**（解码线程拷贝时倒序），这样直出与当 `sceneTex` 两条消费路径都不必再翻

### 9. 复用 `xav.h` 的边界

`MO/xav.h` 是 header-only，直接 `#include` 进 `matev4.cpp`（`#define MINIAUDIO_IMPLEMENTATION` 也放同一 TU），不重写解码逻辑。使用约定：

- **音频归 miniaudio，同步归 xav.h**：`xav.h` 的时钟以音频为主，只要 `AudioQueue` 一直在被消费（miniaudio 回调里 `read()`），视频帧就按它算出的位置出图
- **顺序不能反**：`prefillAudioQueue()` 会把当前读到的视频包丢掉，正确顺序是「`decodeOneFrame()` → `prefillAudioQueue()`」，否则音频在走、画面不动
- **`ImageLoader` / `stb_image` 的实现只由 `threepp_mt.lib` 提供**，`matev4.cpp` 只用到 `xav.h` 的声明，不能在本 TU 再定义一次 `STB_IMAGE_IMPLEMENTATION`
- Seek 走 `setMediaPosition()`：`decoder.seek(t)` + 清 `audioQueue` + 重置时钟基准，随后**同样按「先解一帧视频、再灌音频」的顺序**重启；进度条拖动松手才调用，避免拖动过程中反复 seek

### 10. FFmpeg / miniaudio 链接

- 库：`libavformat.a` / `libavcodec.a` / `libavutil.a` / `libswscale.a` / `libswresample.a` + `libx264/x265/vpxmt/jxl/ass/fribidi/harfbuzz/OpenEXR…` 一大串编解码静态库，`/LIBPATH:"e:/MX/3rd/ffmpeg/FF/lib"`
- 系统库：`secur32 crypt32 bcrypt ole32 strmiids mfplat mfuuid dxva2 d3d11 dxgi`（MediaFoundation / DXVA 相关，静态 lib 会引到）
- **符号重定义躲不掉**：FFmpeg 静态 SDK 与 threepp/FreeType 里都有同名的 CRC/编解码符号，链接必须加 `/FORCE:MULTIPLE /IGNORE:4006,4088`（与已验证的 MPlayer 构建一致）
- miniaudio 不需要额外库（`winmm` 已有），WASAPI 后端由系统提供

### 11. 局部上传与 `GLState` 绑定缓存

绕过 threepp 自己传纹理时，有两条硬约束：

1. **先拿到 GL id**：threepp 的 `Texture` 是惰性的，GL 对象要等第一次被采样时 `initTexture` 才创建。`GLRenderer::getGlTextureId(Texture&)` 在第一帧返回 `nullopt`，此时只能回落 `needsUpdate()` 让 threepp 建；拿到 id 后缓存到 `videoGlTex`，纹理重建时清零重取。
2. **绑定必须经过 `GLState`**：`GLState::bindTexture` 在「记录的当前绑定 == 目标」时直接 early-out，`activeTexture` 也按 `currentTextureSlot` 缓存。裸 `glBindTexture` 会让缓存与实际脱节 —— 之后 threepp 采样自己的纹理时以为已经绑好、跳过重绑，采到的是别的图。所有自建纹理（视频 / 封面 / 缩略图）统一走 `bindGlTexture()` 这一个出口。

顺带：GL 纹理创建必须回到主线程。工作线程（如抽帧）只能产出 CPU 像素，`glGenTextures` / `glTexImage2D` 由主线程在渲染循环里做。

### 12. 缩略图线程模型

```
startThumbnails(路径, 时长, 是否视频)
  └─ std::thread：自己的 AVDecoder（不碰播放中的那个）
       每一格： seekToTarget(dummy, dur * i / 12) → getSeekedFramePtr()
               → downscaleRgba() 盒式均值降到 160x90 框内
               → 上锁写 g.thumbs[i]（只写 CPU 像素，ready = true）
主线程 pumpThumbnails()：ready 的项建/更新 GL 纹理，随即 px.clear()
stopThumbnails()：thumbCancel = true + thumbGen++ + join() + glDeleteTextures
```

- 每个 `seekToTarget` 前 `dummy.clear()`：`AudioQueue` 每轮都会被灌满，不清就写不进去
- `thumbGen` 是代号：切片时旧线程的结果即使晚到也不会被新一轮采纳
- `closeMedia()` 必须先 `stopThumbnails()` 再动解码器状态，否则线程手里握着已失效的对象
- 抽帧是并行的，完成顺序与下标无关 → hover 命中按时间最近的 `|it.t - t|` 找，不按下标推

### 13. matex0：Win32 Canvas + GL 4.5 的边界

- **不继承 `threepp::Canvas`**：`Canvas.cpp` 里带着 GLFW 实现，一旦被链接就会要求 GLFW 库与回调；只继承 `PeripheralsEventSource` 即可拿到键盘/鼠标/滚轮分发，再自己实现 `size()/animate()/isOpen()` 这几个纯虚与工具方法
- **`GLRenderer(size)` 不变**：渲染器只认尺寸，上下文由 `MateCanvas::initGL()` 用 `wglCreateContextAttribsARB` 建，`SwapBuffers` 也由 Canvas 负责；4.5 与 3.3 走同一份渲染器代码
- **回退链要看 `GL_VERSION` 而不是 `wglCreateContextAttribsARB` 返回值**：驱动可能给降级上下文仍返回非空句柄，必须用 `glGetString(GL_VERSION)` 判定实际档位并打日志
- **Key 枚举与 GLFW 同序**：VK 值直接加减偏移即可映射，鼠标按钮顺序 0/1/2 = 左/右/中，滚轮向上为正；这三条约定错一个，OrbitControls 的手感就会反
- **ImGui 与 3D 输入互斥靠 `IOCapture`**：把 `WantCaptureMouse/WantCaptureKeyboard` 接进去，面板拖拽和相机拖拽才不会互相抢

### 14. 资源外置后的启动依赖

- matex0 **没有内嵌兜底**：字体缺失退到 ImGui 默认字体（中文会方块）、`models` 目录为空则列表为空，都只在日志里提示，不阻止启动
- 因此部署时至少要带 `e:/MX/Data/siyuan.ttf`（或同目录 `siyuan.ttf`）与 `e:/MX/Data/models/*`；窗口图标仍编译进 exe（`matex.rc`），不依赖外部 ico
- 体积对照：内嵌字体 + 默认模型的 v5 单 exe 15.45 MB，matex0 只有 4.22 MB

---

## 七、环境核查结论（2026-10-25 实测）

| 项 | 结果 |
|---|---|
| Visual Studio | ✅ `C:\Program Files\Microsoft Visual Studio\2022` |
| CMake | ✅ 3.31.2 |
| Git | ✅ 2.47.0 |
| Python | ✅ 3.12.5 |
| GitHub 直连 | ❌ 超时 |
| gh-proxy.com 等镜像 | ❌ 全部不可用（threepp 无法下载） |
| PyPI | ✅ 可用（threepp 改从 `threepp-2026.10.11.tar.gz` sdist 取） |
| baidu / gitee | ✅ 正常 |

---

## 八、已知风险

1. ~~**threepp + GLFW 解耦**~~：已解决，走 size-only 构造 + FBO 渲染，宿主自带上下文与 `SwapBuffers`（见 §四 v3）。`threepp_mt.lib` 虽有 326.5 MB，链接器只抽取用到的 obj，P6 打出的单 exe 为 15.45 MB（其中 RC 资源占 12.5 MB），暂不需要裁剪。
2. **Win11 24H2**：`0x052C` 行为不稳定（本机实测同一台机器上时而分裂出顶层 `WorkerW`、时而 `19 -> 19` 完全无效），必须有降级方案。已落地：候选父窗口全挂不上时转全屏顶层窗口兜底（图标会被盖住），并且把「挂上了但其实是兜底」也当作可恢复状态（见 §四 v7）。
3. **/MT 全静态**：所有三方库（threepp、FreeType、glad）必须以 /MT 重新编译，混用 /MD 会链接失败。threepp 侧已用 `/MT` 编好。
4. **壁纸模式无输入焦点**：ImGui 面板靠全局热键 F5/F6/F7 唤出；壁纸层的鼠标交互走 `WH_MOUSE_LL` 钩子（F8 开关，见 §四 v8）。低级钩子「吞事件」是被动式的，若将来要做真实控件（按钮/滑块）还得回到子窗口或自绘命中测试。
5. **GPU 占用**：壁纸常驻后台，必须限帧（30~60fps）、支持暂停（1fps），并对前台全屏应用自动降帧（5fps，见 §四 v8）。
6. **glad 单实例约束**：threepp 的 glad 全进程 first-load-wins，后续任何新模块都不能再链接第二份 glad，否则符号冲突或指针指向错误上下文。
7. **FFmpeg 静态库符号冲突**：`/FORCE:MULTIPLE /IGNORE:4006,4088` 是必需的，属于「能用但脏」的方案 —— 重复符号被静默丢弃，若将来某个重名符号行为不同（尤其 CRC / 字符集相关），要留意运行期才暴露的问题。想根治只能自己重编一套裁剪过的 FFmpeg。
8. **4K 视频掉帧（P10 已改判）**：v9 时代每帧一次 `glTexImage2D` 全量重传，1280x720 窗口下 4K 素材只跑到 42~43 fps，据此把「上传」列为主要嫌疑。v10 改成 `glTexSubImage2D` 后做同素材 A/B：v5 与 v4 都稳定在 59.6~59.8 fps，**说明这台机器上 PCIe 上传根本不是瓶颈**（3840x2160x4B x 30fps ≈ 1 GB/s，对 4070 是零头）。局部上传仍然保留（少一次显存重分配、少一次全量拷贝），但真正压 4K 的是 CPU 解码 + `swscale` 转 RGBA；若将来确实要跑 4K 高码率素材，方向是硬件解码（D3D11VA）或「按窗口尺寸在解码侧直接缩到 ≤1440p」，而不是继续动上传。另：v10 的缩略图线程会在打开 4K 时做 12 次 seek+解码，出现约 2~4 s 的低帧起步（9.3 fps，v4 同文件 33.9 fps），属可接受开销。
9. **单 TU 编译开销**：`xav.h` + `miniaudio.h` 两个实现都塞在 `matev5.cpp`，`cl` 单次全量编译较慢；不拆文件是为了保持「按版本单文件」的约定，暂时接受。
10. **matex0 的 GL 4.5 基线**：4.5 core 依赖驱动支持，老机器会回退到 4.5 compat / 3.3 core / legacy（回退后计算着色器类特性不可用）；ImGui OpenGL3 后端必须把着色器头改成 `#version 450`，否则 4.5 core 下链接期直接崩。两条约束都已在 §四 v11 落地，后续新增 GL 代码时不能退回 3.3 语义。

---

## 九、下步优化

**近期（P1 / P2）**
- [x] 建 `matev1.cpp` + `mate.bat`，跑通 WorkerW 挂载与点击穿透（`WM_NCHITTEST` → `HTTRANSPARENT`；`WS_EX_TRANSPARENT` 实测会破坏呈现，不可用）
- [x] 把 ImGui 源码复制到 `3rd/include/imgui`，纳入编译脚本（`mate.bat v2` 时自动追加源文件）
- [ ] 抽出一个 `DesktopWallpaper` 小段逻辑，逐版本复用（暂不建公共头，避免过早抽象）

**中期（P3 / P4 / P5）**
- [x] threepp 剥离 GLFW，产出 `threepp_mt.lib`（/MT，331 obj，326.5 MB）
- [x] `GLRenderer(size)` 对接，验证调试/壁纸双模式与 F5 切换（不需要 `Win32Canvas`）
- [x] 模型目录扫描 + 面板下拉切换（12 个 glb，相机按包围盒自动摆位）
- [x] Shadertoy 全屏层（fxcode.h 4 个预设，`renderOrder` + 关深度写入压在场景最底）
- [x] 三层合成顺序（特效 → 3D → ImGui）+ 帧率锁 30/60/120、暂停恢复、GPU 计时
- [x] 相机视野/距离、三盏灯强度、特效强度面板化
- [ ] `mate.bat v3 wallpaper`（编译期 `BUILD_WALLPAPER`）编译与实测
- [ ] 给 `sceneTex` / `lyricsTex` 接真实画面捕获（细雨、动态矩形才完整）
- [ ] 混合模式与色彩空间统一（sRGB 线性化、特效层与 3D 层的 alpha 语义）

**长期（P6 / P7 / P8）**
- [x] 资源 RC 内嵌 / `.tpz` 打包，单 exe 自包含（走 RC 资源段：字体 + 一个默认模型，见 §四 v6）
- [x] 多显示器（铺满整个虚拟桌面；每屏独立窗口留待需要时再做）
- [x] Explorer 崩溃恢复：全屏顶层窗口兜底 + 0.5s 句柄体检 + shell 恢复轮询回挂（见 §四 v7）
- [x] 壁纸区域内的局部交互：`WH_MOUSE_LL` 钩子 + F8 切换接收/穿透，光标落在模型投影内才拦截（拖拽旋转 / 右键复位 / 滚轮缩放，见 §四 v8）
- [x] FreeType 字体后端（`imgui_freetype`）提升中文清晰度（`LightHinting`，见 §四 v8）
- [x] 性能：低功耗模式（暂停 1fps + 前台全屏应用/演示模式 5fps，见 §四 v8）
- [ ] 性能：脏矩形（只重绘变化区域）—— `SwapBuffers` 全窗口呈现的模式下暂无收益，等按屏分窗或多窗口方案再评估

**音视频（P9 / P10）**
- [x] 复用 `MO/xav.h` 接 FFmpeg 解码，视频帧 → threepp `Texture`（每帧 `needsUpdate`，行序在拷贝时统一翻）
- [x] 视频壁纸双档：面板「视频层」可切 关闭 / 底层直出 / 作为特效输入（`renderOrder = -2000`）
- [x] miniaudio 音频输出 + 64 柱频谱（自写基 2 FFT）
- [x] 播放列表、4 种播放模式、倍速 6 档、字幕（`.srt` / `.lrc` / 内嵌歌词）、封面、断点续播
- [x] F9 媒体播放/暂停，与 F7 渲染暂停解耦
- [x] 补测：外部 `.srt` / `.lrc`（P10 实测通过）、壁纸模式 F9 与面板（F9 生效；面板在壁纸模式按设计不画，频谱/字幕照画）、播放模式切曲（列表循环与随机播放均验，见下）
- [x] 4K 性能：`glTexSubImage2D` 局部上传已落地（`matev5.cpp`），A/B 实测本机上传不是瓶颈，结论见 §八.8
- [x] 音画同步精细度：字幕/进度统一跟 `mediaClock()`（有音轨时即音频时钟），外部 `.srt` 在 15.23 s 命中第 3 条、4K 素材进度与墙钟一致
- [x] 播放器 UI 进阶：时间轴缩略图预览（12 张，hover 显示）、播放列表拖拽重排、按目录建播放队列
- [x] Explorer 重启后的续播：杀 `explorer.exe` → 窗口失效 → 重建 → `TaskbarCreated` 重挂 → 从原进度接着播（43 s 墙钟丢 ~2.5 s），两轮复现一致
- [x] 倍速听感：推进比例已验（1.5x / 1.25x 折算准确、无卡顿），人工试听无爆音、磁带式变调可接受
- [x] 随机播放的实际切曲：EOF 后每次都换到别的曲目（`ogv→tvb1`、`tvb1→music`、`music→mp4` …），三次冷启动序列互不相同（补了 `srand(time ^ pid)` 播种）；随机档下上一首/下一首仍按顺序 ±1，属刻意保留

**matex 分支（P12，2026-10-28）**
- [x] glad 升到 0.1.36（GL 4.5 compatibility，旧版留 `glad_4_1_bak`），只重编 `glad.obj` 后重打 `threepp_mt.lib`（342.4 MB）
- [x] `matex0.cpp`：自写 Win32 `MateCanvas`（`PeripheralsEventSource` 派生，不进 GLFW），上下文按 4.5 core → 4.5 compat → 3.3 core → legacy 回退
- [x] OrbitControls 接入：左键旋转 / 右键平移 / 滚轮 dolly，F9 复位、F7 渲染暂停、F6 面板
- [x] 资源外置：字体与模型全走磁盘，`matex.rc` 只留窗口图标（103/104），exe 降到 4.22 MB
- [x] 模型递归扫描 `.glb` / `.gltf`（本机 22 个，显示名用相对路径），ImGui 下拉切换 + 动画片段选择/播放（`AnimationMixer`）
- [x] 日志面板停靠主窗口右侧（320 px、`PushTextWrapPos(0)` 自动换行、首次启动默认折叠）
- [x] 壁纸模式进层自动挂 `WH_MOUSE_LL` 钩子（模型投影内才拦截），F8 仍可切回完全穿透
- [ ] matex 后续：音视频层（`xav.h` + miniaudio）迁移、ImGui 示例页、按屏多窗口
- [ ] matex 后续：OpenGL 4.5 计算着色器方向的后处理（高斯/泛光）替代现有多 pass 全屏层

**视频帧源（P13 / P14，2026-11-07）**
- [x] 立方体两面贴视频：`BoxGeometry` 的 6 个 group 用材质数组，只给 `+z/-z` 两面 `map`
- [x] 视频模块独立成类 `src/core/mate_video.h`：`Sync` / `Threaded` 双模式 + `Stats`，不再依赖 threepp
- [x] 4K 满 165 fps：解码移到独立线程（队列 2 帧），渲染线程只摘帧
- [x] **解码时直接缩放**：`AVDecoder::setOutputWidth()`，sws 直接输出 1280×720；4K 单帧 33MB → 3.7MB（实测见 §四 v13）
- [x] 接入 mate 场景层：StillWater 的 `video` native 层（`order 50`、`rect` contain、参数 `video/videoWidth/videoAlpha`）
- [x] YUV420P 三平面：解码直接输出 Y/U/V（`AVDecoder::setOutputYuv420p`），GPU 端 BT.709 → RGB；720p 一帧 1.38MB，解码 3.3–3.8 ms（原 RGBA 7.0–7.3 ms）
- [x] PBO 异步上传：StillWater 用 2 块 `GL_PIXEL_UNPACK_BUFFER` 轮转，三个平面 `glBufferSubData` → `glTexSubImage2D`
- [x] PBO 持久映射：改成 `glBufferStorage`（`WRITE|PERSISTENT|COHERENT`）+ 一次 `glMapBufferRange` 常驻映射，三平面直接 `memcpy` 进映射指针，各配一个 `GLsync` fence 防覆盖；映射失败自动回退 `glBufferSubData`
- [x] BT.709 → RGB 系数校正：`R=1.164(Y-16)+1.793(Cr-128)`、`G=1.164(Y-16)-0.213(Cb-128)-0.533(Cr-128)`、`B=1.164(Y-16)+2.112(Cb-128)`（原 B 误用 BT.601 的 2.015）
- [ ] 后续可选：按素材色彩空间（BT.601 / full-range）自动切换系数

---

## 十、场景系统设定（MScene）

> 2026-11-06 设计稿（尚未实现）。目标是把 `mate.exe` 从"一个写死的壁纸"变成"一个场景容器"：引擎只提供能力，场景只提供内容；场景包可以像素材一样被增删、热重载、分发。

### 10.1 设计原则

| # | 原则 | 落地方式 |
|---|---|---|
| 1 | 引擎薄、场景厚 | exe 只保留窗口挂载、GL、资源、脚本、音频等通用能力，具体画面全部在 `MScene/<场景>/` |
| 2 | 结构声明化、行为脚本化、重活原生化 | 层与参数用 JSON 声明；交互与动效交给 Lua；地形/植被这类程序化重活交给 exe 内置的原生模块 |
| 3 | 一切资源走 VFS | 场景文件夹与 `.mscene` 包对场景代码完全等价；`builtin://` 指向 `ma.bin` |
| 4 | 确定性 | 场景自带随机种子、单帧时间步长截断、参数持久化，同一场景每次启动画面一致 |
| 5 | 故障隔离 | 场景解析/编译/脚本失败只降级当前场景，不拖垮引擎；永远有内置兜底场景 |

第 2 条有一个关键取舍：**不允许 Lua 随意创建/删除场景图结构**。结构必须在 `scene.json` 里声明，Lua 只改参数、播动画、触发事件。这样热重载、性能预算、错误定位都可控；确实需要极端自定义时，走原生模块而不是脚本。

### 10.2 运行时目录

```
mate\                          # 部署后的运行目录
├── mate.exe
├── mate.log                   # 引擎写：滚动日志
└── Data\
    ├── ma.bin                 # 引擎必需：字体 / 窗口图标 / 兜底场景 / 默认模型
    ├── mate.json              # 全局配置（引擎级，不含场景内容）
    ├── MScene\                # 场景根目录（可配置，见 mate.json.scenes.root）
    │   ├── _shared\           # 可选：跨场景共享的贴图/模型/音频
    │   │   └── env\...
    │   ├── Stillwater\        # 静水（当前 matex1 场景）
    │   │   ├── scene.json     # 清单（必需）
    │   │   ├── preview.jpg    # 缩略图（可选，场景切换器/面板用）
    │   │   ├── data\          # 模型 / 贴图 / 字体 / 预烘焙数据
    │   │   ├── layer\         # glsl、后处理与合成描述
    │   │   ├── lua\           # 场景脚本
    │   │   └── audio\         # 音频资源（独立目录便于按需加载与 3D 定位）
    │   ├── Watch\             # 劳力士表
    │   ├── ShortTrip\         # 短途之旅
    │   └── ...
    ├── MState\                # 引擎写：每场景的参数与进度（<id>.json）
    └── MCache\                # 引擎写：着色器编译缓存、烘焙缓存、缩略图
```

| 路径 | 归属 | 说明 |
|---|---|---|
| `Data/ma.bin` | 引擎 | 缺失时用 RC 内嵌的最小图标 + 兜底场景，保证还能启动 |
| `Data/mate.json` | 引擎 | 全局配置；缺失则用内置默认值并写日志 |
| `Data/MScene/` | 用户/场景 | 引擎只读（打包场景尤其如此），不往里写状态 |
| `Data/MState/` | 引擎 | 每个场景一份 JSON：参数、上次进度、播放队列 |
| `Data/MCache/` | 引擎 | 可随时删除，删后首次启动变慢但不影响正确性 |

命名约定：场景文件夹名是给人看的（可以中文），`scene.json` 里的 `id` 是小写英文标识（存档、日志、URL 用）。两者不要求一致。资源 URI 三种：场景内相对路径（`data/xxx.glb`）、`builtin://`（ma.bin 内）、`state://`（当前场景的持久化文件）。

### 10.3 ma.bin 格式

自定义单文件包，尽量简单、可校验、可按需读：

```
偏移  大小  内容
0     4     magic "MAB1"
4     2     version = 1
6     2     flags（bit0: 全量 deflate）
8     4     entryCount
12    4     indexOffset
16    16    reserved（留版本位）
...         entry table（变长）
             type   u8   （1字体 / 2图标 / 3模型 / 4贴图 / 5场景 / 6其他）
             nameLen u8
             name   变长 UTF-8（例：fonts/siyuan.ttf）
             offset u32（数据区绝对偏移，16 字节对齐）
             size   u32（压缩后）
             rawSize u32
             crc32  u32
...         数据区
```

- 压缩用 **miniz**（单头文件、zlib 兼容），逐条目可选 deflate；字体/GLB 本身已是压缩格式，默认 `store`。
- 校验：加载时校验 `crc32`，坏条目只跳过该条目并写日志，不整体失败。
- 引擎启动顺序：**先打开 ma.bin → 读窗口图标 → 再建窗口**；这样任务栏/Alt-Tab 图标与 `mate.bin` 一致，不依赖 RC。
- 打包工具：`matepack bin <源目录> ma.bin`，条目按目录结构自动命名。
- `ma.bin` 至少包含：中文字体、窗口图标（多尺寸 ico）、兜底场景（极简：纯色 + 一行提示）、默认 glb 模型各一。

### 10.4 mate.json（引擎级配置）

```json
{
  "schema": 1,
  "app": {
    "language": "zh-CN",
    "singleInstance": true
  },
  "startup": {
    "wallpaper": true,
    "scene": "stillwater",
    "restoreLastScene": true
  },
  "render": {
    "api": "gl4.5",
    "vsync": true,
    "fpsCap": 60,
    "msaa": 4,
    "hdr": true,
    "colorSpace": "srgb",
    "lowPower": { "occludedFps": 5, "pausedFps": 1 }
  },
  "display": {
    "mode": "virtualDesktop",
    "dpiAware": "perMonitorV2"
  },
  "ui": {
    "font": "builtin://fonts/siyuan.ttf",
    "scale": 1.0,
    "theme": "dark",
    "panelOnStart": false
  },
  "audio": {
    "master": 1.0,
    "muted": false,
    "muteWhenOccluded": true
  },
  "scenes": {
    "root": "Data/MScene",
    "builtin": "builtin://scenes/fallback",
    "autoResume": true,
    "crossfadeMs": 300
  },
  "hotkeys": {
    "toggleMode": "F5",
    "panel": "F6",
    "pause": "F7",
    "interactive": "F8",
    "resetScene": "F9",
    "sceneSwitcher": "F10"
  },
  "dev": {
    "hotReload": true,
    "logLevel": "info",
    "showStats": false
  }
}
```

约束：`mate.json` 只放**引擎策略**，不放场景内容（不写模型路径、不写天气参数）。未识别字段一律忽略并保留，便于向前兼容；`schema` 变化才做迁移。

### 10.5 scene.json（场景清单）

```json
{
  "schema": 1,
  "id": "stillwater",
  "name": { "zh-CN": "静水", "en": "Stillwater" },
  "version": "1.0.0",
  "engine": ">=0.2.0",
  "type": "native",
  "native": "stillwater",
  "entry": "lua/main.lua",
  "preview": "preview.jpg",

  "canvas": { "aspect": "cover", "design": [1920, 1080], "background": "#0a0d10" },

  "camera": {
    "type": "perspective", "fov": 34,
    "position": [8.0, 6.0, 21.5], "target": [0.0, 2.0, 0.0],
    "near": 0.1, "far": 200.0
  },

  "assets": { "preload": ["layer/*.frag", "audio/*.ogg"] },

  "layers": [
    { "name": "sky",   "type": "shader", "order": -1000, "source": "layer/sky.frag",
      "output": "sceneColor", "blend": "opaque" },

    { "name": "world", "type": "native", "order": 0, "module": "stillwater", "role": "world",
      "output": "sceneColor",
      "params": { "seed": 90210, "timeOfDay": "$timeOfDay", "weather": "$weather" } },

    { "name": "glass", "type": "native", "order": 100, "module": "stillwater", "role": "glass",
      "output": "sceneColor", "blend": "alpha" },

    { "name": "bloom", "type": "post", "order": 1000, "effect": "bloom",
      "input": "sceneColor", "output": "hdrA",
      "params": { "strength": "$bloom_strength", "radius": "$bloom_radius",
                  "threshold": "$bloom_threshold" } },

    { "name": "grade", "type": "post", "order": 1100, "effect": "grade",
      "input": "hdrA", "output": "final",
      "params": { "exposure": "$exposure", "contrast": "$contrast",
                  "saturation": "$saturation", "vignette": "$vignette",
                  "grain": "$grain" } }
  ],

  "parameters": {
    "timeOfDay": { "type": "float", "default": 0.42, "min": 0, "max": 1, "persist": true,
                   "ui": { "group": "天气", "label": "时间", "widget": "slider" } },
    "autoTime":  { "type": "bool", "default": true,
                   "ui": { "group": "天气", "label": "自动推进" } },
    "timeScale": { "type": "float", "default": 0.01, "min": 0, "max": 0.2,
                   "ui": { "group": "天气", "label": "推进速度" } },
    "weather":   { "type": "enum", "default": "sunny", "values": ["sunny", "rain", "snow"],
                   "ui": { "group": "天气", "label": "天气", "widget": "combo" } },
    "wind":      { "type": "float", "default": 0.25, "min": 0, "max": 1,
                   "ui": { "group": "天气", "label": "风力" } },
    "showVegetation": { "type": "bool", "default": true,
                   "ui": { "group": "内容", "label": "植被" } },
    "bloom_strength": { "type": "float", "default": 0.38, "min": 0, "max": 3,
                   "ui": { "group": "后处理", "label": "泛光强度" } },
    "bloom_radius":    { "type": "float", "default": 0.60, "min": 0, "max": 1,
                   "ui": { "group": "后处理", "label": "泛光半径" } },
    "bloom_threshold": { "type": "float", "default": 1.05, "min": 0, "max": 4,
                   "ui": { "group": "后处理", "label": "泛光阈值" } },
    "exposure":   { "type": "float", "default": 1.06, "min": 0, "max": 4,
                   "ui": { "group": "调色", "label": "曝光" } },
    "contrast":   { "type": "float", "default": 1.06, "min": 0, "max": 3,
                   "ui": { "group": "调色", "label": "对比度" } },
    "saturation": { "type": "float", "default": 1.14, "min": 0, "max": 3,
                   "ui": { "group": "调色", "label": "饱和度" } },
    "vignette":   { "type": "float", "default": 0.28, "min": 0, "max": 1,
                   "ui": { "group": "调色", "label": "暗角" } },
    "grain":      { "type": "float", "default": 0.018, "min": 0, "max": 0.2,
                   "ui": { "group": "调色", "label": "颗粒" } }
  },

  "input": {
    "regions": [
      { "name": "glass", "shape": { "type": "sphere3d", "center": [0, 2, 0], "radius": 3.2 },
        "events": ["enter", "leave", "drag", "wheel", "rclick"] }
    ]
  },

  "audio": {
    "listener": "camera",
    "buses": {
      "music":    { "volume": 0.6 },
      "ambience": { "volume": 0.4 },
      "sfx":      { "volume": 0.8 }
    },
    "sources": [
      { "name": "wind",      "clip": "audio/wind.ogg",  "bus": "ambience", "loop": true },
      { "name": "cabinFire", "clip": "audio/fire.ogg",  "bus": "sfx",
        "spatial": true, "position": [1.2, 1.0, 3.4], "rolloff": 4.0 },
      { "name": "thunder",   "clip": "audio/thunder.ogg", "bus": "sfx", "loop": false }
    ]
  },

  "perf": {
    "cost": "high",
    "targetFps": 60,
    "pauseWhenOccluded": true,
    "quality": {
      "low":  { "bloom": false, "vegetationDensity": 0.4, "msaa": 0 },
      "high": { "bloom": true,  "vegetationDensity": 1.0, "msaa": 4 }
    }
  }
}
```

字段要点：

| 字段 | 作用 |
|---|---|
| `type` | `native`（需要 exe 内置模块）或 `declarative`（纯 JSON + shader + Lua，第三方可做） |
| `engine` | 最低引擎版本，不满足则拒绝加载并提示 |
| `canvas` | 分辨率无关设计基准；`aspect` 取 `cover`/`contain`/`stretch` |
| `assets.preload` | 进入前预载；其余资源首次引用时异步加载 |
| `parameters` | 自动生成面板，同时是 Lua 与层参数的唯一数据源 |
| `perf.quality` | 引擎按档位覆盖层参数，场景不用自己写降级逻辑 |

`"$name"` 是参数绑定语法：值来自 `parameters` 的同名项，参数变化自动下发到 uniform/层参数。复杂逻辑（比如"下雨才随机打雷"）才写 Lua。

实现说明（2026-11-06，`mate.exe` 第一版）：加载器当前只识别 **字符串键值形式**的 `assets`（静水用 `photo` / `koi` / `leaf` / `audio`，按角色直接路径加载）以及 `layers` 里的 `shader` / `native` 两种 `type`；上例中的 `preload`、`camera`、`input.regions`、`audio.sources`、`perf.quality` 仍作为设计保留，尚未接线。

### 10.6 层系统

公共字段：

```json
{
  "name": "唯一名",
  "type": "shader",
  "order": 0,
  "enabled": true,
  "input": ["sceneColor"],
  "output": "sceneColor",
  "format": "rgba16f",
  "scale": 1.0,
  "blend": "opaque",
  "depthWrite": false,
  "clear": false
}
```

| type | 作用 | 关键字段 |
|---|---|---|
| `shader` | 全屏片元 pass（GLSL） | `source`、`uniforms`、`resolutionScale` |
| `model` | glTF/GLB 场景图 | `source`、`transform`、`animation`、`fitCamera`、`pickable` |
| `native` | exe 内置原生模块提供，程序化内容 | `module`、`role`、`params` |
| `image` | 贴图四边形/精灵 | `source`、`anchor`、`size`、`parallax`、`tiling`、`scroll`、`mask` |
| `flipbook` | 预渲染序列（帧动画/循环视频替代） | `frames`、`pattern`、`fps`、`loop` |
| `video` | 视频纹理 | `source`、`loop`、`rate`、`sync`、`asInput` |
| `particle` | 粒子 | `emitter`、`atlas`、`lifetime`、`gravity`、`wind` |
| `text` | 文本（时钟/字幕） | `text`（表达式）、`font`、`size`、`anchor`、`color`、`shadow` |
| `ui` | 场景级控件 | 绑定 `parameters`、`anchor`、`visibleIn`（模式/交互态） |
| `post` | 后处理 | `effect`（bloom/grade/tilt/vignette/grain）、`input`、`output`、质量档 |

渲染图规则：

1. 层只声明 `input`/`output` 的**命名目标**，不手写"先画谁"；引擎按 `order` 稳定排序 + 拓扑约束生成执行顺序，检测到环直接报错。
2. 命名目标带格式（`rgba8` / `rgba16f` / `depth24`）与尺寸倍率（`scale`），由引擎统一分配 FBO、按需 ping-pong。
3. 未指定 `output` 的层默认写 `sceneColor`；`final` 目标自动合成到 swapchain（sRGB 编码在此处一次性完成）。
4. 视频/特效作为输入时，只是把 `output` 改名为一个中间目标，其他层用 `input` 引用即可，不需要特殊的"视频层开关"。
5. 着色器由引擎注入前言（`#version 450 core`、`uniform uTime/uResolution/uPixelScale`、MATE 宏），场景文件只写函数体，避免 GL 版本散落在场景里。
6. 所有纹理/模型引用走 VFS + 内容哈希去重；同一路径只加载一份。

### 10.7 参数系统

- 类型：`float` / `int` / `bool` / `enum` / `color` / `vec2` / `vec3` / `string` / `path`。
- `ui.group` 分组生成折叠面板；`ui.widget` 可选 `slider` / `drag` / `combo` / `checkbox` / `color` / `button`。
- `persist: true` 写 `MState/<id>.json`，切换场景/重启后还原；`persist: false` 每次用默认值。
- 参数是**唯一真相源**：Lua 读写、层参数绑定、面板控件、持久化全部指向同一份数据，不存在三套状态不同步的问题。
- `param` 变更会广播 `on_param(k, v)`，Lua 可据此做联动。

### 10.8 Lua 运行时

- Lua 5.4 静态库（/MT），全引擎一个 VM，每个场景一个独立环境表；只开放 `base/string/table/math/utf8/coroutine`。
- 生命周期回调：

| 回调 | 时机 |
|---|---|
| `on_load(ctx)` | 资源就绪、层已创建、首帧渲染前 |
| `on_enter()` | 场景正式可见（淡入开始） |
| `on_update(dt)` | 每帧一次，`dt` 已截断到 0.1s |
| `on_event(e)` | 命中区域/热键事件 |
| `on_param(k, v)` | 参数变化 |
| `on_pause()` / `on_resume()` | 低功耗或手动暂停 |
| `on_exit()` | 卸载前，用于存档 |

- API 面（保持小而稳定）：

| 表 | 方法 |
|---|---|
| `mate.time` / `mate.dt` / `mate.frame` | 场景累计时间、帧步长、帧号 |
| `mate.params` | `get/set/reset(k, v)` |
| `mate.layer(name)` | `set(k, v)` / `show(bool)` / `opacity(x)` / `play/pause/seek` |
| `mate.scene` | `switch(id)` / `reload()` |
| `mate.audio` | `play(name)` / `stop(name)` / `volume(bus, x)` |
| `mate.input` | 事件由区域自动分发，脚本只读 `e.region/e.type/e.x/e.y/e.dx/e.dy` |
| `mate.storage` | 每场景 JSON 存档：`load()` / `save(t)` |
| `mate.log` / `mate.random` / `mate.monitor` | 日志、确定性随机、显示器信息 |

示例（静水的时间推进与天气联动）：

```lua
local t = 0.42
local spin = 0.0

function on_load(ctx)
  mate.params:set("timeOfDay", t)
end

function on_update(dt)
  if mate.params:get("autoTime") then
    t = (t + dt * mate.params:get("timeScale")) % 1.0
    mate.params:set("timeOfDay", t)
  end
  mate.layer("world"):set("spin", spin)
  if mate.params:get("weather") == "rain" and mate.random:next() < 0.015 then
    mate.audio:play("thunder")
  end
end

function on_event(e)
  if e.region == "glass" and e.type == "drag" then
    spin = spin + e.dx * 0.01
  elseif e.region == "glass" and e.type == "rclick" then
    spin = 0.0
  end
end
```

- 错误处理：所有回调经 `pcall`；同一回调连续失败 3 次即停用该脚本，保留场景与原生层，面板弹错误卡片。
- 热重载：监视文件（防抖 200ms）。`lua/*` 变更 → 保留参数与场景状态，重建环境；`layer/*.frag` → 重编译，成功才换旧程序；`scene.json` → 整场景重载。
- 不允许 `io` / `os` / `package` / `debug`；`print` 重定向到 `mate.log`；需要落盘只能走 `mate.storage`。

### 10.9 音频

- 统一用 **miniaudio 高层 `ma_engine`**：混音、总线、3D 空间化一次到位，不再自己写设备回调与混音。
- **已落地（2026-11-07）**：
  - `src/core/mate_audio.cpp`：底层 `ma_device` + `ma_decoder` 的**单轨无缝循环**（输出固定 f32/2ch/48k，Windows 走 WinMM，`MA_NO_WASAPI` / `MA_NO_DSOUND` 减少依赖），只服务静水环境音；场景层只看到 `mate::AudioLoop`（start/stop/setVolume/setPaused）。
  - `src/core/mate_audio3d.cpp`：高层 `ma_engine` + 自定义 `ma_data_source` 的**视频音轨 3D 音源**，已在 matex2 接入（产品场景可直接复用，见 §11.5）；场景层只看到 `mate::SpatialVideoAudio`（init/attach/detach/update + listener/source/volume/spatial），头文件不含 miniaudio。
  - 多音源混音、总线（`music`/`ambience`/`sfx`/`ui`）仍在后续阶段，统一由 `ma_engine` 承担。
- 显式音轨（wav/ogg/mp3/flac）由 miniaudio 自带解码；视频音轨复用 `xav.h` 解码，再包一个自定义 `ma_data_source` 挂到 `ma_sound`，与普通音源走同一条混音/空间化路径。
- 总线：`music` / `ambience` / `sfx` / `ui`，实施音量 = `mate.json.audio.master × 场景总线 × 音源`。
- 监听者：默认取当前相机的位置/朝向（`ma_engine_listener_set_position/orientation`），每帧同步一次；没有相机时用场景原点。
- 空间化：`spatial: true` 的音源设置 `position` 与 `rolloff`，衰减模型取线性/反距离可配；不做 HRTF，保持开销可控。
- 策略：前台全屏应用/演示模式时按 `muteWhenOccluded` 暂停或静音；恢复时淡入 300ms，避免突兀。
- 音频不占渲染层，与层图平行；Lua 通过音源 `name` 控制，和视觉层解耦。

### 10.10 输入与交互

- 引擎保留 `mate.json.hotkeys` 里的全局热键；场景只能注册**未被占用**的组合，冲突时拒绝并写日志。
- 命中区域在 `scene.json.input.regions` 声明（`rect`（屏幕坐标）、`polygon`（屏幕坐标）、`sphere3d`（世界坐标投影）），由引擎做测试后分发事件给 Lua / 原生层。
- 交互规则沿用 v8：默认完全穿透；只有光标落在命中区域且处于交互态时才拦截，右键 `DOWN`/`UP` 成对吞掉，避免桌面弹菜单。
- 命中区域支持 `capture: false`（观察式）：引擎照常收到事件并派发给场景，但**不吞事件**，桌面图标点击不受影响。静水这类"点任意水面起涟漪"的场景必须用这个模式。
- 原生层可在自己的 `onEvent` 里做精确拾取（射线求交），优先级高于区域级判定；`sphere3d` 只是廉价兜底。
- 壁纸模式无输入焦点是常态：场景必须在"没有任何事件"的情况下依然成立；面板控件只在调试态或交互态出现。
- 光标形状可由场景声明（`cursor: "default" | "grab" | "pointer"`），离开区域即还原。

### 10.11 多显示器与分辨率

- 一期：一个覆盖**虚拟桌面并集**的全屏窗口，场景只渲染一次；`mate.monitor:list()` 暴露每屏矩形/DPI，供场景做安全区与构图偏移。
- 设计分辨率 `canvas.design` + `aspect` 决定世界→屏幕映射；所有层使用归一化坐标或世界坐标，禁止写死像素尺寸（像素级参数乘 `uPixelScale`）。
- DPI 感知 `perMonitorV2`；跨屏不同缩放时，UI 层按所在屏缩放，3D 层按统一逻辑分辨率渲染。
- 二期再评估"每屏独立场景/独立窗口"，届时场景清单只需增加 `"monitor": 0`，层结构不变。

### 10.12 性能与功耗

- 引擎掌握策略：档位（30/60/120）、暂停（1fps）、被遮挡（5fps）、MSAA/HDR 开关；场景只能给 `perf.cost` 与 `perf.targetFps` 作参考，不能直接改全局上限。
- 每场景有硬预算：首帧建场 ≤ 1.5s（超出进后台继续建，先显示占位）、显存 ≤ 512MB、每帧 CPU 提交 ≤ 4ms；超预算在面板标红并写日志。
- 程序化内容（地形/植被/木屋）首次生成后按 `seed + scene.version` 烘焙进 `MCache/`，命中即秒开；种子或版本变化才重建。
- 首帧 `dt` 截断 0.1s（沿用 matex 的经验），避免加载耗时把场景推进到另一个时段。
- 资源加载与 GL 上传严格分线程：工作线程只产 CPU 像素，`glGenTextures`/`glTexImage2D` 回主线程（沿用 §六.11/12 的约束）。
- 着色器预编译：进入前把所有 `layer/*.frag` 编译并链接完，失败列出文件与行号，不允许带着错误进首帧。

### 10.13 生命周期与场景切换

```
Discover → Parse(清单校验) → Mount(VFS) → Preload(加载线程)
        → Upload(主线程) → Build(层/原生模块) → Enter(淡入)
        → Update/Render ⇄ Pause/Resume → Exit(存档) → Unload
```

- 切换：先构建下一个场景到"就绪"（后台），再 300ms 交叉淡出淡入，旧场景在淡出结束后才释放。
- 失败：解析/编译/脚本任一失败 → 保持当前场景不变；若当前场景本身失败 → 载入 `builtin://scenes/fallback`，面板显示错误与"打开日志"。
- 单实例：第二个进程只把切换/唤出请求发给已有实例，不重复注册热键（沿用 1409 的经验）。
- 切换时保留：全局配置、音频设备、GL 上下文、ImGui 上下文；只重建场景相关资源。

### 10.14 打包与工具

```
matepack bin      Data\builtin  ma.bin          # 打包内置资源
matepack pack     Data\MScene\Watch  Watch.mscene
matepack list     Watch.mscene
matepack unpack   Watch.mscene  Data\MScene\Watch
matepack validate Data\MScene\Watch --engine mate.exe
matepack shot     Data\MScene\Watch --size 1920x1080 --out preview.jpg
```

- `.mscene` 用 zip（miniz，store/deflate），引擎按扩展名自动挂成 VFS，场景代码感知不到差异；开发期用文件夹便于热重载。
- `validate` 检查：`scene.json` schema、`id` 合法性、资源引用是否存在、`engine` 版本、参数/层名重复、层图环路、着色器能否编译（需 GL 上下文）、音频/字体可读性。
- 版本兼容：清单未知字段忽略；`schema` 升级才迁移；`engine` 不满足时提示"需要更新 mate.exe"而不是崩溃。

### 10.15 与现有代码的落位

产品基线已定（2026-11-06）：**引擎骨架一比一复刻自 `matex0.cpp`，已拆成 `src/app/main.cpp`（应用外壳）+ `src/core/mate_core.cpp`（VFS/纹理/GLSL/FBO/IScene）+ `src/core/mate_audio.cpp`（miniaudio 无缝循环环境音）+ `src/scenes/stillwater/stillwater.cpp`（首个场景），并通过 `mate.bat mate` 编译成 `mate.exe`**；`matex1.cpp` 只作为场景实现参考，不进入引擎。`AppState` 里混着的场景参数全部外置为场景参数。窗口/GL/输入等引擎部分直接来自 matex0，不再从 matex1 抽取。

| 现有 | 去向 |
|---|---|
| 窗口/WorkerW/热键/低功耗/日志 | `src/app/main.cpp`（后续再拆 `src/app/desktop.cpp`） |
| 音频设备/解码（miniaudio） | `src/core/mate_audio.cpp`：单轨 `AudioLoop`（环境音）；`src/core/mate_audio3d.cpp`：`ma_engine` 3D 空间音源（视频音轨，matex2 已验证）；产品场景后续统一到 `ma_engine` 总线 |
| `AppState` 里的场景参数（timeOfDay、rain、bloom、tilt、exposure…） | `Stillwater/scene.json` 的 `parameters` |
| 视频帧源（`AVDecoder` + 上传） | `src/core/mate_video.h`：`VideoSource`（解码线程 + 解码时缩放），场景侧用 `mate::Texture::uploadSub` |
| `buildSky/buildTerrain/buildWater/buildVegetation/buildCabin…` | `src/scenes/stillwater/`，注册为原生模块 |
| `EffectComposer`（BloomPass + 调色） | `post` 层（bloom → grade），不再走 threepp 的 composer |
| `fxcode.h` / 自写 shader | `Stillwater/layer/*.frag` + 引擎前言 |
| RC 内嵌字体/模型 | `ma.bin`（`builtin://fonts/...`、`builtin://models/...`） |
| 模型目录扫描 | 引擎模型库（场景在清单里选，不全局扫） |

原生模块注册（示意）：

```cpp
MATE_SCENE_MODULE(stillwater)
    .layer("world", &buildWorld)
    .layer("glass", &buildGlass)
    .parameter("seed", 90210)
    .api("sampleHeight", &sampleHeight);
```

三个首批场景的能力覆盖：

| 场景 | 形态 | 验证点 |
|---|---|---|
| Stillwater 静水 | `native`（现有 matex1 迁移） | 程序化大场景、后处理链、天气参数、3D 命中 |
| Watch 劳力士表 | `declarative`（model + shader + text + ui） | 模型层、材质/环境光、指针动画、无原生代码也能成立 |
| ShortTrip 短途之旅 | `declarative`（flipbook/video + timeline + audio） | 预渲染序列、时间轴、音频总线、场景切换 |

落地顺序：

1. VFS + `mate.json` + `ma.bin` + 兜底场景（先保证"没有场景也能启动"）
2. `scene.json` + 层运行时（shader/model/image/video）+ 渲染图
3. 参数系统 + 自动面板 + 持久化
4. Lua 环境与热重载
5. Stillwater 原生模块迁移（分批：天空/地形 → 水/河 → 植被/岩石 → 木屋/玻璃 → 后处理）
6. 3D 音频 + 命中区域
7. `matepack` + 场景切换器 + `.mscene`

### 10.16 已定与待定

已定（2026-11-06）：

| # | 事项 | 决定 |
|---|---|---|
| 1 | 引擎基线 | 基于 `matex0.cpp` 复刻为 `src/app/main.cpp` + `src/core/*.cpp`，产物 `mate.exe`；`matex1` 只做 Stillwater 场景素材 |
| 2 | 产品形态 | play / editor 两个 exe，共用同一套 core 与场景包格式 |
| 3 | 场景结构 | JSON 声明结构 + 原生模块做重活，Lua 只做行为 |
| 4 | native 接口 | 只暴露公开的 `ILayer` / `RenderContext`，场景模块不碰引擎内部 |
| 5 | Lua 绑定 | 手写薄封装（当前 API 面 < 50 个函数，不引 sol2） |
| 6 | editor UI | 复用同一套 ImGui，编辑态与运行态只差权限和面板 |
| 7 | native 分发 | 模块编进 exe，不做 DLL 插件 |

待定：

| # | 事项 | 建议 |
|---|---|---|
| 8 | `.mscene` 分发 | 先文件夹，打包工具后置 |
| 9 | 多显示器 | 一期单场景覆盖虚拟桌面并集 |
| 10 | 着色器缓存 | 先不做，编译耗时可接受再补 |

### 10.17 StillWater（静水）实现拆解

来源 `E:\Dev\H5\StillWater-v2.html`：641 行、纯屏幕空间 2D 管线，不含 3D 场景图。

| HTML 部分 | 做法 | mate 中的位置 |
|---|---|---|
| `pond.png` 底图 | CSS `object-fit: cover` | `shader` 层的纹理输入 + 引擎提供的 cover 采样（`uImageSize` / `uResolution`） |
| WebGL 水面 | 24 个涟漪槽 + 折射/色散/法线光/镜面高光/白沫 | `layer/water.frag`，涟漪缓冲由 native 层写入 |
| 锦鲤 Canvas2D | 3 条鱼 × 64 横片沿正弦脊柱摆动 | native `stillwater/koi`：CPU 只算角度/速度/受惊，GPU 顶点着色器做切片形变 |
| 落叶 Canvas2D | 5~10s 生成、下落、落水起涟漪、漂移 | native `stillwater/leaves`（落水要回调，粒子层表达不了） |
| 环境音 | m4a，裁首尾静音 + 1.8s 交叉淡化实现无缝循环 | 已用 `D:\ffbin\ffmpeg.exe` 转出 `data/pond-audio.wav`（`-ac 1 -ar 48000 -acodec pcm_s16le`，27.9s、2.68MB）；工程内无 Vorbis 解码器，且 PCM 无编码延迟，循环处不留空隙。`src/core/mate_audio.cpp`：`ma_decoder` 解码 + `ma_device`（WinMM 后端）回调，读空即 `ma_decoder_seek_to_pcm_frame(0)` 回绕；音量/暂停为原子量，可跨线程设置 |
| 视频面板（原 HTML 没有，新增验证项） | native `stillwater/video`：`VideoSource` 输出 1280 宽的帧，按 `rect` contain 贴在水面上（`order 50`） |
| 点击/拖拽 | 全屏 pointer 事件，鱼受惊 | 全屏命中区域 `capture: false`（观察式，不吞事件） |

层图：

```
photo(资源纹理) ─────────────┐
koi(native) → koiTex ────────┼→ water(native+shader) → waterColor ─→ video(native, alpha, rect) → leaves(native, alpha) → final
rings(RGBA32F 纹理/UBO) ─────┘
```

要点：

- 该场景验证的是**屏幕空间**通路（`shader` + `native` 互取纹理），不需要 threepp 场景图；`model` 留给 Watch 验证，`video` 已在本场景的 `video` 层先行落地。
- `rings[24]` 做成 24×1 的 RGBA32F 纹理（或 UBO），每帧更新；波前/次波/泡沫/落点凹陷四套公式逐行移植，不改数学。
- GLSL 100 → 450：`texture2D`→`texture`、`gl_FragColor`→`out vec4`、去掉 precision，其余不变。
- 锦鲤不要照搬 Canvas2D 的 64 次 `drawImage`：一条鱼 64×2 顶点，顶点着色器按 `v` 算脊柱切线/扇形缩放，3 次 draw call。
- 观察式输入是硬需求：壁纸层吞点击会让桌面图标不可用；涟漪应"看见点击"而不是"抢走点击"。
- 环境音现状：`scene.json` 用 `assets.audio` + 参数 `audio` / `audioVolume`（默认开、0.6）声明；场景 `update()` 首次进入时 `AudioLoop::start()`，面板勾选框与音量滑杆实时生效，`fc.dt <= 0`（暂停/低功耗档）时同步 `setPaused(true)`，避免空转。
- 待定：原设计"壁纸不应自动出声"（默认静音、首次交互再淡入），当前第一版是装载即播放，需要后续定策略（例如壁纸模式默认 0 音量 + 交互后淡入 300ms）。
- 视频层现状：`scene.json` 用 `assets.video` + `layers.video`（`order 50`、`rect [0.06,0.08,0.26,0.30]`、`blend alpha`）+ 参数 `video` / `videoWidth`（默认 1280，解码时直接缩放）/ `videoAlpha`；`VideoSource` 在独立线程解码并直接输出 YUV420P 三平面，`update()` 有新帧就把 Y/U/V `memcpy` 进双 PBO 的持久映射指针再贴上 3 个 R8 纹理（`GLsync` fence 防覆盖），`drawVideo()` 用 BT.709 限制范围 → RGB 的片元着色器，在水面之后、落叶之前画一个保持视频比例的矩形。视频文件缺失时只关掉这一层，不影响场景其余部分。
- 场景最小能力清单：`shader` 层支持资源纹理与 uniform 缓冲、`native` 层可写自己的 RGBA 目标并暴露给后续层、观察式输入、参数/UI、无缝循环音频。

---

## 十一、TIPS

### 11.1 如何在几何面片上渲染 FX

**目标**：把 `fxcode.h` 里的 Shadertoy 特效（原本是全屏层的素材）贴到场景图里一张普通 `Plane` 上，而不是继续铺满屏幕。

**代码位置**：[matex2.cpp](file:///e:/MX/Projects/Mate/matex2.cpp) 的 `buildFxPlaneFragment()`、`kFxPlaneVertexShader`、`scene->add(fxPlane)` 与主循环里的 uniform 推进；素材仍是 [fxcode.h](file:///e:/MX/Projects/momo/fxcode.h)。

**与全屏 P4 层（[main.cpp](file:///e:/MX/Projects/Mate/src/app/main.cpp#L58-L58)）的差异**：

| 项目 | 全屏层（P4） | 几何面片（matex2） |
|---|---|---|
| 顶点着色器 | 直接给 NDC，铺满屏幕 | 普通 `projectionMatrix * modelViewMatrix * vec4(position,1.0)`，顺带把 `uv` 传给片元 |
| `fragCoord` 来源 | `gl_FragCoord.xy`（屏幕像素） | `uv * iResolution`（面片内部像素） |
| `iResolution` | 窗口/视口分辨率 | 面片的"目标像素尺寸"，需与面片宽高比一致 |
| 空间归属 | 永远正对相机，不随场景变化 | 随面片旋转/平移，可被其它物体遮挡、可进反射 |

**实现流程**：

1. 引用素材源码（相对 `Projects/Mate`）：

```cpp
#include "../momo/fxcode.h"   // 复用 MX 现成的 Shadertoy 特效源码（g_fxSource0..3）
```

2. 顶点着色器：普通 MVP 变换 + 传 uv。threepp 的 ShaderMaterial 会自动注入 `position` / `uv` 属性和 `modelViewMatrix` / `projectionMatrix` / `modelMatrix` 等 uniform，不用自己声明：

```glsl
varying vec2 vFxUv;
void main() {
    vFxUv = uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
```

3. 片元源码做三步改写（`buildFxPlaneFragment`）——**这是整个方案的核心**：
   - `gl_FragCoord.xy` → `(vFxUv * iResolution)`：让特效跟着面片走，而不是钉死在屏幕上；
   - `void main()` → `void fxEntry()`：避免和 threepp 的模板入口重名；
   - 末尾追加一层 `fxGain` 包壳，用于统一调亮度。
   改写后的 `varying` 必须放在源码之前（GLSL 要求先声明后使用）：

```cpp
static std::string buildFxPlaneFragment(const char* body) {
    std::string s = body;

    const std::string from = "gl_FragCoord.xy";
    const std::string to   = "(vFxUv * iResolution)";
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);   // 必须按 from.size() 替换，不能写死长度
        p += to.size();
    }

    const std::string entry = "void main()";
    const size_t pos = s.find(entry);
    if (pos == std::string::npos) return s;   // 兜底：认不出就直接用原源码

    s.replace(pos, entry.size(), "void fxEntry()");
    s = "varying vec2 vFxUv;\n" + s;
    s += "\nuniform float fxGain;\n"
         "void main() {\n"
         "    fxEntry();\n"
         "    gl_FragColor.rgb *= fxGain;\n"
         "}\n";
    return s;
}
```

4. 建面片 + ShaderMaterial（`iResolution` 取 640×360，与面片 3.2×1.8 的 16:9 对齐）：

```cpp
auto fxMaterial = threepp::ShaderMaterial::create();
fxMaterial->vertexShader   = kFxPlaneVertexShader;
fxMaterial->fragmentShader = buildFxPlaneFragment(FXCODE::g_fxSource1);
fxMaterial->uniforms["iTime"]       = threepp::Uniform(0.0f);
fxMaterial->uniforms["iResolution"] = threepp::Uniform(threepp::Vector2(640.0f, 360.0f));
fxMaterial->uniforms["fxGain"]      = threepp::Uniform(1.0f);

auto fxPlane = threepp::Mesh::create(threepp::PlaneGeometry::create(3.2f, 1.8f), fxMaterial);
fxPlane->position.set(0.0f, 2.5f, 0.0f);   // 悬在砖墙面片上方
scene->add(fxPlane);
```

5. 主循环推进时间，并**显式标记 uniform 需要重新上传**（ShaderMaterial 的自定义 uniform 只在 `refreshMaterial` 或 `uniformsNeedUpdate` 时上传，漏掉这一步动画会卡住）：

```cpp
fxTime += dt;
fxMaterial->uniforms["iTime"].value<float>() = fxTime;
fxMaterial->uniformsNeedUpdate = true;
```

**踩坑记录 / 注意点**：

- **替换长度必须用 `from.size()`**：最初写死 `14`（`gl_FragCoord.xy` 实际 15 个字符），残留了一个 `y`，GLSL 报 `syntax error, unexpected identifier, expecting ')' at token 'y'`，报错行正好是改写后的 `vec2 fragCoord = ...` 那一行，很容易误判成驱动问题。
- **不要照搬 `gl_FragCoord`**：直接照搬会得到"贴屏"效果（面片移动时图案不动），看起来像贴图错位；改成 `uv * iResolution` 才真正贴在面片上。
- **`iResolution` 与面片宽高比要一致**，否则画面拉伸；改面片尺寸时同步改这个值。
- **朝向无需翻转**：`PlaneGeometry` 的 uv 是 v=1 在上，Shadertoy 的 `fragCoord` 原点在左下，方向天然一致；若材质设 `side = Double`，背面会看到镜像。
- **换预设的额外依赖**：`g_fxSource0` / `g_fxSource1` / `g_fxSource3` 只依赖 `iTime` / `iResolution`；`g_fxSource2`（雨滴）还声明并使用了 `sceneTex` / `lyricsTex` / `texSize` / `iMouseXY` / `iMouseZW`，换它要先备好纹理与鼠标 uniform。
- **验证方式**：`matex2.bat` 编译后运行，threepp 把 shader 编译日志打到 **stderr**（stdout 是控制台缓冲输出）；stderr 为空即编译通过。实测 640×360 的梵高日落面片稳定 165 fps，原 box / sphere / 砖墙面片渲染不受影响。
- 面片当前不参与光照（纯 `ShaderMaterial`），也不会被 Reflector 收录进反射（反射只渲染指定的镜像场景），后续若要"特效面片出现在静水倒影里"需要把面片加入反射相机可见集合。

### 11.2 同一面片多层 FX 合成（梵高 + 雨滴）与离屏封装

**目标**：同一张面片上"先梵高、后雨滴"，即雨滴叠在梵高画面上，而不是两张面片互相遮挡。

**为什么必须分两遍**：`g_fxSource2`（HeartfeltRain）是**后处理型**特效——它把 `sceneTex` 当"背景画面"，通过折射/模糊把雨滴叠上去，输出恒为 `alpha = 1`。所以如果直接建两张共面面片，后画的雨滴会整块盖住梵高。正确管线是先把梵高渲进离屏纹理，再让雨滴把它当 `sceneTex` 采样：

```
第 1 遍：梵高 FX → 离屏全屏四边形 → RenderTarget(640×360)
第 2 遍：主场景 render，fx 面片用雨滴 FX，sceneTex = 上面那张 RT
```

**代码位置**：[matex2.cpp](file:///e:/MX/Projects/Mate/matex2.cpp) 中的 `FxOffscreen`（离屏封装）、fx 面片创建段、主循环。

**离屏渲染封装 `FxOffscreen`**：把"创建 RT + 全屏四边形 + 正交相机 + 材质 uniform + 进出 RenderTarget"全部收进一个类（[matex2.cpp](file:///e:/MX/Projects/Mate/matex2.cpp#L509-L567)），任意场景可复用：

```cpp
class FxOffscreen {
public:
    FxOffscreen(int width, int height, const char* shaderBody);  // 建 RT/材质/场景/相机
    void update(float dt);                                       // 推进 iTime 并标记 uniform 上传
    void render(threepp::GLRenderer& renderer);                  // 渲到自己的 RT，并复位到屏幕
    threepp::Texture* texture() const;                           // 供下游 sceneTex
    const threepp::Vector2& sizeVec() const;                     // 供下游 texSize / iResolution
private:
    std::shared_ptr<threepp::RenderTarget> rt;
    std::shared_ptr<threepp::ShaderMaterial> material;
    std::shared_ptr<threepp::Scene> scene;
    std::shared_ptr<threepp::OrthographicCamera> camera;
    threepp::Vector2 size;
    float time = 0.0f;
};
```

构造内部的关键点：

- RT 参数：`Format::RGBA`（注意不是 `RGBAFormat`）/ `Type::UnsignedByte` / `Filter::Linear` / `depthBuffer = false`；
- 全屏四边形：`PlaneGeometry(2,2)` 的 `xy` 恰好是 -1..1，顶点着色器直接 `gl_Position = vec4(position.xy, 0.0, 1.0)`，**忽略所有相机矩阵**；
- 必须 `quad->frustumCulled = false`，否则这张"没有正确 MVP"的四边形会被视锥剔除；
- 相机只需一个空 `OrthographicCamera::create()`（顶点不读相机矩阵，但 `render()` 要求传相机）。

**使用方式**：

```cpp
FxOffscreen fxBase(640, 360, FXCODE::g_fxSource1);   // 第 1 遍：梵高

// 第 2 遍：雨滴材质贴到面片上，采样第 1 遍结果
fxMaterial->vertexShader   = kFxPlaneVertexShader;   // 普通 MVP + uv
fxMaterial->fragmentShader = buildFxPlaneFragment(FXCODE::g_fxSource2);
fxMaterial->uniforms["iResolution"] = threepp::Uniform(fxBase.sizeVec());
fxMaterial->uniforms["texSize"]     = threepp::Uniform(fxBase.sizeVec());
fxMaterial->uniforms["sceneTex"]    = threepp::Uniform(fxBase.texture());
fxMaterial->uniforms["iMouseXY"]    = threepp::Uniform(threepp::Vector2(0.0f, 0.0f));
fxMaterial->uniforms["iMouseZW"]    = threepp::Uniform(threepp::Vector2(0.0f, 0.0f));
fxMaterial->uniforms["lyricsTex"]   = threepp::Uniform(lyricsTexPtr);   // 1×1 全透明占位
```

主循环（顺序不能反）：

```cpp
fxBase.update(dt);                                       // 第 1 遍：推进 iTime
fxMaterial->uniforms["iTime"].value<float>() = fxTime;   // 雨滴材质同样推进
fxMaterial->uniformsNeedUpdate = true;

fxBase.render(*renderer);                                // 先渲离屏，才能在本帧被采样
renderer->clear();
renderer->render(*scene, *camera);                       // 第 2 遍：主场景
```

**踩坑记录 / 注意点**：

- **顺序**：离屏 pass 必须在主场景 `render` 之前，否则面片采到的是上一帧纹理（表现为雨滴画面延迟一帧，转动相机时明显）。
- **`lyricsTex` 必须是 1×1 全透明（alpha = 0）**：雨滴源码最后是 `col = mix(col, lrc.rgb, lrc.a)`；若沿用 `main.cpp` 里"黑色不透明占位"的写法，`lrc.a = 1` 会把整屏雨滴变黑（P4 的雨滴预设其实一直受此影响）。
- **`texSize` 必须等于 RT 尺寸**：它参与 `inBounds` 的世界适配（cover）计算，写错会出现黑边或裁切；`iResolution` 则继续与可见面片宽高比对齐。
- **雨滴自带 `HAS_HEART` 剧情**：`T = mod(iTime, 102)`，0~10s 淡入、97~102s 淡出后循环；且 `rainAmount = heart`，**雨只落在心形区域内**。不能简单删掉 `#define HAS_HEART`——`story` 定义在该宏内、但 `colFade` 与闪电又引用它，删掉会编译失败，需要同时补 `story` 声明。
- **RT 方向**：NDC 上下与 RT 纹理 (0,0)=左下 一致，采样不翻转，无需 `UV.y = 1 - UV.y`。
- **尺寸固定**：RT 固定 640×360，窗口缩放只影响面片屏幕像素数，不影响特效分辨率。
- **验证**：编译无 shader 报错（stderr 为空）、165 fps；面片显示"梵高 + 雨滴折射 + 蓝调/闪电后处理"，其余物体与反射正常。

**扩展**：这条管线可以任意加层——每多一层就再 `FxOffscreen` 一个实例，把上一层的 `texture()` 喂给下一层的 `sceneTex`，按链式顺序依次 `render()` 即可；各层尺寸彼此独立。

> 注：11.2 的 `FxOffscreen` 只有"一条链、每层各一块 RT"。当来源变成"若干图片 + 若干 FX，且各自只占画布一块区域"时，链式结构就不够用了，改用 11.3 的 `FxCanvas` 图层栈（它已取代 `FxOffscreen`）。

### 11.3 通用图层栈：图片层 + 多 FX 层的组织与排序

**目标**：一张面片的底图是照片（如 `e:\MX\Data\lev.jpg`），上面再叠若干 FX 层，每层既要能整幅铺满、也要能只占画布一小块，最终一张纹理贴回面片。

**结论先行：排序与定位是两个正交维度，分别用 `order` 和 `rect` 表达，不要混成一个"优先级"数字。**

| 维度 | 手段 | 语义 |
|---|---|---|
| 前后（谁压谁） | `order` | 小在下、大在上；`order` 相同则按添加顺序，后加在上（`std::stable_sort` 保证顺序稳定） |
| 位置（画在哪） | `rect(x, y, w, h)` | 画布归一化坐标、**左下角为原点**（与 Shadertoy 的 `fragCoord` 方向一致） |

`rect` 不参与遮挡判断：两层不重叠时 `order` 谁大谁小都无所谓；**只有重叠区域才由 `order` 决定谁盖谁**。所以"同层不同位置"永远只用 `rect` 表达，"前后关系"永远只用 `order` 表达。

**代码位置**：[matex2.cpp](file:///e:/MX/Projects/Mate/matex2.cpp#L537-L713) 的 `FxCanvas`（图层栈容器）、`addLayer()`（`rect` → NDC 的换算）、`render()`（乒乓合成）、[使用段](file:///e:/MX/Projects/Mate/matex2.cpp#L817-L853) 与 [主循环](file:///e:/MX/Projects/Mate/matex2.cpp#L886-L896)。

**结构**：一张画布 = 一条图层栈 = 两块 RT（`front` 累积画面 / `back` 拷贝中转），所有层都画进同一块纹理：

```
            ┌──────────── front (1024×576) ────────────┐
order 0 图 │ lev.jpg 铺满 rect(0,0,1,1)               │
order 1 FX│ 梵高   只画 rect(0.5,0,0.5,1)  → 右半边  │
order 2 图 │ 棋盘格 只画 rect(0.03,0.04,0.16,0.28)    │
order 3 FX│ 雨滴   整幅后处理（samplesPrevious=true）│
            └─────────────────────────────────────────┘
                            → texture() 给面片 map
```

**接口**（[matex2.cpp](file:///e:/MX/Projects/Mate/matex2.cpp#L537-L605)）：

```cpp
class FxCanvas {
public:
    FxCanvas(int width, int height);                                  // 建 front/back、正交相机、拷贝场景、1×1 透明占位纹理

    void addImage(const std::shared_ptr<Texture>& tex,
                  const Vector4& rect, float opacity, int order);     // 图片层：整张图铺进 rect

    void addFx(const char* body, const Vector4& rect, float opacity, int order,
               bool samplesPrevious = false);                          // FX 层：Shadertoy 源码铺进 rect

    void update(float dt);                                            // 统一推进各层 iTime + 标记 uniform 上传
    void render(threepp::GLRenderer& renderer);                       // 按 order 合成，结束自动回到屏幕
    threepp::Texture* texture() const;                                // 最终画面，给可见面片的 map
};
```

**两类层的画法不同，这是设计的核心**：

| 类型 | 判定 | 画法 |
|---|---|---|
| 前向叠加层 | 图片、只输出自身图案的 FX（`g_fxSource1` / `g_fxSource3`） | `transparent = true` + `depthTest/depthWrite = false` + `autoClear = false`，直接按 SrcAlpha 叠到 `front` |
| 后处理型层 | 需要读下层画面的 FX（`g_fxSource2` 雨滴、`g_fxSource0`），`samplesPrevious = true` | 先把 `front` 整幅拷到 `back`，再在 `back` 上画本层、同时采样 `front`，最后交换 front/back |

**层矩形的换算**（`addLayer`，层本身是一个 `PlaneGeometry(1,1)` 四边形 + 独立 `Scene` + 正交相机）：

```cpp
mesh->scale.set(2.0f * rect.z, 2.0f * rect.w, 1.0f);
mesh->position.set(2.0f * (rect.x + 0.5f * rect.z) - 1.0f,
                   2.0f * (rect.y + 0.5f * rect.w) - 1.0f, 0.0f);
mesh->frustumCulled = false;   // 铺满 NDC 的四边形不能进视锥剔除
```

`iResolution` 必须按**这一层自己的像素尺寸**（`rect.z * size.x, rect.w * size.y`）算，不是画布全尺寸——否则图案会被拉伸成整张画布比例。

**使用方式**（[matex2.cpp](file:///e:/MX/Projects/Mate/matex2.cpp#L817-L853)）：

```cpp
FxCanvas fxCanvas(1024, 576);                                   // 画布比例与面片 3.2×1.8 对齐（16:9）

fxCanvas.addImage(levTex,        {0.0f, 0.0f, 1.0f, 1.0f}, 1.0f, 0);          // order 0：底图铺满
fxCanvas.addFx(FXCODE::g_fxSource1, {0.5f, 0.0f, 0.5f, 1.0f}, 1.0f, 1);      // order 1：梵高占右半边
fxCanvas.addImage(checkerTex,    {0.03f, 0.04f, 0.16f, 0.28f}, 1.0f, 2);      // order 2：左下角小图
fxCanvas.addFx(FXCODE::g_fxSource2, {0.0f, 0.0f, 1.0f, 1.0f}, 1.0f, 3, true); // order 3：雨滴后处理

// 可见面片：map 指向画布最终纹理
fxPlaneMaterial->uniforms["map"] = threepp::Uniform(fxCanvas.texture());
```

主循环（顺序不能反）：

```cpp
fxCanvas.update(dt);
fxCanvas.render(*renderer);                                        // 离屏合成，内部结束已切回屏幕
fxPlaneMaterial->uniforms["map"] = threepp::Uniform(fxCanvas.texture());
fxPlaneMaterial->uniformsNeedUpdate = true;                        // 后处理层会换 front/back，必须每帧重绑
renderer->clear();
renderer->render(*scene, *camera);
```

**踩坑记录 / 注意点**：

- **每帧必须重绑 `map`**：有 `samplesPrevious` 层时 `front`/`back` 每帧交换，最终纹理在两张 RT 之间来回跳；只在启动时绑一次会看到画面在两层结果之间闪烁。
- **画布底色要单独处理**：渲染器只有一个 clear color，`render()` 里先 `getClearColor` 存旧值、改成黑色作画布底色，结束时 `setRenderTarget(nullptr)` + `setClearColor(旧值)` 恢复。不能直接用 `glClearColor`，否则 threepp 的 `GLState` 缓存与实际 GL 状态不一致，会导致主场景背景清成错误颜色。
- **`depthBuffer = false`**：画布不需要深度，层叠顺序完全由绘制顺序（`order`）决定，开深度反而会让后画的层被剔除。
- **`samplesPrevious` 层不能直接叠**：它输出恒为 `alpha = 1`，直接叠会整块盖住下层（与 11.2 的结论一致），所以必须走拷贝乒乓。
- **雨滴的 10s 淡入容易被误判成 bug**：`g_fxSource2` 带 `HAS_HEART` 剧情，`T = mod(iTime, 102)`，前 10s 是 `col *= fade` 淡入阶段，画布接近全黑；这不是图层栈的问题，等 `iTime > 10s` 就正常。
- **验证方式**：这台机器上 `CopyFromScreen` / 窗口 DC `BitBlt` 抓不到 OpenGL 窗口客户区（只能拿到黑图），**不要拿截屏判断黑屏**；可靠做法是在 `render()` 之后临时 `glReadPixels` 导出 BMP 看内容。实测 1024×576、四层合成后约 3000 fps（窗口被遮挡时 `SwapBuffers` 不等垂直同步，帧率会虚高，属正常现象）。
- **扩展方向**：层的混合模式（Add/Multiply）、遮罩（mask 纹理）、按时间轴增删层，都可以在 `addLayer` 上加字段，不需要改动 `order` / `rect` 这套语义。

### 11.4 视频帧源：一个类 + 两个消费方

**目标**：视频要同时服务 threepp 沙箱（`matex2`）和纯 GL 产品（`mate`），但「解码 / 线程 / 同步 / 行序」只能写一份。

**为什么不能直接复用 threepp 版**：`mate` 的场景层只有 OpenGL（`mate::Texture` / `Program` / `IScene`），一旦类里出现 `threepp::Texture`，产品就得把 threepp 拖进场景层。做法是把「取帧」和「上传」切开：

```
            ┌────────── mate::VideoSource（无图形 API）──────────┐
 AVDecoder →│ 解码线程/同步 → 帧队列 → advance(dt) → display_    │→ plane(i)/planeWidth(i)
            └────────────────────────────────────────────────────┘
                     │                              │
        threepp 适配（matex2）              纯 GL 适配（stillwater）
   3 个 R8 纹理 memcpy + needsUpdate      持久映射双 PBO + glTexSubImage2D
```

**接口**（[mate_video.h](file:///e:/MX/Projects/Mate/src/core/mate_video.h)）：

| 成员 | 说明 |
|---|---|
| `open(path, mode, targetWidth=0, yuv=true)` | `targetWidth` 交给 `AVDecoder::setOutputWidth`（解码时直接缩放）；`yuv=false` 回退打包 RGBA |
| `advance(dt)` | 每帧调一次，推进时钟并摘到期的帧；返回 `true` = 有新画面 |
| `plane(i)/planeWidth(i)/planeHeight(i)` | 最近一帧的平面（YUV 时 3 个，RGBA 时只有 0），**第 0 行 = 画面底部**；紧凑行宽 |
| `width()/height()` | 输出尺寸（Y 平面） |
| `stats()` | `decodeMs`（EMA）、`decoded`、`presented` |

**两个消费方**（各自用自己引擎的上传路径）：

```cpp
// threepp（matex2）：3 个 R8 纹理（format=Red, unpackAlignment=1）
if (video.advance(dt)) {
    for (int p = 0; p < 3; ++p) {
        auto& buf = videoPlaneTex[p]->image().data<unsigned char>();
        std::memcpy(buf.data(), video.plane(p), buf.size());
        videoPlaneTex[p]->needsUpdate();
    }
}

// 纯 GL（stillwater）：双 PBO 轮转 + 持久映射
//   建：glBufferStorage(GL_PIXEL_UNPACK_BUFFER, bytes, nullptr, WRITE|PERSISTENT|COHERENT)
//       mapped = glMapBufferRange(..., WRITE|PERSISTENT|COHERENT)   // 只映射一次
//   每帧：memcpy(mapped + planeOffset, plane(i), planeBytes)        // COHERENT，无需 flush
//         glTexSubImage2D(GL_RED, (void*)planeOffset)
//         pbo.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)
//   复用前：glClientWaitSync(pbo.fence, GL_SYNC_FLUSH_COMMANDS_BIT, timeout)
```

**线程与时钟**：

- `Threaded`：解码线程按 `pts` 入队（`kQueueMax=2`），渲染线程只弹「`pts <= clock + 0.04`」的帧；队列空就保持上一帧，不阻塞
- pts 往回跳 = 视频循环，直接把 `clock_` 对齐到新 pts，避免循环后连续追帧
- `Sync` 保留给单线程对照：到点解一帧，解不动就 `seek(0)` 回绕

**踩坑**：

- 帧缓冲**不能直接给解码器的指针**：`decodeNextFrame()` 返回的顶层指针下一帧会被覆盖，必须拷进 `Frame::rgba`；本类把这个拷贝放在解码线程里，顺便完成行序翻转
- 翻转不要放到消费侧：threepp 和 GL 两条路径都按「第 0 行 = 画面底部」写，翻两次会上下颠倒
- 纹理只在 `open()` 后建一次；`targetWidth` 只改输出尺寸、不改宽高比，消费侧别再用原始视频尺寸算缓冲区
- 持久映射必须同时要 `GL_MAP_COHERENT_BIT`，否则每帧写完得 `glFlushMappedBufferRange`；`glMapBufferRange` 返回空时要能回退 `glBufferSubData`，别让视频层变成硬依赖
- 复用同一块 PBO 前要 `glClientWaitSync` 等 fence：GPU 还在从这块缓冲 `glTexSubImage2D` 时被 CPU 覆写，会看到撕裂帧或偶发花屏
- YUV 换算按 0–255 刻度写（`1.164/1.793/2.112` 都是整数刻度系数），最后必须 `gl_FragColor = vec4(clamp(rgb / 255.0, 0, 1), a)`；漏掉 `/255` 会把所有亮部截成纯白、只留少数接近 0 的像素（表现为整块白底 + 稀疏彩色噪点），且纹理回读完全正常，容易被误判成上传或采样问题

### 11.5 视频音轨接 3D 空间音源（miniaudio ma_engine）

**目标**：播放视频自己的音轨，并让它像"挂在面片上的音箱"——面片在哪、离相机多远，声音的方位与衰减就跟到哪；不另开一个解码器，也不重复播音频文件。

**数据链**：

`AVDecoder`（`xav.h`，解码视频帧时顺带解音频，固定 f32/2ch/48k）→ `AudioQueue`（`VideoSource::audioQueue()`）→ 自定义 `ma_data_source`（`onRead` 里 `read()` 取 PCM，不足补静音）→ `ma_sound`（空间化）→ `ma_engine` 混音 → 默认设备。

**接口**（`src/core/mate_audio3d.h`，Pimpl，头文件不含 miniaudio）：

- `init()` / `shutdown()`：创建 / 销毁 `ma_engine`；
- `attach(VideoSource&)` / `detach()`：把音轨挂上 / 摘下（`attach` 内部先 `detach` 旧源）；
- `setListener(px,py,pz, dx,dy,dz)`、`setSourcePosition(x,y,z)`、`setVolume(0..1)`、`setSpatialized(bool)`；
- `update()`：每帧把听者 / 音源写进引擎；`distance()` 供面板显示；`active()`、`error()`。

**接入点（matex2）**：

- 音源 = 立方体 `videoCube->position`，听者 = `camera->position` + `getWorldDirection()`，每帧同步；
- 切视频时顺序必须是 `audio.detach()` → `video.close()` → `video.open()` → `audio.attach(video)`；
- 退出循环先 `audio.shutdown()`，再销毁 ImGui / GL 上下文。

**参数**：衰减模型 `ma_attenuation_model_inverse`，`rolloff=0.3`，近区 `min_distance=2`，`max_distance=300`；不做 HRTF。

**踩坑**：

- 音源不能"结束"：`onRead` 必须把 `*pFramesRead` 报满（哪怕补的是静音），返回 `MA_NOT_IMPLEMENTED` 长度会让 `ma_sound` 进入结束态，之后队列再有数据也不出声
- 欠载是常态：视频循环 / 切源会 `audioQueue_.clear()`，几十毫秒静音属正常；不要因此停 sound 或重开设备
- `detach()` 必须把 `ma_data_source` 里的队列指针清空：音频线程独立运行，`VideoSource` 析构后再去读队列就是野指针
- 这版 miniaudio（0.11.x）没有 `ma_channel_map_init_stereo`，`onGetDataFormat` 直接写 `MA_CHANNEL_FRONT_LEFT/FRONT_RIGHT`；`ma_sound_set_position/volume/...` 多数返回 `void`，别照抄新版 API 的 `ma_result`
- 本 TU 不能定义 `MA_NO_ENGINE`（`mate_audio.cpp` 为省体积定义了它）；两个 `.cpp` 同时定义 `MINIAUDIO_IMPLEMENTATION` 会产生重复符号，产品要把实现合并到单一 TU 或二选一，不能直接把两个都塞进同一个 exe
- 先 `ma_sound_set_spatialization_enabled(true)` 再 `set_position`；`ma_engine_listener_*` 必须每帧同步，只设一次的话相机一移动方位就错
- `attach` 时用 `video.setAudioClock(video.hasAudio())` 切到音频时钟，`detach` 必须切回墙钟；**没有音轨也切**的话 `consumedSeconds()` 永远停在 0，视频就不动了
- **声音卡顿 + 沙沙电流声（2026-11-07 修复）**：根因不是音频设备，而是**解码产出的音轨比设备消费快**（该素材约 15%），2 秒的 `AudioQueue` 8 秒就写满；之后每次写入只塞得下一部分、剩余样本被静默丢弃，播放流里每帧缺一截，听感就是断音 + 沙沙声。修法三条：
  1. `AudioQueue` 增加 `totalRead` 与 `consumedSeconds()`；`VideoSource::setAudioClock(true)` 后**用"已消费音频位置"当视频时钟**（音频主导，不再用墙钟累加）
  2. 解码线程按水位反压（`kAudioHighWater = 36000` 样本 ≈ 0.375 s），宁可视频慢半拍，也不能写满丢样本
  3. `advanceThreaded` 一次只摘**最早的一帧**到期帧（`kDueSlack = 0.012`）；"摘多帧只显示最后一帧"等于主动丢帧，30 fps 素材会成片丢帧（实测丢 23%）
  验证：55 s 连续运行，已解 = 已显示（1612/1612），欠载 0，队列稳定在 0.36–0.40 s，165 fps

**实测（matex2，tvb1.mp4 720×1280）**：`[音频] 音轨已挂到 3D 音源（48000 Hz / 2 ch）`，队列稳定在 0.36–0.40 s 缓冲，连续 55 s 无欠载、无丢帧，165 fps，解码 0.4 ms，退出正常。

---

## 附录：可复用资产

| 资产 | 路径 | 用途 |
|---|---|---|
| 桌面结构检测 | [oapp.h](file:///e:/MX/MO/oapp.h) | WorkerW 定位、Win11 兜底 |
| GL / 模型 / ShaderToy 封装 | [ogl.h](file:///e:/MX/MO/ogl.h) | P4 特效层可直接复用 |
| Shader 集合 | [fxcode.h](file:///e:/MX/Projects/momo/fxcode.h) | P4 特效层素材 |
| 音视频解码器 | [xav.h](file:///e:/MX/MO/xav.h) | P9：`AVDecoder` / `AudioQueue` / 内嵌歌词，从 MPlayer 抽出 |
| 音频输出 | `3rd/include/miniaudio/miniaudio.h` | P9：`ma_device` 回调播放；matex2 起追加 `ma_engine` 3D 空间音源 |
| 参考播放器 | [MPlayer](file:///e:/MX/Projects/MPlayer) | P9 的构建脚本与解码用法来源（链接清单、`/FORCE:MULTIPLE`） |
| 中文字体 | `e:\MX\Data\siyuan.ttf` | ImGui 中文 |
| 测试模型 | `e:\MX\Data\models\DamagedHelmet.glb`、`Fox.glb` | P3 验证 |
| ImGui 源码 | `3rd/include/imgui`（1.91.5） | 已复制，`mate.bat` 自动追加源文件 |
