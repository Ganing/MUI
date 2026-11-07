/****************************************************************************
 * stillwater.cpp - 静水（StillWater）场景
 *
 * 一比一移植自 E:\Dev\H5\StillWater-v2.html：
 *   photo + koi(FBO) + rings(24x1 RGBA32F) -> water.frag -> leaves(叠加)
 * 锦鲤不再用 Canvas2D 画 64 次 drawImage，改成一条三角带（65 段 x 2 顶点），
 * 形变全部在顶点着色器里按原公式计算。
 ****************************************************************************/

#include "../../core/mate_core.h"
#include "../../core/mate_audio.h"
#include "../../core/mate_video.h"   // 视频层：解码时直接缩放的帧源

#include "json.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace mate {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kVW = 1672.0f;   // 原照片虚拟坐标系
constexpr float kVH = 941.0f;
constexpr int   kRingSlots = 24;

/*==================== 着色器 ====================*/
const char* kQuadVertex = R"GLSL(
#version 450
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * .5 + .5;
    gl_Position = vec4(aPos, 0., 1.);
}
)GLSL";

// 水面着色器前言：只负责版本与接口，uniform 由场景文件自己声明
const char* kWaterPrelude = R"GLSL(
#version 450
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;
)GLSL";

const char* kKoiVertex = R"GLSL(
#version 450
layout(location = 0) in vec2 aSV;      // x: 0=左侧 1=右侧, y: 沿鱼身 v
uniform vec2  uResolution;
uniform vec2  uOffset;                // object-fit:cover 的黑边
uniform float uScale;
uniform vec2  uFishPos;               // 虚拟坐标系里的鱼心
uniform float uAngle;
uniform float uLength;
uniform float uTexAspect;             // naturalWidth / naturalHeight
uniform float uBeat;
uniform float uAmp;
uniform float uBend;
out vec2 vTex;
float spine(float v) {
    return sin(uBeat - v * 6.2) * uLength * uAmp * pow(v, 1.8)
         + uBend * uLength * .16 * v * v;
}
void main() {
    float v = aSV.y;
    float tangent = (spine(min(1., v + .008)) - spine(max(0., v - .008))) / (.016 * uLength);
    float chest = exp(-pow((v - .31) / .105, 2.));
    float tail  = exp(-pow((v - .88) / .13, 2.));
    float fan = 1. + chest * sin(uBeat + 1.) * .085 + tail * sin(uBeat - v * 6.2) * .045;

    // Canvas2D 的变换顺序：
    //   translate(鱼心) -> rotate(angle + PI/2) -> translate(spine(v), -L/2 + v*L)
    //   -> rotate(-atan(tangent)) -> 画横向切片
    // 所以锚点本身要先绕鱼心转 angle + PI/2，切片宽度再叠加 -atan(tangent)
    float ao = uAngle + 1.5707963;
    vec2 anchorBody = vec2(spine(v), -uLength * .5 + v * uLength);
    vec2 anchor = vec2(cos(ao) * anchorBody.x - sin(ao) * anchorBody.y,
                       sin(ao) * anchorBody.x + cos(ao) * anchorBody.y);
    float th = ao - atan(tangent);
    float halfW = uLength * uTexAspect * .5 * fan;
    vec2 local = vec2((aSV.x - .5) * 2. * halfW, 0.);
    vec2 pos = anchor + vec2(cos(th) * local.x - sin(th) * local.y,
                             sin(th) * local.x + cos(th) * local.y);

    vec2 screenPx = uOffset + (uFishPos + pos) * uScale;   // y 向下
    vec2 uv = screenPx / uResolution;
    gl_Position = vec4(uv.x * 2. - 1., 1. - uv.y * 2., 0., 1.);
    vTex = vec2(aSV.x, v);                                 // 图片顶部 = 鱼头
}
)GLSL";

const char* kKoiFragment = R"GLSL(
#version 450
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;
uniform sampler2D uKoiTex;
void main() {
    vec4 c = texture(uKoiTex, vTex);
    fragColor = vec4(c.rgb, c.a * .94);       // 对应 Canvas2D 的 globalAlpha = .94
}
)GLSL";

const char* kLeafVertex = R"GLSL(
#version 450
layout(location = 0) in vec2 aPos;        // 全屏三角，这里当 [-1,1] 单位方片用
uniform vec2  uResolution;
uniform vec2  uCenter;                    // 屏幕像素
uniform float uSize;
uniform float uRot;
out vec2 vUv;
void main() {
    vec2 p = aPos * .5 * uSize;
    float c = cos(uRot), s = sin(uRot);
    vec2 q = vec2(c * p.x - s * p.y, s * p.x + c * p.y) + uCenter;
    vec2 uv = q / uResolution;
    gl_Position = vec4(uv.x * 2. - 1., 1. - uv.y * 2., 0., 1.);
    vUv = vec2(aPos.x * .5 + .5, .5 - aPos.y * .5);   // v=0 为图片顶部
}
)GLSL";

const char* kLeafFragment = R"GLSL(
#version 450
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;
uniform sampler2D uLeafTex;
uniform float uAlpha;
void main() {
    vec4 c = texture(uLeafTex, vUv);
    fragColor = vec4(c.rgb, c.a * uAlpha);
}
)GLSL";

// 视频层：一片贴在指定矩形里的方片。帧源给的是 GL 行序（第 0 行=画面底部），
// 所以 aPos.y=+1（屏幕上方）对应 v=1（画面顶部），画面是正的。
const char* kVideoVertex = R"GLSL(
#version 450
layout(location = 0) in vec2 aPos;
uniform vec4 uRect;          // 像素：x,y,w,h，左下原点
uniform vec2 uResolution;
out vec2 vUv;
void main() {
    vUv = aPos * .5 + .5;
    vec2 px = uRect.xy + vUv * uRect.zw;
    gl_Position = vec4(px / uResolution * 2. - 1., 0., 1.);
}
)GLSL";

const char* kVideoFragment = R"GLSL(
#version 450
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;
uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;
uniform float uAlpha;
void main() {
    // YUV420P（BT.709 限制范围）→ RGB：R=1.164(Y-16)+1.793(Cr-128)
    //                                   G=1.164(Y-16)-0.213(Cb-128)-0.533(Cr-128)
    //                                   B=1.164(Y-16)+2.112(Cb-128)
    float y = texture(uTexY, vUv).r * 255.0;
    float u = texture(uTexU, vUv).r * 255.0 - 128.0;
    float v = texture(uTexV, vUv).r * 255.0 - 128.0;
    y = (y - 16.0) * 1.164;
    // 上面算出来是 0..255 的整数刻度，必须除以 255 再 clamp，否则整片被截成白色
    vec3 rgb = vec3(y + 1.793 * v,
                    y - 0.213 * u - 0.533 * v,
                    y + 2.112 * u) / 255.0;
    fragColor = vec4(clamp(rgb, 0.0, 1.0), uAlpha);
}
)GLSL";

/*==================== 数据 ====================*/
struct Fish {
    float x = 0, y = 0;
    float angle = 0, target = 0;
    float length = 0, speed = 0, phase = 0;
    float flee = 0, wake = 0, bend = 0;
};

struct Ring {
    float x = 0, y = 0, t = 0, p = 0;
};

struct Leaf {
    float x = 0, y = 0;
    float start = 0, angle = 0, size = 0, phase = 0;
    bool  landed = false;
};

// 荷叶椭圆区域（锦鲤不会游到荷叶下面）
const float kPads[][4] = {
    { .805f, .120f, .091f, .140f }, { .675f, .210f, .054f, .095f },
    { .723f, .302f, .056f, .089f }, { .878f, .295f, .054f, .090f },
    { .620f, .420f, .085f, .144f }, { .792f, .393f, .037f, .061f },
    { .748f, .463f, .032f, .053f }, { .743f, .733f, .114f, .187f },
    { .945f, .511f, .072f, .131f }, { .560f, .703f, .049f, .088f },
    { .632f, .842f, .055f, .075f }, { .191f, .591f, .039f, .067f },
    { .213f, .780f, .036f, .067f }, { .177f, .836f, .034f, .060f },
    { .266f, .930f, .044f, .074f },
};

float wrapAngle(float a, float b) {
    return std::atan2(std::sin(b - a), std::cos(b - a));
}

/*==================== scene.json 取值（兼容带 ui 的类型化参数） ====================*/
float jsonFloat(const nlohmann::json& p, const char* key, float def) {
    if (!p.contains(key)) return def;
    const auto& v = p[key];
    if (v.is_object()) return v.value("default", def);
    if (v.is_number()) return v.get<float>();
    return def;
}

bool jsonBool(const nlohmann::json& p, const char* key, bool def) {
    if (!p.contains(key)) return def;
    const auto& v = p[key];
    if (v.is_object()) return v.value("default", def);
    if (v.is_boolean()) return v.get<bool>();
    return def;
}

int jsonInt(const nlohmann::json& p, const char* key, int def) {
    if (!p.contains(key)) return def;
    const auto& v = p[key];
    if (v.is_object()) return v.value("default", def);
    if (v.is_number()) return v.get<int>();
    return def;
}

class StillWaterScene : public IScene {
public:
    bool load(const std::string& sceneDir) override;
    void resize(int w, int h) override;
    void update(const FrameContext& fc) override;
    void render(const FrameContext& fc) override;
    void drawUI() override;
    void pointerDown(float nx, float ny) override;
    void pointerMove(float nx, float ny) override;

private:
    bool loadParams(const std::string& sceneDir);
    bool loadShaders(const std::string& sceneDir);
    void updateFish(float t, float dt);
    void updateLeaves(float t, float dt);
    void drawFish(float t);
    void drawLeaves(float t);
    void drawVideo();
    void uploadVideoFrame();
    void videoRectPx(float& x, float& y, float& w, float& h) const;
    void addRipple(float nx, float ny, float power);
    bool fishTouch(float nx, float ny, float t);
    bool fishVisible(float nx, float ny) const;
    void coverMap(int w, int h, float& scale, float& ox, float& oy) const;
    void ensureKoiGeometry();

    std::string dir_;

    // 参数（scene.json parameters）
    float ripplePower_ = 1.25f;
    float dragPower_ = 0.45f;
    float timeScale_ = 1.0f;
    bool  koiEnabled_ = true;
    bool  leavesEnabled_ = true;
    bool  audioEnabled_ = true;
    float audioVolume_ = 0.6f;
    bool  audioStarted_ = false;
    bool  audioFailed_ = false;
    bool  videoEnabled_ = true;
    int   videoWidth_ = 1280;          // 解码输出宽度（解码时直接缩放）
    float videoAlpha_ = 1.0f;
    float videoRect_[4] = { .06f, .10f, .26f, .30f };   // nx, ny, nw, nh（左上原点）
    float debugKoi_ = 0.0f;
    unsigned int seed_ = 90210;

    // 资源（scene.json assets，缺省走约定路径）
    std::string photoPath_ = "data/pond.png";
    std::string koiPath_ = "data/koi.png";
    std::string leafPath_ = "data/leaf.png";
    std::string audioPath_ = "data/pond-audio.wav";
    std::string videoPath_ = "data/video.mp4";
    std::string shaderPath_ = "layer/water.frag";

    AudioLoop audio_;
    VideoSource video_;
    Texture photo_;
    Texture koiSource_;
    Texture leafTex_;
    Texture ringsTex_;
    Texture videoTex_[3];        // Y / U / V 三个 R8 平面
    Framebuffer koiFbo_;
    FullscreenQuad quad_;
    Program waterProg_;
    Program koiProg_;
    Program leafProg_;
    Program videoProg_;

    GLuint koiVao_ = 0;
    GLuint koiVbo_ = 0;
    int    koiVerts_ = 0;

    // 视频上传用双 PBO 轮转 + 持久映射：映射一次，之后每帧直接写内存，
    // 再用 fence 保证"这一轮写完前，上一轮 GPU 已经读完这块缓冲"
    struct VideoPboSlot {
        GLuint buffer = 0;
        void*  mapped = nullptr;
        GLsync fence  = nullptr;
    };
    VideoPboSlot videoPbo_[2];
    int    videoPboIndex_ = 0;
    int    videoPboBytes_ = 0;

    Fish fish_[3];
    std::vector<Leaf> leaves_;
    std::vector<Ring> rings_;
    float ringData_[kRingSlots * 4] = {};

    int   width_ = 0, height_ = 0;
    float time_ = 0.0f;
    float nextLeaf_ = 3.0f;
    float lastDrag_ = -1.0f;
    bool  ready_ = false;

    // 滚动种子：场景自带，保证每次启动分布一致
    std::mt19937 rng_{ 90210u };
    float random01();
};

float StillWaterScene::random01() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng_);
}

bool StillWaterScene::loadParams(const std::string& sceneDir) {
    const std::string path = joinPath(sceneDir, "scene.json");
    std::string text;
    if (!readTextFile(path, text)) {
        logf("[静水] 缺少 scene.json，使用内置默认参数: %s", path.c_str());
        return true;
    }

    try {
        auto j = nlohmann::json::parse(text);

        if (j.contains("parameters")) {
            const auto& p = j["parameters"];
            ripplePower_  = jsonFloat(p, "ripplePower", ripplePower_);
            dragPower_    = jsonFloat(p, "dragPower", dragPower_);
            timeScale_    = jsonFloat(p, "timeScale", timeScale_);
            koiEnabled_   = jsonBool(p, "koi", koiEnabled_);
            leavesEnabled_ = jsonBool(p, "leaves", leavesEnabled_);
            audioEnabled_ = jsonBool(p, "audio", audioEnabled_);
            audioVolume_  = jsonFloat(p, "audioVolume", audioVolume_);
            videoEnabled_ = jsonBool(p, "video", videoEnabled_);
            videoWidth_   = jsonInt(p, "videoWidth", videoWidth_);
            videoAlpha_   = jsonFloat(p, "videoAlpha", videoAlpha_);
            debugKoi_     = jsonBool(p, "debugKoi", false) ? 1.0f : 0.0f;
            seed_         = (unsigned int)jsonInt(p, "seed", (int)seed_);
        }

        if (j.contains("assets")) {
            const auto& a = j["assets"];
            photoPath_ = a.value("photo", photoPath_);
            koiPath_   = a.value("koi", koiPath_);
            leafPath_  = a.value("leaf", leafPath_);
            audioPath_ = a.value("audio", audioPath_);
            videoPath_ = a.value("video", videoPath_);
        }

        // 层清单：shader 层给出水面源码；native 层按 name 关闭对应内容
        if (j.contains("layers") && j["layers"].is_array()) {
            for (const auto& layer : j["layers"]) {
                const std::string type = layer.value("type", "");
                const std::string name = layer.value("name", "");
                const bool enabled = layer.value("enabled", true);
                if (type == "shader" && layer.contains("source")) {
                    shaderPath_ = layer["source"].get<std::string>();
                } else if (type == "native") {
                    if (name == "koi") koiEnabled_ = koiEnabled_ && enabled;
                    if (name == "leaves") leavesEnabled_ = leavesEnabled_ && enabled;
                    if (name == "video") {
                        videoEnabled_ = videoEnabled_ && enabled;
                        // rect：[x, y, w, h]，屏幕归一化、左上原点
                        if (layer.contains("rect") && layer["rect"].is_array()
                                && layer["rect"].size() >= 4) {
                            for (int i = 0; i < 4; ++i) videoRect_[i] = layer["rect"][i].get<float>();
                        }
                    }
                }
            }
        }

        rng_.seed(seed_);
        logf("[静水] scene.json 已加载（种子 %u，%zu 层）", seed_,
             j.contains("layers") ? j["layers"].size() : (size_t)0);
    } catch (const std::exception& e) {
        logf("[静水] scene.json 解析失败: %s", e.what());
        return false;
    }
    return true;
}

bool StillWaterScene::loadShaders(const std::string& sceneDir) {
    std::string frag;
    const std::string fragPath = joinPath(sceneDir, shaderPath_);
    if (!readTextFile(fragPath, frag)) {
        logf("[静水] 水面着色器缺失: %s", fragPath.c_str());
        return false;
    }
    const std::string source = std::string(kWaterPrelude) + frag;
    if (!waterProg_.build(kQuadVertex, source.c_str(), "stillwater/water")) {
        return false;
    }
    if (!koiProg_.build(kKoiVertex, kKoiFragment, "stillwater/koi")) {
        return false;
    }
    if (!leafProg_.build(kLeafVertex, kLeafFragment, "stillwater/leaf")) {
        return false;
    }
    if (!videoProg_.build(kVideoVertex, kVideoFragment, "stillwater/video")) {
        return false;
    }
    return true;
}

bool StillWaterScene::load(const std::string& sceneDir) {
    dir_ = sceneDir;
    logf("[静水] 加载场景: %s", sceneDir.c_str());

    if (!loadParams(sceneDir)) return false;

    if (!photo_.loadPng(joinPath(sceneDir, photoPath_))) return false;
    const bool koiOk  = koiSource_.loadPng(joinPath(sceneDir, koiPath_));
    const bool leafOk = leafTex_.loadPng(joinPath(sceneDir, leafPath_));
    if (!koiOk) {
        logf("[静水] 锦鲤贴图缺失，锦鲤层关闭");
        koiEnabled_ = false;
    }
    if (!leafOk) {
        logf("[静水] 落叶贴图缺失，落叶层关闭");
        leavesEnabled_ = false;
    }

    if (!loadShaders(sceneDir)) return false;
    if (!quad_.create()) {
        logf("[静水] 全屏三角创建失败");
        return false;
    }

    // 视频层：解码时直接缩放（默认 1280 宽）并输出 YUV420P 三平面，
    // 720p 一帧 1.38MB（RGBA 是 3.69MB），颜色换算交给 GPU
    if (videoEnabled_) {
        const std::string videoPath = joinPath(sceneDir, videoPath_);
        if (!fileExists(videoPath)) {
            logf("[静水] 视频资源缺失，视频层关闭: %s", videoPath.c_str());
            videoEnabled_ = false;
        } else if (!video_.open(videoPath, VideoSource::Mode::Threaded, videoWidth_)) {
            logf("[静水] 视频打开失败，视频层关闭: %s", videoPath.c_str());
            videoEnabled_ = false;
        } else {
            bool ok = true;
            int totalBytes = 0;
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // R8 行宽不一定 4 字节对齐
            for (int p = 0; p < 3; ++p) {
                const int pw = video_.planeWidth(p), ph = video_.planeHeight(p);
                videoTex_[p].create(GL_R8, pw, ph, GL_RED, GL_UNSIGNED_BYTE, video_.plane(p));
                totalBytes += pw * ph;
                // 视频每帧变化，用线性过滤避免缩小时出现块状边缘
                glBindTexture(GL_TEXTURE_2D, videoTex_[p].id());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                if (!videoTex_[p].id()) ok = false;
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glBindTexture(GL_TEXTURE_2D, 0);

            if (!ok) {
                logf("[静水] 视频纹理创建失败，视频层关闭");
                videoEnabled_ = false;
            } else {
                videoPboBytes_ = totalBytes;
                bool persistent = true;
                for (int i = 0; i < 2; ++i) {
                    glGenBuffers(1, &videoPbo_[i].buffer);
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, videoPbo_[i].buffer);
                    // 持久映射需要不可变存储：GL_MAP_PERSISTENT_BIT 只能在 glBufferStorage 上申请
                    glBufferStorage(GL_PIXEL_UNPACK_BUFFER, totalBytes, nullptr,
                                    GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
                    videoPbo_[i].mapped = glMapBufferRange(
                            GL_PIXEL_UNPACK_BUFFER, 0, totalBytes,
                            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
                    if (!videoPbo_[i].mapped) persistent = false;
                }
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                logf("[静水] 视频层: %s 输出 %dx%d YUV420P（%d KB/帧，%s）",
                     videoPath.c_str(), video_.width(), video_.height(), totalBytes / 1024,
                     persistent ? "双 PBO 持久映射" : "双 PBO 回退 glBufferSubData");
            }
        }
    }

    // 24x1 RGBA32F 涟漪槽
    ringsTex_.create(GL_RGBA32F, kRingSlots, 1, GL_RGBA, GL_FLOAT, nullptr);

    // 锦鲤初始状态（与 HTML 一致）
    for (int i = 0; i < 3; ++i) {
        Fish& f = fish_[i];
        f.x = 510.0f + i * 130.0f;
        f.y = 320.0f + i * 155.0f;
        f.angle = f.target = i * 1.9f;
        f.length = 96.0f + i * 12.0f;
        f.speed = 0.0f;
        f.phase = i * 1.7f;
        f.flee = f.wake = f.bend = 0.0f;
    }

    ensureKoiGeometry();
    ready_ = true;
    return true;
}

void StillWaterScene::ensureKoiGeometry() {
    if (koiVao_) return;
    // 65 段 x 2 顶点，三角带；属性 (side, v)
    std::vector<float> verts;
    verts.reserve(65 * 2 * 2);
    for (int i = 0; i <= 64; ++i) {
        const float v = (float)i / 64.0f;
        verts.push_back(0.0f); verts.push_back(v);
        verts.push_back(1.0f); verts.push_back(v);
    }
    koiVerts_ = (int)(verts.size() / 2);

    glGenVertexArrays(1, &koiVao_);
    glGenBuffers(1, &koiVbo_);
    glBindVertexArray(koiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, koiVbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StillWaterScene::resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    width_ = w;
    height_ = h;
    koiFbo_.resize(w, h);
}

void StillWaterScene::coverMap(int w, int h, float& scale, float& ox, float& oy) const {
    scale = (std::max)((float)w / kVW, (float)h / kVH);
    ox = ((float)w - kVW * scale) * 0.5f;
    oy = ((float)h - kVH * scale) * 0.5f;
}

bool StillWaterScene::fishVisible(float nx, float ny) const {
    if (nx <= .15f) return false;
    for (const auto& p : kPads) {
        const float dx = (nx - p[0]) / p[2];
        const float dy = (ny - p[1]) / p[3];
        if (std::sqrt(dx * dx + dy * dy) < .95f) return false;
    }
    return true;
}

void StillWaterScene::addRipple(float nx, float ny, float power) {
    Ring r;
    r.x = nx;
    r.y = ny;
    r.t = time_;
    r.p = power;
    rings_.push_back(r);
    if ((int)rings_.size() > kRingSlots) rings_.erase(rings_.begin());
}

bool StillWaterScene::fishTouch(float nx, float ny, float t) {
    if (!koiSource_.id()) return false;
    float scale = 1.0f, ox = 0.0f, oy = 0.0f;
    coverMap(width_, height_, scale, ox, oy);

    Fish* target = nullptr;
    float best = 1.0e9f;
    for (Fish& f : fish_) {
        if (!fishVisible(f.x / kVW, f.y / kVH)) continue;
        const float dx = (nx * (float)width_ - ox) / scale - f.x;
        const float dy = (ny * (float)height_ - oy) / scale - f.y;
        const float forward = dx * std::cos(f.angle) + dy * std::sin(f.angle);
        const float side = -dx * std::sin(f.angle) + dy * std::cos(f.angle);
        const float d = std::sqrt(std::pow(forward / (f.length * .60f + 12.0f / scale), 2.0f)
                                + std::pow(side / (f.length * .25f + 12.0f / scale), 2.0f));
        if (d < 1.0f && d < best) { target = &f; best = d; }
    }
    if (!target) return false;

    float away = std::atan2(target->y - (ny * (float)height_ - oy) / scale,
                            target->x - (nx * (float)width_ - ox) / scale);
    const float ddx = target->x - (nx * (float)width_ - ox) / scale;
    const float ddy = target->y - (ny * (float)height_ - oy) / scale;
    if (std::sqrt(ddx * ddx + ddy * ddy) < 4.0f) away = target->angle;
    target->target = away;
    target->flee = t + 2.4f;
    target->speed = 200.0f;
    target->wake = t;
    return true;
}

void StillWaterScene::pointerDown(float nx, float ny) {
    if (!ready_) return;
    addRipple(nx, ny, ripplePower_);
    fishTouch(nx, ny, time_);
}

void StillWaterScene::pointerMove(float nx, float ny) {
    if (!ready_) return;
    // 引擎按 100ms 节流调用，这里再按场景时间兜一层
    if (lastDrag_ >= 0.0f && time_ - lastDrag_ < 0.1f) return;
    lastDrag_ = time_;
    addRipple(nx, ny, dragPower_);
    fishTouch(nx, ny, time_);
}

void StillWaterScene::updateFish(float t, float dt) {
    for (Fish& f : fish_) {
        const bool fast = t < f.flee;
        if (!fast) f.target += std::sin(t * .6f + f.phase) * dt * .17f;  // 闲游时缓慢改变方向
        if (f.x < 290.0f)  f.target = 0.0f;
        if (f.x > 1120.0f) f.target = kPi;
        if (f.y < 170.0f)  f.target = kPi * 0.5f;
        if (f.y > 850.0f)  f.target = -kPi * 0.5f;
        const float steer = fast ? 5.0f : 1.2f;
        const float curve = wrapAngle(f.angle, f.target);
        f.bend += (std::max(-1.0f, (std::min)(1.0f, curve)) - f.bend) * (std::min)(1.0f, dt * 4.0f);
        f.angle += curve * (std::min)(1.0f, dt * steer);

        const float beat = t * (fast ? 13.0f : 4.6f) + f.phase;
        const float desired = fast ? 160.0f
                                   : (19.0f + std::sin(t * .4f + f.phase) * 4.0f)
                                         * (.85f + .15f * std::cos(beat * 2.0f));
        f.speed += (desired - f.speed) * (std::min)(1.0f, dt * (fast ? 4.0f : 3.0f));
        f.x += std::cos(f.angle) * f.speed * dt;
        f.y += std::sin(f.angle) * f.speed * dt;
        f.x = (std::max)(270.0f, (std::min)(1140.0f, f.x));
        f.y = (std::max)(150.0f, (std::min)(870.0f, f.y));

        if (fast && t - f.wake > .22f) {
            f.wake = t;
            // 锦鲤游动带出的涟漪：屏幕像素 -> 归一化
            float scale = 1.0f, ox = 0.0f, oy = 0.0f;
            coverMap(width_, height_, scale, ox, oy);
            const float px = ox + f.x * scale;
            const float py = oy + f.y * scale;
            addRipple(px / (float)width_, py / (float)height_, .13f);
        }
    }
}

void StillWaterScene::updateLeaves(float t, float dt) {
    if (!leavesEnabled_ || !leafTex_.id()) return;

    // 每 5~10s 一片
    if (t > nextLeaf_) {
        Leaf l;
        l.x = (float)width_ * (.18f + random01() * .63f);
        l.y = (float)height_ * (.14f + random01() * .65f);
        l.start = t;
        l.angle = random01() * 6.28f;
        l.size = 20.0f + random01() * 14.0f;
        l.phase = random01() * 6.28f;
        leaves_.push_back(l);
        if (leaves_.size() > 8) leaves_.erase(leaves_.begin());
        nextLeaf_ = t + 5.0f + random01() * 5.0f;
    }

    // 落水后起涟漪，再缓慢漂移
    for (Leaf& l : leaves_) {
        const float age = t - l.start;
        const float fall = (std::min)(age / 2.8f, 1.0f);
        if (fall >= 1.0f && !l.landed) {
            l.landed = true;
            addRipple(l.x / (float)width_, l.y / (float)height_, .4f);
        }
        if (l.landed) {
            l.x += std::sin(t * .5f + l.phase) * dt * 2.0f;
            l.y += dt * 1.5f;
        }
    }
    leaves_.erase(std::remove_if(leaves_.begin(), leaves_.end(),
                     [t](const Leaf& l) { return t - l.start >= 48.0f; }),
                  leaves_.end());
}

void StillWaterScene::update(const FrameContext& fc) {
    if (!ready_) return;
    if (fc.width != width_ || fc.height != height_) resize(fc.width, fc.height);

    const float dt = (std::min)(0.05f, fc.dt) * timeScale_;
    time_ += dt;

    if (koiEnabled_) updateFish(time_, dt);
    updateLeaves(time_, dt);

    // 视频层：帧源到点就摘一帧（解码在独立线程），这里只把三个平面搬进 PBO 再贴到纹理
    if (videoEnabled_ && video_.isOpen() && video_.advance(dt)) {
        uploadVideoFrame();
    }

    // 5 秒前的涟漪槽不再参与
    rings_.erase(std::remove_if(rings_.begin(), rings_.end(),
                     [this](const Ring& r) { return time_ - r.t >= 5.0f; }),
                 rings_.end());

    // 环境音：加载后无缝循环；引擎暂停时静音
    if (!audioEnabled_) {
        if (audioStarted_) {
            audio_.stop();
            audioStarted_ = false;
        }
    } else if (!audioStarted_ && !audioFailed_) {
        if (audio_.start(joinPath(dir_, audioPath_), audioVolume_)) {
            audioStarted_ = true;
        } else {
            audioFailed_ = true;
        }
    }
    if (audioStarted_) {
        audio_.setVolume(audioVolume_);
        audio_.setPaused(fc.dt <= 0.0f);
    }
}

void StillWaterScene::drawFish(float t) {
    if (!koiEnabled_ || !koiSource_.id() || !koiProg_.valid() || !koiFbo_.valid()) return;

    koiFbo_.bind();
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    koiProg_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, koiSource_.id());
    koiProg_.set1i("uKoiTex", 0);

    float scale = 1.0f, ox = 0.0f, oy = 0.0f;
    coverMap(width_, height_, scale, ox, oy);
    koiProg_.set2f("uResolution", (float)width_, (float)height_);
    koiProg_.set2f("uOffset", ox, oy);
    koiProg_.set1f("uScale", scale);
    koiProg_.set1f("uTexAspect", (float)koiSource_.width() / (float)koiSource_.height());

    glBindVertexArray(koiVao_);
    for (const Fish& f : fish_) {
        const bool fast = t < f.flee;
        koiProg_.set2f("uFishPos", f.x, f.y);
        koiProg_.set1f("uAngle", f.angle);
        koiProg_.set1f("uLength", f.length);
        koiProg_.set1f("uBeat", t * (fast ? 13.0f : 4.6f) + f.phase);
        koiProg_.set1f("uAmp", fast ? .17f : .095f);
        koiProg_.set1f("uBend", f.bend);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, koiVerts_);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void StillWaterScene::drawLeaves(float t) {
    if (!leavesEnabled_ || !leafTex_.id() || !leafProg_.valid()) return;

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    leafProg_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, leafTex_.id());
    leafProg_.set1i("uLeafTex", 0);
    leafProg_.set2f("uResolution", (float)width_, (float)height_);

    for (const Leaf& l : leaves_) {
        const float age = t - l.start;
        const float fall = (std::min)(age / 2.8f, 1.0f);
        const float ease = 1.0f - std::pow(1.0f - fall, 2.0f);
        const float x = l.x + std::sin(age * 2.0f + l.phase) * (1.0f - fall) * 40.0f;
        const float y = l.y - (1.0f - ease) * (float)height_ * .55f;
        const float fade = (std::min)(age * 2.0f, 1.0f)
                         * (std::min)((48.0f - age) / 5.0f, 1.0f);
        const float size = l.size * (1.8f - .8f * ease);
        const float rot = l.angle
                        + (l.landed ? std::sin(t * .3f + l.phase) * .12f : age * .55f);
        if (fade <= 0.0f || size <= 0.0f) continue;

        leafProg_.set2f("uCenter", x, y);
        leafProg_.set1f("uSize", size);
        leafProg_.set1f("uRot", rot);
        leafProg_.set1f("uAlpha", fade);
        quad_.draw();
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void StillWaterScene::videoRectPx(float& x, float& y, float& w, float& h) const {
    const float bw = videoRect_[2] * (float)width_;
    const float bh = videoRect_[3] * (float)height_;
    const float vAspect = (float)video_.width() / (float)video_.height();
    const float bAspect = bh > 0.0f ? bw / bh : vAspect;

    // contain：保持视频比例，多余的黑边留在矩形里
    w = bw;
    h = bh;
    if (vAspect > bAspect) h = bw / vAspect;
    else                   w = bh * vAspect;

    x = videoRect_[0] * (float)width_ + (bw - w) * 0.5f;
    const float yTop = videoRect_[1] * (float)height_ + (bh - h) * 0.5f;
    y = (float)height_ - yTop - h;   // 左上原点 -> 左下原点
}

void StillWaterScene::uploadVideoFrame() {
    if (!videoPboBytes_ || !videoPbo_[0].buffer) return;

    // 双 PBO 轮转：这一帧写 B，上一帧的 A 可能还在被 GPU 读
    VideoPboSlot& pbo = videoPbo_[videoPboIndex_];
    videoPboIndex_ = (videoPboIndex_ + 1) % 2;

    // 上一次用这块 PBO 的 texSubImage2D 必须已经完成，才能覆盖它的内存
    if (pbo.fence) {
        glClientWaitSync(pbo.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 2000000);   // 最多等 2ms
        glDeleteSync(pbo.fence);
    }
    pbo.fence = nullptr;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo.buffer);

    // 1) CPU -> PBO：三平面首尾相接，偏移量与下面的 texSubImage2D 一致。
    //    持久映射下直接写指针（coherent，不需要 glFlushMappedBufferRange）
    int offset = 0;
    if (pbo.mapped) {
        unsigned char* dst = static_cast<unsigned char*>(pbo.mapped);
        for (int p = 0; p < 3; ++p) {
            const int bytes = video_.planeWidth(p) * video_.planeHeight(p);
            std::memcpy(dst + offset, video_.plane(p), (size_t)bytes);
            offset += bytes;
        }
    } else {
        for (int p = 0; p < 3; ++p) {
            const int bytes = video_.planeWidth(p) * video_.planeHeight(p);
            glBufferSubData(GL_PIXEL_UNPACK_BUFFER, (GLintptr)offset,
                            (GLsizeiptr)bytes, video_.plane(p));
            offset += bytes;
        }
    }

    // 2) PBO -> 纹理：R8 平面行宽不一定 4 字节对齐，临时改 UNPACK_ALIGNMENT
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    offset = 0;
    for (int p = 0; p < 3; ++p) {
        const int pw = video_.planeWidth(p), ph = video_.planeHeight(p);
        glBindTexture(GL_TEXTURE_2D, videoTex_[p].id());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph,
                        GL_RED, GL_UNSIGNED_BYTE, (const void*)(intptr_t)offset);
        offset += pw * ph;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // 排在 texSubImage2D 之后：fence 触发时说明 GPU 已经把这块 PBO 读完
    pbo.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void StillWaterScene::drawVideo() {
    if (!videoEnabled_ || !videoTex_[0].id() || !videoProg_.valid()) return;

    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    videoRectPx(x, y, w, h);
    if (w <= 0.0f || h <= 0.0f) return;

    const float rect[4] = { x, y, w, h };

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    videoProg_.use();
    for (int p = 0; p < 3; ++p) {
        glActiveTexture(GL_TEXTURE0 + p);
        glBindTexture(GL_TEXTURE_2D, videoTex_[p].id());
    }
    videoProg_.set1i("uTexY", 0);
    videoProg_.set1i("uTexU", 1);
    videoProg_.set1i("uTexV", 2);
    videoProg_.set2f("uResolution", (float)width_, (float)height_);
    videoProg_.set4fv("uRect", rect, 1);
    videoProg_.set1f("uAlpha", videoAlpha_);
    quad_.draw();

    for (int p = 0; p < 3; ++p) {
        glActiveTexture(GL_TEXTURE0 + p);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
}

void StillWaterScene::render(const FrameContext& fc) {
    if (!ready_) return;
    const int w = fc.width > 0 ? fc.width : width_;
    const int h = fc.height > 0 ? fc.height : height_;

    // 默认帧缓冲：整屏铺满
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.031f, 0.173f, 0.192f, 1.0f);   // #082c31
    glClear(GL_COLOR_BUFFER_BIT);

    drawFish(fc.time);

    // 涟漪数据 -> 24x1 RGBA32F
    for (int i = 0; i < kRingSlots * 4; ++i) ringData_[i] = 0.0f;
    for (size_t i = 0; i < rings_.size() && i < (size_t)kRingSlots; ++i) {
        ringData_[i * 4 + 0] = rings_[i].x;
        ringData_[i * 4 + 1] = rings_[i].y;
        ringData_[i * 4 + 2] = rings_[i].t;
        ringData_[i * 4 + 3] = rings_[i].p;
    }
    ringsTex_.uploadSub(0, 0, kRingSlots, 1, GL_RGBA, GL_FLOAT, ringData_);

    // 水面直接画到窗口（p 的左上原点与照片 v=0 顶部一致）
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    waterProg_.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, photo_.id());
    waterProg_.set1i("photo", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, koiFbo_.valid() ? koiFbo_.texture() : 0);
    waterProg_.set1i("fishScene", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ringsTex_.id());
    waterProg_.set1i("ringsTex", 2);

    waterProg_.set2f("resolution", (float)w, (float)h);
    waterProg_.set2f("imageSize", (float)photo_.width(), (float)photo_.height());
    waterProg_.set1f("time", fc.time);
    waterProg_.set1f("debugKoi", debugKoi_);
    quad_.draw();

    // 解绑纹理单元 2/1，避免影响后面的 ImGui / threepp
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    drawVideo();     // 层序 50：水面之上、落叶之下
    drawLeaves(fc.time);
}

void StillWaterScene::drawUI() {
    if (!ImGui::CollapsingHeader("静水 StillWater")) return;
    ImGui::Text("资源  %dx%d 照片  %dx%d", width_, height_,
                photo_.width(), photo_.height());

    ImGui::Checkbox("锦鲤", &koiEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox("落叶", &leavesEnabled_);
    ImGui::SameLine();
    bool dbg = debugKoi_ > 0.5f;
    if (ImGui::Checkbox("只看锦鲤层", &dbg)) debugKoi_ = dbg ? 1.0f : 0.0f;
    ImGui::SliderFloat("点击力度", &ripplePower_, 0.2f, 2.0f, "%.2f");
    ImGui::SliderFloat("拖拽力度", &dragPower_, 0.1f, 1.0f, "%.2f");
    ImGui::SliderFloat("时间倍率", &timeScale_, 0.0f, 3.0f, "%.2f");
    ImGui::Checkbox("环境音", &audioEnabled_);
    if (audioEnabled_) {
        ImGui::SameLine();
        ImGui::SliderFloat("音量", &audioVolume_, 0.0f, 1.0f, "%.2f");
    }
    ImGui::Text("音频  %s", audioFailed_ ? audio_.error()
                    : (audioStarted_ ? "循环播放中" : "未启动"));
    if (video_.isOpen()) {
        ImGui::Checkbox("视频层", &videoEnabled_);
        ImGui::SameLine();
        const VideoSource::Stats vs = video_.stats();
        ImGui::Text("视频 %dx%d  解码 %.1f ms", video_.width(), video_.height(), vs.decodeMs);
    }
    if (ImGui::Button("测试涟漪")) {
        addRipple(0.48f, 0.52f, ripplePower_);
    }
    ImGui::SameLine();
    ImGui::Text("涟漪槽 %d/%d", (int)rings_.size(), kRingSlots);
}

} // namespace

IScene* createStillWaterScene() { return new StillWaterScene(); }

} // namespace mate
