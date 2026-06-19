# Maratis 3D 引擎分析

## 一、概述

Maratis 是一个开源的轻量级3D游戏引擎，MX Paper 项目参考了其设计理念和实现方式。

---

## 一、MCore（核心基础层） — 24 个文件

MCore 是整个引擎的基石，提供数学库、字符串、文件 I/O、平台抽象接口、资源管理。

### ■ 1. 全局入口与工具 (Global & Utility)

| 文件 | 主要功能 |
|---|---|
| `MCore.h` | 顶层伞形头文件，定义 `M_CORE_EXPORT` 宏，include 所有 MCore 头文件 |
| `MUtils.h` | 基础头文件：包含 STL（`<map>`/`<vector>`/`<string>`），定义 `SAFE_DELETE`/`SAFE_FREE` 安全释放宏 |

### ■ 2. 数学 (Math Primitives)

| 文件 | 主要功能 |
|---|---|
| `MMaths.h` | 数学常量（`M_PI`、`DEG_TO_RAD`、`RAD_TO_DEG`）与工具宏（`MIN`/`MAX`/`CLAMP`）和插值函数（`lerp`/`clamp`） |
| `MVector2.h` | 二维浮点向量类：加/减/乘/除、`dot`、`normalize`、`rotate`、`lerp`、`getLength` |
| `MVector3.h` | 三维浮点向量类：加/减/乘/除、`cross`、`dot`、`normalize`、`lerp`、角度/长度计算 |
| `MVector4.h` | 四维浮点向量类：加/减/乘/除、`dot`、`normalize`、`lerp`、`getLength` |
| `MQuaternion.h` | 四元数类：3D 旋转、球面插值（`slerp`）、轴角转换、欧拉角 |
| `MMatrix4x4.h` | 4×4 列主序矩阵类：四则运算、`translate`/`rotate`/`scale` 变换、逆/转置 |
| `MColor.h` | 4 通道 RGBA 颜色类（`unsigned char`），比较运算符 |

### ■ 3. 字符串与文件 (String & File)

| 文件 | 主要功能 |
|---|---|
| `MString.h` | 轻量字符串类：包装 `char*`，提供 `set`/`getData`/`getSafeString` |
| `MStringTools.h` | 文件名工具：本地/全局路径解析、目录提取、读取文本文件 |
| `MFile.h` | 抽象文件 I/O 接口：`open`/`close`/`read`/`write`/`seek`/`tell`，`MFileOpenHook` 自定义文件打开钩子 |
| `MFileTools.h` | 文件/目录工具函数：复制/创建/删除/存在检查，C风格 `M_fopen`/`M_fclose` 封装 |
| `MStdFile.h` | `MFile` 的具体实现：封装标准 C `FILE*` 操作（fopen/fread/fwrite） |

### ■ 4. 资源管理 (Resource Management)

| 文件 | 主要功能 |
|---|---|
| `MImage.h` | 内存图像类：存储像素数据、宽高、通道数、数据类型 |
| `MDataLoader.h` | 插件式数据加载系统：`MDataLoadFunction` 封装加载回调，`MDataLoader` 链式尝试加载文件 |
| `MDataManager.h` | 引用计数数据缓存：`MDataRef` 基类，`MDataManager` 模板按文件名管理资源 |

### ■ 5. 平台抽象上下文 (Abstract Platform Contexts)

| 文件 | 主要功能 |
|---|---|
| `MRenderingContext.h` | 渲染上下文抽象：混合/深度/裁剪/矩阵模式枚举，GPU API（纹理/着色器/顶点缓冲/绘制调用/帧缓冲/光照/雾） |
| `MInputContext.h` | 输入上下文抽象：创建/读取按键、轴、属性、触摸、鼠标、手柄输入 |
| `MPhysicsContext.h` | 物理上下文抽象：创建刚体/幽灵体/约束/射线检测，动力学模拟 |
| `MSoundContext.h` | 音频上下文抽象：`M_SOUND_FORMAT` 枚举，缓冲/声源创建，3D 定位，播放控制 |
| `MScriptContext.h` | 脚本上下文抽象：运行脚本、调用函数、传递/返回类型化参数（int/float/string/数组/指针） |
| `MSystemContext.h` | 系统上下文抽象：屏幕尺寸、光标位置/可见性、工作目录、系统时钟 |
| `MSound.h` | 原始音频采样类：PCM 数据、格式、采样率、大小（mono8/16, stereo8/16） |

---

## 二、MEngine（引擎层） — 46 个文件

MEngine 是引擎核心，包含场景对象层次、渲染、动画、资源引用、行为系统和关卡管理。

### ■ 1. 引擎入口与工具 (Engine & Utilities)

| 文件 | 主要功能 |
|---|---|
| `MEngine.h` | MEngine 伞形头文件：导出宏、全局枚举、公共 include |
| `MGame.h` | 游戏主循环控制器：`init` → `update` → `draw` → `quit` |
| `MLog.h` | 日志工具类：syslog 风格日志流输出 |
| `MKey.h` | 关键帧基元：存储时间和类型化浮点值 |
| `MVariable.h` | 类型化变体值：支持 bool、int、float、string、`MVector3` 等 |
| `MBox3d.h` | 3D AABB 轴对齐包围盒：min/max 向量、相交测试 |
| `MFrustum.h` | 视锥体：近/远平面点、视锥体裁剪测试工具 |

### ■ 2. 场景对象层次 (Scene Object Hierarchy)

| 文件 | 主要功能 |
|---|---|
| `MObject3d.h` | 所有 3D 对象基类：变换（位置/旋转/缩放）、父子层次、行为绑定、序列 ID |
| `MOBone.h` | 骨骼节点对象（`MObject3d` 子类）：带逆姿态矩阵 |
| `MOCamera.h` | 相机对象：FOV、近/远裁剪面、投影/视图矩阵、雾设置 |
| `MOEntity.h` | 实体对象：网格绑定、物理碰撞形状/约束、材质动画 |
| `MOLight.h` | 光源对象：点光/聚光/方向光类型、颜色、半径、阴影支持 |
| `MOSound.h` | 声音发射对象：绑定 `MSoundRef`、播放/暂停、音调、增益 |
| `MOText.h` | 3D 空间文本对象：字体绑定、对齐、颜色 |

### ■ 3. 网格与几何 (Mesh & Geometry)

| 文件 | 主要功能 |
|---|---|
| `MMesh.h` | 核心网格数据：`MDisplay`、`MSubMesh`、顶点/索引缓冲、包围盒、蒙皮数据 |
| `MMeshTools.h` | 网格工具函数：关键帧插值、骨骼动画、网格蒙皮 |
| `MSkinData.h` | 蒙皮顶点数据：每顶点骨骼权重和索引（`MSkinPoint`） |
| `MMorphingData.h` | 形变目标数据：blendshape 变形顶点位置 |

### ■ 4. 骨骼动画 (Armature & Animation)

| 文件 | 主要功能 |
|---|---|
| `MArmature.h` | 骨骼容器：持有骨骼数组 |
| `MArmatureAnim.h` | 骨骼动画数据：骨骼变换关键帧 |
| `MArmatureAnimRef.h` | 骨骼动画资源引用（引用计数句柄） |
| `MObject3dAnim.h` | 对象变换动画：位置/旋转/缩放关键帧 |

### ■ 5. 材质与纹理 (Material & Texture)

| 文件 | 主要功能 |
|---|---|
| `MMaterial.h` | 材质定义：混合模式、`MTexturePass` 纹理通道层、渲染属性 |
| `MTexture.h` | 纹理对象：包装 `MTextureRef`，设置过滤/寻址模式 |
| `MMaterialAnim.h` | 单材质属性动画：不透明度等关键帧 |
| `MMaterialsAnim.h` | 材质动画集合：捆绑多个材质动画 |
| `MMaterialsAnimRef.h` | 材质动画集合资源引用（引用计数句柄） |
| `MTextureAnim.h` | 纹理动画：UV 偏移/缩放关键帧 |
| `MTexturesAnim.h` | 纹理动画集合 |
| `MTexturesAnimRef.h` | 纹理动画集合资源引用（引用计数句柄） |

### ■ 6. 字体与着色器 (Font & Shader)

| 文件 | 主要功能 |
|---|---|
| `MFont.h` | 字体数据：`MCharacter` 字形定义、文本渲染支持 |
| `MFontRef.h` | 字体资源引用（引用计数句柄） |
| `MShaderRef.h` | 着色器资源引用（顶点/像素类型） |

### ■ 7. 资源引用系统 (Resource Reference System)

| 文件 | 主要功能 |
|---|---|
| `MMeshRef.h` | 网格资源引用（引用计数句柄） |
| `MSoundRef.h` | 声音资源引用（引用计数句柄） |
| `MTextureRef.h` | 纹理资源引用（引用计数句柄） |

### ■ 8. 渲染系统 (Rendering)

| 文件 | 主要功能 |
|---|---|
| `MRenderer.h` | 自定义渲染器抽象基类 |
| `MRendererCreator.h` | 渲染器工厂类：注册并创建 `MRenderer` 子类 |
| `MRendererManager.h` | 渲染器注册表：管理所有 `MRendererCreator` 条目 |
| `MFXManager.h` | 特效管理器：配对着色器引用（vertex + pixel）生成渲染特效 |
| `MScene.h` | 场景渲染管理：对象分组、数据模式、光照、裁剪、绘制调度 |

### ■ 9. 行为系统 (Behavior System / 脚本)

| 文件 | 主要功能 |
|---|---|
| `MBehavior.h` | 行为基类：挂载到 `MObject3d` 的可脚本化对象行为 |
| `MBehaviorCreator.h` | 行为工厂类：注册并实例化自定义 `MBehavior` 子类 |
| `MBehaviorManager.h` | 行为注册表：管理所有 `MBehaviorCreator` 条目 |

### ■ 10. 关卡与包管理 (Level & Package)

| 文件 | 主要功能 |
|---|---|
| `MLevel.h` | 关卡容器：持有所有对象、特效、行为、资源引用 |
| `MPackageManager.h` | 虚拟文件系统/包文件抽象 I/O 接口 |

---

## 三、MGui（GUI 层） — 21 个文件

MGui 提供平台窗口抽象、输入设备封装、2D GUI 控件体系。

### ■ 1. 平台窗口后端 (Platform Window Backends)

| 文件 | 主要功能 |
|---|---|
| `COCOA/MCocoaWindow.h` | macOS Cocoa 原生窗口实现（NSWindow / NSOpenGLContext） |
| `WIN32/MWin32Window.h` | Windows Win32 原生窗口实现（HWND / HDC / HGLRC） |
| `X11/MX11Window.h` | Linux X11 原生窗口实现（Display / Window / GLX Context） |
| `MWindow.h` | 平台分发器：按 OS 条件 include 正确的后端 `MWindow` |

### ■ 2. 输入设备 (Input Devices)

| 文件 | 主要功能 |
|---|---|
| `MWinEvents.h` | 窗口级事件枚举（`MWIN_EVENT_TYPE`）与 `MWinEvent` 结构体 |
| `MMouse.h` | 鼠标类：按键状态、位置、滚轮、压力 |
| `MKeyboard.h` | 键盘单例：256 键按下状态跟踪 |
| `MJoystick.h` | 手柄跨平台类：按钮状态、六轴数据 |
| `X11/MJoystickLinux.h` | Linux 手柄实现：通过 `/dev/input/jsX` 读取 |

### ■ 3. GUI 框架基础 (GUI Framework Base)

| 文件 | 主要功能 |
|---|---|
| `MGui.h` | GUI 伞形头文件：`M_VAR_TYPE` 枚举、前向声明所有控件、include 全部头文件 |
| `MGui2d.h` | 2D 控件基类：位置/缩放/颜色/可见性/字体/父窗口/变量绑定 |
| `MGuiEvents.h` | GUI 事件枚举（`MGUI_EVENT_TYPE`）与 `MGuiEvent` 结构体 |

### ■ 4. GUI 控件 (GUI Widgets)

| 文件 | 主要功能 |
|---|---|
| `MGuiButton.h` | 按钮控件：单击按钮、复选/单选组模式 |
| `MGuiText.h` | 静态文本标签：自动缩放 |
| `MGuiEditText.h` | 文本输入框：单行/多行编辑、选区 |
| `MGuiSlide.h` | 滑块控件：拖拽滑动条、min/max 范围 |
| `MGuiImage.h` | 图片控件：从文件加载纹理显示 |
| `MGuiMenu.h` | 下拉菜单：弹出式菜单容器（底层用 `MGuiWindow`） |
| `MGuiWindow.h` | 滚动窗口容器：容纳子控件、滚动条 |
| `MGuiFileBrowser.h` | 文件浏览器对话框：目录/文件窗格、OK/Cancel 按钮 |
| `MGuiTextureFont.h` | 位图字体渲染器：从纹理图集查询字形、大小、间距 |

---

> **模块依赖关系**：`MCore（数学/字符串/抽象接口）` ← `MEngine（场景/对象/动画/渲染）` ← `MGui（窗口/输入/UI）`
> 沿袭经典三层架构：基础层 → 引擎层 → 交互层，每层均可独立编译。

## 二、架构特点

```
Maratis/
├── MSDK/                      # SDK核心
│   ├── MCore/                 # 核心模块（数学、文件、系统）
│   ├── MEngine/               # 引擎模块（场景、渲染、物理）
│   └── MGui/                  # GUI模块
├── Maratis/                   # 编辑器
│   ├── Editor/                # 编辑器界面
│   ├── Player/                # 播放器
│   └── Common/                # 通用组件
└── 3rdparty/                  # 第三方库
    ├── lua/                   # Lua脚本
    ├── bullet/                # Bullet物理引擎
    ├── freetype/              # 字体渲染
    ├── assimp/                # 模型加载
    └── ...
```

## 三、Scene 组织结构

```
┌─────────────────────────────────────────────────────────────┐
│                     Level（关卡）                           │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                   Scene（场景）                     │   │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐  │   │
│  │  │Entity  │ │ Light   │ │ Camera  │ │ Script  │  │   │
│  │  │(实体)  │ │ (光源)  │ │  (相机) │ │ (脚本)  │  │   │
│  │  └────┬────┘ └─────────┘ └─────────┘ └─────────┘  │   │
│  │       │                                           │   │
│  │       ▼                                           │   │
│  │  ┌─────────────────────────────────────┐          │   │
│  │  │         MeshRef（网格引用）          │          │   │
│  │  │   ┌─────────────────────────┐       │          │   │
│  │  │   │      Mesh（网格数据）    │       │          │   │
│  │  │   │ 顶点 + 索引 + 材质引用   │       │          │   │
│  │  │   └─────────────────────────┘       │          │   │
│  │  └─────────────────────────────────────┘          │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## 四、核心类与对象关系

| 类名 | 说明 | 职责 |
|------|------|------|
| **MLevel** | 关卡/场景容器 | 管理多个 Scene，负责加载/切换 |
| **MScene** | 场景 | 包含所有场景对象的根容器 |
| **MObject3d** | 3D对象基类 | 提供变换（位置/旋转/缩放）能力 |
| **MOEntity** | 实体对象 | 包含 MeshRef 和变换数据 |
| **MOLight** | 光源对象 | 点光/方向光/聚光，控制照明 |
| **MOCamera** | 相机对象 | 定义观察视角和投影 |
| **MMesh** | 网格数据 | 顶点、索引、材质定义 |
| **MMeshRef** | 网格引用 | 实例化网格，支持复用 |
| **MMaterial** | 材质 | Shader、纹理、PBR属性 |
| **MScript** | 脚本组件 | 绑定 Lua 脚本 |

## 五、类继承关系图谱

```
MObject3d (基类)
    ├── MOEntity      (实体)
    │       └── MMeshRef (网格引用)
    │               └── MMesh (网格数据)
    │                       └── MMaterial (材质)
    │
    ├── MOLight       (光源)
    │       ├── MOPointLight    (点光源)
    │       ├── MODirectionalLight (方向光)
    │       └── MOSpotLight     (聚光灯)
    │
    └── MOCamera      (相机)
            ├── MOFrustumCamera (透视相机)
            └── MOOrthoCamera   (正交相机)
```

## 六、模型数据组织

Maratis 使用以下结构组织3D数据：

- **MMesh** - 网格数据（顶点、索引、材质）
- **MMeshRef** - 网格引用（实例化）
- **MObject3d** - 3D对象基类
- **MOEntity** - 实体对象（包含网格引用和变换）
- **MOLight** - 光源对象
- **MOCamera** - 相机对象

## 七、渲染管线

Maratis 的渲染管线包括：

1. **MRenderer** - 渲染器接口
2. **MStandardRenderer** - 标准前向渲染器
3. **MFixedRenderer** - 固定管线渲染器
4. **MShaderRef** - Shader引用管理
5. **MTexture** - 纹理管理
6. **MMaterial** - 材质系统

## 八、Lua 脚本系统

### 脚本 API 示例

```lua
-- 获取对象
Car = getObject("Car")
W1 = getObject("W1")

-- 场景更新回调
function onSceneUpdate()
    -- 键盘输入
    if isKeyPressed("UP") then
        force = 1
    end
    
    -- 获取位置
    newPos = getPosition(Car)
    
    -- 添加力
    addCentralForce(Car, {0, force*power, -power}, "local")
    
    -- 添加扭矩
    addTorque(Car, {0, 0, -x*rot})
end
```

### 脚本运行流程

```lua
-- 1. 初始化阶段
function onInit()
    -- 场景加载完成后调用
    Car = getObject("Car")
    Speed = 0
end

-- 2. 更新阶段（每帧调用）
function onSceneUpdate()
    if isKeyPressed("UP") then
        Speed = Speed + 0.1
    end
    pos = getPosition(Car)
    addCentralForce(Car, {0, 0, -Speed})
end

-- 3. 交互事件
function onKeyDown(key)
    if key == "SPACE" then
        playSound("horn.wav")
    end
end

function onCollision(obj1, obj2)
    print(obj1 .. " collided with " .. obj2)
end
```

### Lua API 分类

| 类别 | API 示例 |
|------|----------|
| **向量运算** | `vec3(x,y,z)`, `length(v)`, `normalize(v)` |
| **对象获取** | `getObject(name)`, `getScene(id)` |
| **变换控制** | `getPosition(obj)`, `setPosition(obj, pos)`, `rotate(obj, rot)` |
| **物理系统** | `addCentralForce(obj, force)`, `addTorque(obj, torque)` |
| **输入交互** | `isKeyPressed(key)`, `getAxis(axis)` |
| **声音系统** | `playSound(path)`, `setSoundGain(sound, gain)` |
| **场景管理** | `changeScene(id)`, `loadLevel(path)` |

### 脚本绑定机制（C++ 端实现）

#### 1. 初始化 Lua 状态机

```cpp
// 创建 Lua 虚拟机
lua_State* L = luaL_newstate();

// 打开标准库
luaL_openlibs(L);

// 注册自定义 API
registerMaratisAPI(L);
```

#### 2. 注册 C++ 函数到 Lua

```cpp
void registerMaratisAPI(lua_State* L) {
    // 对象操作
    lua_register(L, "getObject", lua_getObject);
    lua_register(L, "getScene", lua_getScene);
    
    // 变换控制
    lua_register(L, "getPosition", lua_getPosition);
    lua_register(L, "setPosition", lua_setPosition);
    
    // 物理系统
    lua_register(L, "addCentralForce", lua_addCentralForce);
    lua_register(L, "addTorque", lua_addTorque);
    
    // 输入系统
    lua_register(L, "isKeyPressed", lua_isKeyPressed);
    
    // 声音系统
    lua_register(L, "playSound", lua_playSound);
}
```

#### 3. C++ 函数实现示例

```cpp
static int lua_getObject(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    
    // 在场景中查找对象
    MObject3d* obj = scene->getObject(name);
    
    // 将对象指针压入栈（作为 userdata）
    lua_pushlightuserdata(L, obj);
    
    return 1;
}

static int lua_setPosition(lua_State* L) {
    // 获取 userdata（对象指针）
    MObject3d* obj = (MObject3d*)lua_touserdata(L, 1);
    
    // 获取 Lua 表中的坐标
    lua_getfield(L, 2, "x");
    lua_getfield(L, 2, "y");
    lua_getfield(L, 2, "z");
    
    float x = (float)lua_tonumber(L, -3);
    float y = (float)lua_tonumber(L, -2);
    float z = (float)lua_tonumber(L, -1);
    
    // 设置位置
    obj->setPosition(x, y, z);
    
    return 0;
}
```

### 脚本交互流程

#### 阶段 1：加载脚本

```cpp
// 加载 Lua 脚本文件
int result = luaL_dofile(L, "script.lua");
if (result != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    // 处理错误
}
```

#### 阶段 2：初始化回调

```cpp
// 调用 onInit() 函数
lua_getglobal(L, "onInit");
if (lua_isfunction(L, -1)) {
    lua_call(L, 0, 0);  // 无参数，无返回值
}
```

#### 阶段 3：主循环回调

```cpp
// 每帧调用 onSceneUpdate()
void update(float deltaTime) {
    lua_getglobal(L, "onSceneUpdate");
    if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, deltaTime);
        lua_call(L, 1, 0);  // 1个参数，0个返回值
    }
}
```

#### 阶段 4：事件触发

```cpp
// 键盘事件
void onKeyDown(int key) {
    lua_getglobal(L, "onKeyDown");
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, keyToString(key));
        lua_call(L, 1, 0);
    }
}

// 碰撞事件
void onCollision(MObject3d* obj1, MObject3d* obj2) {
    lua_getglobal(L, "onCollision");
    if (lua_isfunction(L, -1)) {
        lua_pushlightuserdata(L, obj1);
        lua_pushlightuserdata(L, obj2);
        lua_call(L, 2, 0);
    }
}
```

### 数据交互方式

| 类型 | C++ → Lua | Lua → C++ |
|------|-----------|-----------|
| **数值** | `lua_pushnumber()` | `lua_tonumber()` |
| **字符串** | `lua_pushstring()` | `lua_tostring()` |
| **布尔** | `lua_pushboolean()` | `lua_toboolean()` |
| **表** | `lua_newtable()` + `lua_setfield()` | `lua_getfield()` |
| **对象** | `lua_pushlightuserdata()` | `lua_touserdata()` |
| **函数** | `lua_pushcfunction()` | `lua_call()` |

### 脚本生命周期

```
┌─────────────────────────────────────────────────────────┐
│                    脚本生命周期                        │
├─────────────────────────────────────────────────────────┤
│  1. 创建 Lua 状态机                                   │
│           ↓                                           │
│  2. 注册 C++ API                                      │
│           ↓                                           │
│  3. 加载 Lua 脚本                                      │
│           ↓                                           │
│  4. 调用 onInit()                                    │
│           ↓                                           │
│  5. 主循环: 调用 onSceneUpdate()                      │
│           ↓                                           │
│  6. 响应事件: onKeyDown, onCollision, etc.           │
│           ↓                                           │
│  7. 销毁 Lua 状态机                                   │
└─────────────────────────────────────────────────────────┘
```

## 九、Scene 与 Level 的关系

| 维度 | Scene（场景） | Level（关卡） |
|------|--------------|--------------|
| **定义** | 单个渲染场景 | 关卡/地图容器 |
| **内容** | 包含实体、光源、相机 | 包含一个或多个 Scene |
| **加载方式** | 动态加载到 Level | 通过 `loadLevel()` 加载 |
| **切换** | 场景内对象切换 | 整个关卡切换 |
| **文件格式** | `.mts` (Maratis Scene) | `.mtl` (Maratis Level) |

### 关键 API 示例

```lua
-- Level 操作
loadLevel("level1.mtl")           -- 加载关卡
changeScene(1)                    -- 切换到第2个场景

-- Scene 操作
scene = getScene(0)               -- 获取场景
obj = getObject("Player")         -- 获取对象

-- 对象变换
pos = getPosition(obj)            -- 获取位置
setPosition(obj, {10, 0, 5})      -- 设置位置
rotate(obj, {0, 90, 0})          -- 旋转

-- 物理系统
addCentralForce(obj, {0, 10, 0})  -- 添加力
addTorque(obj, {0, 0, 1})        -- 添加扭矩

-- 动画控制
changeAnimation(obj, "run")       -- 切换动画
setAnimationSpeed(obj, 2)         -- 设置动画速度
```

## 十、脚本绑定机制与 Unity 对比

### 10.1 Unity 脚本绑定机制

**特点：组件化绑定**

```
┌─────────────────────────────────────────────────────┐
│              GameObject (游戏对象)                  │
│  ┌────────────────┐ ┌────────────────┐           │
│  │  Transform     │ │  MeshRenderer  │           │
│  └────────────────┘ └────────────────┘           │
│  ┌────────────────┐ ┌────────────────┐           │
│  │  MyScript.cs   │ │  Rigidbody     │           │
│  │  (MonoBehaviour)│ └────────────────┘           │
│  └────────────────┘                               │
└─────────────────────────────────────────────────────┘
```

**Unity 的脚本特点：**
- 脚本继承自 `MonoBehaviour`
- 通过 Inspector 面板拖放绑定到 GameObject
- 每个对象可以绑定多个脚本
- 脚本拥有对象级别的生命周期（`Start()`, `Update()`）

### 10.2 Maratis 脚本绑定机制

**特点：全局回调 + 对象引用**

```
┌─────────────────────────────────────────────────────┐
│                    Scene (场景)                      │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐             │
│  │ Entity  │ │ Entity  │ │ Entity  │             │
│  │ (Car)   │ │ (Wall)  │ │ (Light) │             │
│  └────┬────┘ └─────────┘ └─────────┘             │
│       │                                           │
│       ▼                                           │
│  ┌─────────────────────────────────────────────┐   │
│  │         Global Lua Script                   │   │
│  │  - onInit()                                │   │
│  │  - onSceneUpdate() ←── 引用所有对象         │   │
│  │  - onKeyDown()                             │   │
│  │                                            │   │
│  │  Car = getObject("Car") ←── 获取对象引用   │   │
│  │  setPosition(Car, {10, 0, 0})             │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

**Maratis 的脚本特点：**

| 特性 | 说明 |
|------|------|
| **绑定方式** | 脚本不直接绑定到对象，而是通过 `getObject()` 获取引用 |
| **脚本数量** | 通常每个场景一个主脚本，但支持多个脚本文件 |
| **生命周期** | 全局回调（`onInit`, `onSceneUpdate`）而非对象级别 |
| **对象访问** | 通过名称或ID获取对象引用 |

### 10.3 Maratis 脚本组织方式

#### 单个脚本管理多个对象

```lua
-- main.lua
function onInit()
    Car = getObject("Car")
    Wall = getObject("Wall")
    Light = getObject("SpotLight")
    Speed = 0
end

function onSceneUpdate()
    if isKeyPressed("UP") then
        Speed = Speed + 0.1
    end
    addCentralForce(Car, {0, 0, -Speed})
    setLightIntensity(Light, 1 + sin(getTime()) * 0.5)
end
```

#### 多个脚本文件

```cpp
// C++ 加载多个脚本
luaL_dofile(L, "globals.lua");   // 全局变量和函数
luaL_dofile(L, "player.lua");    // 玩家控制
luaL_dofile(L, "enemy.lua");     // 敌人AI
luaL_dofile(L, "effects.lua");   // 特效系统
```

#### 脚本间通信

```lua
-- player.lua
function initPlayer()
    Player = getObject("Player")
    Player.health = 100
end

-- enemy.lua
function onEnemyAttack()
    Player.health = Player.health - 10
end
```

### 10.4 Unity vs Maratis 对比表

| 维度 | Unity | Maratis |
|------|-------|---------|
| **绑定方式** | 脚本作为组件附加到 GameObject | 脚本全局运行，通过 `getObject()` 获取引用 |
| **脚本数量** | 每个对象可绑定多个脚本 | 单个场景通常一个主脚本 |
| **生命周期** | 对象级别（`Start()`, `Update()`） | 全局级别（`onInit()`, `onSceneUpdate()`） |
| **对象访问** | 通过 `GetComponent<T>()` | 通过 `getObject(name)` |
| **脚本通信** | 通过 `FindObjectOfType()` 或消息系统 | 通过全局变量 |
| **灵活性** | 高（组件化设计） | 中等（全局脚本） |
| **性能** | 略低（组件查找开销） | 略高（直接引用） |

### 10.5 关键问题解答

**Q：Maratis 是否支持多脚本绑定到对象？**

**A：否**。Maratis 的设计理念是：
- 脚本是全局的，不隶属于任何特定对象
- 通过 `getObject()` 获取对象引用后进行操作
- 一个脚本可以控制多个对象

这与 Unity 的组件化设计形成对比，但两种方式各有优劣。

## 十一、对 MX Paper 的启发

Maratis 的设计理念对 MX Paper 有以下启发：

1. **模块化设计** - 清晰的模块分层，便于维护和扩展
2. **Lua 脚本绑定** - 考虑添加 Lua 脚本支持（已实现 `lua_oui.h`）
3. **组件化对象** - 3D对象的组件化设计思路
4. **资源引用管理** - 使用引用计数管理资源生命周期
5. **渲染器抽象** - 支持多种渲染后端

### MX Paper Scene 架构设计

基于 Maratis 的设计，MX Paper 的 Scene 架构可以这样设计：

```cpp
class Scene {
    std::vector<std::unique_ptr<Layer>> layers;  // 图层（类似 Entity）
    std::vector<std::unique_ptr<Light>> lights;  // 光源
    Camera camera;                               // 相机
    std::string script;                          // Lua 脚本
};

class Layer {
    // 图层基类，包含变换、可见性、混合模式
};

class ImageLayer : public Layer { /* 图片层 */ };
class VideoLayer : public Layer { /* 视频层 */ };
class ModelLayer : public Layer { /* 3D模型层 */ };
```

这种设计保持了 Maratis 的层次化架构，同时适配壁纸场景的 2D/3D 混合需求。

---

## 十二、对象拾取（Picking）机制

Maratis/MSDK 的编辑器对象拾取采用**纯 CPU 射线-三角面相交（Ray-Triangle Intersection）**，不是 GPU Color ID Picking，精度达到单个三角面级别。

### 12.1 核心实现位置

| 文件 | 作用 |
|------|------|
| [`Maratis/Editor/Maratis/Maratis.cpp`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/Maratis/Editor/Maratis/Maratis.cpp#L1977-L2077) | 编辑器主入口 `selectObjectsInMainView`，负责生成射线并调用拾取 |
| [`Maratis/Editor/Maratis/Maratis.cpp`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/Maratis/Editor/Maratis/Maratis.cpp#L1691-L1843) | `getNearestObject`：遍历场景所有对象类型并求最近命中 |
| [`Maratis/Editor/Maratis/Maratis.cpp`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/Maratis/Editor/Maratis/Maratis.cpp#L1499-L1635) | `getNearestRaytracedDistance`：对单个 Mesh 做三角面精度的射线求交 |
| [`MSDK/MEngine/Sources/MMeshTools.cpp`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/MSDK/MEngine/Sources/MMeshTools.cpp#L545-L630) | `getNearestRaytracedPosition`：遍历索引缓冲，逐个三角形求最近交点 |
| [`MSDK/MCore/Sources/MMaths.cpp`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/MSDK/MCore/Sources/MMaths.cpp#L371-L389) | `isEdgeTriangleIntersection`：底层射线-三角形相交测试 |
| [`MSDK/MCore/Sources/MMaths.cpp`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/MSDK/MCore/Sources/MMaths.cpp#L247-L273) | `isEdgeToBoxCollision`：射线-AABB 包围盒相交测试，用于粗剔除 |

### 12.2 拾取流程

```
┌─────────────────────────────────────────────────────────────┐
│  1. 获取鼠标屏幕坐标                                          │
│           ↓                                                   │
│  2. MOCamera::getUnProjectedPoint() → 世界空间远端点           │
│           ↓                                                   │
│  3. 构造射线：rayO = 相机位置，rayD = 远端点                   │
│           ↓                                                   │
│  4. getNearestObject(scene, rayO, rayD)                       │
│           ↓                                                   │
│  5. 按对象类型处理                                             │
│     • Entity  → 用自身 Mesh 求交                              │
│     • Camera  → 用 m_cameraEntity 代理网格求交                │
│     • Light   → 用 m_sphereEntity 代理网格求交                │
│     • Sound   → 用 m_sphereEntity 代理网格求交（放大 1.75 倍）│
│     • Text    → 用 m_planeEntity 代理网格求交                 │
│           ↓                                                   │
│  6. getNearestRaytracedDistance()                             │
│     a. 若带骨骼动画，先 computeSkinning 计算皮肤后顶点          │
│     b. 射线变换到模型本地空间                                  │
│     c. Mesh 级 AABB 粗剔除                                     │
│     d. SubMesh 级 AABB 粗剔除                                  │
│     e. 对每个 Display 区间遍历三角形，调用 getNearestRaytracedPosition
│     f. 支持双面检测（CullMode = NONE/FRONT/BACK）              │
│           ↓                                                   │
│  7. 返回最近命中的 MObject3d 与世界空间交点                    │
└─────────────────────────────────────────────────────────────┘
```

### 12.3 射线生成

在 [`Maratis::selectObjectsInMainView`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/Maratis/Editor/Maratis/Maratis.cpp#L1997-L2011) 中：

```cpp
// ray (origin and dest)
MVector3 rayO = camera->getTransformedPosition();
MVector3 rayD = camera->getUnProjectedPoint(MVector3(
    (float)mouse->getXPosition(),
    (float)(window->getHeight() - mouse->getYPosition()),
    1));

rayD = rayO + ((rayD - rayO).getNormalized() * (camera->getClippingFar() - camera->getClippingNear()));
```

- `rayO`：相机世界位置。
- `rayD`：鼠标位置反投影到 far plane 后的世界坐标，再归一化延长。
- 使用 [`MOCamera::getUnProjectedPoint`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/MSDK/MEngine/Sources/MOCamera.cpp#L90-L107) 完成屏幕到世界的变换。

### 12.4 对象级遍历

[`Maratis::getNearestObject`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/Maratis/Editor/Maratis/Maratis.cpp#L1691-L1843) 遍历 `MScene` 中所有 `MObject3d`，不同类型用不同代理网格：

| 对象类型 | 代理 / 真实网格 | 缩放策略 |
|----------|----------------|----------|
| `M_OBJECT3D_ENTITY` | 实体自身 `MMesh` | 使用实体变换矩阵 |
| `M_OBJECT3D_CAMERA` | `m_cameraEntity` | 按与相机距离 × `editObjectSize` |
| `M_OBJECT3D_LIGHT` | `m_sphereEntity` | 按与相机距离 × `editObjectSize` |
| `M_OBJECT3D_SOUND` | `m_sphereEntity` | 按与相机距离 × `editObjectSize` × 1.75 |
| `M_OBJECT3D_TEXT` | `m_planeEntity` | 按文字包围盒尺寸构建缩放矩阵 |

### 12.5 三角面精度求交

[`getNearestRaytracedDistance`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/Maratis/Editor/Maratis/Maratis.cpp#L1499-L1635) 是核心函数：

```cpp
bool getNearestRaytracedDistance(
    MMesh * mesh,
    MMatrix4x4 * matrix,
    const MVector3 & origin,
    const MVector3 & dest,
    float * distance,
    MOEntity * entity = NULL
)
```

内部步骤：

1. **骨骼动画处理**：若 `entity` 有 `MArmature`，先更新骨骼矩阵，再用 `computeSkinning` 计算当前帧皮肤后顶点。
2. **模型空间转换**：用 `matrix->getInverse()` 把射线变换到模型本地空间。
3. **Mesh 级 AABB 粗剔除**：调用 `isEdgeToBoxCollision` 判断射线是否穿过整个 Mesh 的包围盒，未命中直接返回。
4. **SubMesh 级 AABB 粗剔除**：对每个 `MSubMesh` 再用其局部包围盒剔除。
5. **Display 级精确检测**：遍历 `MSubMesh` 的每个 `MDisplay`（绘制区间），调用 `getNearestRaytracedPosition` 对该区间的索引三角形逐一求交。
6. **双面检测**：根据材质的 `CullMode`，分别检测正面和反面（`invertNormal = true`）。
7. 返回最近命中距离，并可输出世界空间交点。

底层三角形相交在 [`isEdgeTriangleIntersection`](file:///e:/MX/Maratis/maratis-read-only/trunk/dev/MSDK/MCore/Sources/MMaths.cpp#L371-L389) 中：

```cpp
bool isEdgeTriangleIntersection(
    const MVector3 & origin, const MVector3 & dest,
    const MVector3 & a, const MVector3 & b, const MVector3 & c,
    const MVector3 & normal, MVector3 * point)
{
    // 1. 射线与三角形所在平面求交
    // 2. 用 isPointInTriangle 判断交点是否在三角形内
}
```

### 12.6 精度结论

**是的，达到三角面精度。**

- 最终判断单位是**单个三角形**，不是包围盒或对象中心点。
- 遍历 `MSubMesh` 的每个 `MDisplay` 区间，对 `display->getSize()` 个索引描述的每个三角形调用 `getNearestRaytracedPosition`。
- 支持骨骼动画后的顶点变形，蒙皮模型可精确拾取当前姿态下的三角面。
- 支持双面拾取，正反面均能命中。
- 返回射线与三角形的实际世界空间交点。

### 12.7 性能特点

| 优化层级 | 方法 | 说明 |
|----------|------|------|
| Mesh 级 | `isEdgeToBoxCollision` | 跳过射线未命中的整个模型 |
| SubMesh 级 | `isEdgeToBoxCollision` | 跳过射线未命中的子网格 |
| 精确级 | `isEdgeTriangleIntersection` | 逐三角形求交 |

**不足**：没有空间加速结构（BVH/Octree/KDTree），场景面数增加时性能线性下降。

### 12.8 对 MX Paper 的启发

MX Paper 目前采用 GPU Color ID Picking，与 Maratis 的 CPU 射线-三角面方案形成互补：

- **Maratis 方案**：纯 CPU、无 GPU 依赖、命中精度到三角面、适合面数可控的编辑器场景。
- **MX Paper 方案**：GPU 颜色拾取，一次绘制即可选中，适合多对象、复杂 GLTF 场景。

若 MX Paper 未来需要支持**不依赖 GPU 的离线拾取**、**骨骼动画模型的精确命中**或**拾取点法线/UV 信息**，可参考 Maratis 这套射线-三角面实现。

---

## 附录：完整 Lua API 参考

### 1. 向量运算

| API | 说明 | 示例 |
|-----|------|------|
| `vec3(x, y, z)` | 创建三维向量 | `v = vec3(1, 2, 3)` |
| `length(v)` | 获取向量长度 | `len = length(v)` |
| `normalize(v)` | 归一化向量 | `nv = normalize(v)` |
| `dot(a, b)` | 向量点积 | `d = dot(v1, v2)` |
| `cross(a, b)` | 向量叉积 | `c = cross(v1, v2)` |

### 2. 对象/场景获取

| API | 说明 | 示例 |
|-----|------|------|
| `getScene(id)` | 获取场景 | `scene = getScene(0)` |
| `getObject(name)` | 通过名称获取对象 | `car = getObject("Car")` |
| `getClone(object)` | 获取对象克隆 | `clone = getClone(car)` |
| `getParent(object)` | 获取父对象 | `parent = getParent(car)` |
| `getChilds(object)` | 获取子对象列表 | `childs = getChilds(parent)` |
| `getCurrentCamera()` | 获取当前相机 | `cam = getCurrentCamera()` |

### 3. 对象变换

| API | 说明 | 示例 |
|-----|------|------|
| `getPosition(obj)` | 获取位置 | `pos = getPosition(car)` |
| `setPosition(obj, vec)` | 设置位置 | `setPosition(car, vec3(1, 2, 3))` |
| `getRotation(obj)` | 获取旋转 | `rot = getRotation(car)` |
| `setRotation(obj, vec)` | 设置旋转 | `setRotation(car, vec3(0, 90, 0))` |
| `getScale(obj)` | 获取缩放 | `s = getScale(car)` |
| `setScale(obj, vec)` | 设置缩放 | `setScale(car, vec3(2, 2, 2))` |
| `rotate(obj, vec)` | 旋转对象 | `rotate(car, vec3(0, 1, 0))` |
| `translate(obj, vec)` | 平移对象 | `translate(car, vec3(0, 0, 1))` |

### 4. 对象状态

| API | 说明 | 示例 |
|-----|------|------|
| `isVisible(obj)` | 检查可见性 | `vis = isVisible(car)` |
| `activate(obj)` | 激活对象 | `activate(car)` |
| `deactivate(obj)` | 停用对象 | `deactivate(car)` |
| `isActive(obj)` | 检查是否激活 | `active = isActive(car)` |
| `getName(obj)` | 获取对象名称 | `name = getName(car)` |
| `setParent(obj, parent)` | 设置父对象 | `setParent(child, parent)` |
| `enableShadow(obj)` | 启用阴影 | `enableShadow(car)` |
| `isCastingShadow(obj)` | 检查是否投射阴影 | `cast = isCastingShadow(car)` |

### 5. 动画控制

| API | 说明 | 示例 |
|-----|------|------|
| `getCurrentAnimation(obj)` | 获取当前动画 | `anim = getCurrentAnimation(car)` |
| `changeAnimation(obj, name)` | 切换动画 | `changeAnimation(car, "run")` |
| `isAnimationOver(obj)` | 检查动画是否结束 | `over = isAnimationOver(car)` |
| `getAnimationSpeed(obj)` | 获取动画速度 | `speed = getAnimationSpeed(car)` |
| `setAnimationSpeed(obj, speed)` | 设置动画速度 | `setAnimationSpeed(car, 2)` |
| `getCurrentFrame(obj)` | 获取当前帧 | `frame = getCurrentFrame(car)` |
| `setCurrentFrame(obj, frame)` | 设置当前帧 | `setCurrentFrame(car, 10)` |

### 6. 物理系统

| API | 说明 | 示例 |
|-----|------|------|
| `setGravity(vec)` | 设置重力 | `setGravity(vec3(0, -9.8, 0))` |
| `getGravity()` | 获取重力 | `g = getGravity()` |
| `addCentralForce(obj, force)` | 添加中心力 | `addCentralForce(car, vec3(0, 0, 10))` |
| `addTorque(obj, torque)` | 添加扭矩 | `addTorque(car, vec3(0, 1, 0))` |
| `getMass(obj)` | 获取质量 | `mass = getMass(car)` |
| `setMass(obj, mass)` | 设置质量 | `setMass(car, 100)` |
| `getFriction(obj)` | 获取摩擦力 | `friction = getFriction(car)` |
| `setFriction(obj, val)` | 设置摩擦力 | `setFriction(car, 0.5)` |
| `getRestitution(obj)` | 获取弹性系数 | `rest = getRestitution(car)` |
| `setRestitution(obj, val)` | 设置弹性系数 | `setRestitution(car, 0.3)` |
| `isCollisionBetween(obj1, obj2)` | 检测碰撞 | `hit = isCollisionBetween(car, wall)` |
| `rayHit(start, end)` | 射线检测 | `hit = rayHit(vec3(0,0,0), vec3(10,0,0))` |

### 7. 输入系统

| API | 说明 | 示例 |
|-----|------|------|
| `isKeyPressed(key)` | 检查按键是否按下 | `if isKeyPressed("UP") then ... end` |
| `onKeyDown(key)` | 按键按下事件 | `onKeyDown("SPACE")` |
| `onKeyUp(key)` | 按键释放事件 | `onKeyUp("SPACE")` |
| `getAxis(axis)` | 获取轴输入 | `axis = getAxis("Horizontal")` |
| `getProperty(name)` | 获取属性 | `prop = getProperty("MouseX")` |

### 8. 声音系统

| API | 说明 | 示例 |
|-----|------|------|
| `playSound(sound)` | 播放声音 | `playSound("bgm.wav")` |
| `pauseSound(sound)` | 暂停声音 | `pauseSound("bgm.wav")` |
| `stopSound(sound)` | 停止声音 | `stopSound("bgm.wav")` |
| `getSoundGain(sound)` | 获取音量 | `gain = getSoundGain("bgm.wav")` |
| `setSoundGain(sound, gain)` | 设置音量 | `setSoundGain("bgm.wav", 0.5)` |

### 9. 场景/关卡

| API | 说明 | 示例 |
|-----|------|------|
| `changeScene(id)` | 切换场景 | `changeScene(1)` |
| `getCurrentSceneId()` | 获取当前场景ID | `id = getCurrentSceneId()` |
| `getScenesNumber()` | 获取场景数量 | `num = getScenesNumber()` |
| `loadLevel(path)` | 加载关卡 | `loadLevel("level.mtl")` |
| `doesLevelExist(path)` | 检查关卡是否存在 | `exist = doesLevelExist("level.mtl")` |

### 10. 灯光

| API | 说明 | 示例 |
|-----|------|------|
| `getLightColor(light)` | 获取灯光颜色 | `color = getLightColor(light)` |
| `setLightColor(light, color)` | 设置灯光颜色 | `setLightColor(light, vec3(1,1,1))` |
| `getLightRadius(light)` | 获取灯光半径 | `r = getLightRadius(light)` |
| `setLightRadius(light, r)` | 设置灯光半径 | `setLightRadius(light, 10)` |
| `getLightIntensity(light)` | 获取灯光强度 | `int = getLightIntensity(light)` |
| `setLightIntensity(light, int)` | 设置灯光强度 | `setLightIntensity(light, 2)` |

### 11. 相机

| API | 说明 | 示例 |
|-----|------|------|
| `changeCurrentCamera(cam)` | 切换相机 | `changeCurrentCamera(cam)` |
| `getCameraFov(cam)` | 获取视野 | `fov = getCameraFov(cam)` |
| `setCameraFov(cam, fov)` | 设置视野 | `setCameraFov(cam, 60)` |
| `getCameraNear(cam)` | 获取近裁剪面 | `near = getCameraNear(cam)` |
| `setCameraNear(cam, near)` | 设置近裁剪面 | `setCameraNear(cam, 0.1)` |
| `getCameraFar(cam)` | 获取远裁剪面 | `far = getCameraFar(cam)` |
| `setCameraFar(cam, far)` | 设置远裁剪面 | `setCameraFar(cam, 1000)` |