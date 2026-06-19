
# 代码架构
## Filament 引擎架构
以下是 36 个 filament/include/filament 头文件的分类表格，共 12 个大类，按功能近似分组：

| 分类 | 头文件 | 主要功能 |
|---|---|---|
| **1. 引擎核心 (Engine Core)** | Engine.h | 渲染引擎入口，创建/销毁所有资源，管理驱动与帧循环 |
| | FilamentAPI.h | 公共 API 基类，提供 PIMPL 私有实现模式与引用计数 |
| | Options.h | 全局渲染质量/配置选项（阴影、抗锯齿、MSAA、着色质量等） |
| **2. 场景与变换 (Scene & Transform)** | Scene.h | 场景容器，管理天空盒、间接光、实体集合与查询回调 |
| | TransformManager.h | 管理实体变换（平移/旋转/缩放），维护局部与世界矩阵 |
| | RenderableManager.h | 管理可渲染组件（网格、材质绑定、蒙皮、形变目标、包围盒） |
| **3. 相机与视图 (Camera & View)** | Camera.h | 物理相机模型（投影矩阵、曝光参数、近远平面、光圈快门） |
| | View.h | 视图核心设置（绑定场景/相机/视口，阴影/后处理/抗锯齿选项） |
| | Viewport.h | 屏幕视口定义（像素坐标系，左下角原点+宽高） |
| **4. 灯光 (Lighting)** | LightManager.h | 灯光管理器（定向光/点光/聚光/面光、阴影投射、IES、强度色温） |
| | IndirectLight.h | 间接光照 IBL（辐照图 + 反射探针 + 球谐 SH 系数） |
| | Exposure.h | 物理相机曝光计算工具（EV100、亮度、照度的换算） |
| **5. 材质系统 (Material System)** | Material.h | 材质包定义（编译后着色器程序，声明参数类型与默认值） |
| | MaterialInstance.h | 材质实例（运行时设置 uniform 参数值、绑定纹理采样器） |
| **6. 颜色与后处理 (Color & Post-Processing)** | Color.h | 线性 / sRGB 颜色类型，颜色空间转换与运算 |
| | ColorGrading.h | 色彩分级（LUT、ACES 色调映射、白平衡、饱和度、对比度） |
| | ToneMapper.h | 色调映射算子接口（将 HDR 动态范围压缩到 LDR 显示范围） |
| | ColorSpace.h | 颜色空间定义（色度坐标、色域转换矩阵、预置 DCI-P3/sRGB 等） |
| **7. 资源与纹理 (Resources & Textures)** | Texture.h | GPU 纹理对象（2D/3D/Cubemap/压缩格式、Mipmap、多平面） |
| | TextureSampler.h | 纹理采样器配置（过滤模式、Wrap 模式、各向异性、深度比较） |
| | Skybox.h | 天空盒资源（环境贴图渲染，支持 HDR 立方体贴图） |
| **8. 缓冲与几何数据 (Buffers & Geometry)** | VertexBuffer.h | 顶点缓冲（位置/法线/UV/颜色/骨骼属性，多缓冲插槽） |
| | IndexBuffer.h | 索引缓冲（三角形索引数组，支持 uint32/uint16） |
| | BufferObject.h | 通用 GPU 缓冲基类（用于顶点/索引/Uniform/SSBO） |
| | MorphTargetBuffer.h | 形变目标缓冲（blendshapes 顶点动画数据） |
| | SkinningBuffer.h | 蒙皮缓冲（骨骼变换矩阵统一缓冲 UBO） |
| | InstanceBuffer.h | 实例化绘制缓冲（GPU instancing 批量渲染数据） |
| **9. 渲染框架 (Rendering)** | Renderer.h | 渲染器调度（执行帧渲染、设置清除色、FXAA、TAA、色调映射） |
| | RenderTarget.h | 渲染目标（离屏渲染附件：颜色/深度/模板纹理绑定） |
| **10. 平台与同步 (Platform & Sync)** | SwapChain.h | 交换链（平台原生可渲染表面，双缓冲/三缓冲 present） |
| | Stream.h | 外部纹理流（如 Camera2、视频解码帧直接送入 GPU 纹理） |
| | Fence.h | GPU 围栏同步（CPU 等待 / 查询 GPU 完成特定操作点） |
| | Sync.h | 跨设备/跨进程同步对象（Vulkan semaphore/fence 互操作） |
| **11. 调试 (Debug)** | DebugRegistry.h | 调试注册表（运行时切换渲染管线各部分可视化，如级联阴影/法线） |
| **12. 数学与几何 (Math & Geometry)** | Box.h | AABB 轴对齐包围盒（Bounding Box 计算、变换与包含检测） |
| | Frustum.h | 视锥体（6 个平面构成的视锥体裁剪与相交测试） |

记忆路径：Engine → Scene → View/Camera → Renderer → 数据流(Vertex/Index/Texture/Material/Instance) → 灯光/后处理 → SwapChain → 同步/调试，正好对应 Filament 一帧渲染的完整管线顺序。

# shaders/src 目录着色器文件分类

以下是 `shaders/src` 目录 43 个着色器文件的分类表格，共 10 个大类，按前缀分组：

| 分类 | 文件 | 用途 |
|---|---|---|
| **1. 基础与平台抽象 (Foundation / Platform)** | `common_defines.glsl` | Vulkan/GLSL 可移植层：`LAYOUT_LOCATION` 宏、类型别名（float2→vec2、float3x3→mat3）、Adreno 高精度变通 |
| | `common_math.glsl` | 全局数学库：PI/HALF_PI/FLT_EPS 常量、pow5/sq/saturate 工具函数、精度安全矩阵运算 |
| | `common_getters.glsl` | 帧统一数据读取 API：相机矩阵（`getViewFromWorldMatrix` 等）、立体眼索引、时间等，供顶点/片段阶段共用 |
| **2. 类型与 varyings 定义 (Types & Varyings)** | `surface_types.glsl` | UBO 结构体定义：`ShadowData`（阴影参数）、`BoneData`（骨骼）、`PerRenderableData`（通道标志：蒙皮/形变/实例化） |
| | `surface_varyings.glsl` | 声明所有顶点→片段插值 varying：`vertex_color`/`vertex_uv01`/`vertex_worldPosition`/`vertex_worldNormal`/`vertex_worldTangent`/`vertex_lightSpacePosition` 等 |
| | `surface_instancing.glsl` | 从 UBO 读取每实例数据（世界矩阵、法线矩阵、对象 ID、形变计数），提供 `getWorldFromModelMatrix()` |
| **3. 通用着色辅助 (Common Shading Helpers)** | `common_graphics.fs` | 颜色空间工具：Rec.709 亮度、YCbCr→sRGB、逆电影色调映射、反预乘 α |
| | `common_shading.fs` | 每片段着色全局变量：TBN 矩阵、`shading_position`/`shading_view`/`shading_normal`/`shading_reflected`/`shading_NoV`/`shading_clearCoatNormal` 等 |
| | `surface_material.fs` | 材质属性计算辅助：粗糙度↔感知粗糙度映射、`computeDiffuseColor()`/`computeF0()`/`computeDielectricF0()`、IOR↔F0 转换 |
| | `surface_brdf.fs` | 完整 Cook-Torrance 微面元 BRDF 库：`D_GGX`/`D_Charlie`/`V_SmithGGXCorrelated`/`V_Neubelt`/`F_Schlick`/`diffuse()`（Lambert/Burley），支持各向异性与清漆 |
| | `surface_fog.fs` | 大气雾效：Beer-Lambert 定律，高度衰减，可选 IBL 立方体贴图雾色采样 |
| | `surface_ambient_occlusion.fs` | SSAO 评估：`evaluateSSAO()`，深度感知双边滤波上采样，支持镜面反射 AO / 弯曲法线 |
| **4. 内联后处理特效 (Inline Effects)** | `inline_dithering.fs` | 去色阶抖动：三角噪声 `triangleNoise()`、交织梯度噪声 `interleavedGradientNoise()` |
| | `inline_vignette.fs` | 屏幕空间暗角效果：`vignette()`，基于像素到中心距离 |
| **5. 材质用户 API（Inputs / Getters）** | `surface_material_inputs.vs` | 顶点阶段材质输入结构 `MaterialVertexInputs`（color, uv0, uv1, 自定义属性, worldNormal, worldPosition 等），`initMaterialVertex()` |
| | `surface_material_inputs.fs` | 片段阶段材质输入结构 `MaterialInputs`（baseColor, roughness, metallic, reflectance, ao, emissive, clearCoat, anisotropy, thickness, subsurface 等全部 PBR 参数） |
| | `surface_getters.vs` | 顶点着色器用户 API：骨骼蒙皮 `mulVertex()`/`mulBoneNormal()`、形变目标 `getMorphTargetsPosition()`、自定义属性 `getCustomVertexAttribute0..4()` |
| | `surface_getters.fs` | 片段着色器用户 API：`getColor()`/`getUV0/1()`/`getWorldTangentFrame()`/`getWorldPosition()`/`getWorldViewVector()`/`getWorldNormalVector()`/`getNdotV()`/`getExposure()`/`getBentNormal()` 等 |
| | `surface_getters.cs` | 计算着色器用户 API：`getWorkGroupID()`/`getLocalInvocationID()`/`getGlobalInvocationID()` |
| **6. 表面管线主入口 (Surface Main Entry Points)** | `surface_main.vs` | 顶点着色器主入口：实例化初始化 → 立体渲染 → 调用用户 `materialVertex()` → TBN 四元数提取 → 蒙皮/形变 → `gl_Position` |
| | `surface_main.fs` | 片段着色器主入口：`computeShadingParams()` → 调用用户 `material()` → α 蒙版 → `prepareMaterial()` → `evaluateMaterial()` → 雾效/抖动/暗角 |
| | `surface_main.cs` | 计算着色器主入口：简单派发用户 `compute()` 函数 |
| | `surface_depth_main.fs` | 深度预通道：运行用户材质，计算 VSM 深度矩或对象拾取 ID（处理蒙版/半透明阴影） |
| **7. 光照 (Lighting)** | `surface_lighting.fs` | 核心光照数据类型：`Light` 结构体（colorIntensity, direction, attenuation, shadows 等）、`PixelParams`（diffuseColor, f0, roughness, dfg, clearCoat, anisotropy, thickness 等） |
| | `surface_light_directional.fs` | 方向光（太阳）：`sampleSunAreaLight()` 面积光近似 → 选择阴影级联 → 派发着色模型 |
| | `surface_light_punctual.fs` | 点光/聚光：`evaluatePunctualLights()`，froxel 聚簇前向光照，支持聚光阴影映射 |
| | `surface_light_indirect.fs` | IBL 间接光照：球谐辐照度（SH 带 1/2/3）、预过滤 DFG 查找、立方体贴图重要性采样 |
| | `surface_light_reflections.fs` | 屏幕空间反射 SSR：`traceScreenSpaceRay()`（McGuire & Mara 2014 算法），深度缓冲光线步进 |
| **8. 着色模型 (Shading Models)** | `surface_shading_parameters.fs` | 着色前准备：`computeShadingParams()`（TBN/位置/视线构建）、`prepareMaterial()`（法线贴图应用、NoV / 反射向量） |
| | `surface_shading_lit.fs` | 光照材质总调度器：`evaluateMaterial()` 依次调用 IBL + 方向光 + 点光/聚光 → 派发着色模型 |
| | `surface_shading_lit_custom.fs` | 自定义着色桥接：打包 LightData/ShadingData → 调用用户 `surfaceShading()` |
| | `surface_shading_unlit.fs` | 无光照材质：baseColor + emissive，可选阴影乘数（用于 AR 阴影接收面） |
| | `surface_shading_reflections.fs` | 反射专用通道：启用 SSR 时调用 `surface_light_reflections.fs` |
| | `surface_shading_model_standard.fs` | 标准 PBR：Cook-Torrance 镜面 + Lambert/Burley 漫反射 + 清漆层 + 绸缎 sheen + 各向异性 + 能量补偿 |
| | `surface_shading_model_cloth.fs` | 布料模型：Charlie NDF + Neubelt visibility + Burley 漫反射，可选次表面散射，彩色半影 |
| | `surface_shading_model_subsurface.fs` | 次表面散射模型：GGX 镜面 + 漫反射 + 近似 BTDF（球形高斯前向/后向散射，基于厚度） |
| **9. 阴影 (Shadowing)** | `surface_shadowing.glsl` | 光源空间位置计算：`computeLightSpacePosition()`，各向异性法线偏移防 z-fighting |
| | `surface_shadowing.fs` | 阴影采样库：`ShadowSample_PCF_Hard/Low()`、`ShadowSample_DPCF()`、`ShadowSample_PCSS()`、EVSM/ELVSM，级联选择，接触阴影 |
| **10. 后处理管线 (Post-Process Pipeline)** | `post_process_inputs.vs` | 后处理顶点输入结构 `PostProcessVertexInputs`（normalizedUV, position, 自定义属性） |
| | `post_process_inputs.fs` | 后处理片段输出结构 `PostProcessInputs`（最多 8 个颜色输出 `FRAG_OUTPUT0..7`，可选深度） |
| | `post_process_getters.vs` | 后处理顶点着色器最小获取器：`getPosition()` |
| | `post_process_main.vs` | 后处理顶点着色器主入口：归一化 UV → 调用用户 `postProcessVertex()` → GL/VK 裁剪坐标适配 |
| | `post_process_main.fs` | 后处理片段着色器主入口：调用用户 `postProcess()` → 可选 sRGB 模拟 → 写入多渲染目标 |

管线记忆路径：`common_defines/math` → `surface_types/varyings/instancing` → `material_inputs` → `surface_getters` → `surface_main.{vs,fs}` → `shading_parameters` → `brdf/material` → `lighting` → `shading_model` → `fog/ssao/dithering/vignette` → 输出；后处理走独立 `post_process_*` 子管线。

# Filament 材质系统学习笔记（LearnFM）

> 本文档汇总 Filament（Google 开源 PBR 引擎）材质相关的学习问答，按主题整理，便于回顾。
> 后续问答将持续追加到对应章节或新增章节。

***

## 一、Filament 材质核心结构

Filament 的材质系统分为三层：

```
filamat::MaterialBuilder  →  filament::Material  →  filament::MaterialInstance
      （编译层/工具链）        （运行时模板）           （运行时实例）
```

### 1.1 `filamat::MaterialBuilder`

负责把 Shader 源码编译成二进制包 `filamat::Package`。

**核心 API**（来自 `filamat/MaterialBuilder.h`）

| 方法                              | 作用                                                                      |
| ------------------------------- | ----------------------------------------------------------------------- |
| `.name(...)`                    | 设置材质名称                                                                  |
| `.material(code)`               | 传入 Fragment Shader（必须包含 `void material(inout MaterialInputs material)`） |
| `.materialVertex(code)`         | 可选，传入自定义 Vertex Shader                                                  |
| `.shading(Shading)`             | 着色模型：`UNLIT` / `LIT` / `SUBSURFACE` 等                                   |
| `.parameter(name, UniformType)` | 声明 uniform 参数                                                           |
| `.parameter(name, SamplerType)` | 声明纹理采样器参数（如 `SAMPLER_2D`）                                               |
| `.require(VertexAttribute)`     | 声明需要的顶点属性（如 `UV0`）                                                      |
| `.targetApi(TargetApi)`         | 目标图形 API：`OPENGL` / `VULKAN` / `METAL` / `WEBGPU`                       |
| `.platform(Platform)`           | 目标平台：`DESKTOP` / `MOBILE` / `ALL`                                       |
| `.build(JobSystem&)`            | 执行编译，返回 `Package`                                                       |

**创建流程示例**

```cpp
filamat::MaterialBuilder builder;
builder
    .name("BakedTexture")
    .material(R"FILAMENT(
        void material(inout MaterialInputs material) {
            prepareMaterial(material);
            material.baseColor = texture(materialParams_albedo, getUV0());
        }
    )FILAMENT")
    .shading(filament::Shading::UNLIT)
    .parameter("albedo", filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
    .require(filament::VertexAttribute::UV0)
    .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
    .platform(filamat::MaterialBuilder::Platform::DESKTOP);

filamat::Package package = builder.build(state.engine->getJobSystem());
if (!package.isValid()) { /* 编译失败 */ }
```

### 1.2 `filament::Material`

运行时材质模板，不保存具体参数值，只保存 Shader 程序、渲染状态（Blend、Cull、Depth 等）和参数元数据。是创建 `MaterialInstance` 的工厂。

**核心 API**（来自 `filament/Material.h`）

| 成员/方法                                                     | 说明                             |
| --------------------------------------------------------- | ------------------------------ |
| `class Builder`                                           | 内部构建器                          |
| `Builder::package(data, size)`                            | 把 `filamat::Package` 的二进制数据喂进去 |
| `Builder::build(Engine&)`                                 | 在指定 Engine 上创建 `Material*`     |
| `createInstance(name)`                                    | 创建材质实例                         |
| `getShading()` / `getBlendingMode()` / `getCullingMode()` | 查询着色/混合/剔除模式                   |
| `getParameterCount()` / `getParameters(...)`              | 查询参数列表                         |
| `hasParameter(name)` / `isSampler(name)`                  | 查询参数是否存在/是否为纹理                 |
| `compile(...)`                                            | 异步预编译指定变体，避免运行时卡顿              |

**创建流程示例**

```cpp
state.mat = Material::Builder()
    .package(package.getData(), package.getSize())
    .build(*state.engine);

state.matInstance = state.mat->createInstance();
```

### 1.3 `filament::MaterialInstance`

可渲染实例，真正绑定到 `Renderable` 上。同一个 `Material` 可创建多个 `MaterialInstance`，各自拥有不同参数值，但共享同一份 Shader 程序。

**核心 API**（来自 `filament/MaterialInstance.h`）

| 方法                                            | 说明                              |
| --------------------------------------------- | ------------------------------- |
| `setParameter(name, value)`                   | 设置 uniform（float、float4、mat4 等） |
| `setParameter(name, texture, sampler)`        | 绑定纹理 + 采样器状态                    |
| `setParameter(name, RgbType, float3)`         | 设置颜色（自动处理 sRGB/Linear）          |
| `setConstant(name, value)`                    | 覆盖 specialization constant      |
| `setCullingMode(...)` / `setDoubleSided(...)` | 覆盖默认剔除/双面状态                     |
| `setMaskThreshold(...)`                       | 覆盖 alpha mask 阈值                |
| `setScissor(...)` / `unsetScissor()`          | 设置裁剪矩形                          |
| `compile(...)`                                | 针对本实例覆盖的 constant 预编译变体         |

**使用示例**

```cpp
state.matInstance->setParameter("albedo", state.tex,
    TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                   TextureSampler::MagFilter::LINEAR));
```

### 1.4 整体工作流程

```
1) 编写 Shader 代码 + 声明参数/属性
            ↓
2) filamat::MaterialBuilder
   .material(code) / .shading(...) / .parameter(...)
   .build() → filamat::Package（二进制 Shader 包）
            ↓
3) filament::Material::Builder
   .package(data, size).build(engine) → Material*
   （上传到 GPU / 创建 Pipeline State / 解析参数元数据）
            ↓
4) Material::createInstance() → MaterialInstance*
   设置实际参数：setParameter("albedo", texture, sampler)
   设置覆盖状态：setCullingMode / setDoubleSided 等
            ↓
5) RenderableManager::Builder(...).material(instance).build()
   把 MaterialInstance 赋给可渲染实体，提交引擎渲染
```

**关键设计点**

- **编译期与运行时分离**：`filamat` 可离线（`matc` 工具）或运行时调用，最终产物都是 `Package`。
- **Material 是模板**：创建开销大（涉及 Shader 编译/Pipeline 创建），应复用。
- **MaterialInstance 轻量**：只改 uniform/纹理指针，同材质可大量实例化。
- **变体（Variant）**：Filament 会根据光照、阴影、雾等组合生成多个 Shader 变体。`Material::compile()` 可异步预热。

***

## 二、bakedTexture 与 UNLIT

### 2.1 `UNLIT`

`UNLIT` 是 Filament 的着色模型枚举值（`MaterialEnums.h`）：

> `UNLIT` — *no lighting applied, emissive possible*

表示该材质**完全不受场景光源影响**。Fragment Shader 里 `baseColor` 直接输出到屏幕，不计算漫反射、镜面反射、阴影等。适合：

- UI/HUD 元素
- 全息投影、特效
- 颜色信息已离线算好的场景（如烘焙光照）

### 2.2 `bakedTexture`

这不是 Filament 内置关键字，而是用户自定义的材质命名习惯。含义是 **"烘焙纹理"**：

把所有光照、阴影、AO 等信息事先离线渲染到一张纹理上。运行时材质只做一件事——**采样这张纹理并输出**，因此天然配合 `UNLIT` 模型使用。GPU 开销极低，效果却可以很精致。

***

## 三、能否获取最终生成的 Shader 源码？

**不能直接通过公开 API 拿到** **`std::string`。**

Filament 的设计输出是二进制 `Package`，内部变体可能是 SPIRV、MSL 或压缩后的 GLSL，没有提供返回源码字符串的接口。

**调试手段**

| API                                     | 作用                             |
| --------------------------------------- | ------------------------------ |
| `MaterialBuilder::printShaders(true)`   | 把生成的 GLSL **打印到** **`stdout`** |
| `MaterialBuilder` 内部 `mSaveRawVariants` | 把每个变体的原始 GLSL **写入本地文本文件**     |

如果想在程序内捕获字符串，只能临时重定向 `stdout`，或修改 Filament 源码。

***

## 四、多 Primitive / 多材质绑定（例：立方体六面六种材质）

Filament 的 `Renderable` 由多个 **Primitive（子网格）** 组成，每个 primitive 可以独立指定 geometry 范围和 material。

**实现方式**：把立方体拆成 6 组索引，每组对应一个面，在 `RenderableManager::Builder` 里逐个绑定。

```cpp
// 6 个面，每面对应一种 MaterialInstance
MaterialInstance* faceMats[6] = { ... };

// 一个立方体 36 个索引（6 面 × 2 三角 × 3 顶点）
// 每面 6 个索引，offset 依次是 0, 6, 12, 18, 24, 30
RenderableManager::Builder(6)   // 6 个 primitive
    .boundingBox({{-1,-1,-1}, {1,1,1}})
    // 面 0
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
              vertexBuffer, indexBuffer, 0, 6)
    .material(0, faceMats[0])
    // 面 1
    .geometry(1, RenderableManager::PrimitiveType::TRIANGLES,
              vertexBuffer, indexBuffer, 6, 6)
    .material(1, faceMats[1])
    // 面 2 ... 以此类推
    .geometry(2, ..., 12, 6)
    .material(2, faceMats[2])
    // ...
    .build(*engine, cubeEntity);
```

**要点**

- `Builder(count)` 的 `count` 是 primitive 数量。
- `geometry(index, type, vertices, indices, offset, count)` 中 `index` 是 primitive 序号；`offset`/`count` 是该面在 `IndexBuffer` 里的起始位置和索引数。
- `material(index, matInstance)` 把材质绑定到对应序号的 primitive。
- 6 个面可共享同一个 `VertexBuffer`/`IndexBuffer`，只需在索引数据里按面顺序排列。
- 最终仍是一个 `Entity`、一个 `Renderable`，但底层会发 6 个 draw call。

***

## 五、Shading 着色模型详解

Filament 支持的着色模型定义在 `filament/MaterialEnums.h`：

| 枚举                    | 含义                                     | 典型用途          |
| --------------------- | -------------------------------------- | ------------- |
| `UNLIT`               | 不受光照影响，直接输出 `baseColor`                | UI、全息、烘焙场景    |
| `LIT`                 | **默认标准 PBR**，使用 Metallic-Roughness 工作流 | 金属、石头、塑料等常规物体 |
| `SUBSURFACE`          | 次表面散射模型                                | 蜡、玉石、皮肤、树叶    |
| `CLOTH`               | 布料光照模型                                 | 衣服、绒布、织物      |
| `SPECULAR_GLOSSINESS` | 传统 Specular-Glossiness 工作流（遗留兼容）       | 旧资源迁移         |

- `LIT` 是主流选择，需要 `normal` 属性（除非面是平的），会响应场景中的 DirectionalLight、PointLight、IBL、阴影等。
- `SUBSURFACE` 在 `LIT` 基础上增加了次表面散射项，让光线能"穿透"物体表面。
- `CLOTH` 模拟布料特有的各向异性边缘高光。
- `SPECULAR_GLOSSINESS` 是为了兼容旧美术流程，新项目建议用 `LIT`（Metallic-Roughness）。

***

## 六、`faceMats[6]` 的具体操作（完整示例）

核心思路：**同一个** **`Material`** **可以** **`createInstance()`** **多次**，每个实例独立设置参数（纹理、颜色等），然后分别绑定到 6 个 primitive。

```cpp
// 1) 创建 6 个 MaterialInstance（共享同一份 Shader）
MaterialInstance* faceMats[6];
for (int i = 0; i < 6; ++i) {
    faceMats[i] = state.mat->createInstance();
    faceMats[i]->setParameter("albedo", tex[i],
        TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                       TextureSampler::MagFilter::LINEAR));
}

// 2) Builder(6) 绑定 6 个 primitive
// IndexBuffer 中每面 6 个索引，offset 依次为 0, 6, 12, 18, 24, 30
RenderableManager::Builder(6)
    .boundingBox({{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}})
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, state.vb, state.ib, 0,  6)
    .material(0, faceMats[0])
    .geometry(1, RenderableManager::PrimitiveType::TRIANGLES, state.vb, state.ib, 6,  6)
    .material(1, faceMats[1])
    .geometry(2, RenderableManager::PrimitiveType::TRIANGLES, state.vb, state.ib, 12, 6)
    .material(2, faceMats[2])
    .geometry(3, RenderableManager::PrimitiveType::TRIANGLES, state.vb, state.ib, 18, 6)
    .material(3, faceMats[3])
    .geometry(4, RenderableManager::PrimitiveType::TRIANGLES, state.vb, state.ib, 24, 6)
    .material(4, faceMats[4])
    .geometry(5, RenderableManager::PrimitiveType::TRIANGLES, state.vb, state.ib, 30, 6)
    .material(5, faceMats[5])
    .build(*state.engine, state.renderable);
```

**要点**

- `createInstance()` 开销很小，6 个实例共享同一份 GPU Pipeline/Shader，只是 uniform/sampler 不同。
- `IndexBuffer` 里的索引必须按面顺序排列（每面 6 个索引），`offset` 和 `count` 才能正确切分。
- 如果 6 个面不仅纹理不同，**着色模型也不同**（比如一面金属、一面布料），就需要编译 **多个** **`Material`**（不是多个 Instance）。同一个 `Material` 的实例只能切换参数，不能切换着色模型。

***

## 七、一个 Plane（2 个三角）赋予两种材质

把平面拆成 **2 个 primitive**，每个三角一个 primitive，分别绑定材质。

```cpp
MaterialInstance* matA = state.mat->createInstance();
matA->setParameter("albedo", texA, TextureSampler(...));

MaterialInstance* matB = state.mat->createInstance();
matB->setParameter("albedo", texB, TextureSampler(...));

// 假设 IndexBuffer 中：三角 A 占索引 0-2，三角 B 占索引 3-5
RenderableManager::Builder(2)
    .boundingBox({{-1,0,-1}, {1,0,1}})
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
              state.vb, state.ib, 0, 3)
    .material(0, matA)
    .geometry(1, RenderableManager::PrimitiveType::TRIANGLES,
              state.vb, state.ib, 3, 3)
    .material(1, matB)
    .build(*state.engine, state.renderable);
```

**替代方案**：如果仅需纹理不同、渲染状态相同，可把两张图合成 **Texture Atlas（图集）**，用 UV 偏移采样不同区域，只需 1 个 primitive、1 个 draw call。若需要不同的 blending/culling/depth 状态，则必须拆 primitive。

***

## 八、Filament 有毛发 / 卡通（3渲2）着色器吗？

**没有内置专门的毛发或卡通着色模型。**

Filament 核心只有 `UNLIT` / `LIT` / `SUBSURFACE` / `CLOTH` / `SPECULAR_GLOSSINESS` 五种。官方未提供：

- 毛发专用模型（如 Marschner 散射）
- 卡通/赛璐璐模型（如 Toon/Cel Shading、内置轮廓线）

但社区有第三方方案（如 DAZ3D 的 **FilaToon**），那是基于 Filament 写的自定义材质，非引擎自带。

***

## 九、可以自定义 Shader 吗？能写熟悉的 GLSL 吗？

**可以，但不是裸 GLSL。** Filament 允许在它的 Shader 框架里插入代码，由 `filamat::MaterialBuilder` 编译成完整 Shader。

### 自定义入口

| 插入点                        | 入口函数                                                       | 用途                            |
| -------------------------- | ---------------------------------------------------------- | ----------------------------- |
| Fragment（必须）               | `void material(inout MaterialInputs material)`             | 计算材质属性（baseColor、roughness 等） |
| Vertex（可选）                 | `void materialVertex(inout MaterialVertexInputs material)` | 自定义顶点变换、UV 扰动                 |
| Custom Surface（可选，仅 `LIT`） | `vec3 surfaceShading(...)`                                 | 完全替换每盏光的 BRDF 计算              |

### 卡通渲染（3渲2）示例

**方式 A：UNLIT + 手写光照阶梯**

```cpp
builder.shading(Shading::UNLIT).material(R"FILAMENT(
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        float4 tex = texture(materialParams_albedo, getUV0());
        
        float3 N = getWorldNormal();
        float3 L = normalize(float3(1.0, 1.0, 0.5));
        float NoL = max(dot(N, L), 0.0);
        float band = (NoL > 0.6) ? 1.0 : (NoL > 0.2) ? 0.5 : 0.2;
        
        float3 V = normalize(getWorldCameraPosition() - getWorldPosition());
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        
        material.baseColor.rgb = tex.rgb * band + float3(rim * 0.3);
        material.baseColor.a = tex.a;
    }
)FILAMENT");
```

**方式 B：LIT +** **`customSurfaceShading(true)`**

```cpp
builder.shading(Shading::LIT)
       .customSurfaceShading(true)
       .material(R"FILAMENT(
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = texture(materialParams_albedo, getUV0());
    }
    
    vec3 surfaceShading(const MaterialInputs materialInputs,
                        const ShadingData shadingData,
                        const LightData lightData) {
        float NoL = max(dot(shadingData.normal, lightData.l), 0.0);
        float band = (NoL > 0.5) ? 1.0 : 0.4;
        return materialInputs.baseColor.rgb * band * lightData.colorIntensity.rgb;
    }
)FILAMENT");
```

### 毛发渲染思路

- 用大量 **alpha-masked quad（hair cards）** + `BlendingMode::MASKED` 渲染发丝。
- 在 Fragment Shader 里用 **Kajiya-Kay 或 Marschner 近似模型** 计算切线方向高光（需传入切线属性）。
- 注意排序：Filament 对透明物体按 per-mesh 排序，hair cards 交叉严重时可能需要拆分多个 mesh 或接受少量排序瑕疵。

### 轮廓线（Outline）

Filament **没有内置后处理轮廓线**。常见做法：

1. **Geometry Shell**：创建第二个 Renderable，材质为 `CullingMode::FRONT`（只渲染背面），顶点沿法线挤出并染黑。
2. **后处理**：需要自己写额外 Pass（Filament 的 PostProcess 接口较封闭，一般需改引擎或在外部帧缓冲上做）。

### 限制与注意

- 不能写裸 `main()` 或 `gl_Position`，必须按 Filament 入口函数写。
- 可用内置函数：`getUV0()`、`getWorldNormal()`、`getWorldPosition()`、`getWorldCameraPosition()` 等。
- 参数需通过 `.parameter()` 声明，在 Shader 里以 `materialParams_xxx` 访问。
- 最终代码会被 Filament 注入 UBO、Sampler、Lights Loop、Fog 等模板代码，编译成 SPIRV/GLSL/MSL。

***

## 十、`setParameter` 参数名与 `MaterialInputs` 内置字段

### 关键区分

| 概念                             | 由谁决定                                  | 用法                |
| ------------------------------ | ------------------------------------- | ----------------- |
| `setParameter("xxx", ...)` 的名字 | **你自己**在 `.parameter("xxx", ...)` 里声明 | C++ 运行时传值，名字完全自定义 |
| `material.xxx` 的字段名            | **Filament 固定**（见下表）                  | Shader 里给材质属性赋值   |

你代码里的 `"albedo"` 只是自定义的纹理参数名，你在 Shader 里把它赋给了 `material.baseColor`。改叫 `"diffuseTex"`、`"mainTexture"` 都可以。

### `MaterialInputs` 内置字段（来自 `filament/MaterialEnums.h`）

| 字段名                     | 类型       | 适用着色模型                        | 含义                       |
| ----------------------- | -------- | ----------------------------- | ------------------------ |
| `baseColor`             | `float4` | 所有                            | 基础颜色（RGBA）               |
| `roughness`             | `float`  | `LIT`/`SUBSURFACE`/`CLOTH` 等  | 粗糙度（0=镜面，1=漫反射）          |
| `metallic`              | `float`  | 除 `UNLIT`、`CLOTH` 外           | 金属度（0=非金属，1=纯金属）         |
| `reflectance`           | `float`  | 除 `UNLIT`、`CLOTH` 外           | 反射率（F0 基础值，默认 0.5）       |
| `ambientOcclusion`      | `float`  | `LIT`（除 `SUBSURFACE`、`CLOTH`） | 环境光遮蔽强度                  |
| `clearCoat`             | `float`  | `LIT`（除 `SUBSURFACE`、`CLOTH`） | 清漆层强度（汽车漆、钢琴漆）           |
| `clearCoatRoughness`    | `float`  | 同上                            | 清漆层粗糙度                   |
| `clearCoatNormal`       | `float3` | 同上                            | 清漆层法线（可独立）               |
| `anisotropy`            | `float`  | 同上                            | 各向异性强度（拉丝金属、头发高光）        |
| `anisotropyDirection`   | `float3` | 同上                            | 各向异性方向（切线空间）             |
| `sheenColor`            | `float3` | `LIT`（除 `SUBSURFACE`）         | 光泽层颜色（天鹅绒边缘光）            |
| `sheenRoughness`        | `float3` | `LIT`（除 `SUBSURFACE`、`CLOTH`） | 光泽层粗糙度                   |
| `subsurfaceColor`       | `float3` | `SUBSURFACE`、`CLOTH`          | 次表面散射颜色                  |
| `subsurfacePower`       | `float`  | `SUBSURFACE`                  | 次表面散射强度/幂                |
| `thickness`             | `float`  | `SUBSURFACE`                  | 次表面厚度                    |
| `specularColor`         | `float3` | `SPECULAR_GLOSSINESS`         | 镜面颜色（旧工作流）               |
| `glossiness`            | `float`  | `SPECULAR_GLOSSINESS`         | 光泽度（旧工作流）                |
| `emissive`              | `float4` | 所有                            | 自发光颜色（直接叠加，不受光照影响）       |
| `normal`                | `float3` | 除 `UNLIT` 外                   | 法线（切线空间）                 |
| `bentNormal`            | `float3` | 除 `UNLIT` 外                   | 弯曲法线（用于间接光遮蔽）            |
| `postLightingColor`     | `float4` | 所有                            | 光照后叠加颜色（如描边、染色）          |
| `postLightingMixFactor` | `float`  | 所有                            | `postLightingColor` 混合因子 |
| `absorption`            | `float3` | 所有                            | 光线被吸收的量（体积/折射）           |
| `transmission`          | `float`  | 所有                            | 透光率（0=不透明，1=全透明）         |
| `ior`                   | `float`  | 所有                            | 折射率（Index of Refraction） |
| `dispersion`            | `float`  | 所有                            | 色散                       |
| `microThickness`        | `float`  | 所有                            | 薄层厚度（薄膜干涉）               |
| `specularFactor`        | `float`  | `LIT`（除 `SUBSURFACE`、`CLOTH`） | 镜面强度因子                   |
| `specularColorFactor`   | `float3` | 同上                            | 镜面颜色因子                   |
| `shadowStrength`        | `float`  | 所有                            | 接收阴影的强度（0-1）             |
| `clipSpaceTransform`    | `mat4`   | Vertex Shader                 | 裁剪空间变换矩阵                 |

### 常见用法示例（`LIT` 模型）

```cpp
// C++ 端声明参数
builder
    .shading(Shading::LIT)
    .parameter("baseColor", UniformType::FLOAT4)
    .parameter("roughness", UniformType::FLOAT)
    .parameter("metallic", UniformType::FLOAT)
    .parameter("normalMap", SamplerType::SAMPLER_2D);

// C++ 端设置值
matInstance->setParameter("baseColor", float4{1.0f, 0.0f, 0.0f, 1.0f});
matInstance->setParameter("roughness", 0.4f);
matInstance->setParameter("metallic", 0.8f);
matInstance->setParameter("normalMap", normalTex, TextureSampler(...));

// Shader 端使用
void material(inout MaterialInputs material) {
    prepareMaterial(material);
    material.baseColor = materialParams_baseColor;
    material.roughness = materialParams_roughness;
    material.metallic  = materialParams_metallic;
    material.normal    = texture(materialParams_normalMap, getUV0()).xyz * 2.0 - 1.0;
}
```

***

## 十一、同一个 Renderable 混用不同着色模型（如 `UNLIT` + `LIT`）

**必须编译两个** **`Material`，不能靠** **`createInstance()`** **解决。**

`shading(Shading::UNLIT)` vs `shading(Shading::LIT)` 是在 `filamat::MaterialBuilder` 编译期就固定的，决定了最终生成的 GPU Shader 代码。`MaterialInstance` 只能切换 uniform/纹理值，**不能切换着色模型**。

```cpp
// 1) 编译两个不同着色模型的 Material
filamat::MaterialBuilder builderUnlit;
builderUnlit.name("UnlitMat").shading(Shading::UNLIT) /* ... */;
Material* matUnlit = Material::Builder().package(...).build(*engine);

filamat::MaterialBuilder builderLit;
builderLit.name("LitMat").shading(Shading::LIT)
    .parameter("roughness", UniformType::FLOAT)
    .parameter("metallic", UniformType::FLOAT) /* ... */;
Material* matLit = Material::Builder().package(...).build(*engine);

// 2) 分别创建实例
MaterialInstance* instUnlit = matUnlit->createInstance();
MaterialInstance* instLit   = matLit->createInstance();
instLit->setParameter("roughness", 0.4f);
instLit->setParameter("metallic", 0.8f);

// 3) 绑定到同一个 Renderable 的不同 primitive
RenderableManager::Builder(2)
    .geometry(0, ..., 0, 3)
    .material(0, instUnlit)
    .geometry(1, ..., 3, 3)
    .material(1, instLit)
    .build(*engine, entity);
```

同一个 Renderable 的不同 primitive **完全可以绑定来自不同** **`Material`** **的实例**。唯一要求是这些 `Material` 的 feature level 不能高于 Engine 当前级别。

***

## 十二、`primitive` 的本质：它就是 Mesh 吗？还能是什么？

**`primitive`** **不等于"Mesh 网格"这个抽象概念，它更接近"一次 draw call 所需的数据包"。**

一个 primitive = **一段 geometry 数据（VertexBuffer + IndexBuffer 范围）+ 一个 MaterialInstance**。

它可以是：

| 形态                        | 说明                                                                   |
| ------------------------- | -------------------------------------------------------------------- |
| **一个完整模型**                | 比如一个立方体、一个球，单独成一个 primitive                                          |
| **一个模型的子部分（submesh）**     | 如角色身体分为头、躯干、四肢，每个部分一个 primitive，各自绑不同材质                              |
| **一个面/一片区域**              | 如立方体的某个面、地形的一块瓦片                                                     |
| **一条线**                   | `PrimitiveType::LINES` / `LINE_STRIP`，用于画线框、Gizmo、轨迹                 |
| **一个点**                   | `PrimitiveType::POINTS`，用于粒子、点云                                      |
| **一个 strip**              | `TRIANGLE_STRIP` / `LINE_STRIP`，用于高效表达连续曲面或线                         |
| **一个 billboard/sprite**   | 一个面朝相机的四边形，配合 `UNLIT` + 透明纹理                                         |
| **纯程序生成（attribute-less）** | 不绑定 VertexBuffer 属性，靠 Vertex Shader 用 `getVertexIndex()` 生成全屏四边形、射线等 |

来自 `backend/DriverEnums.h` 的 `PrimitiveType`：

```cpp
enum class PrimitiveType : uint8_t {
    POINTS         = 0,    // 点
    LINES          = 1,    // 独立线段
    LINE_STRIP     = 3,    // 连续线
    TRIANGLES      = 4,    // 独立三角
    TRIANGLE_STRIP = 5     // 三角带
};
```

所以 `RenderableManager::Builder(count)` 的 `count` 本质上是：**这个 Entity 需要发多少次 draw call**。每个 draw call 就是一个 primitive，各自可以有独立的 geometry 范围和材质。

***

## 十三、Attribute-less / Procedural Primitive（无顶点数据渲染）

Filament 支持完全不绑定顶点属性的 procedural rendering，利用 `gl_VertexID`（在 Shader 中通过 `getVertexIndex()` 访问）来生成位置、UV 等数据。

### C++ 端 Builder 配置

```cpp
// 1) 创建 VertexBuffer：声明顶点数量，但不创建任何 buffer/attribute
VertexBuffer* vb = VertexBuffer::Builder()
    .vertexCount(6)   // 这个 primitive 会画 6 个顶点
    .bufferCount(0)   // 0 个属性 buffer
    .build(*engine);

// 不需要 vb->setBufferAt()，因为没有 buffer

// 2) 绑定到 Renderable（非索引绘制）
RenderableManager::Builder(1)
    .boundingBox({{-1,0,-1}, {1,0,1}})
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, 0, 6)
    .material(0, matInstance)
    .build(*engine, entity);
```

### Shader 端写法

```cpp
builder
    .variable(filamat::MaterialBuilder::Variable::CUSTOM0, "procUV")
    .materialVertex(R"FILAMENT(
        void materialVertex(inout MaterialVertexInputs material) {
            int vid = getVertexIndex();  // 0,1,2,3,4,5
            
            // 程序化生成一个 Quad 的 6 个顶点（2 个三角）
            float2 pos[6] = float2[](
                float2(-1, -1), float2( 1, -1), float2(-1,  1),
                float2(-1,  1), float2( 1, -1), float2( 1,  1)
            );
            float2 uv[6] = float2[](
                float2(0, 0), float2(1, 0), float2(0, 1),
                float2(0, 1), float2(1, 0), float2(1, 1)
            );
            
            // 设置世界空间位置（假设在 XZ 平面）
            material.worldPosition = float3(pos[vid].x, 0.0, pos[vid].y);
            
            // 通过 variable 把 UV 传给 fragment
            material.procUV = uv[vid];
        }
    )FILAMENT")
    .material(R"FILAMENT(
        void material(inout MaterialInputs material) {
            prepareMaterial(material);
            material.baseColor = float4(variable_procUV, 0.0, 1.0);
        }
    )FILAMENT");
```

### 关键点

| 配置项                                             | 说明                                              |
| ----------------------------------------------- | ----------------------------------------------- |
| `VertexBuffer::Builder().bufferCount(0)`        | 不声明任何顶点属性                                       |
| `geometry(index, TRIANGLES, vb, offset, count)` | 非索引绘制，`offset`/`count` 是顶点偏移和数量                 |
| `getVertexIndex()`                              | Shader 中获取 `gl_VertexID`，从 0 开始递增               |
| `material.worldPosition`                        | 在 `materialVertex()` 中设置世界空间位置                  |
| `variables`                                     | 因为没有顶点属性，需要用 `variable` 把数据从 vertex 传到 fragment |

### 限制（来自 `filament/RenderableManager.h`）

- 需要 `FEATURE_LEVEL_1` 或更高（GLES2 没有 `gl_VertexID`）。
- 不支持 skinning 和 morphing。

### 典型用途

- **全屏后处理 Quad**：画 3 个顶点的大三角覆盖整个视口
- **粒子 Billboard**：根据 `vertexIndex` 生成始终面朝相机的四边形
- **Gizmo/射线**：用 `LINES` 程序生成线段
- **地形/水面 Tile**：根据 `vertexIndex` 计算格子位置，配合高度图 displacement

***

## 十四、`ogl.h` 与 Filament 的架构对比

### `ogl.h` 的架构画像（来自 `e:/MX/MO/ogl.h`）

| 设计点        | 实现方式                                                                                                                                    |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| **Shader** | 一个巨型内嵌 Shader（\~250 行 Vertex + \~400 行 Fragment），同时处理 PBR/Morph/Skinning/Unlit/Specular-Glossiness，靠 `flag` 和 `work_flow` uniform 运行时分支 |
| **API**    | 直接 OpenGL 3.3 Core（glad），`glUniform*` / `glBindTexture` 手动调用                                                                            |
| **渲染模式**   | 即时模式（Immediate Mode）——每帧手动 `setMat4`、`bind`、然后 `render()`                                                                               |
| **场景管理**   | 无 ECS / 无场景图。`Model` 自己遍历 nodes/meshes 发 draw call                                                                                      |
| **后处理**    | 有 `FBO` 类，但无自动管线，需手动管理多 Pass                                                                                                            |
| **光照**     | Fragment Shader 里硬编码 4 个点光源（`lp[4]`）                                                                                                    |
| **跨平台**    | 仅 OpenGL 3.3，无 Vulkan/Metal 抽象                                                                                                          |

这套设计非常**实用主义**：单头文件搞定 PBR + 骨骼 + Morph + GLTF，开发迭代极快。对于个人工具或垂直应用（模型查看器、ShaderToy Recorder、壁纸引擎），已覆盖 90% 需求。

### 为什么"扩展"达不到 Filament 水平

Filament 的强大不是某个功能点，而是**底层架构**。以下是 `ogl.h` 若要追赶，必须**重构而非扩展**的点：

#### 1. 多后端（OpenGL → Vulkan/Metal/WebGPU）

`ogl.h` 所有调用都是裸 `gl*` 函数。要支持 Vulkan 需重写所有 buffer/texture/shader/pipeline 创建逻辑（`VkBuffer`、`VkImage`、`VkPipeline` 与 OpenGL 概念完全不同），引入 Command Buffer、Synchronization、Descriptor Set。这等于重写整个 GPU 抽象层。Filament 从设计之初就有 `backend::Driver` 接口，上层只和抽象交互。

#### 2. Shader 变体系统（Variant）

`ogl.h` 的一个巨型 Shader 用 `if ((flag & (1<<N)) != 0)` 运行时分支，GPU 开销重（register pressure 高、warp divergence）。Filament 的 `MaterialBuilder` 在编译期根据 feature 组合生成独立的 Shader 变体。`ogl.h` 若要实现，需引入完整的 Shader 预处理/编译管线（类似 `matc`），不再是"加几行代码"能解决的。

#### 3. 渲染管线架构：Immediate vs Retained

`ogl.h` 是即时模式：每帧手动发 draw call。Filament 是保留模式：初始化时创建 Entity + Renderable + Material，每帧只更新 Transform/Camera，引擎自动处理剔除、排序、批处理、Shadow Pass、Color Pass、Post-Processing。`ogl.h` 若要达到此水平，需引入 **ECS + 渲染命令队列 + RenderGraph/FrameGraph**。

#### 4. 光照与后处理

`ogl.h` 硬编码 4 个点光源。Filament 支持动态多光源（Clustered Shading）、IBL（CubeMap 环境光照）、级联阴影（CSM）、完整后处理管线（Bloom、TAA、FXAA、Tone Mapping、SSAO、DOF）。这些硬塞进 `ogl.h` 的一个 Fragment Shader 会变成数千行怪物，性能也无法接受。

### 客观评估：`ogl.h` 能扩展到什么程度？

| 功能方向                    | 扩展可行性  | 难度                              |
| ----------------------- | ------ | ------------------------------- |
| 更多 PBR 参数（清漆、各向异性）      | ⭐⭐⭐ 可行 | 改巨型 Shader，加 uniform            |
| 更多光源（8\~16 个）           | ⭐⭐⭐ 可行 | 改 Shader + UBO                  |
| 简单的阴影贴图（单方向光）           | ⭐⭐⭐ 可行 | 加 FBO + 深度 Pass + Shadow Map 采样 |
| 简单的 IBL（预过滤 CubeMap）    | ⭐⭐ 较困难 | 需要 CubeMap 预过滤工具 + Shader 改造    |
| 多后端（Vulkan/Metal）       | ⭐ 极困难  | 重写底层                            |
| Shader 变体编译系统           | ⭐ 极困难  | 需要 Shader 预处理/编译管线              |
| 自动场景管理 + 剔除             | ⭐ 极困难  | 需要 ECS + RenderGraph            |
| 完整后处理管线（Bloom/SSAO/TAA） | ⭐⭐ 较困难 | 每个效果都要写独立 Pass，需管线管理            |

### 建议

1. **`ogl.h`** **继续做你的"瑞士军刀"**\
   你的使用场景（模型查看器、ShaderToy、UI 工具、壁纸引擎）完全在它的舒适区。单头文件、内嵌 Shader、手动控制，开发迭代极快。Filament 对这些场景反而太重（学习曲线陡、编译慢、体积大）。
2. **不要试图把** **`ogl.h`** **扩展成 Filament**\
   两者目标不同。`ogl.h` 是"我控制的渲染工具"，Filament 是"跨平台工业级引擎"。若需要跨平台移动端/复杂 PBR 场景/高级后处理，直接用 Filament 或 [bgfx](https://github.com/bkaradzic/bgfx)。
3. **可以"借力"而非"重写"**
   - 把硬编码光源改成 UBO 存储动态光源（上限 16\~32），足够大多数场景。
   - 保留 `FBO` 类，但设计一个简单的 `RenderPass` 链（场景 → FBO → 后处理全屏 Quad）。
   - 把巨型 Shader 拆成 3\~4 个专用 Shader（`pbr.glsl`、`unlit.glsl`、`skinned.glsl`），根据材质特性选程序，这是向 Filament 变体思想靠拢的最小成本改进。

***

## 十五、Filament 的动画支持与物理引擎结合

### Filament 有动画吗？

**有 Skinning / Morphing 的 GPU 支持，但没有内置动画播放器。**

Filament 提供了：

- `SkinningBuffer` + `RenderableManager::Builder::skinning()`：GPU 骨骼动画（最多 255 bones per mesh）
- `MorphTargetBuffer` + `RenderableManager::Builder::morphing()`：顶点 Morph（最多 256 targets）
- 每帧通过 `setBones()` 或 `setMorphWeights()` 更新数据

但 **Filament 不解析 GLTF 动画、不管理关键帧、不插值**。你需要自己用 `tiny_gltf`/`cgltf` 读取动画数据，每帧计算骨骼矩阵和 morph 权重，再传给 Filament。从这个角度看，`ogl.h` 反而更完整（内置了 `Model::updateAnimation()`）。

### Filament 和物理引擎容易结合吗？

**非常容易。因为 Filament 是"纯渲染引擎"，完全不碰物理，耦合度为零。**

标准结合模式：

```cpp
// 1. 物理引擎步进
physicsWorld->stepSimulation(dt);

// 2. 把物理结果同步到 Filament
for (auto& [body, entity] : physicsEntities) {
    Vec3 pos = body->getPosition();
    Quat rot = body->getRotation();
    auto ti = transformMgr.getInstance(entity);
    transformMgr.setTransform(ti, mat4f::translation(pos) * mat4f(rot));
}

// 3. Filament 渲染
renderer->render(view);
```

物理引擎只负责计算 `position`/`rotation`，Filament 只负责画。两者通过 `TransformManager` 桥接，非常干净。

### 物理引擎推荐（轻量易用）

| 引擎                 | 特点                                                                               | 重量 | 推荐场景                                    |
| ------------------ | -------------------------------------------------------------------------------- | -- | --------------------------------------- |
| **Jolt Physics**   | 现代 C++17、多线程、确定性模拟、被 Godot 4.4+ 内置、Horizon Forbidden West / Death Stranding 2 使用 | 中等 | **新项目的首选**。功能全、性能强、API 现代，但需要用 CMake 构建 |
| **ReactPhysics3D** | 纯 C++、零外部依赖、更轻量、确定性                                                              | 轻量 | **嵌入自定义引擎的最佳选择**。比 Jolt 更轻，API 简单，头文件干净 |
| **Bullet3D**       | 老牌、功能全、生态大                                                                       | 较重 | Legacy 项目。维护慢、以单线程为主，不建议新项目用            |
| **PhysX**          | NVIDIA、GPU 加速、工业级                                                                | 重  | 大型商业项目，小项目不需要                           |

**对** **`ogl.h`** **这类轻量框架最友好的是 ReactPhysics3D**，它设计目标就是"被嵌入到自定义引擎里"，无依赖、编译快。如果你需要更强大的车辆、软体、角色控制器，选 Jolt Physics。

**Godot 的物理引擎**：Godot 4 默认是 **Godot Physics**（自己写的），4.4+ 把 **Jolt Physics** 内置为可选后端（之前是社区插件）。

***

> **更新记录**
>
> - 2026-06-18：整理材质系统结构、`bakedTexture`/`UNLIT`、Shader 源码获取、多材质绑定。
> - 2026-06-18：补充 Shading 模型详解、`faceMats[6]` 完整操作示例。
> - 2026-06-18：补充 Plane 多材质绑定、毛发/卡通渲染、自定义 Shader 能力与限制。
> - 2026-06-18：补充 `setParameter` 与 `MaterialInputs` 内置字段说明。
> - 2026-06-18：补充同 Renderable 混用不同着色模型、`primitive` 的本质与形态。
> - 2026-06-18：补充 Attribute-less / Procedural Primitive 无顶点数据渲染配置。
> - 2026-06-18：补充 `ogl.h` 与 Filament 的架构对比与扩展评估。
> - 2026-06-18：补充 Filament 动画支持、物理引擎结合与推荐。
