/****************************************************************************
 * mate_core.h - 引擎通用能力（MScene 场景层共用）
 *
 * 只提供与 threepp 场景图无关的东西：日志桥、VFS 路径、PNG 纹理、
 * GLSL 程序、渲染目标、全屏三角。场景层（src/scenes/*）只依赖这里，
 * 不碰 AppState / threepp / 窗口。
 ****************************************************************************/
#pragma once

#include <glad/glad.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mate {

/*==================== 日志 ====================*/
// 引擎启动时把 mateLog 注入进来；场景层不直接依赖窗口内的日志面板
using LogSink = void (*)(const char* line);
void setLogSink(LogSink sink);
void logf(const char* fmt, ...);

/*==================== VFS ====================*/
// 部署目录约定：mate.exe 同级 /Data
std::string exeDir();
std::string dataDir();
std::string joinPath(const std::string& a, const std::string& b);
bool fileExists(const std::string& path);
bool readTextFile(const std::string& path, std::string& out);

/*==================== GLSL ====================*/
// 编译着色器；失败时把日志（带行号）写进日志并返回 0
GLuint compileShader(GLenum type, const char* source, const std::string& tag);

class Program {
public:
    bool build(const char* vsSource, const char* fsSource, const std::string& tag);
    void destroy();
    void use() const;

    GLuint id() const { return program_; }
    bool valid() const { return program_ != 0; }

    GLint loc(const char* name);

    void set1i(const char* name, int v);
    void set1f(const char* name, float v);
    void set2f(const char* name, float x, float y);
    void set4fv(const char* name, const float* data, int count);

private:
    GLuint program_ = 0;
    std::vector<std::pair<std::string, GLint>> cache_;
};

/*==================== 纹理 ====================*/
class Texture {
public:
    // 从磁盘加载 PNG。flipY=false 时第 0 行（图片顶部）落在 v=0，
    // 与 WebGL texImage2D 的默认行为一致，场景着色器可以直接用左上原点坐标
    bool loadPng(const std::string& path, bool flipY = false);
    // 建空纹理（RGBA32F 涟漪缓冲等）
    bool create(GLenum internalFormat, int w, int h, GLenum format, GLenum type,
                const void* data = nullptr);
    void uploadSub(int x, int y, int w, int h, GLenum format, GLenum type, const void* data);
    void destroy();

    GLuint id() const { return tex_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLuint tex_ = 0;
    int width_ = 0;
    int height_ = 0;
};

/*==================== 渲染目标 ====================*/
class Framebuffer {
public:
    bool create(int w, int h, GLenum internalFormat = GL_RGBA8, GLenum format = GL_RGBA,
                GLenum type = GL_UNSIGNED_BYTE);
    void resize(int w, int h);
    void destroy();

    void bind() const;
    static void bindScreen(int w, int h);

    GLuint texture() const { return color_; }
    GLuint fbo() const { return fbo_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return fbo_ != 0; }

private:
    bool createNoClear(int w, int h, GLenum internalFormat, GLenum format, GLenum type);

    GLuint fbo_ = 0;
    GLuint color_ = 0;
    int width_ = 0;
    int height_ = 0;
};

/*==================== 全屏三角 ====================*/
// 顶点：(-1,-1) (1,-1) (-1,1) (-1,1) (1,-1) (1,1)，对应 vUv 的左下 -> 右上
class FullscreenQuad {
public:
    bool create();
    void destroy();
    void draw() const;
    GLuint vao() const { return vao_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

/*==================== 场景接口 ====================*/
struct FrameContext {
    int   width  = 0;    // 帧缓冲像素
    int   height = 0;
    float dt     = 0.0f; // 暂停时由引擎传 0
    float time   = 0.0f; // 引擎累计时间
};

class IScene {
public:
    virtual ~IScene() = default;

    virtual bool load(const std::string& sceneDir) = 0;
    virtual void resize(int w, int h) = 0;
    virtual void update(const FrameContext& fc) = 0;
    virtual void render(const FrameContext& fc) = 0;
    virtual void drawUI() = 0;   // 运行在引擎的 ImGui 帧内

    // 观察式输入：nx/ny 是 0..1 的屏幕归一化坐标（左上原点）
    virtual void pointerDown(float nx, float ny) { (void)nx; (void)ny; }
    virtual void pointerMove(float nx, float ny) { (void)nx; (void)ny; }
};

// 场景工厂（编进 exe，不做 DLL）
IScene* createStillWaterScene();

} // namespace mate
