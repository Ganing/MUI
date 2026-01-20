/****************************************************************************
 * 标题: SDL3+ThorVG OpenGL渲染示例（使用GLAD）+ ShaderToy背景
 * 文件：APP06 SDL3+GlCanvas+shadertoy.cpp
 * 版本：1.0
 * 修改: 添加ShaderToy背景渲染，实现分层渲染效果
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 *
 * GLAD生成说明：
 * - 访问 https://glad.dav1d.de/
 * - Language: C/C++
 * - Specification: OpenGL
 * - API gl: Version 3.3 Core
 * - API wgl: 勾选（重要！）
 * - Options: Loader
 *
 * 渲染架构：
 * - 底层：ShaderToy背景效果（OpenGL FBO渲染）
 * - 顶层：ThorVG UI内容（GlCanvas渲染）
 * - 实现分层合成效果
 ****************************************************************************/

#if 0   
#define SDL_MAIN_HANDLED  

#include <cstdint>    
#include <cstdio>    
#include <cstdlib>    
#include <cstring>    
#include <memory>    
#include <cmath>  

#include <windows.h>  
#include <glad/glad.h>  

#define TVG_STATIC    
#include <thorvg/thorvg.h>    

#include <SDL3/SDL.h>    
#include <SDL3/SDL_main.h>    

#pragma comment(lib, "user32.lib")    
#pragma comment(lib, "gdi32.lib")    
#pragma comment(lib, "advapi32.lib")    
#pragma comment(lib, "ole32.lib")    
#pragma comment(lib, "shell32.lib")    
#pragma comment(lib, "kernel32.lib")    
#pragma comment(lib, "version.lib")    
#pragma comment(lib, "imm32.lib")    
#pragma comment(lib, "setupapi.lib")    
#pragma comment(lib, "cfgmgr32.lib")    
#pragma comment(lib, "winmm.lib")    

#pragma comment(lib, "SDL3-static.lib")    
#pragma comment(lib, "SDL_uclibc.lib")    
#pragma comment(lib, "libthorvg.lib")    
#pragma comment(lib, "opengl32.lib")  

 // 窗口尺寸  
constexpr uint32_t WINDOW_WIDTH = 800;
constexpr uint32_t WINDOW_HEIGHT = 800;

// ShaderToy FBO相关  
struct ShaderToyFBO {
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint rbo = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    bool init(uint32_t w, uint32_t h) {
        width = w;
        height = h;

        // 创建FBO  
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // 创建颜色纹理  
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

        // 创建渲染缓冲对象（深度和模板）  
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

        // 检查FBO完整性  
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("FBO不完整\n");
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void cleanup() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (colorTexture) glDeleteTextures(1, &colorTexture);
        if (rbo) glDeleteRenderbuffers(1, &rbo);
        fbo = colorTexture = rbo = 0;
    }

    void bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }

    void unbind() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

// Shader程序  
struct ShaderProgram {
    GLuint program = 0;

    bool init(const char* vertSrc, const char* fragSrc) {
        // 编译顶点着色器  
        GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertShader, 1, &vertSrc, nullptr);
        glCompileShader(vertShader);

        // 编译片段着色器  
        GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragShader, 1, &fragSrc, nullptr);
        glCompileShader(fragShader);

        // 创建程序  
        program = glCreateProgram();
        glAttachShader(program, vertShader);
        glAttachShader(program, fragShader);
        glLinkProgram(program);

        // 检查链接状态  
        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint infoLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 0) {
                char* infoLog = (char*)malloc(infoLen);
                glGetProgramInfoLog(program, infoLen, nullptr, infoLog);
                printf("着色器链接错误: %s\n", infoLog);
                free(infoLog);
            }
            return false;
        }

        // 清理  
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);

        return true;
    }

    void use() {
        glUseProgram(program);
    }

    void setUniform2f(const char* name, float x, float y) {
        GLint loc = glGetUniformLocation(program, name);
        if (loc >= 0) glUniform2f(loc, x, y);
    }

    void setUniform1f(const char* name, float value) {
        GLint loc = glGetUniformLocation(program, name);
        if (loc >= 0) glUniform1f(loc, value);
    }

    void cleanup() {
        if (program) glDeleteProgram(program);
        program = 0;
    }
};

// 应用程序状态结构体  
struct OGV {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_GLContext glContext = nullptr;
    bool running = true;
    std::unique_ptr<tvg::GlCanvas> canvas;

    // ShaderToy相关  
    ShaderToyFBO shaderToyFBO;
    ShaderProgram shaderProgram;
    float time = 0.0f;
    int mouseX = 0;
    int mouseY = 0;

    // 全屏四边形VAO  
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;
};

// 全局应用状态  
static OGV ogv;

// ShaderToy着色器代码  
const char* vertShaderSrc = R"(  
#version 330 core  
layout(location = 0) in vec2 aPos;  
layout(location = 1) in vec2 aTexCoord;  
out vec2 fragCoord;  
void main() {  
    fragCoord = aPos * 0.5 + 0.5;  
    gl_Position = vec4(aPos, 0.0, 1.0);  
}  
)";

const char* fragShaderSrc = R"(  
#version 330 core  
in vec2 fragCoord;  
out vec4 fragColor;  
uniform vec2 iResolution;  
uniform vec2 iMouse;  
uniform float iTime;  
  
void mainImage(out vec4 fragColor, in vec2 fragCoord) {  
    vec2 uv = (fragCoord * 2. - iResolution.xy)/iResolution.xx;  
    vec2 M = (iMouse.xy * 2. - iResolution.xy)/iResolution.xx;  
    if (iMouse.xy != vec2(0))  
        uv -= M;  
      
    vec3 sky = vec3(0.3, 0.7, 1.0);  
      
    vec3 sun = vec3(1.0, 0.8, 0.7) * 1.5;  
      
    float sun_radius = 0.12;  
      
    float t = clamp(length(uv) * 0.3 / sun_radius, 0.0, 1.0);  
    t = 1. - (1.-t)*(1.-t);  
      
    vec3 col = mix(  
        sun,  
        sky,  
        t  
    );  
  
    fragColor = vec4(col,1.0);  
}  
  
void main() {  
    mainImage(fragColor, fragCoord * iResolution.xy);  
}  
)";

// 初始化全屏四边形  
void initQuad() {
    float quadVertices[] = {
        // 位置        // 纹理坐标  
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };

    glGenVertexArrays(1, &ogv.quadVAO);
    glGenBuffers(1, &ogv.quadVBO);

    glBindVertexArray(ogv.quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ogv.quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // 位置属性  
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // 纹理坐标属性  
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

// ThorVG 内容绘制函数  
bool content(tvg::Canvas* canvas, uint32_t w, uint32_t h) {
    // 清空之前的绘制内容  
    canvas->remove(nullptr);

    // 半透明黄色图形（带圆角的矩形 + 两个圆形）  
    auto shape4 = tvg::Shape::gen();
    shape4->appendRect(50, 50, 300, 300, 50, 50);
    shape4->appendCircle(450, 200, 150, 150);
    shape4->appendCircle(650, 200, 150, 100);
    shape4->fill(255, 255, 0, 200);  // 添加透明度  
    canvas->push(shape4);

    // 半透明绿色矩形（带圆角）  
    auto shape1 = tvg::Shape::gen();
    shape1->appendRect(50, 500, 300, 300, 50, 50);
    shape1->fill(0, 255, 0, 200);  // 添加透明度  
    canvas->push(shape1);

    // 半透明黄色圆形  
    auto shape2 = tvg::Shape::gen();
    shape2->appendCircle(450, 650, 150, 150);
    shape2->fill(255, 255, 0, 200);  // 添加透明度  
    canvas->push(shape2);

    // 半透明青色圆形  
    auto shape3 = tvg::Shape::gen();
    shape3->appendCircle(650, 650, 150, 100);
    shape3->fill(0, 255, 255, 200);  // 添加透明度  
    canvas->push(shape3);

    return true;
}

// 初始化 SDL（使用OpenGL）  
static bool initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("初始化SDL失败: %s\n", SDL_GetError());
        return false;
    }

    // 设置OpenGL属性  
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // 创建窗口  
    ogv.window = SDL_CreateWindow(u8"SDL3 + ThorVG + ShaderToy 分层渲染",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_OPENGL);

    if (!ogv.window) {
        printf("创建窗口失败: %s\n", SDL_GetError());
        return false;
    }

    // 创建OpenGL上下文  
    ogv.glContext = SDL_GL_CreateContext(ogv.window);
    if (!ogv.glContext) {
        printf("创建OpenGL上下文失败: %s\n", SDL_GetError());
        return false;
    }

    // 加载 GLAD  
    if (!gladLoadGL()) {
        printf("GLAD 初始化失败\n");
        return false;
    }

    // 设置垂直同步  
    SDL_GL_SetSwapInterval(1);

    printf("SDL3 + OpenGL 初始化成功\n");
    printf("OpenGL 版本: %s\n", (const char*)glGetString(GL_VERSION));
    printf("OpenGL 渲染器: %s\n", (const char*)glGetString(GL_RENDERER));

    return true;
}

// 初始化 ShaderToy  
static bool initShaderToy() {
    // 初始化FBO  
    if (!ogv.shaderToyFBO.init(WINDOW_WIDTH, WINDOW_HEIGHT)) {
        printf("初始化ShaderToy FBO失败\n");
        return false;
    }

    // 初始化着色器程序  
    if (!ogv.shaderProgram.init(vertShaderSrc, fragShaderSrc)) {
        printf("初始化ShaderToy着色器失败\n");
        return false;
    }

    // 初始化全屏四边形  
    initQuad();

    printf("ShaderToy初始化成功\n");
    return true;
}

// 初始化 ThorVG（使用GlCanvas）  
static bool initThorVG() {
    // 初始化 ThorVG 引擎  
    if (tvg::Initializer::init(4) != tvg::Result::Success) {
        printf("初始化ThorVG引擎失败\n");
        return false;
    }

    // 生成OpenGL画布  
    ogv.canvas = std::unique_ptr<tvg::GlCanvas>(tvg::GlCanvas::gen());
    if (!ogv.canvas) {
        printf("创建ThorVG OpenGL画布失败\n");
        return false;
    }

    // 获取当前OpenGL上下文  
    HGLRC hglrc = wglGetCurrentContext();
    if (!hglrc) {
        printf("获取当前OpenGL上下文失败\n");
        return false;
    }

    // 设置画布目标  
    if (ogv.canvas->target(hglrc, 0, WINDOW_WIDTH, WINDOW_HEIGHT, tvg::ColorSpace::ABGR8888S)
        != tvg::Result::Success) {
        printf("设置ThorVG画布目标失败\n");
        return false;
    }

    printf("ThorVG GlCanvas target设置成功\n");

    // 设置视口  
    ogv.canvas->viewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    return true;
}

// 渲染ShaderToy背景  
void renderShaderToy() {
    // 绑定ShaderToy FBO  
    ogv.shaderToyFBO.bind();

    // 清空缓冲区  
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 使用着色器程序  
    ogv.shaderProgram.use();

    // 设置uniform变量  
    ogv.shaderProgram.setUniform2f("iResolution", (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT);
    ogv.shaderProgram.setUniform2f("iMouse", (float)ogv.mouseX, (float)ogv.mouseY);
    ogv.shaderProgram.setUniform1f("iTime", ogv.time);

    // 绘制全屏四边形  
    glBindVertexArray(ogv.quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    // 解绑FBO  
    ogv.shaderToyFBO.unbind();
}

// 将ShaderToy结果渲染到主framebuffer  
void renderShaderToyToMain() {
    // 绑定主framebuffer  
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    // 启用混合功能（关键修复）  
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 使用简单的纹理着色器  
    static GLuint simpleProgram = 0;
    if (simpleProgram == 0) {
        // 简单的顶点着色器  
        const char* simpleVert = R"(  
#version 330 core  
layout(location = 0) in vec2 aPos;  
layout(location = 1) in vec2 aTexCoord;  
out vec2 TexCoord;  
void main() {  
    TexCoord = aTexCoord;  
    gl_Position = vec4(aPos, 0.0, 1.0);  
}  
)";

        // 简单的片段着色器  
        const char* simpleFrag = R"(  
#version 330 core  
in vec2 TexCoord;  
out vec4 FragColor;  
uniform sampler2D uTexture;  
void main() {  
    FragColor = texture(uTexture, TexCoord);  
}  
)";

        // 编译着色器  
        GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertShader, 1, &simpleVert, nullptr);
        glCompileShader(vertShader);

        GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragShader, 1, &simpleFrag, nullptr);
        glCompileShader(fragShader);

        simpleProgram = glCreateProgram();
        glAttachShader(simpleProgram, vertShader);
        glAttachShader(simpleProgram, fragShader);
        glLinkProgram(simpleProgram);

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
    }

    // 使用着色器程序  
    glUseProgram(simpleProgram);

    // 绑定ShaderToy纹理  
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ogv.shaderToyFBO.colorTexture);
    glUniform1i(glGetUniformLocation(simpleProgram, "uTexture"), 0);

    // 绘制全屏四边形  
    glBindVertexArray(ogv.quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}
 
 
// 处理事件
static void handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            ogv.running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE ||
                event.key.scancode == SDL_SCANCODE_Q) {
                ogv.running = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:  // 修正：添加下划线
            ogv.mouseX = event.motion.x;
            ogv.mouseY = WINDOW_HEIGHT - event.motion.y;
            break;
        default:
            break;
        }
    }
}

// 渲染到窗口  
static void render() {
    // 1. 渲染ShaderToy背景到FBO  
    renderShaderToy();

    // 2. 将ShaderToy结果渲染到主framebuffer作为背景  
    renderShaderToyToMain();

    // 3. 渲染ThorVG UI内容到顶层  
    content(ogv.canvas.get(), WINDOW_WIDTH, WINDOW_HEIGHT);
    ogv.canvas->update();
    ogv.canvas->draw(false);
    ogv.canvas->sync();

    // 4. 交换缓冲区显示内容  
    SDL_GL_SwapWindow(ogv.window);

    // 更新时间  
    ogv.time += 0.016f;  // 假设60FPS  
}

// 清理资源  
static void cleanup() {
    // 清理ShaderToy资源  
    ogv.shaderProgram.cleanup();
    ogv.shaderToyFBO.cleanup();

    if (ogv.quadVAO) glDeleteVertexArrays(1, &ogv.quadVAO);
    if (ogv.quadVBO) glDeleteBuffers(1, &ogv.quadVBO);

    // 清理ThorVG资源  
    ogv.canvas.reset();
    tvg::Initializer::term();

    if (ogv.glContext) {
        SDL_GL_DestroyContext(ogv.glContext);
        ogv.glContext = nullptr;
    }

    if (ogv.window) {
        SDL_DestroyWindow(ogv.window);
        ogv.window = nullptr;
    }

    SDL_Quit();
}

// 主函数  
int main(int argc, char** argv) {
    printf("SDL3 + ThorVG + ShaderToy 分层渲染示例\n");
    printf("=========================================\n");
    printf("功能说明:\n");
    printf("  - 底层：ShaderToy动态背景效果\n");
    printf("  - 顶层：ThorVG半透明UI内容\n");
    printf("  - 实现分层合成渲染\n");
    printf("\n控制说明:\n");
    printf("  - 鼠标移动：影响ShaderToy效果\n");
    printf("  - 按 ESC 或 Q 退出程序\n\n");

    if (!initSDL()) {
        cleanup();
        return 1;
    }

    if (!initShaderToy()) {
        cleanup();
        return 1;
    }

    if (!initThorVG()) {
        cleanup();
        return 1;
    }

    printf("开始分层渲染...\n");

    while (ogv.running) {
        handleEvents();
        render();
        SDL_Delay(1);
    }

    cleanup();

    printf("程序结束\n");
    return 0;
}

#endif





/****************************************************************************
 * 标题: SDL3+ThorVG OpenGL渲染示例（使用GLAD）+ ShaderToy背景
 * 文件：APP06 SDL3+GlCanvas+shadertoy.cpp
 * 版本：2.0
 * 修改: 添加窗口可调整大小功能，修改ShaderToy效果
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 *
 * GLAD生成说明：
 * - 访问 https://glad.dav1d.de/
 * - Language: C/C++
 * - Specification: OpenGL
 * - API gl: Version 3.3 Core
 * - API wgl: 勾选（重要！）
 * - Options: Loader
 *
 * 渲染架构：
 * - 底层：ShaderToy背景效果（OpenGL FBO渲染）
 * - 顶层：ThorVG UI内容（GlCanvas渲染）
 * - 实现分层合成效果
 * - 支持窗口动态调整大小
 ****************************************************************************/

#if 1  
#define SDL_MAIN_HANDLED  

#include <cstdint>    
#include <cstdio>    
#include <cstdlib>    
#include <cstring>    
#include <memory>    
#include <cmath>  

#include <windows.h>  
#include <glad/glad.h>  

#define TVG_STATIC    
#include <thorvg/thorvg.h>    

#include <SDL3/SDL.h>    
#include <SDL3/SDL_main.h>    

#pragma comment(lib, "user32.lib")    
#pragma comment(lib, "gdi32.lib")    
#pragma comment(lib, "advapi32.lib")    
#pragma comment(lib, "ole32.lib")    
#pragma comment(lib, "shell32.lib")    
#pragma comment(lib, "kernel32.lib")    
#pragma comment(lib, "version.lib")    
#pragma comment(lib, "imm32.lib")    
#pragma comment(lib, "setupapi.lib")    
#pragma comment(lib, "cfgmgr32.lib")    
#pragma comment(lib, "winmm.lib")    

#pragma comment(lib, "SDL3-static.lib")    
#pragma comment(lib, "SDL_uclibc.lib")    
#pragma comment(lib, "libthorvg.lib")    
#pragma comment(lib, "opengl32.lib")  

 // 窗口尺寸（动态）  
uint32_t WINDOW_WIDTH = 800;
uint32_t WINDOW_HEIGHT = 800;

// ShaderToy FBO相关  
struct ShaderToyFBO {
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint rbo = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    bool init(uint32_t w, uint32_t h) {
        width = w;
        height = h;

        // 创建FBO  
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // 创建颜色纹理  
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

        // 创建渲染缓冲对象（深度和模板）  
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

        // 检查FBO完整性  
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("FBO不完整\n");
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void cleanup() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (colorTexture) glDeleteTextures(1, &colorTexture);
        if (rbo) glDeleteRenderbuffers(1, &rbo);
        fbo = colorTexture = rbo = 0;
    }

    void resize(uint32_t w, uint32_t h) {
        cleanup();
        init(w, h);
    }

    void bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }

    void unbind() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

// Shader程序  
struct ShaderProgram {
    GLuint program = 0;

    bool init(const char* vertSrc, const char* fragSrc) {
        // 编译顶点着色器  
        GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertShader, 1, &vertSrc, nullptr);
        glCompileShader(vertShader);

        // 编译片段着色器  
        GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragShader, 1, &fragSrc, nullptr);
        glCompileShader(fragShader);

        // 创建程序  
        program = glCreateProgram();
        glAttachShader(program, vertShader);
        glAttachShader(program, fragShader);
        glLinkProgram(program);

        // 检查链接状态  
        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint infoLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 0) {
                char* infoLog = (char*)malloc(infoLen);
                glGetProgramInfoLog(program, infoLen, nullptr, infoLog);
                printf("着色器链接错误: %s\n", infoLog);
                free(infoLog);
            }
            return false;
        }

        // 清理  
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);

        return true;
    }

    void use() {
        glUseProgram(program);
    }

    void setUniform2f(const char* name, float x, float y) {
        GLint loc = glGetUniformLocation(program, name);
        if (loc >= 0) glUniform2f(loc, x, y);
    }

    void setUniform1f(const char* name, float value) {
        GLint loc = glGetUniformLocation(program, name);
        if (loc >= 0) glUniform1f(loc, value);
    }

    void cleanup() {
        if (program) glDeleteProgram(program);
        program = 0;
    }
};

// 应用程序状态结构体  
struct OGV {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_GLContext glContext = nullptr;
    bool running = true;
    std::unique_ptr<tvg::GlCanvas> canvas;

    // ShaderToy相关  
    ShaderToyFBO shaderToyFBO;
    ShaderProgram shaderProgram;
    float time = 0.0f;
    float heightFactor = 0.5f;  // 高度因子，控制亮度  

    // 全屏四边形VAO  
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;
};

// 全局应用状态  
static OGV ogv;

// ShaderToy着色器代码（修改为高度控制亮度）  
const char* vertShaderSrc = R"(  
#version 330 core  
layout(location = 0) in vec2 aPos;  
layout(location = 1) in vec2 aTexCoord;  
out vec2 fragCoord;  
void main() {  
    fragCoord = aPos * 0.5 + 0.5;  
    gl_Position = vec4(aPos, 0.0, 1.0);  
}  
)";

const char* fragShaderSrc = R"(  
#version 330 core  
in vec2 fragCoord;  
out vec4 fragColor;  
uniform vec2 iResolution;  
uniform float iHeightFactor;  
uniform float iTime;  
  
void mainImage(out vec4 fragColor, in vec2 fragCoord) {  
    vec2 uv = (fragCoord * 2. - iResolution.xy)/iResolution.xx;  
      
    vec3 sky = vec3(0.3, 0.7, 1.0);  
    vec3 sun = vec3(1.0, 0.8, 0.7) * 1.5;  
      
    float sun_radius = 0.12;  
      
    // 使用高度因子控制亮度，越中间越亮  
    float heightBrightness = 1.0 - abs(uv.y) * 0.5;  
    heightBrightness = mix(0.3, 1.0, heightBrightness * iHeightFactor);  
      
    float t = clamp(length(uv) * 0.3 / sun_radius, 0.0, 1.0);  
    t = 1. - (1.-t)*(1.-t);  
      
    vec3 col = mix(  
        sun * heightBrightness,  
        sky * heightBrightness,  
        t  
    );  
  
    fragColor = vec4(col,1.0);  
}  
  
void main() {  
    mainImage(fragColor, fragCoord * iResolution.xy);  
}  
)";

// 初始化全屏四边形  
void initQuad() {
    float quadVertices[] = {
        // 位置        // 纹理坐标  
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };

    glGenVertexArrays(1, &ogv.quadVAO);
    glGenBuffers(1, &ogv.quadVBO);

    glBindVertexArray(ogv.quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ogv.quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // 位置属性  
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // 纹理坐标属性  
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

// ThorVG 内容绘制函数  
bool content(tvg::Canvas* canvas, uint32_t w, uint32_t h) {
    // 清空之前的绘制内容  
    canvas->remove(nullptr);

    // 半透明黄色图形（带圆角的矩形 + 两个圆形）  
    auto shape4 = tvg::Shape::gen();
    shape4->appendRect(50, 50, 300, 300, 50, 50);
    shape4->appendCircle(450, 200, 150, 150);
    shape4->appendCircle(650, 200, 150, 100);
    shape4->fill(255, 255, 0, 200);  // 添加透明度  
    canvas->push(shape4);

    // 半透明绿色矩形（带圆角）  
    auto shape1 = tvg::Shape::gen();
    shape1->appendRect(50, 500, 300, 300, 50, 50);
    shape1->fill(0, 255, 0, 200);  // 添加透明度  
    canvas->push(shape1);

    // 半透明黄色圆形  
    auto shape2 = tvg::Shape::gen();
    shape2->appendCircle(450, 650, 150, 150);
    shape2->fill(255, 255, 0, 200);  // 添加透明度  
    canvas->push(shape2);

    // 半透明青色圆形  
    auto shape3 = tvg::Shape::gen();
    shape3->appendCircle(650, 650, 150, 100);
    shape3->fill(0, 255, 255, 200);  // 添加透明度  
    canvas->push(shape3);

    return true;
}

// 窗口大小调整回调函数  
void onWindowResize(int newWidth, int newHeight) {
    printf("窗口大小调整: %d x %d\n", newWidth, newHeight);

    WINDOW_WIDTH = newWidth;
    WINDOW_HEIGHT = newHeight;

    // 重新创建ShaderToy FBO  
    ogv.shaderToyFBO.resize(WINDOW_WIDTH, WINDOW_HEIGHT);

    // 重新设置ThorVG画布目标  
    HGLRC hglrc = wglGetCurrentContext();
    if (hglrc && ogv.canvas) {
        ogv.canvas->target(hglrc, 0, WINDOW_WIDTH, WINDOW_HEIGHT, tvg::ColorSpace::ABGR8888S);
        ogv.canvas->viewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    }
}

// 初始化 SDL（使用OpenGL）  
static bool initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("初始化SDL失败: %s\n", SDL_GetError());
        return false;
    }

    // 设置OpenGL属性  
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // 创建可调整大小的窗口  
    ogv.window = SDL_CreateWindow(u8"SDL3 + ThorVG + ShaderToy 分层渲染（可调整大小）",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);  // 添加可调整大小标志  

    if (!ogv.window) {
        printf("创建窗口失败: %s\n", SDL_GetError());
        return false;
    }

    // 创建OpenGL上下文  
    ogv.glContext = SDL_GL_CreateContext(ogv.window);
    if (!ogv.glContext) {
        printf("创建OpenGL上下文失败: %s\n", SDL_GetError());
        return false;
    }

    // 加载 GLAD  
    if (!gladLoadGL()) {
        printf("GLAD 初始化失败\n");
        return false;
    }

    // 设置垂直同步  
    SDL_GL_SetSwapInterval(1);

    printf("SDL3 + OpenGL 初始化成功\n");
    printf("OpenGL 版本: %s\n", (const char*)glGetString(GL_VERSION));
    printf("OpenGL 渲染器: %s\n", (const char*)glGetString(GL_RENDERER));

    return true;
}

// 初始化 ShaderToy  
static bool initShaderToy() {
    // 初始化FBO  
    if (!ogv.shaderToyFBO.init(WINDOW_WIDTH, WINDOW_HEIGHT)) {
        printf("初始化ShaderToy FBO失败\n");
        return false;
    }

    // 初始化着色器程序  
    if (!ogv.shaderProgram.init(vertShaderSrc, fragShaderSrc)) {
        printf("初始化ShaderToy着色器失败\n");
        return false;
    }

    // 初始化全屏四边形  
    initQuad();

    printf("ShaderToy初始化成功\n");
    return true;
}

// 初始化 ThorVG（使用GlCanvas）  
static bool initThorVG() {
    // 初始化 ThorVG 引擎  
    if (tvg::Initializer::init(4) != tvg::Result::Success) {
        printf("初始化ThorVG引擎失败\n");
        return false;
    }

    // 生成OpenGL画布  
    ogv.canvas = std::unique_ptr<tvg::GlCanvas>(tvg::GlCanvas::gen());
    if (!ogv.canvas) {
        printf("创建ThorVG OpenGL画布失败\n");
        return false;
    }

    // 获取当前OpenGL上下文  
    HGLRC hglrc = wglGetCurrentContext();
    if (!hglrc) {
        printf("获取当前OpenGL上下文失败\n");
        return false;
    }

    // 设置画布目标  
    if (ogv.canvas->target(hglrc, 0, WINDOW_WIDTH, WINDOW_HEIGHT, tvg::ColorSpace::ABGR8888S)
        != tvg::Result::Success) {
        printf("设置ThorVG画布目标失败\n");
        return false;
    }

    printf("ThorVG GlCanvas target设置成功\n");

    // 设置视口  
    ogv.canvas->viewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    return true;
}

// 渲染ShaderToy背景  
void renderShaderToy() {
    // 绑定ShaderToy FBO  
    ogv.shaderToyFBO.bind();

    // 清空缓冲区  
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 使用着色器程序  
    ogv.shaderProgram.use();

    // 设置uniform变量  
    ogv.shaderProgram.setUniform2f("iResolution", (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT);
    ogv.shaderProgram.setUniform1f("iHeightFactor", ogv.heightFactor);
    ogv.shaderProgram.setUniform1f("iTime", ogv.time);

    // 绘制全屏四边形  
    glBindVertexArray(ogv.quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    // 解绑FBO  
    ogv.shaderToyFBO.unbind();
}

// 将ShaderToy结果渲染到主framebuffer  
void renderShaderToyToMain() {
    // 绑定主framebuffer  
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    // 启用混合功能（关键修复）  
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 使用简单的纹理着色器  
    static GLuint simpleProgram = 0;
    if (simpleProgram == 0) {
        // 简单的顶点着色器  
        const char* simpleVert = R"(  
#version 330 core  
layout(location = 0) in vec2 aPos;  
layout(location = 1) in vec2 aTexCoord;  
out vec2 TexCoord;  
void main() {  
    TexCoord = aTexCoord;  
    gl_Position = vec4(aPos, 0.0, 1.0);  
}  
)";

        // 简单的片段着色器  
        const char* simpleFrag = R"(  
#version 330 core  
in vec2 TexCoord;  
out vec4 FragColor;  
uniform sampler2D uTexture;  
void main() {  
    FragColor = texture(uTexture, TexCoord);  
}  
)";

        // 编译着色器  
        GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertShader, 1, &simpleVert, nullptr);
        glCompileShader(vertShader);

        GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragShader, 1, &simpleFrag, nullptr);
        glCompileShader(fragShader);

        simpleProgram = glCreateProgram();
        glAttachShader(simpleProgram, vertShader);
        glAttachShader(simpleProgram, fragShader);
        glLinkProgram(simpleProgram);

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
    }

    // 使用着色器程序  
    glUseProgram(simpleProgram);

    // 绑定ShaderToy纹理  
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ogv.shaderToyFBO.colorTexture);
    glUniform1i(glGetUniformLocation(simpleProgram, "uTexture"), 0);

    // 绘制全屏四边形  
    glBindVertexArray(ogv.quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}
 
// 处理事件
static void handleEvents() {
    SDL_Event event;
    float normalizedY;  // 声明在 switch 外部

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            ogv.running = false;
            break;

        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE ||
                event.key.scancode == SDL_SCANCODE_Q) {
                ogv.running = false;
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            normalizedY = (float)event.motion.y / WINDOW_HEIGHT;
            ogv.heightFactor = 1.0f - fabs(normalizedY - 0.5f) * 2.0f;
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            onWindowResize(event.window.data1, event.window.data2);
            break;

        default:
            break;
        }
    }
}

// 渲染到窗口  
static void render() {
    // 1. 渲染ShaderToy背景到FBO  
    renderShaderToy();

    // 2. 将ShaderToy结果渲染到主framebuffer作为背景  
    renderShaderToyToMain();

    // 3. 渲染ThorVG UI内容到顶层（使用false避免清空背景）  
    content(ogv.canvas.get(), WINDOW_WIDTH, WINDOW_HEIGHT);
    ogv.canvas->update();
    ogv.canvas->draw(false);  // 关键：使用false避免清空背景  
    ogv.canvas->sync();

    // 4. 交换缓冲区显示内容  
    SDL_GL_SwapWindow(ogv.window);

    // 更新时间  
    ogv.time += 0.016f;  // 假设60FPS  
}

// 清理资源  
static void cleanup() {
    // 清理ShaderToy资源  
    ogv.shaderProgram.cleanup();
    ogv.shaderToyFBO.cleanup();

    if (ogv.quadVAO) glDeleteVertexArrays(1, &ogv.quadVAO);
    if (ogv.quadVBO) glDeleteBuffers(1, &ogv.quadVBO);

    // 清理ThorVG资源  
    ogv.canvas.reset();
    tvg::Initializer::term();

    if (ogv.glContext) {
        SDL_GL_DestroyContext(ogv.glContext);
        ogv.glContext = nullptr;
    }

    if (ogv.window) {
        SDL_DestroyWindow(ogv.window);
        ogv.window = nullptr;
    }

    SDL_Quit();
}

// 主函数  
int main(int argc, char** argv) {
    printf("SDL3 + ThorVG + ShaderToy 分层渲染示例 v2.0\n");
    printf("==========================================\n");
    printf("功能说明:\n");
    printf("  - 底层：ShaderToy动态背景效果\n");
    printf("  - 顶层：ThorVG半透明UI内容\n");
    printf("  - 实现分层合成渲染\n");
    printf("  - 支持窗口动态调整大小\n");
    printf("\n控制说明:\n");
    printf("  - 鼠标上下移动：调整背景亮度（越中间越亮）\n");
    printf("  - 拖动窗口边缘：调整窗口大小\n");
    printf("  - 按 ESC 或 Q 退出程序\n\n");

    if (!initSDL()) {
        cleanup();
        return 1;
    }

    if (!initShaderToy()) {
        cleanup();
        return 1;
    }

    if (!initThorVG()) {
        cleanup();
        return 1;
    }

    printf("开始分层渲染...\n");

    while (ogv.running) {
        handleEvents();
        render();
        SDL_Delay(1);
    }

    cleanup();

    printf("程序结束\n");
    return 0;
}

#endif