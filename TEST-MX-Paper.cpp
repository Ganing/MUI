/****************************************************************************
 * 标题: MXPaper - 壁纸软件 v3
 * 功能: 场景系统 + 默认 Logo 场景 + MScene/MObject 数据结构 + 日志文件
 * 架构: Filament 3D + ThorVG OUI
 ****************************************************************************/

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cmath>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <filesystem>
#include <windows.h>
#include <mmsystem.h>

/*==================== MDEBUG 调试开关 ====================*/
#define MDEBUG
#ifdef MDEBUG
static FILE* g_logFile = nullptr;
static void mxLogOpen() {
    g_logFile = fopen("mxpaper.log", "w");
    if (g_logFile) {
        time_t t = time(nullptr);
        fprintf(g_logFile, "=== MXPaper Log %s", ctime(&t));
        fflush(g_logFile);
    }
}
static void mxLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args); fflush(stdout);
    va_end(args);
    if (g_logFile) {
        va_start(args, fmt);
        vfprintf(g_logFile, fmt, args);
        fflush(g_logFile);
        va_end(args);
    }
}
static void mxLogErr(const char* file, int line) {
    mxLog("ERROR AT: FILE:%hs LINE:%d\n", file, line);
}
#define MLOG(...) mxLog(__VA_ARGS__)
#define MLOGN mxLog("\n")
#define MLOGERR mxLogErr(__FILE__, __LINE__)
#else
#define MLOG(...)
#define MLOGN
#define MLOGERR
#endif

/*==================== Crash handler ====================*/
static LONG WINAPI mxCrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    FILE* f = fopen("mxpaper_crash.log", "w");
    if (f) {
        time_t t = time(nullptr);
        fprintf(f, "=== MXPaper CRASH ===\n%s\n", ctime(&t));
        fprintf(f, "Exception: 0x%08X at 0x%p\n", code, ep->ExceptionRecord->ExceptionAddress);
        fclose(f);
    }
    MessageBoxA(nullptr, "MXPaper crashed. See mxpaper_crash.log", "MXPaper Error", MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}

#include "ocore.h"
#include "oapp.h"
#include "oui.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")

#include <filament/Engine.h>
#include <filament/Camera.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/Skybox.h>
#include <filament/LightManager.h>
#include <filament/Viewport.h>
#include <filament/Color.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <utils/EntityManager.h>
#include <backend/platforms/PlatformWGL.h>
#include <filamat/MaterialBuilder.h>

#define FILAMENT_LIB_PATH "e:/MX/3rd/lib/filament/mt/"
#pragma comment(lib, FILAMENT_LIB_PATH "filament.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "backend.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluegl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filabridge.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filaflat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filamat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "utils.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "ibl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "shaders.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "stb.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "zstd.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluevk.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "smol-v.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "image.lib")
#pragma comment(lib, "e:/MX/3rd/lib/libthorvg_mt.lib")

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace fs = std::filesystem;
using namespace filament;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

/*==================== WGL Platform ====================*/
class NoSwapWGLPlatform : public filament::backend::PlatformWGL {
public:
    filament::backend::Driver* createDriver(void* shared, const Platform::DriverConfig& cfg) override {
        return PlatformWGL::createDriver(nullptr, cfg);
    }
    void commit(filament::backend::Platform::SwapChain* sc) noexcept override {}
};

/*==================== ShaderRing ====================*/
class ShaderRing : public OX::UIElement {
public:
    ShaderRing() { rect = OX::ORect(0, 0, 52.0f, 52.0f); visible = false; }
    void render(tvg::Scene* parent) override {
        if (!visible) return;
        float cx = rect.x + rect.w / 2.0f, cy = rect.y + rect.h / 2.0f;
        auto outer = tvg::Shape::gen();
        outer->appendCircle(cx, cy, 20.0f, 20.0f);
        outer->strokeFill(91, 141, 239, 180); outer->strokeWidth(2.5f); outer->fill(0, 0, 0, 0);
        parent->push(std::move(outer));
        auto inner = tvg::Shape::gen();
        inner->appendCircle(cx, cy, 14.0f, 14.0f);
        inner->strokeFill(255, 255, 255, 40); inner->strokeWidth(1.0f); inner->fill(0, 0, 0, 0);
        parent->push(std::move(inner));
        auto dot = tvg::Shape::gen();
        dot->appendCircle(cx, cy, 3.0f, 3.0f); dot->fill(255, 255, 255, 140);
        parent->push(std::move(dot));
    }
};

/*==================== 场景数据类型 ====================*/
enum class MObjectType { None, Camera, Image, Text, Sound, Light, Model };

struct MObject {
    MObjectType type = MObjectType::None;
    std::string id;
    float3 position{0,0,0};
    float3 rotation{0,0,0};
    float3 scale{1,1,1};
    // type-specific data (unioned by usage context, not C++ union)
    std::string assetPath;   // for Image/Text/Sound/Model
    float4 color{1,1,1,1};  // for Image/Text/Light
    float fov = 45.0f;       // for Camera
    bool ortho = false;      // for Camera
    float intensity = 1.0f;  // for Light
    float volume = 1.0f;     // for Sound
};

struct MScene {
    std::string name;
    std::string dirPath;     // e.g. "Scenes/Hello MX/"
    std::string dataPath;    // e.g. "Scenes/Hello MX/data/"
    bool valid = false;
    std::vector<std::string> resources;
    std::vector<MObject> objects;
};

/*==================== Config ====================*/
static constexpr float kPanelMargin = 8.0f;
static constexpr float kBtnH = 26.0f;
static const OX::OColor kPanelBg{20, 20, 38, 235};
static const OX::OColor kPanelBorder{255, 255, 255, 18};
static const OX::OColor kTextColor{200, 200, 220, 255};
static const OX::OColor kTextDim{110, 110, 145, 255};
static const OX::OColor kAccent{91, 141, 239, 255};
static const OX::OColor kAccentBg{91, 141, 239, 40};
static const OX::OColor kBtnBg{255, 255, 255, 10};
static const OX::OColor kBtnHoverBg{255, 255, 255, 22};
static const OX::OColor kSectionColor{90, 90, 120, 255};
static constexpr const char* kFontName = "siyuan";
static constexpr const char* kLogoPath = "Data/LOGO.jpg";

enum class AppMode { Edit, Play };

/*==================== AppState ====================*/
struct AppState {
    OX::Application app;
    OX::UIManager ui;

    Engine* engine = nullptr;
    SwapChain* swapChain = nullptr;
    Renderer* renderer = nullptr;
    Scene* scene = nullptr;
    View* view = nullptr;
    Camera* camera = nullptr;
    Entity cameraEntity; Entity sunLightEntity;
    Skybox* skybox = nullptr;
    uint32_t winWidth = 1280; uint32_t winHeight = 720;

    AppMode mode = AppMode::Edit;
    float fps = 0.0f;
    std::chrono::steady_clock::time_point lastFrameTime;

    float cameraDistance = 8.0f; float cameraAzimuth = 0.5f; float cameraElevation = 0.3f;
    float3 cameraTarget = {0.0f, 0.8f, 0.0f};
    bool mouseDragging = false; int32_t lastMouseX = 0, lastMouseY = 0;

    // Scene system
    int currentSceneIndex = -1;
    std::vector<MScene> sceneList;

    // Material / objects for current scene
    Material* unlitMat = nullptr;
    Material* texMat = nullptr;
    MaterialInstance* cubeMi = nullptr;
    MaterialInstance* planeMi = nullptr;
    MaterialInstance* logoMi = nullptr;
    Entity cubeEntity; Entity planeEntity; Entity logoEntity;
    Texture* logoTexture = nullptr;
    float cubeRotation = 0.0f;

    // UI
    ShaderRing* ringPtr = nullptr; bool ringExpanded = false;
    std::vector<OX::UIElement*> editOnly; std::vector<OX::UIElement*> playOnly;
};

/*==================== Forward declarations ====================*/
static void initFilament(AppState& st);
static void createDefaultScene(AppState& st);
static void createHelloMXScene(AppState& st);
static void renderCurrentScene(AppState& st, float dt);
static void buildEditUI(AppState& st);
static void buildPlayUI(AppState& st);
static void toggleMode(AppState& st, AppMode mode);
static void updateCamera(AppState& st);
static void cleanup(AppState& st);
static void scanScenes(AppState& st);

/*==================== Panel builders ====================*/
static OX::UIFrame* makePanel(AppState& st, float x, float y, float w, float h,
                               const char* title, bool forEdit) {
    auto fr = std::make_unique<OX::UIFrame>();
    fr->rect = OX::ORect(x, y, w, h);
    fr->setStyle(kPanelBg, kPanelBorder, 1.0f, 6.0f);
    fr->setPadding(kPanelMargin, 28.0f, kPanelMargin, kPanelMargin);
    fr->setVerticalLayout(); fr->setGap(2.0f);
    auto hdr = std::make_unique<OX::UILabel>(title);
    hdr->rect = OX::ORect(kPanelMargin, 6.0f, w - kPanelMargin * 2, 20.0f);
    hdr->fontSize = 11.0f; hdr->fontName = kFontName;
    hdr->textColor = OX::OColor(255, 255, 255, 200);
    fr->addChild(std::move(hdr));
    OX::UIFrame* ptr = fr.get();
    st.ui.addElement(std::move(fr));
    if (forEdit) st.editOnly.push_back(ptr); else st.playOnly.push_back(ptr);
    return ptr;
}
static void addSection(OX::UIFrame* p, const char* title) {
    auto lbl = std::make_unique<OX::UILabel>(title);
    lbl->rect = OX::ORect(0, 0, 0, 16.0f);
    lbl->fontSize = 9.0f; lbl->fontName = kFontName; lbl->textColor = kSectionColor;
    p->addChild(std::move(lbl));
}
static void addListItem(OX::UIFrame* p, const char* text, bool selected = false) {
    auto btn = std::make_unique<OX::UIButton>(text);
    btn->rect = OX::ORect(0, 0, 0, kBtnH);
    btn->fontSize = 11.0f; btn->fontName = kFontName; btn->cornerRadius = 4.0f; btn->textAlignX = 0.08f;
    if (selected)
        btn->setColors(kAccentBg, OX::OColor(91, 141, 239, 70), kAccentBg, kTextColor);
    else
        btn->setColors(OX::OColor(0, 0, 0, 0), kBtnHoverBg, kBtnBg, kTextDim);
    p->addChild(std::move(btn));
}

/*==================== scanScenes ====================*/
static void scanScenes(AppState& st) {
    st.sceneList.clear();
    fs::path scenesDir = "Scenes";
    if (!fs::exists(scenesDir) || !fs::is_directory(scenesDir)) {
        MLOG("[Scene] Scenes/ directory not found\n");
        return;
    }
    for (const auto& entry : fs::directory_iterator(scenesDir)) {
        if (!entry.is_directory()) continue;
        MScene sc;
        sc.name = entry.path().filename().string();
        sc.dirPath = entry.path().string();
        sc.dataPath = (entry.path() / "data").string();
        sc.valid = true;

        // Scan data/ resources
        fs::path dp = entry.path() / "data";
        if (fs::exists(dp) && fs::is_directory(dp)) {
            for (const auto& rf : fs::directory_iterator(dp)) {
                if (rf.is_regular_file())
                    sc.resources.push_back(rf.path().filename().string());
            }
        }

        st.sceneList.push_back(std::move(sc));
        MLOG("[Scene] Found: %s (%zu resources)\n", st.sceneList.back().name.c_str(),
             st.sceneList.back().resources.size());
    }
    MLOG("[Scene] Total scenes: %zu\n", st.sceneList.size());
}

/*==================== initFilament ====================*/
static void initFilament(AppState& st) {
    MLOG("[Filament] Creating engine...\n");
    auto* plat = new NoSwapWGLPlatform();
    st.engine = Engine::create(Engine::Backend::OPENGL, plat, nullptr);
    if (!st.engine) { MLOGERR; return; }

    st.swapChain = st.engine->createSwapChain((void*)st.app.getHwnd());
    st.renderer = st.engine->createRenderer();
    st.renderer->setClearOptions({{0.03f, 0.03f, 0.08f, 1.0f}, true, true});
    st.scene = st.engine->createScene();
    st.view = st.engine->createView();
    st.view->setScene(st.scene);
    st.view->setViewport(filament::Viewport{0, 0, st.winWidth, st.winHeight});
    st.view->setPostProcessingEnabled(true); st.view->setShadowingEnabled(true);

    st.cameraEntity = EntityManager::get().create();
    st.camera = st.engine->createCamera(st.cameraEntity);
    st.view->setCamera(st.camera);

    st.sunLightEntity = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
        .intensity(100000.0f).direction({0.6f, -1.0f, -0.6f})
        .castShadows(true).shadowOptions({.mapSize = 1024})
        .build(*st.engine, st.sunLightEntity);
    st.scene->addEntity(st.sunLightEntity);

    st.skybox = Skybox::Builder().color({0.06f, 0.07f, 0.14f, 1.0f}).build(*st.engine);
    st.scene->setSkybox(st.skybox);

    // Compile materials
    MLOG("[Filament] Compiling materials...\n");
    filamat::MaterialBuilder::init();

    // Unlit color material
    {
        filamat::MaterialBuilder b;
        b.name("MXUnlit")
         .material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=vec4(materialParams.baseColor.rgb,1.0);}")
         .shading(Shading::UNLIT)
         .parameter(filamat::MaterialBuilder::UniformType::FLOAT4, "baseColor")
         .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
         .platform(filamat::MaterialBuilder::Platform::DESKTOP);
        auto pkg = b.build(st.engine->getJobSystem());
        if (pkg.isValid())
            st.unlitMat = Material::Builder().package(pkg.getData(), pkg.getSize()).build(*st.engine);
        else MLOG("[Filament] UNLIT material compile FAILED\n");
    }

    // Textured unlit material
    {
        filamat::MaterialBuilder b;
        b.name("MXTexUnlit")
         .material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=texture(materialParams_tex,getUV0());}")
         .shading(Shading::UNLIT)
         .parameter("tex", filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
         .require(VertexAttribute::UV0)
         .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
         .platform(filamat::MaterialBuilder::Platform::DESKTOP);
        auto pkg = b.build(st.engine->getJobSystem());
        if (pkg.isValid())
            st.texMat = Material::Builder().package(pkg.getData(), pkg.getSize()).build(*st.engine);
        else MLOG("[Filament] TEX material compile FAILED\n");
    }
    MLOG("[Filament] Materials: unlit=%s tex=%s\n", st.unlitMat ? "OK" : "FAIL", st.texMat ? "OK" : "FAIL");
}

/*==================== createDefaultScene (index=-1, 正交+Logo) ====================*/
static void createDefaultScene(AppState& st) {
    MLOG("[Scene] Creating default logo scene...\n");
    try {

    // Orthographic camera
    float aspect = (float)st.winWidth / (float)st.winHeight;
    MLOG("[Scene] Setting ortho projection...\n");
    st.camera->setProjection(Camera::Projection::ORTHO, -aspect, aspect, -1.0f, 1.0f, 0.01f, 100.0f);
    MLOG("[Scene] Ortho projection OK\n");
    MLOG("[Scene] Setting lookAt...\n");
    st.camera->lookAt({0, 0, 1}, {0, 0, 0}, {0, 1, 0});
    MLOG("[Scene] lookAt OK\n");

    MLOG("[Scene] Checking texMat...\n");
    if (!st.texMat) {
        MLOG("[Scene] TEX material not available, skipping logo quad\n");
        return;
    }
    MLOG("[Scene] texMat OK\n");

    // Load LOGO.jpg
    MLOG("[Scene] Loading %s...\n", kLogoPath);
    int tw, th, tn;
    MLOG("[Scene] Calling stbi_load...\n");
    stbi_uc* pixels = stbi_load(kLogoPath, &tw, &th, &tn, 4);
    MLOG("[Scene] stbi_load returned %p\n", (void*)pixels);
    if (!pixels) {
        MLOG("[Scene] CANNOT load %s : %s\n", kLogoPath, stbi_failure_reason());
        MLOGERR;
        return;
    }
    MLOG("[Scene] Logo %dx%d ch=%d OK\n", tw, th, tn);

    MLOG("[Scene] Building texture...\n");
    st.logoTexture = Texture::Builder()
        .width((uint32_t)tw).height((uint32_t)th).levels(1)
        .format(Texture::InternalFormat::RGBA8)
        .sampler(Texture::Sampler::SAMPLER_2D)
        .build(*st.engine);
    MLOG("[Scene] Texture built: %p\n", (void*)st.logoTexture);
    Texture::PixelBufferDescriptor pbd(pixels, (size_t)(tw * th * 4),
        Texture::Format::RGBA, Texture::Type::UBYTE,
        [](void* buf, size_t, void*) { stbi_image_free(buf); });
    MLOG("[Scene] Setting image...\n");
    st.logoTexture->setImage(*st.engine, 0, std::move(pbd));
    MLOG("[Scene] setImage OK, flushAndWait...\n");
    st.engine->flushAndWait();
    MLOG("[Scene] flushAndWait OK\n");

    // Textured quad
    float lw = aspect * 1.2f, lh = 1.2f;
    struct Vtx { float3 pos; float2 uv; };
    Vtx v[4] = {{{-lw,-lh,0},{0,0}}, {{-lw,lh,0},{0,1}}, {{lw,lh,0},{1,1}}, {{lw,-lh,0},{1,0}}};
    uint32_t id[6] = {0,1,2,2,3,0};
    MLOG("[Scene] Creating VertexBuffer...\n");
    auto vb = VertexBuffer::Builder().vertexCount(4).bufferCount(1)
        .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
        .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, 12)
        .build(*st.engine);
    MLOG("[Scene] VB created, setting buffer...\n");
    vb->setBufferAt(*st.engine, 0, VertexBuffer::BufferDescriptor(v, sizeof(v), nullptr));
    MLOG("[Scene] VB buffer set, creating IndexBuffer...\n");
    auto ib = IndexBuffer::Builder().indexCount(6).bufferType(IndexBuffer::IndexType::UINT).build(*st.engine);
    MLOG("[Scene] IB created, setting buffer...\n");
    ib->setBuffer(*st.engine, IndexBuffer::BufferDescriptor(id, sizeof(id), nullptr));
    MLOG("[Scene] IB buffer set\n");

    TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                           TextureSampler::MagFilter::LINEAR);
    MLOG("[Scene] Creating material instance...\n");
    st.logoMi = st.texMat->createInstance();
    MLOG("[Scene] MI created: %p\n", (void*)st.logoMi);
    MLOG("[Scene] Setting tex param...\n");
    st.logoMi->setParameter("tex", st.logoTexture, sampler);
    MLOG("[Scene] tex param OK\n");

    st.logoEntity = EntityManager::get().create();
    MLOG("[Scene] Entity created, building renderable...\n");
    RenderableManager::Builder(1)
        .boundingBox({{-lw,-lh,-0.1f},{lw,lh,0.1f}})
        .material(0, st.logoMi)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib, 0, 6)
        .receiveShadows(false).castShadows(false)
        .build(*st.engine, st.logoEntity);
    MLOG("[Scene] Renderable built, adding to scene...\n");
    st.scene->addEntity(st.logoEntity);
    MLOG("[Scene] Default logo scene ready\n");

    } catch (const std::exception& e) {
        MLOG("[Scene] EXCEPTION: %s\n", e.what());
    } catch (...) {
        MLOG("[Scene] UNKNOWN EXCEPTION in createDefaultScene\n");
    }
}

/*==================== createHelloMXScene (index=0) ====================*/
static void createHelloMXScene(AppState& st) {
    MLOG("[Scene] Creating Hello MX scene...\n");

    // Perspective camera
    st.cameraDistance = 8.0f; st.cameraTarget = float3{0.0f, 0.8f, 0.0f};

    // Plane
    { float s = 10.0f;
        struct V { float3 p; };
        V v[4] = {{{-s,0,-s}},{{-s,0,s}},{{s,0,s}},{{s,0,-s}}};
        uint32_t id[6] = {0,1,2,2,3,0};
        auto vb = VertexBuffer::Builder().vertexCount(4).bufferCount(1)
            .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3).build(*st.engine);
        vb->setBufferAt(*st.engine,0,VertexBuffer::BufferDescriptor(v,sizeof(v),nullptr));
        auto ib = IndexBuffer::Builder().indexCount(6).bufferType(IndexBuffer::IndexType::UINT).build(*st.engine);
        ib->setBuffer(*st.engine,IndexBuffer::BufferDescriptor(id,sizeof(id),nullptr));
        st.planeMi = st.unlitMat->createInstance();
        st.planeMi->setParameter("baseColor",RgbaType::LINEAR,float4{0.18f,0.18f,0.20f,1.0f});
        st.planeEntity = EntityManager::get().create();
        RenderableManager::Builder(1).boundingBox({{-s,-0.1f,-s},{s,0.1f,s}})
            .material(0,st.planeMi).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,vb,ib,0,6)
            .receiveShadows(true).castShadows(false).build(*st.engine,st.planeEntity);
        st.scene->addEntity(st.planeEntity);
        st.engine->getTransformManager().setTransform(
            st.engine->getTransformManager().getInstance(st.planeEntity),
            mat4f::translation(float3{0.0f,-0.01f,0.0f}));
    }
    // Cube
    { float h = 0.75f;
        struct V { float3 p; };
        V v[8] = {{{-h,-h,-h}},{{-h,-h,h}},{{h,-h,h}},{{h,-h,-h}},{{-h,h,-h}},{{-h,h,h}},{{h,h,h}},{{h,h,-h}}};
        uint32_t id[36] = {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,2,3,7,2,7,6,1,2,6,1,6,5,3,0,4,3,4,7};
        auto vb = VertexBuffer::Builder().vertexCount(8).bufferCount(1)
            .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3).build(*st.engine);
        vb->setBufferAt(*st.engine,0,VertexBuffer::BufferDescriptor(v,sizeof(v),nullptr));
        auto ib = IndexBuffer::Builder().indexCount(36).bufferType(IndexBuffer::IndexType::UINT).build(*st.engine);
        ib->setBuffer(*st.engine,IndexBuffer::BufferDescriptor(id,sizeof(id),nullptr));
        st.cubeMi = st.unlitMat->createInstance();
        st.cubeMi->setParameter("baseColor",RgbaType::LINEAR,float4{0.2f,0.5f,0.9f,1.0f});
        st.cubeEntity = EntityManager::get().create();
        RenderableManager::Builder(1)
            .boundingBox({{-h*1.5f,-h*1.5f,-h*1.5f},{h*1.5f,h*1.5f,h*1.5f}})
            .material(0,st.cubeMi).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,vb,ib,0,36)
            .receiveShadows(true).castShadows(true).build(*st.engine,st.cubeEntity);
        st.scene->addEntity(st.cubeEntity);
        st.engine->getTransformManager().setTransform(
            st.engine->getTransformManager().getInstance(st.cubeEntity),
            mat4f::translation(float3{0.0f,1.0f,0.0f}) * mat4f::scaling(float3{1.5f}));
    }
    MLOG("[Scene] Hello MX scene ready\n");
}

/*==================== renderCurrentScene ====================*/
static void renderCurrentScene(AppState& st, float dt) {
    if (st.currentSceneIndex < 0) {
        // Default scene: ortho camera shows logo, cube/plane hidden by ortho
        // No per-frame animation needed for default logo
        return;
    }
    // Scene 0 = Hello MX: cube rotation animation
    st.cubeRotation += dt * 0.8f;
    auto& tcm = st.engine->getTransformManager();
    auto ti = tcm.getInstance(st.cubeEntity);
    if (ti.isValid()) {
        tcm.setTransform(ti, mat4f::translation(float3{0,1,0}) *
            mat4f::rotation(st.cubeRotation, float3{0,1,0}) * mat4f::scaling(float3{1.5f}));
    }
}

/*==================== buildEditUI ====================*/
static void buildEditUI(AppState& st) {
    float w = (float)st.winWidth, h = (float)st.winHeight;

    // Mode switch
    { float mw = 180.0f, mh = 32.0f;
        auto bar = std::make_unique<OX::UIFrame>();
        bar->rect = OX::ORect((w-mw)/2.0f, 6.0f, mw, mh);
        bar->setStyle(OX::OColor(24,24,42,220), kPanelBorder, 1.0f, 8.0f);
        bar->setHorizontalLayout(); bar->setPadding(3,3,3,3); bar->setGap(2);
        auto be = std::make_unique<OX::UIButton>("编辑模式");
        be->rect = OX::ORect(0,0,0,mh-6); be->fontSize=12; be->fontName=kFontName; be->cornerRadius=6;
        be->setColors(OX::OColor(255,255,255,25),OX::OColor(255,255,255,35),OX::OColor(255,255,255,25),OX::OColor(255,255,255,230));
        be->onClick = [&](){ toggleMode(st, AppMode::Edit); };
        bar->addChild(std::move(be));
        auto bp = std::make_unique<OX::UIButton>("播放模式");
        bp->rect = OX::ORect(0,0,0,mh-6); bp->fontSize=12; bp->fontName=kFontName; bp->cornerRadius=6;
        bp->setColors(OX::OColor(0,0,0,0),OX::OColor(255,255,255,10),OX::OColor(0,0,0,0),kTextDim);
        bp->onClick = [&](){ toggleMode(st, AppMode::Play); };
        bar->addChild(std::move(bp));
        st.ui.addElement(std::move(bar));
    }

    // Resource panel
    { auto* p = makePanel(st, kPanelMargin, 48, 210, 360, "资源库", true);
        addSection(p, "🖼 图片"); addListItem(p, "  bg_mountain.jpg  4K");
        addListItem(p, "  sky_panorama.jpg  8K");
        addSection(p, "🎬 模型"); addListItem(p, "  pine_tree.glb  12KA");
        addSection(p, "🎵 音频"); addListItem(p, "  ambient_wind.mp3");
        addSection(p, "✨ ShaderToy"); addListItem(p, "  ocean_waves.fs"); addListItem(p, "  aurora_sky.fs");
    }

    // Layer panel
    { auto* p = makePanel(st, kPanelMargin, 420, 210, 200, "图层", true);
        addListItem(p, "👁 ☀️ Directional Light", true); addListItem(p, "👁 🌍 Skybox");
        addListItem(p, "👁 🟦 Hello Cube"); addListItem(p, "👁 ⬛ Ground Plane");
        addListItem(p, "◌ 💨 Particles (待)"); addListItem(p, "◌ 🎵 Audio (待)");
    }

    // Property panel
    { auto* p = makePanel(st, w-210-kPanelMargin, 48, 210, 310, "属性 · 场景", true);
        addSection(p, "变换 Transform");
        addListItem(p, "  位置  [ 0.00  0.00  0.00 ]"); addListItem(p, "  旋转  [ 0.00  0.00  0.00 ]");
        addListItem(p, "  缩放  [ 1.00  1.00  1.00 ]");
        addSection(p, "材质 Material"); addListItem(p, "  粗糙度    0.65"); addListItem(p, "  金属度    0.05");
        addListItem(p, "  投射阴影  ☑");
        addSection(p, "光照 Lighting"); addListItem(p, "  强度      100000");
    }

    // Toolbar
    { float tw = 600;
        auto tb = std::make_unique<OX::UIFrame>();
        tb->rect = OX::ORect((w-tw)/2, h-44-6, tw, 44);
        tb->setStyle(OX::OColor(24,24,42,220), kPanelBorder, 1, 10);
        tb->setHorizontalLayout(); tb->setPadding(6,7,6,7); tb->setGap(4);
        auto mkBtn = [&](OX::UIFrame* f, const char* t, bool accent=false) {
            auto btn = std::make_unique<OX::UIButton>(t);
            btn->rect = OX::ORect(0,0,0,30); btn->fontSize=12; btn->fontName=kFontName; btn->cornerRadius=6;
            if (accent) btn->setColors(kAccent, OX::OColor(91,141,239,150), OX::OColor(91,141,239,200), OX::OColor(255,255,255,255));
            else btn->setColors(OX::OColor(0,0,0,0), kBtnHoverBg, kBtnBg, kTextDim);
            f->addChild(std::move(btn));
        };
        mkBtn(tb.get(), "➕ 创建"); mkBtn(tb.get(), "📥 导入"); mkBtn(tb.get(), "▶ 测试", true);
        mkBtn(tb.get(), "💾 保存"); mkBtn(tb.get(), "⏺ 录屏"); mkBtn(tb.get(), "📂 场景库"); mkBtn(tb.get(), "📝 脚本");
        auto sl = std::make_unique<OX::UILabel>("场景: Hello MX");
        sl->rect = OX::ORect(0,0,120,30); sl->fontSize=11; sl->fontName=kFontName; sl->textColor=kTextDim;
        tb->addChild(std::move(sl));
        OX::UIFrame* tbp = tb.get(); st.ui.addElement(std::move(tb)); st.editOnly.push_back(tbp);
    }

    // Lua console
    { auto* p = makePanel(st, kPanelMargin, h-220-56, 380, 220, "scene.lua", true);
        auto txt = std::make_unique<OX::UILabel>(
            " 1  -- Hello MX scene.lua\n 2  function onInit(scene)\n"
            " 3    print(\"[Hello MX] ready\")\n 4  end\n"
            " 5  function onUpdate(dt)\n 6    scene:rotate(\"cube\",0,dt*0.8,0)\n 7  end\n");
        txt->rect = OX::ORect(0,0,0,0); txt->fontSize=10; txt->fontName="Consolas";
        txt->textColor = OX::OColor(140,188,140,255); p->addChild(std::move(txt));
    }
}

/*==================== buildPlayUI ====================*/
static void buildPlayUI(AppState& st) {
    float w = (float)st.winWidth, h = (float)st.winHeight;
    { auto ring = std::make_unique<ShaderRing>();
        ring->rect = OX::ORect((w-52)/2, h-80, 52, 52);
        st.ringPtr = ring.get(); st.ui.addElement(std::move(ring)); st.playOnly.push_back(st.ringPtr);
    }
    { float bw = 300, bh = 44;
        auto bar = std::make_unique<OX::UIFrame>();
        bar->rect = OX::ORect((w-bw)/2, h-72, bw, bh);
        bar->setStyle(OX::OColor(24,24,42,220), kPanelBorder, 1, 22);
        bar->setHorizontalLayout(); bar->setPadding(6,6,6,6); bar->setGap(6); bar->visible = false;
        auto mk = [&](OX::UIFrame* f, const char* t) {
            auto btn = std::make_unique<OX::UIButton>(t);
            btn->rect = OX::ORect(0,0,0,32); btn->fontSize=14; btn->cornerRadius=16;
            btn->setColors(OX::OColor(0,0,0,0), kBtnHoverBg, kBtnBg, kTextColor);
            f->addChild(std::move(btn));
        };
        mk(bar.get(), "⏮"); mk(bar.get(), "▶"); mk(bar.get(), "⏭");
        mk(bar.get(), "✏️"); mk(bar.get(), "⚙"); mk(bar.get(), "🖥");
        OX::UIFrame* bp = bar.get(); st.ui.addElement(std::move(bar)); st.playOnly.push_back(bp);
    }
}

/*==================== toggleMode ====================*/
static void toggleMode(AppState& st, AppMode mode) {
    st.mode = mode;
    bool isEdit = (mode == AppMode::Edit);
    for (auto* el : st.editOnly) el->visible = isEdit;
    for (auto* el : st.playOnly) el->visible = !isEdit;
    if (st.ringPtr) st.ringPtr->visible = !isEdit;
}

/*==================== updateCamera ====================*/
static void updateCamera(AppState& st) {
    if (st.currentSceneIndex < 0) return; // Ortho camera already set in createDefaultScene

    float aspect = (st.winWidth > 0 && st.winHeight > 0) ? (float)st.winWidth / (float)st.winHeight : 1.0f;
    st.camera->setProjection(50.0f, aspect, 0.05f, 1000.0f);
    float x = st.cameraDistance * cosf(st.cameraElevation) * sinf(st.cameraAzimuth);
    float y = st.cameraDistance * sinf(st.cameraElevation);
    float z = st.cameraDistance * cosf(st.cameraElevation) * cosf(st.cameraAzimuth);
    st.camera->lookAt({x+st.cameraTarget.x, y+st.cameraTarget.y, z+st.cameraTarget.z}, st.cameraTarget, {0.0f,1.0f,0.0f});
}

/*==================== cleanup ====================*/
static void cleanup(AppState& st) {
    if (st.engine) {
        st.engine->destroy(st.view); st.engine->destroy(st.scene);
        st.engine->destroy(st.renderer); st.engine->destroy(st.swapChain);
        st.engine->destroy(st.skybox);
        st.engine->destroy(st.logoTexture);
        st.engine->destroy(st.cubeMi); st.engine->destroy(st.planeMi); st.engine->destroy(st.logoMi);
        st.engine->destroy(st.unlitMat); st.engine->destroy(st.texMat);
        st.engine->destroyCameraComponent(st.cameraEntity);
        EntityManager::get().destroy(st.cubeEntity); EntityManager::get().destroy(st.planeEntity);
        EntityManager::get().destroy(st.logoEntity);
        EntityManager::get().destroy(st.sunLightEntity); EntityManager::get().destroy(st.cameraEntity);
        Engine::destroy(&st.engine);
    }
    filamat::MaterialBuilder::shutdown();
}

/*==================== MAIN ====================*/
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    OX::Core::initConsole();
    mxLogOpen();
    SetUnhandledExceptionFilter(mxCrashHandler);
    timeBeginPeriod(1);
    MLOG("[MXPaper] === MXPaper v3 ===\n");

    AppState st;

    // Window
    OX::WindowDesc desc;
    desc.title = L"MXPaper"; desc.width = st.winWidth; desc.height = st.winHeight;
    desc.enableOpenGL = true; desc.stencilBits = 0;
    if (!st.app.create(&desc)) {
        MLOGERR; system("pause"); timeEndPeriod(1); return 1;
    }
    MLOG("[MXPaper] Window OK\n");

    typedef BOOL (WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
    auto wsi = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (wsi) wsi(1);
    st.app.getSize(&st.winWidth, &st.winHeight);

    // Filament
    initFilament(st);
    if (!st.engine) { MLOGERR; system("pause"); st.app.destroy(); timeEndPeriod(1); return 1; }
    MLOG("[MXPaper] Filament OK\n");

    // Scenes
    scanScenes(st);
    if (st.sceneList.empty()) MLOG("[MXPaper] No scenes found, using default only\n");

    // Default scene (always render if index == -1)
    createDefaultScene(st);

    // Test: switch to Hello MX if available
    for (size_t i = 0; i < st.sceneList.size(); ++i) {
        if (st.sceneList[i].name == "Hello MX") {
            createHelloMXScene(st);
            st.currentSceneIndex = (int)i;
            MLOG("[MXPaper] Switched to scene: Hello MX\n");
            break;
        }
    }

    // ThorVG / OUI
    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        MLOGERR; system("pause"); cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    bool fontOk = OX::UIManager::loadFont(kFontName, "Data/siyuan.ttf");
    MLOG("[MXPaper] Font '%s': %s\n", kFontName, fontOk ? "OK" : "FAILED");

    if (!st.ui.init(st.app.getGLRC(), (int)st.winWidth, (int)st.winHeight)) {
        MLOGERR; system("pause"); tvg::Initializer::term(); cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }

    // Build UI
    buildEditUI(st); buildPlayUI(st);
    toggleMode(st, AppMode::Edit);
    if (st.currentSceneIndex >= 0) updateCamera(st);

    // Input
    st.app.setMouseWheelCallback([&](int32_t x, int32_t y, int delta) {
        if (st.mode == AppMode::Edit) st.ui.handleMWheel(x, y, delta);
        else if (st.currentSceneIndex >= 0) {
            st.cameraDistance -= delta * 0.005f * st.cameraDistance;
            st.cameraDistance = std::clamp(st.cameraDistance, 0.5f, 200.0f);
        }
    });
    st.app.setMouseMoveCallback([&](int32_t x, int32_t y) {
        st.ui.handleMMove(x, y);
        if (st.mode == AppMode::Play) {
            bool near = (y > (int32_t)st.winHeight - 80);
            if (near != st.ringExpanded) {
                st.ringExpanded = near;
                if (st.playOnly.size() >= 2) { st.playOnly[1]->visible = near; st.ringPtr->visible = !near; }
            }
        }
        if (st.mouseDragging && st.mode == AppMode::Play && st.currentSceneIndex >= 0) {
            float dx = (float)(x - st.lastMouseX), dy = (float)(y - st.lastMouseY);
            st.cameraAzimuth -= dx * 0.008f; st.cameraElevation += dy * 0.008f;
            st.cameraElevation = std::clamp(st.cameraElevation, -1.4f, 1.4f);
            st.lastMouseX = x; st.lastMouseY = y;
        }
    });
    st.app.setMouseButtonCallback([&](int32_t x, int32_t y, int32_t button, bool pressed) {
        if (button == 0) { if (pressed) st.ui.handleMDown(x, y); else st.ui.handleMUp(x, y); }
        if (button == 1) { st.mouseDragging = pressed; st.lastMouseX = x; st.lastMouseY = y; }
    });
    st.app.setKeyDownCallback([&](int32_t key) {
        st.ui.handleKDown(key);
        if (key == 9) toggleMode(st, st.mode == AppMode::Edit ? AppMode::Play : AppMode::Edit);
        if (key == 32 && st.mode == AppMode::Play) {
            st.ringExpanded = !st.ringExpanded;
            if (st.playOnly.size() >= 2) { st.playOnly[1]->visible = st.ringExpanded; st.ringPtr->visible = !st.ringExpanded; }
        }
    });
    st.app.setKeyUpCallback([&](int32_t key) { st.ui.handleKUp(key); });

    st.lastFrameTime = std::chrono::steady_clock::now();
    st.app.setTargetFPS(0);
    MLOG("[MXPaper] Entering main loop (scene=%d)...\n", st.currentSceneIndex);

    // Main loop
    int frameCount = 0; float fpsTimer = 0.0f;
    while (!st.app.shouldClose()) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - st.lastFrameTime).count();
        st.lastFrameTime = now;
        st.app.pollEvents();

        frameCount++; fpsTimer += dt;
        if (fpsTimer >= 0.5f) { st.fps = frameCount / fpsTimer; frameCount = 0; fpsTimer = 0.0f; }

        renderCurrentScene(st, dt);
        if (st.currentSceneIndex >= 0) updateCamera(st);

        if (st.renderer->beginFrame(st.swapChain)) { st.renderer->render(st.view); st.renderer->endFrame(); }
        st.engine->flushAndWait();

        wglMakeCurrent(st.app.getDC(), st.app.getGLRC());
        st.ui.update(dt); st.ui.render();

        st.app.swapBuffers();
        st.app.waitForNextFrame();
    }

    MLOG("[MXPaper] Exiting...\n");
    st.ui.destroy(); tvg::Initializer::term(); cleanup(st); st.app.destroy();
    timeEndPeriod(1);
    return 0;
}
