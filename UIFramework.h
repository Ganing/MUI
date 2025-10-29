#pragma once
#define TVG_STATIC
#include <thorvg/thorvg.h> 
#pragma comment(lib, "libthorvg.lib")
#define NOMINMAX 
#include <windows.h>  
#include <windowsx.h> 
#include <string>  
#include <vector>  
#include <unordered_map>  
#include <functional>  
#include <memory>  


// ===== 基础数据结构 =====    
struct Color {
    uint8_t r, g, b, a;
    Color() : r(0), g(0), b(0), a(255) {}
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {
    }
};

struct Rect {
    float x, y, w, h;
    Rect() : x(0), y(0), w(0), h(0) {}
    Rect(float x, float y, float w, float h)
        : x(x), y(y), w(w), h(h) {
    }

    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};



// 前向声明  
class UIElement;
class UIManager;

// 对象池化模板 (基于 ThorVG 的 LottieRenderPooler 思路)  
template<typename T>
class RenderPooler {
private:
    std::vector<T*> pool;

public:
    ~RenderPooler() {
        for (auto obj : pool) {
            if (obj) obj->unref();
        }
        pool.clear();
    }

    T* acquire() {
        // 查找引用计数为 1 的对象(只被池持有)  
        for (auto obj : pool) {
            if (obj && obj->refCnt() == 1) {
                return obj;
            }
        }

        // 没有可用对象,创建新的  
        T* obj = T::gen();
        if (obj) {
            obj->ref();
            pool.push_back(obj);
        }
        return obj;
    }
};

// 基础 UI 元素类  
class UIElement {
protected:
    // 全局对象池 (所有子类共享)  
    static RenderPooler<tvg::Text> textPool;
    static RenderPooler<tvg::Shape> shapePool;
    static RenderPooler<tvg::Picture> picturePool;

    // 辅助方法  
    tvg::Text* acquireText() { return textPool.acquire(); }
    tvg::Shape* acquireShape() { return shapePool.acquire(); }
    tvg::Picture* acquirePicture() { return picturePool.acquire(); }

public:
    float x = 0.0f, y = 0.0f;
    float width = 100.0f, height = 30.0f;
    bool visible = true;
    bool enabled = true;

    virtual ~UIElement() = default;
    virtual void render(tvg::Scene* parent) = 0;
    virtual bool hitTest(float px, float py) {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
    virtual void onMouseDown(float px, float py) {}
    virtual void onMouseUp(float px, float py) {}
    virtual void onMouseMove(float px, float py) {}

    // 全局字体加载  
    static void loadFonts(const wchar_t* siyuanPath, const wchar_t* fuhaoPath);
};

// UI 管理器  
class UIManager {
private:
    std::unique_ptr<tvg::SwCanvas> canvas;
    std::vector<std::unique_ptr<UIElement>> elements;
    uint32_t* buffer = nullptr;
    uint32_t bufferWidth = 0;
    uint32_t bufferHeight = 0;
    UIElement* hoveredElement = nullptr;
    UIElement* pressedElement = nullptr;

public:
    UIManager();
    ~UIManager();

    bool initialize(uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);
    void addElement(std::unique_ptr<UIElement> element);
    void render();
    uint32_t* getBuffer() { return buffer; }

    // 事件处理  
    void handleMouseDown(int x, int y);
    void handleMouseUp(int x, int y);
    void handleMouseMove(int x, int y);
};

// UI 按钮  
class UIButton : public UIElement {
private:
    tvg::Shape* bgShape = nullptr;
    tvg::Text* labelText = nullptr;
    std::wstring label;
    uint8_t bgR = 70, bgG = 130, bgB = 180;
    uint8_t hoverR = 100, hoverG = 160, hoverB = 210;
    uint8_t pressR = 50, pressG = 110, pressB = 160;
    bool isHovered = false;
    bool isPressed = false;

public:
    std::function<void()> onClick;

    UIButton(const wchar_t* text);
    void render(tvg::Scene* parent) override;
    void onMouseDown(float px, float py) override;
    void onMouseUp(float px, float py) override;
    void onMouseMove(float px, float py) override;
    void setLabel(const wchar_t* text);
};

// UI 标签  
class UILabel : public UIElement {
private:
    tvg::Text* text = nullptr;
    std::wstring content;
    uint8_t r = 255, g = 255, b = 255;
    float fontSize = 16.0f;

public:
    UILabel(const wchar_t* txt);
    void render(tvg::Scene* parent) override;
    void setText(const wchar_t* txt);
    void setColor(uint8_t red, uint8_t green, uint8_t blue);
    void setFontSize(float size);
};

// UI 滑块  
class UISlider : public UIElement {
private:
    tvg::Shape* trackShape = nullptr;
    tvg::Shape* thumbShape = nullptr;
    float value = 0.5f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool isDragging = false;

    void updateValue(float px);

public:
    std::function<void(float)> onValueChanged;

    UISlider();
    void render(tvg::Scene* parent) override;
    void onMouseDown(float px, float py) override;
    void onMouseUp(float px, float py) override;
    void onMouseMove(float px, float py) override;
    void setValue(float val);
    float getValue() const { return value; }
};

// UI 进度条  
class UIProgressBar : public UIElement {
private:
    tvg::Shape* bgShape = nullptr;
    tvg::Shape* fillShape = nullptr;
    float progress = 0.0f;

public:
    UIProgressBar();
    void render(tvg::Scene* parent) override;
    void setProgress(float val);
    float getProgress() const { return progress; }
};

// UI 提示框  
class UITooltip : public UIElement {
private:
    tvg::Shape* bgShape = nullptr;
    tvg::Text* text = nullptr;
    std::wstring content;

public:
    UITooltip(const wchar_t* txt);
    void render(tvg::Scene* parent) override;
    void setText(const wchar_t* txt);
};

// UI 图片 (方案三: 原始像素数据)  
class UIImage : public UIElement {
private:
    tvg::Picture* picture = nullptr;
    uint32_t* pixelData = nullptr;
    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
    bool needsUpdate = true;

public:
    UIImage();
    void render(tvg::Scene* parent) override;
    void loadRawPixels(uint32_t* data, uint32_t w, uint32_t h);
};

// Application 类 - 管理窗口和资源  
class Application {
private:
    HWND hwnd = nullptr;
    HINSTANCE hInstance = nullptr;
    std::unique_ptr<UIManager> uiManager;
    BITMAPINFO bmi = {};
    uint32_t windowWidth = 800;
    uint32_t windowHeight = 600;
    bool running = false;

    // 窗口过程的静态回调  
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 内部方法  
    bool createWindow(const wchar_t* title);
    void setupBitmapInfo();

public:
    Application(HINSTANCE hInst);
    ~Application();

    // 初始化和资源加载  
    bool initialize(const wchar_t* title, uint32_t width, uint32_t height);
    bool loadResources(const wchar_t* siyuanFont, const wchar_t* fuhaoFont);

    // 窗口管理  
    void onResize(uint32_t width, uint32_t height);
    void render();

    // UI 管理  
    UIManager* getUIManager() { return uiManager.get(); }

    // 消息循环  
    int run();
    void quit();
};