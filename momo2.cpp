/****************************************************************************
 * 标题: momo2 - 音视频同步播放器 (OGL 渲染引擎版)
 * 功能: 使用 FFmpeg 解码 MP4 音视频，miniaudio 播放音频，OX::OGL 渲染视频
 *       以音频时钟为主，实现音视频同步播放
 * 依赖: ocore.h, oapp.h, ogl.h, oui.h, miniaudio, FFmpeg静态库, ThorVG
 * 渲染: OX::OGL::Shader + OX::OGL::ShaderToy + OX::OGL::Texture + OX::OGL::FBO
 *       替代原 MPlayer.cpp 的 Filament 渲染引擎
 ****************************************************************************/

#include "json.hpp"
using json = nlohmann::json;

#include "ocore.h"
#include "oapp.h"
#include "ogl.h"
#include "oui.h"

#include "xav.h"
#include "avfft_standalone.h"

#include <glad/glad.h>
#include <windows.h>
#include <wingdi.h>
#include <mmsystem.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <ctime>
#include <thread>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#define HOTKEY_TOGGLE 1
#define HOTKEY_VK VK_F5

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "e:/MX/3rd/lib/libthorvg_mt.lib")

// FFmpeg static libs
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libavformat.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libavcodec.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libavutil.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libswscale.a")
#pragma comment(lib, "e:/MX/3rd/ffmpeg/FF/lib/libswresample.a")

/*==================== 播放模式枚举 ====================*/
enum class PlayMode {
    SingleLoop, ListLoop, Sequential, Random
};

static const char* getPlayModeText(PlayMode mode) {
    switch (mode) {
        case PlayMode::SingleLoop:  return "单曲循环";
        case PlayMode::ListLoop:    return "列表循环";
        case PlayMode::Sequential:  return "顺序播放";
        case PlayMode::Random:      return "随机播放";
        default:                    return "未知";
    }
}

/*==================== 歌单/歌曲数据结构 ====================*/
struct SongInfo {
    std::string title, artist, album, filePath;
    float duration = 0.0f;
    std::string addDate;
};

struct PlaylistInfo {
    std::string name, createDate, type = "builtin";
    std::vector<SongInfo> songs;
    std::string directoryPath;
    bool autoScan = true;
    int scanInterval = 3600;
};

/*==================== Config constants ====================*/
static constexpr const char* kScanDirs[]    = {"E:/xmusic","D:\\ffbin", "E:\\movie\\Movies\\TikTok"};
static constexpr const char* kDefaultFlac   = "E:\\movie\\music\\goodbye happiness.flac";
static constexpr const char* kDefaultMp4    = "E:\\movie\\Movies\\TikTok\\looklook9766_2025-01-23-19-33-40_1737632020200.mp4";
static constexpr const char* kFontPath      = "e:/MX/Data/siyuan.ttf";
static constexpr const char* kFfmpegPath    = "D:\\ffbin\\ffmpeg.exe";

/*==================== 全屏 Quad 顶点数据 ====================*/
static const float kQuadVertices[] = {
    // pos(x,y)   uv(u,v)
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
};

/*==================== FX Effect sources (GLSL ShaderToy) ====================*/

// Effect 0: DynamicRect
static const char* g_fxSource0 = R"GLSL(
uniform float iTime;
uniform vec2 iResolution;
uniform vec4 iMouse;
uniform vec2 texSize;
uniform sampler2D sceneTex;
uniform sampler2D lyricsTex;

vec3 bgColor = vec3(0.01, 0.16, 0.42);
vec3 rectColor = vec3(0.01, 0.26, 0.57);
const float noiseIntensity = 2.8;
const float noiseDefinition = 0.6;
const vec2 glowPos = vec2(-2., 0.);
const float total = 60.;
const float minSize = 0.03;
const float maxSize = 0.08;
const float yDistribution = 0.5;

float fx_random(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}
float fx_noise(in vec2 p) {
    p *= noiseIntensity;
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(fx_random(i + vec2(0.0,0.0)),
                   fx_random(i + vec2(1.0,0.0)), u.x),
               mix(fx_random(i + vec2(0.0,1.0)),
                   fx_random(i + vec2(1.0,1.0)), u.x), u.y);
}
float fx_fbm(in vec2 uv) {
    uv *= 5.0; mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
    float f = 0.5000 * fx_noise(uv); uv = m * uv;
    f += 0.2500 * fx_noise(uv); uv = m * uv;
    f += 0.1250 * fx_noise(uv); uv = m * uv;
    f += 0.0625 * fx_noise(uv); uv = m * uv;
    f = 0.5 + 0.5 * f; return f;
}
vec3 fx_bg(vec2 uv) {
    float velocity = iTime / 1.6;
    float intensity = sin(uv.x * 3. + velocity * 2.) * 1.1 + 1.5;
    uv.y -= 2.; vec2 bp = uv + glowPos; uv *= noiseDefinition;
    float rb = fx_fbm(vec2(uv.x * .5 - velocity * .03, uv.y)) * .1;
    uv += rb;
    float rz = fx_fbm(uv * .9 + vec2(velocity * .35, 0.0));
    rz *= dot(bp * intensity, bp) + 1.2;
    vec3 col = bgColor / (.1 - rz);
    return sqrt(abs(col));
}
float fx_rectangle(vec2 uv, vec2 pos, float width, float height, float blur) {
    pos = (vec2(width, height) + .01) / 2. - abs(uv - pos);
    pos = smoothstep(0., blur, pos);
    return pos.x * pos.y;
}
mat2 fx_rotate2d(float ang) {
    return mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord.xy / iResolution.xy * 2. - 1.;
    uv.x *= iResolution.x / iResolution.y;
    float t = iTime;
    vec3 color = fx_bg(uv) * (2. - abs(uv.y * 2.));
    float velX = -t / 8.;
    float velY = t / 10.;
    for (float i = 0.; i < total; i++) {
        float index = i / total;
        float rnd = fx_random(vec2(index));
        vec3 pos = vec3(0.);
        pos.x = fract(velX * rnd + index) * 4. - 2.0;
        pos.y = sin(index * rnd * 1000. + velY) * yDistribution;
        pos.z = maxSize * rnd + minSize;
        vec2 uvRot = uv - pos.xy + pos.z / 2.;
        uvRot = fx_rotate2d(i + t / 2.) * uvRot;
        uvRot += pos.xy + pos.z / 2.;
        float rect = fx_rectangle(uvRot, pos.xy, pos.z, pos.z, (maxSize + minSize - pos.z) / 2.);
        color += rectColor * rect * pos.z / maxSize;
    }
    float fxAlpha = clamp(dot(color, vec3(0.299, 0.587, 0.114)) * 3.0, 0.0, 0.85);
    fragColor = vec4(color, fxAlpha);
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    vec4 fragColor;
    mainImage(fragColor, fragCoord);
    gl_FragColor = fragColor;
}
)GLSL";

// Effect 1: Van Gogh Sunset (original from MPlayer - painted landscape)
static const char* g_fxSource1 = R"GLSL(
uniform float iTime;
uniform vec2 iResolution;

#define fx_p(t, a, b, c, d) ( a + b*cos( 6.28318*(c*t+d) ) )
#define fx_sp(t) fx_p(t,vec3(.26,.76,.77),vec3(1,.3,1),vec3(.8,.4,.7),vec3(0,.12,.54))
#define fx_hue(v) ( .6 + .76 * cos(6.3*(v) + vec4(0,23,21,0) ) )
#define FX_TWO_PI 6.28318530718

#define MountainLayerThreecol vec3(26., 65., 74.)/255.
#define MountainLayerFourCol vec3(14., 49., 55.)/255.
#define FieldMid              vec3(94., 121., 62.)/255.

float fx_hash1(float p) {
    p = fract(p * .1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float fx_noise1d(float x) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(fx_hash1(i), fx_hash1(i + 1.0), f);
}

float fx_hash12(vec2 p) {
    vec3 p3  = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 fx_hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xx+p3.yz)*p3.zy);
}

vec2 fx_rot2D(vec2 st, float a){
    return mat2(cos(a),-sin(a),sin(a),cos(a))*st;
}
mat2 fx_rot2Dm(float a){
    return mat2(cos(a),-sin(a),sin(a),cos(a));
}

float fx_st(float a, float b, float s) { return smoothstep(a-s, a+s, b); }

float fx_noise2d(in vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f*f*(3.-2.*f);
    return mix(mix(dot(fx_hash22(i+vec2(0,0)), f-vec2(0,0)),
                   dot(fx_hash22(i+vec2(1,0)), f-vec2(1,0)), u.x),
               mix(dot(fx_hash22(i+vec2(0,1)), f-vec2(0,1)),
                   dot(fx_hash22(i+vec2(1,1)), f-vec2(1,1)), u.x), u.y);
}

float fx_snoise(vec2 p) { return fx_noise2d(p)*0.5 + 0.5; }

vec3 fx_paintedSkyMountains(vec2 g, vec2 r, float time) {
    vec2 uv = (g+g-r)/r.y;
    vec2 sun_pos = vec2(r.x/r.y * 0.35, -0.15);
    vec2 sh = fx_rot2D(sun_pos, fx_noise2d(uv+time*.25)*.3);

    vec3 f = vec3(0);
    float sm = 3./r.y;
    vec2 u, id, lc, t;
    float xd, yd, h;

    u = uv + sh;
    yd = 60.;
    id = vec2((length(u)+.01)*yd,0);
    xd = floor(id.x)*.09;
    h = (fx_hash12(floor(id.xx))*.5+.25)*(time+10.)*.25;
    t = fx_rot2D(u,h);
    id.y = atan(t.y,t.x)*xd;
    lc = fract(id);
    id -= lc;

    t = vec2(cos((id.y+.5)/xd)*(id.x+.5)/yd,sin((id.y+.5)/xd)*(id.x+.5)/yd);
    t = fx_rot2D(t,-h) - sh;

    h = fx_noise2d(t*vec2(.5,1)-vec2(time*.2,0)) * step(-.25,t.y);
    h = smoothstep(.052,.055, h);
    lc += (fx_noise2d(lc*vec2(1,4)+id))*vec2(.7,.2);

    f = mix(fx_sp(sin(length(u)-.1))*.35,
           mix(fx_sp(sin(length(u)-.1)+(fx_hash12(id)-.5)*.15),vec3(1),h),
           fx_st(abs(lc.x-.5),.4,sm*yd)*fx_st(abs(lc.y-.5),.48,sm*xd));

    float cld = fx_noise2d(-sh*vec2(.5,1) - vec2(time*.2,0));
    cld = 1.- smoothstep(.0,.15,cld)*.5;
    u = (uv - vec2(0.0, 0.25)) * vec2(1, 15);
    id = floor(u);

    for (float i = 1.; i > -1.; i--) {
        if (id.y+i < 0.0) {
            lc = fract(u)-.5;
            lc.y = (lc.y+(sin(uv.x*8.-time*0.8+id.y+i))*.3-i)*4.;
            h = fx_hash12(vec2(id.y+i,floor(lc.y)));
            xd = 6.+h*4.;
            yd = 30.;
            lc.x = uv.x*xd+sh.x*9.;
            lc.x += sin(time * (.2 + h*1.5))*.5;
            h = .8*smoothstep(5.,.0,abs(floor(lc.x)))*cld+.1;

            f = mix(f,mix(MountainLayerFourCol,MountainLayerThreecol,h),fx_st(lc.y,0.,sm*yd));
            lc += fx_noise2d(lc*vec2(3,.5))*vec2(.1,.6);

            vec3 strokeCol = fx_hue(fx_hash12(floor(lc))*.1+.35).rgb*(1.2+floor(lc.y)*.17);
            f = mix(f, mix(strokeCol,FieldMid,h), fx_st(lc.y,0.,sm*xd)*fx_st(abs(fract(lc.x)-.5),.48,sm*xd)*fx_st(abs(fract(lc.y)-.5),.3,sm*yd));
        }
    }
    return f;
}

vec3 fx_paintedWaterSand(vec2 g, vec2 r, float time, vec3 bgCol) {
    vec2 uv = (g+g-r)/r.y;
    vec2 sun_pos = vec2(r.x/r.y * 0.35, -0.15);
    vec2 sh = fx_rot2D(sun_pos, fx_noise2d(uv+time*.25)*.3);
    float sm = 3./r.y;
    vec3 f = bgCol;

    float waterZone = smoothstep(0.16, 0.14, uv.y);
    f = mix(f, vec3(0.05, 0.15, 0.3), waterZone);

    float cld = fx_noise2d(-sh*vec2(.5,1) - vec2(time*.2,0));
    cld = 1.- smoothstep(.0,.15,cld)*.5;

    vec2 u = (uv - vec2(0.0, 0.15)) * vec2(1,15);
    vec2 id = floor(u);

    for (float i = 1.; i > -1.; i--) {
        if (id.y+i < 0.0) {
            vec2 lc = fract(u)-.5;
            lc.y = (lc.y+(sin(uv.x*12.-time*3.+id.y+i))*.25-i)*4.;
            float h = fx_hash12(vec2(id.y+i,floor(lc.y)));
            float xd = 6.+h*4.;
            float yd = 30.;
            lc.x = uv.x*xd+sh.x*9.;
            lc.x += sin(time * (.5 + h*2.))*.5;
            h = .8*smoothstep(5.,.0,abs(floor(lc.x)))*cld+.1;

            vec3 waterBase = mix(vec3(0.0, 0.15, 0.4), vec3(0.6, 0.4, 0.1), h);
            f = mix(f, waterBase, fx_st(lc.y, 0., sm*yd));
            lc += fx_noise2d(lc*vec2(3,.5))*vec2(.1,.6);

            vec3 strokeCol = mix(fx_hue(fx_hash12(floor(lc))*.1+.56).rgb*(1.2+floor(lc.y)*.17), vec3(1.0, 0.8, 0.2), h);
            f = mix(f, strokeCol, fx_st(lc.y,0.,sm*xd)*fx_st(abs(fract(lc.x)-.5),.48,sm*xd)*fx_st(abs(fract(lc.y)-.5),.3,sm*yd));
        }
    }

    vec2 u_sand = uv + fx_noise2d(uv*2.)*.1 + vec2(0, sin(uv.x*1.2+3.)*.2 + 0.7);
    vec3 sandCol = vec3(0.85, 0.75, 0.50);
    vec3 sandDark = vec3(0.60, 0.45, 0.25);
    f = mix(f, sandDark * 0.7, step(u_sand.y, .0));

    float xd_s = 60.;
    u_sand = u_sand * vec2(xd_s, xd_s/3.5);

    if (u_sand.y < 1.2) {
        for (float y = 0.; y > -3.; y--) {
            for (float x = -2.; x < 3.; x++) {
                vec2 id_s = floor(u_sand) + vec2(x,y);
                vec2 lc_s = (fract(u_sand) + vec2(1.-x,-y))/vec2(5,3);
                float h_s = (fx_hash12(id_s)-.5)*.25+.5;

                lc_s -= vec2(.3, .5-h_s*.4);
                lc_s.x += sin(((time*0.5+h_s*2.-id_s.x*.05-id_s.y*.05)*1.1+id_s.y*.5)*2.)*(lc_s.y+.5)*.2;
                vec2 t_s = abs(lc_s)-vec2(.03, .5-h_s*.5);
                float l = length(max(t_s,0.)) + min(max(t_s.x,t_s.y),0.);

                l -= fx_noise2d(lc_s*7.+id_s)*.1;

                vec4 C = vec4(sandDark * 0.5, fx_st(l, .1, sm*xd_s*.09));
                vec4 fg = vec4(sandCol * (1.1+lc_s.y*1.2) * (1.2-h_s*1.2), 1.);
                C = mix(C, fg, fx_st(l, .04, sm*xd_s*.09));

                f = mix(f, C.rgb, C.a * step(id_s.y, -1.));
            }
        }
    }
    return f;
}

float fx_sdTri(vec2 p, float base, float tip, float width) {
    float dY = max(base - p.y, p.y - tip);
    float frac = clamp((p.y - base) / (tip - base), 0.0, 1.0);
    float curWidth = width * (1.0 - frac);
    float dX = abs(p.x) - curWidth;
    return max(dX, dY);
}

float fx_sdTree(vec2 p, vec2 pos, float scale, float ftime) {
    vec2 q = (p - pos) / scale;
    q.x -= sin(ftime * 1.5 + pos.x * 10.0) * 0.02 * max(0.0, q.y);
    float d = max(abs(q.x) - 0.03, max(-q.y, q.y - 0.2));
    d = min(d, fx_sdTri(q, 0.1, 0.45, 0.25));
    d = min(d, fx_sdTri(q, 0.25, 0.65, 0.2));
    d = min(d, fx_sdTri(q, 0.45, 0.85, 0.15));
    d = min(d, fx_sdTri(q, 0.65, 1.0, 0.1));
    return d * scale;
}

float fx_getLand(vec2 uv, float aspect) {
    float d = 1.0;
    float w1 = 0.05 * exp(-12.0 * pow(uv.x - aspect*0.1, 2.0));
    float c1 = 0.36 - 0.01 * uv.x;
    float land1 = abs(uv.y - c1) - w1 + smoothstep(0.008, 0.0, w1);
    d = min(d, land1);
    float w2 = 0.03 * exp(-30.0 * pow(uv.x - aspect*0.52, 2.0));
    float c2 = 0.48;
    float land2 = abs(uv.y - c2) - w2 + smoothstep(0.008, 0.0, w2);
    d = min(d, land2);
    return d;
}

float fx_getTrees(vec2 uv, float aspect, float ftime) {
    float d = 1.0;
    d = min(d, fx_sdTree(uv, vec2(aspect*0.05, 0.41), 0.18, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.11, 0.40), 0.22, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.17, 0.38), 0.14, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.23, 0.36), 0.09, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.48, 0.508), 0.07, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.51, 0.51), 0.10, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.54, 0.50), 0.06, ftime));
    return d;
}

vec4 fx_paintedTrees(vec2 uv, vec2 r, float time, float aspect) {
    float sm = 3.0 / r.y;
    float treeDist = fx_getTrees(uv, aspect, time);
    float treeMask = smoothstep(0.02, -0.01, treeDist);
    float coreMask = smoothstep(0.003, 0.0, treeDist);
    if (treeMask <= 0.0) return vec4(0.0);
    vec3 baseCol = vec3(0.05, 0.1, 0.08);
    vec4 C = vec4(baseCol, coreMask);
    float xd = 160.0;
    float yd = 45.0;
    for (float i = 0.0; i < 3.0; i++) {
        vec2 u = uv;
        u.x -= sin(time * 1.5 + uv.x * 10.0) * 0.02 * max(0.0, uv.y - 0.4);
        u += fx_noise2d(u * 12.0 + vec2(time * 0.3, i * 5.0)) * 0.025;
        vec2 t = u * vec2(xd, yd) + vec2(i * 11.3, i * 17.7);
        vec2 id = floor(t);
        vec2 lc = fract(t);
        float h = fx_hash12(id);
        if (h < 0.4) continue;
        float taper = mix(1.2, 0.2, lc.y);
        float outline = fx_st(abs(lc.x - 0.5), 0.35 * taper, sm * xd) * fx_st(abs(lc.y - 0.5), 0.48, sm * yd);
        float body = fx_st(abs(lc.x - 0.5), 0.22 * taper, sm * xd) * fx_st(abs(lc.y - 0.5), 0.4, sm * yd);
        vec3 strokeCol = mix(vec3(0.05, 0.25, 0.1), vec3(0.15, 0.4, 0.15), h);
        if (h > 0.8) strokeCol = mix(strokeCol, vec3(0.3, 0.5, 0.15), 0.6);
        if (h < 0.55) strokeCol = mix(strokeCol, vec3(0.0, 0.15, 0.2), 0.7);
        float strokeAlpha = outline * treeMask;
        C.rgb = mix(C.rgb, vec3(0.02, 0.04, 0.03), strokeAlpha);
        C.rgb = mix(C.rgb, strokeCol, body * treeMask);
        C.a = max(C.a, strokeAlpha);
    }
    return C;
}

vec4 fx_islandGrass(vec2 uv, vec2 r, float time, float aspect) {
    float sm = 3.0 / r.y;
    vec4 O = vec4(0.0);
    vec2 u = uv + fx_noise2d(uv*2.0)*0.05;
    float xd = 160.0;
    u = u * vec2(xd, xd/2.0);
    vec3 f_col = mix(vec3(0.3, 0.65, 0.2), vec3(0.5, 0.8, 0.2), sin(time*0.5)*0.5 + 0.5);
    vec3 cRock = vec3(0.08, 0.1, 0.12);
    for (float y = 0.0; y > -4.0; y--) {
        for (float x = -2.0; x < 3.0; x++) {
            vec2 id = floor(u) + vec2(x,y);
            vec2 root_uv = id / vec2(xd, xd/2.0);
            float w1 = 0.05 * exp(-12.0 * pow(root_uv.x - aspect*0.1, 2.0));
            float c1 = 0.36 - 0.01 * root_uv.x;
            float land1 = abs(root_uv.y - c1) - w1 + smoothstep(0.008, 0.0, w1);
            float w2 = 0.03 * exp(-30.0 * pow(root_uv.x - aspect*0.52, 2.0));
            float c2 = 0.48;
            float land2 = abs(root_uv.y - c2) - w2 + smoothstep(0.008, 0.0, w2);
            float landDist = min(land1, land2);
            float rootAlpha = smoothstep(0.01, -0.01, landDist);
            if (rootAlpha <= 0.0) continue;
            vec2 lc = (fract(u) + vec2(1.0-x, -y)) / vec2(5.0, 3.0);
            float h = (fx_hash12(id) - 0.5) * 0.25 + 0.5;
            lc -= vec2(0.3, 0.5 - h*0.4);
            lc.x += sin(((time*1.5 + h*2.0 - id.x*0.05 - id.y*0.05)*1.1 + id.y*0.5)*2.0) * (lc.y + 0.5) * 0.25;
            vec2 t = abs(lc) - vec2(0.02, 0.5 - h*0.5);
            float l = length(max(t, 0.0)) + min(max(t.x, t.y), 0.0);
            l -= fx_noise2d(lc*7.0 + id) * 0.1;
            vec4 C = vec4(cRock, fx_st(l, 0.1, sm*xd*0.09));
            vec4 fg = vec4(f_col * (1.2 + lc.y*2.0) * (1.8 - h*2.5), 1.0);
            C = mix(C, fg, fx_st(l, 0.04, sm*xd*0.09));
            C.a *= rootAlpha;
            O = mix(O, C, C.a);
        }
    }
    return O;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord.xy / iResolution;
    float aspect = iResolution.x / iResolution.y;
    uv.x *= aspect;
    float time = iTime;

    // Sky & Mountains
    vec3 col = fx_paintedSkyMountains(fragCoord.xy, iResolution, time);
    // Water & Sand
    col = fx_paintedWaterSand(fragCoord.xy, iResolution, time, col);
    // Land base
    float landDist = fx_getLand(uv, aspect);
    float landMask = smoothstep(0.003, 0.0, landDist);
    vec3 cRock = vec3(0.08, 0.1, 0.12);
    col = mix(col, cRock, landMask);
    // Painted Trees
    vec4 trees = fx_paintedTrees(uv, iResolution, time, aspect);
    col = mix(col, trees.rgb, trees.a);
    // Island Grass
    vec4 grass = fx_islandGrass(uv, iResolution, time, aspect);
    col = mix(col, grass.rgb, grass.a);

    float fxAlpha = clamp(dot(col, vec3(0.299, 0.587, 0.114)) * 1.8 + 0.15, 0.0, 0.85);
    fragColor = vec4(col, fxAlpha);
}

void main() {
    vec4 c;
    mainImage(c, gl_FragCoord.xy);
    gl_FragColor = c;
}
)GLSL";

// Effect 2: HeartfeltRain (original from MPlayer - glass refraction + rain by BigWIngs)
static const char* g_fxSource2 = R"GLSL(
#define S(a, b, t) smoothstep(a, b, t)
#define HAS_HEART
#define USE_POST_PROCESSING

uniform float iTime;
uniform vec2 iResolution;
uniform vec2 iMouseXY;
uniform vec2 iMouseZW;
uniform vec2 texSize;
uniform sampler2D sceneTex;
uniform sampler2D lyricsTex;

vec3 fx_N13(float p) {
   vec3 p3 = fract(vec3(p) * vec3(.1031,.11369,.13787));
   p3 += dot(p3, p3.yzx + 19.19);
   return fract(vec3((p3.x + p3.y)*p3.z, (p3.x+p3.z)*p3.y, (p3.y+p3.z)*p3.x));
}

float fx_N(float t) {
    return fract(sin(t*12345.564)*7658.76);
}

float fx_Saw(float b, float t) {
    return S(0., b, t)*S(1., b, t);
}

vec2 fx_DropLayer2(vec2 uv, float t) {
    vec2 UV = uv;
    uv.y += t*0.75;
    vec2 a = vec2(6., 1.);
    vec2 grid = a*2.;
    vec2 id = floor(uv*grid);

    float colShift = fx_N(id.x);
    uv.y += colShift;

    id = floor(uv*grid);
    vec3 n = fx_N13(id.x*35.2+id.y*2376.1);
    vec2 st = fract(uv*grid)-vec2(.5, 0);

    float x = n.x-.5;
    float y = UV.y*20.;
    float wiggle = sin(y+sin(y));
    x += wiggle*(.5-abs(x))*(n.z-.5);
    x *= .7;
    float ti = fract(t+n.z);
    y = (fx_Saw(.85, ti)-.5)*.9+.5;
    vec2 p = vec2(x, y);

    float d = length((st-p)*a.yx);
    float mainDrop = S(.4, .0, d);

    float r = sqrt(S(1., y, st.y));
    float cd = abs(st.x-x);
    float trail = S(.23*r, .15*r*r, cd);
    float trailFront = S(-.02, .02, st.y-y);
    trail *= trailFront*r*r;

    y = UV.y;
    float trail2 = S(.2*r, .0, cd);
    float droplets = max(0., (sin(y*(1.-y)*120.)-st.y))*trail2*trailFront*n.z;
    y = fract(y*10.)+(st.y-.5);
    float dd = length(st-vec2(x, y));
    droplets = S(.3, 0., dd);
    float m = mainDrop+droplets*r*trailFront;

    return vec2(m, trail);
}

float fx_StaticDrops(vec2 uv, float t) {
    uv *= 40.;
    vec2 id = floor(uv);
    uv = fract(uv)-.5;
    vec3 n = fx_N13(id.x*107.45+id.y*3543.654);
    vec2 p = (n.xy-.5)*.7;
    float d = length(uv-p);
    float fade = fx_Saw(.025, fract(t+n.z));
    float c = S(.3, 0., d)*fract(n.z*10.)*fade;
    return c;
}

vec2 fx_Drops(vec2 uv, float t, float l0, float l1, float l2) {
    float s = fx_StaticDrops(uv, t)*l0;
    vec2 m1 = fx_DropLayer2(uv, t)*l1;
    vec2 m2 = fx_DropLayer2(uv*1.85, t)*l2;
    float c = s+m1.x+m2.x;
    c = S(.3, 1., c);
    return vec2(c, max(m1.y*l0, m2.y*l1));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord-.5*iResolution) / iResolution.y;
    vec2 UV = fragCoord/iResolution;
    vec2 lrcUV = UV;
    float mx = iMouseXY.x / iResolution.x;
    float my = iMouseXY.y / iResolution.y;
    float mz = iMouseZW.x;
    float T = iTime + mx*2.;

    #ifdef HAS_HEART
    T = mod(iTime, 102.);
    T = mix(T, mx*102., mz>0.?1.:0.);
    #endif

    float t = T*.2;
    float rainAmount = mz>0. ? my : sin(T*.05)*.3+.7;

    float maxBlur = mix(3., 6., rainAmount);
    float minBlur = 2.;

    #ifdef HAS_HEART
    float story = S(0., 70., T);
    t = min(1., T/70.);
    t = 1.-t;
    t = (1.-t*t)*70.;
    float zoom = mix(.3, 1.2, story);
    uv *= zoom;
    minBlur = 4.+S(.5, 1., story)*3.;
    maxBlur = 6.+S(.5, 1., story)*1.5;
    vec2 hv = uv-vec2(.0, -.1);
    hv.x *= .5;
    float s = S(110., 70., T);
    hv.y -= sqrt(abs(hv.x))*.5*s;
    float heart = length(hv);
    heart = S(.4*s, .2*s, heart)*s;
    rainAmount = heart;
    maxBlur -= heart;
    uv *= 1.5;
    t *= .25;
    #else
    float zoom = -cos(T*.2);
    uv *= .7+zoom*.3;
    #endif
    UV = (UV-.5)*(.9+zoom*.1)+.5;

    float texAspect = texSize.x / texSize.y;
    float scrAspect = iResolution.x / iResolution.y;
    vec2 bgUV = UV;
    float inBounds = 1.0;
    if (scrAspect > texAspect) {
        float w = texAspect / scrAspect;
        float l = (1.0 - w) * 0.5;
        bgUV.x = (UV.x - l) / w;
        inBounds = step(l, UV.x) * step(UV.x, 1.0 - l);
    } else {
        float h = scrAspect / texAspect;
        float t = (1.0 - h) * 0.5;
        bgUV.y = (UV.y - t) / h;
        inBounds = step(t, UV.y) * step(UV.y, 1.0 - t);
    }

    float staticDrops = S(-.5, 1., rainAmount)*2.;
    float layer1 = S(.25, .75, rainAmount);
    float layer2 = S(.0, .5, rainAmount);

    vec2 c = fx_Drops(uv, t, staticDrops, layer1, layer2);
    vec2 e = vec2(.001, 0.);
    float cx = fx_Drops(uv+e, t, staticDrops, layer1, layer2).x;
    float cy = fx_Drops(uv+e.yx, t, staticDrops, layer1, layer2).x;
    vec2 n = vec2(cx-c.x, cy-c.x);

    #ifdef HAS_HEART
    n *= 1.-S(60., 85., T);
    c.y *= 1.-S(80., 100., T)*.8;
    #endif

    float focus = mix(maxBlur-c.y, minBlur, S(.1, .2, c.x));
    vec2 sampleUV = bgUV + vec2(n.x,-n.y)*(1.0/(1.0+focus*4.0));
    vec2 lrcSampleUV = vec2(lrcUV.x, lrcUV.y) + vec2(n.x, n.y)*(1.0/(1.0+focus*4.0));
    vec3 col = inBounds > 0.5 ? texture(sceneTex, sampleUV).rgb : vec3(0.0);
    vec4 lrc = texture(lyricsTex, lrcSampleUV);
    col = mix(col, lrc.rgb, lrc.a);

    #ifdef USE_POST_PROCESSING
    t = (T+3.)*.5;
    float colFade = sin(t*.2)*.5+.5+story;
    col *= mix(vec3(1.), vec3(.8, .9, 1.3), colFade);
    float fade = S(0., 10., T);
    float lightning = sin(t*sin(t*10.));
    lightning *= pow(max(0., sin(t+sin(t))), 10.);
    col *= 1.+lightning*fade*mix(1., .1, story*story);
    col *= 1.-dot(UV-=.5, UV);

    #ifdef HAS_HEART
    col = mix(pow(col, vec3(1.2)), col, heart);
    fade *= S(102., 97., T);
    #endif

    col *= fade;
    #endif

    fragColor = vec4(col, 1.0);
}

void main() {
    vec4 c;
    mainImage(c, gl_FragCoord.xy);
    gl_FragColor = c;
}
)GLSL";

struct FXEffect { const char* name; const char* code; };
static const FXEffect g_fxEffects[] = {
    {"DynamicRect",   g_fxSource0},
    {"VanGoghSunset", g_fxSource1},
    {"HeartfeltRain", g_fxSource2},
};
static const int g_fxEffectCount = sizeof(g_fxEffects) / sizeof(g_fxEffects[0]);

/*==================== AppState ====================*/
struct AppState {
    // --- Window / Rendering ---
    OX::Application app;
    OX::UIManager ui;
    bool needRebuildUI = false;
    int pendingLoadFileIdx = -1;

    // --- Aux GL context (for ThorVG rendering) ---
    HWND  auxHwnd  = nullptr;
    HDC   auxDC    = nullptr;
    HGLRC auxGLRC  = nullptr;
    HGLRC mainGLRC = nullptr;

    // --- GL resources ---
    OX::OGL::Shader videoShader;
    OX::OGL::Shader lyricsShader;
    OX::OGL::Texture videoGLTex;
    OX::OGL::Texture lyricsGLTex;
    GLuint videoVAO = 0;
    GLuint videoVBO = 0;
    float videoScaleX = 1.0f;
    float videoScaleY = 1.0f;

    // --- Scene FBO (for HeartfeltRain which samples the composed scene) ---
    GLuint sceneFboId = 0;
    GLuint sceneTexId = 0;
    int sceneFboW = 0, sceneFboH = 0;

    // --- ShaderToy FX ---
    static constexpr int MAX_FX = 8;
    bool fxEnabled = false;
    float fxTime = 0.0f;
    int fxEffectIndex = 0;
    int currentFxEffectInToy = -1;
    OX::OGL::ShaderToy fxShaderToy;
    bool fxShaderToyCreated = false;
    float fxMouseX = 0.0f, fxMouseY = 0.0f;
    bool fxMouseClicked = false;
    OX::UIButton* fxBtnPtr = nullptr;
    OX::UIDropdown* fxDropdownPtr = nullptr;

    // --- 背景显示控制 ---
    bool showBackground = true;
    OX::UIButton* bgBtnPtr = nullptr;

    // --- Lyrics FBO ---
    GLuint lrcFboId = 0;
    tvg::GlCanvas* lrcGlCanvas = nullptr;
    int lrcFboW = 0, lrcFboH = 0;
    bool lrcFboDirty = true;
    int lrcLastTexW = 0, lrcLastTexH = 0;

    // --- OUI FBO (for rendering OUI to texture, then overlay quad) ---
    GLuint ouiFboId = 0;
    OX::OGL::Texture ouiGLTex;
    int ouiFboW = 0, ouiFboH = 0;
    int ouiLastTexW = 0, ouiLastTexH = 0;
    bool ouiFboDirty = true;

    // --- CPU pixel buffers (for transfer between aux and main GL contexts) ---
    std::vector<uint8_t> ouiPixelBuf;
    std::vector<uint8_t> lrcPixelBuf;

    // --- 频谱条（FFT 分析） ---
    static constexpr int kAvBarsTexW = 512;
    static constexpr int kAvBarsTexH = 512;
    AudioVisConfig avConfig;
    std::unique_ptr<AudioVisRingBuffer> avRingBuf;
    FftContext avFft{};
    std::vector<float> avMonoBuf;
    std::vector<float> avSpectrum;
    std::vector<float> avBarTargets;
    std::vector<float> avBarHeights;
    std::vector<float> avBarPeaks;
    bool showAudioBars = false;
    double avLastFftTime = 0.0;

    // --- Playback State ---
    bool isPlaying = false;
    bool isPaused = false;
    bool hasFrame = false;
    PlayMode playMode = PlayMode::Sequential;
    bool audioStarted = false;
    double videoTime = 0.0;
    double currentVideoPts = 0.0;
    double frameDuration = 0.0;
    bool pendingSeek = false;
    double pendingSeekTarget = 0.0;

    // --- Audio ---
    ma_device audioDevice{};
    std::atomic<uint64_t> audioSamplesPlayed{0};
    AudioQueue audioQueue{kAudioBufCapacity};
    float volume = 0.5f;
    static constexpr int AUDIO_SAMPLE_RATE = kAudioSampleRate;
    static constexpr int AUDIO_CHANNELS = kAudioChannels;
    static constexpr size_t AUDIO_BUF_FIFTH_SEC   = kAudioBufFifth;
    static constexpr size_t AUDIO_BUF_QUARTER_SEC  = kAudioBufQuarter;
    static constexpr size_t AUDIO_BUF_HALF_SEC     = kAudioBufHalf;
    static constexpr size_t AUDIO_BUF_EOF_THRESH   = kAudioBufEofThresh;
    static constexpr size_t AUDIO_BUF_CAPACITY     = kAudioBufCapacity;
    static constexpr double VIDEO_SYNC_THRESHOLD_SEC = 0.005;
    static constexpr double DEFAULT_FRAME_DURATION_SEC = 0.033;

    // --- Decoder ---
    AVDecoder decoder;
    int videoW = 0;
    int videoH = 0;

    // --- Recorder ---
    FFRecorder recorder;
    bool recording = false;
    std::vector<uint8_t> recFrameBuf;
    int recFBW = 0, recFBH = 0;

    // --- UI Data ---
    char fpsText[64] = "FPS: 0";
    char infoText[128] = "No video";
    char timeText[64] = "00:00 / 00:00";
    char statusText[32] = "Stopped";
    char syncText[64] = "Sync: --";
    char frameText[64] = "Frame: --";

    // --- UI Widgets ---
    OX::UILabel* fpsLabelPtr = nullptr;
    OX::UILabel* infoLabelPtr = nullptr;
    OX::UILabel* timeLabelPtr = nullptr;
    OX::UILabel* statusLabelPtr = nullptr;
    OX::UILabel* syncLabelPtr = nullptr;
    OX::UILabel* frameLabelPtr = nullptr;
    OX::UILabel* rightInfoLabelPtr = nullptr;
    OX::UIButton* playBtnPtr = nullptr;
    OX::UIButton* loopBtnPtr = nullptr;
    OX::UIButton* prevBtnPtr = nullptr;
    OX::UIButton* nextBtnPtr = nullptr;
    OX::UIDropdown* fileDropdownPtr = nullptr;
    OX::UISlider* seekBarPtr = nullptr;
    OX::UISlider* volumeSliderPtr = nullptr;
    OX::UIButton* recordBtnPtr = nullptr;
    bool seekBarDragging = false;

    // --- Playlist ---
    std::vector<std::string> mediaFiles;
    int currentFileIndex = -1;
    char rightInfoText[256] = "";

    // --- 歌单系统 ---
    std::vector<PlaylistInfo> playlists;
    int currentPlaylistIndex = -1;

    // --- 创建歌单对话框 ---
    bool showCreatePlaylistDlg = false;
    char plDlgName[256] = "";
    int plDlgType = 0;
    char plDlgDir[512] = "";
    bool plDlgAutoScan = true;
    int plDlgScanInterval = 3600;

    // --- 添加歌曲对话框 ---
    bool showAddSongsDlg = false;
    int addSongSelIdx = -1;

    // --- 断点续播 ---
    int resumePlaylistIndex = -1;
    int resumeSongIndex = -1;
    double resumePosition = 0.0;
    char resumeFilePath[512] = "";

    // --- 歌单切换 Dropdown ---
    OX::UIDropdown* playlistDropdownPtr = nullptr;

    // --- Lyrics ---
    std::vector<LrcLine> lrcLines;
    bool showLyrics = true;
    OX::UIButton* lyricsBtnPtr = nullptr;
    int lrcCurrentIndex = -1;

    // --- Timing ---
    std::chrono::steady_clock::time_point lastFrameTime;
    float fps = 0.0f;
};

/*==================== 辅助 OpenGL 上下文（用于 ThorVG 渲染） ====================*/
static bool createAuxGLContext(AppState& st) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MoAuxHelperWindow";
    RegisterClassExW(&wc);

    st.auxHwnd = CreateWindowExW(0, L"MoAuxHelperWindow", L"", WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (!st.auxHwnd) {
        OX_LOG("[AUX] CreateWindowExW failed: %lu\n", GetLastError());
        return false;
    }

    st.auxDC = GetDC(st.auxHwnd);
    if (!st.auxDC) {
        OX_LOG("[AUX] GetDC failed\n");
        DestroyWindow(st.auxHwnd); st.auxHwnd = nullptr;
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(st.auxDC, &pfd);
    if (pf == 0 || !SetPixelFormat(st.auxDC, pf, &pfd)) {
        OX_LOG("[AUX] Pixel format failed: %lu\n", GetLastError());
        ReleaseDC(st.auxHwnd, st.auxDC); st.auxDC = nullptr;
        DestroyWindow(st.auxHwnd); st.auxHwnd = nullptr;
        return false;
    }

    st.auxGLRC = wglCreateContext(st.auxDC);
    if (!st.auxGLRC) {
        OX_LOG("[AUX] wglCreateContext failed: %lu\n", GetLastError());
        ReleaseDC(st.auxHwnd, st.auxDC); st.auxDC = nullptr;
        DestroyWindow(st.auxHwnd); st.auxHwnd = nullptr;
        return false;
    }

    // 在辅助上下文中加载 glad
    wglMakeCurrent(st.auxDC, st.auxGLRC);
    if (!gladLoadGL()) {
        OX_LOG("[AUX] gladLoadGL failed\n");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(st.auxGLRC); st.auxGLRC = nullptr;
        ReleaseDC(st.auxHwnd, st.auxDC); st.auxDC = nullptr;
        DestroyWindow(st.auxHwnd); st.auxHwnd = nullptr;
        return false;
    }
    wglMakeCurrent(nullptr, nullptr);

    OX_LOG("[AUX] Aux GL context (hidden window + independent HGLRC) created\n");
    return true;
}

static void destroyAuxGLContext(AppState& st) {
    if (st.auxGLRC) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(st.auxGLRC);
        st.auxGLRC = nullptr;
    }
    if (st.auxDC) {
        ReleaseDC(st.auxHwnd, st.auxDC);
        st.auxDC = nullptr;
    }
    if (st.auxHwnd) {
        DestroyWindow(st.auxHwnd);
        st.auxHwnd = nullptr;
    }
    OX_LOG("[AUX] Aux GL context destroyed\n");
}

/*==================== updateVideoTransform ====================*/
static void updateVideoTransform(AppState& st, int winW, int winH) {
    if (st.videoW <= 0 || st.videoH <= 0) {
        st.videoScaleX = 0.0f;
        st.videoScaleY = 0.0f;
        return;
    }
    float windowAspect = (float)winW / (float)winH;
    float videoAspect  = (float)st.videoW / (float)st.videoH;
    if (windowAspect > videoAspect) {
        st.videoScaleX = videoAspect / windowAspect;
        st.videoScaleY = 1.0f;
    } else {
        st.videoScaleX = 1.0f;
        st.videoScaleY = windowAspect / videoAspect;
    }
}

/*==================== uploadVideoFrame (OGL version) ====================*/
static void uploadVideoFrame(AppState& st, const uint8_t* data, int w, int h) {
    if (!data) return;
    if (!st.videoGLTex.isValid() || (int)st.videoGLTex.getWidth() != w || (int)st.videoGLTex.getHeight() != h) {
        st.videoGLTex.destroy();
        st.videoGLTex.create(w, h, 4);
    }
    glBindTexture(GL_TEXTURE_2D, st.videoGLTex.getId());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

/*==================== 歌词更新 ====================*/
static void updateLyrics(AppState& st) {
    if (st.lrcLines.empty()) return;

    int idx = -1;
    int lo = 0, hi = (int)st.lrcLines.size() - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (st.lrcLines[mid].timestamp <= st.videoTime) {
            idx = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (idx == st.lrcCurrentIndex) return;
    st.lrcCurrentIndex = idx;
    st.lrcFboDirty = true;
}

/*==================== 歌词 FBO 管理（在 aux 上下文中创建） ====================*/
static void destroyLrcFbo(AppState& st) {
    if (!st.auxDC || !st.auxGLRC) return;
    wglMakeCurrent(st.auxDC, st.auxGLRC);
    if (st.lrcGlCanvas) {
        st.lrcGlCanvas->remove(nullptr);
        delete st.lrcGlCanvas;
        st.lrcGlCanvas = nullptr;
    }
    if (st.lrcFboId) {
        glDeleteFramebuffers(1, &st.lrcFboId);
        st.lrcFboId = 0;
    }
    st.lrcFboW = st.lrcFboH = 0;
    wglMakeCurrent(nullptr, nullptr);
    // lyricsGLTex is a main-context texture, destroyed separately
}

static bool createLrcFbo(AppState& st, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (w == st.lrcFboW && h == st.lrcFboH) return true;
    if (!st.auxDC || !st.auxGLRC) return false;

    wglMakeCurrent(st.auxDC, st.auxGLRC);

    destroyLrcFbo(st);
    wglMakeCurrent(st.auxDC, st.auxGLRC); // destroyLrcFbo releases context

    glGenFramebuffers(1, &st.lrcFboId);
    glBindFramebuffer(GL_FRAMEBUFFER, st.lrcFboId);

    GLuint lrcTexId = 0;
    glGenTextures(1, &lrcTexId);
    glBindTexture(GL_TEXTURE_2D, lrcTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lrcTexId, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteTextures(1, &lrcTexId);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &st.lrcFboId);
        st.lrcFboId = 0;
        wglMakeCurrent(nullptr, nullptr);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 创建 GlCanvas（绑定到 FBO，使用 auxGLRC）
    st.lrcGlCanvas = tvg::GlCanvas::gen();
    if (!st.lrcGlCanvas) {
        glDeleteTextures(1, &lrcTexId);
        glDeleteFramebuffers(1, &st.lrcFboId);
        st.lrcFboId = 0;
        wglMakeCurrent(nullptr, nullptr);
        return false;
    }

    auto r = st.lrcGlCanvas->target(st.auxGLRC, st.lrcFboId, (uint32_t)w, (uint32_t)h, tvg::ColorSpace::ABGR8888S);
    if (r != tvg::Result::Success) {
        delete st.lrcGlCanvas;
        st.lrcGlCanvas = nullptr;
        glDeleteTextures(1, &lrcTexId);
        glDeleteFramebuffers(1, &st.lrcFboId);
        st.lrcFboId = 0;
        wglMakeCurrent(nullptr, nullptr);
        return false;
    }

    st.lrcPixelBuf.resize((size_t)w * h * 4);

    st.lrcFboW = w;
    st.lrcFboH = h;
    st.lrcFboDirty = true;
    wglMakeCurrent(nullptr, nullptr);
    OX_LOG("[LRC] FBO created in aux context: %dx%d\n", w, h);
    return true;
}

static void renderLyricsToFbo(AppState& st, float lrcX, float lrcY, float lrcW, float lrcH,
                               const char* fontName) {
    if (!st.lrcGlCanvas || st.lrcLines.empty()) return;

    st.lrcGlCanvas->remove(nullptr);
    auto scene = tvg::Scene::gen();
    if (!scene) return;

    // 歌词面板背景（半透明）
    auto bg = tvg::Shape::gen();
    bg->appendRect(lrcX, lrcY, lrcW, lrcH, 8.0f, 8.0f);
    bg->fill(20, 22, 30, 200);
    bg->strokeFill(60, 62, 80, 200);
    bg->strokeWidth(1.0f);
    scene->push(bg);

    // 7 行歌词
    float lineH = 40.0f;
    float startY = lrcY + (lrcH - 7.0f * lineH) / 2.0f;
    for (int i = 0; i < 7; i++) {
        int lineIdx = st.lrcCurrentIndex + (i - 3);
        if (lineIdx < 0 || lineIdx >= (int)st.lrcLines.size()) continue;
        auto txt = tvg::Text::gen();
        txt->text(st.lrcLines[lineIdx].text.c_str());
        txt->font(fontName);
        txt->translate(lrcX + lrcW / 2.0f, startY + i * lineH + lineH / 2.0f - 4.0f);
        txt->align(0.5f, 0.5f);
        if (i == 3) {
            txt->size(18.0f);
            txt->fill(255, 255, 255);
        } else if (i == 2 || i == 4) {
            txt->size(14.0f);
            txt->fill(180, 180, 200);
        } else {
            txt->size(12.0f);
            txt->fill(120, 120, 140);
        }
        scene->push(txt);
    }

    st.lrcGlCanvas->push(scene);
    st.lrcGlCanvas->draw(true);
    st.lrcGlCanvas->sync();
}

/*==================== Scene FBO (for HeartfeltRain) ====================*/
static void destroySceneFbo(AppState& st) {
    if (st.sceneTexId) { glDeleteTextures(1, &st.sceneTexId); st.sceneTexId = 0; }
    if (st.sceneFboId) { glDeleteFramebuffers(1, &st.sceneFboId); st.sceneFboId = 0; }
    st.sceneFboW = st.sceneFboH = 0;
}

static bool ensureSceneFbo(AppState& st, int w, int h) {
    if (st.sceneFboId && w == st.sceneFboW && h == st.sceneFboH) return true;
    destroySceneFbo(st);

    glGenFramebuffers(1, &st.sceneFboId);
    glBindFramebuffer(GL_FRAMEBUFFER, st.sceneFboId);

    glGenTextures(1, &st.sceneTexId);
    glBindTexture(GL_TEXTURE_2D, st.sceneTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, st.sceneTexId, 0);

    st.sceneFboW = w;
    st.sceneFboH = h;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

/*==================== OUI FBO（在 aux 上下文中创建，ThorVG 需要独立 HGLRC） ====================*/
static void destroyOuiFbo(AppState& st) {
    if (!st.auxDC || !st.auxGLRC) return;
    wglMakeCurrent(st.auxDC, st.auxGLRC);
    if (st.ouiFboId) { glDeleteFramebuffers(1, &st.ouiFboId); st.ouiFboId = 0; }
    st.ouiFboW = st.ouiFboH = 0;
    wglMakeCurrent(nullptr, nullptr);
    // ouiGLTex is a main-context texture, destroyed separately
}

static bool createOuiFbo(AppState& st, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (w == st.ouiFboW && h == st.ouiFboH) return true;
    if (!st.auxDC || !st.auxGLRC) return false;

    wglMakeCurrent(st.auxDC, st.auxGLRC);

    destroyOuiFbo(st);
    wglMakeCurrent(st.auxDC, st.auxGLRC); // destroyOuiFbo releases context

    glGenFramebuffers(1, &st.ouiFboId);
    glBindFramebuffer(GL_FRAMEBUFFER, st.ouiFboId);

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteTextures(1, &texId);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &st.ouiFboId);
        st.ouiFboId = 0;
        wglMakeCurrent(nullptr, nullptr);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    st.ouiPixelBuf.resize((size_t)w * h * 4);

    st.ouiFboW = w;
    st.ouiFboH = h;
    st.ouiFboDirty = true;
    wglMakeCurrent(nullptr, nullptr);
    OX_LOG("[OUI] OUI FBO created in aux context: %dx%d\n", w, h);
    return true;
}

/*==================== AudioBars ThorVG 渲染 ====================*/
static void renderAudioBarsToScene(tvg::Scene* scene, AppState& st, float winW, float winH) {
    if (!st.showAudioBars || !scene) return;
    auto& cfg = st.avConfig;

    float dtClamped = 0.016f;
    float attackSpeed = 1.0f - expf(-dtClamped / 0.10f);
    float releaseSpeed = 1.0f - expf(-dtClamped / 0.40f);

    for (int b = 0; b < cfg.barCount; b++) {
        float target = st.avBarTargets[b];
        float cur = st.avBarHeights[b];
        float speed = (target > cur) ? attackSpeed : releaseSpeed;
        float newH = cur + (target - cur) * speed;
        if (target <= 0.0f && newH < releaseSpeed * 2.0f) newH = 0.0f;
        if (newH < 0.0005f) newH = 0.0f;
        st.avBarHeights[b] = newH;
    }

    float barAreaTop = winH * 0.90f;
    float barAreaH = winH * 0.10f;
    float slotW = winW / (float)cfg.barCount;
    float padding = slotW * 0.15f;

    uint8_t cr = (uint8_t)(cfg.barColor[0] * 255.0f);
    uint8_t cg = (uint8_t)(cfg.barColor[1] * 255.0f);
    uint8_t cb = (uint8_t)(cfg.barColor[2] * 255.0f);
    uint8_t ca = (uint8_t)(cfg.overlayAlpha * 255.0f);

    bool anyActive = false;
    for (int ib = 0; ib < cfg.barCount; ib++) {
        if (st.avBarHeights[ib] > 0.01f) { anyActive = true; break; }
    }

    for (int ib = 0; ib < cfg.barCount; ib++) {
        float hNorm = st.avBarHeights[ib];
        if (hNorm < cfg.minBarHeight) hNorm = anyActive ? cfg.minBarHeight : 0.0f;
        float barH = hNorm * barAreaH;
        if (barH < 1.0f) barH = 1.0f;

        float barX = ib * slotW + padding;
        float barW = slotW - padding * 2.0f;
        float barY = barAreaTop + (barAreaH - barH);

        auto shape = tvg::Shape::gen();
        shape->appendRect(barX, barY, barW, barH, 2.0f, 2.0f);
        shape->fill(cr, cg, cb, ca);
        scene->push(shape);
    }
}

/*==================== FFT 频谱分析 ====================*/
static void analyzeSpectrum(AppState& st, double audioTime) {
    (void)audioTime;
    auto& cfg = st.avConfig;
    size_t need = (size_t)cfg.fftWindowSize;
    if (st.avRingBuf->cap < need) return;

    st.avRingBuf->read(st.avMonoBuf, need);

    for (int i = 0; i < cfg.fftWindowSize; i++) {
        float win = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (float)(cfg.fftWindowSize - 1)));
        st.avMonoBuf[i] *= win;
    }
    fft_forward(st.avFft, st.avMonoBuf.data(), st.avSpectrum.data());

    const int fftBins = cfg.fftBins;
    const int barCount = cfg.barCount;
    constexpr float SAMPLE_RATE = 44100.0f;
    const float nyquist = SAMPLE_RATE * 0.5f;
    const float logMin = log10f(20.0f);
    const float logMax = log10f(20000.0f);
    const float logRange = logMax - logMin;
    const float fBinWidth = SAMPLE_RATE / (float)(cfg.fftWindowSize);

    for (int b = 0; b < barCount; b++) {
        float frac = (float)b / (float)barCount;
        float fLo = powf(10.0f, logMin + ((float)b - 0.5f) / (float)barCount * logRange);
        float fHi = powf(10.0f, logMin + ((float)b + 0.5f) / (float)barCount * logRange);
        fLo = std::max(fLo, 1.0f);
        fHi = std::min(fHi, nyquist);
        int iLo = std::max(1, (int)(fLo / fBinWidth));
        int iHi = std::min(fftBins - 1, (int)(fHi / fBinWidth));
        if (iLo > iHi) iLo = iHi;
        float sumMag = 0.0f;
        int count = 0;
        for (int i = iLo; i <= iHi; i++) { sumMag += st.avSpectrum[i]; count++; }
        float avgMag = (count > 0) ? (sumMag / (float)count) : 0.0f;
        float dB = 20.0f * log10f(avgMag + 1e-10f);
        dB = std::max(dB, -80.0f);
        float scaled = (dB + 80.0f) / 80.0f;
        scaled = std::max(0.0f, std::min(1.0f, scaled));
        st.avBarTargets[b] = scaled;
    }
}

/*==================== miniaudio callback ====================*/
static void ma_data_callback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    AppState* st = (AppState*)pDevice->pUserData;
    if (!st) return;
    float* out = (float*)pOutput;
    size_t needed = frameCount * st->AUDIO_CHANNELS;
    size_t got = st->audioQueue.read(out, needed);
    if (got == 0) {
        static int underflowCount = 0;
        if (++underflowCount <= 5) {
            OX_LOG("[Audio] underflow! frameCount=%u needed=%zu queueSize=%zu\n",
                   frameCount, needed, st->audioQueue.size());
        }
    }
    if (got < needed) {
        memset(out + got, 0, (needed - got) * sizeof(float));
    }
    if (st->avRingBuf && st->avRingBuf->cap > 0 && got > 0) {
        ma_uint32 gotFrames = (ma_uint32)(got / st->AUDIO_CHANNELS);
        if (gotFrames > 0) {
            size_t monoCount = gotFrames;
            std::vector<float> mono(monoCount);
            int ch = st->AUDIO_CHANNELS;
            for (ma_uint32 i = 0; i < gotFrames; i++) {
                float sum = 0.0f;
                for (int c = 0; c < ch; c++) sum += out[i * ch + c];
                mono[i] = sum / (float)ch;
            }
            st->avRingBuf->write(mono.data(), monoCount);
        }
    }
    st->audioSamplesPlayed += frameCount;
}

/*==================== cleanup ====================*/
static void cleanup(AppState& st) {
    if (st.audioDevice.pUserData) {
        ma_device_uninit(&st.audioDevice);
    }
    if (st.videoVAO) { glDeleteVertexArrays(1, &st.videoVAO); st.videoVAO = 0; }
    if (st.videoVBO) { glDeleteBuffers(1, &st.videoVBO); st.videoVBO = 0; }
    st.videoShader.release();
    st.lyricsShader.release();
    st.videoGLTex.destroy();
    st.lyricsGLTex.destroy();
    st.ouiGLTex.destroy();
    destroyLrcFbo(st);
    destroySceneFbo(st);
    destroyOuiFbo(st);
    st.fxShaderToy.destroy();
    fft_free(st.avFft);
    st.decoder.close();
    destroyAuxGLContext(st);
}

/*==================== normalizePath, scanMediaFiles ====================*/
static std::string normalizePath(const std::string& path) {
    std::string s = path;
    for (char& c : s) { if (c == '\\') c = '/'; }
    return s;
}

static void scanMediaFiles(std::vector<std::string>& files, const std::vector<std::string>& dirs) {
    const std::vector<std::string> exts = {
        ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".ts",
        ".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma"
    };
    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) continue;
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = OX::Core::wstrToUtf8(entry.path().extension().wstring());
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
                if (std::find(exts.begin(), exts.end(), ext) != exts.end()) {
                    files.push_back(normalizePath(OX::Core::wstrToUtf8(entry.path().wstring())));
                }
            }
        } catch (...) {}
    }
}

/*==================== parseLRC, parsePlainLyrics, loadLyrics ====================*/
static bool parseLRC(const char* lrcPath, std::vector<LrcLine>& lines) {
    FILE* f = nullptr;
    fopen_s(&f, lrcPath, "rb");
    if (!f) return false;

    lines.clear();
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '\0' || buf[0] == '\n' || buf[0] == '\r') continue;

        char* p = buf;
        std::vector<double> timestamps;
        while (*p == '[') {
            int min = 0, sec_int = 0, frac = 0;
            if (sscanf_s(p, "[%d:%d.%d]", &min, &sec_int, &frac) == 3) {
                double sec = sec_int;
                if (frac < 100)      sec += frac / 100.0;
                else                 sec += frac / 1000.0;
                timestamps.push_back(min * 60.0 + sec);
            }
            char* close = strchr(p, ']');
            if (!close) break;
            p = close + 1;
        }

        while (*p == ' ' || *p == '\t') p++;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';

        std::string text(p);
        if (text.empty()) text = "\xe2\x99\xaa";

        for (double ts : timestamps) {
            lines.push_back({ts, text});
        }
    }
    fclose(f);

    std::sort(lines.begin(), lines.end(),
        [](const LrcLine& a, const LrcLine& b) { return a.timestamp < b.timestamp; });

    OX_LOG("[Lyrics] Parsed %zu lines from %s\n", lines.size(), lrcPath);
    return !lines.empty();
}

static void parsePlainLyrics(const std::string& text, std::vector<LrcLine>& lines, double duration) {
    lines.clear();
    if (text.empty()) return;

    std::istringstream iss(text);
    std::vector<std::string> rawLines;
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) rawLines.push_back(line);
    }
    if (rawLines.empty()) return;

    double interval = duration / (double)(rawLines.size() + 1);
    for (size_t i = 0; i < rawLines.size(); i++) {
        lines.push_back({interval * (i + 1), rawLines[i]});
    }
    OX_LOG("[Lyrics] Parsed %zu plain text lines, interval=%.1fs\n", lines.size(), interval);
}

static void loadLyrics(AppState& st) {
    st.lrcLines.clear();
    st.lrcCurrentIndex = -1;

    std::string embedded = st.decoder.getEmbeddedLyrics();
    if (!embedded.empty()) {
        OX_LOG("[Lyrics] Embedded lyrics: %zu bytes\n", embedded.size());
        if (embedded.find("[") != std::string::npos &&
            embedded.find(":") != std::string::npos) {
            std::string tmpPath = "e:/MX/Data/_lrc_temp.lrc";
            std::ofstream ofs(tmpPath, std::ios::binary);
            if (ofs) {
                ofs << embedded;
                ofs.close();
                if (parseLRC(tmpPath.c_str(), st.lrcLines)) {
                    OX_LOG("[Lyrics] Parsed embedded LRC lyrics: %zu lines\n", st.lrcLines.size());
                    std::remove(tmpPath.c_str());
                    return;
                }
                std::remove(tmpPath.c_str());
            }
        }
        double dur = st.decoder.getDuration();
        if (dur <= 0) dur = 240.0;
        parsePlainLyrics(embedded, st.lrcLines, dur);
        if (!st.lrcLines.empty()) return;
    }

    if (st.currentFileIndex < 0 || st.currentFileIndex >= (int)st.mediaFiles.size()) return;

    std::string audioPath = st.mediaFiles[st.currentFileIndex];
    size_t dotPos = audioPath.find_last_of('.');
    if (dotPos == std::string::npos) return;
    std::string lrcPath = audioPath.substr(0, dotPos) + ".lrc";

    std::ifstream test(lrcPath);
    if (!test.good()) {
        OX_LOG("[Lyrics] No .lrc file found: %s\n", lrcPath.c_str());
        return;
    }
    test.close();

    if (parseLRC(lrcPath.c_str(), st.lrcLines)) {
        st.lrcCurrentIndex = 0;
    }
}

/*==================== loadMediaFile ====================*/
static bool loadMediaFile(AppState& st, const char* path, int winW, int winH) {
    st.decoder.close();
    if (!st.decoder.open(path)) return false;

    st.videoW = st.decoder.getWidth();
    st.videoH = st.decoder.getHeight();

    bool usingCover = false;
    unsigned char* logoData = nullptr;
    int logoW = 0, logoH = 0;

    if (!st.decoder.hasVideo()) {
        if (st.decoder.hasCover()) {
            st.videoW = st.decoder.getCoverWidth();
            st.videoH = st.decoder.getCoverHeight();
            usingCover = true;
            OX_LOG("[AV] Using embedded cover art %dx%d\n", st.videoW, st.videoH);
        } else {
            logoData = stbi_load("e:/MX/Projects/MPlayer/wp.png", &logoW, &logoH, nullptr, 4);
            if (logoData) {
                st.videoW = logoW;
                st.videoH = logoH;
                OX_LOG("[AV] Using fallback LOGO.jpg %dx%d\n", logoW, logoH);
            }
        }
    }

    st.frameDuration = st.decoder.hasVideo() ? (1.0 / st.decoder.getFps()) : 0.0;

    if (st.decoder.hasVideo()) {
        snprintf(st.infoText, sizeof(st.infoText), "%dx%d @ %.1ffps", st.videoW, st.videoH, st.decoder.getFps());
    } else if (usingCover) {
        snprintf(st.infoText, sizeof(st.infoText), "Audio only + Cover (%dx%d)", st.videoW, st.videoH);
    } else if (logoData) {
        snprintf(st.infoText, sizeof(st.infoText), "Audio only + Logo (%dx%d)", st.videoW, st.videoH);
    } else {
        snprintf(st.infoText, sizeof(st.infoText), "Audio only (%dHz/%dch)", st.decoder.getSampleRate(), st.decoder.getChannels());
    }

    int texW = (st.videoW > 0) ? st.videoW : 1;
    int texH = (st.videoH > 0) ? st.videoH : 1;
    st.videoGLTex.destroy();
    st.videoGLTex.create(texW, texH, 4);

    if (usingCover) {
        uploadVideoFrame(st, st.decoder.getCoverData(), st.videoW, st.videoH);
    } else if (logoData) {
        uploadVideoFrame(st, logoData, st.videoW, st.videoH);
        stbi_image_free(logoData);
    } else if (!st.decoder.hasVideo()) {
        std::vector<uint8_t> black(texW * texH * 4, 0);
        uploadVideoFrame(st, black.data(), texW, texH);
    }

    updateVideoTransform(st, winW, winH);
    loadLyrics(st);

    st.videoTime = 0.0;
    if (st.decoder.hasVideo()) {
        // hasFrame will be set by first frame decode after loadMediaFile returns
        st.hasFrame = false;
    } else if (usingCover || logoData) {
        // Cover art or logo was uploaded - treat as having a frame
        st.hasFrame = true;
    } else {
        st.hasFrame = false;
    }
    st.isPlaying = false;
    st.isPaused = false;
    OX_LOG("[Audio] audioQueue.clear() from loadMediaFile\n");
    st.audioQueue.clear();
    st.audioSamplesPlayed = 0;
    st.avLastFftTime = 0.0;
    if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }

    return true;
}

/*==================== updateRightInfo, formatTime ====================*/
static void updateRightInfo(AppState& st) {
    int total = (int)st.mediaFiles.size();
    std::string name = "None";
    if (st.currentFileIndex >= 0 && st.currentFileIndex < total) {
        const std::string& fullPath = st.mediaFiles[st.currentFileIndex];
        size_t pos = fullPath.find_last_of("\\/");
        name = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
    }
    if (st.decoder.hasVideo()) {
        snprintf(st.rightInfoText, sizeof(st.rightInfoText),
            "Files: %d/%d\nName: %s\nRes: %dx%d\nFPS: %.1f\nDuration: %.1fs",
            st.currentFileIndex + 1, total, name.c_str(), st.videoW, st.videoH,
            st.decoder.getFps(), st.decoder.getDuration());
    } else {
        snprintf(st.rightInfoText, sizeof(st.rightInfoText),
            "Files: %d/%d\nName: %s\nRes: Audio only\nFPS: -\nDuration: %.1fs",
            st.currentFileIndex + 1, total, name.c_str(), st.decoder.getDuration());
    }
    if (st.rightInfoLabelPtr) st.rightInfoLabelPtr->setText(st.rightInfoText);
}

static void formatTime(char* buf, size_t sz, double sec) {
    int m = (int)(sec / 60.0);
    int s = (int)sec % 60;
    snprintf(buf, sz, "%02d:%02d", m, s);
}

/*==================== 播放模式处理 ====================*/
static void handlePlayModeEOF(AppState& st) {
    switch (st.playMode) {
        case PlayMode::SingleLoop:
            OX_LOG("[SingleLoop] Restarting current file\n");
            st.audioQueue.clear();
            st.audioSamplesPlayed = 0;
            st.videoTime = 0.0;
            st.decoder.seek(0);
            if (st.decoder.hasVideo()) {
                double pts;
                const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                if (f) {
                    st.currentVideoPts = pts;
                    st.hasFrame = true;
                    uploadVideoFrame(st, f, st.videoW, st.videoH);
                }
            }
            st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_HALF_SEC);
            if (st.audioStarted) {
                ma_device_stop(&st.audioDevice);
                ma_result maRes = ma_device_start(&st.audioDevice);
                if (maRes != MA_SUCCESS) { OX_LOG("[Audio] restart failed: %d\n", maRes); }
            }
            break;

        case PlayMode::ListLoop:
            if (!st.mediaFiles.empty()) {
                int nextIndex = st.currentFileIndex + 1;
                if (nextIndex >= (int)st.mediaFiles.size()) nextIndex = 0;
                uint32_t w, h; st.app.getSize(&w, &h);
                if (loadMediaFile(st, st.mediaFiles[nextIndex].c_str(), (int)w, (int)h)) {
                    st.currentFileIndex = nextIndex;
                    if (st.decoder.hasVideo()) {
                        double pts;
                        const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                        if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                    }
                    st.isPlaying = true; st.isPaused = false;
                    snprintf(st.statusText, sizeof(st.statusText), "Playing");
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                    if (!st.audioStarted) {
                        if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                    }
                    st.needRebuildUI = true;
                }
            } else {
                st.isPlaying = false;
                st.isPaused = false;
                snprintf(st.statusText, sizeof(st.statusText), "Stopped");
                if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
                if (st.playBtnPtr) st.playBtnPtr->setText("Play");
                if (st.statusLabelPtr) st.statusLabelPtr->setText(st.statusText);
            }
            break;

        case PlayMode::Random:
            if (!st.mediaFiles.empty()) {
                int nextIndex;
                if (st.mediaFiles.size() > 1) {
                    do { nextIndex = rand() % (int)st.mediaFiles.size(); } while (nextIndex == st.currentFileIndex);
                } else {
                    nextIndex = 0;
                }
                uint32_t w, h; st.app.getSize(&w, &h);
                if (loadMediaFile(st, st.mediaFiles[nextIndex].c_str(), (int)w, (int)h)) {
                    st.currentFileIndex = nextIndex;
                    if (st.decoder.hasVideo()) {
                        double pts;
                        const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                        if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                    }
                    st.isPlaying = true; st.isPaused = false;
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                    if (!st.audioStarted) {
                        if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                    }
                    st.needRebuildUI = true;
                }
            }
            break;
    }
}

/*==================== 文件切换辅助 ====================*/
static void switchFile(AppState& st, int delta) {
    if (st.mediaFiles.empty()) return;
    if (st.isPlaying) {
        st.isPlaying = false; st.isPaused = true;
        if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
    }
    st.currentFileIndex += delta;
    if (st.currentFileIndex < 0) st.currentFileIndex = (int)st.mediaFiles.size() - 1;
    if (st.currentFileIndex >= (int)st.mediaFiles.size()) st.currentFileIndex = 0;
    uint32_t w, h; st.app.getSize(&w, &h);
    if (loadMediaFile(st, st.mediaFiles[st.currentFileIndex].c_str(), (int)w, (int)h)) {
        if (st.decoder.hasVideo()) {
            double pts;
            const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
            if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
        }
        st.isPlaying = true; st.isPaused = false;
        snprintf(st.statusText, sizeof(st.statusText), "Playing");
        st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
        if (!st.audioStarted) {
            if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
        }
    }
    st.needRebuildUI = true;
}

/*==================== 自检: 确保 MData/config.json 存在 ====================*/
static void ensureConfig() {
    namespace fs = std::filesystem;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path mdataDir = exeDir / "MData";
    fs::path configPath = mdataDir / "config.json";

    if (!fs::exists(mdataDir)) {
        OX_LOG("[Config] Creating MData directory: %s\n", mdataDir.string().c_str());
        std::error_code ec;
        if (!fs::create_directory(mdataDir, ec)) {
            OX_LOG("[Config] ERROR: Failed to create MData directory: %s\n", ec.message().c_str());
            return;
        }
    }

    if (!fs::exists(configPath)) {
        OX_LOG("[Config] Creating default config.json: %s\n", configPath.string().c_str());
        json cfg = R"({
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
        })"_json;

        std::ofstream ofs(configPath);
        if (ofs) {
            ofs << cfg.dump(4);
            OX_LOG("[Config] Default config.json created successfully\n");
        } else {
            OX_LOG("[Config] ERROR: Failed to write config.json\n");
        }
    } else {
        OX_LOG("[Config] config.json found: %s\n", configPath.string().c_str());
    }
}

/*==================== 歌单读写 ====================*/
static std::vector<std::string> splitDirs(const std::string& raw) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t end = raw.find(';', start);
        if (end == std::string::npos) end = raw.size();
        std::string part = raw.substr(start, end - start);
        size_t s = 0, e = part.size();
        while (s < e && (part[s] == ' ' || part[s] == '\t' || part[s] == '"' || part[s] == '\'')) ++s;
        while (e > s && (part[e - 1] == ' ' || part[e - 1] == '\t' || part[e - 1] == '"' || part[e - 1] == '\'')) --e;
        if (s < e) result.push_back(normalizePath(part.substr(s, e - s)));
        start = end + 1;
    }
    return result;
}

static std::string getTodayDate() {
    time_t now = time(nullptr);
    tm t{};
    localtime_s(&t, &now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return buf;
}

static std::filesystem::path getConfigPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::filesystem::path(exePath).parent_path() / "MData" / "config.json";
}

static void loadPlaylists(AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        if (cfg.contains("playlists") && cfg["playlists"].is_array()) {
            st.playlists.clear();
            for (const auto& jpl : cfg["playlists"]) {
                PlaylistInfo pl;
                pl.name        = jpl.value("name", "");
                pl.createDate  = jpl.value("createDate", "");
                pl.type        = jpl.value("type", "builtin");
                pl.directoryPath = jpl.value("directoryPath", "");
                {
                    auto dirs = splitDirs(pl.directoryPath);
                    pl.directoryPath = "";
                    for (size_t i = 0; i < dirs.size(); ++i) {
                        if (i > 0) pl.directoryPath += ";";
                        pl.directoryPath += dirs[i];
                    }
                }
                pl.autoScan    = jpl.value("autoScan", true);
                pl.scanInterval = jpl.value("scanInterval", 3600);
                if (jpl.contains("songs") && jpl["songs"].is_array()) {
                    for (const auto& js : jpl["songs"]) {
                        SongInfo s;
                        s.title    = js.value("title", "");
                        s.artist   = js.value("artist", "");
                        s.album    = js.value("album", "");
                        s.filePath = normalizePath(js.value("filePath", ""));
                        s.duration = js.value("duration", 0.0f);
                        s.addDate  = js.value("addDate", "");
                        pl.songs.push_back(s);
                    }
                }
                st.playlists.push_back(pl);
            }
            OX_LOG("[Playlist] Loaded %zu playlists\n", st.playlists.size());
        }
    } catch (const std::exception& e) {
        OX_LOG("[Playlist] loadPlaylists error: %s\n", e.what());
    }
}

static void savePlaylists(const AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        ifs.close();

        cfg["playlists"] = json::array();
        for (const auto& pl : st.playlists) {
            json jpl;
            jpl["name"]         = pl.name;
            jpl["createDate"]   = pl.createDate;
            jpl["type"]         = pl.type;
            jpl["autoScan"]     = pl.autoScan;
            jpl["scanInterval"] = pl.scanInterval;
            jpl["directoryPath"] = pl.directoryPath;
            jpl["songs"]        = json::array();
            for (const auto& s : pl.songs) {
                json js;
                js["title"]    = s.title;
                js["artist"]   = s.artist;
                js["album"]    = s.album;
                js["filePath"] = s.filePath;
                js["duration"] = s.duration;
                js["addDate"]  = s.addDate;
                jpl["songs"].push_back(js);
            }
            cfg["playlists"].push_back(jpl);
        }

        std::ofstream ofs(configPath);
        ofs << cfg.dump(4);
        OX_LOG("[Playlist] Saved %zu playlists\n", st.playlists.size());
    } catch (const std::exception& e) {
        OX_LOG("[Playlist] savePlaylists error: %s\n", e.what());
    }
}

static void saveShowLyrics(const AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        ifs.close();

        cfg["ui"]["showLyrics"] = st.showLyrics;

        std::ofstream ofs(configPath);
        ofs << cfg.dump(4);
    } catch (const std::exception& e) {
        OX_LOG("[Config] saveShowLyrics error: %s\n", e.what());
    }
}

static void saveLastPlayed(const AppState& st) {
    auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);
        ifs.close();

        double pos = st.videoTime;
        cfg["lastPlayed"] = {
            {"playlistIndex", st.currentPlaylistIndex},
            {"songIndex",     st.currentFileIndex},
            {"position",      pos}
        };

        std::ofstream ofs(configPath);
        ofs << cfg.dump(4);
        OX_LOG("[LastPlayed] Saved: playlist=%d song=%d pos=%.1fs\n",
               st.currentPlaylistIndex, st.currentFileIndex, pos);
    } catch (const std::exception& e) {
        OX_LOG("[LastPlayed] save error: %s\n", e.what());
    }
}

/*==================== 读取并应用 config.json 配置 ====================*/
static void applyConfig(AppState& st) {
    namespace fs = std::filesystem;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path configPath = fs::path(exePath).parent_path() / "MData" / "config.json";

    if (!fs::exists(configPath)) {
        OX_LOG("[Config] applyConfig: config.json not found\n");
        return;
    }

    try {
        std::ifstream ifs(configPath);
        json cfg = json::parse(ifs);

        if (cfg.contains("player") && cfg["player"].contains("volume")) {
            int vol = cfg["player"]["volume"].get<int>();
            st.volume = (float)std::clamp(vol, 0, 100) / 100.0f;
            OX_LOG("[Config] volume set to %d -> %.2f\n", vol, st.volume);
        }

        if (cfg.contains("player") && cfg["player"].contains("playMode")) {
            std::string mode = cfg["player"]["playMode"].get<std::string>();
            if (mode == "singleLoop")      st.playMode = PlayMode::SingleLoop;
            else if (mode == "listLoop")   st.playMode = PlayMode::ListLoop;
            else if (mode == "random")     st.playMode = PlayMode::Random;
            else                           st.playMode = PlayMode::Sequential;
            OX_LOG("[Config] playMode set to %s -> %s\n", mode.c_str(), getPlayModeText(st.playMode));
        }

        if (cfg.contains("ui")) {
            if (cfg["ui"].contains("showLyrics")) {
                st.showLyrics = cfg["ui"]["showLyrics"].get<bool>();
                OX_LOG("[Config] showLyrics = %s\n", st.showLyrics ? "true" : "false");
            }
        }

        loadPlaylists(st);

        if (cfg.contains("lastPlayed")) {
            st.resumePlaylistIndex = cfg["lastPlayed"].value("playlistIndex", -1);
            st.resumeSongIndex     = cfg["lastPlayed"].value("songIndex", -1);
            st.resumePosition      = cfg["lastPlayed"].value("position", 0.0);
            OX_LOG("[LastPlayed] Read: playlist=%d song=%d pos=%.1fs\n",
                   st.resumePlaylistIndex, st.resumeSongIndex, st.resumePosition);
        }

        OX_LOG("[Config] applyConfig done\n");
    } catch (const std::exception& e) {
        OX_LOG("[Config] applyConfig parse error: %s\n", e.what());
    }
}

/*==================== 生成录制文件名 ====================*/
static std::string generateRecordFilename() {
    time_t now = time(nullptr);
    struct tm local;
    localtime_s(&local, &now);
    char buf[80];
    strftime(buf, sizeof(buf), "recording_%Y%m%d_%H%M%S.mp4", &local);
    return std::string(buf);
}

/*==================== MAIN ====================*/
int main() {
    ensureConfig();
    AllocConsole();
    SetConsoleOutputCP(65001);
    OX::Core::initConsole();
    freopen("NUL", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    timeBeginPeriod(1);

    AppState st;
    applyConfig(st);
    const char* videoPath = "e:/MX/Data/tvb1.mp4";

    // ---------- 1. Window ----------
    OX::WindowDesc desc;
    desc.title = L"momo2";
    desc.width = 1920;
    desc.height = 1080;
    desc.style = OX::WindowStyle::Borderless;
    desc.enableOpenGL = true;
    desc.stencilBits = 0;
    if (!st.app.create(&desc)) {
        OX_LOG("[MAIN] Window create failed\n");
        return 1;
    }
    uint32_t width, height;
    st.app.getSize(&width, &height);

    // ---------- 1.5 VSync off / Hotkey ----------
    st.app.registerHotKey(HOTKEY_TOGGLE, 0, HOTKEY_VK);

    std::function<void()> toggleWallpaperMode = [&st]() {
        if (st.app.isWallpaperMode()) {
            if (st.app.exitWallpaperMode()) {
                st.app.setTitle(L"momo2 - \xe7\xaa\x97\xe5\x8f\xa3\xe6\xa8\xa1\xe5\xbc\x8f");
            }
        } else {
            if (st.app.enterWallpaperMode()) {
                st.app.setTitle(L"momo2 - \xe5\xa3\x81\xe7\xba\xb8\xe6\xa8\xa1\xe5\xbc\x8f");
            }
        }
    };

    st.app.setHotKeyCallback([&](int id) {
        if (id == HOTKEY_TOGGLE) { toggleWallpaperMode(); }
    });

    // ---------- 2. Video Shader ----------
    const char* videoVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
uniform vec2 uScale;
void main() { gl_Position = vec4(aPos * uScale, 0.0, 1.0); vUV = aUV; }
)";
    const char* videoFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D videoTex;
void main() { fragColor = texture(videoTex, vec2(vUV.x, 1.0 - vUV.y)); }
)";
    st.videoShader.initFromSource(videoVS, videoFS);

    // ---------- 3. Lyrics Shader (for overlay quad) ----------
    const char* lyricsVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUV = aUV; }
)";
    const char* lyricsFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D lyricsTex;
void main() { fragColor = texture(lyricsTex, vUV); }
)";
    st.lyricsShader.initFromSource(lyricsVS, lyricsFS);

    // ---------- 4. VAO/VBO ----------
    glGenVertexArrays(1, &st.videoVAO);
    glGenBuffers(1, &st.videoVBO);
    glBindVertexArray(st.videoVAO);
    glBindBuffer(GL_ARRAY_BUFFER, st.videoVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ---------- 5. Aux GL context + ThorVG / OUI ----------
    // 保存主上下文句柄（用于 wglShareLists）
    st.mainGLRC = wglGetCurrentContext();
    if (!st.mainGLRC) {
        OX_LOG("[MAIN] Failed to get main GLRC\n");
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    if (!createAuxGLContext(st)) {
        OX_LOG("[MAIN] Aux GL context creation failed\n");
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        OX_LOG("[MAIN] ThorVG init failed\n");
        destroyAuxGLContext(st); cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    bool fontLoaded = OX::UIManager::loadFont("siyuan", kFontPath);
    const char* fontName = fontLoaded ? "siyuan" : "Arial";

    // Init OUI with FBO in aux context
    createOuiFbo(st, (int)width, (int)height);
    wglMakeCurrent(st.auxDC, st.auxGLRC);
    glBindFramebuffer(GL_FRAMEBUFFER, st.ouiFboId);
    bool ouiInitOk = st.ui.initFbo(st.auxGLRC, st.ouiFboId, (int)width, (int)height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    wglMakeCurrent(nullptr, nullptr);
    if (!ouiInitOk) {
        OX_LOG("[MAIN] OUI initFbo failed\n");
        tvg::Initializer::term(); destroyAuxGLContext(st); cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }

    // Create lyrics FBO
    createLrcFbo(st, (int)width, (int)height);

    // Restore main GL context (aux init code releases it)
    wglMakeCurrent(st.app.getDC(), st.mainGLRC);

    // ---------- 6. Scan media files ----------
    std::vector<std::string> scanDirs {kScanDirs[0], kScanDirs[1]};
    scanMediaFiles(st.mediaFiles, scanDirs);
    OX_LOG("[MAIN] Initial scan: %zu files from %s, %s\n", st.mediaFiles.size(), kScanDirs[0], kScanDirs[1]);
    if (std::filesystem::exists(kDefaultFlac)) {
        for (size_t i = 0; i < st.mediaFiles.size(); i++) {
            if (st.mediaFiles[i] == kDefaultFlac) {
                if (i > 0) { std::swap(st.mediaFiles[0], st.mediaFiles[i]); }
                break;
            }
        }
    }
    if (std::filesystem::exists(kDefaultMp4)) {
        for (size_t i = 0; i < st.mediaFiles.size(); i++) {
            if (st.mediaFiles[i] == kDefaultMp4 && i != 1) {
                std::swap(st.mediaFiles[1], st.mediaFiles[i]);
                break;
            }
        }
    }
    if (!st.mediaFiles.empty()) {
        st.currentFileIndex = 0;
        videoPath = st.mediaFiles[st.currentFileIndex].c_str();
    }

    // ---------- 7. Open video ----------
    if (!loadMediaFile(st, videoPath, (int)width, (int)height)) {
        OX_LOG("[MAIN] Failed to open video: %s\n", videoPath);
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }

    // ---------- 8. Decode first frame ----------
    if (st.decoder.hasVideo()) {
        double firstPts = 0.0;
        const uint8_t* firstFrame = st.decoder.getFirstFrame(firstPts, st.audioQueue);
        if (firstFrame) {
            st.currentVideoPts = firstPts;
            st.hasFrame = true;
            uploadVideoFrame(st, firstFrame, st.videoW, st.videoH);
        } else {
            OX_LOG("[MAIN] Failed to decode first frame\n");
        }
    }

    // ---------- 9. Init miniaudio ----------
    ma_device_config aConfig = ma_device_config_init(ma_device_type_playback);
    aConfig.playback.format   = ma_format_f32;
    aConfig.playback.channels = st.AUDIO_CHANNELS;
    aConfig.sampleRate        = st.AUDIO_SAMPLE_RATE;
    aConfig.dataCallback      = ma_data_callback;
    aConfig.pUserData         = &st;
    if (ma_device_init(nullptr, &aConfig, &st.audioDevice) != MA_SUCCESS) {
        OX_LOG("[MAIN] miniaudio device init failed\n");
        cleanup(st); st.app.destroy(); timeEndPeriod(1); return 1;
    }
    ma_device_set_master_volume(&st.audioDevice, st.volume);

    // FFT init
    {
        auto& cfg = st.avConfig;
        st.avFft = fft_init(cfg.fftWindowSize);
        st.avMonoBuf.resize(cfg.fftWindowSize, 0.0f);
        st.avSpectrum.resize(cfg.fftBins, 0.0f);
        st.avBarTargets.assign(cfg.barCount, 0.0f);
        st.avBarHeights.assign(cfg.barCount, 0.0f);
        st.avBarPeaks.assign(cfg.barCount, 0.0f);
        size_t ringCap = (size_t)cfg.fftWindowSize * 2 * 2;
        st.avRingBuf = std::make_unique<AudioVisRingBuffer>(ringCap);
    }

    // ================================================================
    // rebuildUI lambda
    // ================================================================
    float leftX = 20.0f, leftY = 20.0f, panelW = 280.0f;
    std::function<void()> rebuildUI;
    rebuildUI = [&]() {
        st.ui.clearElements();
        st.fpsLabelPtr = st.infoLabelPtr = st.timeLabelPtr = st.statusLabelPtr = st.syncLabelPtr = st.rightInfoLabelPtr = nullptr;
        st.playBtnPtr = st.loopBtnPtr = st.prevBtnPtr = st.nextBtnPtr = nullptr;
        st.seekBarPtr = st.volumeSliderPtr = nullptr;
        st.fileDropdownPtr = nullptr;
        st.playlistDropdownPtr = nullptr;
        st.fxBtnPtr = nullptr;
        st.fxDropdownPtr = nullptr;
        st.lyricsBtnPtr = nullptr;
        float curY = leftY;

        auto addL = [&](const char* t, float h, OX::OColor c, OX::UILabel** o = nullptr) {
            auto l = std::make_unique<OX::UILabel>(t);
            l->rect = OX::ORect(leftX, curY, panelW, h);
            l->fontSize = 14.0f; l->fontName = fontName; l->textColor = c;
            if (o) *o = l.get(); st.ui.addElement(std::move(l)); curY += h + 6.0f;
        };
        auto addB = [&](const char* t, std::function<void()> cb, float h = 32.0f, OX::UIButton** o = nullptr) {
            auto b = std::make_unique<OX::UIButton>(t);
            b->rect = OX::ORect(leftX, curY, panelW, h);
            b->fontName = fontName; b->fontSize = 11.0f; b->onClick = cb;
            if (o) *o = b.get(); st.ui.addElement(std::move(b)); curY += h + 4.0f;
        };

        addL("momo2--作者：元亨利贞", 28.0f, OX::OColor(100, 200, 255, 255));
        addL(st.infoText, 22.0f, OX::OColor(255, 255, 200, 255), &st.infoLabelPtr);
        addL(st.statusText, 22.0f, OX::OColor(255, 200, 100, 255), &st.statusLabelPtr);
        addL(st.timeText, 22.0f, OX::OColor(200, 255, 200, 255), &st.timeLabelPtr);
        addL(st.syncText, 22.0f, OX::OColor(200, 200, 255, 255), &st.syncLabelPtr);
        addL(st.frameText, 22.0f, OX::OColor(255, 255, 255, 255), &st.frameLabelPtr);

        auto playPauseCb = [&st, &rebuildUI]() {
            if (st.isPlaying) {
                st.isPlaying = false;
                st.isPaused = true;
                snprintf(st.statusText, sizeof(st.statusText), "Paused");
                if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
            } else {
                st.isPlaying = true;
                st.isPaused = false;
                snprintf(st.statusText, sizeof(st.statusText), "Playing");
                if (!st.hasFrame) {
                    st.decoder.seek(0);
                    st.audioQueue.clear();
                    st.audioSamplesPlayed = 0;
                    double pts;
                    const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                    if (f) {
                        st.currentVideoPts = pts;
                        st.hasFrame = true;
                        uploadVideoFrame(st, f, st.videoW, st.videoH);
                    }
                }
                st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                if (!st.audioStarted) {
                    ma_result maRes = ma_device_start(&st.audioDevice);
                    if (maRes != MA_SUCCESS) {
                        OX_LOG("[Audio] ma_device_start failed: %d\n", maRes);
                    } else {
                        st.audioStarted = true;
                    }
                }
            }
            if (st.playBtnPtr) st.playBtnPtr->setText(st.isPlaying ? "Pause" : "Play");
            if (st.statusLabelPtr) st.statusLabelPtr->setText(st.statusText);
        };

        // Play/Pause + Stop (dual column)
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            auto pauseBtn = std::make_unique<OX::UIButton>(st.isPlaying ? "Pause" : "Play");
            pauseBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            pauseBtn->fontName = fontName; pauseBtn->fontSize = 14.0f; pauseBtn->onClick = playPauseCb;
            st.playBtnPtr = pauseBtn.get(); st.ui.addElement(std::move(pauseBtn));
            auto stopBtn = std::make_unique<OX::UIButton>("Stop");
            stopBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            stopBtn->fontName = fontName; stopBtn->fontSize = 14.0f;
            stopBtn->onClick = [&st]() {
                st.isPlaying = false;
                st.isPaused = false;
                st.videoTime = 0.0;
                snprintf(st.statusText, sizeof(st.statusText), "Stopped");
                if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
                st.audioQueue.clear();
                st.audioSamplesPlayed = 0;
                st.avLastFftTime = 0.0;
                std::fill(st.avBarTargets.begin(), st.avBarTargets.end(), 0.0f);
                std::fill(st.avBarHeights.begin(), st.avBarHeights.end(), 0.0f);
                st.decoder.seek(0);
                double pts;
                const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                if (f) {
                    st.currentVideoPts = pts;
                    st.hasFrame = true;
                    uploadVideoFrame(st, f, st.videoW, st.videoH);
                }
                if (st.playBtnPtr) st.playBtnPtr->setText("Play");
                if (st.statusLabelPtr) st.statusLabelPtr->setText(st.statusText);
            };
            st.ui.addElement(std::move(stopBtn));
            curY += 32.0f + 4.0f;
        }

        // Prev/Next (dual column)
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            auto prevBtn = std::make_unique<OX::UIButton>("Prev");
            prevBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            prevBtn->fontName = fontName; prevBtn->fontSize = 14.0f;
            prevBtn->onClick = [&st]() {
                if (!st.mediaFiles.empty()) {
                    if (st.playMode == PlayMode::Random && st.mediaFiles.size() > 1) {
                        int next;
                        do { next = rand() % (int)st.mediaFiles.size(); } while (next == st.currentFileIndex);
                        st.pendingLoadFileIdx = next;
                    } else {
                        int next = st.currentFileIndex - 1;
                        if (next < 0) next = (int)st.mediaFiles.size() - 1;
                        st.pendingLoadFileIdx = next;
                    }
                }
            };
            st.prevBtnPtr = prevBtn.get(); st.ui.addElement(std::move(prevBtn));
            auto nextBtn = std::make_unique<OX::UIButton>("Next");
            nextBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            nextBtn->fontName = fontName; nextBtn->fontSize = 14.0f;
            nextBtn->onClick = [&st]() {
                if (!st.mediaFiles.empty()) {
                    if (st.playMode == PlayMode::Random && st.mediaFiles.size() > 1) {
                        int next;
                        do { next = rand() % (int)st.mediaFiles.size(); } while (next == st.currentFileIndex);
                        st.pendingLoadFileIdx = next;
                    } else {
                        int next = st.currentFileIndex + 1;
                        if (next >= (int)st.mediaFiles.size()) next = 0;
                        st.pendingLoadFileIdx = next;
                    }
                }
            };
            st.nextBtnPtr = nextBtn.get(); st.ui.addElement(std::move(nextBtn));
            curY += 32.0f + 4.0f;
        }

        // Screenshot buttons
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            auto winShotCb = [&st]() {
                uint32_t w, h;
                st.app.getSize(&w, &h);
                if (w == 0 || h == 0) return;
                std::vector<uint8_t> pixels(w * h * 4);
                glReadPixels(0, 0, (int)w, (int)h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                std::vector<uint8_t> flipped(w * h * 4);
                for (uint32_t y = 0; y < h; y++) {
                    memcpy(&flipped[y * w * 4], &pixels[(h - 1 - y) * w * 4], w * 4);
                }
                stbi_write_png("dump_window.png", (int)w, (int)h, 4, flipped.data(), (int)w * 4);
                OX_LOG("[Dump] Saved window screenshot: dump_window.png\n");
            };
            auto frameShotCb = [&st]() {
                if (!st.decoder.hasVideo() || !st.hasFrame) return;
                int frameNum = (int)(st.videoTime * st.decoder.getFps() + 0.5);
                char pngName[256];
                snprintf(pngName, sizeof(pngName), "dump_frame_%04d_%.3f.png", frameNum, st.videoTime);
                const uint8_t* framePtr = st.decoder.getCurrentFramePtr();
                if (framePtr) {
                    stbi_write_png(pngName, st.videoW, st.videoH, 4, framePtr, st.videoW * 4);
                    OX_LOG("[Dump] Saved frame screenshot: %s\n", pngName);
                }
                if (st.currentFileIndex >= 0 && st.currentFileIndex < (int)st.mediaFiles.size()) {
                    char ffmpegCmd[1024];
                    snprintf(ffmpegCmd, sizeof(ffmpegCmd),
                             "start /B \"\" \"%s\" -y -ss %.3f -i \"%s\" -vframes 1 -q:v 2 dump_ffmpeg_%04d_%.3f.png >nul 2>&1",
                             kFfmpegPath, st.videoTime, st.mediaFiles[st.currentFileIndex].c_str(), frameNum, st.videoTime);
                    std::thread([ffmpegCmd]() { system(ffmpegCmd); }).detach();
                    OX_LOG("[Dump] FFmpeg screenshot launched\n");
                }
            };
            auto winShotBtn = std::make_unique<OX::UIButton>("Window Shot");
            winShotBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            winShotBtn->fontName = fontName; winShotBtn->fontSize = 11.0f; winShotBtn->onClick = winShotCb;
            st.ui.addElement(std::move(winShotBtn));
            auto frameShotBtn = std::make_unique<OX::UIButton>("Frame Shot");
            frameShotBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            frameShotBtn->fontName = fontName; frameShotBtn->fontSize = 11.0f; frameShotBtn->onClick = frameShotCb;
            st.ui.addElement(std::move(frameShotBtn));
            curY += 32.0f + 4.0f;
        }

        // Recording button
        {
            auto recCb = [&st]() {
                if (!st.recording) {
                    uint32_t w, h;
                    st.app.getSize(&w, &h);
                    if (w == 0 || h == 0) return;
                    st.recFBW = (int)w; st.recFBH = (int)h;
                    st.recFrameBuf.resize((size_t)w * h * 3);
                    std::string filename = generateRecordFilename();
                    if (st.recorder.beginRecording(filename.c_str(), st.recFBW, st.recFBH, 30)) {
                        st.recording = true;
                        OX_LOG("[Rec] Started: %s (%dx%d)\n", filename.c_str(), w, h);
                    } else {
                        OX_LOG("[Rec] Failed to start recording\n");
                    }
                } else {
                    st.recorder.endRecording();
                    double dur = st.recorder.getDuration();
                    int64_t frames = st.recorder.getFrameCount();
                    OX_LOG("[Rec] Stopped: %.1fs, %lld frames\n", dur, frames);
                    st.recording = false;
                    st.recFrameBuf.clear();
                    if (st.recordBtnPtr) st.recordBtnPtr->setText("Start Recording");
                }
            };
            auto recBtn = std::make_unique<OX::UIButton>("Start Recording");
            recBtn->rect = OX::ORect(leftX, curY, panelW, 32.0f);
            recBtn->fontName = fontName; recBtn->fontSize = 11.0f; recBtn->onClick = recCb;
            st.recordBtnPtr = recBtn.get();
            st.ui.addElement(std::move(recBtn));
            curY += 32.0f + 4.0f;
        }

        // FX特效选择
        {
            auto fxLabel = std::make_unique<OX::UILabel>("FX特效");
            fxLabel->rect = OX::ORect(leftX, curY, panelW, 26.0f);
            fxLabel->fontSize = 14.0f; fxLabel->fontName = fontName;
            st.ui.addElement(std::move(fxLabel));
            curY += 26.0f + 2.0f;

            auto dd = std::make_unique<OX::UIDropdown>("选择特效...");
            dd->rect = OX::ORect(leftX, curY, 180.0f, 32.0f);
            dd->fontName = fontName; dd->fontSize = 12.0f;
            for (int i = 0; i < g_fxEffectCount; i++) {
                dd->options.push_back(g_fxEffects[i].name);
            }
            dd->selectedIndex = st.fxEffectIndex;
            dd->onSelectionChanged = [&st](int idx, const std::string&) {
                if (idx < 0 || idx >= g_fxEffectCount) return;
                st.fxEffectIndex = idx;
                st.fxTime = 0.0f;
                st.currentFxEffectInToy = -1; // force reload in main loop
                st.lrcFboDirty = true;
            };
            st.fxDropdownPtr = dd.get();
            st.ui.addElement(std::move(dd));
            curY += 32.0f + 4.0f;

            addB(st.fxEnabled ? "关闭FX" : "打开FX", [&st]() {
                st.fxEnabled = !st.fxEnabled;
                st.lrcFboDirty = true;
                if (st.fxBtnPtr) st.fxBtnPtr->setText(st.fxEnabled ? "关闭FX" : "打开FX");
            }, 32.0f, &st.fxBtnPtr);
        }

        // 显示/关闭背景
        addB(st.showBackground ? "关闭背景" : "显示背景", [&st]() {
            st.showBackground = !st.showBackground;
            if (st.bgBtnPtr) st.bgBtnPtr->setText(st.showBackground ? "关闭背景" : "显示背景");
        }, 32.0f, &st.bgBtnPtr);

        // 显示/关闭歌词
        addB(st.showLyrics ? "关闭歌词" : "显示歌词", [&st]() {
            st.showLyrics = !st.showLyrics;
            st.lrcFboDirty = true;
            saveShowLyrics(st);
            if (st.lyricsBtnPtr) st.lyricsBtnPtr->setText(st.showLyrics ? "关闭歌词" : "显示歌词");
        }, 32.0f, &st.lyricsBtnPtr);

        // 打开/关闭频谱
        addB(st.showAudioBars ? "关闭频谱" : "打开频谱", [&st]() {
            st.showAudioBars = !st.showAudioBars;
            st.avLastFftTime = 0.0;
            st.ouiFboDirty = true;
        }, 32.0f);

        addB(getPlayModeText(st.playMode), [&st]() {
            switch (st.playMode) {
                case PlayMode::SingleLoop: st.playMode = PlayMode::ListLoop; break;
                case PlayMode::ListLoop:   st.playMode = PlayMode::Sequential; break;
                case PlayMode::Sequential: st.playMode = PlayMode::Random; break;
                case PlayMode::Random:     st.playMode = PlayMode::SingleLoop; break;
            }
            if (st.loopBtnPtr) st.loopBtnPtr->setText(getPlayModeText(st.playMode));
        }, 32.0f, &st.loopBtnPtr);

        // Progress bar
        if (st.decoder.getDuration() > 0) {
            curY += 4.0f;
            auto seekBar = std::make_unique<OX::UISlider>();
            seekBar->rect = OX::ORect(leftX, curY, panelW, 24.0f);
            seekBar->minValue = 0.0f;
            seekBar->maxValue = (float)st.decoder.getDuration();
            seekBar->value = (float)st.videoTime;
            seekBar->onValueChanged = [&st](float val) {
                if (std::abs(val - (float)st.videoTime) > 1.0f) {
                    st.pendingSeek = true;
                    st.pendingSeekTarget = (double)val;
                }
            };
            st.seekBarPtr = seekBar.get();
            st.ui.addElement(std::move(seekBar));
            curY += 24.0f + 4.0f;
        }

        // Volume control
        {
            curY += 4.0f;
            auto volSlider = std::make_unique<OX::UISlider>();
            volSlider->rect = OX::ORect(leftX, curY, panelW, 24.0f);
            volSlider->minValue = 0.0f;
            volSlider->maxValue = 1.0f;
            volSlider->value = st.volume;
            volSlider->onValueChanged = [&st](float val) {
                st.volume = val;
                ma_device_set_master_volume(&st.audioDevice, val);
            };
            st.volumeSliderPtr = volSlider.get();
            st.ui.addElement(std::move(volSlider));
            curY += 24.0f + 4.0f;
        }

        addL(st.fpsText, 22.0f, OX::OColor(0, 255, 0, 255), &st.fpsLabelPtr);

        // 歌单切换
        if (!st.playlists.empty()) {
            curY += 8.0f;
            auto plDD = std::make_unique<OX::UIDropdown>("选择歌单...");
            plDD->rect = OX::ORect(leftX, curY, panelW, 36.0f);
            plDD->fontName = fontName; plDD->fontSize = 12.0f;
            for (const auto& pl : st.playlists) {
                plDD->options.push_back(pl.name);
            }
            plDD->selectedIndex = st.currentPlaylistIndex;
            plDD->onSelectionChanged = [&st](int idx, const std::string&) {
                if (idx < 0 || idx >= (int)st.playlists.size()) return;
                st.currentPlaylistIndex = idx;
                auto& pl = st.playlists[idx];
                st.mediaFiles.clear();
                st.currentFileIndex = -1;
                if (pl.type == "builtin") {
                    for (const auto& s : pl.songs)
                        st.mediaFiles.push_back(s.filePath);
                } else {
                    std::vector<std::string> dirs = splitDirs(pl.directoryPath);
                    scanMediaFiles(st.mediaFiles, dirs);
                }
                if (!st.mediaFiles.empty()) {
                    st.currentFileIndex = 0;
                    uint32_t w, h; st.app.getSize(&w, &h);
                    if (loadMediaFile(st, st.mediaFiles[0].c_str(), (int)w, (int)h)) {
                        if (st.decoder.hasVideo()) {
                            double pts;
                            const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                            if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                        }
                        st.isPlaying = true; st.isPaused = false;
                        snprintf(st.statusText, sizeof(st.statusText), "Playing");
                        st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                        if (!st.audioStarted) {
                            if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                        }
                    }
                }
                st.needRebuildUI = true;
            };
            st.playlistDropdownPtr = plDD.get();
            st.ui.addElement(std::move(plDD));
            curY += 36.0f + 4.0f;
        }

        // 新建歌单 / 添加歌曲
        {
            float btnGap = 4.0f;
            float halfW = (panelW - btnGap) / 2.0f;
            auto newPlBtn = std::make_unique<OX::UIButton>("+ 新建歌单");
            newPlBtn->rect = OX::ORect(leftX, curY, halfW, 32.0f);
            newPlBtn->fontName = fontName; newPlBtn->fontSize = 11.0f;
            newPlBtn->onClick = [&st]() {
                st.showCreatePlaylistDlg = true;
                st.plDlgName[0] = '\0';
                st.plDlgType = 0;
                st.plDlgDir[0] = '\0';
                st.plDlgAutoScan = true;
                st.plDlgScanInterval = 3600;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(newPlBtn));

            auto addSongsBtn = std::make_unique<OX::UIButton>("+ 添加歌曲");
            addSongsBtn->rect = OX::ORect(leftX + halfW + btnGap, curY, halfW, 32.0f);
            addSongsBtn->fontName = fontName; addSongsBtn->fontSize = 11.0f;
            addSongsBtn->onClick = [&st]() {
                if (st.currentPlaylistIndex < 0 || st.currentPlaylistIndex >= (int)st.playlists.size()) return;
                if (st.playlists[st.currentPlaylistIndex].type != "builtin") return;
                st.showAddSongsDlg = true;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(addSongsBtn));
            curY += 32.0f + 4.0f;
        }

        // File Dropdown
        if (!st.mediaFiles.empty()) {
            curY += 4.0f;
            auto dd = std::make_unique<OX::UIDropdown>("选择文件...");
            dd->rect = OX::ORect(leftX, curY, panelW, 36.0f);
            dd->fontName = fontName; dd->fontSize = 12.0f;
            for (const auto& fp : st.mediaFiles) {
                size_t pos = fp.find_last_of("\\/");
                dd->options.push_back((pos != std::string::npos) ? fp.substr(pos + 1) : fp);
            }
            dd->selectedIndex = st.currentFileIndex;
            dd->onSelectionChanged = [&st](int idx, const std::string&) {
                if (idx < 0 || idx >= (int)st.mediaFiles.size()) return;
                st.currentFileIndex = idx;
                st.pendingLoadFileIdx = idx;
                st.needRebuildUI = true;
            };
            st.fileDropdownPtr = dd.get();
            st.ui.addElement(std::move(dd));
        }

        // Right info panel
        updateRightInfo(st);
        auto infoR = std::make_unique<OX::UILabel>(st.rightInfoText);
        infoR->rect = OX::ORect((float)width - panelW - 20.0f, leftY, panelW, 240.0f);
        infoR->fontSize = 13.0f; infoR->fontName = fontName;
        infoR->textColor = OX::OColor(200, 220, 255, 255);
        infoR->setWrapMode(tvg::TextWrap::Word);
        st.rightInfoLabelPtr = infoR.get();
        st.ui.addElement(std::move(infoR));

        // Lyrics dirty flag
        if (st.showLyrics) { st.lrcFboDirty = true; }

        // Drag region
        auto dragRegion = std::make_unique<OX::UIDragRegion>(st.app.getHwnd());
        dragRegion->rect = OX::ORect(0, 0, static_cast<float>(desc.width), 48.0f);
        st.ui.addElement(std::move(dragRegion));

        // ================================================================
        // 创建歌单对话框
        // ================================================================
        if (st.showCreatePlaylistDlg) {
            float dlgW = 440.0f, dlgH = 320.0f;
            float dlgX = ((float)width - dlgW) / 2.0f;
            float dlgY = ((float)height - dlgH) / 2.0f;

            auto backdrop = std::make_unique<OX::UIFrame>();
            backdrop->rect = OX::ORect(0, 0, (float)width, (float)height);
            backdrop->bgColor = OX::OColor(0, 0, 0, 180);
            backdrop->borderWidth = 0;
            backdrop->enabled = false;
            st.ui.addElement(std::move(backdrop));

            auto card = std::make_unique<OX::UIFrame>();
            card->rect = OX::ORect(dlgX, dlgY, dlgW, dlgH);
            card->bgColor = OX::OColor(40, 42, 54, 255);
            card->borderColor = OX::OColor(80, 82, 100, 255);
            card->borderWidth = 1.0f;
            card->cornerRadius = 8.0f;
            card->enabled = false;
            st.ui.addElement(std::move(card));

            auto dlgTitle = std::make_unique<OX::UILabel>("创建歌单");
            dlgTitle->rect = OX::ORect(dlgX + 20.0f, dlgY + 16.0f, dlgW - 40.0f, 28.0f);
            dlgTitle->fontName = fontName; dlgTitle->fontSize = 18.0f;
            dlgTitle->textColor = OX::OColor(255, 255, 255, 255);
            st.ui.addElement(std::move(dlgTitle));

            float dlgCurY = dlgY + 56.0f;
            auto nameLabel = std::make_unique<OX::UILabel>("名称:");
            nameLabel->rect = OX::ORect(dlgX + 20.0f, dlgCurY, 60.0f, 24.0f);
            nameLabel->fontName = fontName; nameLabel->fontSize = 14.0f;
            nameLabel->textColor = OX::OColor(200, 200, 200, 255);
            st.ui.addElement(std::move(nameLabel));

            auto nameInput = std::make_unique<OX::UITextInput>();
            nameInput->rect = OX::ORect(dlgX + 80.0f, dlgCurY, dlgW - 100.0f, 30.0f);
            nameInput->fontName = fontName;
            nameInput->setText(st.plDlgName);
            nameInput->onTextChanged = [&st](const std::string& text) {
                strncpy_s(st.plDlgName, text.c_str(), sizeof(st.plDlgName) - 1);
            };
            st.ui.addElement(std::move(nameInput));
            dlgCurY += 40.0f;

            auto typeLabel = std::make_unique<OX::UILabel>("类型:");
            typeLabel->rect = OX::ORect(dlgX + 20.0f, dlgCurY, 60.0f, 24.0f);
            typeLabel->fontName = fontName; typeLabel->fontSize = 14.0f;
            typeLabel->textColor = OX::OColor(200, 200, 200, 255);
            st.ui.addElement(std::move(typeLabel));

            auto typeDD = std::make_unique<OX::UIDropdown>("");
            typeDD->rect = OX::ORect(dlgX + 80.0f, dlgCurY, dlgW - 100.0f, 30.0f);
            typeDD->fontName = fontName; typeDD->fontSize = 13.0f;
            typeDD->options = {"内置歌单", "本地目录"};
            typeDD->selectedIndex = st.plDlgType;
            typeDD->onSelectionChanged = [&st](int idx, const std::string&) {
                st.plDlgType = idx;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(typeDD));
            dlgCurY += 40.0f;

            if (st.plDlgType == 1) {
                auto dirLabel = std::make_unique<OX::UILabel>("目录:");
                dirLabel->rect = OX::ORect(dlgX + 20.0f, dlgCurY, 60.0f, 24.0f);
                dirLabel->fontName = fontName; dirLabel->fontSize = 14.0f;
                dirLabel->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(dirLabel));

                auto dirInput = std::make_unique<OX::UITextInput>();
                dirInput->rect = OX::ORect(dlgX + 80.0f, dlgCurY, dlgW - 100.0f, 30.0f);
                dirInput->fontName = fontName;
                dirInput->setText(st.plDlgDir);
                dirInput->onTextChanged = [&st](const std::string& text) {
                    strncpy_s(st.plDlgDir, text.c_str(), sizeof(st.plDlgDir) - 1);
                };
                st.ui.addElement(std::move(dirInput));
                dlgCurY += 44.0f;

                auto autoScanCB = std::make_unique<OX::UICheckbox>();
                autoScanCB->rect = OX::ORect(dlgX + 80.0f, dlgCurY, 24.0f, 24.0f);
                autoScanCB->setChecked(st.plDlgAutoScan);
                autoScanCB->onStateChanged = [&st](bool checked) {
                    st.plDlgAutoScan = checked;
                };
                st.ui.addElement(std::move(autoScanCB));

                auto autoScanLbl = std::make_unique<OX::UILabel>("自动扫描新文件");
                autoScanLbl->rect = OX::ORect(dlgX + 110.0f, dlgCurY, 160.0f, 24.0f);
                autoScanLbl->fontName = fontName; autoScanLbl->fontSize = 13.0f;
                autoScanLbl->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(autoScanLbl));
                dlgCurY += 34.0f;

                auto intervalLabel = std::make_unique<OX::UILabel>("扫描间隔(秒):");
                intervalLabel->rect = OX::ORect(dlgX + 80.0f, dlgCurY, 120.0f, 24.0f);
                intervalLabel->fontName = fontName; intervalLabel->fontSize = 13.0f;
                intervalLabel->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(intervalLabel));

                auto intervalSlider = std::make_unique<OX::UISlider>();
                intervalSlider->rect = OX::ORect(dlgX + 80.0f, dlgCurY + 22.0f, dlgW - 100.0f, 20.0f);
                intervalSlider->minValue = 300.0f;
                intervalSlider->maxValue = 7200.0f;
                intervalSlider->value = (float)st.plDlgScanInterval;
                intervalSlider->step = 300.0f;
                intervalSlider->onValueChanged = [&st](float val) {
                    st.plDlgScanInterval = (int)val;
                };
                st.ui.addElement(std::move(intervalSlider));
                dlgCurY += 50.0f;
                dlgH = dlgCurY - dlgY + 60.0f;
            }

            float btnY = dlgY + dlgH - 48.0f;
            auto cancelBtn = std::make_unique<OX::UIButton>("取消");
            cancelBtn->rect = OX::ORect(dlgX + dlgW - 200.0f, btnY, 80.0f, 32.0f);
            cancelBtn->fontName = fontName; cancelBtn->fontSize = 13.0f;
            cancelBtn->onClick = [&st]() {
                st.showCreatePlaylistDlg = false;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(cancelBtn));

            auto createBtn = std::make_unique<OX::UIButton>("创建");
            createBtn->rect = OX::ORect(dlgX + dlgW - 110.0f, btnY, 80.0f, 32.0f);
            createBtn->fontName = fontName; createBtn->fontSize = 13.0f;
            createBtn->bgColor = OX::OColor(100, 180, 100, 255);
            createBtn->onClick = [&st]() {
                if (strlen(st.plDlgName) == 0) {
                    OX_LOG("[Playlist] Create failed: name is empty\n");
                    return;
                }
                PlaylistInfo pl;
                pl.name = st.plDlgName;
                pl.createDate = getTodayDate();
                pl.type = (st.plDlgType == 1) ? "localDir" : "builtin";
                if (pl.type == "localDir") {
                    auto dirs = splitDirs(st.plDlgDir);
                    pl.directoryPath = "";
                    for (size_t i = 0; i < dirs.size(); ++i) {
                        if (i > 0) pl.directoryPath += ";";
                        pl.directoryPath += dirs[i];
                    }
                    pl.autoScan = st.plDlgAutoScan;
                    pl.scanInterval = st.plDlgScanInterval;
                } else {
                    pl.directoryPath = "";
                    pl.autoScan = false;
                    pl.scanInterval = 0;
                }
                pl.songs.clear();
                st.playlists.push_back(pl);
                savePlaylists(st);
                st.currentPlaylistIndex = (int)st.playlists.size() - 1;
                st.mediaFiles.clear();
                st.currentFileIndex = -1;
                if (pl.type == "localDir") {
                    std::vector<std::string> dirs = splitDirs(pl.directoryPath);
                    scanMediaFiles(st.mediaFiles, dirs);
                }
                st.showCreatePlaylistDlg = false;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(createBtn));
        }

        // ================================================================
        // 添加歌曲对话框
        // ================================================================
        if (st.showAddSongsDlg && st.currentPlaylistIndex >= 0 && st.currentPlaylistIndex < (int)st.playlists.size()) {
            float dlgW = 460.0f, dlgH = 240.0f;
            float dlgX = ((float)width - dlgW) / 2.0f;
            float dlgY = ((float)height - dlgH) / 2.0f;

            auto addBackdrop = std::make_unique<OX::UIFrame>();
            addBackdrop->rect = OX::ORect(0, 0, (float)width, (float)height);
            addBackdrop->bgColor = OX::OColor(0, 0, 0, 180);
            addBackdrop->borderWidth = 0;
            addBackdrop->enabled = false;
            st.ui.addElement(std::move(addBackdrop));

            auto addCard = std::make_unique<OX::UIFrame>();
            addCard->rect = OX::ORect(dlgX, dlgY, dlgW, dlgH);
            addCard->bgColor = OX::OColor(40, 42, 54, 255);
            addCard->borderColor = OX::OColor(80, 82, 100, 255);
            addCard->borderWidth = 1.0f;
            addCard->cornerRadius = 8.0f;
            addCard->enabled = false;
            st.ui.addElement(std::move(addCard));

            char addTitle[128];
            snprintf(addTitle, sizeof(addTitle), "添加歌曲到 %s", st.playlists[st.currentPlaylistIndex].name.c_str());
            auto addTitleLbl = std::make_unique<OX::UILabel>(addTitle);
            addTitleLbl->rect = OX::ORect(dlgX + 20.0f, dlgY + 16.0f, dlgW - 40.0f, 28.0f);
            addTitleLbl->fontName = fontName; addTitleLbl->fontSize = 16.0f;
            addTitleLbl->textColor = OX::OColor(255, 255, 255, 255);
            st.ui.addElement(std::move(addTitleLbl));

            auto& pl = st.playlists[st.currentPlaylistIndex];
            std::vector<std::string> pool;
            scanMediaFiles(pool, std::vector<std::string>{kScanDirs[0], kScanDirs[1]});
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&pl](const std::string& fp) {
                for (const auto& s : pl.songs) {
                    if (s.filePath == fp) return true;
                }
                return false;
            }), pool.end());

            if (!pool.empty()) {
                st.addSongSelIdx = -1;
                auto addFileDD = std::make_unique<OX::UIDropdown>("选择要添加的文件...");
                addFileDD->rect = OX::ORect(dlgX + 20.0f, dlgY + 60.0f, dlgW - 40.0f, 36.0f);
                addFileDD->fontName = fontName; addFileDD->fontSize = 12.0f;
                addFileDD->options.clear();
                for (const auto& fp : pool) {
                    size_t pos = fp.find_last_of("\\/");
                    addFileDD->options.push_back((pos != std::string::npos) ? fp.substr(pos + 1) : fp);
                }
                addFileDD->selectedIndex = (st.addSongSelIdx >= 0 && st.addSongSelIdx < (int)pool.size()) ? st.addSongSelIdx : 0;
                addFileDD->onSelectionChanged = [&st](int idx, const std::string&) {
                    st.addSongSelIdx = idx;
                };
                st.ui.addElement(std::move(addFileDD));

                auto addBtn = std::make_unique<OX::UIButton>("添加");
                addBtn->rect = OX::ORect(dlgX + dlgW - 210.0f, dlgY + dlgH - 48.0f, 80.0f, 32.0f);
                addBtn->fontName = fontName; addBtn->fontSize = 13.0f;
                addBtn->bgColor = OX::OColor(100, 180, 100, 255);
                addBtn->onClick = [&st, pool]() {
                    if (st.addSongSelIdx < 0 || st.addSongSelIdx >= (int)pool.size()) return;
                    const std::string& fp = pool[st.addSongSelIdx];
                    auto& songs = st.playlists[st.currentPlaylistIndex].songs;
                    for (const auto& s : songs) {
                        if (s.filePath == fp) return;
                    }
                    SongInfo si;
                    size_t pos = fp.find_last_of("\\/");
                    si.title = (pos != std::string::npos) ? fp.substr(pos + 1) : fp;
                    si.filePath = normalizePath(fp);
                    si.addDate = getTodayDate();
                    songs.push_back(si);
                    st.mediaFiles.push_back(fp);
                    if (st.currentFileIndex < 0) st.currentFileIndex = 0;
                    savePlaylists(st);
                    st.showAddSongsDlg = false;
                    st.needRebuildUI = true;
                };
                st.ui.addElement(std::move(addBtn));
            } else {
                auto noFileLbl = std::make_unique<OX::UILabel>("没有可添加的新文件");
                noFileLbl->rect = OX::ORect(dlgX + 20.0f, dlgY + 60.0f, dlgW - 40.0f, 24.0f);
                noFileLbl->fontName = fontName; noFileLbl->fontSize = 14.0f;
                noFileLbl->textColor = OX::OColor(200, 200, 200, 255);
                st.ui.addElement(std::move(noFileLbl));
            }

            auto closeBtn = std::make_unique<OX::UIButton>("关闭");
            closeBtn->rect = OX::ORect(dlgX + dlgW - 110.0f, dlgY + dlgH - 48.0f, 80.0f, 32.0f);
            closeBtn->fontName = fontName; closeBtn->fontSize = 13.0f;
            closeBtn->onClick = [&st]() {
                st.showAddSongsDlg = false;
                st.needRebuildUI = true;
            };
            st.ui.addElement(std::move(closeBtn));
        }
    };
    rebuildUI();

    // ---------- 10. Size callback ----------
    st.app.setSizeCallback([&](uint32_t w, uint32_t h) {
        width = w; height = h;
        updateVideoTransform(st, (int)w, (int)h);
        createLrcFbo(st, (int)w, (int)h);
        createOuiFbo(st, (int)w, (int)h);
        wglMakeCurrent(st.auxDC, st.auxGLRC);
        glBindFramebuffer(GL_FRAMEBUFFER, st.ouiFboId);
        if (st.ui.initFbo(st.auxGLRC, st.ouiFboId, (int)w, (int)h)) st.needRebuildUI = true;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        wglMakeCurrent(nullptr, nullptr);
        ensureSceneFbo(st, (int)w, (int)h);
        st.lrcFboDirty = true;
        st.ouiFboDirty = true;
    });

    // ---------- 11. Input callbacks ----------
    st.app.setMouseWheelCallback([&](int32_t x, int32_t y, int d) { st.ui.handleMWheel((int)x, (int)y, d); });
    st.app.setMouseMoveCallback([&](int32_t x, int32_t y) {
        st.ui.handleMMove(x, y);
        st.fxMouseX = (float)x;
        uint32_t winH = 0; st.app.getSize(nullptr, &winH);
        st.fxMouseY = (float)(winH - 1 - y);
    });
    st.app.setMouseButtonCallback([&](int32_t x, int32_t y, int32_t btn, bool pressed) {
        if (btn == 0) {
            if (pressed) {
                st.ui.handleMDown(x, y);
                st.fxMouseClicked = true;
            } else {
                st.ui.handleMUp(x, y);
                st.fxMouseClicked = false;
            }
        }
    });
    st.app.setKeyDownCallback([&](int keyCode) {
        if (keyCode == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (OpenClipboard(nullptr)) {
                HANDLE h = GetClipboardData(CF_UNICODETEXT);
                if (h) {
                    wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(h));
                    if (wstr) {
                        for (int i = 0; wstr[i]; ++i) {
                            if (wstr[i] != L'\r' && wstr[i] != L'\n')
                                st.ui.handleChar(wstr[i]);
                        }
                        GlobalUnlock(h);
                    }
                }
                CloseClipboard();
            }
        } else {
            st.ui.handleKDown(keyCode);
        }
    });
    st.app.setKeyUpCallback([&](int keyCode) { st.ui.handleKUp(keyCode); });
    st.app.setCharCallback([&](wchar_t ch) { st.ui.handleChar(ch); });

    // ---------- 12. Auto-start playback ----------
    st.lastFrameTime = std::chrono::steady_clock::now();
    int frameCount = 0; float fpsTimer = 0.0f;

    st.isPlaying = true;
    st.isPaused = false;
    snprintf(st.statusText, sizeof(st.statusText), "Playing");
    if (st.decoder.hasAudio()) {
        st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
    }
    if (!st.audioStarted) {
        ma_result maRes = ma_device_start(&st.audioDevice);
        if (maRes != MA_SUCCESS) {
            OX_LOG("[Audio] ma_device_start init failed: %d\n", maRes);
        } else {
            st.audioStarted = true;
        }
    }

    // ---------- 13. Resume last played ----------
    if (st.resumePlaylistIndex >= 0 && st.resumePlaylistIndex < (int)st.playlists.size() && st.resumePosition >= 0.0) {
        OX_LOG("[LastPlayed] Resuming: playlist=%d song=%d pos=%.1fs\n",
               st.resumePlaylistIndex, st.resumeSongIndex, st.resumePosition);

        st.currentPlaylistIndex = st.resumePlaylistIndex;
        auto& pl = st.playlists[st.resumePlaylistIndex];

        st.mediaFiles.clear();
        st.currentFileIndex = -1;
        if (pl.type == "builtin") {
            for (const auto& s : pl.songs)
                st.mediaFiles.push_back(s.filePath);
        } else {
            std::vector<std::string> dirs = splitDirs(pl.directoryPath);
            scanMediaFiles(st.mediaFiles, dirs);
        }

        if (!st.mediaFiles.empty()) {
            if (st.resumeSongIndex >= 0 && st.resumeSongIndex < (int)st.mediaFiles.size())
                st.currentFileIndex = st.resumeSongIndex;
            else
                st.currentFileIndex = 0;

            if (st.audioStarted) {
                ma_device_stop(&st.audioDevice);
                st.audioStarted = false;
            }
            st.decoder.close();

            uint32_t w2, h2; st.app.getSize(&w2, &h2);
            if (loadMediaFile(st, st.mediaFiles[st.currentFileIndex].c_str(), (int)w2, (int)h2)) {
                if (st.decoder.hasVideo()) {
                    double firstPts2;
                    const uint8_t* f2 = st.decoder.getFirstFrame(firstPts2, st.audioQueue);
                    if (f2) { st.currentVideoPts = firstPts2; st.hasFrame = true; uploadVideoFrame(st, f2, st.videoW, st.videoH); }
                }
                st.isPlaying = true; st.isPaused = false;
                snprintf(st.statusText, sizeof(st.statusText), "Playing");
                st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);

                if (ma_device_start(&st.audioDevice) == MA_SUCCESS) {
                    st.audioStarted = true;
                    if (st.resumePosition > 0.5 && st.decoder.getDuration() > 0) {
                        st.pendingSeek = true;
                        st.pendingSeekTarget = st.resumePosition;
                    }
                }
            }
        }
        st.needRebuildUI = true;
    }

    // ============================================================
    // Main Loop
    // ============================================================
    while (!st.app.shouldClose()) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - st.lastFrameTime).count();
        st.lastFrameTime = now;
        st.app.pollEvents();

        // Pending seek
        if (st.pendingSeek) {
            st.pendingSeek = false;
            double seekTarget = st.pendingSeekTarget;
            if (st.audioStarted) { ma_device_stop(&st.audioDevice); st.audioStarted = false; }
            st.audioQueue.clear();
            st.audioSamplesPlayed = 0;
            st.avLastFftTime = 0.0;
            bool ok = st.decoder.seekToTarget(st.audioQueue, seekTarget);
            if (ok) {
                double actualPts = st.decoder.getSeekedPts();
                st.videoTime = actualPts;
                if (st.decoder.hasVideo() && st.decoder.hasSeekedFrame()) {
                    const uint8_t* f = st.decoder.getSeekedFramePtr();
                    if (f) { st.currentVideoPts = actualPts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                }
                st.audioSamplesPlayed.store((uint64_t)(actualPts * st.AUDIO_SAMPLE_RATE));
            } else {
                st.audioSamplesPlayed.store((uint64_t)(seekTarget * st.AUDIO_SAMPLE_RATE));
                st.videoTime = seekTarget;
            }
            ma_result maRes = ma_device_start(&st.audioDevice);
            st.audioStarted = (maRes == MA_SUCCESS);
        }

        // FPS
        if (!st.needRebuildUI) {
            frameCount++; fpsTimer += dt;
            if (fpsTimer >= 0.5f) {
                st.fps = frameCount / fpsTimer;
                snprintf(st.fpsText, sizeof(st.fpsText), "FPS: %.1f", st.fps);
                if (st.fpsLabelPtr) st.fpsLabelPtr->setText(st.fpsText);
                frameCount = 0; fpsTimer = 0.0f;
            }
        }

        // Update time & sync text
        double audioClock = st.audioSamplesPlayed.load() / (double)st.AUDIO_SAMPLE_RATE;
        st.videoTime = audioClock;
        updateLyrics(st);
        if (!st.needRebuildUI) {
            if (st.timeLabelPtr) {
                char cur[16], dur[16];
                formatTime(cur, sizeof(cur), st.videoTime);
                formatTime(dur, sizeof(dur), st.decoder.getDuration());
                snprintf(st.timeText, sizeof(st.timeText), "%s / %s", cur, dur);
                st.timeLabelPtr->setText(st.timeText);
            }
            if (st.seekBarPtr && !st.seekBarDragging && st.decoder.getDuration() > 0) {
                st.seekBarPtr->setValue((float)st.videoTime);
            }
            if (st.syncLabelPtr) {
                double diff = st.currentVideoPts - audioClock;
                snprintf(st.syncText, sizeof(st.syncText), "Sync: %+.3fs", diff);
                st.syncLabelPtr->setText(st.syncText);
            }
            if (st.frameLabelPtr) {
                if (st.decoder.hasVideo()) {
                    int frmNum = (int)(st.videoTime * st.decoder.getFps() + 0.5);
                    snprintf(st.frameText, sizeof(st.frameText), "Time: %.2fs Frame: %d", st.videoTime, frmNum);
                } else {
                    snprintf(st.frameText, sizeof(st.frameText), "Time: %.2fs", st.videoTime);
                }
                st.frameLabelPtr->setText(st.frameText);
            }
        }

        // Playback: keep audio queue fed
        if (st.isPlaying) {
            if (st.decoder.hasAudio()) {
                if (st.audioQueue.size() < (size_t)st.AUDIO_BUF_FIFTH_SEC) {
                    st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_HALF_SEC);
                }
            }
            // EOF handling (includes cover art / audio-only files)
            if (st.decoder.hasAudio() && st.decoder.isEof() &&
                st.audioQueue.size() < (size_t)st.AUDIO_BUF_EOF_THRESH) {
                handlePlayModeEOF(st);
            }
            // Video decode
            if (st.hasFrame && st.decoder.hasVideo()) {
                if (st.currentVideoPts <= audioClock + st.VIDEO_SYNC_THRESHOLD_SEC) {
                    double nextPts = 0.0;
                    const uint8_t* frameData = st.decoder.decodeNextFrame(nextPts, st.audioQueue);
                    if (frameData) {
                        st.currentVideoPts = nextPts;
                        uploadVideoFrame(st, frameData, st.videoW, st.videoH);
                    } else {
                        handlePlayModeEOF(st);
                    }
                }
            }
        }

        // FX ShaderToy: reload when effect changed
        if (st.fxEnabled && st.fxEffectIndex != st.currentFxEffectInToy) {
            st.currentFxEffectInToy = st.fxEffectIndex;
            st.fxShaderToyCreated = false;
            st.fxShaderToy.destroy();
            if (st.fxEffectIndex >= 0 && st.fxEffectIndex < g_fxEffectCount) {
                bool ok = st.fxShaderToy.create(g_fxEffects[st.fxEffectIndex].code);
                if (ok) {
                    st.fxShaderToyCreated = true;
                }
            }
            // Set lyricsTex for HeartfeltRain shader
            if (st.fxShaderToyCreated && st.fxEffectIndex == 2 && st.lyricsGLTex.isValid()) {
                GLuint progId = st.fxShaderToy.getProgramId();
                if (progId) {
                    glUseProgram(progId);
                    glUniform1i(glGetUniformLocation(progId, "lyricsTex"), 1);
                    glUseProgram(0);
                }
            }
        }

        // Update FX time + mouse
        if (st.fxEnabled && st.fxShaderToyCreated) {
            st.fxTime += dt;
            st.fxShaderToy.setMouse(st.fxMouseX, st.fxMouseY, st.fxMouseClicked);
        }

        // FFT analysis
        if (st.showAudioBars) {
            double audioTimeForFft = (double)st.audioSamplesPlayed / (double)st.AUDIO_SAMPLE_RATE;
            if (audioTimeForFft - st.avLastFftTime >= (double)st.avConfig.updateIntervalMs * 0.001) {
                analyzeSpectrum(st, audioTimeForFft);
                st.avLastFftTime = audioTimeForFft;
            }
        }

        // Pending file load
        if (st.pendingLoadFileIdx >= 0 && st.pendingLoadFileIdx < (int)st.mediaFiles.size()) {
            int idx = st.pendingLoadFileIdx;
            st.pendingLoadFileIdx = -1;
            st.currentFileIndex = idx;
            uint32_t pw, ph; st.app.getSize(&pw, &ph);
            if (loadMediaFile(st, st.mediaFiles[idx].c_str(), (int)pw, (int)ph)) {
                if (st.decoder.hasVideo()) {
                    double pts;
                    const uint8_t* f = st.decoder.getFirstFrame(pts, st.audioQueue);
                    if (f) { st.currentVideoPts = pts; st.hasFrame = true; uploadVideoFrame(st, f, st.videoW, st.videoH); }
                }
                st.isPlaying = true; st.isPaused = false;
                snprintf(st.statusText, sizeof(st.statusText), "Playing");
                st.decoder.prefillAudioQueue(st.audioQueue, st.AUDIO_BUF_FIFTH_SEC);
                if (!st.audioStarted) {
                    if (ma_device_start(&st.audioDevice) == MA_SUCCESS) st.audioStarted = true;
                }
            } else {
                snprintf(st.statusText, sizeof(st.statusText), "Load failed");
            }
            st.needRebuildUI = true;
        }

        if (st.needRebuildUI) {
            st.needRebuildUI = false;
            rebuildUI();
        }
        st.ui.update(dt);

        // ========== RENDERING (aux context for ThorVG, CPU pixel transfer) ==========
        if (wglMakeCurrent(st.auxDC, st.auxGLRC)) {

        // --- Lyrics FBO ---
        if (st.lrcFboDirty) {
            if (st.showLyrics && !st.lrcLines.empty()) {
                float lrcW = 560.0f;
                float lrcX = ((float)width - lrcW) / 2.0f;
                float lrcY = 20.0f + 248.0f;
                float lrcH = 340.0f;
                renderLyricsToFbo(st, lrcX, lrcY, lrcW, lrcH, "siyuan");
            } else if (st.lrcFboId && st.lrcGlCanvas) {
                st.lrcGlCanvas->remove(nullptr);
                glBindFramebuffer(GL_FRAMEBUFFER, st.lrcFboId);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // Read lyrics FBO pixels to CPU buffer (for transfer to main context)
            if (st.lrcFboId && st.lrcFboW > 0 && st.lrcFboH > 0) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, st.lrcFboId);
                glReadPixels(0, 0, st.lrcFboW, st.lrcFboH, GL_RGBA, GL_UNSIGNED_BYTE, st.lrcPixelBuf.data());
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }
            st.lrcFboDirty = false;
        }

        // --- OUI FBO ---
        {
            glBindFramebuffer(GL_FRAMEBUFFER, st.ouiFboId);
            glViewport(0, 0, st.ouiFboW, st.ouiFboH);
            st.ui.render([&](tvg::Scene* scene) {
                if (st.showAudioBars && scene) {
                    renderAudioBarsToScene(scene, st, (float)width, (float)height);
                }
            });

            // Read OUI FBO pixels to CPU buffer (for transfer to main context)
            if (st.ouiFboId && st.ouiFboW > 0 && st.ouiFboH > 0) {
                glReadPixels(0, 0, st.ouiFboW, st.ouiFboH, GL_RGBA, GL_UNSIGNED_BYTE, st.ouiPixelBuf.data());
                st.ouiFboDirty = false;
            }
        }

        wglMakeCurrent(nullptr, nullptr);
        } // end aux context block

        // Restore main GL context
        if (st.mainGLRC) {
            wglMakeCurrent(st.app.getDC(), st.mainGLRC);
        }

        // Upload OUI/lyrics pixel buffers to main-context textures
        if (st.ouiFboW > 0 && st.ouiFboH > 0 && !st.ouiPixelBuf.empty()) {
            if (!st.ouiGLTex.isValid() || st.ouiFboW != st.ouiLastTexW || st.ouiFboH != st.ouiLastTexH) {
                st.ouiGLTex.destroy();
                st.ouiGLTex.create(st.ouiFboW, st.ouiFboH, 4);
                st.ouiLastTexW = st.ouiFboW; st.ouiLastTexH = st.ouiFboH;
            }
            glBindTexture(GL_TEXTURE_2D, st.ouiGLTex.getId());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, st.ouiFboW, st.ouiFboH, GL_RGBA, GL_UNSIGNED_BYTE, st.ouiPixelBuf.data());
        }
        if (st.lrcFboW > 0 && st.lrcFboH > 0 && !st.lrcPixelBuf.empty()) {
            if (!st.lyricsGLTex.isValid() || st.lrcFboW != st.lrcLastTexW || st.lrcFboH != st.lrcLastTexH) {
                st.lyricsGLTex.destroy();
                st.lyricsGLTex.create(st.lrcFboW, st.lrcFboH, 4);
                st.lrcLastTexW = st.lrcFboW; st.lrcLastTexH = st.lrcFboH;
            }
            glBindTexture(GL_TEXTURE_2D, st.lyricsGLTex.getId());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, st.lrcFboW, st.lrcFboH, GL_RGBA, GL_UNSIGNED_BYTE, st.lrcPixelBuf.data());
        }

        // ========== RENDER (main context) ==========
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        bool isHR = st.fxEnabled && st.fxShaderToyCreated && st.fxEffectIndex == 2;

        if (isHR) {
            // Render video + lyrics into sceneFbo first
            ensureSceneFbo(st, (int)width, (int)height);
            glBindFramebuffer(GL_FRAMEBUFFER, st.sceneFboId);
            glViewport(0, 0, (GLsizei)st.sceneFboW, (GLsizei)st.sceneFboH);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // Draw video quad into sceneFbo
            if (st.showBackground && st.hasFrame && st.videoGLTex.isValid()) {
                st.videoShader.use();
                st.videoShader.setVec2("uScale", st.videoScaleX, st.videoScaleY);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, st.videoGLTex.getId());
                st.videoShader.setInt("videoTex", 0);
                glBindVertexArray(st.videoVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }

            // NOTE: sceneFbo is video-only. HeartfeltRain shader samples
            // sceneTex and lyricsTex separately and composites them with rain distortion.

            // Render ShaderToy (HeartfeltRain) onto default FB
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, (GLsizei)width, (GLsizei)height);

            GLuint hrProgId = st.fxShaderToy.getProgramId();
            if (hrProgId) {
                glUseProgram(hrProgId);
                // Bind sceneTex (unit 0) and lyricsTex (unit 1)
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, st.sceneTexId);
                glUniform1i(glGetUniformLocation(hrProgId, "sceneTex"), 0);
                if (st.lyricsGLTex.isValid()) {
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, st.lyricsGLTex.getId());
                    glUniform1i(glGetUniformLocation(hrProgId, "lyricsTex"), 1);
                }
                // Pass uniform parameters expected by ShaderToy render()
                glUniform1f(glGetUniformLocation(hrProgId, "iTime"), st.fxTime);
                glUniform2f(glGetUniformLocation(hrProgId, "iResolution"), (float)width, (float)height);
                glUniform2f(glGetUniformLocation(hrProgId, "texSize"), (float)st.videoW, (float)st.videoH);
                float mxz = st.fxMouseClicked ? std::abs(st.fxMouseX) : -std::abs(st.fxMouseX);
                float myw = st.fxMouseClicked ? std::abs(st.fxMouseY) : -std::abs(st.fxMouseY);
                glUniform2f(glGetUniformLocation(hrProgId, "iMouseXY"), st.fxMouseX, st.fxMouseY);
                glUniform2f(glGetUniformLocation(hrProgId, "iMouseZW"), mxz, myw);
                glBindVertexArray(st.videoVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        } else if (st.fxEnabled && st.fxShaderToyCreated) {
            // Non-HeartfeltRain FX: video → lyrics → FX (blended)
            if (st.showBackground && st.hasFrame && st.videoGLTex.isValid()) {
                st.videoShader.use();
                st.videoShader.setVec2("uScale", st.videoScaleX, st.videoScaleY);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, st.videoGLTex.getId());
                st.videoShader.setInt("videoTex", 0);
                glBindVertexArray(st.videoVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            // Draw lyrics under FX
            if (st.showLyrics && st.lyricsGLTex.isValid()) {
                st.lyricsShader.use();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, st.lyricsGLTex.getId());
                st.lyricsShader.setInt("lyricsTex", 0);
                glBindVertexArray(st.videoVAO);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisable(GL_BLEND);
            }
            // ShaderToy renders on top of video+lyrics
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            st.fxShaderToy.render(st.fxTime, (uint32_t)width, (uint32_t)height);
            glDisable(GL_BLEND);
        } else {
            // No FX: video → lyrics
            if (st.showBackground && st.hasFrame && st.videoGLTex.isValid()) {
                st.videoShader.use();
                st.videoShader.setVec2("uScale", st.videoScaleX, st.videoScaleY);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, st.videoGLTex.getId());
                st.videoShader.setInt("videoTex", 0);
                glBindVertexArray(st.videoVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            if (st.showLyrics && st.lyricsGLTex.isValid()) {
                st.lyricsShader.use();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, st.lyricsGLTex.getId());
                st.lyricsShader.setInt("lyricsTex", 0);
                glBindVertexArray(st.videoVAO);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisable(GL_BLEND);
            }
        }

        // Draw OUI overlay (always on top)
        if (st.ouiGLTex.isValid()) {
            st.lyricsShader.use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, st.ouiGLTex.getId());
            st.lyricsShader.setInt("lyricsTex", 0);
            glBindVertexArray(st.videoVAO);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_BLEND);
        }

        // === Recording via glReadPixels ===
        if (st.recording && st.recFBW > 0 && st.recFBH > 0) {
            int rgbStride = st.recFBW * 3;
            int rgbaStride = st.recFBW * 4;
            std::vector<uint8_t> rgbaBuf((size_t)rgbaStride * st.recFBH);
            glReadPixels(0, 0, st.recFBW, st.recFBH, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBuf.data());
            // Strip alpha & vertical flip
            for (int y = 0; y < st.recFBH; y++) {
                const uint8_t* src = rgbaBuf.data() + y * rgbaStride;
                uint8_t* dst = st.recFrameBuf.data() + (st.recFBH - 1 - y) * rgbStride;
                for (int x = 0; x < st.recFBW; x++) {
                    dst[x * 3 + 0] = src[x * 4 + 0];
                    dst[x * 3 + 1] = src[x * 4 + 1];
                    dst[x * 3 + 2] = src[x * 4 + 2];
                }
            }
            st.recorder.writeFrame(st.recFrameBuf.data());
            if (st.recordBtnPtr) {
                double dur = st.recorder.getDuration();
                int64_t fc = st.recorder.getFrameCount();
                char buf[64];
                snprintf(buf, sizeof(buf), "Stop (%.1fs %lldf)", dur, fc);
                st.recordBtnPtr->setText(buf);
            }
        }

        st.app.swapBuffers();
    }

    // ========== Cleanup ==========
    if (st.recording) {
        st.recorder.endRecording();
        st.recording = false;
    }
    saveLastPlayed(st);
    st.ui.destroy();
    destroyLrcFbo(st);
    destroySceneFbo(st);
    destroyOuiFbo(st);
    tvg::Initializer::term();
    cleanup(st);
    st.app.unregisterHotKey(HOTKEY_TOGGLE);
    st.app.destroy();
    timeEndPeriod(1);
    return 0;
}
