# Mineradio 项目分析

## 项目概览

**Mineradio** 是一款 Windows 桌面沉浸式音乐播放器，基于 Electron 构建，融合天气电台、搜索播放、歌词舞台、粒子视觉预设和 3D 歌单架。

| 项目 | 详情 |
|------|------|
| 当前版本 | `1.1.1`（纯净安装发布版） |
| 技术栈 | Electron 42 + GSAP + Three.js r128 + NeteaseCloudMusicApi |
| 打包工具 | electron-builder 26 (NSIS) |
| 授权协议 | GPL-3.0 |
| GitHub | https://github.com/XxHuberrr/Mineradio |
| 当前源码 | `E:\Dev\KU\Mineradio`（即 `resources\app`） |

---

## 目录结构

```
Mineradio/
├── public/
│   ├── index.html            # 主 UI（26879 行，含 CSS/JS/HTML）
│   ├── vendor/               # 本地依赖 (three.r128.min.js, music-tempo.min.js, gsap.min.js)
│   └── assets/               # 静态资源（图片、字体等）
├── desktop/                  # Electron main/preload 进程
│   ├── main.js               # 主进程入口
│   ├── preload.js            # 预加载脚本
│   └── overlay-preload.js    # 桌面歌词/覆盖层预加载
├── build/                    # 打包资源和安装器
│   ├── installer.nsh         # NSIS 安装器脚本（中文极简风格）
│   ├── after-pack.js         # 打包后处理
│   └── icon.ico              # 应用图标
├── docs/                     # 项目文档
│   ├── PROJECT_MEMORY.md     # 项目记忆（用户偏好、视觉边界、发布记录）
│   ├── GLASS_SVG_TEXTURE.md  # 玻璃质感规范
│   ├── 3D_PLAYLIST_SHELF_MEMORY.md
│   └── ...
├── server.js                 # Node.js 本地 API 服务器
├── dj-analyzer.js            # 音频节奏分析引擎
├── package.json              # 项目配置与依赖
├── CHANGELOG.md              # 更新日志（v0.9.9 ~ v1.1.1）
└── RELEASE.md / SECURITY.md  # 发布与安全说明
```

---

## 核心架构

### 1. 后端服务 (`server.js`)

本地 Node.js HTTP 服务器，端口 `3000`，负责：

- **网易云音乐 API**：搜索、歌曲 URL、扫码登录、Cookie 持久化、歌单/播客/艺人
- **QQ 音乐 API**：搜索、登录态、播放票据验证、音源补充
- **天气电台**：接入 Open-Meteo，根据地理位置和天气 mood 生成播放队列
- **更新系统**：GitHub Releases 检测、多镜像下载、SHA256/SHA512 校验、快速补丁（patch.json）
- **Beatmap 缓存**：节奏分析结果缓存到 `D:\MineradioCache\beatmaps`
- **Cookie 管理**：`.cookie`（网易云）、`.qq-cookie`（QQ 音乐）持久化

```
前端请求 → server.js (port 3000) → 网易云/QQ/Open-Meteo API
                                    ↓
                              Cookie 持久化 / Beatmap 缓存
```

### 2. 音频分析 (`dj-analyzer.js`)

自研本地音频分析引擎，864 行：

- **节拍检测**：BPM、鼓点置信度、kick/snare/onset 识别
- **能量分析**：能量曲线、段落变化、drop 检测、低频比例
- **情绪参数**：energy / aggression / groove / space / brightness / warmth / stability
- **Beatmap 生成**：为可视化系统提供节拍映射（kicks / beats / pulseBeats / cameraBeats）
- **播客/DJ 适配**：针对长音频流分析，支持 `analyzePodcastDjStream` / `analyzePodcastDjIntro`

### 3. 前端 UI (`public/index.html`)

单个巨型 HTML 文件（26879 行），包含：

- CSS 样式（暗色主题、SVG 玻璃质感、响应式布局）
- Three.js 3D 粒子系统
- 歌词舞台渲染
- 3D 歌单架（右侧栏/全屏详情页）
- 视觉预设控制台
- 播放器控制台（底部）
- 搜索栏和歌单列表

### 4. Electron 主进程 (`desktop/`)

- `main.js`：窗口管理、菜单、快捷键、单实例控制、最小化内存优化
- `preload.js`：渲染进程与主进程通信桥接
- `overlay-preload.js`：桌面歌词窗口预加载

---

## 视觉系统

### 视觉预设（Preset）

| 预设 | 说明 |
|------|------|
| **emily** | 默认预设，粒子覆盖 + 封面渐变 |
| **安魂** | 骷髅点云模型，低角度仰视，伦勃朗式明暗 |
| **星河** | 3D 歌单架默认粒子背景 |
| **唱片** | 唱片旋转视觉 |
| **星球** | 星球环绕视觉 |
| **滚筒** | 滚筒式粒子流动 |
| **虚空** | 虚空粒子效果 |

### 粒子系统（Three.js r128）

- WebGL 粒子渲染，基于音频节奏驱动
- 骷髅预设使用预采样点云资产（`skull-decimation-points.bin`）
- 粒子与歌词舞台绑定到同一世界坐标系（`getWorldPosition` / `getWorldQuaternion`）
- 封面粒子分辨率可调节（默认 `1.55`）

### SVG 玻璃质感

- 使用 `<filter>` + `feTurbulence` + `feDisplacementMap` 实现
- 核心参数：`--saved-panel-glass-*`、`--saved-button-glass-*`
- 用户黄金版本，不轻易改动

### 电影镜头系统

- 基于 `dj-analyzer.js` 输出的情绪节奏参数驱动
- 电子歌偏 kick 锁拍，摇滚偏军鼓/段落爆发，阴郁歌偏慢推镜
- 镜头参数：景深、震动强度、粒子呼吸

---

## 3D 歌单架

- **触发方式**：右键菜单 / 呼出按钮
- **内容**：我的歌单、收藏歌单、播客歌单（可关闭）
- **交互**：鼠标滚轮滚动选择、清脆机械齿轮选择音（WebAudio 合成）
- **详情页**：歌曲列表，支持加载更多，跟随封面粒子旋转
- **镜头模式**：动态（跟随相机）/ 静态（跟随粒子）/ 固定
- **常驻模式**：未命中时降低层级不遮挡歌词，命中时浮到前景

---

## 歌词系统

- **舞台歌词**：嵌入主界面，3D 歌单详情页打开时保持可读性
- **桌面歌词**：独立窗口，支持锁定穿透、位置调节、自定义外观
- **歌词预设**：绑定视觉预设，安魂预设嘴部歌词原位置作为原点
- **布局控制**：上下角度、左右角度、景深调节

---

## 更新机制

```
启动检测 → GitHub Releases API / latest.yml
           ↓
     版本比较 → 有新版本 → 展示更新弹窗
           ↓
     下载策略：优先国内镜像 → 失败切换 → 兜底 GitHub 直连
           ↓
     校验：SHA256 / SHA512 digest
           ↓
     应用：快速补丁 (patch.json) 或完整安装包 (.exe)
```

- 快速补丁范围：只为低于新版的最近 4 个版本生成
- 补丁文件限制：12MB 以内，仅允许修改 `public/`、`desktop/`、`build/`、`server.js`、`dj-analyzer.js`、`package.json`、`package-lock.json`
- 国内镜像：gh.llkk.cc、ghfast.top、gh-proxy.com

---

## 依赖分析

### 运行时依赖

| 包名 | 版本 | 用途 |
|------|------|------|
| `gsap` | ^3.15.0 | 动画引擎 |
| `mpg123-decoder` | ^1.0.3 | MP3 解码 |
| `NeteaseCloudMusicApi` | ^4.32.0 | 网易云音乐 API 封装 |

### 开发依赖

| 包名 | 版本 | 用途 |
|------|------|------|
| `electron` | ^42.4.1 | 桌面应用框架 |
| `electron-builder` | ^26.15.3 | 打包工具 |
| `rcedit` | ^5.0.2 | Windows 可执行文件资源编辑 |

---

## 发布历史要点

| 版本 | 关键更新 |
|------|----------|
| **v1.1.1** | 安装器安全修复：默认优先 D 盘，卸载不误删，旧安装器跳过 |
| **v1.1.0** | 纯净安装重建，默认视觉快照「默认测试」，3D 歌单架内容开关，高级性能设置 |
| **v1.0.10** | 桌面歌词视觉重做，方向键音量，更新包规则调整 |
| **v1.0.9** | 安装器中文界面，允许自选安装目录，单实例启动 |
| **v1.0.8** | QQ 播放授权修复，用户存档，播放淡入淡出 |
| **v1.0.0** | 首个正式版，天气电台，首页布局，登录态修复 |
| **v0.9.13** | WebGL 光流线开场动画 |

---

## 关键设计决策

1. **单文件前端**：`index.html` 承载所有 UI 逻辑，改动需精确定位，避免大块重写
2. **本地 API 代理**：前端不直接调网易云/QQ API，统一经 `server.js` 代理，便于 Cookie 管理和鉴权
3. **自研节奏分析**：不依赖外部音效 API，本地计算 BPM/能量/情绪参数驱动视觉
4. **快速补丁机制**：跨版本更新支持轻量 patch.json，减少下载量
5. **安装器安全**：默认 D 盘安装，不递归删除，旧安装隔离
6. **视觉参数存档**：用户可保存预设，首次启动内置「默认测试」默认快照

---

## 敏感区域与已知风险

| 区域 | 风险 | 备注 |
|------|------|------|
| `public/index.html` | 26879 行巨型文件 | 改动需 grep 定位，避免全局重写 |
| 播放/暂停按钮 | 多次失效历史 | 天气电台、歌单加载后状态同步 |
| Emily 视觉入场 | 曾卡顿跳帧 | 优化动画过渡 |
| 3D 歌单架 | 曾强制切星河/详情页遮挡 | 详情页/镜头/层级已修复 |
| 左侧歌单页 | 曾全量加载 CPU 高 | 需虚拟化/分批渲染 |
| Ctrl 缩放 | 曾卡住无法恢复 | 已临时修复，待正式修复 |
| 旧安装包 (≤v1.0.10) | 不安全卸载器 | 建议隔离，不再传播 |

---

## 命令速查

```powershell
# 开发运行
npm start

# 语法检查
node --check server.js
node --check dj-analyzer.js

# 发布打包
npm run build:win          # NSIS 安装包
npm run build:win:dir      # 目录分发

# Git
git diff --check           # 空白检查
git push                   # 推送源码
```

---

## ThorVG 双 FBO 渲染 Splash 启动页

### 架构总览

```
┌─────────────────────────────────────────────────────┐
│                   主渲染循环                          │
├─────────────────────────────────────────────────────┤
│                                                     │
│  FBO_A (tvgFbo)        FBO_B (glFbo)               │
│  ┌──────────┐          ┌──────────┐                │
│  │ ThorVG   │          │ GLSL     │                │
│  │ 绘制:    │          │ 绘制:    │                │
│  │ • 网格线  │          │ • WebGL  │                │
│  │ • 噪声   │          │   loop   │                │
│  │ • 暗角   │          │ • 扫描线 │                │
│  │ • 混合   │          │ • 暗角   │                │
│  └────┬─────┘          └────┬─────┘                │
│       │                     │                      │
│       ▼                     ▼                      │
│  ┌──────────────────────────────────┐              │
│  │   最终合成: 两个 FBO 纹理 → screen │              │
│  └──────────────────────────────────┘              │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 1. FBO 创建

```cpp
struct Framebuffer {
    GLuint fbo = 0, texture = 0, rbo = 0;

    void create(uint32_t w, uint32_t h) {
        // 颜色附件: BGRA8 纹理
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 深度附件
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

        // 绑定 FBO
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
    void unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    ~Framebuffer() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (texture) glDeleteTextures(1, &texture);
        if (rbo) glDeleteRenderbuffers(1, &rbo);
    }
};
```

### 2. GLSL Splash 着色器 (对应 index.html:25573-25673)

**顶点着色器:**

```glsl
#version 140
in vec2 aPosition;
out vec2 vUv;
void main(){
    vUv = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
```

**片段着色器 (精简自 index.html:25573-25673，保留核心 animatedLoop):**

```glsl
#version 140
precision highp float;
in vec2 vUv;
out vec4 fragColor;
uniform vec2 uResolution;
uniform float uTime;

float saturate(float v){ return clamp(v, 0.0, 1.0); }
float ease(float v){ v = saturate(v); return v * v * (3.0 - 2.0 * v); }
mat2 rot(float a){ float c = cos(a); float s = sin(a); return mat2(c, -s, s, c); }
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123); }
float noise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0,0.0)), u.x),
               mix(hash(i + vec2(0.0,1.0)), hash(i + vec2(1.0,1.0)), u.x), u.y);
}

float animatedLoop(vec2 uv, float t, float channel){
    vec2 q = uv;
    q *= rot(0.28 + sin(t * 0.18) * 0.12);
    q.x += 0.055 * sin(t * 0.30 + channel);
    q.y += 0.040 * cos(t * 0.24 + channel * 1.7);
    float ang = atan(q.y, q.x);
    float angularShift = sin(ang * 3.0 + t * 0.72 + channel * 1.9) * 0.078;
    angularShift += sin(ang * 7.0 - t * 0.54 + channel) * 0.020;
    float neonD = length(q) + angularShift;
    float warpD = length(q * vec2(1.34 + 0.06 * sin(t * 0.25), 0.82 + 0.04 * cos(t * 0.31)));
    warpD += 0.026 * sin(q.x * 4.4 + t * 0.62) + 0.018 * sin(q.y * 5.2 - t * 0.45);
    float diamondD = abs(q.x) * 1.20 + abs(q.y) * 0.84;
    float d = mix(warpD, diamondD, 0.32);
    d = mix(d, neonD, 0.20 + 0.04 * sin(t * 0.18 + channel));
    float pattern = mod((q.x + q.y) * 0.62 + sin(q.x * 5.5 + t) * 0.015 + sin(q.y * 7.0 - t * 0.75) * 0.012, 0.20);
    float acc = 0.0;
    for (int i = 1; i <= 6; i++){
        float fi = float(i);
        float f = fract(t * 0.152 - channel * 0.018 + 0.011 * fi) * 4.70 - d + pattern;
        acc += 0.00110 * fi * fi / max(abs(f), 0.0065);
    }
    float threadCoord = q.x * 0.92 - q.y * 0.58 + 0.030 * sin(q.x * 5.2 + t * 0.72);
    float threadLines = 0.0065 / max(abs(sin((threadCoord + t * 0.10 + channel * 0.035) * 27.0)), 0.070);
    acc += threadLines * (0.50 + 0.30 * sin(ang * 1.2 + t + channel));
    return min(acc, 1.95);
}

void main(){
    vec2 p = vUv * 2.0 - 1.0;
    p.x *= uResolution.x / max(uResolution.y, 1.0);
    float t = uTime;
    float intro = ease(t / 0.72);
    float bloomIn = ease((t - 0.10) / 1.10);
    float climax = exp(-pow((t - 3.62) / 0.58, 2.0));
    float preClimax = ease((t - 2.15) / 1.25) * (1.0 - ease((t - 3.86) / 0.72));
    float afterglow = exp(-pow((t - 4.14) / 0.62, 2.0));
    float calm = 1.0 - 0.22 * ease((t - 4.75) / 0.70);
    float settle = 1.0 - 0.34 * ease((t - 5.05) / 0.52);

    vec2 uv = p * (0.98 + 0.05 * sin(t * 0.25));
    uv += vec2(0.0, -0.025);

    vec2 flowAxis = normalize(vec2(0.86, -0.50));
    vec2 crossAxis = vec2(-flowAxis.y, flowAxis.x);
    float lane = dot(p, flowAxis);
    float crossLane = dot(p, crossAxis);
    float syncWave = sin(crossLane * 5.4 + lane * 1.1 - t * 1.85);
    uv += flowAxis * syncWave * 0.055 * climax;
    uv += crossAxis * sin(lane * 7.2 + t * 1.25) * 0.034 * climax;
    uv *= 1.0 + 0.045 * preClimax - 0.020 * climax;

    vec3 ch1 = vec3(1.00, 0.13, 0.31);
    vec3 ch2 = vec3(0.16, 1.00, 0.86);
    vec3 ch3 = vec3(1.00, 0.76, 0.28);

    float a = animatedLoop(uv, t, 0.0);
    float b = animatedLoop(uv * 1.018 + vec2(0.012, -0.008), t + 0.18, 1.0);
    float c = animatedLoop(uv * 0.986 + vec2(-0.010, 0.010), t + 0.35, 2.0);
    vec3 loopCol = ch1 * a + ch2 * b + ch3 * c;

    float tunnel = animatedLoop(uv * 1.42 + vec2(sin(t * 0.2) * 0.08, cos(t * 0.17) * 0.05), t * 1.12 + 1.7, 2.7);
    loopCol += mix(ch2, ch3, 0.35 + 0.25 * sin(t)) * tunnel * (0.30 + 0.24 * preClimax);

    float syncBand = exp(-pow((lane + 0.08 * sin(t * 0.72)) / 0.62, 2.0));
    float phaseThread = pow(0.5 + 0.5 * sin(crossLane * 13.5 + lane * 2.2 - t * 3.1), 8.0);
    float phaseThread2 = pow(0.5 + 0.5 * sin(crossLane * 9.0 - lane * 5.4 + t * 2.4), 10.0);
    vec3 climaxCol = (mix(ch2, ch3, 0.36) * phaseThread + ch1 * phaseThread2 * 0.52) * syncBand * climax;
    float afterBand = exp(-pow((lane - 0.34) / 0.72, 2.0));
    climaxCol += mix(ch1, ch2, vUv.x) * afterBand * afterglow * 0.13;

    float centerBeam = exp(-abs(p.y + 0.005 * sin(t * 3.0)) * 24.0) * (0.14 + 0.52 * exp(-pow((t - 0.74) / 0.34, 2.0)));
    float bladeMask = smoothstep(-1.55, -0.08, p.x) * (1.0 - smoothstep(0.08, 1.55, p.x));
    vec3 blade = mix(ch1, ch2, vUv.x) * centerBeam * bladeMask * (0.40 + 0.28 * climax);
    float flare = exp(-dot(p, p) * 3.6) * exp(-pow((t - 0.88) / 0.40, 2.0));

    vec3 col = vec3(0.002, 0.004, 0.005);
    col += loopCol * (0.56 + 0.46 * bloomIn) * calm * settle;
    col += climaxCol * 0.22;
    float diagonalGlint = exp(-pow(lane * 1.2 + crossLane * 0.10, 2.0) / 0.030) * climax;
    col += blade + vec3(1.0, 0.78, 0.42) * flare * 0.18 + vec3(1.0, 0.86, 0.58) * diagonalGlint * 0.07;

    float scan = 0.92 + 0.08 * sin((vUv.y * uResolution.y + t * 52.0) * 0.72);
    float grain = noise(vUv * uResolution.xy * 0.52 + t * 17.0) - 0.5;
    col *= scan;
    col += grain * 0.018;
    col *= intro;
    col = max(col - vec3(0.010, 0.012, 0.012), 0.0);
    col = vec3(1.0) - exp(-max(col, 0.0) * (0.62 + 0.18 * climax));

    float vignette = smoothstep(1.52, 0.20, length(p * vec2(0.78, 1.04)));
    col *= 0.38 + 0.86 * vignette;
    col += vec3(0.020, 0.010, 0.014) * (1.0 - vignette);

    fragColor = vec4(col, 1.0);
}
```

### 3. 全屏 Quad 着色器 (用于 FBO → screen 合成)

```glsl
// 顶点
#version 140
in vec2 aPosition;
out vec2 vUv;
void main(){
    vUv = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}

// 片段
#version 140
precision highp float;
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform int uBlend;  // 0=replace, 1=screen, 2=mul, 3=overlay
void main(){
    vec4 texCol = texture(uTex, vUv);
    if (uBlend == 1) {
        vec4 bgCol = texCol;
        fragColor = vec4(1.0) - (1.0 - bgCol) * (1.0 - bgCol);
    } else {
        fragColor = texCol;
    }
}
```

### 4. ThorVG Splash 层设置

```cpp
void setupThorvgSplash(tvg::GlCanvas* tvgCanvas, float W, float H) {
    // 网格线 - 横向 (90deg repeating linear gradient)
    {
        auto shape = tvg::Shape::gen();
        shape->appendRect(0, 0, W, H);

        auto grad = tvg::LinearGradient::gen();
        grad->linear(0, 0, 1, 0);  // 从左到右

        tvg::Fill::ColorStop stops[4];
        stops[0] = {0.0f/54.0f, 255, 255, 255, 8};
        stops[1] = {1.0f/54.0f, 255, 255, 255, 8};
        stops[2] = {1.0f/54.0f, 255, 255, 255, 0};
        stops[3] = {54.0f/54.0f, 255, 255, 255, 0};
        grad->colorStops(stops, 4);
        grad->spread(tvg::FillSpread::Repeat);
        shape->fill(grad);
        shape->blend(tvg::BlendMethod::Screen);
        tvgCanvas->push(shape);
    }

    // 网格线 - 纵向 (0deg repeating linear gradient)
    {
        auto shape = tvg::Shape::gen();
        shape->appendRect(0, 0, W, H);

        auto grad = tvg::LinearGradient::gen();
        grad->linear(0, 0, 0, 1);  // 从上到下

        tvg::Fill::ColorStop stops[4];
        stops[0] = {0.0f/46.0f, 255, 255, 255, 5};
        stops[1] = {1.0f/46.0f, 255, 255, 255, 5};
        stops[2] = {1.0f/46.0f, 255, 255, 255, 0};
        stops[3] = {46.0f/46.0f, 255, 255, 255, 0};
        grad->colorStops(stops, 4);
        grad->spread(tvg::FillSpread::Repeat);
        shape->fill(grad);
        shape->blend(tvg::BlendMethod::Screen);
        tvgCanvas->push(shape);
    }

    // 噪声层 (程序化噪声纹理)
    {
        constexpr int N = 180;
        uint32_t noisePixels[N * N];
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                float val = 0.5f + 0.5f * (
                    sin(x * 0.3f + y * 0.1f) * 0.5f +
                    sin(x * 0.7f - y * 0.5f) * 0.3f +
                    sin((x + y) * 0.15f) * 0.2f
                );
                uint8_t gray = (uint8_t)(val * 255.0f);
                noisePixels[y * N + x] =
                    (gray << 16) | (gray << 8) | gray | (0xFF << 24);
            }
        }

        auto pic = tvg::Picture::gen();
        pic->load(noisePixels, N, N, tvg::ColorSpace::RGBA8888, true);
        pic->size(W, H);
        pic->blend(tvg::BlendMethod::Screen);
        pic->opacity(0x0A);  // ≈ 0.038 * 255
        tvgCanvas->push(pic);
    }

    // 暗角 - 水平方向
    {
        auto shape = tvg::Shape::gen();
        shape->appendRect(0, 0, W, H);

        auto grad = tvg::LinearGradient::gen();
        grad->linear(0, 0, W, 0);

        tvg::Fill::ColorStop stops[4];
        stops[0]  = {0.0f, 0, 0, 0, 209};    // rgba(0,0,0,.82)
        stops[1]  = {0.21f, 0, 0, 0, 0};
        stops[2]  = {0.79f, 0, 0, 0, 0};
        stops[3]  = {1.0f, 0, 0, 0, 209};    // rgba(0,0,0,.82)
        grad->colorStops(stops, 4);
        shape->fill(grad);
        shape->blend(tvg::BlendMethod::Screen);
        tvgCanvas->push(shape);
    }

    // 暗角 - 垂直方向
    {
        auto shape = tvg::Shape::gen();
        shape->appendRect(0, 0, W, H);

        auto grad = tvg::LinearGradient::gen();
        grad->linear(0, 0, 0, H);

        tvg::Fill::ColorStop stops[4];
        stops[0]  = {0.0f, 0, 0, 0, 173};    // rgba(0,0,0,.68)
        stops[1]  = {0.32f, 0, 0, 0, 0};
        stops[2]  = {0.64f, 0, 0, 0, 0};
        stops[3]  = {1.0f, 0, 0, 0, 189};    // rgba(0,0,0,.74)
        grad->colorStops(stops, 4);
        shape->fill(grad);
        shape->blend(tvg::BlendMethod::Screen);
        tvgCanvas->push(shape);
    }

    tvgCanvas->sync();
}
```

### 5. 主渲染循环

```cpp
void renderSplashLoop(Framebuffer& tvgFbo, Framebuffer& glFbo,
                      GlslProgram& glslProg, tvg::GlCanvas* tvgCanvas,
                      uint32_t W, uint32_t H, float elapsed)
{
    // 步骤 1: ThorVG 绘制到 tvgFbo
    tvgFbo.bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_STENCIL_BUFFER_BIT);
    glViewport(0, 0, W, H);

    SDL_GLContext glCtx = /* 你的 SDL_GLContext */;
    static_cast<tvg::GlCanvas*>(tvgCanvas)->target(glCtx, tvgFbo.fbo, W, H, tvg::ColorSpace::ABGR8888S);

    setupThorvgSplash(tvgCanvas, W, H);  // 首次调用
    tvgCanvas->draw();
    tvgCanvas->sync();

    // 步骤 2: GLSL 绘制到 glFbo
    glFbo.bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_STENCIL_BUFFER_BIT);
    glViewport(0, 0, W, H);

    glslProg.use();
    glUniform2f(glslProg.uResolutionLoc, (float)W, (float)H);
    glUniform1f(glslProg.uTimeLoc, elapsed);

    GLfloat quad[6] = {-1, -1,  1, -1,  -1, 1};
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);

    glFlush();

    // 步骤 3: 合成两个 FBO → screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, glFbo.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tvgFbo.texture);

    // 使用 composite shader 做 screen blend
    // ... (见下方 composite shader)

    glDisable(GL_BLEND);
    glFlush();
}
```

### 6. 合成 Shader

```glsl
#version 140
precision highp float;
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uGlslTex;   // glFbo 的输出 (GLSL splash)
uniform sampler2D uTvgTex;    // tvgFbo 的输出 (网格/噪声/暗角)
void main(){
    vec4 glslCol = texture(uGlslTex, vUv);
    vec4 tvgCol  = texture(uTvgTex, vUv);

    // Screen blend: result = 1 - (1-a)*(1-b)
    vec3 result = 1.0 - (1.0 - glslCol.rgb) * (1.0 - tvgCol.rgb);

    fragColor = vec4(result, 1.0);
}
```

### 7. 合成顺序 (对应 CSS z-index)

| z-index | 内容 | FBO | 合成角色 |
|---------|------|-----|----------|
| 底层 | 背景渐变 + 网格 | `tvgFbo` | **叠加层** (screen blend) |
| 中层 | GLSL animated loop | `glFbo` | **基底层** |
| 上层 | 暗角 + 噪声 | `tvgFbo` | **叠加层** (screen blend) |

**关键**: ThorVG 的 `blend(tvg::BlendMethod::Screen)` 在 `tvgFbo` 内部已做 screen 混合。最终合成只需将 `tvgFbo` 作为纹理以 screen mode 叠加到 `glFbo` 输出上。

### 8. 性能要点

1. **ThorVG 只需 draw+sync 一次**: `setupThorvgSplash()` 中的 shapes 创建只做一次，每帧只调 `canvas->sync()`。
2. **噪声纹理一次性上传**: `noisePixels[180*180]` 在初始化时生成，`tvg::Picture::load()` 上传后复用。
3. **FBO 复用**: `tvgFbo` 和 `glFbo` 每帧 `bind→clear→render→unbind`，不销毁重建。
4. **合成只用一次 blit + 一次 texture sample**: 如果 ThorVG 层的 screen blend 已在 FBO 内完成，最终合成可以简化为 `glFbo → screen` (glFbo 输出已包含 GLSL + tvgFbo 的 screen 叠加)。

---

## ThorVG + oanime 复刻品牌字样动画

### 1. Mineradio CSS 动画分析

**HTML 结构** (index.html:1889-1897):

```html
<div class="splash-content">
  <div class="splash-wordmark" id="splash-wordmark" aria-label="Mineradio">
    <span class="splash-word-mine">Mine</span>
    <span class="splash-word-radio" aria-label="radio">
      rad<span class="splash-word-i" aria-hidden="true"></span>
      <span class="splash-word-o">o</span>
    </span>
  </div>
  <div class="splash-signal-line"></div>
  <div class="splash-sub">private visual radio</div>
  <div class="splash-enter" aria-hidden="true">点击进入</div>
</div>
```

**CSS 动画关键帧** (index.html:138-144):

```css
/* Mine: 从中心偏左出现，滑到左侧最终位置 */
@keyframes splash-mine-in {
  0%:  opacity:0; clip-path:inset(48% 0 49% 0);
      transform:translate(calc(-50% - 10px),-42%) skewX(-10deg) scaleX(1.08); letter-spacing:.055em
  14%: opacity:.92; clip-path:inset(40% 0 42% 0);
       transform:translate(calc(-50% - 4px),-50%) skewX(-4deg) scaleX(1.04); letter-spacing:.014em
  26%: opacity:1; clip-path:inset(0);
       transform:translate(-50%,-50%) skewX(0) scaleX(1); letter-spacing:-.040em
  48%: opacity:1; transform:translate(-50%,-50%) scale(1)
  67%: opacity:1; transform:translate(calc(-50% - clamp(66px,10.8vw,130px)),-50%) scale(.998); letter-spacing:-.055em
  100%:opacity:1; transform:translate(calc(-50% - clamp(66px,10.8vw,130px)),-50%) scale(.998)
}

/* Radio: 从中心偏右出现，滑到右侧最终位置 + 渐变背景扫过 */
@keyframes splash-radio-in {
  0%,32%: opacity:0; clip-path:inset(52% 0 44% 0);
          transform:translate(calc(-50% + clamp(78px,12vw,142px)),-50%) skewX(9deg) scaleX(1.06); background-position:0 0
  48%:    opacity:.88; clip-path:inset(34% 0 32% 0);
           transform:translate(calc(-50% + clamp(72px,11.5vw,138px)),-50%) skewX(3deg) scaleX(1.02); background-position:52% 0
  66%:    opacity:1; clip-path:inset(0);
           transform:translate(calc(-50% + clamp(70px,11.4vw,136px)),-50%) scale(1); background-position:76% 0
  100%:   opacity:1; transform:translate(calc(-50% + clamp(70px,11.4vw,136px)),-50%) scale(1); background-position:100% 0
}

/* i 字母圆点弹入 */
@keyframes splash-i-dot-pop {
  0%,48%: opacity:0; transform:translate(-50%,.22em) scale(.34)
  60%:    opacity:1; transform:translate(-50%,-.018em) scale(1.12)
  68%:    opacity:.92; transform:translate(-50%,.010em) scale(.94)
  76%,100%:opacity:1; transform:translate(-50%,0) scale(1)
}

/* 信号线 */
@keyframes splash-signal-line {
  0%,28%: opacity:0; transform:scaleX(.10)
  44%:    opacity:.98; transform:scaleX(1.05)
  64%:    opacity:.70; transform:scaleX(.82)
  76%:    opacity:1; transform:scaleX(1.14); box-shadow:...
  100%:   opacity:.30; transform:scaleX(.64)
}

/* 信号亮点 */
@keyframes splash-signal-blip {
  0%,42%: opacity:0; left:18%; transform:translate(-50%,-50%) scale(.24)
  62%:    opacity:.94; left:50%; transform:translate(-50%,-50%) scale(1)
  76%:    opacity:1; left:50%; transform:translate(-50%,-50%) scale(1.45)
  100%:   opacity:.16; left:82%; transform:translate(-50%,-50%) scale(.46)
}

/* 副标题 + 点击进入 */
@keyframes splash-sub-in {
  0%,38%: opacity:0; transform:translateY(7px)
  56%:    opacity:.58; transform:translateY(0)
  100%:   opacity:.42; transform:translateY(0)
}
```

**统一缓动**: 所有关键帧使用 `cubic-bezier(.22,1,.36,1)` → 对应 `easeOutCubic` 或 `Easing::cubicBezier(.22,1,.36,1)`。

### 2. 动画时间线

| 元素 | 开始时间 | 动画类型 | 持续时间 |
|------|----------|----------|----------|
| Mine 出现 | 0ms | clip-path 展开 + 滑入 + skewX 回正 | ~3000ms |
| Mine 滑到最终位置 | ~2000ms | translateX 左移 | ~1200ms |
| radio 出现 | 32% (≈1600ms) | clip-path 展开 + 滑入 + skewX 回正 | ~1600ms |
| radio 渐变扫过 | 同步 | background-position 0→100% | 全周期 |
| i 圆点弹入 | 48% (≈2400ms) | scale + opacity 弹跳 | ~600ms |
| 信号线出现 | 28% (≈1400ms) | scaleX + opacity | ~1500ms |
| 信号亮点移动 | 42% (≈2100ms) | 左→右 + scale | ~1000ms |
| 副标题 | 38% (≈1900ms) | translateY + opacity | ~1500ms |
| 点击进入 | 就绪后 | opacity + pulse | 无限循环 |

总周期约 5200ms (5.2 秒)。

### 3. ThorVG + oanime::Timeline + OX::Timer 实现

**核心思路**:
- 每个文字元素作为 `tvg::Shape`，用 clipPath 控制可见区域
- 用 `OX::Anime::Timeline` 编排各元素动画，精确控制延迟/重叠
- 用 `OX::Timer::Delta` 模式驱动主循环，每帧调用 `timeline.update(dt)`
- 渐变文字背景用 `tvg::LinearGradient` + `tvg::Fill`，动画化渐变位置

```cpp
#include "ocore.h"
#include "oanime.h"
#include <thorvg.h>

using namespace OX;
using namespace OX::Anime;

// ============================================================
// 品牌字样 Splash Wordmark 动画
// ============================================================

struct BrandSplash {
    tvg::GlCanvas* canvas = nullptr;
    uint32_t W = 0, H = 0;
    float wordHeight = 0;

    // 各元素形状
    tvg::Shape* mineShape = nullptr;
    tvg::Shape* radioShape = nullptr;
    tvg::Shape* iDotShape = nullptr;
    tvg::Shape* signalLineShape = nullptr;
    tvg::Shape* signalBlipShape = nullptr;
    tvg::Shape* subShape = nullptr;
    tvg::Shape* enterShape = nullptr;

    // clip-path 矩形 (用 Shape 模拟)
    tvg::Shape* mineClipRect = nullptr;
    tvg::Shape* radioClipRect = nullptr;

    // 动画时间线
    Timeline timeline;
    std::shared_ptr<Timer> timer;

    // radio 渐变
    tvg::LinearGradient* radioGrad = nullptr;

    // 缓动: cubic-bezier(.22, 1, .36, 1)
    Easing::EasingFunc easeOut = Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f);

    // Mine 最终偏移 (clamp(66px, 10.8vw, 130px))
    float mineFinalOffsetX = 0;
    // radio 最终偏移 (clamp(70px, 11.4vw, 136px))
    float radioFinalOffsetX = 0;

    BrandSplash() {
        mineFinalOffsetX = clampf(W * 0.108f, 66.0f, 130.0f);
        radioFinalOffsetX = clampf(W * 0.114f, 70.0f, 136.0f);
    }

    void init(tvg::GlCanvas* cv, uint32_t w, uint32_t h) {
        canvas = cv;
        W = w;
        H = h;
        wordHeight = clampf(h * 0.12f, 70.0f, 136.0f);
        mineFinalOffsetX = clampf(w * 0.108f, 66.0f, 130.0f);
        radioFinalOffsetX = clampf(w * 0.114f, 70.0f, 136.0f);

        buildShapes();
        buildTimeline();
    }

    void buildShapes() {
        // --- "Mine" 文字 ---
        mineShape = tvg::Shape::gen();
        mineShape->appendText("Mine", 0, wordHeight * 0.85f);  // 简化: 用矩形代替文字
        mineShape->fill(0xF8, 0xF8, 0xF2, 255);               // #f8f8f2
        mineShape->opacity(0);                                  // 初始隐藏

        // Mine clip-path: inset(top right bottom left)
        mineClipRect = tvg::Shape::gen();
        mineClipRect->appendRect(0, 0, W, H);
        mineShape->clippath(mineClipRect);

        // --- "radio" 文字 ---
        radioShape = tvg::Shape::gen();
        radioShape->appendText("radio", 0, wordHeight * 0.85f);
        // radio 使用渐变填充 (background: linear-gradient)
        radioGrad = tvg::LinearGradient::gen();
        radioGrad->linear(0, 0, W * 3.0f, 0);  // 300% 背景宽
        {
            tvg::Fill::ColorStop stops[5];
            stops[0] = {0.00f, 255, 255, 255, 15};  // rgba(255,255,255,.06)
            stops[1] = {0.26f, 255, 255, 255, 255};  // #fff
            stops[2] = {0.48f, 244, 210, 138, 250};  // rgba(244,210,138,.98)
            stops[3] = {0.68f, 122, 215, 194, 229};  // rgba(122,215,194,.90)
            stops[4] = {1.00f, 255, 255, 255, 209};  // rgba(255,255,255,.82)
            radioGrad->colorStops(stops, 5);
        }
        radioShape->fill(radioGrad);

        // radio clip-path
        radioClipRect = tvg::Shape::gen();
        radioClipRect->appendRect(0, 0, W, H);
        radioShape->clippath(radioClipRect);

        // --- "i" 圆点 (splash-word-i) ---
        iDotShape = tvg::Shape::gen();
        iDotShape->appendCircle(0, 0, wordHeight * 0.07f, wordHeight * 0.07f);  // 小圆
        iDotShape->fill(0xFF, 0xFF, 0xFF, 0);
        iDotShape->opacity(0);

        // --- 信号线 ---
        signalLineShape = tvg::Shape::gen();
        float lineW = clampf(w * 0.54f, 460.0f, w);  // min(460px, 54vw)
        signalLineShape->appendRect(0, wordHeight * 0.85f + wordHeight, lineW, 2);
        signalLineShape->fill(0xFF, 0xFF, 0xFF, 0);
        signalLineShape->opacity(0);

        // 信号线渐变
        {
            auto lineGrad = tvg::LinearGradient::gen();
            lineGrad->linear(0, 0, lineW, 0);
            tvg::Fill::ColorStop stops[5];
            stops[0] = {0.0f, 122, 215, 194, 0};
            stops[1] = {0.35f, 122, 215, 194, 56};
            stops[2] = {0.50f, 255, 255, 255, 200};
            stops[3] = {0.65f, 244, 210, 138, 168};
            stops[4] = {1.0f, 255, 83, 103, 0};
            lineGrad->colorStops(stops, 5);
            signalLineShape->fill(lineGrad);
        }

        // --- 信号亮点 ---
        signalBlipShape = tvg::Shape::gen();
        signalBlipShape->appendCircle(0, wordHeight * 0.85f + wordHeight, 4, 4);
        signalBlipShape->fill(0xFF, 0xFF, 0xFF, 0);
        signalBlipShape->opacity(0);

        // --- 副标题 ---
        subShape = tvg::Shape::gen();
        subShape->appendText("PRIVATE VISUAL RADIO", 0, wordHeight * 0.85f + wordHeight * 2.0f);
        subShape->fill(0xFF, 0xFF, 0xFF, 86);  // rgba(255,255,255,.34)
        subShape->opacity(0);

        // --- 点击进入 ---
        enterShape = tvg::Shape::gen();
        enterShape->appendText("点击进入", 0, wordHeight * 0.85f + wordHeight * 3.0f);
        enterShape->fill(0xFF, 0xFF, 0xFF, 158);  // rgba(255,255,255,.62)
        enterShape->opacity(0);

        // 推入 canvas
        canvas->push(mineShape);
        canvas->push(radioShape);
        canvas->push(iDotShape);
        canvas->push(signalLineShape);
        canvas->push(signalBlipShape);
        canvas->push(subShape);
        canvas->push(enterShape);

        // 初始 transform: 所有元素居中
        mineShape->translate(W * 0.5f, H * 0.5f);
        radioShape->translate(W * 0.5f, H * 0.5f);
        iDotShape->translate(W * 0.5f, H * 0.5f);
        signalLineShape->translate(W * 0.5f, H * 0.5f);
        signalBlipShape->translate(W * 0.5f, H * 0.5f);
        subShape->translate(W * 0.5f, H * 0.5f);
        enterShape->translate(W * 0.5f, H * 0.5f);
    }

    // ========================================================
    // 构建 Timeline (基于 CSS 关键帧参数)
    // ========================================================
    void buildTimeline() {
        // --- Mine 动画 ---
        // CSS: 0%→26% 从 clip-path 展开 + 滑入, 26%→67% 滑到最终位置
        // 总时长 5200ms (与 CSS 一致)
        auto mineAnim = Anime::target(mineShape)
            .prop(PropType::Opacity, 0.0f, 1.0f)
            .duration(5200.0f)
            .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));
        mineAnim.play();
        mineAnim.get()->onUpdate = [this]() {
            applyMineAnim(mineAnim.get()->progress);
        };
        timeline.add(mineAnim.get());

        // --- Radio 动画 (32% 延迟) ---
        auto radioAnim = Anime::target(radioShape)
            .prop(PropType::Opacity, 0.0f, 1.0f)
            .duration(5200.0f)
            .delay(1664.0f)  // 5200 * 0.32 = 1664ms
            .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));
        radioAnim.play();
        radioAnim.get()->onUpdate = [this]() {
            applyRadioAnim(radioAnim.get()->progress);
        };
        timeline.add(radioAnim.get());

        // --- i 圆点弹入 (48% 延迟) ---
        auto iDotAnim = Anime::target(iDotShape)
            .prop(PropType::Opacity, 0.0f, 255.0f)
            .duration(600.0f)
            .delay(2496.0f)  // 5200 * 0.48 = 2496ms
            .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));
        iDotAnim.play();
        iDotAnim.get()->onUpdate = [this]() {
            applyIDotAnim(iDotAnim.get()->progress);
        };
        timeline.add(iDotAnim.get());

        // --- 信号线 (28% 延迟) ---
        auto lineAnim = Anime::target(signalLineShape)
            .prop(PropType::Opacity, 0.0f, 255.0f)
            .duration(1500.0f)
            .delay(1456.0f)  // 5200 * 0.28 = 1456ms
            .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));
        lineAnim.play();
        lineAnim.get()->onUpdate = [this]() {
            applySignalLineAnim(lineAnim.get()->progress);
        };
        timeline.add(lineAnim.get());

        // --- 信号亮点 (42% 延迟) ---
        auto blipAnim = Anime::target(signalBlipShape)
            .prop(PropType::Opacity, 0.0f, 255.0f)
            .duration(1000.0f)
            .delay(2184.0f)  // 5200 * 0.42 = 2184ms
            .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));
        blipAnim.play();
        blipAnim.get()->onUpdate = [this]() {
            applySignalBlipAnim(blipAnim.get()->progress);
        };
        timeline.add(blipAnim.get());

        // --- 副标题 (38% 延迟) ---
        auto subAnim = Anime::target(subShape)
            .prop(PropType::Opacity, 0.0f, 106.0f)  // 最终 opacity 0.42
            .duration(1500.0f)
            .delay(1976.0f)  // 5200 * 0.38 = 1976ms
            .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));
        subAnim.play();
        subAnim.get()->onUpdate = [this]() {
            applySubAnim(subAnim.get()->progress);
        };
        timeline.add(subAnim.get());
    }

    // ========================================================
    // 各元素动画 apply 函数 (对应 CSS 关键帧)
    // ========================================================

    /*
     * Mine 动画映射:
     *   0-26%: clip-path inset(top% 0 bottom% 0) 展开 + translate + skewX
     *   26-67%: translateX 左移到最终位置
     *   67-100%: 保持最终位置
     */
    void applyMineAnim(float p) {
        float eased = easeOut(p);

        // --- clip-path inset 动画 (0-26%) ---
        float clipP = clampf(p / 0.26f, 0.0f, 1.0f);
        float easedClip = easeOut(clipP);
        // inset(top, 0, bottom, 0) 从 inset(48% 0 49% 0) → inset(0 0 0 0)
        float insetTop = 48.0f - easedClip * 48.0f;
        float insetBottom = 49.0f - easedClip * 49.0f;
        // clipRect 尺寸
        if (mineClipRect) {
            mineClipRect->reset();
            mineClipRect->appendRect(
                0, insetTop * H * 0.01f, W, H * (1.0f - (insetTop + insetBottom) * 0.01f));
        }

        // --- transform: translate + skewX ---
        // 0%: translate(-50% - 10px, -42%) skewX(-10deg) scaleX(1.08)
        // 26%: translate(-50%, -50%) skewX(0) scaleX(1)
        float transX0 = -W * 0.5f - 10.0f;  // -50% - 10px
        float transX26 = -W * 0.5f;          // -50%
        float transY0 = -H * 0.42f;          // -42%
        float transY26 = -H * 0.5f;          // -50%
        float skewX0 = -10.0f;
        float skewX26 = 0.0f;

        float transX = lerpf(transX0, transX26, easedClip);
        float transY = lerpf(transY0, transY26, easedClip);
        float skewX = lerpf(skewX0, skewX26, easedClip);
        float scaleX = lerpf(1.08f, 1.0f, easedClip);

        // --- 滑到最终位置 (67-100%): 从 -50% 移到 -50% - mineFinalOffsetX ---
        if (p >= 0.26f) {
            float slideP = clampf((p - 0.26f) / 0.41f, 0.0f, 1.0f);
            float easedSlide = easeOut(slideP);
            transX = lerpf(transX26, transX26 - mineFinalOffsetX, easedSlide);
            if (p >= 0.67f) {
                transX = transX26 - mineFinalOffsetX;
            }
        }

        // 应用 transform (ThorVG: translate → rotate → scale)
        mineShape->translate(W * 0.5f + transX, H * 0.5f + transY);
        mineShape->rotate(skewX);  // skewX ≈ rotate
        mineShape->scale(scaleX);

        // --- letter-spacing (0→-0.055em) ---
        float letterSpacing = lerpf(0.055f, -0.055f, easedClip);
        // ThorVG 文字间距通过 appendText offset 模拟
        // 简化: 通过 scaleX 微调
    }

    /*
     * Radio 动画映射:
     *   0-32%: 隐藏，等待
     *   32-66%: clip-path inset 展开 + skewX 回正 + translate
     *   66-100%: 保持 + 渐变背景扫过
     */
    void applyRadioAnim(float p) {
        float eased = easeOut(p);

        // clip-path: inset(52% 0 44% 0) → inset(0 0 0 0)
        float clipP = clampf((p - 0.32f) / 0.34f, 0.0f, 1.0f);
        float easedClip = easeOut(clipP);
        float insetTop = 52.0f - easedClip * 52.0f;
        float insetBottom = 44.0f - easedClip * 44.0f;

        if (radioClipRect) {
            radioClipRect->reset();
            radioClipRect->appendRect(
                0, insetTop * H * 0.01f, W, H * (1.0f - (insetTop + insetBottom) * 0.01f));
        }

        // transform: translate + skewX
        // 32%: translate(-50% + 78px, -50%) skewX(9deg) scaleX(1.06)
        // 66%: translate(-50% + 70px, -50%) skewX(0) scaleX(1)
        float transX0 = -W * 0.5f + 78.0f;
        float transX1 = -W * 0.5f + 70.0f;  // 最终位置
        float transY0 = -H * 0.5f;
        float skewX0 = 9.0f;
        float skewX1 = 0.0f;

        float transX = lerpf(transX0, transX1, easedClip);
        float skewX = lerpf(skewX0, skewX1, easedClip);
        float scaleX = lerpf(1.06f, 1.0f, easedClip);

        radioShape->translate(W * 0.5f + transX, H * 0.5f + transY0);
        radioShape->rotate(skewX);
        radioShape->scale(scaleX);

        // --- 渐变背景扫过 (background-position 0 → 100%) ---
        // CSS: background-size: 300% 100%, position 0% → 100%
        // ThorVG: 移动 LinearGradient 的起点/终点
        if (radioGrad) {
            float bgP = clampf((p - 0.32f) / 0.68f, 0.0f, 1.0f);
            // 渐变轴从 (0,0)→(W*3, 0) 移动到 (W*2, 0)→(W*3, 0)
            // 即 background-position 0% → 100% 对应渐进取模偏移
            float gradStartX = -W * 2.0f * bgP;
            float gradEndX = gradStartX + W * 3.0f;
            radioGrad->linear(gradStartX, 0, gradEndX, 0);
        }
    }

    /*
     * i 圆点弹入 (48% 开始):
     *   48%-60%: 出现 + scale 弹跳 (0.34 → 1.12)
     *   60%-68%: scale 回弹 (1.12 → 0.94)
     *   68%-100%: scale 最终 (0.94 → 1)
     */
    void applyIDotAnim(float p) {
        float eased = easeOut(p);

        // scale 弹跳
        float scale = 0.34f + eased * 0.66f;
        if (p > 0.12f) {  // 60%
            scale = 1.12f - easeOut((p - 0.12f) / 0.56f) * 0.18f;
        }
        if (p > 0.20f) {  // 68%
            scale = 0.94f + easeOut((p - 0.20f) / 0.80f) * 0.06f;
        }

        iDotShape->scale(scale);
        iDotShape->opacity((uint8_t)(255.0f * clampf(eased, 0.0f, 1.0f)));
    }

    /*
     * 信号线 (28% 开始):
     *   scaleX .10 → 1.05 → .82 → 1.14 → .64
     *   opacity 0 → .98 → .70 → 1 → .30
     */
    void applySignalLineAnim(float p) {
        float eased = easeOut(p);

        // scaleX 动画 (简化为 4 段关键帧)
        float scaleX = 0.10f + eased * 0.90f;
        // 44%→64% 收缩, 64%→76% 膨胀, 76%→100% 收缩
        if (p > 0.36f) {  // 44%
            float t = (p - 0.36f) / 0.40f;
            scaleX = 1.05f - easeOut(t) * 0.23f;  // 1.05 → 0.82
        }
        if (p > 0.56f) {  // 64%
            float t = (p - 0.56f) / 0.20f;
            scaleX = 0.82f + easeOut(t) * 0.32f;  // 0.82 → 1.14
        }
        if (p > 0.76f) {  // 76%
            float t = (p - 0.76f) / 0.24f;
            scaleX = 1.14f - easeOut(t) * 0.50f;  // 1.14 → 0.64
        }

        signalLineShape->scale(scaleX);
        signalLineShape->opacity((uint8_t)(255.0f * clampf(eased, 0.0f, 1.0f)));
    }

    /*
     * 信号亮点 (42% 开始):
     *   移动: left 18% → 50% → 82%
     *   scale: .24 → 1 → 1.45 → .46
     *   opacity: 0 → .94 → 1 → .16
     */
    void applySignalBlipAnim(float p) {
        float eased = easeOut(p);

        // 水平位置 18% → 50% → 82%
        float posX;
        if (p < 0.50f) {  // 42%→62%
            float t = p / 0.50f;
            posX = 0.18f + (0.50f - 0.18f) * easeOut(t);
        } else {  // 62%→100%
            float t = (p - 0.50f) / 0.50f;
            posX = 0.50f + (0.82f - 0.50f) * easeOut(t);
        }

        // scale: .24 → 1 → 1.45 → .46
        float scale;
        if (p < 0.20f) {  // 42%→62%
            float t = p / 0.20f;
            scale = 0.24f + (1.0f - 0.24f) * easeOut(t);
        } else if (p < 0.36f) {  // 62%→76%
            float t = (p - 0.20f) / 0.16f;
            scale = 1.0f + (1.45f - 1.0f) * easeOut(t);
        } else {  // 76%→100%
            float t = (p - 0.36f) / 0.64f;
            scale = 1.45f - (1.45f - 0.46f) * easeOut(t);
        }

        signalBlipShape->translate(W * posX - W * 0.5f, 0);
        signalBlipShape->scale(scale);
        signalBlipShape->opacity((uint8_t)(255.0f * clampf(eased, 0.0f, 1.0f)));
    }

    /*
     * 副标题 (38% 开始):
     *   translateY 7px → 0, opacity 0 → 0.42
     */
    void applySubAnim(float p) {
        float eased = easeOut(p);
        subShape->translate(0, lerpf(7.0f, 0.0f, eased));
        subShape->opacity((uint8_t)(255.0f * clampf(eased * 0.42f, 0.0f, 0.42f)));
    }

    // ========================================================
    // 主循环更新
    // ========================================================
    void update(float dtMs) {
        // dtMs: 帧间隔 (ms)，由 OX::Timer::tick() 提供
        timeline.update(dtMs);

        // ThorVG 渲染
        if (canvas) {
            canvas->sync();
        }
    }
};
```

### 4. 主循环集成 (使用 OX::Timer)

```cpp
// 初始化
BrandSplash brand;
brand.init(glCanvas, windowWidth, windowHeight);

// 创建 Delta 模式 Timer
OX::Timer mainTimer;
mainTimer.setMode(OX::Timer::Mode::Delta);
mainTimer.setCallback([&]() {
    float dt = mainTimer.tick();  // 获取帧间隔 (ms)
    brand.update(dt);             // 更新动画
});
mainTimer.start();

// 主循环
while (running) {
    // 处理事件...

    // Timer Delta 模式: update() 传入 dt
    mainTimer.update(dt);

    // 如果需要在特定帧触发回调
    if (mainTimer.shouldFire()) {
        // 每帧触发一次回调
    }

    // SDL/窗口事件循环...
}
```

### 5. 或使用 Anime::Timeline 的完整编排

```cpp
// 使用 oanime::Timeline 更精细地编排各元素
auto mineAnim = Anime::target(mineShape)
    .prop(PropType::Opacity, 0.0f, 255.0f)
    .duration(5200.0f)
    .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));

auto radioAnim = Anime::target(radioShape)
    .prop(PropType::Opacity, 0.0f, 255.0f)
    .duration(5200.0f)
    .delay(1664.0f)  // 32% of 5200
    .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));

auto iDotAnim = Anime::target(iDotShape)
    .prop(PropType::Opacity, 0.0f, 255.0f)
    .duration(600.0f)
    .delay(2496.0f)  // 48% of 5200
    .easing(Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f));

Timeline tl;
tl.add(mineAnim.get());
tl.add(radioAnim.get(), 0.0f);           // 同时
tl.add(iDotAnim.get(), 0.0f);            // 同时
tl.play();

// 每帧
tl.update(dt);
canvas->sync();
```

### 6. 关键映射关系

| CSS 特性 | ThorVG 对应 |
|----------|-------------|
| `clip-path: inset(top 0 bottom 0)` | `tvg::Shape::clippath()` + 动态 `appendRect` 尺寸 |
| `transform: translate(-50%, -50%)` | `shape->translate(cx + offsetX, cy + offsetY)` |
| `skewX(-10deg)` | `shape->rotate(-10.0f)` (近似) |
| `background: linear-gradient(...) background-size: 300%` | `tvg::LinearGradient` + 动画化 `linear(startX, startY, endX, endY)` |
| `background-position: 0% → 100%` | 渐变轴起点从 `-2W` 移动到 `0` |
| `mix-blend-mode: screen` | `shape->blend(tvg::BlendMethod::Screen)` |
| `text-shadow: ...` | `tvg::Shape` + `tvg::Fill` 多层叠加或 GlCanvas 后处理 |
| `@keyframes` + `cubic-bezier(.22,1,.36,1)` | `Easing::cubicBezier(.22f, 1.0f, .36f, 1.0f)` + `Timeline.update(dt)` |

### 7. 性能要点

1. **clip-path 每帧重算**: `appendRect` 开销极小，无需重建 Shape。
2. **渐变位置动画**: 每帧调用 `linear(startX, 0, endX, 0)` 更新渐变轴，ThorVG 内部做 GPU 插值。
3. **文字渲染**: 简化方案用矩形+填充代替真实文字；真实文字可用 `tvg::Text::gen()` + `appendText()`。
4. **Timer 精度**: `OX::Timer::Mode::Delta` 使用 `std::chrono::steady_clock`，毫秒级精度足够 60fps。
5. **Timeline 编排**: 用 `delay()` 控制元素进入时机，无需手动管理时间偏移。
