# MPlayer 开发日志

> 记录时间: 2026-06-16 ~ 2026-06-20
> 记录范围: MPlayer 音视频播放器项目

MPlayer 是基于 FFmpeg + Filament + ThorVG 的音视频同步播放器，支持 MP4/FLAC 等格式，以音频时钟为主实现同步播放。

***

## 一、项目概述

**核心功能：**

- FFmpeg 解码音视频流
- miniaudio 音频输出
- Filament 渲染视频帧（全屏 Quad + 材质系统）
- ThorVG UI 覆盖层（播放控制、进度条、文件列表）
- 音视频同步（音频时钟为主）

**依赖库：**

- FFmpeg（libavformat/libavcodec/libavutil/libswscale/libswresample）
- Filament 1.71.6（渲染引擎）
- ThorVG（UI 矢量渲染）
- miniaudio（音频输出）
- oapp.h（窗口管理）

***

## 二、开发日志

### 2026-06-16

#### 1. 编译阶段: 修复第三方库头文件 C4267 警告

- **现象**：`ImmutableCString.h(80): warning C4267: 从"size_t"转换到"uint32_t"，可能丢失数据`
- **根因**：`StaticString::size()` 返回 `size_t`（64位），`ImmutableCString::mSize` 是 `uint32_t`（32位），隐式转换触发警告
- **解决**：在 `E:\MX\3rd\include\Filament1716\utils\ImmutableCString.h:80` 添加显式转换：
  ```cpp
  mSize(static_cast<uint32_t>(str.size()))
  ```

#### 2. 链接阶段: 修复 LNK4006 重复符号警告

- **现象**：`LNK4006: WebPInitSamplers 已在 libthorvg_mt.lib 中定义；已忽略第二个定义`
- **根因**：`libthorvg_mt.lib` 内嵌了 libwebp 代码，与单独链接的 `libwebp.lib` 产生符号冲突
- **解决**：在 `BUILD-MPlayer.bat` 链接选项添加 `/IGNORE:4006`

#### 3. 编译阶段: 修复 FFmpeg API 弃用警告 C4996

- **现象**：`MPlayer.cpp(405): warning C4996: 'av_stream_get_side_data': 被声明为已否决`
- **根因**：FFmpeg 7.x 弃用了 `av_stream_get_side_data()`，推荐使用 `av_packet_side_data_get()`
- **解决**：替换为新 API：
  ```cpp
  const AVPacketSideData* sd = av_packet_side_data_get(
      vStream->codecpar->coded_side_data,
      vStream->codecpar->nb_coded_side_data,
      AV_PKT_DATA_DISPLAYMATRIX);
  if (sd) {
      displayMatrix = (const int32_t*)sd->data;
      dmSize = sd->size;
  }
  ```

#### 4. 链接阶段: 修复 LNK4088 /FORCE 选项警告

- **现象**：`LNK4088: 因 /FORCE 选项生成了映像；映像可能不能运行`
- **根因**：使用 `/FORCE:MULTIPLE` 允许重复符号时，链接器发出此信息性警告
- **解决**：在 `BUILD-MPlayer.bat` 的 `/IGNORE` 列表添加 `4088`，最终为 `/IGNORE:4006,4088`

***

### 2026-06-19 — 歌单系统 & UI 交互

#### 1. config.json 基础设施

- **功能**：引入 `nlohmann/json.hpp`，在 `main()` 入口调用 `ensureConfig()` 自动创建 `MData/config.json`
- **模板**：参照 `mplayerdev.md` 字段说明表，6 个顶层节点：`player` / `playlists` / `ui` / `shortcuts` / `lastPlayed`
- **应用**：`applyConfig()` 读取 `player.volume` 和 `player.playMode` 映射到 `AppState`
- **注意**：`json.hpp` 的 `#include` 必须在 `ocore.h` 之前，避免与 Filament 的 `ERROR` 宏冲突

#### 2. 歌单数据结构

- **新增**：`SongInfo`（title/artist/album/filePath/duration/addDate）和 `PlaylistInfo`（name/createDate/type/songs[]/directoryPath/autoScan/scanInterval）
- **新增**：`loadPlaylists()` / `savePlaylists()` — 从 config.json 读写 playlists 数组
- **辅助函数**：`getTodayDate()` 返回 `YYYY-MM-DD` 格式；`normalizePath()` 统一 `\` → `/`；`splitDirs()` 按 `;` 分割多目录并去引号
- **旧数据兼容**：`loadPlaylists` 加载时对 `directoryPath` 重新走 `splitDirs` → 自动清洗旧配置中的引号和反斜杠

#### 3. 创建歌单对话框

- **触发**：左侧面板"+ 新建歌单"按钮 → `st.showCreatePlaylistDlg = true` → `rebuildUI()`
- **布局**：居中 modal 卡片，含名称输入（UITextInput）、类型选择（UIDropdown: 内置歌单/本地目录）
- **本地目录专属**：目录路径输入、自动扫描 UICheckbox、扫描间隔 UISlider（300~7200s）
- **保存**：校验名称非空 → 构建 `PlaylistInfo` → `savePlaylists()` → 自动切换到新歌单
- **localDir 创建后**：立即 `scanMediaFiles()` 扫描目录并填充文件列表

#### 4. 添加歌曲对话框（builtin 专用）

- **触发**：左侧面板"+ 添加歌曲"按钮，仅 `type=builtin` 歌单可用
- **流程**：扫描全局目录 → 移除已在歌单中的文件 → UIDropdown 展示候选 → 选中后点击"添加"
- **保存**：追加 `SongInfo` 到 `playlists[].songs` → 同步更新 `mediaFiles` → `savePlaylists()`

#### 5. 歌单切换 & 文件列表联动

- **歌单 UIDropdown**：`onSelectionChanged` → 清空 `mediaFiles` → builtin 用 `songs[].filePath`，localDir 调用 `scanMediaFiles`（支持 `;` 多目录）→ 自动加载第一个文件
- **文件 Dropdown**：根据 `mediaFiles` 动态重建，切换歌单后自动刷新

#### 6. Dialog 事件穿透修复

- **现象**：对话框遮罩挡住了所有 UI，输入框/按钮无法响应
- **根因**：`UIManager::handleMDown()` 和 `render()` 都正序遍历元素数组，视觉层级和点击优先级冲突
- **解决**：遮罩和卡片 `UIFrame` 均设 `enabled = false`（仅视觉作用）；先添加遮罩/卡片（渲染在下层），后添加控件（渲染在上层且接收点击）

#### 7. 键盘输入修复

- **现象**：`UITextInput` 有光标但无法输入文字
- **根因**：MPlayer 从未注册 `setKeyDownCallback/setCharCallback`，键盘事件未转发到 `UIManager`
- **解决**：参照 `TEST-OUI-ALL-REC.cpp` 补上 3 条回调：
  ```cpp
  st.app.setKeyDownCallback([&](int keyCode) { st.ui.handleKDown(keyCode); });
  st.app.setKeyUpCallback([&](int keyCode) { st.ui.handleKUp(keyCode); });
  st.app.setCharCallback([&](wchar_t ch) { st.ui.handleChar(ch); });
  ```

#### 8. Ctrl+V 粘贴支持

- **实现**：在 `setKeyDownCallback` 中拦截 `VK_V + Ctrl`，从剪贴板取 Unicode 文本，逐字符走 `handleChar` → `UITextInput::onChar`（自动处理光标和 UTF-8）
- **过滤**：跳过 `\r` `\n` 换行符避免意外行为

#### 9. 多目录支持（`;` 分隔符）

- **需求**：用户输入 `"E:\xmusic";"E:\phone link"` 时，应识别为两个独立目录
- **解决**：`splitDirs()` 按 `;` 分割后逐个去引号，三处调用统一——创建歌单保存、创建后扫描、歌单切换扫描

#### 10. 路径分隔符统一

- **现象**：`filePath` 中 `\` 和 `/` 混用
- **解决**：新增 `normalizePath()`，在所有路径写入点统一替换 `\` → `/`（`scanMediaFiles`/`splitDirs`/`loadPlaylists`/添加歌曲）

#### 11. 添加歌曲对话框 pool 悬空引用 Bug

- **现象**：点击"添加"按钮无效果
- **根因**：`onClick` lambda 用 `&pool` 捕获了 `rebuildUI` 内的局部变量，用户点击时 `pool` 已销毁
- **解决**：`[&st, &pool]` → `[&st, pool]`（按值拷贝）；`addSongSelIdx` 从 `static int` 改为 `st.addSongSelIdx` 成员变量

#### 12. 断点续播 — 开机自动恢复播放进度

- **功能**：退出时保存当前播放状态，下次启动自动恢复
- **保存**：`saveLastPlayed()` 在主循环退出后写入 `config.json` 的 `lastPlayed` 段：
  - `playlistIndex`：当前歌单索引
  - `songIndex`：当前歌曲在歌单中的索引
  - `position`：`st.videoTime` 播放进度（秒）
- **恢复**：`applyConfig()` 读取 `lastPlayed` → `resumePlaylistIndex/SongIndex/Position` → 在 `9.6 Resume` 段（音频启动后）执行：
  1. 切换到断点歌单 → 填充 `mediaFiles`
  2. 校验 `songIndex`（越界回退到 0）
  3. 停止当前音频 → `close()` 旧解码器 → `loadMediaFile` 加载断点文件
  4. `ma_device_start` → `pendingSeek(resumePosition)`
  5. 主循环第 1 帧通过已有的 `pendingSeek` 机制完成 seek
- **边界**：`position < 0.5s` 不 seek、无断点数据时走原流程不变

#### 13. 屏蔽 FFmpeg 内部日志

- **现象**：控制台刷屏 `[h264] co located POCs unavailable` 等内部警告，泄露库信息
- **根因**：FFmpeg 默认通过 `fprintf(stderr, ...)` 输出解码日志
- **解决**：`freopen("NUL", "w", stderr)` 在 `AllocConsole`/`initConsole` 之后重定向 stderr → FFmpeg 输出全部丢弃，程序自身日志正常显示
- **注意**：`av_log_set_level/callback` 在此 FFmpeg 构建中因链接规范冲突（无 `extern "C"`）无法使用

#### 14. 纯音频封面显示

- **功能**：播放纯音频文件时显示歌曲封面，不再黑屏
- **提取**：`AVDecoder::open()` 遍历流检查 `AV_DISPOSITION_ATTACHED_PIC` → 命中则用 `stbi_load_from_memory` 将 `attached_pic` 数据解码为 RGBA 存入 `coverArtRGBA`
- **显示**：`loadMediaFile()` 无视频流时：
  1. `decoder.hasCover()` → 用封面尺寸覆盖 `videoW/H`，上传封面 RGBA
  2. 无封面 → `stbi_load("LOGO.jpg")` → 上传 logo
  3. logo 也失败 → 回退 1x1 黑色
- **新增字段**：`coverArtRGBA` / `coverArtW` / `coverArtH` / `hasCoverArt` + 4 个 public getter

#### 15. 修复：切换歌曲后封面残影

- **现象**：从有封面的歌切到无封面的歌，仍显示上首歌曲封面
- **根因**：`AVDecoder::close()` 未清 `hasCoverArt` / `coverArtRGBA`，`open()` 中只在命中 attached_pic 时才设置 → 无封面的文件残留上首状态
- **解决**：`close()` 追加 `hasCoverArt = false; coverArtRGBA.clear(); coverArtW = coverArtH = 0;`

#### 16. ShaderToy 动态视觉效果系统

- **架构**：全屏 Quad (`gfxEntity`) 使用独立 Camera + View，通过 `RenderableManager` layer mask 切换视频/特效可见性
- **材质编译**：`buildShaderToyFXMaterial(engine, name, code, blend)` 用 `filamat::MaterialBuilder` 运行时编译 GLSL 着色器为 Filament Material
- **ShaderToy 适配规范**：
  - `mainImage(out fragColor, in fragCoord)` → `void material(inout MaterialInputs m)` + `m.baseColor = vec4(col, 1.0)`
  - `iTime` → `materialParams.iTime`（FLOAT uniform）
  - `iResolution` → `materialParams.iResolution`（FLOAT2 uniform）
  - `iChannel0` → `materialParams_sceneTex`（SAMPLER_2D，绑定视频/封面纹理）
  - `iMouse` → 拆分为 `iMouseXY` + `iMouseZW`（两个 FLOAT2，因旧版 filamat 对 FLOAT4 + vec3 引用组合存在编译异常）
  - `fragCoord` → `gl_FragCoord`
- **3 个特效**（定义在 `shadertoy_effects.h`，注册在 `g_fxEffects[]`）：
  - `DynamicRect` — 噪声背景 + 60 个随机浮动矩形（TRANSPARENT 覆盖）
  - `VanGoghSunset` — 梵高风格日落风景画，纯生成式无纹理（TRANSPARENT 覆盖）
  - `HeartfeltRain` — 玻璃雨滴折射特效（OPAQUE 全屏替换，折射背景视频纹理）

#### 17. HeartfeltRain 详解 — 原版 ShaderToy 照搬

- **策略**：完全照搬原版 ShaderToy 代码，仅做最小适配（9 项，见适配规范），禁止自行调整参数
- **原版宏全部开启**：`#define HAS_HEART` + `#define USE_POST_PROCESSING`
  - `HAS_HEART`：iTime 102 秒循环、70 秒缩放入画（zoom 0.3→1.2）、心形遮罩雨滴、心形色彩保留
  - `USE_POST_PROCESSING`：色彩漂移（蓝紫色调）、闪电闪烁、vignette、起始淡入/心形淡出
- **OPAQUE 材质**：HeartfeltRain 使用 `BlendingMode::OPAQUE`（输出 `fragColor.rgb, 1.0`），区别于另外两个使用 TRANSPARENT 叠加的效果
- **纹理坐标系修正**：视频纹理首行像素 `v=0` 对应屏幕顶部，但 `gl_FragCoord.y=0` 是屏幕底部 → `sampleUV.y = 1.0 - UV.y`
- **textureLod 降级**：Filament 不支持 `textureLod`，用 `texture()` + UV 缩放 `1.0/(1.0+focus*4.0)` 补偿模糊效果

#### 18. 宽高比校正 — 不拉伸背景

- **功能**：HeartfeltRain 背景按视频原始比例显示，超宽/超高区域渲染黑色（letterbox/pillarbox）
- **实现**：新增 `texSize` FLOAT2 uniform（绑定 `float2(videoW, videoH)`），shader 中计算：
  ```glsl
  float texAspect = materialParams.texSize.x / materialParams.texSize.y;
  float scrAspect = materialParams.iResolution.x / materialParams.iResolution.y;
  // pillarbox（视频更窄）：左右黑边
  // letterbox（视频更短）：上下黑边
  vec3 col = inBounds > 0.5 ? texture(...) : vec3(0.0);
  ```

#### 19. iMouse 鼠标交互

- **功能**：对标 ShaderToy 的 iMouse 行为：
  - 移动鼠标 → `.xy` 实时跟随（Y 坐标翻转到 bottom-left 原点）
  - 按住左键 → `.z > 0`；释放 → `.zw` 变负值
  - 拖拽水平方向 → 控制时间流速和时间定位
  - 拖拽垂直方向 → 控制雨量密度
- **实现**：`AppState::fxMouse`（float4）；鼠标回调中同步；渲染循环每帧 `setParameter("iMouseXY/ZW", ...)`
- **编译兼容**：旧版 filamat 对 `vec3 M = vec3(0.);` 后引用 `M.x`/`M.z` 会导致内部异常 → 改为三个独立 float（`mx/my/mz`）

#### 20. 断电恢复 — filamat 编译异常定位

- **现象**：电脑断电重启后，HeartfeltRain 第三个特效消失，日志 `[FX] HeartfeltRain material compile FAILED`
- **排查过程**：
  1. 怀疑 FLOAT4 uniform 不兼容 → 拆为 FLOAT2，仍然失败
  2. 怀疑 shader GLSL 语法错误 → 极简 shader（红色输出）通过
  3. 逐步添加：纹理采样 ✓ → 辅助函数 ✓ → 雨滴逻辑 ✓ → `HAS_HEART` ✓ → `USE_POST_PROCESSING` ✓
  4. 最终定位：`T = materialParams.iTime+M.x*2.;` 中 `M.x` 引用（即使在 `vec3(0.)` 下等价无操作）触发旧版 filamat 内部编译器崩溃
- **解决**：拆开 vec3 为独立 float，避免在 vec3(0.) 上引用 `.x` / `.z` 成员

#### 21. FFmpeg 许可证调研 — BtbN/FFmpeg-Builds 来源确认

- **当前 FFmpeg 来源**：`ffmpeg-n7.1-241205-gpl-amd64-static.zip`（2024-12-05），来自 [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds)（GitHub 10.6k stars），CI 自动构建
- **许可状态**：GPL 2.0+（含 `--enable-gpl --enable-libx264 --enable-libx265`），编译器 MSVC 19.42，静态链接
- **LGPL 替代调研**：
  - **BtbN LGPL static**（不含 `-shared`）→ 只有 `ffmpeg.exe`，**不含开发文件**（无 `include/`、无 `lib/`），不可用于开发
  - **BtbN LGPL shared**（含 `-shared`）→ 有 `include/`（FFmpeg 头）+ `lib/`（MSVC 兼容 `.lib` 导入库）+ `bin/`（DLL），可用于开发但需 DLL 分发
  - **gyan.dev** → 同样只提供 shared 变体的开发文件，无静态版
- **结论**：Windows 上**不存在**预编译的 LGPL 静态（零 DLL）开发库。要 LGPL 就必须接受 DLL。当前保持 GPL 静态构建不变，待后续决定

#### 22. 清理测试文件

- 下载验证用的临时 zip 和解压目录：`test-lgpl.zip`、`test-lgpl-shared.zip`、`FF-lgpl/`、`FF-lgpl-shared/` → 已清理

### 2026-06-21 歌词面板开发

#### 23. 歌词面板（showLyrics）

- 新增 `LrcLine` 结构体（timestamp + text）
- `parseLRC()`：解析标准 LRC 格式，支持 `[mm:ss.xx]` 和 `[mm:ss.xxx]`，处理多时间戳行，按时间排序
- `AVDecoder::getEmbeddedLyrics()`：遍历 format/stream 级别 metadata，覆盖 FLAC Vorbis Comment / MP3 ID3v2 / MP4 ©lyr 等常见 key
- `loadLyrics()` 三级回退：
  1. 优先提取内嵌歌词 → 含时间戳按 LRC 解析，纯文本按时长均匀分布
  2. 无内嵌歌词回退到外部同目录 .lrc 文件
- 歌词面板 UI：右侧 UIFrame 半透明背景 + 7 行 UILabel 居中
  - 当前行（第4行）高亮白色 18px，邻近行 14px，远行 12px
- `updateLyrics()`：二分查找当前歌词行，每帧更新 label 文本和样式
- `config.json` `ui.showLyrics` 控制显隐，`applyConfig()` 中读取

#### 24. 显示: FX3 HeartfeltRain 雨滴覆盖歌词层

- **现象**：切换到 FX3（HeartfeltRain）时，歌词层渲染在雨滴效果之上，雨滴无法覆盖歌词。尝试多种 shader 混合方案（先混 bg+lrc 再用 rainMask 覆盖、降低歌词透明度等）均失败。中间曾出现歌词渲染两次：一次雨滴覆盖歌词但 Y 反转，一次歌词覆盖雨滴且 Y 正常。
- **根因**：
  1. **Shader 混合顺序错误**：原代码 `col = mix(col, lrc.rgb, lrc.a)` 在雨滴扭曲后的背景上混合歌词 → 歌词永远画在雨滴上面。
  2. **歌词 Y 翻转**：歌词纹理通过 `glReadPixels` 上传（行 0 = 底部），已对齐 GL 坐标系（v=0 在底部），无需 Y 翻转。但和背景共用 `sampleUV`（含 `1.0-bgUV.y`）导致歌词 Y 反转。
  3. **C++ 端遗漏刷新**：FX 下拉选择回调 `onSelectionChanged` 未调用 `updateVisibleLayers()` → 切换到雨滴 FX 时独立歌词 entity 未被隐藏 → 歌词渲染两次。
- **解决**：
  1. **Shader**：背景和歌词**在同一个雨滴扭曲坐标系下分别采样，再混合**。背景用 `sampleUV = bgUV(含Y翻转) + n * factor`，歌词独立用 `lrcSampleUV = lrcUV(不含Y翻转) + n * factor`。两者受到相同雨滴偏移 → 雨滴视觉效果同时覆盖背景和歌词。
  2. **C++**（`MPlayerV10.cpp` FX dropdown `onSelectionChanged`）：补上 `updateVisibleLayers(st)` 调用，确保切到 FX3 时隐藏独立歌词 entity。
  - 关键代码（`shadertoy_effects.h` L618-623）：
    ```glsl
    vec2 sampleUV = vec2(bgUV.x, 1.0-bgUV.y) + vec2(n.x,-n.y)*(1.0/(1.0+focus*4.0));
    vec2 lrcSampleUV = vec2(lrcUV.x, lrcUV.y) + vec2(n.x, n.y)*(1.0/(1.0+focus*4.0));
    vec3 col = inBounds > 0.5 ? texture(materialParams_sceneTex, sampleUV).rgb : vec3(0.0);
    vec4 lrc = texture(materialParams_lyricsTex, lrcSampleUV);
    col = mix(col, lrc.rgb, lrc.a);
    ```

***

## 三、已知问题备忘

| Bug  | 位置     | 影响     | 状态     |
| ---- | ------ | ------ | ------ |
| （暂无） | <br /> | <br /> | <br /> |

***

## 四、商业化 & 许可证分析（2026-06-20）

### 4.1 第三方库许可证概览

| 组件 | 许可证 | 可商用 | 约束 |
|---|---|---|---|
| FFmpeg（当前构建含 x264/x265） | **GPL 2.0+** | 可以卖 | **必须开源全部代码**，购买者可自由再分发 |
| ShaderToy 移植 shader | **CC BY-NC-SA 4.0** | **不可以** | NC = NonCommercial 禁止商用；SA = 衍生作品须同许可 |
| Filament | Apache 2.0 | 可以 | 无需开源 |
| ThorVG | MIT | 可以 | 无需开源 |
| miniaudio | MIT / Public Domain | 可以 | 无需开源 |

### 4.2 解决 FFmpeg GPL 风险的方案

当前 FFmpeg 来源：[BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) `ffmpeg-n7.1-241205-gpl-amd64-static.zip`（2024-12-05），静态链接了 `libx264` 和 `x265`（GPL 组件），整个程序受 GPL 约束。

**调研结论（2026-06-20）**：Windows 上不存在预编译的 LGPL 静态（零 DLL）开发库。

| 变体 | 有 `include/` + `lib/`？ | 需要 DLL？ | 许可 |
|---|---|---|---|
| BtbN LGPL static | 无 | 不需要 | LGPL |
| BtbN LGPL shared | 有（`.lib` MSVC 导入库） | 需要 5 个 DLL | LGPL |
| gyan.dev | 有（`.lib` MSVC 导入库） | 需要 5 个 DLL | LGPL |
| BtbN GPL static（当前） | 有（`.a` 静态库） | 不需要 | GPL |

**可行路径**：

| 方案 | 做法 | 效果 |
|---|---|---|
| **方案 A** | 切换到 BtbN LGPL shared，链接 `.lib`，分发 DLL | LGPL 许可，可闭源，但 exe 旁边有 5 个 DLL（~105 MB） |
| **方案 B** | 自己从源码编译 FFmpeg 为静态 LGPL | LGPL 许可，零 DLL，但编译难度高（用户曾尝试失败） |
| **方案 C（当前）** | 保持 BtbN GPL static | 商用需开源全部代码 |

**注意**：`/MT`（静态链接 CRT）与 FFmpeg 静态/动态链接是两件独立的事，二者可共存。

### 4.3 ShaderToy 版权避让 — FX 外置加载

**核心策略**：程序不内置任何 ShaderToy 代码，只作为显示平台。用户自行获取 `.glsl` 文件放入 `effects/` 目录。

```
MPlayer.exe              ← 自带 0 个 ShaderToy 特效
effects/
  ├── MyRain.glsl         ← 原创（可内置）
  ├── MySunset.glsl       ← 原创（可内置）
  └── (用户自行放入)       ← 非你责任
```

**法律原理**：你分发的 exe 不含侵权内容（类比 VLC 不内置盗版电影）。在 About 页面声明"用户对其加载内容的合法性负责"。

### 4.4"借鉴改改"能否算原创 — 判定标准

| 做法 | 法律定性 |
|---|---|
| 复制原代码 → 改参数名/变量名 | 衍生作品，受原许可证约束 |
| 复制原代码 → 调整数值 | 衍生作品 |
| 翻译原代码（HLSL→GLSL） | 衍生作品 |
| 对着原代码写自己的实现（结构、算法相同） | 灰色地带，有风险 |
| **不看代码，理解算法思想后从零重写** | **算原创** |
| 看着视觉效果，自己独立编写 shader | 算原创 |

**实操建议**：理解原特效的算法原理 → 关掉原代码 → 凭记忆独立编写。

### 4.5 变现模式对比

| 模式 | 优势 | 劣势 |
|---|---|---|
| **免费 + 付费特效包** | 低门槛获客，持续收益 | 需要原创特效积累 |
| 纯捐赠 | 零许可证风险 | 万分之一转化率 |
| 技术文章/教程变现 | 已有完整移植方案可直接写 | 需持续运营 |
| 买断制销售 | 一次性收益清晰 | 竞品全是免费的 |
| Steam 上架 | 平台流量，用户付费习惯成熟 | 需产品级打磨 |

### 4.6 推荐方案：免费基础版 + Pro 解锁

```
MPlayer（免费版）                   MPlayer Pro（赞赏解锁）
├── 完整播放功能                     ├── 以上全部
├── 歌单系统                          ├── 高级特效包（5+ 个）
├── 2 个内置原创特效                    ├── 自定义快捷键
├── 特效参数面板                       └── 无启动延迟
└── About 页面有"解锁 Pro"按钮
```

### 4.7 激活流程设计

```
1. 用户点"解锁 Pro" → 展示微信赞赏码 + 设备码
2. 用户扫码赞赏（¥19.9）→ 截图发你
3. 你运行 keygen <设备码> → 生成激活码 → 回复用户
4. 用户输入激活码 → config.json 写入 proActivated: true
5. 程序重启后扫描 effects/pro/ 目录（此前隐藏）
```

**Keygen 思路**：`MD5(设备码 + 固定盐值)` 取前 16 位格式化，极简离线验证。对目标用户（会付 ¥19.9 的小众群体）足够。

### 4.8 当前待办（商业化路线）

- [ ] FFmpeg 许可证切换（方案 A: BtbN LGPL shared 或 方案 B: 自编译静态 LGPL）
- [ ] 将现有 3 个 ShaderToy 特效移出代码，改为从 `effects/` 目录加载
- [ ] 原创 5 个 CC0 特效用于内置 + Pro 包
- [ ] 实现 FX 外置 `.glsl` 文件扫描加载架构
- [ ] 添加 Pro 解锁对话框 + keygen 工具
- [ ] About 页面声明
























# Next

- [x] 添加json模块"E:\MX\3rd\include\json.hpp"，添加config.json文件
- [x] 歌单系统：创建歌单 / 添加歌曲 / 切换歌单 / 文件列表联动
- [x] Dialog 事件穿透 + 键盘输入 + Ctrl+V 粘贴
- [x] 多目录支持 + 路径分隔符统一
- [x] 断点续播（记录 & 恢复播放进度）
- [x] 屏蔽 FFmpeg 内部日志
- [x] 纯音频封面显示（嵌入式 → LOGO.jpg 回退）
- [ ] 歌单内歌曲的删除和排序
- [x] ShaderToy 动态视觉效果系统（3 种特效 + iMouse + texSize）
- [x] ShaderToy 适配 Filament 规范（mainImage→material, uniform 映射）
- [x] HeartfeltRain 玻璃雨滴折射（原版照搬 + OPAQUE + 纹理折射 + 宽高比校正）
- [x] 歌词面板（showLyrics）：LRC 解析、7行滚动高亮、内嵌歌词提取(FLAC/MP3/MP4)、纯文本回退
- [ ] 均衡器设置面板
- [ ] miniMode 迷你模式








## config.json 字段说明表

### 1. player — 播放器设置

| 字段               | 类型   | 默认值      | 作用               | 备注                                                   |
| ------------------ | ------ | ----------- | ------------------ | ------------------------------------------------------ |
| volume             | int    | 80          | 初始音量（0~100）  | 每次启动时读取，退出时保存                             |
| playMode           | string | "listLoop"  | 播放模式           | 可选值：listLoop, singleLoop, random, sequence         |
| equalizer.enable   | bool   | false       | 是否启用均衡器     | 开启后使用 preset 预设                                 |
| equalizer.preset   | string | "classical" | 均衡器预设方案     | 可选：classical, pop, rock, jazz, dance, vocal, custom |
| crossfade.enable   | bool   | false       | 是否启用淡入淡出   | 切歌时过渡效果                                         |
| crossfade.duration | int    | 3           | 淡入淡出时长（秒） | 范围 1~10                                              |

### 2. playlists[] — 歌单列表（数组）

| 字段          | 类型   | 默认值    | 作用               | 备注                                |
| ------------- | ------ | --------- | ------------------ | ----------------------------------- |
| name          | string | 必填      | 歌单显示名称       | 不可为空                            |
| createDate    | string | 创建当日  | 歌单创建日期       | 格式 YYYY-MM-DD                     |
| type          | string | "builtin" | 歌单来源类型       | builtin=JSON内置, localDir=本地目录 |
| songs[]       | array  | []        | 歌曲列表           | type=builtin 时有效                 |
| directoryPath | string | ""        | 本地音乐文件夹路径 | type=localDir 时必填                |
| autoScan      | bool   | true      | 是否自动扫描新文件 | type=localDir 时有效                |
| scanInterval  | int    | 3600      | 自动扫描间隔（秒） | 默认1小时，最小值300                |

### 3. songs[] — 歌曲信息（内嵌于歌单）

| 字段     | 类型   | 默认值               | 作用             | 备注                   |
| -------- | ------ | -------------------- | ---------------- | ---------------------- |
| title    | string | 文件名（不含扩展名） | 歌曲标题         | 优先从标签读取         |
| artist   | string | "未知艺术家"         | 歌手名           | 可从ID3标签自动填充    |
| album    | string | "未知专辑"           | 专辑名称         | 可从ID3标签自动填充    |
| filePath | string | 必填                 | 音频文件完整路径 | 支持相对路径和绝对路径 |
| duration | float  | 0.0                  | 歌曲时长（秒）   | 启动时自动计算         |
| addDate  | string | 添加当日             | 添加到歌单的日期 | 格式 YYYY-MM-DD        |

### 4. ui — 界面设置

| 字段           | 类型   | 默认值  | 作用             | 备注                       |
| -------------- | ------ | ------- | ---------------- | -------------------------- |
| theme          | string | "dark"  | 界面主题         | 可选：dark, light, auto    |
| language       | string | "zh-CN" | 界面语言         | 可选：zh-CN, zh-TW, en, ja |
| showLyrics     | bool   | true    | 是否显示歌词面板 | 需配合歌词文件             |
| miniMode       | bool   | false   | 是否默认迷你模式 | 窗口缩小到只保留基本控制   |
| backgroundBlur | int    | 30      | 背景模糊强度     | 范围 0~100，0为不模糊      |

### 5. shortcuts — 快捷键映射

| 字段       | 类型   | 默认值       | 作用      | 备注                |
| ---------- | ------ | ------------ | --------- | ------------------- |
| playPause  | string | "Space"      | 播放/暂停 | 支持组合键如 Ctrl+P |
| nextTrack  | string | "Ctrl+Right" | 下一首    | —                   |
| prevTrack  | string | "Ctrl+Left"  | 上一首    | —                   |
| volumeUp   | string | "Ctrl+Up"    | 音量增加  | 步进5%              |
| volumeDown | string | "Ctrl+Down"  | 音量减小  | 步进5%              |

### 6. lastPlayed — 上次播放记录

| 字段          | 类型  | 默认值 | 作用               | 备注         |
| ------------- | ----- | ------ | ------------------ | ------------ |
| playlistIndex | int   | 0      | 上次播放的歌单索引 | 从0开始计数  |
| songIndex     | int   | 0      | 上次播放的歌曲索引 | 从0开始计数  |
| position      | float | 0.0    | 上次播放进度（秒） | 用于断点续播 |

------

### 完整 JSON 模板（含默认值注释版）

```json
{
  "player": {
    "volume": 80,
    "playMode": "listLoop",
    "equalizer": { "enable": false, "preset": "classical" },
    "crossfade": { "enable": false, "duration": 3 }
  },
  "playlists": [],
  "ui": {
    "theme": "dark",
    "language": "zh-CN",
    "showLyrics": true,
    "miniMode": false,
    "backgroundBlur": 30
  },
  "shortcuts": {
    "playPause": "Space",
    "nextTrack": "Ctrl+Right",
    "prevTrack": "Ctrl+Left",
    "volumeUp": "Ctrl+Up",
    "volumeDown": "Ctrl+Down"
  },
  "lastPlayed": {
    "playlistIndex": 0,
    "songIndex": 0,
    "position": 0.0
  }
}
```

> 提示：所有标注"必填"的字段在生成配置文件时必须存在；其他字段缺失时会自动使用默认值。





























# END