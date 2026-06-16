/****************************************************************************
 * 标题: TEST-Filament-GLTF - Filament GLTF 模型浏览器 + OUI 控制面板
 * 功能: 加载并浏览 E:\MX\Data\models 中的 GLTF/GLB 模型，支持 IBL、阴影、动画
 * 架构: 基于 TEST-Filament-OUI 的分层渲染方案 (Filament 3D + ThorVG UI)
 ****************************************************************************/

#include "ocore.h"
#include "oapp.h"
#include "oui.h"

// Filament headers
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
#include <filament/IndirectLight.h>
#include <filament/Viewport.h>
#include <filament/Texture.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>
#include <utils/Panic.h>

#include <filamat/MaterialBuilder.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/FilamentInstance.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/Animator.h>
#include <gltfio/MaterialProvider.h>

#include <ktxreader/Ktx1Reader.h>
#include <image/Ktx1Bundle.h>

#include <windows.h>
#include <wingdi.h>
#include <mmsystem.h>
#include <backend/platforms/PlatformWGL.h>

#include <cmath>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

using namespace filament;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

// Filament libs (/MT) — 相对编译目录的父目录
#define FILAMENT_LIB_PATH "../filament-v1.71.6-windows/lib/x86_64/mt/"
#pragma comment(lib, FILAMENT_LIB_PATH "filament.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "backend.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluegl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filabridge.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filaflat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filamat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "utils.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "ibl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "geometry.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "shaders.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "stb.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "zstd.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "abseil.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "dracodec.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "meshoptimizer.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "mikktspace.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "getopt.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "uberzlib.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "uberarchive.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluevk.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "smol-v.lib")

// gltfio & ktxreader
#pragma comment(lib, FILAMENT_LIB_PATH "gltfio.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "gltfio_core.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "ktxreader.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "image.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "basis_transcoder.lib")

// ThorVG lib (/MT)
#pragma comment(lib, "e:/MX/3rd/lib/libthorvg_mt.lib")

/*==================== 自定义 WGL Platform：禁用自管 SwapBuffers ====================*/
class NoSwapWGLPlatform : public filament::backend::PlatformWGL {
public:
    void* filamentHGLRC = nullptr;
    filament::backend::Driver* createDriver(void* sharedGLContext,
            const Platform::DriverConfig& driverConfig) override {
        filament::backend::Driver* driver = PlatformWGL::createDriver(nullptr, driverConfig);
        if (driver) { filamentHGLRC = wglGetCurrentContext(); }
        return driver;
    }
    void commit(filament::backend::Platform::SwapChain* swapChain) noexcept override {
        // 由 oapp.h 统一管理 SwapBuffers
    }
};

/*==================== 模型信息 ====================*/
struct TestModel {
    std::string name;
    std::string path;
    float defaultDistance = 3.0f;
};

std::vector<TestModel> testModels;
int currentModelIndex = 0;

// 模型目录 (使用绝对路径指向 E:\MX\Data)
constexpr const char* MODELS_DIR = "e:/MX/Data/models/";
constexpr const char* IBL_DIR = "e:/MX/Data/ibl/lightroom_14b";

/*==================== 应用状态 ====================*/
struct AppState {
    OX::Application app;
    OX::UIManager ui;

    // Filament core
    Engine* engine = nullptr;
    SwapChain* swapChain = nullptr;
    Renderer* renderer = nullptr;
    Scene* scene = nullptr;
    View* view = nullptr;
    Camera* camera = nullptr;
    Entity cameraEntity;

    // Lighting & env
    Skybox* skybox = nullptr;
    IndirectLight* indirectLight = nullptr;
    Entity sunLightEntity;
    float lightIntensity = 110000.0f;

    // Ground plane
    Entity groundEntity;
    VertexBuffer* groundVb = nullptr;
    IndexBuffer* groundIb = nullptr;
    Material* groundMat = nullptr;
    MaterialInstance* groundMi = nullptr;

    // glTF
    gltfio::AssetLoader* assetLoader = nullptr;
    gltfio::ResourceLoader* resourceLoader = nullptr;
    gltfio::FilamentAsset* asset = nullptr;
    gltfio::FilamentInstance* instance = nullptr;
    gltfio::MaterialProvider* materialProvider = nullptr;
    gltfio::TextureProvider* stbDecoder = nullptr;
    gltfio::TextureProvider* ktxDecoder = nullptr;

    // Camera orbit
    float cameraDistance = 5.0f;
    float cameraAzimuth = 0.0f;
    float cameraElevation = 0.3f;
    filament::math::float3 cameraTarget = {0.0f, 0.0f, 0.0f};
    bool autoRotate = false;
    float rotationSpeed = 0.5f;

    // Mouse interaction
    bool mouseDragging = false;
    int32_t lastMouseX = 0;
    int32_t lastMouseY = 0;
    bool mousePanning = false;
    int32_t panLastX = 0;
    int32_t panLastY = 0;

    // Animation
    bool animPlaying = true;
    float animSpeed = 1.0f;
    size_t currentAnimIndex = 0;
    size_t animCount = 0;
    float animTime = 0.0f;

    // Model scale
    float modelScale = 1.0f;

    // FPS
    float fps = 0.0f;
    std::chrono::steady_clock::time_point lastFrameTime;

    // UI text buffers
    char fpsText[64] = "FPS: 0";
    char modelText[256] = "Model: None";
    char animText[256] = "Animation: None";
    char scaleText[64] = "Scale: 1.0x";
    char lightText[64] = "Light: 110k";

    // UI element pointers (for dynamic update)
    OX::UILabel* fpsLabelPtr = nullptr;
    OX::UILabel* modelLabelPtr = nullptr;
    OX::UILabel* animLabelPtr = nullptr;
    OX::UILabel* scaleLabelPtr = nullptr;
    OX::UILabel* lightLabelPtr = nullptr;
};

/*==================== 扫描模型目录 ====================*/
static void scanModels() {
    testModels.clear();
    fs::path modelsDir(MODELS_DIR);
    if (!fs::exists(modelsDir) || !fs::is_directory(modelsDir)) {
        OX_LOG("[MODEL] Models directory not found: %s\n", MODELS_DIR);
        return;
    }
    // GLB files
    for (const auto& entry : fs::directory_iterator(modelsDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".glb") {
            std::string name = entry.path().stem().string();
            testModels.push_back({name, entry.path().string(), 3.0f});
        }
    }
    // GLTF directories (scene.gltf)
    for (const auto& entry : fs::directory_iterator(modelsDir)) {
        if (entry.is_directory()) {
            std::string dirname = entry.path().filename().string();
            fs::path gltfPath = entry.path() / "scene.gltf";
            if (fs::exists(gltfPath)) {
                testModels.push_back({dirname, gltfPath.string(), 8.0f});
            }
        }
    }
    OX_LOG("[MODEL] Found %zu models\n", testModels.size());
}

/*==================== 读取 SH 系数文件 ====================*/
static bool parseSphericalHarmonics(const char* path, float3 sh[9]) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string line;
    int idx = 0;
    while (std::getline(file, line) && idx < 9) {
        float r, g, b;
        if (sscanf(line.c_str(), "( %f, %f, %f)", &r, &g, &b) == 3) {
            sh[idx++] = float3(r, g, b);
        }
    }
    return idx == 9;
}

/*==================== 加载 IBL (KTX + SH) ====================*/
static bool loadIBL(AppState& state) {
    fs::path iblPath(IBL_DIR);
    if (!fs::exists(iblPath)) {
        OX_LOG("[IBL] IBL directory not found: %s\n", IBL_DIR);
        return false;
    }

    fs::path iblKtx = iblPath / "lightroom_14b_ibl.ktx";
    fs::path skyKtx = iblPath / "lightroom_14b_skybox.ktx";
    fs::path shPath = iblPath / "sh.txt";

    if (!fs::exists(iblKtx) || !fs::exists(skyKtx) || !fs::exists(shPath)) {
        OX_LOG("[IBL] Missing IBL files\n");
        return false;
    }

    // Read IBL KTX
    std::ifstream iblFile(iblKtx, std::ios::binary);
    std::vector<uint8_t> iblData((std::istreambuf_iterator<char>(iblFile)),
                                  std::istreambuf_iterator<char>());
    image::Ktx1Bundle* iblBundle = new image::Ktx1Bundle(iblData.data(), iblData.size());
    Texture* iblTexture = ktxreader::Ktx1Reader::createTexture(state.engine, iblBundle, false);
    if (!iblTexture) {
        OX_LOG("[IBL] Failed to create IBL texture\n");
        delete iblBundle;
        return false;
    }

    // Read Skybox KTX
    std::ifstream skyFile(skyKtx, std::ios::binary);
    std::vector<uint8_t> skyData((std::istreambuf_iterator<char>(skyFile)),
                                  std::istreambuf_iterator<char>());
    image::Ktx1Bundle* skyBundle = new image::Ktx1Bundle(skyData.data(), skyData.size());
    Texture* skyTexture = ktxreader::Ktx1Reader::createTexture(state.engine, skyBundle, false);
    if (!skyTexture) {
        OX_LOG("[IBL] Failed to create skybox texture\n");
        state.engine->destroy(iblTexture);
        delete skyBundle;
        return false;
    }

    // Wait for texture upload to complete before local vectors go out of scope
    state.engine->flushAndWait();

    // Parse SH
    float3 sh[9];
    if (!parseSphericalHarmonics(shPath.string().c_str(), sh)) {
        OX_LOG("[IBL] Failed to parse SH coefficients\n");
        state.engine->destroy(iblTexture);
        state.engine->destroy(skyTexture);
        return false;
    }

    // Create IndirectLight
    state.indirectLight = IndirectLight::Builder()
        .reflections(iblTexture)
        .irradiance(9, sh)
        .intensity(30000.0f)
        .build(*state.engine);
    state.scene->setIndirectLight(state.indirectLight);

    // Create Skybox
    state.skybox = Skybox::Builder()
        .environment(skyTexture)
        .build(*state.engine);
    state.scene->setSkybox(state.skybox);

    OX_LOG("[IBL] Loaded successfully\n");
    return true;
}

/*==================== 创建地面平面 ====================*/
static void createGroundPlane(AppState& state) {
    static bool filamatInit = false;
    if (!filamatInit) {
        filamat::MaterialBuilder::init();
        filamatInit = true;
    }

    filamat::MaterialBuilder builder;
    builder.name("GroundShadow")
        .material(R"FILAMENT(
            void material(inout MaterialInputs material) {
                prepareMaterial(material);
                material.baseColor = vec4(0.12, 0.12, 0.12, 1.0);
            }
        )FILAMENT")
        .shading(Shading::UNLIT)
        .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
        .platform(filamat::MaterialBuilder::Platform::DESKTOP);

    filamat::Package package = builder.build(state.engine->getJobSystem());
    if (!package.isValid()) {
        OX_LOG("[GROUND] Material compilation failed\n");
        return;
    }

    state.groundMat = Material::Builder()
        .package(package.getData(), package.getSize())
        .build(*state.engine);
    state.groundMi = state.groundMat->createInstance();

    // Large ground plane
    float ext = 50.0f;
    float3 vertices[4] = {
        {-ext, 0.0f, -ext},
        {-ext, 0.0f,  ext},
        { ext, 0.0f,  ext},
        { ext, 0.0f, -ext}
    };
    uint32_t indices[6] = {0, 1, 2, 2, 3, 0};

    state.groundVb = VertexBuffer::Builder()
        .vertexCount(4)
        .bufferCount(1)
        .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
        .build(*state.engine);
    state.groundVb->setBufferAt(*state.engine, 0,
        VertexBuffer::BufferDescriptor(vertices, sizeof(vertices), nullptr));

    state.groundIb = IndexBuffer::Builder()
        .indexCount(6)
        .bufferType(IndexBuffer::IndexType::UINT)
        .build(*state.engine);
    state.groundIb->setBuffer(*state.engine,
        IndexBuffer::BufferDescriptor(indices, sizeof(indices), nullptr));

    state.groundEntity = EntityManager::get().create();
    RenderableManager::Builder(1)
        .boundingBox({{-ext, -0.1f, -ext}, {ext, 0.1f, ext}})
        .material(0, state.groundMi)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, state.groundVb, state.groundIb, 0, 6)
        .receiveShadows(true)
        .castShadows(false)
        .build(*state.engine, state.groundEntity);
    state.scene->addEntity(state.groundEntity);
}

/*==================== Panic 保护全局变量 ====================*/
#include <csetjmp>
static jmp_buf g_panicJmpBuf;
static void filamentPanicHandler(void*, utils::Panic const&) {
    longjmp(g_panicJmpBuf, 1);
}

/*==================== 卸载当前模型 ====================*/
static void unloadCurrentModel(AppState& state) {
    if (state.asset) {
        if (state.instance) {
            state.scene->removeEntities(state.instance->getEntities(), state.instance->getEntityCount());

            // Manually destroy renderables first to release MaterialInstance references
            auto& rcm = state.engine->getRenderableManager();
            size_t entityCount = state.instance->getEntityCount();
            const Entity* entities = state.instance->getEntities();
            for (size_t i = 0; i < entityCount; ++i) {
                rcm.destroy(entities[i]);
            }

            // Manually destroy material instances (workaround for gltfio variant leak)
            size_t miCount = state.instance->getMaterialInstanceCount();
            const MaterialInstance* const* mis = state.instance->getMaterialInstances();
            std::vector<MaterialInstance*> toDestroy;
            for (size_t i = 0; i < miCount; ++i) {
                toDestroy.push_back(const_cast<MaterialInstance*>(mis[i]));
            }
            state.instance->detachMaterialInstances();
            for (auto mi : toDestroy) {
                state.engine->destroy(mi);
            }
        }
        state.assetLoader->destroyAsset(state.asset);
        state.asset = nullptr;
        state.instance = nullptr;
    }
}

/*==================== 安全加载资产（panic 保护） ====================*/

static bool safeCreateAsset(AppState* state, const uint8_t* data, uint32_t size) {
    if (setjmp(g_panicJmpBuf) == 0) {
        utils::Panic::setPanicHandler(filamentPanicHandler, nullptr);
        state->asset = state->assetLoader->createAsset(data, size);
        if (state->asset) {
            state->instance = state->asset->getInstance();
            if (state->instance) {
                state->resourceLoader->loadResources(state->asset);
            }
        }
        utils::Panic::setPanicHandler(nullptr, nullptr);
        return state->asset != nullptr && state->instance != nullptr;
    } else {
        // Panic occurred inside filament (e.g. bone count > 256)
        utils::Panic::setPanicHandler(nullptr, nullptr);
        if (state->asset) {
            state->assetLoader->destroyAsset(state->asset);
            state->asset = nullptr;
        }
        state->instance = nullptr;
        return false;
    }
}

/*==================== 加载当前模型 ====================*/
static bool loadCurrentModel(AppState& state) {
    if (testModels.empty()) {
        snprintf(state.modelText, sizeof(state.modelText), "Model: No models found");
        return false;
    }

    int modelCount = static_cast<int>(testModels.size());
    if (currentModelIndex < 0) currentModelIndex = modelCount - 1;
    if (currentModelIndex >= modelCount) currentModelIndex = 0;

    const std::string& modelPath = testModels[currentModelIndex].path;
    float defaultDist = testModels[currentModelIndex].defaultDistance;

    OX_LOG("Loading model [%d/%zu]: %s\n", currentModelIndex + 1, testModels.size(), modelPath.c_str());
    snprintf(state.modelText, sizeof(state.modelText), "Model [%d/%zu]: %s",
             currentModelIndex + 1, testModels.size(), testModels[currentModelIndex].name.c_str());

    // Destroy previous asset
    unloadCurrentModel(state);

    // Ensure material provider and resource loader exist
    if (!state.materialProvider) {
        state.materialProvider = gltfio::createJitShaderProvider(state.engine, false, {});
        state.assetLoader = gltfio::AssetLoader::create({state.engine, state.materialProvider, nullptr});
    }
    if (!state.resourceLoader) {
        gltfio::ResourceConfiguration config = {};
        config.engine = state.engine;
        config.gltfPath = modelPath.c_str();
        config.normalizeSkinningWeights = true;
        state.resourceLoader = new gltfio::ResourceLoader(config);
        state.stbDecoder = gltfio::createStbProvider(state.engine);
        state.ktxDecoder = gltfio::createKtx2Provider(state.engine);
        state.resourceLoader->addTextureProvider("image/png", state.stbDecoder);
        state.resourceLoader->addTextureProvider("image/jpeg", state.stbDecoder);
        state.resourceLoader->addTextureProvider("image/ktx2", state.ktxDecoder);
    } else {
        gltfio::ResourceConfiguration newConfig = {};
        newConfig.engine = state.engine;
        newConfig.gltfPath = modelPath.c_str();
        newConfig.normalizeSkinningWeights = true;
        state.resourceLoader->setConfiguration(newConfig);
    }

    // Load asset
    std::ifstream file(modelPath, std::ios::binary);
    if (!file.is_open()) {
        OX_LOG("[GLTF] Failed to open file: %s\n", modelPath.c_str());
        return false;
    }
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    file.close();

    bool assetCreated = safeCreateAsset(&state, buffer.data(), (uint32_t)buffer.size());
    if (!assetCreated) {
        OX_LOG("[GLTF] Failed to create asset (possible bone count limit exceeded)\n");
        if (state.asset) {
            state.assetLoader->destroyAsset(state.asset);
            state.asset = nullptr;
            state.instance = nullptr;
        }
        return false;
    }

    // Add to scene
    state.scene->addEntities(state.instance->getEntities(), state.instance->getEntityCount());

    // Clear post-processing frame history to avoid artifacts from previous model
    if (state.view) {
        state.view->clearFrameHistory(*state.engine);
    }

    // Update camera distance based on bounding box
    const auto& aabb = state.asset->getBoundingBox();
    float3 extent = aabb.extent();
    float maxExtent = std::max({extent.x, extent.y, extent.z});
    if (maxExtent > 0.0f) {
        state.cameraDistance = maxExtent * 2.5f;
    } else {
        state.cameraDistance = defaultDist;
    }
    state.cameraAzimuth = 0.0f;
    state.cameraElevation = 0.3f;
    state.cameraTarget = aabb.center();
    state.modelScale = 1.0f;
    state.animTime = 0.0f;

    // Animation info
    auto* animator = state.instance->getAnimator();
    state.animCount = animator ? animator->getAnimationCount() : 0;
    state.currentAnimIndex = 0;
    if (state.animCount > 0) {
        const char* animName = animator->getAnimationName(0);
        snprintf(state.animText, sizeof(state.animText), "Anim [1/%zu]: %s",
                 state.animCount, animName ? animName : "(unnamed)");
    } else {
        snprintf(state.animText, sizeof(state.animText), "Animation: None (Static)");
    }

    OX_LOG("[GLTF] Loaded: %zu entities, %zu anims\n",
           state.instance->getEntityCount(), state.animCount);
    return true;
}

/*==================== 更新相机 ====================*/
static void updateCamera(AppState& state, uint32_t w, uint32_t h) {
    float aspect = (w > 0 && h > 0) ? (float)w / (float)h : 1.0f;
    state.camera->setProjection(45.0, aspect, 0.01, 1000.0);

    float x = state.cameraDistance * cosf(state.cameraElevation) * sinf(state.cameraAzimuth);
    float y = state.cameraDistance * sinf(state.cameraElevation);
    float z = state.cameraDistance * cosf(state.cameraElevation) * cosf(state.cameraAzimuth);
    float3 eye = {x + state.cameraTarget.x, y + state.cameraTarget.y, z + state.cameraTarget.z};
    float3 up = {0.0f, 1.0f, 0.0f};
    state.camera->lookAt(eye, state.cameraTarget, up);
}

/*==================== 更新动画 ====================*/
static void updateAnimation(AppState& state, float deltaTime) {
    if (!state.instance || !state.animPlaying || state.animCount == 0) return;
    auto* animator = state.instance->getAnimator();
    if (!animator) return;
    state.animTime += deltaTime * state.animSpeed;
    animator->applyAnimation(state.currentAnimIndex, state.animTime);
    animator->updateBoneMatrices();
}

/*==================== 更新模型变换（缩放 + 自动旋转） ====================*/
static void updateModelTransform(AppState& state, float deltaTime) {
    if (!state.instance) return;
    auto& tcm = state.engine->getTransformManager();
    auto root = state.instance->getRoot();
    auto ti = tcm.getInstance(root);
    if (!ti.isValid()) return;

    if (state.autoRotate) {
        state.cameraAzimuth += deltaTime * state.rotationSpeed;
    }

    mat4f scale = mat4f::scaling(float3(state.modelScale));
    tcm.setTransform(ti, scale);
}

/*==================== 更新 UI 文本 ====================*/
static void updateUIText(AppState& state) {
    if (state.fpsLabelPtr) state.fpsLabelPtr->setText(state.fpsText);
    if (state.modelLabelPtr) state.modelLabelPtr->setText(state.modelText);
    if (state.animLabelPtr) state.animLabelPtr->setText(state.animText);
    if (state.scaleLabelPtr) state.scaleLabelPtr->setText(state.scaleText);
    if (state.lightLabelPtr) state.lightLabelPtr->setText(state.lightText);
}

/*==================== 切换动画 ====================*/
static void switchAnimation(AppState& state, int delta) {
    if (state.animCount == 0) return;
    state.currentAnimIndex = (state.currentAnimIndex + delta + state.animCount) % state.animCount;
    auto* animator = state.instance->getAnimator();
    if (animator) {
        const char* animName = animator->getAnimationName(state.currentAnimIndex);
        snprintf(state.animText, sizeof(state.animText), "Anim [%zu/%zu]: %s",
                 state.currentAnimIndex + 1, state.animCount,
                 animName ? animName : "(unnamed)");
        updateUIText(state);
    }
}

/*==================== 清理 ====================*/
static void cleanup(AppState& state) {
    if (state.engine) {
        unloadCurrentModel(state);

        state.engine->destroy(state.groundEntity);
        state.engine->destroy(state.groundMi);
        state.engine->destroy(state.groundMat);
        state.engine->destroy(state.groundVb);
        state.engine->destroy(state.groundIb);

        state.engine->destroy(state.view);
        state.engine->destroy(state.scene);
        state.engine->destroy(state.renderer);
        state.engine->destroy(state.swapChain);
        state.engine->destroy(state.skybox);
        state.engine->destroy(state.indirectLight);
        state.engine->destroyCameraComponent(state.cameraEntity);
        EntityManager::get().destroy(state.sunLightEntity);
        EntityManager::get().destroy(state.cameraEntity);

        delete state.resourceLoader;
        delete state.stbDecoder;
        delete state.ktxDecoder;
        if (state.assetLoader) gltfio::AssetLoader::destroy(&state.assetLoader);
        if (state.materialProvider) {
            if (setjmp(g_panicJmpBuf) == 0) {
                utils::Panic::setPanicHandler(filamentPanicHandler, nullptr);
                state.materialProvider->destroyMaterials();
                utils::Panic::setPanicHandler(nullptr, nullptr);
            } else {
                utils::Panic::setPanicHandler(nullptr, nullptr);
            }
            delete state.materialProvider;
        }

        if (setjmp(g_panicJmpBuf) == 0) {
            utils::Panic::setPanicHandler(filamentPanicHandler, nullptr);
            Engine::destroy(&state.engine);
            utils::Panic::setPanicHandler(nullptr, nullptr);
        } else {
            utils::Panic::setPanicHandler(nullptr, nullptr);
            state.engine = nullptr;
        }
    }
    filamat::MaterialBuilder::shutdown();
}

/*==================== 主函数 ====================*/
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    OX::Core::initConsole();
    timeBeginPeriod(1);

    AppState state;
    scanModels();

    // ---------- 1. 窗口 ----------
    OX::WindowDesc desc;
    desc.title = L"TEST-Filament-GLTF";
    desc.width = 1280;
    desc.height = 720;
    desc.enableOpenGL = true;
    desc.stencilBits = 0;
    if (!state.app.create(&desc)) {
        OX_LOG("Failed to create window\n");
        timeEndPeriod(1);
        return 1;
    }

    typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int interval);
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT =
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);

    uint32_t width, height;
    state.app.getSize(&width, &height);

    // ---------- 2. Filament ----------
    NoSwapWGLPlatform* platform = new NoSwapWGLPlatform();
    state.engine = Engine::create(Engine::Backend::OPENGL, platform, nullptr);
    if (!state.engine) {
        OX_LOG("Engine creation failed\n");
        state.app.destroy();
        timeEndPeriod(1);
        return 1;
    }

    state.swapChain = state.engine->createSwapChain((void*)state.app.getHwnd());
    state.renderer = state.engine->createRenderer();
    state.renderer->setClearOptions({.clearColor = {0.0, 0.0, 0.0, 1.0}, .clear = true, .discard = true});
    state.scene = state.engine->createScene();
    state.view = state.engine->createView();
    state.view->setScene(state.scene);
    state.view->setViewport({0, 0, width, height});
    state.view->setPostProcessingEnabled(true);
    state.view->setShadowingEnabled(true);
    state.view->setAmbientOcclusion(View::AmbientOcclusion::SSAO);

    state.cameraEntity = EntityManager::get().create();
    state.camera = state.engine->createCamera(state.cameraEntity);
    state.view->setCamera(state.camera);

    // Sun light (directional with shadows)
    state.sunLightEntity = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
        .intensity(state.lightIntensity)
        .direction({0.5f, -1.0f, -0.8f})
        .castShadows(true)
        .shadowOptions({.mapSize = 2048})
        .build(*state.engine, state.sunLightEntity);
    state.scene->addEntity(state.sunLightEntity);

    // IBL
    if (!loadIBL(state)) {
        OX_LOG("[WARN] IBL load failed, using fallback skybox\n");
        state.skybox = Skybox::Builder()
            .color({0.1f, 0.125f, 0.25f, 1.0f})
            .build(*state.engine);
        state.scene->setSkybox(state.skybox);
    }

    // Ground
    createGroundPlane(state);

    // ---------- 3. ThorVG / OUI ----------
    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        OX_LOG("Failed to initialize ThorVG\n");
        cleanup(state);
        state.app.destroy();
        timeEndPeriod(1);
        return 1;
    }

    bool fontLoaded = OX::UIManager::loadFont("siyuan", "e:/MX/Data/siyuan.ttf");
    const char* fontName = fontLoaded ? "siyuan" : "Arial";

    if (!state.ui.init(state.app.getGLRC(), static_cast<int>(width), static_cast<int>(height))) {
        OX_LOG("Failed to initialize UI\n");
        tvg::Initializer::term();
        cleanup(state);
        state.app.destroy();
        timeEndPeriod(1);
        return 1;
    }

    // UI Panel layout (left side)
    float leftX = 20.0f, leftY = 20.0f;
    float panelW = 260.0f;
    float curY = leftY;

    auto addLabel = [&](const char* text, float h, OX::OColor color, OX::UILabel** outPtr = nullptr) {
        auto lbl = std::make_unique<OX::UILabel>(text);
        lbl->rect = OX::ORect(leftX, curY, panelW, h);
        lbl->fontSize = 14.0f;
        lbl->fontName = fontName;
        lbl->textColor = color;
        if (outPtr) *outPtr = lbl.get();
        state.ui.addElement(std::move(lbl));
        curY += h + 8.0f;
    };

    auto addButton = [&](const char* text, std::function<void()> cb, float h = 35.0f, float w = 0.0f) {
        if (w <= 0.0f) w = panelW;
        auto btn = std::make_unique<OX::UIButton>(text);
        btn->rect = OX::ORect(leftX, curY, w, h);
        btn->fontName = fontName;
        btn->fontSize = 12.0f;
        btn->onClick = cb;
        state.ui.addElement(std::move(btn));
        curY += h + 8.0f;
    };

    auto addHalfButtons = [&](const char* t1, std::function<void()> c1,
                             const char* t2, std::function<void()> c2, float h = 35.0f) {
        float half = panelW / 2.0f - 5.0f;
        auto btn1 = std::make_unique<OX::UIButton>(t1);
        btn1->rect = OX::ORect(leftX, curY, half, h);
        btn1->fontName = fontName;
        btn1->fontSize = 12.0f;
        btn1->onClick = c1;
        state.ui.addElement(std::move(btn1));

        auto btn2 = std::make_unique<OX::UIButton>(t2);
        btn2->rect = OX::ORect(leftX + half + 10.0f, curY, half, h);
        btn2->fontName = fontName;
        btn2->fontSize = 12.0f;
        btn2->onClick = c2;
        state.ui.addElement(std::move(btn2));
        curY += h + 8.0f;
    };

    addLabel(state.modelText, 30.0f, OX::OColor(100, 200, 255, 255), &state.modelLabelPtr);
    addButton("Next Model", [&]() {
        currentModelIndex++;
        loadCurrentModel(state);
        updateUIText(state);
    });
    addButton("Prev Model", [&]() {
        currentModelIndex--;
        loadCurrentModel(state);
        updateUIText(state);
    });
    addLabel(state.animText, 25.0f, OX::OColor(255, 200, 100, 255), &state.animLabelPtr);
    addHalfButtons("Prev Anim", [&]() { switchAnimation(state, -1); },
                   "Next Anim", [&]() { switchAnimation(state, 1); });
    addButton(state.animPlaying ? "Pause Anim" : "Play Anim", [&]() {
        state.animPlaying = !state.animPlaying;
        updateUIText(state);
    });
    addLabel(state.scaleText, 25.0f, OX::OColor(100, 255, 150, 255), &state.scaleLabelPtr);
    addHalfButtons("Scale -0.5", [&]() {
        state.modelScale = std::max(0.1f, state.modelScale - 0.5f);
        snprintf(state.scaleText, sizeof(state.scaleText), "Scale: %.1fx", state.modelScale);
        updateUIText(state);
    }, "Scale +0.5", [&]() {
        state.modelScale += 0.5f;
        snprintf(state.scaleText, sizeof(state.scaleText), "Scale: %.1fx", state.modelScale);
        updateUIText(state);
    });
    addLabel(state.lightText, 25.0f, OX::OColor(255, 255, 150, 255), &state.lightLabelPtr);
    addHalfButtons("Light -10k", [&]() {
        state.lightIntensity = std::max(0.0f, state.lightIntensity - 10000.0f);
        auto& lm = state.engine->getLightManager();
        auto inst = lm.getInstance(state.sunLightEntity);
        if (inst.isValid()) lm.setIntensity(inst, state.lightIntensity);
        snprintf(state.lightText, sizeof(state.lightText), "Light: %.0fk", state.lightIntensity / 1000.0f);
        updateUIText(state);
    }, "Light +10k", [&]() {
        state.lightIntensity += 10000.0f;
        auto& lm = state.engine->getLightManager();
        auto inst = lm.getInstance(state.sunLightEntity);
        if (inst.isValid()) lm.setIntensity(inst, state.lightIntensity);
        snprintf(state.lightText, sizeof(state.lightText), "Light: %.0fk", state.lightIntensity / 1000.0f);
        updateUIText(state);
    });
    addButton(state.autoRotate ? "Stop AutoRotate" : "Start AutoRotate", [&]() {
        state.autoRotate = !state.autoRotate;
    });
    addButton("Reset Camera", [&]() {
        state.cameraAzimuth = 0.0f;
        state.cameraElevation = 0.3f;
        if (state.asset) {
            const auto& aabb = state.asset->getBoundingBox();
            float3 ext = aabb.extent();
            float maxExt = std::max({ext.x, ext.y, ext.z});
            state.cameraDistance = maxExt > 0.0f ? maxExt * 2.5f : 3.0f;
            state.cameraTarget = aabb.center();
        } else {
            state.cameraDistance = 3.0f;
            state.cameraTarget = {0.0f, 0.0f, 0.0f};
        }
    });
    addLabel(state.fpsText, 25.0f, OX::OColor(0, 255, 0, 255), &state.fpsLabelPtr);

    // Load first model
    loadCurrentModel(state);
    updateUIText(state);

    // ---------- 4. Input callbacks ----------
    state.app.setMouseWheelCallback([&state](int32_t x, int32_t y, int delta) {
        (void)x; (void)y;
        state.cameraDistance -= delta * 0.005f * state.cameraDistance;
        state.cameraDistance = std::clamp(state.cameraDistance, 0.1f, 500.0f);
    });

    state.app.setMouseMoveCallback([&state](int32_t x, int32_t y) {
        state.ui.handleMMove(x, y);
        if (state.mouseDragging) {
            float dx = static_cast<float>(x - state.lastMouseX);
            float dy = static_cast<float>(y - state.lastMouseY);
            state.cameraAzimuth -= dx * 0.008f;
            state.cameraElevation += dy * 0.008f;
            state.cameraElevation = std::clamp(state.cameraElevation, -1.5f, 1.5f);
            state.lastMouseX = x;
            state.lastMouseY = y;
        }
        if (state.mousePanning) {
            float dx = static_cast<float>(x - state.panLastX);
            float dy = static_cast<float>(y - state.panLastY);
            float scale = 0.002f * state.cameraDistance;
            float cosEl = cosf(state.cameraElevation);
            float sinEl = sinf(state.cameraElevation);
            float sinAz = sinf(state.cameraAzimuth);
            float cosAz = cosf(state.cameraAzimuth);
            float3 forward = {cosEl * sinAz, sinEl, cosEl * cosAz};
            float3 right = normalize(cross(float3{0,1,0}, forward));
            float3 up = {0,1,0};
            state.cameraTarget -= right * (dx * scale);
            state.cameraTarget += up * (dy * scale);
            state.panLastX = x;
            state.panLastY = y;
        }
    });

    state.app.setMouseButtonCallback([&state](int32_t x, int32_t y, int32_t button, bool pressed) {
        if (button == 0) {
            if (pressed) state.ui.handleMDown(x, y);
            else state.ui.handleMUp(x, y);
        }
        if (button == 1) {
            state.mouseDragging = pressed;
            state.lastMouseX = x;
            state.lastMouseY = y;
        }
        if (button == 2) {
            state.mousePanning = pressed;
            state.panLastX = x;
            state.panLastY = y;
        }
    });

    state.lastFrameTime = std::chrono::steady_clock::now();
    state.app.setTargetFPS(0);  // VSync syncs to 165Hz

    int frameCount = 0;
    float fpsTimer = 0.0f;

    // ---------- 5. Main loop ----------
    while (!state.app.shouldClose()) {
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - state.lastFrameTime).count();
        state.lastFrameTime = now;

        state.app.pollEvents();

        // FPS
        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 0.5f) {
            state.fps = frameCount / fpsTimer;
            snprintf(state.fpsText, sizeof(state.fpsText), "FPS: %.1f", state.fps);
            if (state.fpsLabelPtr) state.fpsLabelPtr->setText(state.fpsText);
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // Update logic
        updateAnimation(state, deltaTime);
        updateModelTransform(state, deltaTime);
        updateCamera(state, width, height);

        // Filament render
        if (state.renderer->beginFrame(state.swapChain)) {
            state.renderer->render(state.view);
            state.renderer->endFrame();
        }
        state.engine->flushAndWait();

        // ThorVG UI render
        wglMakeCurrent(state.app.getDC(), state.app.getGLRC());
        state.ui.update(deltaTime);
        state.ui.render();

        state.app.swapBuffers();
        state.app.waitForNextFrame();
    }

    // ---------- 6. Cleanup ----------
    state.ui.destroy();
    tvg::Initializer::term();
    cleanup(state);
    state.app.destroy();
    timeEndPeriod(1);
    return 0;
}
