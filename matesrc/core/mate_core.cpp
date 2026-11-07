#define NOMINMAX
#include <windows.h>

#include "mate_core.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC          // 内部链接，避免和 threepp_mt.lib 里的 stb_image 重名
#define STBI_ONLY_PNG
#include <stb/stb_image.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>

namespace mate {

/*==================== 日志 ====================*/
static LogSink g_sink = nullptr;

void setLogSink(LogSink sink) { g_sink = sink; }

void logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_sink) g_sink(buf);
}

/*==================== VFS ====================*/
std::string exeDir() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
        std::wstring w = path;
        const size_t slash = w.find_last_of(L"\\/");
        if (slash != std::wstring::npos) w.resize(slash);
        // 用宽字符版本拼接后再转窄字符，避免中文路径乱码
        std::string out;
        char narrow[MAX_PATH * 2] = {};
        if (WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, narrow, (int)sizeof(narrow),
                                nullptr, nullptr) > 0) {
            out.assign(narrow);
        }
        return out;
    }
    return ".";
}

std::string dataDir() { return joinPath(exeDir(), "Data"); }

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

static std::wstring utf8ToWide(const std::string& s) {
    std::wstring out;
    if (s.empty()) return out;
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return out;
    out.resize((size_t)len);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

bool fileExists(const std::string& path) {
    const DWORD attr = GetFileAttributesW(utf8ToWide(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream in(utf8ToWide(path).c_str(), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

/*==================== GLSL ====================*/
static void dumpShaderError(GLuint shader, const std::string& tag) {
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) return;
    std::vector<char> log((size_t)len);
    glGetShaderInfoLog(shader, len, nullptr, log.data());
    // 逐行写日志，便于在窗口内日志面板里定位
    std::string text(log.data(), log.size());
    std::string line;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) { logf("[GLSL %s] %s", tag.c_str(), line.c_str()); line.clear(); }
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) logf("[GLSL %s] %s", tag.c_str(), line.c_str());
}

GLuint compileShader(GLenum type, const char* source, const std::string& tag) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        dumpShaderError(shader, tag + (type == GL_VERTEX_SHADER ? " vs" : " fs"));
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Program::build(const char* vsSource, const char* fsSource, const std::string& tag) {
    destroy();
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource, tag);
    if (!vs) return false;
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource, tag);
    if (!fs) { glDeleteShader(vs); return false; }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            std::vector<char> log((size_t)len);
            glGetProgramInfoLog(program_, len, nullptr, log.data());
            logf("[GLSL %s] link: %s", tag.c_str(), log.data());
        }
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    return true;
}

void Program::destroy() {
    if (program_) glDeleteProgram(program_);
    program_ = 0;
    cache_.clear();
}

void Program::use() const { glUseProgram(program_); }

GLint Program::loc(const char* name) {
    for (const auto& kv : cache_) {
        if (kv.first == name) return kv.second;
    }
    const GLint l = glGetUniformLocation(program_, name);
    cache_.emplace_back(name, l);
    return l;
}

void Program::set1i(const char* name, int v) { glUniform1i(loc(name), v); }
void Program::set1f(const char* name, float v) { glUniform1f(loc(name), v); }
void Program::set2f(const char* name, float x, float y) { glUniform2f(loc(name), x, y); }
void Program::set4fv(const char* name, const float* data, int count) {
    glUniform4fv(loc(name), count, data);
}

/*==================== 纹理 ====================*/
bool Texture::loadPng(const std::string& path, bool flipY) {
    destroy();
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) {
        logf("[纹理] 加载失败: %s（%s）", path.c_str(), stbi_failure_reason());
        return false;
    }

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);

    width_ = w;
    height_ = h;
    logf("[纹理] %s  %dx%d", path.c_str(), w, h);
    return true;
}

bool Texture::create(GLenum internalFormat, int w, int h, GLenum format, GLenum type,
                     const void* data) {
    destroy();
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFormat, w, h, 0, format, type, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = w;
    height_ = h;
    return tex_ != 0;
}

void Texture::uploadSub(int x, int y, int w, int h, GLenum format, GLenum type,
                        const void* data) {
    if (!tex_) return;
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, format, type, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::destroy() {
    if (tex_) glDeleteTextures(1, &tex_);
    tex_ = 0;
    width_ = height_ = 0;
}

/*==================== 渲染目标 ====================*/
bool Framebuffer::createNoClear(int w, int h, GLenum internalFormat, GLenum format, GLenum type) {
    width_ = w;
    height_ = h;

    glGenTextures(1, &color_);
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFormat, w, h, 0, format, type, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        logf("[FBO] 创建失败 0x%X（%dx%d）", (unsigned)status, w, h);
        destroy();
        return false;
    }
    return true;
}

bool Framebuffer::create(int w, int h, GLenum internalFormat, GLenum format, GLenum type) {
    destroy();
    return createNoClear(w, h, internalFormat, format, type);
}

void Framebuffer::resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (w == width_ && h == height_ && fbo_) return;
    destroy();
    createNoClear(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
}

void Framebuffer::bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo_); }

void Framebuffer::bindScreen(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
}

void Framebuffer::destroy() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_) glDeleteTextures(1, &color_);
    fbo_ = 0;
    color_ = 0;
    width_ = height_ = 0;
}

/*==================== 全屏三角 ====================*/
bool FullscreenQuad::create() {
    if (vao_) return true;
    static const float kVertices[] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,   1.0f, 1.0f,
    };
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return vao_ != 0;
}

void FullscreenQuad::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
    vbo_ = 0;
}

void FullscreenQuad::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

} // namespace mate
