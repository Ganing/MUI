# MXPaper

> 创建日期: 2026-06-16
> 项目目录: E:\MX\Projects\MXPaper\
> 对标参考: TEST-Filament-GLTF (Filament GLTF 模型浏览器)

---

## 一、项目描述

一款基于 3D 场景的壁纸软件。所有壁纸类型（静态图、动态图、视频、音频、3D 模型）统一在 Filament 3D 场景中呈现，支持播放模式和编辑模式，使用 Lua 脚本驱动场景交互逻辑。

**核心理念**：场景即壁纸。每个壁纸是一个独立的场景目录，自包含描述文件、脚本和资源。

比 Wallpaper Engine 更简洁，但更酷炫。

### 壁纸类型（全部在 3D 场景中实现）

| 类型 | 实现方式 | 参考 |
|------|---------|------|
| 静态图片 | 纹理面片（quad） | — |
| 动态图片/GIF | 纹理序列帧 | — |
| 视频壁纸 | 视频纹理面片 + 音频输出 | MPlayer (FFmpeg) |
| 音频可视化 | 音频频谱驱动粒子/波形 | miniaudio |
| 3D 场景 | glTF 模型 + 光照 + 动画 | gltfio |
| ShaderToy | 全屏 Fragment Shader 特效 | TEST-OGL-SHADERTOY-REC / TEST-Filament-PostProcess |
| 粒子系统 | GPU 粒子发射器（待开发） | — |
| 混合场景 | 上述任意组合 | — |

### 场景结构

```
MXPaper/
├── TEST-MX-Paper.exe
├── Data/                       # 程序运行时资源（exe 同级，必须存在）
│   ├── fonts/                  # 字体文件（siyuan.ttf 等）
│   │   └── siyuan.ttf
│   ├── bin/                    # 二进制资源（预编译 shader、默认模型）
│   ├── config/                 # 配置文件（settings.json）
│   └── license/                # 授权证书（后期）
├── Scenes/                     # 壁纸场景库
│   ├── Forest/                # 一个场景 = 一张壁纸
│   │   ├── scene.json         # 场景描述（名称、作者、资源列表、相机、光照…）
│   │   ├── scene.lua          # 场景初始化/每帧逻辑
│   │   ├── bg.jpg             # 资源：背景图
│   │   ├── tree.glb           # 资源：3D 模型
│   │   └── audio.mp3          # 资源：背景音乐
│   ├── AudioVisualizer/
│   │   ├── scene.json
│   │   ├── scene.lua
│   │   └── song.mp3
│   └── ...
```

### 程序自检

启动时按以下顺序检查（路径相对于 exe 所在目录，用 `ocore.h` 获取 exe 路径）：

| # | 检查项 | 缺失行为 |
|---|--------|---------|
| 1 | `Data/` | 弹 MessageBox 错误 → 退出 |
| 2 | `Data/fonts/` | 弹 MessageBox 错误 → 退出 |
| 3 | `Data/fonts/siyuan.ttf` | 弹 MessageBox 错误 → 退出 |
| 4 | `Data/config/` | 自动创建目录 |
| 5 | `Scenes/` | 自动创建目录 |
| 6 | `Data/bin/` | 自动创建目录 |
| 7 | `Data/license/` | 自动创建目录（后期校验） |

### 双模式设计

| | 播放模式 | 编辑模式 |
|------|---------|---------|
| 用途 | 浏览/轮播/设为壁纸 | 创建/修改场景 |
| 界面 | 缩略图网格 + 全屏预览 | 场景视口 + 属性面板 + 资源面板 + 脚本编辑器 |
| 操作 | 切换、收藏、设壁纸 | 导入资源、摆放物体、调属性、写 Lua |

### 技术栈

| 模块 | 用途 |
|------|------|
| filament | 3D 渲染引擎（统一场景管线） |
| thorvg | OUI 交互组件 |
| glad | OpenGL 加载器 |
| gltfio | glTF 模型加载 |
| lua | 场景脚本引擎（交互逻辑） |
| FFmpeg | 视频/音频解码（参考 MPlayer） |
| miniaudio | 音频播放 |

### UI 动效增强 — ouiex.h

当 `oui.h` 原生控件功能不足或外观不够炫酷时，在 `E:\MX\MO\` 下创建 **`ouiex.h`** 模块（单头文件），结合以下两模块构建带动效的扩展控件：

| 依赖 | 来源 | 提供能力 |
|------|------|---------|
| `OX::Timer` | `ocore.h` | 毫秒级帧计时（`tick()` → dt ms） |
| `OX::Anime::*` | `oanime.h` | 缓动/弹簧/时间线/拖拽/交错动画 |

**ouiex 预设控件类型：**

| 控件 | 动效 |
|------|------|
| `AnimatedButton` | hover 放大、press 缩小回弹、toggle 颜色渐变 |
| `AnimatedPanel` | slideIn/slideOut、fade，带弹性 overshoot |
| `AnimatedToggle` | 弹簧物理开关动画 |
| `AnimatedSlider` | 缓动跟随拖拽 + 释放惯性衰减 |
| `AnimatedProgress` | 圆环/条形进度，缓动过渡 |
| `AnimatedList` | 项目交错入场（Stagger） |
| `Modal/Dialog` | scale+opacity 弹入弹出 |
| `Toast` | slide+fade 通知条 |

**动画数据流：**
```
OX::Timer.tick() → dt(ms)
    → Anime::Timer.update(dt) → 帧控 + 播放速率
        → AnimeInstance.update(dtMs) → easedProgress
            → applyProgress() → lerp 写入 tvg::Paint 属性
                → ThorVG 重绘
```

---

## 二、项目文件

```
E:\MX\Projects\MXPaper\
├── TEST-MX-Paper.cpp       # 主程序（~440 行）
├── BUILD-MX-Paper.bat      # 编译脚本
├── mxpaperdev.md           # 本文件
├── Data/
│   └── LOGO.jpg            # 默认 Logo 纹理
├── Scenes/
│   └── Hello MX/
│       ├── scene.json      # 场景描述
│       └── scene.lua       # 场景脚本
└── prototype/
    └── index.html          # HTML5 UI 原型
```

---

## 三、参考目录

| 路径 | 说明 |
|------|------|
| `E:\MX\MO\` | MX 模块头文件 |
| `E:\MX\3rd\include\` | 第三方头文件 |
| `E:\MX\3rd\lib\` | 第三方静态库 |
| `E:\MX\Data\` | 资源文件（字体/图标/图片/模型） |
| `E:\Dev\KU\thorvg` | ThorVG 源代码 |
| `E:\MX\3rd\include\thorvg` | ThorVG 头文件 |

---

## 四、快速编译

```batch
cd E:\MX\Projects\MXPaper
BUILD-MX-Paper.bat
```

**依赖**：
- VS2022 或 VS2026
- Filament v1.71.6 (位于编译目录的 `../filament-v1.71.6-windows/`)
- ThorVG 静态库 (`E:\MX\3rd\lib\libthorvg_mt.lib`)
- OUI/OApp/OCore 模块 (`E:\MX\MO\`)
- 资源文件 (`E:\MX\Data\app.rc`, `E:\MX\Data\siyuan.ttf`)
- IBL 光照 (`E:\MX\Data\ibl\lightroom_14b`)
- 模型文件 (`E:\MX\Data\models\`)

---

## 五、功能规划（讨论中）

### 核心功能

| # | 功能 | 说明 | 优先级 |
|---|------|------|--------|
| 1 | 场景库浏览 | 扫描 Scenes/ 目录，缩略图网格 | P0 |
| 2 | 场景预览/播放 | 加载场景，渲染 3D 画面，循环播放 | P0 |
| 3 | 设为系统壁纸 | 将当前场景输出到桌面 | P0 |
| 4 | 自动轮播 | 定时切换场景，可配间隔和顺序 | P1 |
| 5 | 播放模式界面 | 场景缩略图网格 + 全屏预览 | P0 |
| 6 | 编辑模式界面 | 场景视口 + 属性面板 + 资源面板 + 脚本区 | P0 |
| 7 | 资源导入 | 拖入图片/视频/音频/glTF 到场景 | P0 |
| 8 | 场景对象操作 | 平移/旋转/缩放物体（Gizmo） | P0 |
| 9 | 属性面板 | 选中物体后编辑属性（位置、材质、纹理等） | P0 |
| 10 | Lua 脚本引擎 | 加载 scene.lua，驱动场景交互/动画逻辑 | P0 |
| 11 | 视频/音频支持 | FFmpeg 解码，视频纹理 + 音频输出 | P1 |
| 12 | ShaderToy 特效 | 全屏 Fragment Shader，支持参数调节 | P1 |
| 13 | 后处理管线 | 辉光/色差/暗角/Bloom 等后处理 Pass | P1 |
| 14 | 粒子系统 | GPU 粒子发射器，可配置发射/生命周期/受力 | P2 |
| 15 | 录屏导出 | 录制场景为 MP4，自动叠加水印 | P2 |
| 16 | 场景打包/分享 | 导出场景目录为 zip | P2 |

### UI 设计原则

- **编辑模式**：Figma 风格 — 3D 场景全窗口渲染，所有面板（资源/属性/工具栏/时间线）为 OUI 浮动叠加层，可拖拽/折叠/关闭
- **播放模式**：全屏无边框窗口 — 分壁纸模式（oapp 设桌面背景层）和窗口模式（普通全屏预览）
- 编辑模式下切换场景视口为全窗口画布，UI 元素渲染在 ThorVG 层

### 编辑模式布局（Figma 风格）

```
┌──────────────────────────────────────────────────────────┐
│ ┌──────────────┐                          ┌───────────┐  │
│ │ 资源  图层   │                          │ 属性       │  │  ← 浮动面板
│ │ 🖼 bg.jpg   │     3D 场景视口           │ 位置 x,y,z │  │
│ │ 🎬 tree.glb │   (全窗口渲染)            │ 旋转       │  │
│ │ 🔊 audio    │                          │ 缩放       │  │
│ │ ✨ particle │                          │ 材质...    │  │
│ └──────────────┘                          └───────────┘  │
│                                                          │
│  ┌─────────────────────────────────────────────────────┐ │
│  │ [导入] [▶播放测试] [保存] [录屏]   场景: Forest      │ │  ← 浮动工具栏
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### 播放模式

```
壁纸模式（桌面背景层）              窗口模式（全屏无边框窗口）
┌─────────────────────┐          ┌──────────────────────────┐
│                     │          │                          │
│   桌面图标          │          │    全屏场景渲染           │
│   ┌───────────┐     │          │    （鼠标移动显示控制条）  │
│   │ 3D 场景   │     │          │                          │
│   │ (壁纸层)  │     │          │  ┌────────────────────┐  │
│   └───────────┘     │          │  │ [←] [▶/⏸] [→] [⚙] │  │  ← 浮动控制条
│                     │          │  └────────────────────┘  │
│  任务栏             │          │                          │
└─────────────────────┘          └──────────────────────────┘
```

### 播放模式控制条（窗口模式下悬停显示）

```
┌─────────────────────────────────────────────┐
│  [← 上一个]  [▶ 播放/暂停]  [→ 下一个]  [编辑] [设置]  │
└─────────────────────────────────────────────┘
```

### 场景描述文件 scene.json（草案）

```json
{
    "name": "Forest",
    "author": "xxx",
    "version": "1.0",
    "camera": {
        "position": [0, 2, 10],
        "target": [0, 0, 0],
        "fov": 45
    },
    "lighting": {
        "ambient": [0.1, 0.1, 0.15],
        "sun": { "direction": [0.5, -1, -0.8], "intensity": 110000 }
    },
    "assets": [
        { "type": "image",  "path": "bg.jpg",    "id": "background" },
        { "type": "model",  "path": "tree.glb",  "id": "tree" },
        { "type": "audio",  "path": "birds.mp3", "id": "ambient" }
    ],
    "nodes": [
        { "id": "bg_quad",   "asset": "background", "position": [0,0,-5], "scale": [10,5,1] },
        { "id": "tree_node", "asset": "tree",       "position": [0,0,0],  "scale": [1,1,1] }
    ],
    "script": "scene.lua"
}
```

### Lua 脚本接口（草案）

```lua
-- scene.lua 生命周期
function onInit(scene)
    -- 场景加载完成
end

function onUpdate(dt)
    -- 每帧调用，dt 为秒
    -- 可访问 scene 对象：操作节点、播放音频、切换动画...
end

function onInput(event)
    -- 鼠标/键盘事件（编辑模式用）
end
```

### 场景资源类型

每个场景目录下必须包含 `data/` 子目录，存放场景所有数据资源。支持的资源类型：

| 类型 | 后缀 | MObjectType | 说明 |
|------|------|-------------|------|
| 图片 | `.jpg` `.jpeg` `.png` `.webp` `.bmp` `.tga` | MImage | 静态纹理（面片展示） |
| GIF 动图 | `.gif` | MImage | GIF 序列帧（面片动画） |
| 视频 | `.mp4` `.webm` `.ogv` `.avi` | MImage | 视频纹理（面片播放） |
| 音频 | `.mp3` `.wav` `.ogg` `.flac` `.aac` | MSound | 场景背景音 / 音频可视化 |
| 3D 模型 | `.glb` `.gltf` | MModel | glTF/Filament 模型 |
| 字体 | `.ttf` `.otf` | MText | 文字渲染 |
| Shader | `.fs` `.vs` | — | ShaderToy 全屏特效 |
| 脚本 | `.lua` | — | 场景交互逻辑 |

### MObject 数据结构

```cpp
struct MObject {
    MObjectType type;      // Camera / Image / Text / Sound / Light / Model
    std::string id;        // 场景内唯一标识
    float3 position;       // 位置
    float3 rotation;       // 旋转（欧拉角）
    float3 scale;          // 缩放
    std::string assetPath; // 资源路径（Image/Model/Sound）
    float4 color;          // 颜色（Image/Text/Light）
    float fov;             // 视场角（Camera）
    bool ortho;            // 正交/透视（Camera）
};
```

所有对象带 RST 矩阵即可使用 Gizmo 进行平移/旋转/缩放操作。

## 六、开发日志

### 2026-06-16

#### 2. 编译: 移植路径适配

- **现象**：BUILD-Filament-GLTF.bat 中的相对路径在新项目目录下无法找到 Filament
- **根因**：原脚本位于 `E:\Dev\KU\ZIP备份\MO\`，相对路径 `../filament-v1.71.6-windows/` 指向 `E:\Dev\KU\ZIP备份\`。移到 `E:\MX\Projects\MXPaper\` 后路径失效
- **解决**：Filament 头文件和库路径改为 `E:\MX\3rd\include\Filament1716\` 和 `E:\MX\3rd\lib\filament\mt\`，编译通过

#### 3. 架构设计: 功能规划与界面方案

- **现象**：需要确定 MXPaper 的完整功能范围和架构设计
- **根因**：壁纸软件不限于静态图，需支持视频/音频/3D 场景，且要双模式（播放+编辑）
- **解决**：
  - 统一 3D 场景管线：所有壁纸类型在 Filament 3D 场景中呈现
  - 场景即壁纸：`Scenes/场景名/` 自包含 `scene.json` + `scene.lua` + 资源
  - 双模式：播放模式（浏览/轮播/设壁纸）+ 编辑模式（导入资源/摆放物体/属性面板/Lua 脚本）
  - 参考 MPlayer 的视频/音频解码方案
  - 13 项核心功能分 P0/P1/P2 三级
  - 场景描述 JSON 草案 + Lua 脚本接口草案
#### 4. 功能补充: ShaderToy + 粒子系统 + 录屏水印

- **现象**：需要支持更多壁纸类型和用户增长功能
- **补充**：
  - **ShaderToy**：全屏 Fragment Shader 特效，参考 `TEST-OGL-SHADERTOY-REC.cpp`（OpenGL 版）和 `TEST-Filament-PostProcess.cpp`（Filament 后处理管线版）
  - **后处理管线**：辉光/色差/暗角/Bloom，使用 Filament RenderTarget + 双 Pass 方案
  - **粒子系统**：GPU 粒子发射器（待开发），可配置发射速率/生命周期/受力/颜色渐变
  - **录屏导出**：FFmpeg 录制场景为 MP4，自动叠加水印（增加分享粘性），参考 `oav.h` 录制模块

#### 5. 原型: HTML5 UI 原型 v1

- **内容**：`prototype/index.html` — 单文件 H5 原型
  - 编辑模式：全窗口 3D 视口 + 浮动面板（资源/图层/属性/Lua 控制台）+ 工具栏 + 模式切换
  - 播放模式：全屏预览 + 浮动控制条（切换/暂停/设置/设壁纸）
  - 场景库：6 张卡片网格（3D/ShaderToy/视频/音频可视化/粒子/城市）
  - 设置面板：轮播/适配/开机/多屏/水印
  - 交互：面板可拖拽、模式切换、壁纸模式切换、ShaderToy 环收缩展开（hover/空格键）、创建几何体（平面/立方/球/圆柱）
  - 风格：深色毛玻璃面板 + Figma 浮动叠加层

#### 11. 踩坑: 纹理 Mipmap 导致部分面黑色

- **现象**：立方体仍有几面没有纹理/显示黑色
- **根因**：`TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR` 需要 mipmap，但纹理创建时 `.levels(1)` 未生成 mipmap。OpenGL 在此情况下对某些面的采样返回黑色（undefined behavior）
- **解决**：改为 `MinFilter::LINEAR`（无 mipmap），对标官方 `texturedquad.cpp` 示例
- **参考**：`E:\Dev\KU\ZIP备份\filament-1.71.6\samples\texturedquad.cpp` — 参数名 `albedo`，shader 中 `materialParams_albedo`，无 mipmap
- **经验**：纹理采样器级别必须与纹理实际 mipmap 层次匹配。无 mipmap 用 `LINEAR`，有 mipmap 才用 `LINEAR_MIPMAP_LINEAR`

#### 10. 踩坑: 立方体 UV 纹理面片朝向 + 材质方案

- **现象**：立方体部分面没有纹理/显示黑色
- **根因 1**：顶点颜色方案 `getColor()` 需 `VertexAttribute::COLOR` 而非参数。在 Filament 1.71 unlit 中，`getColor()` 只取顶点色，不接受 `setParameter("baseColor",...)` 参数传入
- **根因 2**：24 顶点 UV 立方体 winding order 错误。CCW 朝向需从每面外侧往内看计算，一旦算错就被 `culling(true)` 裁剪
- **解决**：
  - 使用 `filamat::MaterialBuilder` 编译 texUnlit 材质，`.parameter("tex", SAMPLER_2D)` + `texture(materialParams_tex, getUV0())`
  - 设 `.culling(false)` 避免 winding 问题
  - 相机距离从 5.0 调到 6.5，确保立方体完整可见
- **经验**：Filament unlit 材质改参数方案时，优先用 `materialParams.xxx` 方式声明 `.parameter()`，或在 shader 中硬编码颜色

#### 12. 功能: Gizmo 创建/选择/平移/缩放 + UI 面板完善

- **日期**：2026-06-17
- **内容**：
  - 工具栏独立 UIButton："盒"/"面"/"球" 创建几何体，"平移"/"旋转"/"缩放" 切换模式
  - 点击视口空白区选中最近物体（距离排序），FPS 栏显示 `[T]#0`
  - Gizmo 拖拽：平移 (XY) / 缩放 (XY→XYZ)
  - ThorVG 十字准星叠加层显示选中物体位置
  - 全文字按钮（无图标），UIFrame 布局问题规避
  - 响应式 resize：viewport + panel rect 重新计算
  - 参数名改为 `albedo` 对标官方 `texturedquad.cpp`
- **已知问题**：Gizmo 仅 2D 平移缩放、无线框高亮、旋转未实现

---

- **决策**：当 `oui.h` 原生控件效果不够炫酷时，创建 `E:\MX\MO\ouiex.h` 单头文件模块
- **方案**：结合 `ocore.h` 的 `OX::Timer`（帧计时）+ `oanime.h` 的缓动/弹簧/时间线/拖拽/交错（31 种缓动曲线、Spring 物理、Timeline、Stagger、Draggable），给 OUI 控件增加动画包装
- **预设**：AnimatedButton、AnimatedPanel、AnimatedToggle、AnimatedSlider、AnimatedProgress、AnimatedList、Modal、Toast 等带动效控件

#### 8. 功能: 首个场景 Hello MX + 基础 3D 渲染

- **内容**：创建 `Scenes/Hello MX/` 场景目录，含 `scene.json` + `scene.lua`
- **场景结构**：蓝色立方体（unlit 材质）+ 深灰地面平面 + 方向光阴影 + 天空盒背景
- **渲染**：使用 `filamat::MaterialBuilder` 动态编译 unlit 材质，为立方体和地面分别创建 `MaterialInstance`
- **动画**：立方体绕 Y 轴自动旋转（`mat4f::rotation`），右键拖拽旋转相机
- **面板**：使用 OUI 的 `UIFrame` + `UIButton`/`UILabel` 拼出浮动面板

- **决策**：exe 同级目录必须有 `Data/`，含 `fonts/`、`bin/`、`config/`、`license/` 子目录
- **自检规则**：启动时检查 — `Data/` 和 `Data/fonts/siyuan.ttf` 缺失→错误退出；`Scenes/`、`Data/config/`、`Data/bin/`、`Data/license/` 缺失→自动创建
- **路径**：使用 `ocore.h` 获取 exe 目录作为基准路径，不再硬编码 `e:/MX/Data/`

---

## 七、已知问题

| # | 问题 | 状态 |
|---|------|------|
| 1 | UIFrame 水平布局（`setHorizontalLayout`）子控件重叠 | 规避：工具栏改用独立 UIButton |
| 2 | Gizmo 仅支持平移/缩放 2D，无旋转、无线框高亮、无轴拖拽 | 待完善 |

---

## 八、待实现功能

**高优先级**

| # | 功能 | 说明 |
|---|------|------|
| 1 | 壁纸素材库管理 | 扫描、浏览、选择壁纸资源 |
| 2 | 壁纸展示渲染 | 3D场景中展示壁纸效果 |
| 3 | 壁纸切换交互 | 上一张/下一张、自动轮播 |

**中优先级**

| # | 功能 | 说明 |
|---|------|------|
| 4 | 壁纸参数调节 | 缩放、位置、光照等 |
| 5 | 设置面板 | UI 参数配置面板 |
