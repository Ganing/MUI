#include "UIFramework.h"  
#include <algorithm>  
#include <thread>

// 静态成员定义  
RenderPooler<tvg::Text> UIElement::textPool;
RenderPooler<tvg::Shape> UIElement::shapePool;
RenderPooler<tvg::Picture> UIElement::picturePool;

// 辅助函数: 宽字符转 UTF-8  
std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

// UIElement 实现  
void UIElement::loadFonts(const wchar_t* siyuanPath, const wchar_t* fuhaoPath) {
    // 读取思源字体  
    HANDLE hFile = CreateFileW(siyuanPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD fileSize = GetFileSize(hFile, nullptr);
        char* fontData = new char[fileSize];
        DWORD bytesRead;
        ReadFile(hFile, fontData, fileSize, &bytesRead, nullptr);
        CloseHandle(hFile);

        tvg::Text::load("siyuan", fontData, fileSize, "ttf", true);
        delete[] fontData;
    }

    // 读取符号字体  
    hFile = CreateFileW(fuhaoPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD fileSize = GetFileSize(hFile, nullptr);
        char* fontData = new char[fileSize];
        DWORD bytesRead;
        ReadFile(hFile, fontData, fileSize, &bytesRead, nullptr);
        CloseHandle(hFile);

        tvg::Text::load("fuhao", fontData, fileSize, "ttf", true);
        delete[] fontData;
    }
}

// UIManager 实现  
UIManager::UIManager() {}

UIManager::~UIManager() {
    if (buffer) delete[] buffer;
}

bool UIManager::initialize(uint32_t width, uint32_t height) {
    bufferWidth = width;
    bufferHeight = height;
    buffer = new uint32_t[width * height];

    canvas = std::unique_ptr<tvg::SwCanvas>(tvg::SwCanvas::gen());
    if (!canvas) return false;

    return canvas->target(buffer, width, width, height, tvg::ColorSpace::ARGB8888) == tvg::Result::Success;
}

void UIManager::resize(uint32_t width, uint32_t height) {
    if (buffer) delete[] buffer;
    bufferWidth = width;
    bufferHeight = height;
    buffer = new uint32_t[width * height];
    canvas->target(buffer, width, width, height, tvg::ColorSpace::ARGB8888);
}

void UIManager::addElement(std::unique_ptr<UIElement> element) {
    elements.push_back(std::move(element));
}

void UIManager::render() {
    // 清空画布  
    memset(buffer, 0, bufferWidth * bufferHeight * sizeof(uint32_t));

    auto scene = tvg::Scene::gen();

    // 渲染所有元素  
    for (auto& element : elements) {
        if (element->visible) {
            element->render(scene);
        }
    }

    canvas->push(scene);
    canvas->draw(true);
    canvas->sync();
    canvas->remove();
}

void UIManager::handleMouseDown(int x, int y) {
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        if ((*it)->visible && (*it)->enabled && (*it)->hitTest((float)x, (float)y)) {
            pressedElement = it->get();
            pressedElement->onMouseDown((float)x, (float)y);
            break;
        }
    }
}

void UIManager::handleMouseUp(int x, int y) {
    if (pressedElement) {
        pressedElement->onMouseUp((float)x, (float)y);
        pressedElement = nullptr;
    }
}

void UIManager::handleMouseMove(int x, int y) {
    UIElement* newHovered = nullptr;

    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        if ((*it)->visible && (*it)->enabled && (*it)->hitTest((float)x, (float)y)) {
            newHovered = it->get();
            break;
        }
    }

    if (newHovered != hoveredElement) {
        hoveredElement = newHovered;
    }

    if (pressedElement) {
        pressedElement->onMouseMove((float)x, (float)y);
    }
    else if (hoveredElement) {
        hoveredElement->onMouseMove((float)x, (float)y);
    }
}

// UIButton 实现  
UIButton::UIButton(const wchar_t* text) : label(text) {
    height = 40.0f;
}

void UIButton::render(tvg::Scene* parent) {
    if (!visible) return;

    // 获取背景形状  
    if (!bgShape) {
        bgShape = acquireShape();
    }

    bgShape->reset();
    bgShape->appendRect(x, y, width, height, 5, 5);

    // 根据状态设置颜色  
    if (isPressed) {
        bgShape->fill(pressR, pressG, pressB);
    }
    else if (isHovered) {
        bgShape->fill(hoverR, hoverG, hoverB);
    }
    else {
        bgShape->fill(bgR, bgG, bgB);
    }

    parent->push(bgShape);

    // 获取文本  
    if (!labelText) {
        labelText = acquireText();
        labelText->font("siyuan");
    }

    std::string utf8Label = WideToUtf8(label.c_str());
    labelText->text(utf8Label.c_str());
    labelText->size(16.0f);
    labelText->fill(255, 255, 255);
    labelText->translate(x + 10, y + height / 2 + 6);

    parent->push(labelText);
}

void UIButton::onMouseDown(float px, float py) {
    isPressed = true;
}

void UIButton::onMouseUp(float px, float py) {
    if (isPressed && hitTest(px, py)) {
        if (onClick) onClick();
    }
    isPressed = false;
}

void UIButton::onMouseMove(float px, float py) {
    isHovered = hitTest(px, py);
}

void UIButton::setLabel(const wchar_t* text) {
    label = text;
}

//  
UILabel::UILabel(const wchar_t* txt) : content(txt) {
    height = 20.0f;
}

void UILabel::render(tvg::Scene* parent) {
    if (!visible) return;

    if (!text) {
        text = acquireText();
        text->font("siyuan");
    }

    std::string utf8Content = WideToUtf8(content.c_str());
    text->text(utf8Content.c_str());
    text->size(fontSize);
    text->fill(r, g, b);
    text->translate(x, y);

    parent->push(text);
}

void UILabel::setText(const wchar_t* txt) {
    content = txt;
}

void UILabel::setColor(uint8_t red, uint8_t green, uint8_t blue) {
    r = red;
    g = green;
    b = blue;
}

void UILabel::setFontSize(float size) {
    fontSize = size;
}

UISlider::UISlider() {
    height = 20.0f;
    width = 200.0f;
}

void UISlider::render(tvg::Scene* parent) {
    if (!visible) return;

    // 轨道  
    if (!trackShape) {
        trackShape = acquireShape();
    }
    trackShape->reset();
    trackShape->appendRect(x, y + height / 2 - 2, width, 4, 2, 2);
    trackShape->fill(100, 100, 100);
    parent->push(trackShape);

    // 滑块  
    if (!thumbShape) {
        thumbShape = acquireShape();
    }
    float thumbX = x + (width - 16) * ((value - minValue) / (maxValue - minValue));
    thumbShape->reset();
    thumbShape->appendCircle(thumbX + 8, y + height / 2, 8, 8);
    thumbShape->fill(70, 130, 180);
    parent->push(thumbShape);
}

void UISlider::onMouseDown(float px, float py) {
    isDragging = true;
    updateValue(px);
}

void UISlider::onMouseUp(float px, float py) {
    isDragging = false;
}

void UISlider::onMouseMove(float px, float py) {
    if (isDragging) {
        updateValue(px);
    }
}

void UISlider::setValue(float val) {
    value = std::max(minValue, std::min(maxValue, val));
}

void UISlider::updateValue(float px) {
    float ratio = (px - x) / width;
    ratio = std::max(0.0f, std::min(1.0f, ratio));
    float newValue = minValue + ratio * (maxValue - minValue);

    if (newValue != value) {
        value = newValue;
        if (onValueChanged) onValueChanged(value);
    }
}

UIProgressBar::UIProgressBar() {
    height = 20.0f;
    width = 200.0f;
}

void UIProgressBar::render(tvg::Scene* parent) {
    if (!visible) return;

    // 背景  
    if (!bgShape) {
        bgShape = acquireShape();
    }
    bgShape->reset();
    bgShape->appendRect(x, y, width, height, 3, 3);
    bgShape->fill(50, 50, 50);
    parent->push(bgShape);

    // 填充  
    if (!fillShape) {
        fillShape = acquireShape();
    }
    float fillWidth = width * progress;
    fillShape->reset();
    if (fillWidth > 0) {
        fillShape->appendRect(x, y, fillWidth, height, 3, 3);
        fillShape->fill(70, 180, 130);
        parent->push(fillShape);
    }
}

void UIProgressBar::setProgress(float val) {
    progress = std::max(0.0f, std::min(1.0f, val));
}

UITooltip::UITooltip(const wchar_t* txt) : content(txt) {
    width = 150.0f;
    height = 30.0f;
}

void UITooltip::render(tvg::Scene* parent) {
    if (!visible) return;

    // 背景  
    if (!bgShape) {
        bgShape = acquireShape();
    }
    bgShape->reset();
    bgShape->appendRect(x, y, width, height, 5, 5);
    bgShape->fill(40, 40, 40, 230);
    parent->push(bgShape);

    // 文本  
    if (!text) {
        text = acquireText();
        text->font("siyuan");
    }
    std::string utf8Content = WideToUtf8(content.c_str());
    text->text(utf8Content.c_str());
    text->size(14.0f);
    text->fill(255, 255, 255);
    text->translate(x + 10, y + height / 2 + 5);
    parent->push(text);
}

void UITooltip::setText(const wchar_t* txt) {
    content = txt;
}



UIImage::UIImage() {
    width = 100.0f;
    height = 100.0f;
}

void UIImage::render(tvg::Scene* parent) {
    if (!visible || !pixelData) return;

    if (!picture) {
        picture = acquirePicture();
    }

    if (needsUpdate) {
        picture->load(pixelData, imageWidth, imageHeight, tvg::ColorSpace::ARGB8888, false);
        needsUpdate = false;
    }

    picture->size(width, height);
    picture->translate(x, y);
    parent->push(picture);
}

void UIImage::loadRawPixels(uint32_t* data, uint32_t w, uint32_t h) {
    pixelData = data;
    imageWidth = w;
    imageHeight = h;
    needsUpdate = true;
}


// Application 实现  
Application::Application(HINSTANCE hInst) : hInstance(hInst) {
}

Application::~Application() {
    if (uiManager) {
        uiManager.reset();
    }
}

bool Application::initialize(const wchar_t* title, uint32_t width, uint32_t height) {
    windowWidth = width;
    windowHeight = height;

    // 初始化 ThorVG 引擎  
    auto threads = std::thread::hardware_concurrency();
    if (threads > 0) --threads;

    if (tvg::Initializer::init(threads) != tvg::Result::Success) {
        MessageBoxW(nullptr, L"ThorVG 引擎初始化失败", L"错误", MB_OK | MB_ICONERROR);
        return false;
    }

    // 先初始化 UI 管理器 (在创建窗口之前)  
    uiManager = std::make_unique<UIManager>();
    if (!uiManager->initialize(width, height)) {
        MessageBoxW(nullptr, L"UI 管理器初始化失败", L"错误", MB_OK | MB_ICONERROR);
        return false;
    }

    setupBitmapInfo();

    // 最后创建窗口 (此时 uiManager 已经初始化)  
    if (!createWindow(title)) {
        return false;
    }

    return true;
}

bool Application::loadResources(const wchar_t* siyuanFont, const wchar_t* fuhaoFont) {
    UIElement::loadFonts(siyuanFont, fuhaoFont);
    return true;
}

bool Application::createWindow(const wchar_t* title) {
    // 注册窗口类  
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ThorVGApplication";

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"窗口类注册失败", L"错误", MB_OK | MB_ICONERROR);
        return false;
    }

    // 创建窗口  
    hwnd = CreateWindowExW(
        0, L"ThorVGApplication", title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, hInstance, this);  // 传递 this 指针  

    if (!hwnd) {
        MessageBoxW(nullptr, L"窗口创建失败", L"错误", MB_OK | MB_ICONERROR);
        return false;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return true;
}

void Application::setupBitmapInfo() {
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = windowWidth;
    bmi.bmiHeader.biHeight = -static_cast<int>(windowHeight);  // 负值表示从上到下  
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
}

void Application::onResize(uint32_t width, uint32_t height) {
    windowWidth = width;
    windowHeight = height;

    if (uiManager) {  // 添加检查  
        uiManager->resize(width, height);
    }

    setupBitmapInfo();
}

void Application::render() {
    if (uiManager) {
        uiManager->render();
    }
}

LRESULT CALLBACK Application::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Application* app = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = reinterpret_cast<Application*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    else {
        app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (app) {
        return app->WndProc(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);
        onResize(width, height);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 添加空指针检查  
        if (uiManager) {
            render();

            SetDIBitsToDevice(hdc,
                0, 0, windowWidth, windowHeight,
                0, 0, 0, windowHeight,
                uiManager->getBuffer(),
                &bmi,
                DIB_RGB_COLORS);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (uiManager) {  // 添加检查  
            uiManager->handleMouseDown(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (uiManager) {  // 添加检查  
            uiManager->handleMouseUp(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (uiManager) {  // 添加检查  
            uiManager->handleMouseMove(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int Application::run() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void Application::quit() {
    if (hwnd) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}
