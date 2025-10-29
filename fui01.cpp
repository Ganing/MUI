



#if 0

#include "UIFramework.h"  
#include <windowsx.h>  
#define ODD(...) printf(__VA_ARGS__)

// 全局变量  
UIManager* g_uiManager = nullptr;
HWND g_hwnd = nullptr;
BITMAPINFO g_bmi = {};


// 窗口过程  
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_SIZE: {
        if (g_uiManager) {
            RECT rect;
            GetClientRect(hwnd, &rect);
            g_uiManager->resize(rect.right, rect.bottom);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        if (g_uiManager) {
            g_uiManager->render();

            RECT rect;
            GetClientRect(hwnd, &rect);

            SetDIBitsToDevice(hdc,
                0, 0, rect.right, rect.bottom,
                0, 0, 0, rect.bottom,
                g_uiManager->getBuffer(),
                &g_bmi,
                DIB_RGB_COLORS);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (g_uiManager) {
            g_uiManager->handleMouseDown(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (g_uiManager) {
            g_uiManager->handleMouseUp(x, y);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (g_uiManager) {
            g_uiManager->handleMouseMove(x, y);
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

int main() {
    // 使用 Unicode 入口点  
    //return wWinMain(GetModuleHandle(nullptr), nullptr, nullptr, SW_SHOWDEFAULT);
//int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // 初始化 ThorVG  
    tvg::Initializer::init(4);

    // 加载字体  
    UIElement::loadFonts(L"E:\\Dev\\project\\AT01\\res\\siyuan.ttf", L"E:\\Dev\\project\\AT01\\res\\fuhao.ttf");

	HINSTANCE hInstance = GetModuleHandleW(nullptr);
    // 注册窗口类  
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ThorVGUIFramework";
    RegisterClassExW(&wc);

    // 创建窗口  
    g_hwnd = CreateWindowExW(
        0, L"ThorVGUIFramework", L"ThorVG UI Framework Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hwnd) return -1;

    // 初始化 UI 管理器  
    g_uiManager = new UIManager();
    RECT rect;
    GetClientRect(g_hwnd, &rect);
    g_uiManager->initialize(rect.right, rect.bottom);

    // 设置 BITMAPINFO  
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = rect.right;
    g_bmi.bmiHeader.biHeight = -rect.bottom;  // 负值表示从上到下  
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    // 创建 UI 控件  
    auto button = std::make_unique<UIButton>(L"点击我");
    button->x = 50;
    button->y = 50;
    button->width = 150;
    button->onClick = []() {
        ODD("按钮被点击了!\n");
        };
    g_uiManager->addElement(std::move(button));

    auto label = std::make_unique<UILabel>(L"这是一个标签");
    label->x = 50;
    label->y = 120;
    label->setColor(255, 255, 255);
    label->setFontSize(18.0f);
    g_uiManager->addElement(std::move(label));

    auto slider = std::make_unique<UISlider>();
    slider->x = 50;
    slider->y = 180;
    slider->width = 300;
    slider->onValueChanged = [](float value) {
        wchar_t buf[64];
        swprintf_s(buf, L"滑块值: %.2f", value);
        SetWindowTextW(g_hwnd, buf);
        };
    g_uiManager->addElement(std::move(slider));

    auto progressBar = std::make_unique<UIProgressBar>();
    progressBar->x = 50;
    progressBar->y = 240;
    progressBar->width = 300;
    progressBar->setProgress(0.65f);
    g_uiManager->addElement(std::move(progressBar));

    auto tooltip = std::make_unique<UITooltip>(L"这是提示信息");
    tooltip->x = 400;
    tooltip->y = 50;
    tooltip->width = 200;
    g_uiManager->addElement(std::move(tooltip));

    // 创建示例图片 (纯色矩形)  
    uint32_t* imageData = new uint32_t[100 * 100];
    for (int i = 0; i < 100 * 100; i++) {
        imageData[i] = 0xFF4080FF;  // ARGB: 蓝色  
    }
    auto image = std::make_unique<UIImage>();
    image->x = 400;
    image->y = 150;
    image->width = 100;
    image->height = 100;
    image->loadRawPixels(imageData, 100, 100);
    g_uiManager->addElement(std::move(image));

    // 显示窗口  
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // 消息循环  
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理  
    delete[] imageData;
    delete g_uiManager;
    tvg::Initializer::term();

    return (int)msg.wParam;
}



#endif // 0



#if 1
#include "UIFramework.h"
#define ODD(...) printf(__VA_ARGS__)

// ===== 歌曲数据结构 =====    
struct Song {
    std::string title;
    std::string artist;
    std::string duration;
};

// ===== 虚拟歌曲列表数据 =====    
std::vector<Song> generateSongList() {
    return {
        {u8"夜曲", u8"周杰伦", u8"3:45"},
        {u8"七里香", u8"周杰伦", u8"4:58"},
        {u8"稻香", u8"周杰伦", u8"3:43"},
        {u8"青花瓷", u8"周杰伦", u8"3:58"},
        {u8"告白气球", u8"周杰伦", u8"3:35"},
        {u8"晴天", u8"周杰伦", u8"4:29"},
        {u8"简单爱", u8"周杰伦", u8"4:30"},
        {u8"东风破", u8"周杰伦", u8"5:13"},
        {u8"菊花台", u8"周杰伦", u8"4:54"},
        {u8"发如雪", u8"周杰伦", u8"4:59"},
        {u8"千里之外", u8"周杰伦", u8"4:14"},
        {u8"听妈妈的话", u8"周杰伦", u8"4:23"},
        {u8"彩虹", u8"周杰伦", u8"4:24"},
        {u8"蒲公英的约定", u8"周杰伦", u8"3:59"},
        {u8"说好的幸福呢", u8"周杰伦", u8"4:00"},
        {u8"兰亭序", u8"周杰伦", u8"4:15"},
        {u8"烟花易冷", u8"周杰伦", u8"4:21"},
        {u8"明明就", u8"周杰伦", u8"4:35"},
        {u8"红尘客栈", u8"周杰伦", u8"4:31"},
        {u8"算什么男人", u8"周杰伦", u8"4:33"}
    };
}

int main(int argc, char** argv) {
    // 获取 HINSTANCE  
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // 创建 Application 实例  
    Application app(hInstance);

    // 初始化应用  
    if (!app.initialize(L"ThorVG UI Framework Demo", 800, 600)) {
        MessageBoxW(nullptr, L"应用初始化失败", L"错误", MB_OK | MB_ICONERROR);
        return -1;
    }

    // 加载字体资源  
    // 注意: 这里使用 Windows 系统字体作为示例  
    // 您需要根据实际情况修改字体路径  
    if (!app.loadResources(L"E:\\Dev\\project\\AT01\\res\\siyuan.ttf", L"E:\\Dev\\project\\AT01\\res\\fuhao.ttf")) {
        MessageBoxW(nullptr, L"字体加载失败", L"错误", MB_OK | MB_ICONERROR);
        return -1;
    }

    // 获取 UI 管理器并创建控件  
    auto uiManager = app.getUIManager();

    // 创建按钮  
    auto button = std::make_unique<UIButton>(L"点击我");
    button->x = 50;
    button->y = 50;
    button->width = 150;
    button->onClick = [&app]() {
        ODD("按钮被点击了!\n");
        };
    uiManager->addElement(std::move(button));

    // 创建标签并保存原始指针  
    auto labelPtr = std::make_unique<UILabel>(L"这是一个标签");
    labelPtr->x = 50;
    labelPtr->y = 120;
    labelPtr->setColor(255, 255, 255);
    labelPtr->setFontSize(18.0f);
    UILabel* labelRaw = labelPtr.get();  // 保存原始指针  
    uiManager->addElement(std::move(labelPtr));  // 先添加  

    // 创建滑块并使用保存的原始指针  
    auto slider = std::make_unique<UISlider>();
    slider->x = 50;
    slider->y = 180;
    slider->width = 300;
    slider->onValueChanged = [labelRaw](float value) {  // 使用原始指针  
        wchar_t buf[64];
        swprintf_s(buf, L"滑块值: %.2f", value);
        labelRaw->setText(buf);
        };
    uiManager->addElement(std::move(slider));

    // 创建进度条  
    auto progressBar = std::make_unique<UIProgressBar>();
    progressBar->x = 50;
    progressBar->y = 240;
    progressBar->width = 300;
    progressBar->setProgress(0.65f);
    uiManager->addElement(std::move(progressBar));

    // 创建提示框  
    auto tooltip = std::make_unique<UITooltip>(L"这是提示信息");
    tooltip->x = 400;
    tooltip->y = 50;
    tooltip->width = 200;
    uiManager->addElement(std::move(tooltip));

    // 创建图片控件 (使用原始像素数据)  
    uint32_t* imageData = new uint32_t[100 * 100];
    for (int i = 0; i < 100 * 100; i++) {
        // 创建渐变效果  
        int x = i % 100;
        int y = i / 100;
        uint8_t r = static_cast<uint8_t>(x * 255 / 100);
        uint8_t g = static_cast<uint8_t>(y * 255 / 100);
        uint8_t b = 128;
        imageData[i] = 0xFF000000 | (r << 16) | (g << 8) | b;  // ARGB  
    }

    auto image = std::make_unique<UIImage>();
    image->x = 400;
    image->y = 150;
    image->width = 100;
    image->height = 100;
    image->loadRawPixels(imageData, 100, 100);
    uiManager->addElement(std::move(image));

    // 运行消息循环  
    int result = app.run();

    // 清理  
    delete[] imageData;

    return result;
}
#endif // 1
