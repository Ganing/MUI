# MUI 开发（改进版）

本文件面向未来与 AI 或合作者沟通时使用，目标是让问题描述更精准、上下文更完整，从而加速开发与问题定位。

---

## 项目速览
- 名称：MUI（轻量级 UI 框架）
- 技术栈：Win32 + OpenGL + ThorVG(GlCanvas) + C++17 (VS2022)
- 平台：Windows x64
- 编码：GB2312（代码页936），工程使用宽字符集
- 关键文件：`mui.h`（核心控件/布局/管理器）、`mui.cpp`（实现，若存在）、`Renderer3D.h` / `Renderer3D.cpp`、测试样例 `mui_test*.cpp`
- 渲染模式：即时模式（每帧 `canvas->remove()` -> push -> update -> draw(false) -> sync）

---

## 问题/任务描述
- 标题：修复 `UITextInput` 在删除多字节字符时破坏字符串
- 背景：`UITextInput` 内部用 UTF-8 存储并用字节偏移 `cursorPos`
- 目标：删除键按“字符”而非“字节”，不损坏 UTF-8 串
- 文件：`mui.h` 中 `UITextInput` 的 `onKeyDown` / `onChar`
- 复现步骤：输入中文字符，按 Backspace，观察显示
- 期望：Backspace 删除一个 Unicode codepoint
- 尝试：将 `cursorPos` 改为 UTF-16 单位（失败）
- 限制：保持内部 UTF-8 存储，兼容 VS2022
- 优先级：高
- 交付：修改后的文件 + 编译无误报告

---

## NEXT
短期（立即开始）：
1. 学习智能指针用法，改造 `UIManager` 和 `UIGroup` 的 `children` 管理
1.（`UIManager::elements`、`UIGroup::children`）替换为智能指针管理，避免内存泄漏（高）
1. 修复 `UIButton` 文本显示越界问题
2. 为控件增加 `dirty` 标记并实现 Scene 缓存策略，减少每帧重建（高）
3. 修复 `UITextInput` 的光标与删除逻辑，按 Unicode codepoint 操作（中）

中期（可并行）：
- 优化 `hitTest`，支持 z-order 和可配置点击区域
- 添加基础主题/样式系统（Theme）
- 将 3D 渲染与 2D UI 抽象化接口

长期：
- 动画系统、可移植渲染后端、可访问性支持、国际化与 IME 支持



## TASK

- [ ] 开机自启，快捷键绑定，内置资源打包调用，【知识点参见H5-MUI-node.md】
- [x] 设置窗口图标。
- [x] 设置默认字体，文本字体【思源黑体SourceHanSansCN-Regular.ttf=>siyuan.ttf】，符号字体【seguisym.ttf=>fuhao】并打包为RCDATA。
- [x] 应用最小化后，歌词列表等布局失效
- [ ] 搜索框-TextInput在没有焦点时颜色要淡一些
- [ ] 任务栏图标没正常显示
- [ ] JZON，config.json，如需进一步美化歌词（渐隐、滚动、居中对齐或卡拉 OK 高亮），可在 LyricView::buildScene() 中增加颜色/透明度随距离变化或绘制裁剪与渐变。

### 封面|歌词相关处理

**使用stb_image.h和stb_image_write.h处理图片**

``` c++
// 在mui.h中添加
// stb静态库
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
```

**设置默认存储路径**

``` c++
// 获取当前可执行文件所在的目录路径
static std::wstring getExecutableDirectory() {
    wchar_t buf[MAX_PATH] = { 0 };  // 缓冲区，用于存储完整路径
    // 获取当前可执行文件的完整路径（包括文件名）
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) return L"";  // 获取失败时返回空字符串
    
    // 将路径字符串转换为filesystem路径对象
    std::filesystem::path p(buf);
    // 返回父目录（即去掉文件名后的路径）
    return p.parent_path().wstring();
}

/**
 * @brief 创建数据目录结构（data/cover, data/image, data/text）
 * @param basePath 基础路径，通常为当前工作目录
 * @return 创建成功返回true，失败返回false
 */
bool createDataSubdirs(const std::wstring& basePath) {
    try {
        // 构建各级目录路径
        std::filesystem::path dataDir = std::filesystem::path(basePath) / L"data";
        std::filesystem::path coverDir = dataDir / L"cover";
        std::filesystem::path imageDir = dataDir / L"image";
        std::filesystem::path textDir = dataDir / L"text";
        
        // 创建所有目录（如果已存在则不会重复创建）
        bool dataCreated = std::filesystem::create_directories(dataDir);
        bool coverCreated = std::filesystem::create_directories(coverDir);
        bool imageCreated = std::filesystem::create_directories(imageDir);
        bool textCreated = std::filesystem::create_directories(textDir);
        
        // 检查所有目录是否创建成功或已存在
        bool dataExists = std::filesystem::exists(dataDir) && std::filesystem::is_directory(dataDir);
        bool coverExists = std::filesystem::exists(coverDir) && std::filesystem::is_directory(coverDir);
        bool imageExists = std::filesystem::exists(imageDir) && std::filesystem::is_directory(imageDir);
        bool textExists = std::filesystem::exists(textDir) && std::filesystem::is_directory(textDir);
        
        return dataExists && coverExists && imageExists && textExists;
    }
    catch (const std::filesystem::filesystem_error& ex) {
        ODD(L"创建数据子目录失败: %s\n", ex.what());
        return false;
    }
    catch (const std::exception& ex) {
        ODD(L"创建数据子目录时发生异常: %s\n", ex.what());
        return false;
    }
}
```

**创建|提取|保存封面到默认数据目录**

``` c++
/**
 * @brief 从歌曲文件名生成对应的封面文件名（通过检查实际文件确定扩展名）
 * @param coverDir 封面目录
 * @param songPath 歌曲文件路径
 * @param index 封面索引（用于多封面情况）
 * @return 封面文件路径（如果存在），否则返回空字符串
 */
std::wstring findCoverFile(const std::filesystem::path& coverDir, const std::wstring& songPath, int index = 0) {
    std::filesystem::path path(songPath);
    std::wstring stem = path.stem().wstring();
    
    // 可能的图片扩展名
    std::vector<std::wstring> extensions = {L".jpg", L".jpeg", L".png", L".gif", L".bmp"};
    
    // 生成可能的文件名
    std::wstring baseName = (index == 0) ? stem : stem + L"_" + std::to_wstring(index);
    
    // 检查每个可能的扩展名
    for (const auto& ext : extensions) {
        std::wstring filename = baseName + ext;
        std::filesystem::path coverPath = coverDir / filename;
        
        if (std::filesystem::exists(coverPath)) {
            std::wstring result = coverPath.wstring();
            std::replace(result.begin(), result.end(), L'\\', L'/');
            return result;
        }
    }
    
    return L""; // 没有找到对应的封面文件
}



/**
 * @brief 创建默认封面图片
 * @param coverDir 封面目录路径
 * @return 成功返回true，失败返回false
 */
bool createDefaultCover(const std::filesystem::path& coverDir) {
    std::filesystem::path defaultCoverPath = coverDir / L"defaultcover.png";
    
    // 如果默认封面已存在，直接返回成功
    if (std::filesystem::exists(defaultCoverPath)) {
        return true;
    }
    
    // 使用stb_image_write创建默认封面
    // 这里创建一个简单的128x128蓝色渐变图片作为默认封面
    const int width = 128;
    const int height = 128;
    const int channels = 3; // RGB
    
    // 分配图片数据内存
    unsigned char* image_data = new unsigned char[width * height * channels];
    
    // 生成蓝色渐变图片
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = (y * width + x) * channels;
            
            // 简单的蓝色渐变
            float fx = static_cast<float>(x) / width;
            float fy = static_cast<float>(y) / height;
            
            image_data[index] = static_cast<unsigned char>(fx * 100);     // R
            image_data[index + 1] = static_cast<unsigned char>(fy * 100); // G
            image_data[index + 2] = static_cast<unsigned char>(200);       // B
        }
    }
    
    // 使用stb_image_write保存为PNG
    // 注意：stb_image_write需要窄字符串路径
    std::string narrowPath = defaultCoverPath.string();
    int result = stbi_write_png(narrowPath.c_str(), width, height, channels, image_data, width * channels);
    
    // 释放内存
    delete[] image_data;
    
    if (result == 0) {
        ODD(L"创建默认封面失败: %ls\n", defaultCoverPath.wstring().c_str());
        return false;
    }
    
    ODD(L"创建默认封面成功: %ls\n", defaultCoverPath.wstring().c_str());
    return true;
}

/**
 * @brief 为歌曲列表提取和管理封面图片
 * @param songs 歌曲列表
 * @param dataParentPath data目录的父路径
 * @return 成功提取的封面数量
 */
int extractAndManageCovers(std::vector<Song>& songs, const std::wstring& dataParentPath) {
    // 创建目录结构
    if (!createDataSubdirs(dataParentPath)) {
        ODD(L"创建数据目录失败，无法提取封面\n");
        return 0;
    }
    
    std::filesystem::path coverDir = std::filesystem::path(dataParentPath) / L"data" / L"cover";
    
    // 创建默认封面
    if (!createDefaultCover(coverDir)) {
        ODD(L"警告: 创建默认封面失败\n");
    }
    
    int totalCoversExtracted = 0;
    
    for (auto& song : songs) {
        // 如果歌曲已经有封面路径，跳过处理
        if (!song.coverPaths.empty()) {
            ODD(L"歌曲已有封面，跳过处理: %ls\n", song.filePath.c_str());
            continue;
        }
        
        // 首先检查data/cover目录下是否已有该歌曲的封面
        bool foundExistingCovers = false;
        
        // 根据embeddedCoverCount检查封面
        for (int i = 0; i < song.embeddedCoverCount; i++) {
            std::wstring existingCoverPath = findCoverFile(coverDir, song.filePath, i);
            
            if (!existingCoverPath.empty()) {
                song.coverPaths.push_back(existingCoverPath);
                foundExistingCovers = true;
                ODD(L"找到已存在的封面: %ls\n", existingCoverPath.c_str());
            }
        }
        
        // 如果data/cover目录下没有封面，尝试从音频文件提取
        if (!foundExistingCovers && song.embeddedCoverCount > 0) {
            ODD(L"尝试从音频文件提取封面: %ls (数量: %d)\n", 
                song.filePath.c_str(), song.embeddedCoverCount);
            
            // 根据embeddedCoverCount提取封面
            for (int i = 0; i < song.embeddedCoverCount; i++) {
                std::wstring extractedPath = extractCover(song.filePath, coverDir.wstring(), i);
                if (!extractedPath.empty()) {
                    song.coverPaths.push_back(extractedPath);
                    totalCoversExtracted++;
                    ODD(L"成功提取封面: %ls\n", extractedPath.c_str());
                } else {
                    ODD(L"提取封面失败 (索引 %d): %ls\n", i, song.filePath.c_str());
                    // 即使某个索引失败，也继续尝试其他索引
                }
            }
        }
        
        // 如果没有找到任何封面，使用默认封面
        if (song.coverPaths.empty()) {
            std::filesystem::path defaultCoverPath = coverDir / L"defaultcover.png";
            if (std::filesystem::exists(defaultCoverPath)) {
                std::wstring defaultCover = defaultCoverPath.wstring();
                std::replace(defaultCover.begin(), defaultCover.end(), L'\\', L'/');
                song.coverPaths.push_back(defaultCover);
                ODD(L"使用默认封面: %ls\n", defaultCover.c_str());
            }
        }
    }
    
    ODD(L"封面处理完成，共提取 %d 个新封面\n", totalCoversExtracted);
    return totalCoversExtracted;
}
```

### UIImage

``` c++
typedef struct OBitmap_t {
    void* data;             // 图像原始数据
    int width;              // 原始宽度
    int height;             // 原始宽度
    int bpp;				// 每个像素需要多少个比特bits
    int pitch;				//(((8*img.width) + 31) >> 5) << 2; 如果4字节对齐，每行需要的字节byte数目
    int pixSize;			// 像素数据 大小=pitch*height
    int tag;               // 字体渲染中的top度量，或者纹理的Mipmap levels, 1 by default
    int format;             // 像素格式，和OGL的对齐。
}OBitmap;
```



### 歌词显示的流程和原理

``` 
核心流程
LyricView 控件的歌词显示基于以下流程:

歌词解析: 使用 parseLrc() 函数解析 LRC 格式的歌词文本,将每行歌词转换为 LyricLine 结构(包含时间戳 t 和文本 text)

时间同步: 通过 setTimeProvider() 绑定播放器的 getCurrentPosition() 方法,在 update() 中实时获取当前播放时间

当前行定位: 在 update() 中遍历歌词数组,找到当前时间对应的歌词行索引 curIndex

滚动动画: 当 curIndex 变化时,触发平滑滚动动画,通过 baseIndexF 插值实现歌词列表的垂直移动

渲染: 在 render() 中使用 ThorVG 的 Text::gen() 创建文本对象,根据每行与当前行的距离设置不同的颜色和透明度,实现高亮效果

关键技术点
卡拉OK效果: 当前行使用 curColor (白色),其他行使用 otherColor (灰色),通过透明度渐变实现平滑过渡
居中对齐: 当前歌词行始终显示在容器中央,通过调整 baseIndexF 实现
性能优化: 只渲染可见范围内的歌词行,避免不必要的文本对象创建
```

### Figma Frame 容器的属性和功能

``` 
Frame 的核心属性
布局属性:
x, y, width, height: 位置和尺寸
padding: 内边距 (top, right, bottom, left)
gap: 子元素间距 (Auto Layout 模式)
Auto Layout:
direction: 水平/垂直排列
alignment: 对齐方式 (start, center, end, stretch)
distribution: 分布方式 (packed, space-between, space-around)
视觉样式:
fill: 背景填充 (纯色/渐变/图片)
stroke: 边框 (颜色、宽度、位置)
cornerRadius: 圆角半径
opacity: 不透明度
blendMode: 混合模式
约束和响应式:
constraints: 子元素相对父容器的约束
resizing: 自适应调整行为
裁剪和溢出:
clipsContent: 是否裁剪超出边界的内容
overflow: 滚动行为
```



``` c++
namespace MUI {  
  
class UIFrame : public UIElement {  
private:  
    // 子元素列表  
    std::vector<std::unique_ptr<UIElement>> children;  
      
    // 布局属性  
    struct {  
        float top = 0, right = 0, bottom = 0, left = 0;  
    } padding;  
      
    float gap = 0.0f;  // 子元素间距  
      
    enum class Direction { Horizontal, Vertical } direction = Direction::Vertical;  
    enum class Alignment { Start, Center, End, Stretch } alignment = Alignment::Start;  
      
    // 视觉样式  
    Color fillColor = Color(255, 255, 255, 255);  
    bool enableFill = true;  
    Color strokeColor = Color(200, 200, 200, 255);  
    float strokeWidth = 1.0f;  
    bool enableStroke = false;  
    float cornerRadius = 0.0f;  
    float opacity = 1.0f;  
      
    // 裁剪和滚动  
    bool clipsContent = false;  
    float scrollOffsetX = 0.0f;  
    float scrollOffsetY = 0.0f;  
  
public:  
    UIFrame() {  
        rect = {0, 0, 100, 100};  
    }  
      
    ~UIFrame() noexcept override {  
        children.clear();  
    }  
      
    // 添加子元素  
    void addChild(std::unique_ptr<UIElement> child) {  
        children.push_back(std::move(child));  
        updateLayout();  
    }  
      
    // 移除子元素  
    void removeChild(UIElement* child) {  
        children.erase(  
            std::remove_if(children.begin(), children.end(),  
                [child](const auto& c) { return c.get() == child; }),  
            children.end()  
        );  
        updateLayout();  
    }  
      
    // 设置布局方向  
    void setDirection(Direction dir) {  
        direction = dir;  
        updateLayout();  
    }  
      
    // 设置对齐方式  
    void setAlignment(Alignment align) {  
        alignment = align;  
        updateLayout();  
    }  
      
    // 设置内边距  
    void setPadding(float top, float right, float bottom, float left) {  
        padding.top = top;  
        padding.right = right;  
        padding.bottom = bottom;  
        padding.left = left;  
        updateLayout();  
    }  
      
    // 设置间距  
    void setGap(float g) {  
        gap = g;  
        updateLayout();  
    }  
      
    // 设置样式  
    void setStyle(Color fill, Color stroke, float strokeW, float corner) {  
        fillColor = fill;  
        strokeColor = stroke;  
        strokeWidth = strokeW;  
        cornerRadius = corner;  
    }  
      
    // 启用/禁用裁剪  
    void setClipsContent(bool clips) {  
        clipsContent = clips;  
    }  
      
    // 更新布局  
    void updateLayout() {  
        if (children.empty()) return;  
          
        float contentX = rect.x + padding.left;  
        float contentY = rect.y + padding.top;  
        float availableW = rect.w - padding.left - padding.right;  
        float availableH = rect.h - padding.top - padding.bottom;  
          
        if (direction == Direction::Horizontal) {  
            // 水平布局  
            float currentX = contentX;  
            for (auto& child : children) {  
                if (!child->visible) continue;  
                  
                child->rect.x = currentX;  
                child->rect.y = contentY;  
                  
                // 根据对齐方式调整 Y 坐标  
                if (alignment == Alignment::Center) {  
                    child->rect.y += (availableH - child->rect.h) / 2.0f;  
                } else if (alignment == Alignment::End) {  
                    child->rect.y += availableH - child->rect.h;  
                } else if (alignment == Alignment::Stretch) {  
                    child->rect.h = availableH;  
                }  
                  
                currentX += child->rect.w + gap;  
            }  
        } else {  
            // 垂直布局  
            float currentY = contentY;  
            for (auto& child : children) {  
                if (!child->visible) continue;  
                  
                child->rect.x = contentX;  
                child->rect.y = currentY;  
                  
                // 根据对齐方式调整 X 坐标  
                if (alignment == Alignment::Center) {  
                    child->rect.x += (availableW - child->rect.w) / 2.0f;  
                } else if (alignment == Alignment::End) {  
                    child->rect.x += availableW - child->rect.w;  
                } else if (alignment == Alignment::Stretch) {  
                    child->rect.w = availableW;  
                }  
                  
                currentY += child->rect.h + gap;  
            }  
        }  
    }  
      
    void render(tvg::Scene* parent) override {  
        if (!visible) return;  
          
        // 渲染背景  
        if (enableFill || enableStroke) {  
            auto bg = tvg::Shape::gen();  
            bg->appendRect(rect.x, rect.y, rect.w, rect.h, cornerRadius, cornerRadius);  
              
            if (enableFill) {  
                bg->fill(fillColor.r, fillColor.g, fillColor.b, fillColor.a);  
            }  
              
            if (enableStroke) {  
                bg->strokeFill(strokeColor.r, strokeColor.g, strokeColor.b, strokeColor.a);  
                bg->strokeWidth(strokeWidth);  
            }  
              
            bg->opacity(static_cast<uint8_t>(opacity * 255));  
            parent->push(std::move(bg));  
        }  
          
        // 渲染子元素  
        if (clipsContent) {  
            // 创建裁剪区域  
            auto clipScene = tvg::Scene::gen();  
            auto clipMask = tvg::Shape::gen();  
            clipMask->appendRect(rect.x, rect.y, rect.w, rect.h, cornerRadius, cornerRadius);  
            clipMask->fill(255, 255, 255, 255);  
            clipScene->composite(std::move(clipMask), tvg::CompositeMethod::ClipPath);  
              
            for (auto& child : children) {  
                if (child->visible) {  
                    child->render(clipScene.get());  
                }  
            }  
              
            parent->push(std::move(clipScene));  
        } else {  
            for (auto& child : children) {  
                if (child->visible) {  
                    child->render(parent);  
                }  
            }  
        }  
    }  
      
    bool hitTest(float px, float py) override {  
        return rect.contains(px, py);  
    }  
      
    void onMMove(float px, float py) override {  
        for (auto& child : children) {  
            if (child->hitTest(px, py)) {  
                child->onMMove(px, py);  
            }  
        }  
    }  
      
    void onMDown(float px, float py) override {  
        for (auto& child : children) {  
            if (child->hitTest(px, py)) {  
                child->onMDown(px, py);  
            }  
        }  
    }  
      
    void onMWheel(float px, float py, int delta) override {  
        for (auto& child : children) {  
            if (child->hitTest(px, py)) {  
                child->onMWheel(px, py, delta);  
            }  
        }  
    }  
};  
  
} // namespace MUI
```



``` c++
// 创建垂直布局的 Frame  
auto frame = std::make_unique<MUI::UIFrame>();  
frame->rect = MUI::Rect(20, 20, 300, 400);  
frame->setDirection(MUI::UIFrame::Direction::Vertical);  
frame->setAlignment(MUI::UIFrame::Alignment::Center);  
frame->setPadding(10, 10, 10, 10);  
frame->setGap(8.0f);  
frame->setStyle(  
    MUI::Color(240, 240, 240, 255),  // 背景色  
    MUI::Color(200, 200, 200, 255),  // 边框色  
    1.0f,                             // 边框宽度  
    5.0f                              // 圆角  
);  
frame->setClipsContent(true);  
  
// 添加子控件  
auto label = std::make_unique<MUI::UILabel>("标题");  
label->rect = MUI::Rect(0, 0, 280, 30);  
frame->addChild(std::move(label));  
  
auto button = std::make_unique<MUI::UIButton>("按钮");  
button->rect = MUI::Rect(0, 0, 100, 40);  
frame->addChild(std::move(button));  
  
ui->addElement(std::move(frame));
```



``` 
Notes
这个 UIFrame 设计参考了 Figma 的 Frame 容器,提供了:

Auto Layout: 自动排列子元素 (水平/垂直)
对齐和分布: 灵活的对齐方式
内边距和间距: 精确控制布局
裁剪支持: 使用 ThorVG 的 CompositeMethod::ClipPath 实现内容裁剪
事件传递: 自动将鼠标事件传递给子元素
与 Figma 的主要区别是简化了约束系统和响应式逻辑,专注于核心的容器布局功能。
```

### 任务栏图标未显示 — 原因与处理

•	问题：程序中既有 MUI05.cpp 的图标加载逻辑，又有 mui.h::Application::createWindow 的处理，导致混淆与重复，最终任务栏图标仍未正确显示。
•	解决：把图标逻辑统一到 mui.h::Application::createWindow（你已在 mui.h 修改），并从 MUI05.cpp 中移除所有图标相关代码，避免重复加载/冲突。
•	影响文件：mui.h（你修改）、MUI05.cpp（移除图标加载代码）。

### 窗口“透明”看不到后面内容 — 理由与改造

•	问题：使用 DWM BlurBehind + 圆角并不能实现“真正穿透”——OpenGL 直接绘制且像素不是按割层透明合成，因此看不到后面的窗口内容。
•	解决：将方案从 DWM Blur 转为分层窗口的统一半透明：在 MUI05.cpp 使用 WS_EX_LAYERED + SetLayeredWindowAttributes（整窗 alpha）并设置类背景刷以避免闪烁。
•	备注：这是整窗半透明（trade-off）。若需逐像素“只显示 UI”/真穿透，需重构为离屏渲染（FBO -> 读像素 -> UpdateLayeredWindow），这会较大改动渲染管线。
•	影响文件：MUI05.cpp（替换透明实现）；注意保留清理 g_hWindowBackground。

### 启动后自动播放第一首但列表/封面未同步显示 — 修复

•	问题：程序启动时自动 playSongByIndex(0)，但播放列表中没有被标为选中，且 CoverImage 没显示第一首封面。
•	解决：在创建完 UI 并将 PlayerBinder 添加到 UIManager 后调用：
•	player.playSongByIndex(0);
•	playlist->setSelectedIndex(0);
•	binder->updateSongInfo();
这样会同步列表选中、标题/歌手、封面与歌词。
•	影响文件：MUI05.cpp（在初始化流程末尾加入上述调用）。

### 资源/生命周期处理

•	问题：新增窗口背景刷和区域资源需要释放以免泄漏。
•	解决：在程序退出处释放 g_hWindowBackground（DeleteObject），并调用 MUI::CoverImage::clearCache() 与 tvg::Initializer::term() 保证 ThorVG/位图资源清理。
•	影响文件：MUI05.cpp（退出处增加释放逻辑）。
构建状态：上述改动后项目已成功生成（编译通过）。

### UIGroup 与 UIFrame 的作用与使用场景

简要说明：

- `UIGroup`：布局容器，负责子控件的组织与布局管理。它持有 `layoutManager`（例如 `FlexLayout` / `GridLayout`）和每个子控件的 `LayoutParams`，用于控制权重、对齐、间距和 padding。`UIGroup` 关注“如何摆放子元素”，自身通常不绘制背景或边框。

- `UIFrame`：视觉容器 / 面板，负责绘制背景、边框与圆角，并可选择对其子元素进行裁剪（`clipChildren`）。`UIFrame` 关注“外观”和可选的裁剪/遮罩语义，不承担复杂布局算法（但可以包含子元素）。

共同点与差异：

- 共同点：
  - 都继承自 `UIElement`，可以包含子元素并参与事件传播与渲染。

- 差异：
  - 职责：`UIGroup` = 布局；`UIFrame` = 装饰/视觉（+可选裁剪）。
  - 接口：`UIGroup` 提供 `setLayoutManager()`、`setChildLayoutParams()` 等布局相关 API；`UIFrame` 提供 `setBackgroundColor()`、`setBorderColor()`、`setCornerRadius()`、`setClipChildren()` 等视觉相关 API。
  - 渲染：`UIFrame::buildScene()` 绘制矩形与边框；`UIGroup::buildScene()` 主要创建子元素群组并依赖 `ILayout::doLayout()` 在 `update()` 中设置子元素 bounds。

典型使用场景与示例：

1. 面板 + 布局（推荐组合）
   - 场景：窗口左侧需要带背景与边框的控件区，内部按列/行排列若干控件。
   - 做法：使用 `UIFrame` 作为视觉容器，再在其内部添加 `UIGroup`（或将 `UIGroup` 作为 `UIFrame` 的子元素），并设置 `FlexLayout`/`GridLayout` 来管理子控件。
   - 代码示例：
     - `auto frame = new MUI::UIFrame();`
     - `auto group = new MUI::UIGroup(); group->setLayoutManager(std::make_shared<MUI::FlexLayout>(...));`
     - `frame->addChild(group); group->addChild(child, params);`

2. 简单工具栏/按钮组（仅布局，无视觉装饰）
   - 场景：顶部或侧边的一排按钮需要按权重拉伸或等距排列。
   - 做法：直接用 `UIGroup` + `FlexLayout`，通过 `LayoutParams::weight` 控制伸缩。例如 `btnGroup` 在 `mui_test04.cpp` 即是该用法。

3. 弹出/卡片式控件（需要背景与裁剪）
   - 场景：对话框、工具提示或卡片式控件，需要圆角背景并限制子内容显示区域。
   - 做法：使用 `UIFrame`（设置 `cornerRadius`、`setClipChildren(true)`），将需裁剪的内容作为子元素加入。注意：ThorVG 的实际裁剪能力依赖后端版本，可能需要基于 mask/clip API 做额外实现。

建议：

- 若只关心“如何摆放”，使用 `UIGroup`；若需要“视觉样式/背景/裁剪”，使用 `UIFrame`。
- 常见模式是把两者组合：`UIFrame`（视觉）包裹 `UIGroup`（布局），既有外观又有自动布局管理，适用于大多数界面面板场景。

### 裁剪（Clipping）与遮罩（Mask）语义

简要定义：

- 裁剪（clipping）：将绘制限制在一个几何边界内（通常为矩形或圆角矩形），超出边界的像素被丢弃（不绘制）。裁剪通常为“硬边界”。

- 遮罩/蒙版（mask）：使用一张形状或灰度/alpha 图控制目标内容的可见度与透明度。遮罩支持任意形状和软边（渐变透明），能实现更复杂的视觉效果。

区别与成本：

- 裁剪：实现简单且性能开销小（例如 GPU scissor）。适合矩形或简单圆角场景。通常不会影响子元素的布局计算，仅影响视觉输出与命中检测（可选）。

- 遮罩：功能更强、视觉表现更灵活，但通常需要离屏合成或模板操作，性能开销大。适用于非矩形、软边或渐变透明的场景。

常见用途与场景：

- 滚动视图 / 列表：裁剪滚动内容到视口，避免溢出元素显示。
- 圆角卡片 / 面板：用裁剪实现严格的圆角边界，或用遮罩实现软化边缘和渐隐效果。
- 弹出层 / Tooltip：限制弹出内容不越界并创建羽化/投影效果（遮罩）。
- 图片裁剪与形状编辑：遮罩用于显示特定形状内的图片或实现可编辑裁剪框。
- 动画与转场：遮罩可做擦除、蒙版动画或复杂合成效果。

实现建议（针对 MUI）：

- 优先实现矩形/圆角裁剪（scissor 或等价 GPU 操作），因为简单且高效。把 `UIFrame::clipChildren` 语义明确为至少提供视觉裁剪。
- 提供可选的事件裁剪开关（例如 `clipVisual` / `clipInput` 或 `clipInput` 参数），决定裁剪是否同时阻止被裁剪区域的事件响应。
- 当需要非矩形/软边效果时，采用离屏缓冲区 + 遮罩合成的方式，并对遮罩做缓存，避免每帧重建。
- 在 hitTest 中优先判断父容器的裁剪区域：若点位于视觉裁剪之外，应被视为不可交互。
- 若底层 ThorVG 不直接支持遮罩/clip API，可采用：
  - GPU 层 scissor/stencil 实现硬裁剪；
  - 或先渲染子内容到离屏纹理，再按遮罩合成回主画布（适用于软边或任意形状）。

性能注意：

- 遮罩实现可能触发离屏渲染、额外的纹理带宽和内存消耗，应只在必要时启用并做好结果缓存。
- 裁剪在每帧均可低成本执行，但若与大量子元素结合，仍需配合 Scene 缓存 / 脏标记策略以降低重绘成本。

短结：

- 裁剪用于限制绘制区域（高效、简单）；遮罩用于复杂的可见性控制与视觉效果（灵活但昂贵）。
- 在 MUI 里，先实现矩形/圆角裁剪，后续按需补充遮罩合成并加入缓存策略与事件处理控制。

------

### 设置开机自启

开机自启可以通过将程序路径添加到注册表的 Run 项中实现



```c++
#include <windows.h>
#include <tchar.h>
#include <string>

// 设置开机自启
bool SetAutoStart(const std::wstring& appName, const std::wstring& appPath, bool enable) {
    HKEY hKey;
    const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    // 打开注册表项
if (RegOpenKeyEx(HKEY_CURRENT_USER, runKey, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
    return false;
}

if (enable) {
    // 添加程序路径到注册表
    if (RegSetValueEx(hKey, appName.c_str(), 0, REG_SZ, 
                      reinterpret_cast<const BYTE*>(appPath.c_str()), 
                      (appPath.size() + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }
} else {
    // 删除注册表项
    if (RegDeleteValue(hKey, appName.c_str()) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }
}

RegCloseKey(hKey);
return true;
}
```


示例调用

```c++
int main() {
    std::wstring appName = L"MyApp";
    std::wstring appPath = L"C:\\Path\\To\\YourApp.exe";
    // 启用开机自启
if (SetAutoStart(appName, appPath, true)) {
    MessageBox(nullptr, L"开机自启已设置", L"提示", MB_OK);
} else {
    MessageBox(nullptr, L"设置开机自启失败", L"错误", MB_OK);
}

return 0;
}
```
说明
1.	注册表路径：
?	HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run：当前用户的开机自启。
?	HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Run：所有用户的开机自启（需要管理员权限）。
2.	参数说明：
?	appName：程序名称，用于注册表键名。
?	appPath：程序的完整路径。
3.	启用/禁用：
?	enable = true：添加到注册表，启用开机自启。
?	enable = false：从注册表删除，禁用开机自启。



### 快捷键绑定

Win32 应用程序可以通过 RegisterHotKey 函数绑定全局快捷键。

``` C++
#include <windows.h>
#include <tchar.h>
#include <iostream>

// 快捷键 ID
#define HOTKEY_ID 1

// 窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            MessageBox(hwnd, L"快捷键触发！", L"提示", MB_OK);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int main() {
    // 注册窗口类
    const wchar_t CLASS_NAME[] = L"SampleWindowClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // 创建窗口
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"快捷键示例", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
                               nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!hwnd) {
        std::cerr << "窗口创建失败！" << std::endl;
        return -1;
    }

    // 注册全局快捷键 (Ctrl + Alt + A)
    if (!RegisterHotKey(hwnd, HOTKEY_ID, MOD_CONTROL | MOD_ALT, 0x41)) { // 0x41 = 'A'
        MessageBox(hwnd, L"快捷键注册失败！", L"错误", MB_OK);
        return -1;
    }

    ShowWindow(hwnd, SW_SHOW);

    // 消息循环
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 注销快捷键
    UnregisterHotKey(hwnd, HOTKEY_ID);

    return 0;
}
```

说明
1.	RegisterHotKey 参数：
?	hwnd：窗口句柄，接收快捷键消息的窗口。
?	id：快捷键 ID，用于标识快捷键。
?	fsModifiers：修饰键（如 MOD_CONTROL、MOD_ALT、MOD_SHIFT）。
?	vk：虚拟键码（如 0x41 表示 A 键）。
2.	快捷键触发：
?	当快捷键被触发时，窗口会收到 WM_HOTKEY 消息。
3.	注销快捷键：
?	在程序退出前调用 UnregisterHotKey，释放快捷键。
4.	全局快捷键：
?	RegisterHotKey 注册的快捷键是全局的，即使程序最小化或不在前台也能触发。
---
注意事项
1.	权限：
?	设置开机自启时，修改 HKEY_LOCAL_MACHINE 需要管理员权限。
?	注册全局快捷键不需要管理员权限。
2.	快捷键冲突：
?	如果快捷键已被其他程序占用，RegisterHotKey 会失败。
3.	兼容性：
?	以上代码适用于 Windows 10/11，使用 C++17 编译。
4.	资源管理：
?	确保在程序退出时释放注册表项和快捷键。
---
通过以上方法，你可以在 Win32 应用程序中实现开机自启和快捷键绑定功能。



### RC打包

在 Win32 应用程序中，将资源（如图标、字体、Lua 脚本等）打包到可执行文件（EXE）的资源文件（RC）中，通常需要以下工作流程：

1. 准备资源文件
?	确保所有资源文件（如 .ico、.ttf、.lua 等）已准备好，并放置在项目的资源目录中。
?	示例：
``` c
 resources/
 ├── app_icon.ico
 ├── default_font.ttf
 ├── script.lua
 └── theme.json
```
2. **创建资源脚本文件（.rc）**
?	创建一个资源脚本文件（如 resources.rc），用于描述资源的类型和标识符。
?	示例 resources.rc：
``` 
 // 图标资源
 IDI_APP_ICON ICON "resources/app_icon.ico"
 // 字体资源
 IDF_DEFAULT_FONT FONT "resources/default_font.ttf"
 // Lua 脚本资源
 IDS_SCRIPT LUA "resources/script.lua"
 // JSON 主题文件
 IDT_THEME JSON "resources/theme.json"
```
3. **在 Visual Studio 中添加资源脚本**
?	将 resources.rc 文件添加到 Visual Studio 项目中：
	1.	右键项目 -> 添加 -> 现有项。
	2.	选择 resources.rc 文件并添加到项目中。
?	Visual Studio 会自动将资源脚本编译为 .res 文件，并链接到最终的 EXE 文件中。
4. 在代码中加载资源
?	使用 Win32 API（如 FindResource、LoadResource、LockResource）加载嵌入的资源。
?	示例代码：
```c++
 #include <windows.h>
 #include <iostream>

 // 加载资源
 void LoadEmbeddedResource(int resourceId, const wchar_t* resourceType) {
     HMODULE hModule = GetModuleHandle(nullptr);
     HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(resourceId), resourceType);
     if (!hResource) {
         std::wcerr << L"资源未找到！" << std::endl;
         return;
     }

     HGLOBAL hLoadedResource = LoadResource(hModule, hResource);
     if (!hLoadedResource) {
         std::wcerr << L"资源加载失败！" << std::endl;
         return;
     }

     void* pResourceData = LockResource(hLoadedResource);
     DWORD resourceSize = SizeofResource(hModule, hResource);

     if (pResourceData && resourceSize > 0) {
         std::wcout << L"资源加载成功，大小: " << resourceSize << L" 字节" << std::endl;
         // 处理资源数据（如保存到文件或直接使用）
     }
 }

 int main() {
     // 加载图标资源
     LoadEmbeddedResource(IDI_APP_ICON, RT_ICON);

     // 加载字体资源
     LoadEmbeddedResource(IDF_DEFAULT_FONT, L"FONT");

     // 加载 Lua 脚本资源
     LoadEmbeddedResource(IDS_SCRIPT, L"LUA");

     return 0;
     }
```
5. **预留内存资源（可选）**
?	如果需要预留内存块，可以在资源脚本中添加自定义资源类型。
?	示例 resources.rc：
```
 IDR_MEMORY_BLOCK RCDATA "resources/memory_block.bin"
```
6. **编译并验证**
?	编译项目，确保资源已正确嵌入到 EXE 文件中。
?	使用工具（如 Resource Hacker 或 Visual Studio 的资源查看器）检查 EXE 文件中的资源。
---
**资源类型说明**

```
| 资源类型       | 描述                     | Win32 常量 | 
|--------------|--------------------------|----------------| 
| ICON         | 图标资源                  | RT_ICON      | 
| FONT         | 字体资源                  | 自定义类型      | 
| LUA          | Lua 脚本资源（自定义类型）   | 自定义类型      | 
| JSON         | JSON 文件资源（自定义类型）  | 自定义类型      | 
| RCDATA       | 二进制数据（任意格式）       | RT_RCDATA    |
```

**注意事项**

1.	资源标识符：
?	标识符可以是整数（如 101）或字符串（如 "APP_ICON"）。
?	推荐使用宏定义标识符，便于管理：
```
 #define IDI_APP_ICON 101
 #define IDF_DEFAULT_FONT 102
 #define IDS_SCRIPT 103
```
2.	资源路径：
?	在资源脚本中使用相对路径，确保资源文件在项目目录中。
3.	资源大小限制：
?	Windows 对嵌入资源的大小没有严格限制，但过大的资源可能导致 EXE 文件体积膨胀。
4.	动态加载：
?	嵌入的资源可以在运行时解压到临时文件夹，供程序动态加载（如字体文件）。
---
**扩展功能**

1.	动态字体加载：
?	使用 AddFontResourceEx 或 AddFontMemResourceEx 动态加载嵌入的字体资源。
?	示例：
```c++
 #include <windows.h>
 #include <iostream>

 void LoadFontFromResource(int resourceId) {
     HMODULE hModule = GetModuleHandle(nullptr);
     HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(resourceId), L"FONT");
     if (!hResource) return;

     HGLOBAL hLoadedResource = LoadResource(hModule, hResource);
     void* pFontData = LockResource(hLoadedResource);
     DWORD fontSize = SizeofResource(hModule, hResource);

     DWORD numFonts = 0;
     HANDLE hFont = AddFontMemResourceEx(pFontData, fontSize, nullptr, &numFonts);
     if (hFont) {
         std::wcout << L"字体加载成功！" << std::endl;
     }
 }
```
2.	Lua 脚本执行：
?	嵌入 Lua 脚本后，使用 Lua 解释器（如 LuaJIT）加载并执行脚本。
3.	资源更新：
?	使用 UpdateResource API 动态更新 EXE 文件中的资源（需管理员权限）。
---
通过以上流程，你可以将图标、字体、Lua 脚本等资源打包到 EXE 文件中，并在运行时动态加载和使用这些资源。

### 修改更新RCDATA

在 Win32 应用程序中，RCDATA 类型的资源可以在运行时读取、修改，并根据需要写回到文件或更新到 EXE 文件中。以下是实现的完整流程

1. 读取资源内容
使用 Win32 API FindResource、LoadResource 和 LockResource 读取嵌入的资源内容。
示例代码：

```c++
#include <windows.h>
#include <iostream>
#include <fstream>

// 读取资源内容
bool ReadResource(int resourceId, const wchar_t* resourceType, std::vector<char>& outData) {
    HMODULE hModule = GetModuleHandle(nullptr);
    HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(resourceId), resourceType);
    if (!hResource) {
        std::cerr << "资源未找到！" << std::endl;
        return false;
    }

    HGLOBAL hLoadedResource = LoadResource(hModule, hResource);
    if (!hLoadedResource) {
        std::cerr << "资源加载失败！" << std::endl;
        return false;
    }

    void* pResourceData = LockResource(hLoadedResource);
    DWORD resourceSize = SizeofResource(hModule, hResource);

    if (pResourceData && resourceSize > 0) {
        outData.assign((char*)pResourceData, (char*)pResourceData + resourceSize);
        return true;
    }

    std::cerr << "资源锁定失败！" << std::endl;
    return false;
}
```

2. 修改资源内容
读取资源后，可以对其内容进行修改。例如，将资源数据保存到文件、修改后再写回。
示例代码

``` C
void ModifyResource(std::vector<char>& data) {
    // 示例：将资源内容的前 4 个字节设置为 "TEST"
    if (data.size() >= 4) {
        data[0] = 'T';
        data[1] = 'E';
        data[2] = 'S';
        data[3] = 'T';
    }
}
```

3. 写入修改后的资源到文件
将修改后的资源数据保存到文件中。
示例代码：

```C
bool WriteToFile(const std::string& filePath, const std::vector<char>& data) {
    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile) {
        std::cerr << "文件写入失败！" << std::endl;
        return false;
    }

    outFile.write(data.data(), data.size());
    outFile.close();
    return true;
}
```

4. 更新资源到 EXE 文件
如果需要将修改后的资源写回到 EXE 文件中，可以使用 BeginUpdateResource、UpdateResource 和 EndUpdateResource。
示例代码：

``` c++
bool UpdateResourceInExe(const std::wstring& exePath, int resourceId, const wchar_t* resourceType, const std::vector<char>& data) {
    HANDLE hUpdate = BeginUpdateResource(exePath.c_str(), FALSE);
    if (!hUpdate) {
        std::cerr << "无法打开 EXE 文件进行更新！" << std::endl;
        return false;
    }

    if (!UpdateResource(hUpdate, resourceType, MAKEINTRESOURCE(resourceId), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), 
                        (void*)data.data(), data.size())) {
        std::cerr << "资源更新失败！" << std::endl;
        EndUpdateResource(hUpdate, TRUE); // 放弃更新
        return false;
    }

    if (!EndUpdateResource(hUpdate, FALSE)) {
        std::cerr << "无法完成资源更新！" << std::endl;
        return false;
    }

    return true;
}
```

完整示例
以下是一个完整的示例，展示如何读取、修改并写回资源：

``` c++
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

// 资源 ID 和类型
#define IDR_MEMORY_BLOCK 101
#define RESOURCE_TYPE L"RCDATA"

int main() {
    // 读取资源
    std::vector<char> resourceData;
    if (!ReadResource(IDR_MEMORY_BLOCK, RESOURCE_TYPE, resourceData)) {
        return -1;
    }

    std::cout << "资源读取成功，大小: " << resourceData.size() << " 字节" << std::endl;

    // 修改资源
    ModifyResource(resourceData);

    // 保存到文件
    if (!WriteToFile("modified_memory_block.bin", resourceData)) {
        return -1;
    }
    std::cout << "修改后的资源已保存到文件！" << std::endl;

    // 更新资源到 EXE
    std::wstring exePath = L"YourApp.exe"; // 替换为实际 EXE 文件路径
    if (!UpdateResourceInExe(exePath, IDR_MEMORY_BLOCK, RESOURCE_TYPE, resourceData)) {
        return -1;
    }
    std::cout << "资源已更新到 EXE 文件！" << std::endl;

    return 0;
}
```

注意事项
1.	权限：
?	更新 EXE 文件时需要管理员权限。
?	确保 EXE 文件未被其他进程占用。
2.	资源大小：
?	修改后的资源大小不能超过原始大小，否则可能导致资源更新失败。
3.	备份：
?	在更新 EXE 文件前，建议备份原始文件。
4.	资源标识符：
?	确保资源 ID 和类型与资源脚本中的定义一致。
5.	动态加载资源：
?	如果资源频繁修改，建议将资源存储在外部文件中，避免频繁更新 EXE 文件。
---
通过以上步骤，你可以在程序中读取、修改并写回嵌入的资源。

### 用户权限

1. 使用 UAC 提示要求管理员权限
通过 ShellExecute 或 CreateProcess 启动自身，并指定 runas 动作，触发 UAC 提示。
示例代码：

``` c++
#include <windows.h>
#include <tchar.h>

// 提升到管理员权限
bool RunAsAdmin(const wchar_t* exePath, const wchar_t* params) {
    SHELLEXECUTEINFO sei = { sizeof(SHELLEXECUTEINFO) };
    sei.lpVerb = L"runas"; // 指定以管理员权限运行
    sei.lpFile = exePath;  // 可执行文件路径
    sei.lpParameters = params; // 命令行参数
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteEx(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            MessageBox(nullptr, L"用户取消了管理员权限请求。", L"提示", MB_OK);
        } else {
            MessageBox(nullptr, L"无法请求管理员权限。", L"错误", MB_OK);
        }
        return false;
    }
    return true;
}

// 示例调用
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(nullptr, exePath, MAX_PATH);

    if (!RunAsAdmin(exePath, L"--elevated")) {
        return -1;
    }

    MessageBox(nullptr, L"程序已以管理员权限运行。", L"提示", MB_OK);
    return 0;
}
```

说明
1.	ShellExecuteEx 参数：
?	lpVerb = "runas"：指定以管理员权限运行。
?	lpFile：可执行文件路径。
?	lpParameters：传递给程序的命令行参数。
2.	错误处理：
?	ERROR_CANCELLED：用户取消了 UAC 提示。
?	其他错误：可能是路径无效或权限不足。
3.	自我提升：
?	程序会重新启动自身，并以管理员权限运行。
---
2. 使用应用程序清单文件（Manifest）声明管理员权限
通过在应用程序的清单文件中声明 requireAdministrator，程序启动时会自动触发 UAC 提示。
示例清单文件（app.manifest）：

``` c
<?xml version="1.0" encoding="utf-8"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="requireAdministrator" uiAccess="false" />
      </requestedPrivileges>
    </security>
  </trustInfo>
</assembly>
```

使用步骤
1.	添加清单文件：
?	在 Visual Studio 中右键项目 -> 添加 -> 新建项 -> 应用程序清单文件。
?	将上述内容粘贴到清单文件中。
2.	编译链接：
?	Visual Studio 会自动将清单文件嵌入到 EXE 中。
3.	效果：
?	程序启动时会自动触发 UAC 提示，要求管理员权限。
---
3. 检测当前是否具有管理员权限
在程序中检测当前是否以管理员权限运行。
示例代码：

``` c
#include <windows.h>
#include <tchar.h>
#include <iostream>

// 检测是否以管理员权限运行
bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;

    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

// 示例调用
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    if (IsRunningAsAdmin()) {
        MessageBox(nullptr, L"程序已以管理员权限运行。", L"提示", MB_OK);
    } else {
        MessageBox(nullptr, L"程序未以管理员权限运行。", L"提示", MB_OK);
    }
    return 0;
}
```

4. 更新资源或修改系统设置时的权限处理
如果程序需要动态修改 EXE 文件资源或更改系统设置（如注册表、服务等），需要确保以管理员权限运行。
示例：动态更新资源
?	在调用 BeginUpdateResource 或修改注册表时，先检测是否具有管理员权限。
?	如果没有权限，提示用户重新以管理员权限运行。
---
注意事项
1.	UAC 提示：
?	使用 ShellExecuteEx 或清单文件触发 UAC 提示。
?	用户必须确认 UAC 提示，程序才能以管理员权限运行。
2.	权限检测：
?	在执行需要管理员权限的操作前，先调用 IsRunningAsAdmin 检测权限。
3.	兼容性：
?	以上方法适用于 Windows Vista 及更高版本（UAC 引入后）。
4.	安全性：
?	避免滥用管理员权限，仅在必要时请求权限。
---
通过以上方法，你可以在 Win32 应用程序中实现管理员权限的请求和检测。推荐使用清单文件声明权限，结合 ShellExecuteEx 实现动态权限提升。

------



### 窗口图标

在 Win32 应用程序中，窗口的图标主要分为以下几种类型，并可以通过相关 API 设置：

**窗口图标的类型**

1.	应用程序图标（Application Icon）：
?	显示在任务栏和窗口标题栏左上角。
?	通常是一个较大的图标（如 32x32 或 48x48）。
2.	小图标（Small Icon）：
?	显示在窗口标题栏左上角（窗口缩小时也会显示）。
?	通常是一个较小的图标（如 16x16）。
3.	任务栏图标（Taskbar Icon）：
?	显示在任务栏中，通常与应用程序图标一致。
4.	通知区域图标（Tray Icon）：
?	显示在系统托盘（通知区域）中，通常用于后台运行的程序。
---
**相关 API**
以下是设置不同类型图标的主要 API：

1. **LoadIcon**
?	用于加载资源文件中的图标。
?	示例：
``` 
HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
```
2. **LoadImage**
?	用于加载图标、光标或位图，支持从文件或资源加载。
?	示例：

```
HICON hIcon = (HICON)LoadImage(hInstance, L"icon.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
```
3. **SendMessage**
?	用于向窗口发送消息，设置窗口的图标。
?	常用消息：
?	WM_SETICON：设置窗口的图标。
?	ICON_BIG：设置大图标（任务栏、Alt+Tab）。
?	ICON_SMALL：设置小图标（标题栏）。
?	示例：

```
SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
```
4. **Shell_NotifyIcon**
?	用于在通知区域（系统托盘）添加、修改或删除图标。
?	示例：
```
NOTIFYICONDATA nid = {};
nid.cbSize = sizeof(NOTIFYICONDATA);
nid.hWnd = hwnd;
nid.uID = 1;
nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
nid.hIcon = hIcon;
nid.uCallbackMessage = WM_USER + 1;
wcscpy_s(nid.szTip, L"My Application");
Shell_NotifyIcon(NIM_ADD, &nid);
```
**设置窗口图标的完整示例**
以下是一个完整的代码示例，展示如何设置应用程序图标和小图标：

```c++
#include <windows.h>
#include <tchar.h>

// 资源标识符
#define IDI_APP_ICON 101

// 窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 注册窗口类
    const wchar_t CLASS_NAME[] = L"SampleWindowClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON)); // 设置应用程序图标

    RegisterClass(&wc);

    // 创建窗口
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"窗口图标示例", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 500, 400,
                               nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        return -1;
    }

    // 设置小图标
    HICON hSmallIcon = (HICON)LoadImage(hInstance, L"small_icon.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

    ShowWindow(hwnd, nCmdShow);

    // 消息循环
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
```

另外方法：在favicon.io中将图片转化为icon格式再在sln目录下改名替换。

**如何在程序中使用图像库(比如stbi_image.h)将png格式图片设置为窗口图标？**

``` C
// 2. 加载 PNG 图片
define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <windows.h>
#include <iostream>

HICON LoadPngAsIcon(const char* filePath, int iconWidth, int iconHeight) {
    int width, height, channels;
    unsigned char* imageData = stbi_load(filePath, &width, &height, &channels, 4); // 强制加载为 RGBA

    if (!imageData) {
        std::cerr << "无法加载 PNG 图片: " << filePath << std::endl;
        return nullptr;
    }

    // 确保图像尺寸与目标图标尺寸一致
    if (width != iconWidth || height != iconHeight) {
        std::cerr << "图片尺寸不匹配: 需要 " << iconWidth << "x" << iconHeight << "，但加载的是 " << width << "x" << height << std::endl;
        stbi_image_free(imageData);
        return nullptr;
    }

    // 创建位图信息头
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = width;
    bi.bV5Height = -height; // 注意：负值表示自上而下的位图
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32; // 每像素 32 位（RGBA）
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    // 创建 DIB（设备无关位图）
    void* pBits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hBitmap = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (!hBitmap) {
        std::cerr << "无法创建位图！" << std::endl;
        stbi_image_free(imageData);
        return nullptr;
    }

    // 将 PNG 数据复制到位图内存
    memcpy(pBits, imageData, width * height * 4);
    stbi_image_free(imageData);

    // 创建图标蒙版（全透明）
    HBITMAP hMonoBitmap = CreateBitmap(width, height, 1, 1, nullptr);

    // 创建图标信息
    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE; // TRUE 表示图标，FALSE 表示光标
    iconInfo.hbmMask = hMonoBitmap;
    iconInfo.hbmColor = hBitmap;

    // 创建图标
    HICON hIcon = CreateIconIndirect(&iconInfo);

    // 清理资源
    DeleteObject(hMonoBitmap);
    DeleteObject(hBitmap);

    return hIcon;
}

3. 设置窗口图标

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"SampleWindowClass";

    // 注册窗口类
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // 创建窗口
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"PNG 图标示例", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                               nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        return -1;
    }

    // 加载 PNG 并设置为窗口图标
    HICON hIcon = LoadPngAsIcon("icon.png", 32, 32); // 加载 32x32 的图标
    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon); // 设置大图标
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon); // 设置小图标
    }

    ShowWindow(hwnd, nCmdShow);

    // 消息循环
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理图标资源
    if (hIcon) {
        DestroyIcon(hIcon);
    }

    return 0;
}


```



### MEJSON

``` json
{
  "resources": {
    "fonts": [ "default_font.ttf" ],
    "images": [ "prev.svg", "next.svg", "play.svg", "pause.svg", "default_cover.png" ],
    "theme": "default_theme.json"
  },
  "scenes": {
    "mscene": {
      "mode": "mini",
      "description": "Mini mode: A compact version of the software with minimal UI elements.",
      "ui_elements": {
        "buttons": [ "prev", "next", "play", "pause" ],
        "progress_bar": "playback",
        "album_cover": "default_cover.png",
        "text_info": [ "song_name", "artist_info", "time_info" ]
      },
      "shortcuts": {
        "prev": "Ctrl+Left",
        "next": "Ctrl+Right",
        "play_pause": "Space",
        "volume_up": "Ctrl+Up",
        "volume_down": "Ctrl+Down"
      }
    },
    "nscene": {
      "mode": "normal",
      "description": "Normal mode: The standard version of the software with full UI functionality.",
      "shortcuts": {
        "prev": "Ctrl+Left",
        "next": "Ctrl+Right",
        "play_pause": "Space",
        "stop": "Ctrl+S",
        "volume_up": "Ctrl+Up",
        "volume_down": "Ctrl+Down"
      }
    },
    "wscene": {
      "mode": "wallpaper",
      "description": "Wallpaper mode: A fullscreen version of the software designed for wallpaper display.",
      "shortcuts": {
        "exit_wallpaper": "Esc"
      }
    }
  },
  "global_shortcuts": {
    "help": "F1",
    "settings": "Ctrl+P",
    "exit": "Alt+F4"
  }
}
```



### Whisper.cpp配置和测试

``` 
从GitHub克隆whisper.cpp到成功运行实时中文语音转录的完整步骤总结

====================
第一部分：环境准备
开发环境信息
操作系统：Windows 11 x64
开发工具：Visual Studio 2022
编译标准：C++17
字符集：Unicode
编码：UTF-8 with BOM (代码页65001)
GPU硬件：NVIDIA GeForce RTX 4070
必需软件安装
Git（用于克隆代码）
CMake（用于构建项目）
CUDA Toolkit 12.9（用于GPU加速）
Visual Studio 2022（包含C++开发工具）
====================
第二部分：下载和构建
步骤1：克隆whisper.cpp仓库
命令：
git clone https://github.com/ggml-org/whisper.cpp.git
cd whisper.cpp

步骤2：下载模型文件
由于网络原因无法从Hugging Face下载，改用ModelScope下载已转换好的GGML格式模型。

选择模型：

对于中文识别，必须选择多语言模型（不带.en后缀）
推荐：ggml-large-v3.bin (3.10GB) - 最高准确性
备选：ggml-medium.bin (1.53GB) - 平衡性能和准确性
文件存放位置：
将下载的.bin文件放在 whisper.cpp\models\ 目录下

步骤3：检查CUDA安装状态
问题：执行nvcc --version提示命令无法识别
原因：CUDA Toolkit已安装但未添加到环境变量

解决方法：
a) 确认CUDA已安装（路径：C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9\bin\nvcc.exe）
b) 添加环境变量：

打开系统环境变量设置（Win+X → 系统 → 高级系统设置 → 环境变量）
编辑系统变量Path，添加：
C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9\bin
C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9\libnvvp
新建系统变量CUDA_PATH，值为：
C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9
c) 重新打开PowerShell验证：
nvcc --version
echo $env:CUDA_PATH
步骤4：构建whisper.cpp（启用CUDA）
命令：
cmake -B build -DGGML_CUDA=1 -DCMAKE_CUDA_ARCHITECTURES="89"
cmake --build build -j --config Release

说明：

-DGGML_CUDA=1 启用CUDA支持
-DCMAKE_CUDA_ARCHITECTURES="89" 指定RTX 4070的架构代码
生成的可执行文件位于：build\bin\Release\whisper-cli.exe
步骤5：验证GPU加速
运行测试命令：
.\build\bin\Release\whisper-cli.exe -m models\ggml-large-v3.bin -f samples\jfk.wav

成功标志：

输出显示：ggml_cuda_init: found 1 CUDA devices: Device 0: NVIDIA GeForce RTX 4070
系统信息显示：CUDA : ARCHS = 890
性能提升：40.9秒音频仅需7.5秒处理（5.5倍实时速度）
====================
第三部分：静态库集成
步骤6：构建静态库
命令：
cmake -B staticbuild -DBUILD_SHARED_LIBS=OFF -DGGML_CUDA=1 -DCMAKE_CUDA_ARCHITECTURES="89"
cmake --build staticbuild -j --config Release

生成的静态库文件：

staticbuild\src\Release\whisper.lib (2.06 MB)
staticbuild\ggml\src\Release\ggml.lib (287 KB)
staticbuild\ggml\src\Release\ggml-base.lib (2.00 MB)
staticbuild\ggml\src\Release\ggml-cpu.lib (1.97 MB)
staticbuild\ggml\src\ggml-cuda\Release\ggml-cuda.lib
步骤7：解决CUDA库链接错误
问题：链接时出现cublasCreate_v2等符号无法解析的错误
原因：缺少cuBLAS库的链接

解决方法：
在项目中添加以下库链接（使用#pragma comment）：
#pragma comment(lib, "whisper.lib")
#pragma comment(lib, "ggml.lib")
#pragma comment(lib, "ggml-base.lib")
#pragma comment(lib, "ggml-cpu.lib")
#pragma comment(lib, "ggml-cuda.lib")
#pragma comment(lib, "cudart_static.lib")
#pragma comment(lib, "cublas.lib")
#pragma comment(lib, "cublasLt.lib")
#pragma comment(lib, "cuda.lib")

并在VS2022项目属性中添加库目录：
C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9\lib\x64

====================
第四部分：实时转录实现
步骤8：集成miniaudio库

miniaudio是单头文件库，只需包含miniaudio.h
定义MINIAUDIO_IMPLEMENTATION在包含前
步骤9：实现回环设备捕获（系统音频输出）
关键配置：

设备类型：ma_device_type_loopback（而非ma_device_type_capture）
音频格式：ma_format_f32（32位浮点PCM）
采样率：16000 Hz（Whisper要求）
声道数：1（单声道）
步骤10：解决中文乱码问题
问题：转录结果显示为乱码（如：濂? 鍝?）
原因：whisper.cpp返回UTF-8编码文本，但Windows控制台默认使用GBK编码

解决方法（推荐方案）：
在main函数开始时设置控制台编码：
SetConsoleOutputCP(CP_UTF8);
SetConsoleCP(CP_UTF8);

注意：不要使用_setmode(_fileno(stdout), _O_U16TEXT)，因为会导致混用窄字符和宽字符输出时崩溃

步骤11：添加文件输出功能
使用std::ofstream将转录结果同时保存到文本文件：

使用std::ios::app追加模式
每个片段立即flush()确保数据不丢失
使用std::mutex保护文件写入操作（线程安全）
====================
第五部分：性能验证
最终性能指标：

音频时长：40.9秒
处理时间：7.457秒
实时因子RTF：0.182
实时倍速：5.5倍
编码时间：181ms（相比CPU的46575ms提升256倍）
实时性判断标准：

RTF < 1.0 表示可以实时处理
RTF < 0.5 满足实时应用要求
当前RTF = 0.182 远超实时要求
====================
关键问题总结
问题1：CUDA Toolkit已安装但nvcc命令无法识别
解决：添加CUDA的bin目录到系统PATH环境变量

问题2：链接时出现cublas相关符号无法解析
解决：添加cublas.lib、cublasLt.lib、cudart_static.lib、cuda.lib的链接

问题3：中文转录结果显示乱码
解决：使用SetConsoleOutputCP(CP_UTF8)设置控制台编码，避免使用_setmode导致的混用输出崩溃

问题4：需要捕获系统音频输出而非麦克风
解决：使用ma_device_type_loopback而非ma_device_type_capture

问题5：找不到生成的静态库文件
解决：静态库位于staticbuild\src\Release和staticbuild\ggml\src\Release目录

====================
最终代码结构
项目文件组织：
您的项目\
├── libs\
│ ├── whisper.lib
│ ├── ggml.lib
│ ├── ggml-base.lib
│ ├── ggml-cpu.lib
│ └── ggml-cuda.lib
├── include\
│ ├── whisper.h
│ ├── ggml.h
│ ├── ggml-alloc.h
│ ├── ggml-backend.h
│ ├── ggml-cpu.h
│ └── gguf.h
└── models\
└── ggml-large-v3.bin

主要代码特点：

使用miniaudio捕获系统音频输出（回环设备）
使用whisper.cpp进行实时语音转录
支持CUDA GPU加速
正确处理UTF-8中文编码
同时输出到控制台和文本文件
支持空格键手动停止录制
运行要求：

Windows 10或更高版本（回环捕获需要）
NVIDIA GPU（CUDA支持）
系统必须有正在播放的音频才能捕获
使用方法：
编译运行程序后，系统会自动捕获正在播放的音频并实时转录为中文文字，结果同时显示在控制台并保存到loopback_transcription.txt文件中。按空格键可停止录制。
```



``` c++
/****************************************************************************
 * 标题: Whisper实时系统音频转录（回环捕获）
 * 文件: whisper_loopback_transcription.cpp
 * 版本: 1.3
 * 作者: AEGLOVE
 * 日期: 2025-11-16
 * 功能: 使用miniaudio捕获系统音频输出（回环设备），通过whisper.cpp实现实时转录
 * 依赖: whisper.cpp, miniaudio, CUDA
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
****************************************************************************/

#if 1  

 
#include "miniaudio.h"  
#include "whisper.h"  

#include <iostream>  
#include <fstream>  
#include <vector>  
#include <mutex>  
#include <cstdint>  
#include <string>  
#include <windows.h>  
#include <conio.h>  // 用于_kbhit()和_getch()  

#pragma comment(lib, "whisper.lib")  
#pragma comment(lib, "ggml.lib")  
#pragma comment(lib, "ggml-base.lib")  
#pragma comment(lib, "ggml-cpu.lib")  
#pragma comment(lib, "ggml-cuda.lib")  
#pragma comment(lib, "cudart_static.lib")  
#pragma comment(lib, "cublas.lib")  
#pragma comment(lib, "cublasLt.lib")  
#pragma comment(lib, "cuda.lib")  

// 音频缓冲区配置  
constexpr int32_t SAMPLE_RATE = 16000;      // Whisper要求16kHz采样率  
constexpr int32_t BUFFER_SIZE_MS = 5000;    // 5秒音频窗口  
constexpr int32_t STEP_SIZE_MS = 500;       // 每500ms处理一次  

// 全局文件输出流  
std::ofstream g_outputFile;
std::mutex g_fileMutex;

class WhisperRealtimeTranscriber {
private:
    whisper_context* ctx;
    std::vector<float> audioBuffer;
    std::mutex bufferMutex;
    ma_device device;
    bool isRunning;
    DWORD startTime;

    // miniaudio回调函数 - 捕获系统音频输出  
    static void audioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* transcriber = static_cast<WhisperRealtimeTranscriber*>(pDevice->pUserData);
        const float* input = static_cast<const float*>(pInput);

        if (input) {
            std::lock_guard<std::mutex> lock(transcriber->bufferMutex);
            transcriber->audioBuffer.insert(
                transcriber->audioBuffer.end(),
                input,
                input + frameCount
            );
        }

        (void)pOutput; // 不需要输出  
    }

public:
    WhisperRealtimeTranscriber(const char* modelPath) : ctx(nullptr), isRunning(false), startTime(0) {
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = true;

        ctx = whisper_init_from_file_with_params(modelPath, cparams);
        if (!ctx) {
            throw std::runtime_error("Failed to load whisper model");
        }

        std::cout << "Whisper模型加载成功" << std::endl;
    }

    ~WhisperRealtimeTranscriber() {
        stop();
        if (ctx) {
            whisper_free(ctx);
        }
    }

    bool start() {
        // 配置回环捕获设备（系统音频输出）  
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_loopback);
        deviceConfig.capture.format = ma_format_f32;      // 32位浮点格式  
        deviceConfig.capture.channels = 1;                  // 单声道（Whisper要求）  
        deviceConfig.sampleRate = SAMPLE_RATE;        // 16kHz采样率  
        deviceConfig.dataCallback = audioCallback;      // 设置回调函数  
        deviceConfig.pUserData = this;               // 传递this指针  
        deviceConfig.periodSizeInFrames = 1024;             // 缓冲区大小  

        // 初始化音频设备  
        ma_result result = ma_device_init(nullptr, &deviceConfig, &device);
        if (result != MA_SUCCESS) {
            std::cerr << "设备初始化失败: " << ma_result_description(result) << std::endl;
            std::cerr << "可能原因:" << std::endl;
            std::cerr << "1. 系统音频服务未运行" << std::endl;
            std::cerr << "2. 缺少音频输出设备" << std::endl;
            std::cerr << "3. 权限不足（需要管理员权限）" << std::endl;
            std::cerr << "4. 系统不支持回环捕获（需要Windows 10或更高版本）" << std::endl;
            return false;
        }

        // 启动音频设备  
        result = ma_device_start(&device);
        if (result != MA_SUCCESS) {
            std::cerr << "设备启动失败: " << ma_result_description(result) << std::endl;
            ma_device_uninit(&device);
            return false;
        }

        isRunning = true;
        startTime = GetTickCount();
        std::cout << "开始捕获系统音频输出..." << std::endl;
        return true;
    }

    void stop() {
        if (isRunning) {
            ma_device_uninit(&device);
            isRunning = false;
            std::cout << "停止捕获" << std::endl;
        }
    }

    void processAudio() {
        std::vector<float> processingBuffer;

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (audioBuffer.size() < SAMPLE_RATE * BUFFER_SIZE_MS / 1000) {
                return; // 缓冲区数据不足  
            }

            // 复制音频数据用于处理  
            size_t bufferSize = SAMPLE_RATE * BUFFER_SIZE_MS / 1000;
            processingBuffer.assign(
                audioBuffer.end() - bufferSize,
                audioBuffer.end()
            );

            // 保留最后一部分数据用于下次处理（重叠）  
            size_t keepSize = SAMPLE_RATE * STEP_SIZE_MS / 1000;
            audioBuffer.erase(
                audioBuffer.begin(),
                audioBuffer.end() - keepSize
            );
        }

        // 配置whisper参数  
        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.language = "zh";           // 中文识别  
        wparams.translate = false;         // 不翻译，仅转录  
        wparams.print_realtime = false;
        wparams.print_progress = false;
        wparams.print_timestamps = true;
        wparams.n_threads = 4;

        // 执行转录  
        if (whisper_full(ctx, wparams, processingBuffer.data(), processingBuffer.size()) != 0) {
            std::cerr << "转录失败" << std::endl;
            return;
        }

        // 输出转录结果  
        const int32_t n_segments = whisper_full_n_segments(ctx);
        for (int32_t i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            const int64_t t0 = whisper_full_get_segment_t0(ctx, i);
            const int64_t t1 = whisper_full_get_segment_t1(ctx, i);

            char buffer[4096];
            snprintf(buffer, sizeof(buffer), "[%6.2f -> %6.2f] %s\n",
                t0 / 100.0f, t1 / 100.0f, text);

            // 输出到控制台  
            printf("%s", buffer);

            // 同时写入文件  
            std::lock_guard<std::mutex> lock(g_fileMutex);
            if (g_outputFile.is_open()) {
                g_outputFile << buffer;
                g_outputFile.flush();
            }
        }
    }

    bool isActive() const { return isRunning; }

    // 显示录制状态  
    void displayStatus() {
        DWORD elapsed = GetTickCount() - startTime;
        float seconds = elapsed / 1000.0f;

        // 估算缓冲区大小（字节）  
        size_t bufferBytes;
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            bufferBytes = audioBuffer.size() * sizeof(float);
        }
        float bufferSizeMB = bufferBytes / (1024.0f * 1024.0f);

        printf("\r| %2d:%02d             | %.2f MB               |",
            (int)seconds / 60, (int)seconds % 60, bufferSizeMB);
    }
};

int main(int argc, char** argv) {
    // 设置控制台UTF-8编码  
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 打开输出文件  
    g_outputFile.open("loopback_transcription.txt", std::ios::out | std::ios::app);
    if (!g_outputFile.is_open()) {
        std::cerr << "无法打开输出文件" << std::endl;
        return 1;
    }

    // 写入文件头  
    g_outputFile << "=== 系统音频转录开始 ===" << std::endl;
    g_outputFile << "时间: " << __DATE__ << " " << __TIME__ << std::endl;
    g_outputFile << std::endl;

    try {
        WhisperRealtimeTranscriber transcriber("models/ggml-large-v3.bin");

        if (!transcriber.start()) {
            g_outputFile.close();
            return 1;
        }

        std::cout << "实时转录已启动（捕获系统音频输出）" << std::endl;
        std::cout << "结果将保存到: loopback_transcription.txt" << std::endl;
        std::cout << "按空格键停止录制..." << std::endl;
        std::cout << "+------------------+-------------------------+" << std::endl;
        std::cout << "| 录制时间          | 缓冲区大小              |" << std::endl;
        std::cout << "+------------------+-------------------------+" << std::endl;

        // 等待空格键停止  
        bool recording = true;
        while (recording && transcriber.isActive()) {
            // 检查按键  
            if (_kbhit()) {
                int ch = _getch();
                if (ch == ' ') {  // 空格键  
                    recording = false;
                }
            }

            // 处理音频  
            transcriber.processAudio();

            // 显示状态  
            transcriber.displayStatus();

            Sleep(100);
        }

        std::cout << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        g_outputFile << "错误: " << e.what() << std::endl;
        g_outputFile.close();
        return 1;
    }

    // 写入文件尾  
    g_outputFile << std::endl;
    g_outputFile << "=== 转录结束 ===" << std::endl;
    g_outputFile.close();

    std::cout << "转录结果已保存到文件" << std::endl;

    return 0;
}

#endif
```

### 20251202 赞赏码内嵌
https://yb.tencent.com/s/bSfMdJSdxalT

``` python
from PIL import Image
import numpy as np

# 关键步骤：打开图片时直接转换为灰度模式 ('L')
img = Image.open('mcode400.png').convert('L')
pixel_array = np.array(img)

height, width = pixel_array.shape

print("uint8_t mcode400_pixels[{}][{}] = {{".format(height, width))
for i in range(height):
    # 将每一行的像素值转换为逗号分隔的字符串
    row_str = ", ".join(str(pixel) for pixel in pixel_array[i])
    if i == height - 1:
        print("    {" + row_str + "}")
    else:
        print("    {" + row_str + "},")
print("};")
```

[Search | DeepWiki](https://deepwiki.com/search/thorvg-uint8t-mcode400pixels40_f45269e0-d70e-4f0f-b4f4-cdd2b3a8aa21?mode=fast)

``` c++
/****************************************************************************  
 * 标题: ThorVG灰度图像显示示例  
 * 文件：grayscale_display.cpp  
 * 版本：0.1  
 * 作者: AEGLOVE  
 * 日期: 2025-12-02  
 * 功能: 使用ThorVG显示灰度图像数组  
 * 依赖: ThorVG图形库  
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集  
 * 编码: utf8 with BOM (代码页65001)  
 ****************************************************************************/  
  
#include <iostream>  
#include <cstdint>  
#include <memory>  
#include <thorvg.h>  
  
#pragma comment(lib, "thorvg.lib")  
  
using namespace std;  
using namespace tvg;  
  
// 将灰度数组转换为ARGB8888格式  
unique_ptr<uint32_t[]> convertGrayscaleToArgb(const uint8_t* grayData, uint32_t width, uint32_t height)  
{  
    auto argbData = make_unique<uint32_t[]>(width * height);  
      
    for (uint32_t y = 0; y < height; ++y) {  
        for (uint32_t x = 0; x < width; ++x) {  
            uint8_t gray = grayData[y * width + x];  
            // ARGB格式：Alpha(255) + Gray重复3次  
            argbData[y * width + x] = (255u << 24) | (gray << 16) | (gray << 8) | gray;  
        }  
    }  
      
    return argbData;  
}  
  
// 显示灰度图像  
void displayGrayscaleImage(const uint8_t* grayData, uint32_t width, uint32_t height)  
{  
    // 初始化ThorVG引擎  
    if (Initializer::init() != Result::Success) {  
        cout << "ThorVG初始化失败" << endl;  
        return;  
    }  
      
    // 创建画布  
    auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());  
    if (!canvas) {  
        cout << "画布创建失败" << endl;  
        Initializer::term();  
        return;  
    }  
      
    // 准备输出缓冲区  
    auto buffer = make_unique<uint32_t[]>(width * height);  
      
    // 设置画布目标  
    if (canvas->target(buffer.get(), width, width, height, ColorSpace::ARGB8888) != Result::Success) {  
        cout << "画布目标设置失败" << endl;  
        Initializer::term();  
        return;  
    }  
      
    // 转换灰度数据为ARGB格式  
    auto argbData = convertGrayscaleToArgb(grayData, width, height);  
      
    // 创建Picture并加载原始数据  
    auto picture = Picture::gen();  
    if (!picture) {  
        cout << "Picture创建失败" << endl;  
        Initializer::term();  
        return;  
    }  
      
    // 加载原始图像数据  
    if (picture->load(argbData.get(), width, height, ColorSpace::ARGB8888, false) != Result::Success) {  
        cout << "图像数据加载失败" << endl;  
        Paint::rel(picture);  
        Initializer::term();  
        return;  
    }  
      
    // 推送到画布并渲染  
    canvas->push(picture);  
    canvas->draw();  
    canvas->sync();  
      
    // 输出部分像素值验证  
    cout << "渲染完成，前10个像素值：" << endl;  
    for (int i = 0; i < 10; ++i) {  
        uint32_t pixel = buffer[i];  
        uint8_t a = (pixel >> 24) & 0xff;  
        uint8_t r = (pixel >> 16) & 0xff;  
        uint8_t g = (pixel >> 8) & 0xff;  
        uint8_t b = pixel & 0xff;  
        cout << "像素[" << i << "]: A=" << (int)a << ", R=" << (int)r << ", G=" << (int)g << ", B=" << (int)b << endl;  
    }  
      
    // 清理资源  
    Paint::rel(picture);  
    Initializer::term();  
}  
  
#if 1 // 测试代码  
int main(int argc, char** argv)  
{  
    // 示例灰度数据 (400x400)  
    static const uint32_t WIDTH = 400;  
    static const uint32_t HEIGHT = 400;  
      
    // 创建测试灰度图案  
    auto grayData = make_unique<uint8_t[]>(WIDTH * HEIGHT);  
      
    // 生成渐变灰度图案用于测试  
    for (uint32_t y = 0; y < HEIGHT; ++y) {  
        for (uint32_t x = 0; x < WIDTH; ++x) {  
            // 创建一个简单的渐变效果  
            grayData[y * WIDTH + x] = (uint8_t)((x + y) * 255 / (WIDTH + HEIGHT));  
        }  
    }  
      
    cout << "开始显示" << WIDTH << "x" << HEIGHT << "灰度图像..." << endl;  
      
    // 显示灰度图像  
    displayGrayscaleImage(grayData.get(), WIDTH, HEIGHT);  
      
    cout << "显示完成" << endl;  
      
    return 0;  
}  
#endif
```

### Python图片切割

我现在有一张图片，里面有按格子分成各子图片。我想用python生成一个软件，用于分割各子图片然后分别保存。要求可以添加网格，横线竖线可以手动调节。包含UI。需要什么库你可以选。

我现在有一张图片，里面有一个几何图形。我想用python生成一个软件，使用常见的canvas2d几何形状和线条，直线，圆弧，三次贝兹曲线来逼近图片中的图形轮廓路径，最终获取这些顶点或控制点的坐标数据，这样我就可以在我的其它绘图应用使用这些数据复现图片的矢量形式了。包含UI。需要什么库你可以选。



---

## 附录：TIPS（开发环境与注意事项）
- 包含 ThorVG：因开发环境路径问题，请使用：`#include <thorvg/thorvg.h>`
- Windows 宏冲突：在包含 `windows.h` 前添加 `#define NOMINMAX`
- ThorVG 初始化：`tvg::Initializer::init(4)` 使用 4 个线程
- ThorVG 字体加载：`tvg::Text::load("./res/syhtr.ttf")` 在 `UIApplication::initialize()` 中全局加载
- ThorVG `Canvas::push()`：直接传递裸指针 `Paint*`，使用 `std::move()` 转移所有权
- ThorVG `Canvas::remove()`：每帧渲染前调用 `remove()`（无参数），清空所有 `Paint` 对象
- ThorVG `SwCanvas::draw(true)`：`SwCanvas` 必须使用 `draw(true)` 清空缓冲区，否则会残留之前帧的内容（特别是 `SwCanvas`）。如果想实现先 3D 渲染，然后再使用 `GlCanvas` 渲染 UI，则 `draw(false)`
- ThorVG 渲染顺序：`canvas->remove()` -> `push()` -> `update()` -> `draw(true)` -> `sync()`
- ThorVG `Text` 设置：`font(name)` 设置字体名，`size(float)` 设置大小，`translate()` 设置位置，`layout(w,h)` 设置布局框，`align(x,y)` 设置对齐
- ThorVG `Text` 布局：必须先调用 `layout(w,h)` 设置布局框，再调用 `align(x,y)` 才能正确对齐
- ThorVG `Text` 对齐：`align(x,y)` 中 `x,y` 范围 `[0..1]`，0=左/上，0.5=中，1=右/下
- ThorVG `Paint` 生命周期：使用 `std::move()` 转移给 `Canvas` 后，不要再访问或删除该对象
- ThorVG GlCanvas只支持颜色空间ColorSpace::ABGR8888S
- ThorVG 即时模式：每帧调用 `canvas->remove()` 后重新创建 `Paint` 对象，避免状态累积
- UTF-8 字符串：VS2022 GB2312 编码下，中文字符串使用 `u8"..."` 前缀
- 开发环境：Windows x64，VS2022，C++17，宽字符集，GB2312（代码页936）
- 程序入口：`int main(int argc, char** argv)`
- 代码风格：函数使用小驼峰命名
- 整数类型：使用 `<cstdint>` 标准库类型
- 静态库链接：`#pragma comment(lib, "xxx.lib")`
- 测试代码：放在 `#if 1 ... #endif` 块中，方便折叠
- 注释规范：文件头使用文档注释块，类和复杂函数前添加说明，关键逻辑处添加行内注释，使用中文
- 示例中注释中版本号要对应 MUI 库的版本号
- 若源文件是以UTF8WITHBOM保存，字符串字面常量可以不带前缀u8，此时若控制台输出是乱码，可以在main函数的前面添加以下命令：SetConsoleOutputCP(CP_UTF8); // 若需要，可在控制台启用 UTF-8 输出
- 

注释示例：

```cpp
/****************************************************************************
 * 标题: MUI轻量级UI框架 - 基础控件系统
 * 文件: mui.h
 * 版本: 0.3
 * 作者: AEGLOVE
 * 日期: 2025-10-18
 * 功能: 基于WIN32+ThorVG+C++17的轻量级UI框架,采用即时模式渲染
 * 依赖: ThorVG图形库, Windows SDK
 * 环境: Windows x64, VS2022, C++17, 宽字符集
 * 编码: GB2312 (代码页936)
 ****************************************************************************/

// UI控件基类 - 所有控件的抽象基类
class UIElement {
    // ...

    // 渲染控件到 Canvas (纯虚函数,子类必须实现)
    virtual void render(tvg::Canvas* canvas) = 0;
};
```

这些 TIPS 用于提醒 AI 和开发者注意常见环境/库的使用细节与陷阱，请保留并在新问题中一起提供给 AI，以避免重复错误。

### 统一路径分隔符后再返回  
``` C
// 统一路径分隔符后再返回  
std::wstring result = fullPath.wstring();
std::replace(result.begin(), result.end(), L'\\', L'/');
```



## 附录：Meson编译thorvg

**准备工作**：下载thorvg.tar.gz，解压得到包含源代码的文件夹thorvg

**编辑选项**：

【thorvg--meson.build】主要是buildtype|cpp_std

```
project('thorvg',
        'cpp',
        default_options : ['buildtype=release', 'b_sanitize=none', 'werror=false', 'optimization=3', 'cpp_std=c++17', 'strip=true'],
        version : '1.0.0',
        license : 'MIT')
```


【thorvg--meson_options.txt】选项设置，一目了然。重点解释：

- `static`：强制使用静态链接的模块。当设置为 true 时,ThorVG 会强制使用内置的静态实现,即使系统中存在外部库
  影响图像加载器的选择:PNG、JPG、WebP 等格式会使用内部解码器而非 libpng、libjpeg-turbo、libwebp
  等效命令：`meson setup builddir -Dstatic=true # 强制使用内置实现` 
- `file`：启用文件 I/O 调用。控制是否允许 ThorVG 直接从文件系统读取文件。当设置为 true 时,定义 THORVG_FILE_IO_SUPPORT 宏 meson.build:128-130，影响 Picture::load(path) 和 Text::load(path) 等文件加载 API 的可用性。使用场景:
  启用 (true): 桌面应用、移动应用等可以访问文件系统的环境
  禁用 (false):WebAssembly 环境(文件系统访问受限)，嵌入式系统(只使用内存数据)，安全受限环境(禁止文件访问)

```c++
//标准构建(支持文件 I/O)  meson setup builddir -Dfile=true   
//纯内存构建(不支持文件 I/O)  meson setup builddir -Dfile=false

//当 file=false 时,您只能使用内存加载 API:
// 这些 API 仍然可用  
picture->load(data, size, "png", false);  
text->load("fontName", data, size, false);  
// 这些 API 将不可用或返回错误  
picture->load("image.png");  // 需要 THORVG_FILE_IO_SUPPORT  
text->load("font.ttf");      // 需要 THORVG_FILE_IO_SUPPORT
```

【thorvg--src--meson.build】

```
thorvg_lib = library(  
    'mui',  # 将 'thorvg' 改为你的喜好，如： 'mui'  
    include_directories    : headers,  
    version                : meson.project_version(),  
    dependencies           : thorvg_lib_dep,  
    install                : true,  
    cpp_args               : compiler_flags,  
    gnu_symbol_visibility  : 'hidden',  
    override_options       : override_options  
)
这样修改后:
静态库会生成为 mui.lib (Windows) 或 libmui.a (Linux/macOS)
动态库会生成为 mui.dll (Windows) 或 libmui.so (Linux) 或 libmui.dylib (macOS)
pkg_mod.generate(  
    libraries    : thorvg_lib,  
    version      : meson.project_version(),  
    name         : 'libmui',      # 改为 'libmui'  
    filebase     : 'mui',          # 改为 'mui'  
    description  : 'A Thor library for rendering vector graphics'  
)
这会生成 mui.pc 文件而不是 thorvg.pc
```




管理员身份打开：x64 Native Tools Command Prompt for VS 2022
转移到指定目录：
	cd /d E:\Dev\KU\thorvg
	rmdir /s /q builddir

— 构建方式 A：生成 Visual Studio 工程（--backend=vs）
- 配置构建(静态库，推荐；使用方需定义 TVG_STATIC)

	meson setup builddir --backend=vs -Ddefault_library=static -Dloaders=all -Dsavers=all -Dfile=true

- 或者配置为动态库 (DLL)

	meson setup builddir --backend=vs -Ddefault_library=shared -Dloaders=all -Dsavers=all -Dfile=true

- 打开并编译解决方案：

	start builddir\thorvg.sln

— 构建方式 B：Ninja 直接构建（无 VS 解决方案）

	meson setup builddir -Ddefault_library=static -Dloaders=all -Dsavers=all -Dfile=true
	meson compile -C builddir
	rem 可选安装:
	rem meson install -C builddir

**清理与查看配置**

- 重新配置/切换选项后，建议：

	meson setup builddir --wipe -Ddefault_library=static -Dloaders=all -Dsavers=all -Dfile=true

- 查看当前配置：

	meson configure builddir

**Windows 特殊注意**

- 注释或删除 src/renderer/tvgInitializer.cpp 中的全局运算符重载，避免与宿主/CRT 混用导致释放边界错误：

	// void* operator new(std::size_t size) {
	//     return tvg::malloc(size);
	// }
	// void operator delete(void* ptr) noexcept {
	//     tvg::free(ptr);
	// }

**项目使用注意事项**：

```c++
// 使用纯静态库时,您的应用程序代码必须定义 TVG_STATIC 宏,
#define TVG_STATIC  
#include "thorvg.h"
#pragma comment(lib, "libthorvg.lib")// 使用纯静态库
```

**FAQ**

- 反复切换 static/shared 或 file 选项后构建异常
  - 清理或使用 --wipe 重新配置
- 链接时报找不到符号
  - 静态库场景检查是否定义了 TVG_STATIC；确认库名是否自定义为 mui 并在链接中一致
- 运行时文本/图片加载失败
  - 如果 -Dfile=false，只能用内存加载 API；检查 Text::load/Picture::load 的调用方式

## 控制台信息输出

``` c++
#include <cwchar>   // 提供 std::wprintf、宽字符相关的 C 函数
#include <conio.h>  // 提供 _kbhit(), _getch()（控制台非阻塞/读取按键）
#include <io.h>     // 提供 _setmode(), _fileno()（切换 stdout 模式）
#include <fcntl.h>  // 提供 _O_U16TEXT 等文件/模式常量
#define ODD(...) wprintf(__VA_ARGS__)
// 控制台颜色设置  
void setConsoleColor(int32_t color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
system("cls");// 清屏：调用 Windows shell 的 CLS 命令清除控制台内容，方便每次重绘播放器状态（注意：system 调用开销和安全性）
//main:	添加
_setmode(_fileno(stdout), _O_U16TEXT);

std::wcout << L"=== MUI 音乐播放器 ===" << std::endl;
ODD(L"开如打印信息\n");
```



``` c++
// 主循环  
bool running = true;
auto lastUpdateTime = std::chrono::steady_clock::now();

while (running) {
    // 更新播放位置  
    auto currentTime = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastUpdateTime).count() > 500) {
        player.updateAudioPosition();

        // 检查是否播放结束，自动播放下一首  
        if (player.isPlaybackFinished() && player.getPlaybackState() == MUI::PlaybackState::Playing) {
            player.nextSong();
        }

        displayPlayerStatus(player);
        lastUpdateTime = currentTime;
    }

    // 检查按键输入  
    if (_kbhit()) {
        char key = _getch();

        switch (key) {
        case ' ': // 空格键：播放/暂停  
            if (player.getPlaybackState() == MUI::PlaybackState::Playing) {
                player.pauseAudio();
            }
            else if (player.getPlaybackState() == MUI::PlaybackState::Paused) {
                player.resumeAudio();
            }
            else {
                player.playSongByIndex(player.getCurrentSongIndex());
            }
            break;

        case 'n':
        case 'N': // 下一首  
            player.nextSong();
            break;

        case 'p':
        case 'P': // 上一首  
            player.preSong();
            break;

        case 's':
        case 'S': // 停止  
            player.stopAudio();
            break;

        case '+':
        case '=': // 音量增加  
            player.setVolume(std::min(100.0f, static_cast<float>(player.getVolume()) + 5.0f));
            break;

        case '-':
        case '_': // 音量减少  
            player.setVolume(std::max(0.0f, static_cast<float>(player.getVolume()) - 5.0f));
            break;

        case 'm':
        case 'M': // 切换播放模式  
        {
            auto currentMode = player.getPlayMode();
            if (currentMode == MUI::PlayMode::Normal) {
                player.setPlayMode(MUI::PlayMode::Repeat);
            }
            else if (currentMode == MUI::PlayMode::Repeat) {
                player.setPlayMode(MUI::PlayMode::Shuffle);
            }
            else {
                player.setPlayMode(MUI::PlayMode::Normal);
            }
        }
        break;

        case 'l':
        case 'L': // 显示歌曲列表  
            displaySongList(player);
            break;

        case 'i':
        case 'I': // 显示当前歌曲信息  
        {
            const auto* song = player.getCurrentSong();
            if (song) {
                system("cls");
                showSongInfo(*song);
                std::wcout << L"\n按任意键返回..." << std::endl;
                _getch();
            }
        }
        break;

        case 'e':
        case 'E': // 提取封面  
        {
            const auto* song = player.getCurrentSong();
            if (song && song->embeddedCoverCount > 0) {
                std::wcout << L"正在提取封面..." << std::endl;

                // 获取歌曲所在目录  
                fs::path songDir = fs::path(song->filePath).parent_path();

                // 构建 xcovers 子目录路径  
                fs::path coversDir = songDir / L"xcovers";

                // 调用 extractCover,传入构建的路径  
                std::wstring coverPath = extractCover(song->filePath, coversDir.wstring());

                if (!coverPath.empty()) {
                    std::wcout << L"封面已提取到: " << coverPath << std::endl;
                }
                else {
                    std::wcout << L"封面提取失败!" << std::endl;
                }
                Sleep(2000);
            }
        }
        break;

        case 'q':
        case 'Q': // 退出  
            running = false;
            break;

        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9': // 快速选择歌曲  
        {
            int32_t songIndex = key - '1';
            if (songIndex < static_cast<int32_t>(player.getSongLibrary().size())) {
                player.playSongByIndex(songIndex);
            }
        }
        break;
        }
    }

    Sleep(50); // 避免CPU占用过高  
}
```





## 开发日志

### 2025-10-21 v0.3
项目使用注意事项：
// 使用纯静态库时,您的应用程序代码必须定义 TVG_STATIC 宏,
#define TVG_STATIC  
#include "thorvg.h"
#pragma comment(lib, "libthorvg.lib")// 使用纯静态库

今日变更与问题修复（汇总）
- 新增与完善测试：创建并升级 `uitest_group_frame.cpp`（v0.1→v0.2），覆盖 UIGroup、UIFrame 的布局、命中、叠放与 sharedElements 遮挡等用例：
  - FlexLayout、GridLayout 基本布局与点击命中。
  - 嵌套 UIGroup（父→子）命中递归到最深子元素。
  - 叠放遮挡验证：普通元素遮挡与 sharedElements 遮挡（shared 在最上层优先）。
- 事件派发逻辑重构：在 `UIManager` 内构建“全局命中链”（顶层→最深子），仅将 MouseDown/MouseUp 派发给“最深命中者”。
  - 修复“UIGroup 中按钮无法点击”的问题（容器不再需要自转发）。
  - 保持 MouseMove 仍为广播，以兼容现有 hover/tooltip 行为。
  - 遮挡语义生效：顶层 UIFrame 或 sharedElements 遮挡时，底层按钮不再收到点击。
- UITextInput 稳定性与可用性：
  - 移除 `tvg::Text::composite(..., ClipPath)`（本地 ThorVG 版本 `Text` 无该接口）。
  - 防止 ThorVG 栈溢出：为 `layout(w,h)` 与字号设置安全下限（width/height/fontSize ≥ 1.0f），避免生成退化/NaN 曲线导致 Bezier 递归溢出。
  - 单行可视窗口与“光标可见”滚动：基于 `measureTextWidth()` 与 UTF-8 工具，按像素截取可视子串，保证文本不溢出边界框，同时光标始终可见。
  - 光标垂直居中绘制，X 位置夹取在可视宽度内。
- 文本与 UTF-8 工具（位于 `namespace MUI`）：
  - `utf8Next/utf8Prev/utf8Count/utf8ByteIndexFromCount`
  - `measureTextWidth`（ThorVG 文本测量）
  - `fitTextEllipsis`（UTF-8 安全尾省略）
- UIButton/UITooltip：
  - UIButton 新增文本溢出策略与内边距、测量缓存；点击判定改为即时 `contains()`，不依赖 hover。
  - Tooltip 支持便捷设置与共享管理（通过 `UIManager::addSharedElement` 注册，Button 侧持有 `weak_ptr`）。
- UILabel 编码显示修复：
  - 说明文本需使用 UTF-8 字面量（`u8"..."` 或 `u8R"(...)"`）；否则在 GB2312 源文件下会传入非 UTF-8 字节导致不显示。

涉及的关键修改
- `mui.h`
  - UIManager：
    - `dispatchMouseDown/dispatchMouseUp` 改为：先找顶层命中（shared→elements，逆序），再自上而下构建命中链，仅派发给链尾（最深命中者）。
  - UITextInput：
    - `buildScene()` 移除不兼容的 composite/clip 用法；加入 width/height/fontSize 安全下限；实现基于测量的可视窗口与光标滚动，防止溢出；修复栈溢出问题。
  - UIButton/UILabel/UITooltip：
    - 文本布局统一使用 `translate -> layout -> align`，垂直居中；UIButton 文本截断与缓存；Tooltip 便捷 API 与共享管理。
- `uitest_group_frame.cpp`
  - v0.2 场景：
    - 左上：UIGroup + FlexLayout（按钮应可点击）。
    - 左下：嵌套 UIGroup（父→子，最深子按钮应可点击）。
    - 右上：UIGroup + GridLayout（按钮应可点击）。
    - 右下：遮挡测试（普通 elements 与 sharedElements 皆可遮挡，shared 更靠顶）。
  - 说明标签文本改为 UTF-8 字面量以确保显示。

问题与根因记录
- `tvg::Text::composite` 与 `tvg::CompositeMethod::ClipPath` 在本地 ThorVG 版本不可用，导致编译错误。
- 传入 `layout(w,h)` 的尺寸为 0 或负值，会在 ThorVG 内部生成退化曲线，Bezier 递归长度计算产生 NaN 并造成栈溢出；通过最小尺寸与测量裁剪规避。
- 事件广播导致遮挡失效与容器内控件无响应；已改为命中链派发至最深子元素。
- GB2312 源文件中未加 `u8` 的中文字符串传入 ThorVG 不是 UTF-8，导致文本不显示；统一改为 `u8` 字面量或设置编译器 `/utf-8`。

验证要点（通过）
- UIGroup 内按钮可点击；嵌套 Group 的子按钮可点击。
- 顶层 UIFrame/共享遮挡框存在时，底层按钮不触发点击。
- UITextInput：极窄宽度、不输入文本、快速输入/删除均无异常；长文本不溢出，光标保持可见。
- 说明标签（`info`）中文可见。

后续建议
- 事件阶段：引入 Capture/Bubble 两阶段与返回布尔拦截；支持 Pointer Capture（拖动）。
- UIFrame 输入穿透：新增 `blocksInput` 开关（在 `hitTest` 或派发时跳过）。
- `clipChildren`：后续可在容器中统一实现裁剪（不依赖 `Text::composite`）。
- `UITextInput`：支持鼠标选中、复制粘贴、IME 组合串显示。

**2025-10-21 Hotfix — ThorVG 文本测量崩溃**

- 现象：运行 uitest_textinput.cpp 偶发崩溃，堆栈在 SceneImpl::clearPaints → LoadModule 析构（thorvg-1.dll）。
- 根因：`measureTextWidth()` 每次创建/销毁 `tvg::Text`，导致字体 Loader 频繁获取/释放，在 Canvas 清理时序下触发崩溃。
- 修复：`measureTextWidth()` 复用静态测量对象，类型用 `decltype(tvg::Text::gen())` 保持删除器一致；加入字号下限与 `isfinite` 防御。
- 影响：稳定性提升，无功能变化；Button/Label/UITextInput 正常。
- 验证：uitest_textinput.cpp 快速输入/删除、极窄宽度测试通过，无崩溃。

### 2025-10-22 v0.3

今日变更与修复
- uitest_textinput 左栏改为自动布局：新增 UIGroup + FlexLayout(Vertical)，将 info 与四个 UITextInput 收纳，设置 padding(8) 与 spacing(12)，视觉上位于 UIFrame 内；右侧按钮保留绝对定位。
- 修复 info 与第一个输入框“重叠”：UILabel 不裁剪且首选高度过小，提升 info 的 preferredSize（64→128）；建议后续用 Text::bounds 动态测量高度。
- 焦点切换与光标管理：引入 UIElement::onFocusChanged；UIManager::setFocus 通知旧/新焦点；dispatchMouseDown 改为使用 setFocus，未命中清空焦点；UITextInput 覆盖 onFocusChanged，onMouseDown 仅请求焦点。结果：切换输入框时只有当前输入框显示并闪烁光标。
- 光标与前字形重叠：原因在于按 codepoint 导航与紧边界(bounds)偏差。新增字素簇(grapheme)导航工具（utf8DecodeAt、graphemePrev/graphemeNext，覆盖 ZWJ/变体选择符/组合附标/Emoji 肤色/区域指示符）；UITextInput::onKeyDown 全面改为按字素簇处理 Backspace/Delete/左右移动；并在绘制时为光标 X 位置加入 caretEpsilon=0.5f 微偏移，减轻 bounds<advance 的视觉压字。

涉及文件
- mui.h
  - 新增虚函数 `UIElement::onFocusChanged(bool)`;
  - `UIManager::setFocus`/`dispatchMouseDown` 更新为发出焦点变更并支持未命中清焦点；
  - 在 namespace MUI 内新增 Unicode 字素簇工具：`utf8DecodeAt`、`graphemePrev`、`graphemeNext`；
  - `UITextInput`：实现 `onFocusChanged`；`onMouseDown` 仅请求焦点；`onKeyDown` 改用字素簇导航；`buildScene` 为光标 X 增加 `caretEpsilon`。
- uitest_textinput.cpp
  - 左栏添加 `UIGroup + Flex(Vertical)` 承载 info 与四个输入框；info 首选高度上调，避免视觉溢出。

验证
- 焦点切换：A/B/Long/Emoji 间切换时，旧输入框光标熄灭且不再闪烁。
- 文本编辑：Emoji/肤色变体/国旗序列下，Backspace/Delete/左右光标均按“字素簇”预期工作，不出现光标压字。
- info 不再与第一个输入框发生视觉重叠。

已知限制/后续
- UILabel 仍未统一 clip，建议在容器层提供 `clipChildren` 实裁剪或使用 mask。
- UITextInput 后续补充：鼠标选区、复制粘贴、IME 组合串显示。



### 2025-10-23 v0.3

（UITextInput：字形布局、裁剪与末尾光标可见性修复）

#### 变更摘要
- 引入 thorvg 新增 `Text::getGlyphInfo()`，以字形级布局计算插入点，修复光标与字符重叠/错位。
- 调整长文本渲染为“完整文本 + 水平滚动”，并对 `Text`/光标 `Shape` 分别应用裁剪，修复溢出。
- 修复“文本恰好填满时，最后一个字符位置光标不可见”的边界问题。

#### 问题与修复详情
1) 光标与字符重叠/错位
- 现象：中英混排/Emoji/Kerning 场景下，光标位置与视觉字形不一致。
- 根因：旧实现用字符宽度估算，未考虑字距与真实字形布局。
- 方案：
  - 使用 `Text::getGlyphInfo()` 构建插入点数组：
    - pos[0] = 0
    - pos[i] = glyph[i].x (i>=1)
    - pos[n] = glyph[n-1].x + glyph[n-1].advance
  - 维护 `m_caretIndex`（插入点索引），点击命中用 `lower_bound` 选最近插入点。
  - 滚动与渲染：
    - 文本起点：x = paddingL - scrollX
    - 光标位置：x = paddingL + (pos[idx] - scrollX)

2) 长文本显示溢出（回归）
- 现象：A/Long 输入框出现文本越出内容区域。
- 根因：切换到“完整文本 + 水平滚动”后未显式裁剪；`Text::layout()` 不负责裁剪。
- 修复：
  - 正确使用 ThorVG 裁剪 API 对 Paint 级裁剪：
    - 文本：`textObj->clip(clipRect);`
    - 光标：`cursorShape->clip(clipRect);`
  - 注意：
    - 使用 `clip()`（参考 thorvg.h:462-475），而非对 `Scene` 调用 `composite(ClipPath)`。
    - `clip()` 接受原始指针；不使用 `std::move(clipRect)`（参考 tvgCapi.cpp:335-339）。

3) Scene 裁剪 API 误用导致编译错误
- 现象：`tvg::Scene` 无 `composite()`，编译失败。
- 修复：移除对 `Scene::composite()` 的调用，改为对各 Paint 使用 `clip()`。

4) 文本恰好填满时末尾光标不可见
- 现象：插入点在最后一个可见字符处时，光标被裁剪掉；在倒数第二个位置正常。
- 根因：光标左边缘正好落在裁剪右边界，1.5px 宽度被完全裁掉。
- 修复：
  - 统一使用 `kCaretW=1.5f` 光标宽度。
  - `ensureCaretVisible_()` 将右侧视图边界改为 `m_scrollX + (contentW - kCaretW)`，并相应调整 `maxScroll`。
  - `caretVisualX_()` 将可视 X clamp 到 `[paddingL, paddingL + contentW - kCaretW]`。
  - 绘制时使用同样的 `kCaretW`，与 clamp 一致。

#### 关键接口/实现要点
- 布局缓存：`UI::TextGlyphLayout::build()` 利用 `Text::getGlyphInfo()` 生成 `positions[]` 与总宽度。
- 光标/滚动：
  - `m_caretIndex`：插入点索引，范围 `[0..positions.size()-1]`
  - `ensureCaretVisible_()`：以 `contentW - kCaretW` 做右边界
  - `caretVisualX_()`：局部坐标，已减 `scrollX` 且做 clamp
- 裁剪范围：`clipRect = [bounds.x + paddingL, bounds.y, contentW, bounds.h]`
- ThorVG 使用规范：
  - 对 `Text`/`Shape` 使用 `clip()` 裁剪；不要对 `Scene` 裁剪
  - `clip()` 传入原始指针；生命周期由 ThorVG 管理
- 指针一致性：
  - `m_layout.build(textObj, text)`：当前实现保持与项目中 `Text::gen()` 句柄用法一致

#### 验证清单
- 中英混排/Emoji/长串文本：光标与字形布局一致，无重叠/错位。
- A/Long 输入框：文本不越界，水平方向滚动正常，裁剪边界不闪烁。
- 边界场景：文本恰好填满时，将光标移动至最后一个可见字符位置，光标稳定可见；移动至末尾/倒数第二个位置均正确。
- 点击命中：任意位置点击，插入点与视觉最近位置一致。

#### 经验与约束
- `Text::layout(w,h)` 控制排版盒，不负责裁剪；裁剪需显式调用 `clip()`。
- push 后不要再访问 Paint；裁剪应在 push 前完成。
- 保持光标绘制宽度常量，并在滚动/位置 clamp 中统一使用，避免边界抖动。

#### 后续工作（建议）
- 字素簇级插入点（ZWJ/变体选择符合并）：在 `TextGlyphLayout::build()` 中按需合并 `positions[]`，避免把光标落在不可分割的 ZWJ 链中间。
- UI 性能：为 `UITextInput` 增加 dirty 标记与 Scene 缓存，减少每帧对象重建。
- IME 支持：组合态处理与候选窗对齐（后续迭代）。



### 2025-10-24 v0.3

开发日志 — 嵌入图标与字体并在运行时供 ThorVG 使用
1.	目标
•	将 资源文件 目录下的 ME.ico、small.ico 嵌入到可执行文件（exe）作为窗口图标。
•	将 siyuan.ttf、fuhao.ttf 嵌入到 exe（RCDATA），并在运行时加载到 ThorVG（通过内存接口），供 UI 渲染使用。
•	提供测试程序 uitest_icon.cpp，验证图标与字体加载及渲染。
2.	资源准备（工程内）
•	新增/修改文件：
•	resource.h：定义资源 ID（IDI_APP_ICON=101, IDI_SMALL_ICON=102, IDR_FONT_SIYUAN=201, IDR_FONT_FUHAO=202）。
•	app.rc：将图标与字体以资源方式嵌入：
•	IDI_APP_ICON ICON "资源文件\\ME.ico"
•	IDI_SMALL_ICON ICON "资源文件\\small.ico"
•	IDR_FONT_SIYUAN RCDATA "资源文件\\siyuan.ttf"
•	IDR_FONT_FUHAO  RCDATA "资源文件\\fuhao.ttf"
•	把 .ico 与 .ttf 文件加入项目，确保 app.rc 在编译时被链接为资源。
3.	mui.h 的关键改动
•	包含 resource.h：
•	#include "resource.h"
•	UIApplication 增加成员：
•	HICON hIconBig; HICON hIconSmall;
•	在 initialize() 中设置窗口类图标：
•	使用 LoadImageW(hInst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 0,0, LR_DEFAULTCOLOR | LR_SHARED) / IDI_SMALL_ICON。
•	在 WNDCLASSEXW 中设置 wc.hIcon / wc.hIconSm，并在 CreateWindowExW 后调用 SendMessageW(hwnd, WM_SETICON, ICON_BIG/ICON_SMALL, (LPARAM)...) 以保证生效。
•	说明：示例使用 LR_SHARED，由系统管理句柄；若改为不共享则需在 shutdown() 调用 DestroyIcon。
•	实现 bool loadMFontForTVG() noexcept：
•	实现逻辑：
•	获取模块句柄 HINSTANCE hInst = GetModuleHandleW(nullptr)。
•	对每个字体资源 ID 调用 FindResourceW → LoadResource → LockResource → SizeofResource 获得内存指针与大小。
•	调用 ThorVG 的内存加载接口：
•	tvg::Text::load(fontName, reinterpret_cast<const char*>(pData), static_cast<uint32_t>(sz), "font/ttf", true)（copy=true 使 ThorVG 内部拷贝数据，资源生命周期不再成问题）。
•	返回加载状态（全部成功返回 true，否则 false）。
•	在 UIApplication::initialize() 中，在 tvg::Initializer::init(threadCnt) 成功后调用 loadMFontForTVG() 并记录日志（失败为警告，可继续运行）。
•	在 shutdown() 中：
•	卸载 ThorVG 中用到的字体（可选）：tvg::Text::unload("siyuan.ttf") / tvg::Text::unload("fuhao.ttf")（示例中卸载默认为可选）。
•	若未使用 LR_SHARED 加载图标或以非共享方式创建 HICON，需显式 DestroyIcon。
4.	测试程序 uitest_icon.cpp
•	启动流程：
•	创建 MUI::UIApplication app; app.initialize(...)（内部已设置图标并调用 loadMFontForTVG()）。
•	新增自定义控件 FontTest : MUI::UIElement：
•	在 buildScene() 中使用 tvg::Text::gen()：
•	txt->font("siyuan.ttf")、txt->size(28)、txt->text(u8"思源示例：Hello 世界 😊") 等。
•	txt->font("fuhao.ttf") 渲染符号示例。
•	将 FontTest 加入 UIManager，与其他控件（UITextInput, UIButton, UIFrame, UIGroup）一起渲染。
•	结果：在测试中字体渲染成功，图标显示在窗口和任务栏。
5.	可选与替代方案（注意点）
•	字体注册替代：
•	可用 AddFontMemResourceEx 将字体注册到 GDI（使系统字体列表可用），但 ThorVG 已提供内存加载接口，优先使用 tvg::Text::load。
•	若第三方库仅支持从文件加载，可把资源写出到临时文件（GetTempPathW + CreateFileW），然后调用库的文件加载接口，程序退出时删除临时文件。
•	LR_SHARED vs 非共享：
•	LR_SHARED：系统管理 HICON，调用 DestroyIcon 不安全/不必要。
•	非共享加载时要负责 DestroyIcon。
•	tvg::Text::load(..., copy)：
•	copy=true：ThorVG 内部拷贝，安全且资源释放无需担心。
•	copy=false：需保证资源内存有效直到 ThorVG 卸载字体。
6.	Visual Studio 操作小结
•	将 app.rc、resource.h、.ico、.ttf 添加到项目（Project > Add > Existing Item...）。
•	确认 app.rc 的属性：Build Action = Resource（会被链接进 exe）。
•	若需要以资源 ID 在代码中引用，确保 resource.h 与 app.rc 中 ID 一致。
•	可用 Resource Hacker 等工具检查生成的 exe 中是否包含 ICON / RCDATA。
7.	调试与故障排查（简要）
•	若字体未生效：
•	检查 loadMFontForTVG() 返回值并查看日志输出（ODD(...)）。
•	确认 app.rc 中字体项路径正确且已经加入到资源里。
•	确认 tvg::Text::load 返回 tvg::Result::Success。
•	如果字体包含复杂字形或需要特定 cmap，尝试写出临时文件并用 ThorVG 从文件加载。
•	若图标不显示：
•	确认 wc.hIcon/wc.hIconSm 已设置并 RegisterClassExW 成功。
•	在 CreateWindowExW 后调用 SendMessageW(hwnd, WM_SETICON, ICON_BIG/ICON_SMALL, ...)。
•	检查资源 ID 与 resource.h 一致，使用 Resource Hacker 验证 exe。
8.	结论与下步建议
•	当前实现把图标与字体都编入 exe，字体通过 tvg::Text::load 从内存注册到 ThorVG，测试程序 uitest_icon.cpp 已验证图标与字体渲染成功。
•	若要发布更稳健的版本，可：
•	在 shutdown() 中显式 tvg::Text::unload 已加载字体。
•	在需要时改为非共享图标加载并正确释放 HICON。
•	增加运行时日志与错误提示以方便用户诊断资源加载失败。



``` C
根据前面的问答,以下是关于ThorVG字体加载的知识点总结:

一、从内存加载字体的基本方法

ThorVG提供静态方法Text::load()从内存加载字体,参数包括:

name: 字体标识符,用于后续引用
data: 字体数据指针
size: 数据大小
mimeType: MIME类型(如"font/ttf"或"ttf")
copy: 是否复制数据到内部缓冲区 tvgText.cpp:67-79
二、copy参数的重要性

当从Windows资源(RCDATA)加载字体时,必须设置copy=true。原因是:

LockResource返回的指针生命周期由系统管理
模块卸载时资源数据会失效
ThorVG会缓存字体数据供多个Text对象共享
复制数据可确保字体数据持久有效 tvgLoader.cpp:405-426
三、字体缓存机制

ThorVG将加载的字体存储在全局_activeLoaders列表中:

使用name参数作为唯一标识符
同名字体不会重复加载,会复用已缓存数据
字体按加载顺序添加到列表末尾
通过引用计数管理字体生命周期 tvgLoader.cpp:416-418
四、字体名称匹配规则

调用text->font(name)时,ThorVG执行精确字符串匹配:

必须使用Text::load()注册时的完整name
name只是标识符,不必是文件名格式
从文件加载时,ThorVG会自动提取基础文件名(不含路径和扩展名)作为标识符
从内存加载时,可自由选择任何标识符 tvgText.h:71-88
五、默认字体(fallback)机制

当调用text->font(nullptr)时:

ThorVG调用LoaderMgr::anyfont()选择任意已加载字体
返回_activeLoaders列表中第一个TTF字体加载器
字体顺序由Text::load()调用顺序决定
无法保证具体是哪个字体,行为不确定 tvgLoader.cpp:442-452
六、设置默认字体的方法

ThorVG本身不支持"默认字体"概念,但可通过以下方式实现:

方法1:确保目标字体最先加载
将期望的默认字体放在加载数组的第一个位置,使其成为_activeLoaders中的第一个字体。

方法2:显式指定字体(推荐)
不依赖fallback机制,而是显式调用text->font("字体名"),失败时才使用nullptr作为最后的fallback。 tvgLottieBuilder.cpp:928-930

七、loadMFontForTVG()函数的行为

该函数使用allOk标志跟踪加载状态:

遇到失败时设置allOk=false,但继续加载下一个字体
返回false只表示至少有一个字体失败,不表示全部失败
部分成功时,已加载的字体仍然可用
若需确保全部成功,应改用计数器判断successCount == 字体总数
八、实际应用建议

在调用loadMFontForTVG()前先初始化ThorVG(tvg::Initializer::init())
字体会持续缓存直到调用Text::unload()或ThorVG终止
建议显式指定字体名称,避免依赖不确定的fallback行为
使用有意义的字体名称便于调试
根据应用需求决定是否要求"全部成功"或"部分成功"
```

### 2025-10-27

开发日志：Windows控制台宽字符输出问题

问题描述：
在使用TagLib库处理包含中文、日文等非ASCII字符的音频文件时，程序在扫描到第36首歌曲后停止运行，无法完成全部70首歌曲的扫描。具体表现为在输出某些歌曲的元数据（特别是专辑名）时程序中断，未显示"扫描完成"提示。

问题根源：
使用std::locale::global(std::locale(""))和std::wcout.imbue(std::locale())来设置控制台输出时，程序会尝试使用系统默认locale进行宽字符到多字节字符的转换。当遇到某些特殊Unicode字符（如日文假名、特殊标点符号等）时，locale转换可能失败或抛出异常，导致程序崩溃或停止。

失败的代码：

```
int main()  
{  
    // 使用 UTF-8 控制台输出（对于 wcout 需要设置 locale）  
    std::locale::global(std::locale(""));  
    std::wcout.imbue(std::locale());  
      
    // ... 后续代码  
}
```

成功的代码：

```
int main()  
{  
    // Windows 控制台用 UTF-16 宽字符输出  
    _setmode(_fileno(stdout), _O_U16TEXT);  
      
    // ... 后续代码  
}
```

解决方案：
使用Windows特定的_setmode函数将标准输出流设置为UTF-16模式（_O_U16TEXT）。这种方式直接输出宽字符，完全避免了locale转换过程，因此不会因为字符转换失败而导致程序中断。

技术细节：

1. _setmode是Windows CRT库提供的函数，需要包含<io.h>和<fcntl.h>头文件
2. _O_U16TEXT模式使控制台直接接受UTF-16编码的宽字符输出
3. 这种方式绕过了locale转换机制，对所有Unicode字符都能正确处理
4. 该方法仅适用于Windows平台，跨平台项目需要使用条件编译

适用场景：

- Windows平台下需要输出包含多语言字符的宽字符串
- 处理音频文件元数据等可能包含各种Unicode字符的场景
- 需要避免locale转换失败导致的程序崩溃

注意事项：

1. 使用_setmode后，所有std::wcout输出都应使用宽字符串（L"..."）
2. 该方法是Windows特定的，不可移植到其他操作系统
3. 设置后整个程序的标准输出都会受影响，需要在程序开始时设置
4. 如果需要跨平台，应使用#ifdef _WIN32进行条件编译

验证结果：
修改后程序成功扫描全部70首歌曲，包括文件名和元数据中包含日文、中文等非ASCII字符的文件，未出现任何中断或异常。

### 2025-10-29

重构MUI，主要是引进了【对象池化】，复用对象降低性能消耗。

- [ ] 处理Button,tooltip,Label文本溢出边框问题。
- [ ] UIElement添加按键，滚轮，字符输入等相关事件处理。

#### 强制UTF8

VS如何强制源代码文件使用utf8withBOM格式？

•	为所有 MSVC 编译配置追加编译开关 /utf-8，强制源字符集和执行字符集为 UTF-8。

```
方式二：在 VS 项目属性里设置（每个项目一次）
•	打开项目属性：项目 > 属性
•	先切到：配置 选择 “所有配置”，平台 选择 “所有平台”
•	方法A（图形化选项）：
•	到 C/C++ > 高级 > 源字符集 选择 “UTF-8”
•	到 C/C++ > 高级 > 执行字符集 选择 “UTF-8”
•	方法B（直接加开关）：
•	到 C/C++ > 命令行 > 附加选项，追加 /utf-8
•	如需资源编译器也用 UTF-8：
•	到 资源 > 命令行 > 附加选项，追加 /codepage:65001
```



## 网络

### libhv

[Search | DeepWiki](https://deepwiki.com/search/window64vs2022unicodec17utf8-w_5aecd964-6528-4982-ae17-ad13237d2131?mode=fast)

https://deepwiki.com/search/window64vs2022unicodec17utf8-w_5aecd964-6528-4982-ae17-ad13237d2131?mode=fast

编译静态库：

``` 
PS E:\Dev\KU> git clone https://gitee.com/libhv/libhv.git  
PS E:\Dev\KU> cd libhv
PS E:\Dev\KU\libhv> mkdir build
PS E:\Dev\KU\libhv> cd build
PS E:\Dev\KU\libhv\build> cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_STATIC=ON  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DBUILD_SHARED=OFF -DWITH_OPENSSL=ON -DWITH_MQTT=ON
简单版本：cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_STATIC=ON -DBUILD_SHARED=OFF
PS E:\Dev\KU\libhv\build> cmake --build . --config Release

最终得到：
"E:\Dev\KU\libhv\build\lib\Release\hv_static.lib"

OpenSSL 支持: -DWITH_OPENSSL=ON CMakeLists.txt:27
HTTP/2 支持: -DWITH_NGHTTP2=ON CMakeLists.txt:25 #这个在实际编译中会报错(它是C库)，所以暂时不支持
MQTT 支持: -DWITH_MQTT=ON CMakeLists.txt:19

注意：若想支持OpenSSL,HTTP/2，必须预先安装它们，推荐使用vcpkg安装：
PS E:\Dev\KU\libhv\build> vcpkg install openssl:x64-windows nghttp2:x64-windows
```

**集成到 VS2022 项目**
**方法一: 手动配置**
复制文件:
将 build/lib/hv_static.lib 复制到您的项目目录
将 build/include/hv/ 目录复制到您的项目 CMakeLists.txt:247-248
项目配置:
在项目属性 → C/C++ → 常规 → 附加包含目录中添加 include 路径
在项目属性 → 链接器 → 输入 → 附加依赖项中添加 hv_static.lib
添加必需的系统库: ws2_32.lib;secur32.lib;crypt32.lib;winmm.lib;iphlpapi.lib CMakeLists.txt:168-170
预处理器定义:
添加 HV_STATICLIB 宏定义

**方法二: 使用 CMake 集成**
在您的 CMakeLists.txt 中:

```
# 安装 libhv 后  

find_package(libhv REQUIRED)  
target_link_libraries(your_target PRIVATE hv_static)
```

**HV客户端和服务端示例**

编译HV静态库时只使用：cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_STATIC=ON -DBUILD_SHARED=OFF

暂时不支持SSL/HTTP2，因为老是有错。

重点是要包含vcpkg相关目录到VS项目中。

``` 
问题1: 初始编译失败 - 缺少libhv-dev包 现象: Ubuntu官方源找不到libhv-dev 解决: 从Gitee源码编译安装 步骤:

sudo apt install git cmake g++
git clone https://gitee.com/libhv/libhv.git
cd libhv && mkdir build && cd build
cmake .. && make -j4
sudo make install
问题2: 头文件找不到 现象: 编译时报错TcpServer.h缺失 解决: 指定libhv头文件路径 g++需添加参数: -I/usr/local/include/hv

问题3: OpenSSL链接失败 现象: 缺少libssl/libcrypto符号 解决:

sudo apt install libssl-dev
添加链接参数: -lssl -lcrypto
问题4: 服务器连接失败 现象: 客户端显示立即断开 排查步骤:

确认服务进程运行: ps aux | grep tcp_echo_server
检查端口监听: ss -tulnp | grep 8888
关闭防火墙: sudo ufw disable 根本原因: 腾讯云安全组未放行8888端口 最终解决: 在腾讯云控制台添加8888/TCP入站规则
客户端问题: Windows编译错误 现象: 25个OpenSSL相关符号未解析 解决:

通过vcpkg安装openssl:x64-windows
VS2022配置:
包含目录添加vcpkg的include路径
链接器添加libssl.lib和libcrypto.lib
验证结果:

服务器启动命令: cd /home/ubuntu/cpp_projects && ./tcp_echo_server
成功测试记录: [连接成功] 106.53.108.139:8888 [消息收发] 客户端发送"你好" -> 收到相同回显 [延迟测试] 平均往返延迟27ms
经验总结:

云服务器需同时配置系统防火墙和安全组规则
跨平台开发时注意库的兼容性
建议将常用端口(如8888)提前加入白名单
重要日志及时保存到文件(nohup.out)
```

运行命令：

cd /home/ubuntu/cpp_projects && nohup ./tcp_echo_server & sleep 2 && ps aux | grep tcp_echo_server && ss -tulnp | grep 8888 这句命令是什么意思 ？

### 服务器

#### 概述

实例ID：lhins-qovbujxr

实例名称：Ubuntu-momoone

地域和可用区：广州  |  广州七区

系统版本： **Ubuntu Server 22.04 LTS 64位**。

CPU - 4核 内存 - 4GB
系统盘 - SSD云硬盘 40GB
流量包 - 300GB/月（带宽 3Mbps）

IPV4：(公)106.53.108.139	(内)10.1.0.2

IPV6：

桂ICP备2025075921号-1

#### **3Mbps带宽含义**

``` 
3Mbps = 3 Megabits per second（3兆比特每秒）
这是指公网出带宽（从服务器向外传输数据的速度）

实际速度换算：
理论下载速度：3Mbps ÷ 8 = 0.375MB/s（约384KB/s）
理论上传速度：通常与下载对称，也是0.375MB/s

实际使用场景：
下载文件：约384KB/s
网页访问：支持中小型网站正常访问
视频传输：可支持480p-720p视频流畅播放
并发用户：约支持10-20个并发用户访问

注意事项：
这是共享带宽，高峰期可能略有下降
实际速度受网络环境、距离、协议开销等因素影响
如果需要更高速度，可以考虑升级到5Mbps或10Mbps套餐
```

#### 密钥

``` 
密钥对（SSH Key Pair）说明：

是什么：

由公钥和私钥组成的安全认证方式
替代传统密码登录，更安全便捷
公钥放在服务器上，私钥保存在本地电脑
优势：

✅ 比密码登录更安全，防止暴力破解
✅ 免密码登录，一键连接
✅ 支持多设备管理
✅ 可设置密钥密码双重保护
如何使用：

1. 创建密钥对：

腾讯云控制台 → 轻量应用服务器 → 密钥对 → 创建密钥对
会自动生成并下载私钥文件（.pem格式）
2. 绑定到实例：

在实例详情页 → 绑定密钥对
重启实例后生效
3. 本地连接：

Linux/Mac：ssh -i 私钥文件.pem root@服务器IP
Windows：使用PuTTY或Xshell，导入私钥文件连接
4. 权限设置：

Linux/Mac：chmod 400 私钥文件.pem
Windows：无需额外设置
当前状态： 您还没有创建密钥对，建议立即创建一个来提升服务器安全性！
```

#### 公网内网

``` 
公网IP（106.53.108.139）
特点：

全球唯一，可直接从互联网访问
需要购买带宽（您的是3Mbps）
按流量或带宽计费
应用场景：

🌐 网站部署：对外提供Web服务
📧 邮件服务器：接收外部邮件
🔗 API接口：对外提供数据服务
🎮 游戏服务器：玩家从公网连接
📱 移动应用后端：APP访问的服务器
内网IP（10.1.0.2）
特点：

局域网地址，仅在腾讯云内部网络有效
免费使用，不计流量
安全性高，外部无法直接访问
应用场景：

🗄️ 数据库服务器：仅应用服务器访问
🔗 微服务通信：内部服务间调用
📊 文件存储：内部文件共享
🔄 负载均衡：后端服务器集群
🔒 管理后台：仅内部人员访问
实际使用建议：
典型架构：

BASH
公网用户 → 公网IP(106.53.108.139) → Web服务器
                                ↓
                        内网IP(10.1.0.x) → 数据库服务器
公网用户 → 公网IP(106.53.108.139) → Web服务器
                                ↓
                        内网IP(10.1.0.x) → 数据库服务器
安全最佳实践：

数据库、缓存等敏感服务放内网
Web服务器放公网并配置防火墙
通过内网IP访问数据库，避免暴露公网
费用优化：

内网流量完全免费
公网流量按带宽或流量计费
同地域实例间优先使用内网通信
```

#### API服务是什么

``` 
API服务是什么？

API（Application Programming Interface）服务就是服务器对外提供的数据接口，其他程序或网站可以通过这些接口获取或提交数据，而不需要直接访问数据库。

简单理解：

就像餐厅的点餐窗口，顾客（手机APP、网站）告诉窗口要什么菜（发送请求），窗口给什么菜（返回数据）
不需要顾客进入厨房（数据库）自己拿菜

实际例子：
天气APP：从天气API获取实时天气数据
淘宝商品：从商品API获取商品信息
微信登录：从微信API获取用户信息

为什么要安装Node.js和Python？

Node.js用途：
专门用于构建高性能API服务
JavaScript语言，前后端统一
处理大量并发请求能力强
适合实时应用（聊天、游戏）
安装简单：curl -fsSL https://deb.nodesource.com/setup_lts.x | sudo -E bash -

Python用途：

最流行的后端开发语言之一
语法简单，开发效率高
丰富的第三方库（Django、Flask框架）
适合数据处理、机器学习API
安装：sudo apt install python3 python3-pip

实际场景：
个人博客：用Node.js搭建，提供文章API
电商网站：用Python Django提供商品API
小程序：需要API提供数据给前端
移动APP：需要后端API处理业务逻辑
简单来说，如果你想让手机APP、网站、小程序能够访问你服务器上的数据，就需要搭建API服务，而Node.js和Python是最常用的工具。
```

#### 访问次数

``` 
像我的流量包每月300GB，而index.html大小为6k，那么每个月大概能打开网页多少次？

简单计算：

300GB = 300 × 1024 × 1024 KB = 314,572,800 KB

6KB的index.html理论上可以访问： 314,572,800 KB ÷ 6 KB ≈ 52,428,800 次

实际考虑：

HTTP协议开销：每次请求实际传输约8-10KB
浏览器缓存：用户第二次访问可能只消耗1-2KB
其他资源：如果网页有CSS、JS、图片等，每次访问会消耗更多流量
保守估计：

纯文本页面：约4000万次访问
含简单图片的页面：约1000万次访问
含多张图片的页面：约100万次访问
结论： 对于纯6KB的index.html，300GB流量包足够支持数千万次访问，完全不用担心流量问题。只有当网站包含大量图片、视频或下载文件时才需要关注流量消耗。
```

#### 服务器与实例

一台服务器可以同时运行多个实例吗？

``` 
需要澄清概念：

在云计算中：

你购买的"轻量应用服务器"本身就是一个独立的实例
一台物理服务器可以虚拟化出多个云服务器实例
每个实例都有独立的IP、系统、资源
你的情况：

你现在拥有的是1个轻量服务器实例（lhins-qovbujxr）
这个实例已经独立运行Ubuntu系统
可以在这个实例内运行多个程序/服务（如Nginx、MySQL、多个网站）
如果你想拥有多个独立的服务器：

需要在腾讯云购买多个轻量服务器实例
每个实例都是完全独立的服务器
每个实例都有独立的公网IP
实例内运行多个服务：

可以在当前服务器上运行：
多个网站（通过Nginx虚拟主机）
数据库服务
应用程序
Docker容器
总结：

一台物理服务器可以虚拟出多个云实例（需要购买）
一个云实例内可以运行多个程序/服务（你现在的做法）
你的300GB流量是针对当前这个实例的
```

所以我购买的"轻量应用服务器"只是一个实例，而不是一台物理服务器对吗？

``` 
完全正确！

你的理解完全正确：

你购买的"轻量应用服务器" = 1个云服务器实例

具体关系：

物理服务器：真实的硬件机器（你摸不到，在腾讯云机房）
云服务器实例：从物理服务器虚拟化出来的"虚拟电脑"
你的实例ID：lhins-qovbujxr 就是1个独立的虚拟服务器
实际体验：

对你来说，这个实例就是一台完整的服务器
有独立的CPU、内存、硬盘、IP地址
可以安装系统、软件，完全独立
其他用户的实例与你完全隔离
资源分配：

4核CPU = 物理CPU的4个虚拟核心
4GB内存 = 物理内存的4GB虚拟内存
40GB硬盘 = 存储系统的40GB虚拟空间
3Mbps带宽 = 网络接口的3Mbps虚拟带宽
好处：

成本低（比租用整台物理服务器便宜很多）
弹性扩展（可以随时升级配置）
管理简单（通过网页就能控制）
所以：

你现在有1台"虚拟服务器"在运行
这台虚拟服务器就是你的全部资源
300GB流量、4核4G配置都是这个实例独享的
```





### 新手下一步

``` 
Ubuntu 22.04 轻量服务器学习笔记

服务器基础信息 实例ID: lhins-qovbujxr 系统: Ubuntu Server 22.04 LTS 64位 公网IP: 106.53.108.139 内网IP: 10.1.0.2 带宽: 3Mbps（约384KB/s） 配置: 4核4G内存，40GB SSD

后续操作清单（按优先级排序）

第一阶段：安全加固（必做）

更新系统 sudo apt update && sudo apt upgrade -y

配置防火墙 查看当前规则：sudo ufw status 允许SSH：sudo ufw allow 22 允许HTTP/HTTPS：sudo ufw allow 80 和 sudo ufw allow 443 启用防火墙：sudo ufw enable

创建新用户（避免直接使用root） sudo adduser username sudo usermod -aG sudo username

SSH安全配置 修改SSH端口：编辑 /etc/ssh/sshd_config 禁用root登录：PermitRootLogin no 重启SSH：sudo systemctl restart sshd

第二阶段：环境配置

安装基础软件 sudo apt install curl wget git vim htop unzip -y

安装开发环境（根据需要选择） 
Web服务器: sudo apt install nginx apache2 
数据库: sudo apt install mysql-server postgresql 
PHP: sudo apt install php php-mysql 
Node.js: curl -fsSL https://deb.nodesource.com/setup_lts.x | sudo -E bash -

配置时区 sudo timedatectl set-timezone Asia/Shanghai

第三阶段：网站部署

Nginx配置 sudo nano /etc/nginx/sites-available/yourdomain sudo ln -s /etc/nginx/sites-available/yourdomain /etc/nginx/sites-enabled/ sudo nginx -t && sudo systemctl reload nginx

部署静态网站 网站根目录：/var/www/html/ 设置权限：sudo chown -R www-data:www-data /var/www/html/

SSL证书（Let's Encrypt免费证书） sudo apt install certbot python3-certbot-nginx sudo certbot --nginx -d yourdomain.com

第四阶段：监控与维护

资源监控 使用htop查看实时资源使用 腾讯云控制台查看监控图表

日志管理 系统日志：/var/log/syslog Nginx日志：/var/log/nginx/ 使用logrotate自动轮转日志

备份策略 数据库定期备份 网站文件同步到云存储 使用rsync进行增量备份

第五阶段：高级配置

SSH密钥登录 本地生成密钥：ssh-keygen -t rsa -b 4096 上传公钥到服务器：ssh-copy-id username@106.53.108.139

防火墙模板 腾讯云控制台创建防火墙规则模板 应用到多个实例

快照备份 创建系统盘快照 设置定期自动快照

常用命令速查

系统信息 lsb_release -a 查看系统版本 uname -a 查看内核版本 df -h 查看磁盘使用 free -h 查看内存使用

网络相关 ip addr show 查看IP地址 netstat -tulpn 查看端口占用 ss -tulpn 查看端口占用（新版）

服务管理 systemctl status nginx 查看服务状态 systemctl start nginx 启动服务 systemctl enable nginx 开机自启

注意事项

安全组规则：腾讯云控制台也要配置安全组
密码强度：使用复杂密码或密钥认证
定期更新：每月至少更新一次系统
监控告警：设置CPU、内存、带宽告警
数据备份：重要数据至少保留3个备份

下一步建议 根据您的具体需求，可以选择： 
个人博客：直接安装WordPress 
开发测试：配置Docker环境 
API服务：安装Node.js/Python环境 
学习Linux：从基础命令开始练习
```

#### 静态网站部署成功流程总结

``` 
静态网站部署成功流程总结

域名：momoone.cloud 服务器：Ubuntu 22.04轻量服务器（106.53.108.139）

完整流程回顾：

域名解析设置 登录腾讯云控制台 → 域名注册 → 找到momoone.cloud → 点击解析 → 添加两条A记录： 主机记录@，记录值106.53.108.139 主机记录www，记录值106.53.108.139 等待5-30分钟生效，用ping momoone.cloud验证

服务器环境准备 通过腾讯云控制台 → 轻量应用服务器 → OrcaTerm登录 执行： 
sudo apt update 
sudo apt install nginx -y 
sudo systemctl start nginx 
sudo systemctl enable nginx

网站文件部署 创建目录：sudo mkdir -p /var/www/momoone.cloud 上传文件：通过文件管理器上传index.html到该目录 设置权限：sudo chown -R www-data:www-data /var/www/momoone.cloud

Nginx配置 
创建配置文件：sudo nano /etc/nginx/sites-available/momoone.cloud 
内容： 
server { listen 80; server_name momoone.cloud www.momoone.cloud; root /var/www/momoone.cloud; index index.html; location / { try_files $uri $uri/ =404; } gzip on; gzip_types text/plain text/css application/json application/javascript text/xml application/xml application/xml+rss text/javascript; }

启用站点： 
sudo ln -s /etc/nginx/sites-available/momoone.cloud /etc/nginx/sites-enabled/ 
sudo rm /etc/nginx/sites-enabled/default 
sudo nginx -t 
sudo systemctl reload nginx

验证成功 浏览器访问http://momoone.cloud看到网页即成功
新手注意事项： 
每条命令单独执行，不要合并 
配置文件注意大括号配对 
修改文件后记得sudo nginx -t测试 
出现问题查看日志：sudo tail -f /var/log/nginx/error.log

下一步学习方向： 
学习HTML/CSS美化页面 
了解SSL证书配置HTTPS 
学习Linux基础命令 
研究Nginx更多配置选项 
学习文件上传和备份方法
```

#### Ubuntu Linux 新手到进阶学习计划

``` 
Ubuntu Linux 新手到进阶学习计划

第一阶段：基础入门（第1-2周） 目标：熟悉基本操作，能独立管理文件和系统

每日任务： 第1天：认识Linux和Ubuntu

了解Linux发行版概念
学习Ubuntu版本号含义
掌握终端打开方式
第2-3天：文件系统基础

学习/根目录结构
掌握cd、ls、pwd命令
练习绝对路径和相对路径
作业：在/home目录下创建个人文件夹结构
第4-5天：文件操作

掌握cp、mv、rm、mkdir、touch
学习通配符*和?的使用
理解文件权限rwx
作业：整理下载文件夹，按类型分类
第6-7天：文本编辑

学习nano基本使用
掌握cat、more、less查看文件
学习grep搜索文本
作业：创建并编辑个人简历文件
第二阶段：系统管理（第3-4周） 目标：掌握用户管理、软件安装、系统监控

第8-10天：用户和权限

学习sudo原理
掌握adduser、usermod、passwd
理解用户组概念
作业：创建开发用户并配置权限
第11-13天：软件管理

学习apt包管理器
掌握update、upgrade、install、remove
了解PPA源
作业：安装开发工具链（git、vim、curl）
第14-16天：系统监控

学习top、htop、df、free命令
掌握systemctl服务管理
了解日志文件位置
作业：设置定时重启服务
第三阶段：网络服务（第5-6周） 目标：搭建常用网络服务

第17-19天：Web服务器

深入学习Nginx配置
学习虚拟主机概念
掌握反向代理配置
作业：搭建个人博客
第20-22天：数据库基础

学习MySQL安装和配置
掌握基本SQL语句
了解用户权限管理
作业：创建产品数据库
第23-25天：文件传输

学习SSH密钥登录
掌握scp、rsync使用
了解FTP/SFTP概念
作业：设置自动备份脚本
第四阶段：安全加固（第7-8周） 目标：掌握基本安全防护

第26-28天：防火墙配置

深入学习ufw防火墙
学习iptables基础
掌握端口管理
作业：配置Web服务器安全规则
第29-31天：SSH安全

学习SSH配置文件
掌握密钥认证
了解fail2ban防暴力破解
作业：禁用密码登录
第32-34天：数据备份

学习tar、rsync备份
掌握定时任务crontab
了解云存储备份
作业：设置每日自动备份
第五阶段：进阶技能（第9-12周） 目标：掌握脚本编程和自动化

第35-38天：Shell脚本

学习bash基础语法
掌握变量、循环、条件判断
学习函数定义
作业：编写系统监控脚本
第39-42天：Docker容器

学习容器概念
掌握Docker基本命令
了解docker-compose
作业：容器化部署应用
第43-46天：Python自动化

学习Python基础
掌握文件操作
学习系统管理库
作业：编写自动化部署脚本
第47-50天：监控告警

学习Prometheus基础
掌握Grafana可视化
了解日志分析
作业：搭建监控面板
每日学习安排： 上午：理论学习（30分钟） 下午：动手实践（1小时） 晚上：总结记录（15分钟）

学习资源： 官方文档：ubuntu.com/tutorials 在线教程：linuxcommand.org 视频课程：B站Ubuntu入门 实践平台：自己的轻量服务器

每周检查点： 第1周：能熟练操作文件和目录 第2周：能独立安装软件和配置用户 第3周：能搭建基本Web服务 第4周：能配置基本安全防护 第8周：能编写简单自动化脚本 第12周：能独立管理生产服务器

常见错误提醒：

命令区分大小写
注意空格和特殊字符
重要操作前备份
学会查看错误信息
善用man手册
进阶路线图： 初级：用户管理 → 文件操作 → 软件安装 中级：网络配置 → 服务部署 → 安全设置 高级：脚本编程 → 自动化 → 监控告警 专家：系统优化 → 架构设计 → 故障排查

建议学习顺序： 先掌握基础命令，再学习系统管理，然后搭建服务，最后学习自动化和脚本编程。每个阶段都要配合实际项目练习，比如搭建个人网站、文件服务器、监控系统等。
```



## Lite开发

[Search | DeepWiki](https://deepwiki.com/search/cc-window64vs2022unicodec17utf_6a9794a9-74a9-4ef7-a9fc-a51838e768b7?mode=fast)

### Lua静态库

``` 
git clone https://kkgithub.com/lua/lua.git
 
在VS2022中创建空白项目，然后将lua源代码的所有h/c文件通过添加现有项添加到VS项目中，然后移除：
	onelua.c(导致当前问题的根源)
	lua.c(Lua解释器的入口，包含 main函数)
	luac.c(Lua编译器的入口，也包含 main函数)
	
在项目属性中设置【配置属性】为lib，设置【目标文件名】后进行编译，得到形如lua55.lib的静态库文件。

在使用 lua55.lib时，你不需要包含所有的头文件，只需引入几个核心头文件即可。下面这个表格列出了必需和可选的头文件，能帮你快速把握。

头文件名称

必要性

主要作用描述

lua.h

必需​

Lua C API 的核心，包含创建状态机、操作栈等基础函数。

lauxlib.h

必需​

提供辅助库函数（如 luaL_newstate, luaL_dofile），简化常见操作。

lualib.h

通常需要​

用于打开 Lua 标准库（如 luaopen_base, luaopen_math）。

lua.hpp

可选（C++项目友好）

C++ 包装头文件，已内部使用 extern "C"包含上述三个核心头文件。
```



### 下一步

``` 
1. 文本选择功能 docview.lua:195-213
作用: 允许用户通过鼠标拖拽或Shift+方向键选择文本
实现:
鼠标按下时记录起始位置
鼠标移动时更新选择终点
支持单击(字符选择)、双击(单词选择)、三击(行选择)
绘制选择区域的高亮背景 docview.lua:310-319
2. 撤销/重做系统 init.lua:229-262
作用: 允许用户撤销或重做编辑操作
实现:
维护undo_stack和redo_stack
每次文本修改时记录操作类型(insert/remove)和参数
时间窗口内的连续操作自动合并
Ctrl+Z撤销,Ctrl+Y重做
3. 剪贴板操作 doc.lua:66-90
作用: 支持复制、剪切、粘贴文本
实现:
复制(Ctrl+C): 将选中文本复制到系统剪贴板
剪切(Ctrl+X): 复制后删除选中文本
粘贴(Ctrl+V): 从剪贴板插入文本
4. 文件操作 init.lua:68-114
作用: 加载和保存文件
实现:
打开文件: 读取文件内容到文档
保存文件: 将文档内容写入文件
检测文件修改状态(dirty标志)
保存时处理CRLF/LF行尾
5. 滚动和视口管理 docview.lua:171-192
作用: 处理大文件时的滚动和可见区域管理
实现:
计算可见行范围,只渲染可见部分
自动滚动以保持光标可见
支持鼠标滚轮和滚动条
6. 语法高亮 docview.lua:296-303
作用: 根据文件类型对代码进行着色
实现:
使用highlighter模块进行词法分析
为不同token类型应用不同颜色
支持多种编程语言
7. 多光标支持
作用: 同时在多个位置编辑文本
实现:
维护多个光标位置
所有编辑操作同时应用到所有光标
8. 查找和替换 doc.lua:137-180
作用: 在文档中搜索和替换文本
实现:
正则表达式或普通文本搜索
单个替换或全部替换
高亮显示匹配结果
```

### data目录下脚本

``` 
Core核心模块 (data/core/)
data/core/init.lua - 核心初始化模块,负责启动编辑器、加载插件、用户模块和项目模块 init.lua:77-126

data/core/common.lua - 通用工具函数库,提供模糊匹配、路径处理、颜色转换等辅助功能 common.lua:91-140

data/core/config.lua - 配置管理模块,存储编辑器的各项配置参数

data/core/style.lua - 样式定义模块,管理颜色、字体、间距等UI样式

data/core/keymap.lua - 键盘映射模块,处理快捷键绑定和按键事件

data/core/command.lua - 命令系统模块,管理命令注册和执行

data/core/object.lua - 基础对象类,提供面向对象编程的基础

data/core/view.lua - 视图基类,所有UI组件的父类

data/core/rootview.lua - 根视图模块,管理窗口布局和视图树 rootview.lua:1-60

data/core/docview.lua - 文档视图模块,负责渲染和交互文本文档 docview.lua:91-150

data/core/statusview.lua - 状态栏视图,显示编辑器底部状态信息

data/core/commandview.lua - 命令输入视图,处理命令面板交互

data/core/logview.lua - 日志视图,显示编辑器日志信息

data/core/syntax.lua - 语法高亮系统,管理语法定义和高亮规则

文档子模块 (data/core/doc/)
data/core/doc/init.lua - 文档数据模型,管理文本内容、选择、撤销/重做 init.lua:31-90

data/core/doc/translate.lua - 位置转换函数,提供光标移动的各种方式 translate.lua:1-60

data/core/doc/highlighter.lua - 语法高亮器,实现实时语法着色

命令模块 (data/core/commands/)
data/core/commands/core.lua - 核心命令,包括文件打开、查找、模块管理等 core.lua:31-101

data/core/commands/doc.lua - 文档编辑命令,包括复制粘贴、缩进、大小写转换等 doc.lua:1-60

data/core/commands/findreplace.lua - 查找替换命令,实现文本搜索和替换功能

插件 (data/plugins/)
data/plugins/language_lua.lua - Lua语言支持插件,定义Lua语法高亮规则 language_lua.lua:1-50

data/plugins/treeview.lua - 文件树视图插件,显示项目文件结构 treeview.lua:61-120

data/plugins/projectsearch.lua - 项目搜索插件,在整个项目中搜索文本 projectsearch.lua:31-90

data/plugins/autoreload.lua - 自动重载插件,检测文件外部修改并自动重载

data/plugins/autocomplete.lua - 自动补全插件,提供代码补全功能

data/plugins/language_*.lua - 各种编程语言的语法高亮插件(C, Python, JS等)

字体资源 (data/fonts/)
data/fonts/FiraSans-Regular.ttf - 默认UI字体

data/fonts/JetBrainsMono-Regular.ttf - 默认代码字体

data/fonts/icons.ttf - 图标字体

用户目录 (data/user/)
data/user/init.lua - 用户配置模块,用户自定义设置的入口点

data/user/colors/ - 用户颜色主题目录

Notes
核心模块负责编辑器的基础架构和功能
插件系统通过data/plugins/目录实现扩展 init.lua:175-188
用户可通过data/user/init.lua自定义配置
所有Lua文件都是模块,通过require加载
```

## 多媒体

### FFMPEG编程相

``` 
开发环境：windows11，C++17，VS2022，Unicode宽字符，代码页65001，入口函数int main(int argc,char* argv[])方便控制台输出调试信息，静态库链接使用#pragma comment方式，使用cstdint，类成员函数小驼峰。类声明部分(数据成员和函数成员)附加简洁的中文注释。 
现在我下载好了FFMPEG8.0开发包，想使用它开发一个简单的播放器。请参考ffplay.c代码，使用WIN32本地窗口取代SDL，使用GDI绘制视频帧，使用waveout相关函数播放音频，并实现精确的音视频同步机制。
视频帧要转换为以下结构：
typedef struct OBitmap_t {
    void* data;             // 图像原始数据
    int width;              // 原始宽度
    int height;             // 原始宽度
    int bpp;				// 每个像素需要多少个比特bits
    int pitch;				//(((8*img.width) + 31) >> 5) << 2; 如果4字节对齐，每行需要的字节byte数目
    int pixSize;			// 像素数据 大小=pitch*height
    int tag;               // 字体渲染中的top度量，或者纹理的Mipmap levels, 1 by default
    int format;             // 像素格式，和OGL的对齐。
}OBitmap;
渲染时再使用GDI绘制OBitmap。
同样，音频帧要转换为以下结构：
typedef struct Sound {
    uint32_t channels;       // 声道数（1=单声道，2=立体声）
    uint32_t sampleRate;     // 采样率（Hz，如44100）
    uint32_t bitsPerSample;  // 采样位深（16/24/32位）
    uint64_t numSamples;     // 总采样数（单声道计数）
    size_t   dataSize;       // PCM数据字节数 = numSamples * channels * (bitsPerSample/8)
    void* data;           // PCM数据指针（需根据位深转换访问）

    // 计算字段
    double   duration;       // 音频时长（秒）numSamples/(double)sampleRate
    uint32_t byteRate;       // 字节速率 sampleRate * channels * (bitsPerSample/8)
    uint32_t playms;        //  开始播放的时间
} OSound;
然后再使用waveout相关函数播放OSound所代表的音频。

控制方面，使用空格键实现播放/暂停，左右箭头实现前后seek10秒(注意不要超过视频时长范围)。
所有代码集成到文件DXPlayer.cpp中，代码要完整，不要分开在多个文件中，中间的实现不要省略，因为我要复制粘贴直接测试。
测试文件名为test.mp4。
然后，我的开发环境需要如以下形式包含对应的头文件和静态库：
// DXPlayer.cpp - 完整的GDI+WaveOut播放器实现  
#pragma comment(lib, "avformat.lib")  
#pragma comment(lib, "avcodec.lib")   
#pragma comment(lib, "avutil.lib")  
#pragma comment(lib, "swscale.lib")  
#pragma comment(lib, "swresample.lib")  
#pragma comment(lib, "winmm.lib")  

#include <windows.h>  
#include <mmreg.h>  
#include <mmsystem.h>  


extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
}

#include <thread>  
#include <atomic>  
#include <queue>  
#include <mutex>  
#include <condition_variable>  
#include <string>  
#include <algorithm>  
#include <cmath>  
#include <limits>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cwchar>

使用中文回复我。

继续剩下的代码，请注意衔接处的完整。

那你能不能实现一个简单示例，读取并解析文件"D:\ffbin\fuyin.mp4"，然后每隔10抽一帧，总共100帧，使用stb_image_write.h保存为png图片，目录为当前文件夹下的IMGSAVE目录(若不存在则先创建)。程序过程中要显示相应的调试信息，给我完整的代码。

我现在需要获取并打印一个媒体文件的相关信息，包括视频流/音频流，你觉得如何设计相关的结构体？
另外，控制台显示信息请使用以下宏和相关设置
#include <cwchar>   // 提供 std::wprintf、宽字符相关的 C 函数
#include <fcntl.h>  // 提供 _O_U16TEXT 等文件/模式常量
#define ODD(...) wprintf(__VA_ARGS__)
// 控制台颜色设置  
void setConsoleColor(int32_t color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
system("cls");// 清屏：调用 Windows shell 的 CLS 命令清除控制台内容，方便每次重绘播放器状态（注意：system 调用开销和安全性）
//main:	添加
_setmode(_fileno(stdout), _O_U16TEXT);
程序过程中要显示相应的调试信息，给我完整的代码。
```



### libwebm+opus

如何编译成静态库.lib（非符号导入库） 开发环境：window64平台，VS2022，Unicode字符集，C++17标准，utf8 with BOM （代码页65001） 请使用中文回复。

编译：

``` 
总结：
环境：使用了Visual Studio 2022 Developer Command Prompt（即VS开发环境），确保编译器为MSVC。

步骤：

进入libwebm源码目录：E:\Dev\KU\libwebm

创建并进入构建目录：mkdir build_ninja_msv然后 cd build_ninja_msv

运行CMake配置：cmake .. -G Ninja -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release

编译：ninja

结果：在build_ninja_msv目录下生成了静态库（如mkvparser.lib、mkvmuxer.lib等）以及一些示例可执行文件（如webm_info.exe）。
```



opus

``` 
https://deepwiki.com/search/lib-window64vs2022unicodec17ut_a647253f-8022-4331-bf12-6a62b5e3d1ac?mode=fast

git clone https://gitee.com/mirrors_sailfishos-mirror/opus.git
cd opus  
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release  
cmake --build build --config Release

构建完成后，静态库将位于：build/Release/opus.lib  
头文件安装在：build/include/opus/
```

libvpx

``` 
git clone https://gitee.com/mirrors_sailfishos-mirror/libvpx.git

# 从build目录执行，应该使用：  
../configure --target=x86_64-win64-vs17 --enable-static --disable-shared --enable-vp8 --enable-vp9 --enable-vp8-encoder --enable-vp8-decoder --enable-vp9-encoder --enable-vp9-decoder --disable-examples --disable-tools --disable-docs

make
```







### WIN32 WAV播放

在 Win32 下用 waveOut 播放音频的典型流程是 ：

1. →打开设备（waveOutOpen）
2. → 准备 PCM 格式（WAVEFORMATEX）
3. → 准备缓冲区（WAVEHDR + waveOutPrepareHeader）
4. → 写入播放（waveOutWrite）
5. → 等待/处理完成回调（WOM_DONE）
6. → 解除准备并关闭设备（waveOutUnprepareHeader / waveOutClose）。



常用头文件：<windows.h>、<mmsystem.h>，链接库 winmm.lib。

``` c++
typedef struct tWAVEFORMATEX
{
    WORD  wFormatTag;        /* 音频格式类型（例如：PCM、IEEE浮点数） */
    WORD  nChannels;         /* 声道数量（1为单声道，2为立体声） */
    DWORD nSamplesPerSec;    /* 采样率（每秒样本数，单位：Hz） */
    DWORD nAvgBytesPerSec;   /* 平均数据传输速率（字节/秒），用于缓冲区估算 */
    WORD  nBlockAlign;       /* 数据块对齐（单位：字节），每个样本帧的大小 */
    WORD  wBitsPerSample;    /* 每个样本的位数（即采样位深，如8位、16位） */
    WORD  cbSize;            /* 附加信息的字节大小（位于本结构体之后） */
} WAVEFORMATEX;

typedef struct wavehdr_tag {
    LPSTR       lpData;           /* 指向音频数据缓冲区的指针 */
    DWORD       dwBufferLength;   /* 数据缓冲区的长度（以字节为单位） */
    DWORD       dwBytesRecorded;  /* 实际录制的数据量（主要用于输入） */
    DWORD_PTR   dwUser;           /* 用户自定义数据，供应用程序使用 */
    DWORD       dwFlags;          /* 标志位，表示缓冲区的状态（如 WHDR_DONE、WHDR_PREPARED 等） */
    DWORD       dwLoops;          /* 循环播放次数（用于输出缓冲区的循环控制） */
    struct wavehdr_tag FAR *lpNext;     /* 保留字段，供驱动程序内部使用 */
    DWORD_PTR   reserved;         /* 保留字段，供驱动程序内部使用 */
} WAVEHDR;

// 打开波形音频输出设备以进行播放
MMRESULT waveOutOpen(
    LPHWAVEOUT      phwo,        // 输出参数，用于接收设备句柄的指针
    UINT            uDeviceID,   // 输入参数，指定要打开的设备ID，可使用WAVE_MAPPER由系统选择
    LPCWAVEFORMATEX pwfx,        // 输入参数，指向包含音频格式（如采样率、位深度）的WAVEFORMATEX结构
    DWORD_PTR       dwCallback,  // 输入参数，指定回调机制（如函数指针、窗口句柄、事件句柄等）
    DWORD_PTR       dwInstance,  // 输入参数，传递给回调函数的用户自定义数据，通常可设为0
    DWORD           fdwOpen      // 输入参数，打开设备的标志（如CALLBACK_FUNCTION、CALLBACK_EVENT等）
);

// 准备一个波形音频数据块以供播放，必须调用此函数后才能传递数据给设备
MMRESULT waveOutPrepareHeader(
    HWAVEOUT  hwo,  // 输入参数，由waveOutOpen获得的波形音频输出设备句柄
    LPWAVEHDR pwh,  // 输入/输出参数，指向包含音频数据块信息的WAVEHDR结构
    UINT      cbwh  // 输入参数，WAVEHDR结构的大小，通常为sizeof(WAVEHDR)
);

// 将准备好的数据块发送到音频输出设备开始播放
MMRESULT waveOutWrite(
    HWAVEOUT hwo,  // 输入参数，波形音频输出设备句柄
    LPWAVEHDR pwh, // 输入参数，指向已通过waveOutPrepareHeader准备的WAVEHDR结构
    UINT cbwh      // 输入参数，WAVEHDR结构的大小，通常为sizeof(WAVEHDR)
);

// 清理waveOutPrepareHeader所做的准备工作，在释放音频缓冲区前必须调用
MMRESULT waveOutUnprepareHeader(
    HWAVEOUT hwo,  // 输入参数，波形音频输出设备句柄
    LPWAVEHDR pwh, // 输入参数，指向要清理的WAVEHDR结构
    UINT cbwh      // 输入参数，WAVEHDR结构的大小，通常为sizeof(WAVEHDR)
);

// 暂停波形音频输出设备的播放
MMRESULT waveOutPause(
    HWAVEOUT hwo   // 输入参数，波形音频输出设备句柄
);

// 从暂停状态恢复波形音频输出设备的播放
MMRESULT waveOutRestart(
    HWAVEOUT hwo   // 输入参数，波形音频输出设备句柄
);

// 停止波形音频输出设备的播放，并标记所有缓冲区为已完成
MMRESULT waveOutReset(
    HWAVEOUT hwo   // 输入参数，波形音频输出设备句柄
);

// 关闭先前打开的波形音频输出设备
MMRESULT waveOutClose(
    HWAVEOUT hwo   // 输入参数，要关闭的波形音频输出设备句柄
);

// 获取波形音频输出设备的音量级别
MMRESULT waveOutGetVolume(
    HWAVEOUT hwo,      // 输入参数，波形音频输出设备句柄或设备标识符
    LPDWORD pdwVolume  // 输出参数，指向接收音量值的变量的指针
);

// 设置波形音频输出设备的音量级别
MMRESULT waveOutSetVolume(
    HWAVEOUT hwo,  // 输入参数，波形音频输出设备句柄或设备标识符
    DWORD dwVolume // 输入参数，新的音量设置值
);
```

**播放示例**

``` c++
#include <iostream>
#include <cmath>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// 回调函数，处理播放完成事件
void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        std::cout << "音频播放完成" << std::endl;
    }
}

// 生成正弦波PCM数据
void generateSineWave(BYTE* buffer, DWORD bufferSize, double frequency, DWORD sampleRate, WORD bitsPerSample, WORD channels) {
    if (bitsPerSample == 16) {
        // 16位PCM数据生成
        short* samples = (short*)buffer;
        DWORD numSamples = bufferSize / (bitsPerSample / 8) / channels;
        
        for (DWORD i = 0; i < numSamples; i++) {
            double time = (double)i / sampleRate;
            double value = sin(2.0 * M_PI * frequency * time);
            short sampleValue = (short)(value * 32767); // 16位有符号整数范围
            
            // 交错存储多声道数据
            for (WORD ch = 0; ch < channels; ch++) {
                samples[i * channels + ch] = sampleValue;
            }
        }
    }
}

int main() {
    std::cout << "开始生成并播放PCM音频..." << std::endl;
    
    // 音频参数设置
    const DWORD SAMPLE_RATE = 44100;        // 44.1kHz采样率
    const WORD BITS_PER_SAMPLE = 16;        // 16位采样精度
    const WORD CHANNELS = 2;                // 立体声
    const double FREQUENCY = 440.0;         // 440Hz (标准A音)
    const DWORD DURATION_MS = 1000;         // 1秒时长
    
    // 计算缓冲区大小（采样数 × 声道数 × 每样本字节数）
    DWORD bufferSize = (DWORD)((SAMPLE_RATE * DURATION_MS / 1000.0) * CHANNELS * (BITS_PER_SAMPLE / 8));
    
    // 分配音频数据缓冲区
    BYTE* audioData = new BYTE[bufferSize];
    if (!audioData) {
        std::cout << "内存分配失败" << std::endl;
        return 1;
    }
    
    // 生成正弦波数据
    std::cout << "生成" << FREQUENCY << "Hz正弦波..." << std::endl;
    generateSineWave(audioData, bufferSize, FREQUENCY, SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS);
    
    // 设置音频格式
    WAVEFORMATEX waveFormat;
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;      // PCM格式
    waveFormat.nChannels = CHANNELS;              // 声道数
    waveFormat.nSamplesPerSec = SAMPLE_RATE;      // 采样率
    waveFormat.wBitsPerSample = BITS_PER_SAMPLE; // 采样位数
    waveFormat.nBlockAlign = CHANNELS * BITS_PER_SAMPLE / 8;           // 块对齐
    waveFormat.nAvgBytesPerSec = SAMPLE_RATE * waveFormat.nBlockAlign; // 平均字节率
    waveFormat.cbSize = 0;                        // 无额外信息
    
    // 打开音频设备
    HWAVEOUT hWaveOut;
    MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &waveFormat, 
                                 (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION);
    
    if (result != MMSYSERR_NOERROR) {
        std::cout << "无法打开音频设备，错误码: " << result << std::endl;
        delete[] audioData;
        return 1;
    }
    
    std::cout << "音频设备打开成功" << std::endl;
    
    // 准备WAVE头
    WAVEHDR waveHeader;
    ZeroMemory(&waveHeader, sizeof(WAVEHDR));
    waveHeader.lpData = (LPSTR)audioData;
    waveHeader.dwBufferLength = bufferSize;
    waveHeader.dwFlags = 0;
    
    result = waveOutPrepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        std::cout << "准备音频头失败，错误码: " << result << std::endl;
        waveOutClose(hWaveOut);
        delete[] audioData;
        return 1;
    }
    
    // 播放音频
    std::cout << "开始播放音频..." << std::endl;
    result = waveOutWrite(hWaveOut, &waveHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        std::cout << "播放音频失败，错误码: " << result << std::endl;
        waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);
        delete[] audioData;
        return 1;
    }
    
    // 等待播放完成
    std::cout << "等待播放完成..." << std::endl;
    while ((waveHeader.dwFlags & WHDR_DONE) == 0) {
        Sleep(100);
    }
    
    // 清理资源
    std::cout << "清理资源..." << std::endl;
    waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(WAVEHDR));
    waveOutClose(hWaveOut);
    delete[] audioData;
    
    std::cout << "程序执行完成" << std::endl;
    return 0;
}
```

### MDecoder实现

以下代码可以做到A/V同步。可用于备份。

``` c++

// 【OK】
#if 1




/****************************************************************************
 * 标题: Ogg多媒体解码器 (Theora+Vorbis)
 * 文件: MDecoder.cpp
 * 版本: 0.1
 * 作者: AEGLOVE
 * 日期: 2025-11-29
 * 功能: 基于Ogg容器的多线程解码器，支持Theora视频与Vorbis音频；
 *      提供`MDecoder`类，输出`VideoFrame`与`AudioPacket`链表结构；
 *      设计考虑线程安全、播放时间戳、搜索代次。
 * 依赖: Win32 API, libogg, libtheora, libvorbis
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/

 // 统一使用C++17与Unicode
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>

#define NOMINMAX
#include <windows.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <theora/theoradec.h>

#pragma comment(lib, "libogg.lib")
#pragma comment(lib, "libtheora.lib")
#pragma comment(lib, "libvorbis.lib")
#pragma comment(lib, "winmm.lib")

//---------------------------------------------------------------------------
// 像素格式枚举（可根据项目扩展）
//---------------------------------------------------------------------------
enum class VideoFormat : std::uint32_t {
    Unknown = 0,
    ARGB32 = 1,    // 8:8:8:8
    YUV420P = 2
};

//---------------------------------------------------------------------------
// 优化后的数据结构
// - 使用<stdint.h>/<cstdint>中的固定宽度整数
// - 明确所有权：`pixels`和`samples`使用堆分配，调用者负责释放，或通过辅助函数释放
// - 增加`dataBytes`/`stride`便于渲染或传输
// - 保持链表接口以兼容现有设计
//---------------------------------------------------------------------------
typedef struct VideoFrame
{
    std::uint32_t seek_generation;  // 搜索代次，用于丢弃旧帧
    std::uint32_t playms;           // 预定播放时间戳(ms)
    double        fps;              // 帧率
    std::uint32_t width;            // 像素宽
    std::uint32_t height;           // 像素高
    VideoFormat   format;           // 像素格式
    std::uint32_t stride;           // 每行字节数(如ARGB32: width*4)
    std::uint32_t dataBytes;        // 像素数据总字节数
    std::uint8_t* pixels;           // 原始像素缓冲(堆分配)
    VideoFrame* next;             // 链表下一项
} VideoFrame;

typedef struct AudioPacket
{
    std::uint32_t seek_generation;  // 搜索代次，用于丢弃旧音频包
    std::uint32_t playms;           // 预定播放起始时间(ms)
    std::int32_t  channels;         // 声道数
    std::int32_t  freq;             // 采样率(Hz)
    std::int32_t  frames;           // 本包包含的帧数量(每帧=channels个样本)
    std::uint32_t dataFloats;       // 样本总数=frames*channels
    float* samples;          // 线性PCM(f32)，长度为dataFloats
    AudioPacket* next;             // 链表下一项
} AudioPacket;

// 简单的辅助释放函数，确保易用性
static inline void freeVideoFrameChain(VideoFrame* head) {
    while (head) {
        VideoFrame* nxt = head->next;
        if (head->pixels) { free(head->pixels); head->pixels = nullptr; }
        free(head);
        head = nxt;
    }
}
static inline void freeAudioPacketChain(AudioPacket* head) {
    while (head) {
        AudioPacket* nxt = head->next;
        if (head->samples) { free(head->samples); head->samples = nullptr; }
        free(head);
        head = nxt;
    }
}

//---------------------------------------------------------------------------
// MDecoder: 多线程Ogg解码器 (Theora + Vorbis)
// - 读取Ogg文件，解析并在后台线程解码为VideoFrame/AudioPacket链表
// - 提供拉取接口popVideo/popAudio用于获取已解码的数据
// - 支持停止与代次标识
//---------------------------------------------------------------------------
class MDecoder {
public:
    MDecoder() = default;
    ~MDecoder() { stop(); }

    bool start(const std::wstring& path) {
        stop();
        // 打开文件
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) {
            std::fwprintf(stderr, L"[MDecoder] open failed: %ls\n", path.c_str());
            return false;
        }
        infile = f;
        seekGeneration.store(seekGeneration.load() + 1); // 递增代次
        quit.store(false);
        eof.store(false);
        // 启动线程
        worker = std::thread(&MDecoder::decodeThreadProc, this);
        return true;
    }

    void stop() {
        quit.store(true);
        if (worker.joinable()) worker.join();
        cleanup();
    }

    // 拉取一帧视频（返回链表头，调用者消费后负责释放）
    VideoFrame* popVideo() {
        std::lock_guard<std::mutex> lock(mtxVideo);
        VideoFrame* head = videoHead;
        videoHead = nullptr;
        videoTail = nullptr;
        return head;
    }

    // 拉取一段音频（返回链表头，调用者消费后负责释放）
    AudioPacket* popAudio() {
        std::lock_guard<std::mutex> lock(mtxAudio);
        AudioPacket* head = audioHead;
        audioHead = nullptr;
        audioTail = nullptr;
        return head;
    }

private:
    // 线程过程：参考SDLPlayer5.cpp的解码流程，输出链表
    void decodeThreadProc() {
        initCodecStates();
        if (!probeHeaders()) { cleanup(); return; }
        if (!initDecoders()) { cleanup(); return; }
        decodeLoop();
        cleanupCodecStates();
        eof.store(true);
    }

    // 初始化基础Ogg/Theora/Vorbis状态
    void initCodecStates() {
        ogg_sync_init(&oy);
        vorbis_info_init(&vi);
        vorbis_comment_init(&vc);
        th_info_init(&ti);
        th_comment_init(&tc);
    }
    void cleanupCodecStates() {
        if (hasVorbis) {
            ogg_stream_clear(&vo);
            vorbis_block_clear(&vb);
            vorbis_dsp_clear(&vd);
            vorbis_comment_clear(&vc);
            vorbis_info_clear(&vi);
        }
        if (hasTheora) {
            ogg_stream_clear(&to);
            if (td) th_decode_free(td);
            th_comment_clear(&tc);
            th_info_clear(&ti);
        }
        ogg_sync_clear(&oy);
    }

    bool probeHeaders() {
        ogg_packet op{};
        hasTheora = false; hasVorbis = false; theoraHeaders = 0; vorbisHeaders = 0;
        // 识别流
        while (!quit.load() && (!hasTheora || !hasVorbis)) {
            if (bufferData() == 0) { std::fprintf(stderr, "[MDecoder] EOF while probing.\n"); return false; }
            while (ogg_sync_pageout(&oy, &og) > 0) {
                ogg_stream_state test{};
                if (!ogg_page_bos(&og)) {
                    if (hasTheora) ogg_stream_pagein(&to, &og);
                    if (hasVorbis) ogg_stream_pagein(&vo, &og);
                    continue;
                }
                ogg_stream_init(&test, ogg_page_serialno(&og));
                ogg_stream_pagein(&test, &og);
                ogg_stream_packetout(&test, &op);
                if (!hasTheora && th_decode_headerin(&ti, &tc, &ts, &op) >= 0) {
                    std::memcpy(&to, &test, sizeof(test)); hasTheora = true; theoraHeaders = 1;
                }
                else if (!hasVorbis && vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {
                    std::memcpy(&vo, &test, sizeof(test)); hasVorbis = true; vorbisHeaders = 1;
                }
                else {
                    ogg_stream_clear(&test);
                }
            }
        }
        if (!hasTheora && !hasVorbis) return false;
        // 解析剩余头
        while ((hasTheora && theoraHeaders < 3) || (hasVorbis && vorbisHeaders < 3)) {
            while (hasTheora && theoraHeaders < 3 && ogg_stream_packetout(&to, &op)) {
                if (!th_decode_headerin(&ti, &tc, &ts, &op)) { std::fprintf(stderr, "[MDecoder] Theora header error.\n"); return false; }
                ++theoraHeaders;
            }
            while (hasVorbis && vorbisHeaders < 3 && ogg_stream_packetout(&vo, &op)) {
                if (vorbis_synthesis_headerin(&vi, &vc, &op)) { std::fprintf(stderr, "[MDecoder] Vorbis header error.\n"); return false; }
                ++vorbisHeaders;
            }
            if (ogg_sync_pageout(&oy, &og) > 0) {
                if (hasTheora) ogg_stream_pagein(&to, &og);
                if (hasVorbis) ogg_stream_pagein(&vo, &og);
            }
            else {
                int ret = bufferData(); if (ret == 0) { std::fprintf(stderr, "[MDecoder] EOF while reading headers.\n"); return false; }
            }
        }
        return true;
    }

    bool initDecoders() {
        if (hasTheora) {
            td = th_decode_alloc(&ti, ts);
            vWidth = ti.pic_width;
            vHeight = ti.pic_height;
            vFps = (ti.fps_denominator != 0) ? (double)ti.fps_numerator / (double)ti.fps_denominator : 30.0;
            if (vFps <= 0.0) vFps = 30.0;
        }
        if (hasVorbis) {
            if (vorbis_synthesis_init(&vd, &vi) != 0) return false;
            if (vorbis_block_init(&vd, &vb) != 0) return false;
            aFreq = vi.rate;
            aChannels = (vi.channels > 2) ? 2 : (vi.channels < 1 ? 1 : vi.channels);
        }
        startTicks = timeGetTime();
        return true;
    }

    int bufferData() {
        char* buffer = (char*)ogg_sync_buffer(&oy, 4096);
        int bytes = (int)std::fread(buffer, 1, 4096, infile);
        ogg_sync_wrote(&oy, bytes);
        return bytes;
    }

    // 简单的YUV420 -> ARGB32转换（BT.601近似）
    void yuv420ToARGB(const th_ycbcr_buffer& yuv, std::uint8_t* outPixels, std::uint32_t stride) {
        const int w = vWidth;
        const int h = vHeight;
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* Yp = (const std::uint8_t*)yuv[0].data + y * yuv[0].stride;
            const std::uint8_t* Up = (const std::uint8_t*)yuv[1].data + (y / 2) * yuv[1].stride;
            const std::uint8_t* Vp = (const std::uint8_t*)yuv[2].data + (y / 2) * yuv[2].stride;
            std::uint32_t* dst = (std::uint32_t*)(outPixels + (std::size_t)y * stride);
            for (int x = 0; x < w; ++x) {
                int Yv = Yp[x];
                int Uv = Up[x / 2];
                int Vv = Vp[x / 2];
                int C = Yv - 16;
                int D = Uv - 128;
                int E = Vv - 128;
                int R = (298 * C + 409 * E + 128) >> 8;
                int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
                int B = (298 * C + 516 * D + 128) >> 8;
                if (R < 0) R = 0; if (R > 255) R = 255;
                if (G < 0) G = 0; if (G > 255) G = 255;
                if (B < 0) B = 0; if (B > 255) B = 255;
                dst[x] = (0xFFu << 24) | (std::uint32_t(R) << 16) | (std::uint32_t(G) << 8) | (std::uint32_t)B;
            }
        }
    }

    void pushVideoFrame(VideoFrame* vf) {
        std::lock_guard<std::mutex> lock(mtxVideo);
        if (!videoHead) { videoHead = vf; videoTail = vf; }
        else { videoTail->next = vf; videoTail = vf; }
    }
    void pushAudioPacket(AudioPacket* ap) {
        std::lock_guard<std::mutex> lock(mtxAudio);
        if (!audioHead) { audioHead = ap; audioTail = ap; }
        else { audioTail->next = ap; audioTail = ap; }
    }

    void decodeLoop() {
        ogg_packet op{};
        th_ycbcr_buffer yuv{};
        bool alive = true;
        std::uint32_t gen = seekGeneration.load();
        while (!quit.load() && alive) {
            bool aPkt = false, vPkt = false;
            if (hasVorbis) {
                if (ogg_stream_packetout(&vo, &op) > 0) aPkt = true;
            }
            if (hasTheora && !aPkt) {
                if (ogg_stream_packetout(&to, &op) > 0) vPkt = true;
            }
            if (!aPkt && !vPkt) {
                if (bufferData() == 0) eof.store(true);
                while (ogg_sync_pageout(&oy, &og) > 0) {
                    if (hasTheora && ogg_page_serialno(&og) == to.serialno) ogg_stream_pagein(&to, &og);
                    if (hasVorbis && ogg_page_serialno(&og) == vo.serialno) ogg_stream_pagein(&vo, &og);
                }
                if (eof.load()) {
                    // 尝试最后再取一次包
                    if (hasVorbis && ogg_stream_packetout(&vo, &op) > 0) aPkt = true;
                    else if (hasTheora && ogg_stream_packetout(&to, &op) > 0) vPkt = true;
                    if (!aPkt && !vPkt) alive = false;
                }
            }
            // 音频
            if (aPkt) {
                if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
                float** pcm = nullptr; int samples = 0;
                while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                    // 输出为f32 interleaved
                    int outCh = aChannels;
                    std::size_t floats = (std::size_t)samples * (std::size_t)outCh;
                    float* buf = (float*)std::malloc(sizeof(float) * floats);
                    if (!buf) { vorbis_synthesis_read(&vd, samples); break; }
                    float* dst = buf;
                    for (int i = 0; i < samples; ++i) {
                        float L = 0.f, R = 0.f;
                        if (vi.channels == 1) { L = R = pcm[0][i]; }
                        else {
                            L = pcm[0][i]; R = pcm[1][i];
                            if (vi.channels > 2) {
                                for (int ch = 2; ch < vi.channels; ++ch) {
                                    float v = pcm[ch][i]; float scale = 0.5f / (vi.channels - 1);
                                    L += v * scale; R += v * scale;
                                }
                            }
                        }
                        if (outCh == 1) { *dst++ = L; }
                        else { *dst++ = L; *dst++ = R; }
                    }
                    // 计算播放时间戳(ms)
                    std::uint32_t nowms = (std::uint32_t)(timeGetTime() - startTicks);
                    AudioPacket* ap = (AudioPacket*)std::malloc(sizeof(AudioPacket));
                    if (!ap) { std::free(buf); vorbis_synthesis_read(&vd, samples); break; }
                    ap->seek_generation = gen;
                    ap->playms = nowms;
                    ap->channels = outCh;
                    ap->freq = aFreq;
                    ap->frames = samples;
                    ap->dataFloats = (std::uint32_t)floats;
                    ap->samples = buf;
                    ap->next = nullptr;
                    pushAudioPacket(ap);
                    vorbis_synthesis_read(&vd, samples);
                }
            }
            // 视频
            if (vPkt) {
                ogg_int64_t gran = -1;
                if (th_decode_packetin(td, &op, &gran) == 0) {
                    double vtime = th_granule_time(td, gran);
                    if (vtime < 0.0) vtime = (double)(timeGetTime() - startTicks) / 1000.0;
                    // 分配像素并转换
                    std::uint32_t stride = (std::uint32_t)vWidth * 4u;
                    std::uint32_t bytes = stride * (std::uint32_t)vHeight;
                    std::uint8_t* px = (std::uint8_t*)std::malloc(bytes);
                    if (!px) { /* OOM */ }
                    th_decode_ycbcr_out(td, yuv);
                    yuv420ToARGB(yuv, px, stride);
                    // 计算播放时间戳(ms)
                    std::uint32_t playms = (std::uint32_t)std::llround(vtime * 1000.0);
                    VideoFrame* vf = (VideoFrame*)std::malloc(sizeof(VideoFrame));
                    if (!vf) { std::free(px); }
                    else {
                        vf->seek_generation = gen;
                        vf->playms = playms;
                        vf->fps = vFps;
                        vf->width = (std::uint32_t)vWidth;
                        vf->height = (std::uint32_t)vHeight;
                        vf->format = VideoFormat::ARGB32;
                        vf->stride = stride;
                        vf->dataBytes = bytes;
                        vf->pixels = px;
                        vf->next = nullptr;
                        pushVideoFrame(vf);
                    }
                }
            }
        }
    }

    void cleanup() {
        if (infile) { std::fclose(infile); infile = nullptr; }
        // 清空队列
        {
            std::lock_guard<std::mutex> lock(mtxVideo);
            freeVideoFrameChain(videoHead); videoHead = nullptr; videoTail = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(mtxAudio);
            freeAudioPacketChain(audioHead); audioHead = nullptr; audioTail = nullptr;
        }
    }

private:
    // 文件与线程
    FILE* infile = nullptr;
    std::thread worker;
    std::atomic<bool> quit{ false };
    std::atomic<bool> eof{ false };
    std::atomic<std::uint32_t> seekGeneration{ 0 };

    // 输出队列（链表头尾）
    VideoFrame* videoHead = nullptr;
    VideoFrame* videoTail = nullptr;
    AudioPacket* audioHead = nullptr;
    AudioPacket* audioTail = nullptr;
    std::mutex mtxVideo;
    std::mutex mtxAudio;

    // Ogg/Theora/Vorbis状态
    ogg_sync_state   oy{};
    ogg_page         og{};

    ogg_stream_state to{}; // Theora
    th_info          ti{};
    th_comment       tc{};
    th_dec_ctx* td = nullptr;
    th_setup_info* ts = nullptr;

    ogg_stream_state vo{}; // Vorbis
    vorbis_info      vi{};
    vorbis_comment   vc{};
    vorbis_dsp_state vd{};
    vorbis_block     vb{};

    bool             hasTheora = false;
    bool             hasVorbis = false;
    int              theoraHeaders = 0;
    int              vorbisHeaders = 0;

    // 运行时信息
    int vWidth = 0, vHeight = 0;
    double vFps = 0.0;
    int aFreq = 0, aChannels = 0;
    DWORD startTicks = 0;
};


#endif // 0

//---------------------------------------------------------------------------
// 测试/示例代码（可折叠）
//---------------------------------------------------------------------------
#if 0

/****************************************************************************
 * 标题: MDecoder示例 (Theora+Vorbis链表输出)
 * 文件: MDecoder.cpp
 * 版本: 0.1
 * 作者: AEGLOVE
 * 日期: 2025-11-29
 * 功能: 展示如何使用`MDecoder`解码Ogg文件，并提取视频帧与音频包；
 *      控制台打印信息，并演示释放链表资源；
 * 依赖: Win32 API, libogg, libtheora, libvorbis
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
 ****************************************************************************/

#include <iostream>
#include <locale>
#include <codecvt>

static void printVideoFrames(VideoFrame* head) {
    std::size_t count = 0, bytes = 0;
    for (VideoFrame* p = head; p; p = p->next) { ++count; bytes += p->dataBytes; }
    std::cout << "[Demo] VideoFrames count=" << count << ", bytes=" << bytes << std::endl;
}
static void printAudioPackets(AudioPacket* head) {
    std::size_t count = 0, floats = 0;
    for (AudioPacket* p = head; p; p = p->next) { ++count; floats += p->dataFloats; }
    std::cout << "[Demo] AudioPackets count=" << count << ", floats=" << floats << std::endl;
}

int main(int argc, char** argv) {
    timeBeginPeriod(1);
    // C++17中std::wstring_convert已弃用。
    // 在Windows上，更现代的替代方法是使用MultiByteToWideChar(CP_UTF8, ...)。
    // 但为了保持代码简洁并解决直接的编译错误，我们暂时只包含必要的头文件。
    std::wstring path = (argc >= 2) ? std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(argv[1]) : L"test.ogg";

    MDecoder dec;
    if (!dec.start(path)) {
        std::wcerr << L"[Demo] Failed to start decoder for: " << path << std::endl;
        timeEndPeriod(1);
        return 1;
    }

    // 简单轮询，获取数据
    for (int i = 0; i < 50; ++i) { // 轮询约5秒
        Sleep(100);
        VideoFrame* vf = dec.popVideo();
        AudioPacket* ap = dec.popAudio();
        if (vf) { printVideoFrames(vf); freeVideoFrameChain(vf); }
        if (ap) { printAudioPackets(ap); freeAudioPacketChain(ap); }
    }

    dec.stop();
    timeEndPeriod(1);
    return 0;
}

#endif

//---------------------------------------------------------------------------
// 新示例：使用GDI+ + WaveOut 播放 Ogg(Theora+Vorbis)，并做A/V同步
//---------------------------------------------------------------------------
#if 1

#include <gdiplus.h>
#include <mmsystem.h>
#include <deque>
#include <algorithm>

#pragma comment(lib, "Gdiplus.lib")

namespace DemoPlayer {

    struct SharedVideo {
        std::mutex mtx;
        VideoFrame* current = nullptr; // 持有当前用于绘制的帧
    };

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    static void drawFrameToWindow(HWND hwnd, const VideoFrame* vf)
    {
        if (!hwnd || !vf || !vf->pixels) return;
        RECT rc{}; GetClientRect(hwnd, &rc);
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top;
        if (cw <= 0 || ch <= 0) return;

        HDC hdc = GetDC(hwnd);
        if (!hdc) return;

        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

        const UINT w = vf->width;
        const UINT h = vf->height;
        const INT stride = (INT)vf->stride;

        Gdiplus::Bitmap bmp(w, h, stride, PixelFormat32bppARGB, (BYTE*)vf->pixels);

        // 按窗口大小等比缩放并居中
        double sx = (double)cw / (double)w;
        double sy = (double)ch / (double)h;
        double s = std::min(sx, sy);
        int dw = std::max(1, (int)std::llround(w * s));
        int dh = std::max(1, (int)std::llround(h * s));
        int dx = (cw - dw) / 2;
        int dy = (ch - dh) / 2;

        g.DrawImage(&bmp, Gdiplus::Rect(dx, dy, dw, dh));

        ReleaseDC(hwnd, hdc);
    }

    static void freeVideoFrame(VideoFrame*& vf)
    {
        if (!vf) return;
        if (vf->pixels) { free(vf->pixels); vf->pixels = nullptr; }
        free(vf); vf = nullptr;
    }

} // namespace DemoPlayer

int main(int argc, char** argv)
{
    using namespace DemoPlayer;

    timeBeginPeriod(1);

    std::wstring path = L"test.ogg";
    if (argc >= 2) {
        // 尝试将UTF-8的argv[1]转为UTF-16
        int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wbuf; wbuf.resize((size_t)wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wbuf.data(), wlen);
            if (!wbuf.empty()) path = wbuf;
        }
    }

    // 初始化GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        std::fprintf(stderr, "[Demo] GdiplusStartup failed.\n");
        return 2;
    }

    // 启动解码器
    MDecoder dec;
    if (!dec.start(path)) {
        std::fwprintf(stderr, L"[Demo] Failed to start decoder for: %ls\n", path.c_str());
        Gdiplus::GdiplusShutdown(gdiplusToken);
        timeEndPeriod(1);
        return 1;
    }

    // 创建窗口
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    const wchar_t* kCls = L"MDecoderPlayerWnd";
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = kCls; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    // 尝试获取第一帧来确定窗口大小
    int wndW = 800, wndH = 480;
    {
        VideoFrame* vfFirst = nullptr;
        for (int i = 0; i < 50 && !vfFirst; ++i) {
            Sleep(50);
            vfFirst = dec.popVideo();
        }
        if (vfFirst) {
            wndW = (int)vfFirst->width;
            wndH = (int)vfFirst->height;
        }
        // 我们会在之后绘制vfFirst或释放它（推回队列）
        if (vfFirst) {
            // 直接先用作第一帧显示
            // 暂时存下，后续作为current
        }
    }

    HWND hwnd = CreateWindowExW(0, kCls, L"MDecoder GDI+ + WaveOut Player",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, wndW + 16, wndH + 39, // 简单估计边框
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "[Demo] CreateWindow failed.\n");
        dec.stop();
        Gdiplus::GdiplusShutdown(gdiplusToken);
        timeEndPeriod(1);
        return 3;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 音频输出初始化（等到拿到第一包音频再初始化）
    HWAVEOUT hwo = nullptr;
    WAVEFORMATEX wfx{};
    std::vector<WAVEHDR*> activeHdrs; activeHdrs.reserve(64);

    DWORD audioSampleRate = 0;
    DWORD audioClockMs = 0;

    std::deque<VideoFrame*> vQueue;
    SharedVideo shared;

    // 主循环
    bool running = true;
    DWORD lastPaintTick = timeGetTime();

    while (running) {
        // 消息泵
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        // 回收已完成的音频缓冲
        if (hwo) {
            for (size_t i = 0; i < activeHdrs.size();) {
                WAVEHDR* hdr = activeHdrs[i];
                if (hdr->dwFlags & WHDR_DONE) {
                    waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
                    if (hdr->lpData) free(hdr->lpData);
                    free(hdr);
                    activeHdrs.erase(activeHdrs.begin() + i);
                }
                else {
                    ++i;
                }
            }
        }

        // 从解码器拉取数据
        if (VideoFrame* vfList = dec.popVideo()) {
            for (VideoFrame* p = vfList; p; ) {
                VideoFrame* nxt = p->next; p->next = nullptr; vQueue.push_back(p); p = nxt;
            }
        }
        if (AudioPacket* apList = dec.popAudio()) {
            for (AudioPacket* p = apList; p; ) {
                AudioPacket* nxt = p->next; p->next = nullptr;
                // 首次打开音频设备
                if (!hwo) {
                    audioSampleRate = (DWORD)p->freq;
                    wfx.wFormatTag = WAVE_FORMAT_PCM;
                    wfx.nChannels = (WORD)p->channels;
                    if (wfx.nChannels < 1) wfx.nChannels = 1; if (wfx.nChannels > 2) wfx.nChannels = 2;
                    wfx.nSamplesPerSec = audioSampleRate;
                    wfx.wBitsPerSample = 16;
                    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
                    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
                    MMRESULT mmr = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
                    if (mmr != MMSYSERR_NOERROR) {
                        std::fprintf(stderr, "[Demo] waveOutOpen failed: %u\n", (unsigned)mmr);
                        hwo = nullptr;
                    }
                }

                if (hwo) {
                    // float32 -> int16 转换并提交
                    size_t frames = (size_t)p->frames;
                    size_t channels = (size_t)wfx.nChannels;
                    size_t samples = frames * channels;
                    int16_t* pcm16 = (int16_t*)malloc(samples * sizeof(int16_t));
                    if (pcm16) {
                        for (size_t i = 0; i < samples; ++i) {
                            float v = p->samples[i];
                            if (v > 1.f) v = 1.f; else if (v < -1.f) v = -1.f;
                            int s = (int)std::lrintf(v * 32767.0f);
                            if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                            pcm16[i] = (int16_t)s;
                        }
                        WAVEHDR* hdr = (WAVEHDR*)malloc(sizeof(WAVEHDR));
                        if (hdr) {
                            std::memset(hdr, 0, sizeof(WAVEHDR));
                            hdr->lpData = (LPSTR)pcm16;
                            hdr->dwBufferLength = (DWORD)(samples * sizeof(int16_t));
                            if (waveOutPrepareHeader(hwo, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                                if (waveOutWrite(hwo, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                                    activeHdrs.push_back(hdr);
                                }
                                else {
                                    waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
                                    free(pcm16); free(hdr);
                                }
                            }
                            else {
                                free(pcm16); free(hdr);
                            }
                        }
                        else {
                            free(pcm16);
                        }
                    }
                }

                // 释放AudioPacket本身
                if (p->samples) free(p->samples);
                free(p);
                p = nxt;
            }
        }

        // 获取音频时钟（毫秒）
        if (hwo && audioSampleRate > 0) {
            MMTIME mt{}; mt.wType = TIME_SAMPLES;
            if (waveOutGetPosition(hwo, &mt, sizeof(mt)) == MMSYSERR_NOERROR) {
                if (mt.wType == TIME_SAMPLES) {
                    audioClockMs = (DWORD)((mt.u.sample * 1000ull) / (audioSampleRate));
                }
                else if (mt.wType == TIME_MS) {
                    audioClockMs = mt.u.ms;
                }
                else if (mt.wType == TIME_BYTES) {
                    DWORD bytes = mt.u.cb;
                    DWORD samples = bytes / wfx.nBlockAlign; // frames
                    audioClockMs = (DWORD)((samples * 1000ull) / (audioSampleRate));
                }
            }
        }

        // 依据音频时钟显示视频
        const DWORD nowTick = timeGetTime();
        bool needPaint = false;
        while (!vQueue.empty()) {
            VideoFrame* vf = vQueue.front();
            // 若还未打开音频，则用系统时间推动
            DWORD refClock = (hwo ? audioClockMs : (nowTick));
            DWORD vms = vf->playms;
            // 允许一点提前/滞后
            if (!hwo) {
                // 当还没音频时，用当前时间近似判断
                needPaint = true;
            }
            else if (vms <= refClock + 10) {
                needPaint = true;
            }
            else {
                break;
            }

            if (needPaint) {
                // 交换到当前帧
                {
                    std::lock_guard<std::mutex> lk(shared.mtx);
                    if (shared.current) freeVideoFrame(shared.current);
                    shared.current = vf;
                }
                vQueue.pop_front();
            }
        }

        // 定期重绘
        if (nowTick - lastPaintTick >= 15) { // ~60fps刷新
            VideoFrame* cur = nullptr;
            {
                std::lock_guard<std::mutex> lk(shared.mtx);
                cur = shared.current;
            }
            if (cur) drawFrameToWindow(hwnd, cur);
            lastPaintTick = nowTick;
        }

        Sleep(1);
    }

    // 清理
    dec.stop();

    // 回收视频帧
    {
        std::lock_guard<std::mutex> lk(shared.mtx);
        if (shared.current) { freeVideoFrame(shared.current); }
    }
    for (VideoFrame* vf : vQueue) { freeVideoFrame(vf); }
    vQueue.clear();

    if (hwo) {
        waveOutReset(hwo);
        for (WAVEHDR* hdr : activeHdrs) {
            waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
            if (hdr->lpData) free(hdr->lpData);
            free(hdr);
        }
        activeHdrs.clear();
        waveOutClose(hwo);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    timeEndPeriod(1);
    return 0;
}

#endif


```

### FFT和人声分离

``` 
开发环境：windows11，C++17，VS2022，Unicode宽字符，代码页65001，入口函数int main(int argc,char* argv[])方便控制台输出调试信息，静态库链接使用#pragma comment方式，使用cstdint，类成员函数小驼峰。类声明部分(数据成员和函数成员)附加简洁的中文注释。
现在我想通过FFT实现音乐和人声分离，测试文件为"love.flac"，然后把分离的声音分别保存为对应的wav文件。不知道能不能实现？
另外，控制台显示信息请使用以下宏和相关设置
#include <cwchar>   // 提供 std::wprintf、宽字符相关的 C 函数
#include <fcntl.h>  // 提供 _O_U16TEXT 等文件/模式常量
#define ODD(...) wprintf(__VA_ARGS__)
// 控制台颜色设置  
void setConsoleColor(int32_t color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
system("cls");// 清屏：调用 Windows shell 的 CLS 命令清除控制台内容，方便每次重绘播放器状态（注意：system 调用开销和安全性）
//main:	添加
_setmode(_fileno(stdout), _O_U16TEXT);
程序过程中要显示相应的调试信息，给我完整的代码。
给出完整的代码，我要复制粘贴进行测试。使用中文回复。
```



## Filament相关

### Overview

[Search | DeepWiki](https://deepwiki.com/search/v1680-httpsgithubcomgooglefila_3ef55035-1c6d-4283-a16e-066c3a4a3350?mode=fast)

下载源代码，版本V1.68.0

https://github.com/google/filament/archive/refs/tags/v1.68.0.zip
https://kkgithub.com/google/filament/archive/refs/tags/v1.68.0.zip

现在我下载好了Filament源代码，请问我如何编译成静态库(非符号导入库)？有哪些编译选项？

我是一名C/C++软件工程师：

开发环境：window64平台，VS2022，Unicode字符集，C++17标准，utf8 with BOM (代码页65001)

程序入口：int main(int argc, char** argv)函数，这样方便在控制台输出信息。

代码风格：函数以小驼峰命名。

整数类型：使用标准库<cstdint>

静态库链接的形式：#pragma comment(lib, "xxx.lib")

测试或示例代码放在#if...#endif块中，包括注释，头文件，静态库都在内，方便在IDE折叠代码。代码要完整，方便复制粘贴进行测试。

在#if...#endif块前添加文档注释，内容包括程序标题，文件，版本，作者：AEGLOVE，时间，功能，依赖，环境，编码，格式类似如下：

/****************************************************************************

 \* 标题: MUI轻量级UI框架 (基于ThorVG和OpenGL)

 \* 文件：mui_framework.h

 \* 版本：0.1

 \* 作者: AEGLOVE

 \* 日期: 2025-10-13

 \* 功能: 基于C++和OpenGL的轻量级UI框架，支持动画系统、事件处理和多种UI控件

 \* 依赖: OpenGL, GLAD, GLM, ThorVG图形库, miniaudio

 \* 环境: Windows11 x64, VS2022, C++17, Unicode字符集

 \* 编码: utf8 with BOM (代码页65001)

****************************************************************************/

我下载好了源代码zip包："E:\XKU\tem\v1.68.0.zip"
使用Bandizip解压得到："E:\XKU\tem\filament-1.68.0"
但有报错信息：

filament-1.68.0\third_party\dawn\src\cmake\HermeticXcode\ranlib - 以管理员身份运行 Bandizip 并创建符号链接.
filament-1.68.0\third_party\zstd\tests\cli-tests\bin\unzstd - 以管理员身份运行 Bandizip 并创建符号链接.
filament-1.68.0\third_party\zstd\tests\cli-tests\bin\zstdcat - 以管理员身份运行 Bandizip 并创建符号链接.
filament-1.68.0\web\filament-js\filament-viewer.js - 以管理员身份运行 Bandizip 并创建符号链接.
出现错误

上述报错信息是否影响编译？接下来怎么操作？

### Windows编译步骤

#### 1. 环境准备

确保已安装：

- Visual Studio 2022
- Windows SDK
- CMake 3.22.1+
- Python 3.7+ BUILDING.md:185-193

#### 2. 编译命令

```
:: 打开 "x64 Native Tools Command Prompt for VS 2022"  
cd E:\XKU\tem\filament-1.68.0  
mkdir out  
cd out  
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../release/filament -DUSE_STATIC_CRT=ON -DFILAMENT_SUPPORTS_OPENGL=ON -DFILAMENT_SUPPORTS_VULKAN=OFF ..
ninja install
```

#### 主要编译选项

| 选项                       | 默认值 | 说明                                     |
| -------------------------- | ------ | ---------------------------------------- |
| `USE_STATIC_CRT`           | ON     | 静态链接CRT运行时 CMakeLists.txt:212-233 |
| `FILAMENT_SUPPORTS_OPENGL` | ON     | 包含OpenGL后端                           |
| `FILAMENT_SUPPORTS_VULKAN` | OFF    | 包含Vulkan后端                           |
| `FILAMENT_SUPPORTS_METAL`  | OFF    | 包含Metal后端（仅Apple平台）             |
| `FILAMENT_BUILD_FILAMAT`   | ON     | 构建filamat材质库                        |
| `FILAMENT_SKIP_SAMPLES`    | OFF    | 跳过示例程序                             |

#### FILAMENT_SKIP_SAMPLES 选项功能总结

FILAMENT_SKIP_SAMPLES 是一个CMake编译选项，用于控制是否构建Filament的示例程序。

选项含义：

- FILAMENT_SKIP_SAMPLES=ON → 跳过构建示例程序（不构建）
- FILAMENT_SKIP_SAMPLES=OFF → 构建示例程序（默认值）

功能说明：

1. 跳过示例程序构建 - 不编译samples/目录下的所有演示程序
2. 跳过辅助库构建 - 不编译libs/filamentapp/目录（示例程序使用的SDL2辅助库）
3. 减少编译时间 - 显著缩短整体构建时间
4. 减少输出文件 - 生成的构建产物更少，节省磁盘空间

使用场景：

- 快速构建 - 只需要核心库而不需要示例时
- CI/CD流水线 - 自动化构建中节省时间
- 交叉编译 - Android、iOS等平台的构建工具链会自动启用
- 嵌入式开发 - 目标设备不需要示例程序

启用方法：
cmake -DFILAMENT_SKIP_SAMPLES=ON ..

注意事项：
启用此选项后，所有示例程序（如gltf_viewer、hello_triangle等）、filamentapp辅助库、示例程序相关的资源文件和材质都不会被构建。但核心Filament库、工具（如matc、cmgen）和后端支持不受影响。

**Notes**

- 示例程序需要相应的资源文件（模型、纹理等），这些会自动复制到构建目录 CMakeLists.txt:346-360
- 建议先构建带示例的版本进行学习，掌握后再使用FILAMENT_SKIP_SAMPLES=ON重新构建精简版本
- 某些示例需要IBL环境贴图，可以使用cmgen工具生成 

### Filament工具程序说明

| 程序名称                  | 主要作用                | 简要描述                                                     |
| ------------------------- | ----------------------- | ------------------------------------------------------------ |
| `basisu.exe`              | Basis Universal纹理压缩 | 将纹理压缩为Basis Universal格式，支持GPU实时解压缩 README.md:8-20 |
| `cmgen.exe`               | 基于图像的光照生成器    | 从HDR环境贴图生成IBL（Image-Based Lighting）资源，包括预过滤环境贴图和天空盒 README.md:8-20 |
| `filamesh.exe`            | 网格转换工具            | 将OBJ/FBX等格式的3D模型转换为Filament优化的filamesh格式 README.md:8-20 |
| `glslminifier.exe`        | GLSL代码压缩            | 压缩和优化GLSL着色器代码，减少文件大小 README.md:8-20        |
| `gltf_viewer.exe`         | glTF模型查看器          | 加载和显示glTF 2.0格式的3D模型，支持各种Filament渲染特性 README.md:8-20 |
| `matc.exe`                | 材质编译器              | 将Filament材质定义文件(.mat)编译成二进制.filamat文件 README.md:8-20 |
| `material_sandbox.exe`    | 材质测试工具            | 交互式材质编辑器，可实时调整材质参数并查看效果 README.md:8-20 |
| `matinfo.exe`             | 材质信息查看器          | 显示已编译材质(.filamat)的详细信息，包括参数和着色器变体 README.md:8-20 |
| `mipgen.exe`              | Mipmap生成器            | 为纹理生成多级渐远纹理(Mipmap)，支持多种压缩格式 README.md:8-20 |
| `normal-blending.exe`     | 法线贴图混合            | 将多张法线贴图混合成一张，用于复杂表面细节 README.md:8-20    |
| `resgen.exe`              | 资源生成器              | 将多个二进制文件打包成可嵌入的资源文件 README.md:8-20        |
| `roughness-prefilter.exe` | 粗糙度预过滤            | 从法线贴图预过滤粗糙度贴图，减少锯齿和闪烁 README.md:8-20    |
| `specular-color.exe`      | 镜面反射颜色计算        | 基于光谱数据计算导体的镜面反射颜色 README.md:8-20            |
| `uberz.exe`               | Uber着色器打包          | 将多个材质打包成uber着色器档案，优化材质切换性能 README.md:8-20 |

#### Notes

- 这些工具是Filament工作流程的核心组成部分，涵盖了从资源准备到最终渲染的整个流程 README.md:8-20
- 每个工具都有对应的文档文件在 `docs/` 目录下，可通过命令行参数查看详细用法
- `cmgen`、`matc`、`filamesh` 和 `mipgen` 是最常用的工具，通常在资源准备阶段使用 BUILDING.md:414-422
- `gltf_viewer` 和 `material_sandbox` 是查看和测试工具，用于验证资源效果

### Filament示例程序测试内容

| 示例程序         | 测试内容       | 主要功能                                                     |
| ---------------- | -------------- | ------------------------------------------------------------ |
| hellotriangle    | 基础三角形渲染 | 最简单的渲染示例，展示如何创建基本的几何体和材质 hellotriangle.cpp:17-45 |
| hellopbr         | 物理基础渲染   | 演示PBR材质系统，包括金属度、粗糙度等属性 CMakeLists.txt:274 |
| suzanne          | 猴子头模型渲染 | 展示复杂模型的加载和渲染，包含多种纹理贴图 suzanne.cpp:133-196 |
| material_sandbox | 材质编辑器     | 交互式材质测试工具，可实时调整各种材质参数 material_sandbox.cpp:1037-1059 |
| gltf_viewer      | glTF模型查看器 | 加载和显示glTF 2.0格式的3D模型 gltf_viewer.cpp:17-76         |
| shadowtest       | 阴影渲染测试   | 演示实时阴影的生成和渲染 CMakeLists.txt:295                  |
| helloskinning    | 骨骼动画       | 展示骨骼动画系统的使用 CMakeLists.txt:276                    |
| sample_cloth     | 布料渲染       | 演示布料材质的特殊渲染效果 sample_cloth.cpp:190-261          |
| image_viewer     | 图像查看器     | 显示2D图像，测试纹理加载功能 CMakeLists.txt:281              |
| depthtesting     | 深度测试       | 演示深度缓冲区的使用和效果 CMakeLists.txt:268                |
| rendertarget     | 渲染到纹理     | 演示离屏渲染技术 CMakeLists.txt:291                          |
| point_sprites    | 点精灵渲染     | 演示粒子系统的渲染 CMakeLists.txt:288-290                    |
| lightbulb        | 光照效果       | 演示各种光源类型和光照效果 CMakeLists.txt:282                |
| animation        | 动画系统       | 演示关键帧动画的播放 CMakeLists.txt:267                      |
| texturedquad     | 纹理四边形     | 基础纹理映射示例 CMakeLists.txt:298                          |

#### 运行建议

1. **从简单开始**：建议先运行 `hellotriangle.exe` 验证基础环境 BUILDING.md:215-219
2. **逐步深入**：然后尝试 `hellopbr.exe` 了解材质系统
3. **交互测试**：使用 `material_sandbox.exe` 进行材质参数的实时调整
4. **模型加载**：用 `gltf_viewer.exe` 测试各种3D模型格式

#### 注意事项

- 示例程序需要相应的资源文件（模型、纹理等），这些已自动复制到构建目录 CMakeLists.txt:346-360
- 某些示例需要IBL环境贴图，可以使用 `cmgen` 工具生成 BUILDING.md:414-422
- 运行前确保已安装最新的显卡驱动程序

### Filament头文件相关功能

| 目录名                              | 功能描述                                                     |
| ----------------------------------- | ------------------------------------------------------------ |
| `backend/`                          | 渲染后端/驱动程序，支持 Vulkan、Metal、OpenGL/ES README.md:300-301 |
| `camutils/`                         | 相机操作工具库 README.md:306-307                             |
| `filamat/`                          | 材质生成库，用于编译和处理材质 README.md:310-311             |
| `filament/`                         | Filament 核心渲染引擎（最小依赖） README.md:299-300          |
| `filament-generatePrefilterMipmap/` | 生成预过滤 mipmap 的工具 generatePrefilterMipmap.cpp:17-29   |
| `filament-iblprefilter/`            | IBL（基于图像的光照）预过滤上下文，用于环境贴图处理 IBLPrefilterContext.h:38-57 |
| `filament-matp/`                    | 材质解析器 README.md:334                                     |
| `filameshio/`                       | 网格解析库，用于读取 filamesh 格式 README.md:312-313         |
| `geometry/`                         | 网格相关工具 README.md:313-314                               |
| `gltfio/`                           | glTF 2.0 格式加载器 README.md:314-315                        |
| `ibl/`                              | IBL 生成工具 README.md:315-316                               |
| `image/`                            | 图像过滤和简单变换 README.md:316-317                         |
| `ktxreader/`                        | KTX 纹理格式读取器 jsbindings.cpp:73-74                      |
| `math/`                             | 数学库 README.md:319-320                                     |
| `mathio/`                           | 数学类型的输出流支持 README.md:320-321                       |
| `mikktspace/`                       | 切线空间计算库 CMakeLists.txt:882                            |
| `tsl/`                              | 任务调度库 Filament.podspec:95-98                            |
| `uberz/`                            | 材质归档系统 jsbindings.cpp:71                               |
| `utils/`                            | 工具库（线程、内存、数据结构等） README.md:321-322           |
| `viewer/`                           | glTF 查看器库（需要 gltfio） README.md:322-323               |

### Filament字体渲染支持

Filament本身不提供专门的字体渲染API，但可以通过多种方式实现文本渲染：

#### 底层支持方式

1. NV_path_rendering扩展支持
   - 支持OpenGL的NV_path_rendering扩展，可直接从字体文件生成路径对象 NV_path_rendering.txt:2891-2920
   - 提供`PathGlyphsNV`等函数从字符码生成渲染路径 NV_path_rendering.txt:2992-3020
2. 集成UI库方案
   - 可集成ImGui进行字体渲染，支持TTF字体加载 FONTS.md:301-330
   - 支持自定义字体图标和彩色字体 FONTS.md:308-346
3. 纹理贴图方案
   - 将字体渲染为纹理，通过材质系统显示
   - 使用Filament的材质编译器(matc)处理字体纹理

#### 使用建议

由于Filament是3D渲染引擎，推荐的字体渲染方式：

1. **UI文本** - 集成ImGui或Nuklear等UI库
2. **3D文本** - 使用建模软件创建3D文字模型
3. **HUD文本** - 渲染到纹理后叠加到场景

#### 注意事项

- NV_path_rendering是NVIDIA专有扩展，跨平台兼容性有限
- ImGui集成需要额外的渲染后端适配
- 字体纹理方案需要处理Mipmap和抗锯齿

#### Notes

- Filament的示例程序中没有专门的字体渲染示例
- 如需完整的UI支持，建议研究Filament的viewer组件 nuklear.h:3782-3841
- 字体文件通常放在assets/fonts目录下，如Roboto-Medium.ttf CHANGELOG.txt:5530-5552

### Filament屏蔽调试信息

屏蔽Filament调试信息的方法

Filament的调试输出可以通过以下几种方式控制：

#### 1. 编译时选项（推荐）

重新编译Filament时禁用日志输出：

```
cmake -DFILAMENT_USE_ABSEIL_LOGGING=OFF ..
```

这个选项在`CMakeLists.txt`中定义 CMakeLists.txt:57-58 ，默认为OFF，但如果您的构建启用了它，可以显式禁用。

#### 2. 运行时重定向（简单方案）

在您的代码中重定向stdout：

```
// 在main函数开始处添加  
#include <fstream>  
#include <iostream>  
  
int main(int argc, char** argv) {  
    // 重定向stdout到null设备  
    std::ofstream nullStream("nul");  
    std::cout.rdbuf(nullStream.rdbuf());  
    std::cerr.rdbuf(nullStream.rdbuf());  
      
    // 其余代码...  
}
```

#### 3. 禁用特定后端调试

如果您使用的是Vulkan后端，可以通过环境变量控制：

```
// 在Engine创建前设置  
putenv("FILAMENT_BACKEND_DEBUG_FLAG=0");
```

这会影响后端调试标志 VulkanConstants.h:91-100 。

#### 4. 使用Release版本

确保使用Release版本而非Debug版本构建的Filament库，Debug版本会输出更多调试信息。

#### 注意事项

- Engine创建时的基本信息（如"FEngine created"）可能无法完全屏蔽，因为它们是初始化的一部分
- 最彻底的方法是重新编译Filament时禁用所有日志选项
- 运行时重定向是最简单的解决方案，不需要重新编译Filament

#### Notes

- FILAMENT_USE_ABSEIL_LOGGING是控制日志输出的主要选项
- 运行时重定向stdout是最简单的临时解决方案
- 重新编译Filament可以彻底控制所有输出行为



### IMGUI和shadowtest

#### 简要回答

是的，你可以在Win32本地窗口中集成ImGui。Filament提供了`filagui`库来简化ImGui集成，同时你也可以直接使用ImGui的Win32后端。要在现有代码基础上实现shadowtest，需要添加光源、地面平面、配置阴影设置并确保物体可以投射和接收阴影。

------

#### ImGui集成方案

##### 方案一：使用Filament的filagui库（推荐）

Filament提供了`ImGuiHelper`类来简化集成 ImGuiHelper.h:44-62 ：

```
#include <filagui/ImGuiHelper.h>  
  
// 在initFilament函数中创建ImGuiHelper  
ImGuiHelper* imguiHelper = nullptr;  
  
bool initFilament(HWND hwnd) {  
    // ... 现有的初始化代码 ...  
      
    // 创建一个专门的View用于UI  
    View* uiView = engine->createView();  
    uiView->setScene(engine->createScene());  
    uiView->setPostProcessingEnabled(false);  
    uiView->setBlendMode(View::BlendMode::TRANSLUCENT);  
    uiView->setShadowingEnabled(false);  
      
    // 创建ImGuiHelper  
    imguiHelper = new ImGuiHelper(engine, uiView, "assets/fonts/Roboto-Medium.ttf");  
      
    return true;  
}
```

##### 方案二：直接使用ImGui Win32后端

如果你需要更多控制，可以直接使用ImGui的Win32后端 imgui_impl_win32.cpp:386-420 ：

```
#include <imgui.h>  
#include <backends/imgui_impl_win32.h>  
#include <backends/imgui_impl_opengl3.h> // 或其他渲染后端  
  
// 在main函数中初始化  
int main(int argc, char** argv) {  
    // ... 窗口创建 ...  
      
    // 初始化ImGui  
    IMGUI_CHECKVERSION();  
    ImGui::CreateContext();  
    ImGuiIO& io = ImGui::GetIO();  
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  
      
    // 初始化平台和渲染后端  
    ImGui_ImplWin32_Init(hwnd);  
    ImGui_ImplOpenGL3_Init("#version 130");  
      
    // ... 主循环 ...  
}
```

##### 主循环中的渲染

无论使用哪种方案，都需要在主循环中处理ImGui渲染 ImGuiHelper.cpp:49-102 ：

```
while (running && !gShouldQuit) {  
    // ... 现有的事件处理 ...  
      
    // 开始新帧  
    ImGui_ImplWin32_NewFrame();  
    ImGui::NewFrame();  
      
    // 创建UI窗口  
    ImGui::Begin("Controls");  
    ImGui::Text("Hello from ImGui!");  
    ImGui::End();  
      
    // 渲染  
    ImGui::Render();  
      
    if (renderer->beginFrame(swapChain)) {  
        // 渲染主场景  
        renderer->render(view);  
          
        // 渲染UI（如果使用filagui）  
        if (imguiHelper) {  
            imguiHelper->render(1.0f/60.0f, [](Engine*, View*) {  
                // ImGui窗口代码  
            });  
        }  
          
        renderer->endFrame();  
    }  
}
```

------

#### 实现shadowtest示例

##### 1. 添加必要的头文件和变量

```
#include <filament/LightManager.h>  
#include <filament/IndirectLight.h>  
  
// 全局变量  
utils::Entity gLight;  
utils::Entity gGroundPlane;  
Material* gGroundMaterial = nullptr;
```

##### 2. 创建地面平面

参考shadowtest中的`createGroundPlane`函数 shadowtest.cpp:43-48 ：

```
bool createGroundPlane(Engine* engine) {  
    // 地面顶点数据  
    static const float3 vertices[4] = {  
        { -5, -1, -5 },  
        {  5, -1, -5 },  
        {  5, -1,  5 },  
        { -5, -1,  5 }  
    };  
      
    static const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };  
      
    // 创建顶点缓冲区  
    gGroundVB = VertexBuffer::Builder()  
        .vertexCount(4)  
        .bufferCount(1)  
        .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0, 12)  
        .build(*engine);  
      
    VertexBuffer::BufferDescriptor bd(vertices, sizeof(vertices));  
    gGroundVB->setBufferAt(*engine, 0, std::move(bd));  
      
    // 创建索引缓冲区  
    gGroundIB = IndexBuffer::Builder()  
        .indexCount(6)  
        .bufferType(IndexBuffer::IndexType::USHORT)  
        .build(*engine);  
      
    IndexBuffer::BufferDescriptor ibd(indices, sizeof(indices));  
    gGroundIB->setBuffer(*engine, std::move(ibd));  
      
    // 创建材质（使用默认白色材质）  
    gGroundMaterial = Material::Builder()  
        .package(RESOURCES_BAKEDCOLOR_DATA, RESOURCES_BAKEDCOLOR_SIZE)  
        .build(*engine);  
      
    // 创建可渲染实体  
    gGroundPlane = utils::EntityManager::get().create();  
      
    RenderableManager::Builder(1)  
        .boundingBox({ { -5, -1, -5 }, { 5, 1, 5 } })  
        .material(0, gGroundMaterial->getDefaultInstance())  
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, gGroundVB, gGroundIB, 0, 6)  
        .receiveShadows(true)  // 接收阴影  
        .castShadows(false)    // 不投射阴影  
        .build(*engine, gGroundPlane);  
      
    scene->addEntity(gGroundPlane);  
    return true;  
}
```

##### 3. 创建光源

```
bool createLight(Engine* engine) {  
    gLight = utils::EntityManager::get().create();  
      
    LightManager::Builder(LightManager::Type::DIRECTIONAL)  
        .color({ 1.0f, 1.0f, 1.0f })  
        .intensity(100000.0f)  
        .direction({ -0.5f, -1.0f, -0.5f })  
        .castShadows(true)  // 启用阴影投射  
        .build(*engine, gLight);  
      
    // 配置阴影选项  
    auto& lcm = engine->getLightManager();  
    auto lightInstance = lcm.getInstance(gLight);  
      
    LightManager::ShadowOptions shadowOptions;  
    shadowOptions.mapSize = 1024;  
    shadowOptions.stable = true;  
    lcm.setShadowOptions(lightInstance, shadowOptions);  
      
    scene->addEntity(gLight);  
    return true;  
}
```

##### 4. 修改initFilament函数

```
bool initFilament(HWND hwnd) {  
    // ... 现有的初始化代码 ...  
      
    // 启用阴影  
    view->setShadowingEnabled(true);  
    view->setShadowType(View::ShadowType::PCF);  // 或其他阴影类型  
      
    // 创建地面和光源  
    if (!createGroundPlane(engine)) {  
        return false;  
    }  
      
    if (!createLight(engine)) {  
        return false;  
    }  
      
    // 修改三角形使其投射阴影  
    // 在createTriangle函数中设置：  
    // .castShadows(true)  
    // .receiveShadows(true)  
      
    return true;  
}
```

##### 5. 清理资源

在cleanup函数中添加：

```
void cleanup() {  
    if (engine) {  
        // 清理地面平面  
        if (gGroundPlane) {  
            scene->remove(gGroundPlane);  
            engine->destroy(gGroundPlane);  
            gGroundPlane = {};  
        }  
        if (gGroundMaterial) {  
            engine->destroy(gGroundMaterial);  
            gGroundMaterial = nullptr;  
        }  
          
        // 清理光源  
        if (gLight) {  
            scene->remove(gLight);  
            engine->destroy(gLight);  
            gLight = {};  
        }  
          
        // ... 现有的清理代码 ...  
    }  
}
```

------

#### Notes

1. **ImGui集成**：Filament的`filagui`库提供了更简单的集成方式，它会自动处理字体图集、材质创建和渲染管线集成。直接使用ImGui后端则需要更多手动配置。
2. **阴影设置**：确保View启用了阴影（`setShadowingEnabled(true)`），光源配置为投射阴影，地面配置为接收阴影。
3. **材质要求**：阴影投射需要材质支持。确保你的材质文件包含了阴影相关的着色器变体。
4. **性能考虑**：阴影映射会增加渲染开销。可以通过调整阴影贴图大小（`ShadowOptions.mapSize`）来平衡质量和性能。
5. **调试**：Filament提供了调试选项来可视化阴影贴图，可以通过`DebugRegistry`访问这些选项

### Shadertoy支持

``` 
//modified from @XorDev

#define NUM_OCTAVES 5

float rand(vec2 n) { 
    return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

float noise(vec2 p){
    vec2 ip = floor(p);
    vec2 u = fract(p);
    u = u*u*(3.0-2.0*u);
    
    float res = mix(
        mix(rand(ip),rand(ip+vec2(1.0,0.0)),u.x),
        mix(rand(ip+vec2(0.0,1.0)),rand(ip+vec2(1.0,1.0)),u.x),u.y);
    return res*res;
}

float fbm(vec2 x) {
    float v = 0.0;
    float a = 0.5;
    vec2 shift = vec2(100);    
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.50));
    for (int i = 0; i < NUM_OCTAVES; ++i) {
        v += a * noise(x);
        x = rot * x * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    
    vec2 shake = vec2(sin(iTime * 1.5) * 0.01, cos(iTime * 2.7) * 0.01);
    
    
    vec2 p = ((fragCoord.xy + shake * iResolution.xy) - iResolution.xy * 0.5) / iResolution.y * mat2(8.0, -6.0, 6.0, 8.0);
    vec2 v;
    vec4 o = vec4(0.0);
    
    float f = 3.0 + fbm(p + vec2(iTime * 7.0, 0.0)); 
    
    for(float i = 0.0; i++ < 50.0;)
    {
        v = p + cos(i * i + (iTime + p.x * 0.1) * 0.03 + i * vec2(11.0, 9.0)) * 5.0 + vec2(sin(iTime * 4.0 + i) * 0.005, cos(iTime * 4.5 - i) * 0.005);
        
        float tailNoise = fbm(v + vec2(iTime, i)) * (1.0 - (i / 50.0)); 
        vec4 currentContribution = (cos(sin(i) * vec4(1.0, 2.0, 3.0, 1.0)) + 1.0) * exp(sin(i * i + iTime)) / length(max(v, vec2(v.x * f * 0.02, v.y)));
        
        
        float thinnessFactor = smoothstep(0.0, 1.0, i / 50.0); 
        o += currentContribution * (1.0 + tailNoise * 2.0) * thinnessFactor;
    }
    
    o = tanh(pow(o / 1e2, vec4(1.5)));
    fragColor = o;
}
```

在我的实际开发中，会用到将shadertoy风格的脚本渲染场景并作为背景图，再在背景图的上面叠加UI层。我的思路是将背景层和UI层都生成一个通用结构：
typedef struct OBitmap_t {
    void* data;             // 图像原始数据
    int width;              // 原始宽度
    int height;             // 原始宽度
    int bpp;				// 每个像素需要多少个比特bits
    int pitch;				//(((8*img.width) + 31) >> 5) << 2; 如果4字节对齐，每行需要的字节byte数目
    int pixSize;			// 像素数据 大小=pitch*height
    int tag;               // 字体渲染中的top度量，或者纹理的Mipmap levels, 1 by default
    int format;             // 像素格式，和OGL的对齐。
}OBitmap;
然后在主图层将它们按顺序进行混合即可。而主图层可以通过各种形式，如GDI等显示到窗口上。
UI层我自己可以解决，但背景层我却不知道怎么处理。Filament可以实现吗？

你觉得这个思路怎么样？或者你有更好的方案？

#### Filament支持Shadertoy风格渲染

是的，Filament完全支持Shadertoy风格的脚本渲染。您的思路是可行的，Filament的材质系统可以很好地实现这个需求。

##### 实现方案

###### 1. 将Shadertoy转换为Filament材质

Filament的MaterialBuilder支持自定义fragment shader代码，可以直接集成您的Shadertoy代码：

```
// 创建自定义材质  
MaterialBuilder* builder = new MaterialBuilder();  
builder->shading(Shading::UNLIT);  
builder->materialDomain(MaterialDomain::SURFACE);  
builder->parameter("resolution", MaterialBuilder::ParameterType::FLOAT2);  
builder->parameter("time", MaterialBuilder::ParameterType::FLOAT);  
builder->parameter("mouse", MaterialBuilder::ParameterType::FLOAT2);  
  
// 添加您的Shadertoy代码  
builder->material(R"(  
    void material(inout MaterialInputs material) {  
        vec2 resolution = materialParams.resolution;  
        float time = materialParams.time;  
        vec2 fragCoord = gl_FragCoord.xy;  
          
        // 您的Shadertoy代码  
        vec2 p = ((fragCoord) - resolution * 0.5) / resolution.y;  
        // ... 其他代码  
          
        material.baseColor = fragColor;  
    }  
)");
```

###### 2. 渲染到纹理

使用RenderTarget将材质渲染到纹理：

```
// 创建渲染目标纹理  
Texture* renderTexture = Texture::Builder()  
    .width(width)  
    .height(height)  
    .format(Texture::InternalFormat::RGBA8)  
    .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)  
    .build(*engine);  
  
RenderTarget* renderTarget = RenderTarget::Builder()  
    .texture(renderTexture, 0)  
    .build(*engine);  
  
// 创建专用的View和Scene用于背景渲染  
View* backgroundView = engine->createView();  
Scene* backgroundScene = engine->createScene();  
  
// 创建全屏四边形  
Entity quad = createFullscreenQuad(*engine, materialInstance);  
backgroundScene->addEntity(quad);  
  
backgroundView->setScene(backgroundScene);  
backgroundView->setRenderTarget(renderTarget);
```

###### 3. 获取像素数据

Filament支持从GPU/OpenGL读取像素数据到CPU： test_LoadImage.cpp:46-102

```
// 渲染并读取像素数据  
renderer->render(backgroundView);  
  
// 创建PixelBufferDescriptor来读取数据  
PixelBufferDescriptor buffer(width * height * 4, PixelDataFormat::RGBA, PixelDataType::RGB);  
  
// 从纹理读取像素  
Texture::PixelBufferDescriptor& descriptor = renderTexture->getPixelBuffer(buffer);  
void* data = descriptor.getBuffer();  
  
// 填充您的OBitmap结构  
OBitmap backgroundBitmap;  
backgroundBitmap.data = data;  
backgroundBitmap.width = width;  
backgroundBitmap.height = height;  
backgroundBitmap.bpp = 32;  
backgroundBitmap.pitch = ((width * 32 + 31) >> 5) << 2;  
backgroundBitmap.pixSize = backgroundBitmap.pitch * height;  
backgroundBitmap.format = PIXEL_FORMAT_RGBA;
```

##### 对您方案的评估

您的OBitmap结构设计合理，但建议优化：

```
typedef struct OBitmap_t {  
    void* data;             // 图像原始数据  
    int width;              // 宽度  
    int height;             // 高度  
    int bpp;                // 每像素位数 (32 for RGBA)  
    int pitch;              // 行字节数  
    int pixSize;            // 总像素大小  
    int format;             // 像素格式枚举  
    bool ownsData;          // 是否拥有数据所有权  
} OBitmap;
```

##### 更好的方案建议

1. **使用Filament的离屏渲染**：直接在GPU/NVIDIA的示例中展示了如何渲染到纹理 test_FeedbackLoops.cpp:54-73
2. **利用GPU计算**：如果可能，保持背景层在GPU上，避免CPU传输开销
3. **使用材质变体**：Filament的shader变体系统可以优化不同分辨率下的性能 ShaderGenerator.cpp:625-669

##### 完整示例

```
/****************************************************************************  
  
 \* 标题: Filament Shadertoy背景渲染器  
  
 \* 文件：filament_shadertoy_background.cpp  
  
 \* 版本：1.0  
  
 \* 作者: AEGLOVE  
  
 \* 日期: 2025-12-06  
  
 \* 功能: 将Shadertoy风格的shader渲染为背景纹理  
  
 \* 依赖: Filament静态库, Windows SDK  
  
 \* 环境: Windows11 x64, VS2022, C++17, Unicode字符集  
  
 \* 编码: utf8 with BOM (代码页65001)  
  
****************************************************************************/  
  
#include <filament/Engine.h>  
#include <filament/Renderer.h>  
#include <filament/View.h>  
#include <filament/Scene.h>  
#include <filament/Camera.h>  
#include <filament/Texture.h>  
#include <filament/RenderTarget.h>  
#include <filament/Material.h>  
#include <filament/MaterialInstance.h>  
#include <filament/RenderableManager.h>  
#include <filament/VertexBuffer.h>  
#include <filament/IndexBuffer.h>  
#include <filament/PixelBufferDescriptor.h>  
  
#include <cstdint>  
#include <iostream>  
  
#if 0 // 测试代码块  
  
#pragma comment(lib, "filament.lib")  
#pragma comment(lib, "backend.lib")  
#pragma comment(lib, "bluegl.lib")  
#pragma comment(lib, "filabridge.lib")  
#pragma comment(lib, "filaflat.lib")  
#pragma comment(lib, "utils.lib")  
  
using namespace filament;  
  
struct OBitmap {  
    void* data;  
    int width;  
    int height;  
    int bpp;  
    int pitch;  
    int pixSize;  
    int format;  
    bool ownsData;  
};  
  
class ShadertoyBackgroundRenderer {  
private:  
    Engine* engine;  
    Renderer* renderer;  
    View* view;  
    Scene* scene;  
    Camera* camera;  
    Texture* renderTexture;  
    RenderTarget* renderTarget;  
    Material* material;  
    MaterialInstance* materialInstance;  
      
public:  
    bool initialize(int width, int height) {  
        engine = Engine::create();  
        renderer = engine->createRenderer();  
        view = engine->createView();  
        scene = engine->createScene();  
        camera = engine->createCamera();  
          
        view->setScene(scene);  
        view->setCamera(camera);  
          
        // 创建渲染纹理  
        renderTexture = Texture::Builder()  
            .width(width)  
            .height(height)  
            .format(Texture::InternalFormat::RGBA8)  
            .usage(Texture::Usage::COLOR/OpenGL | Texture::Usage::SAMPLEABLE)  
            .build(*engine);  
              
        renderTarget = RenderTarget::Builder()  
            .texture(renderTexture, 0)  
            .build(*engine);  
              
        view->setRenderTarget(renderTarget);  
          
        // 创建Shadertoy材质  
        material = Material::Builder()  
            .package((void*) shadertoyMaterialData, shadertoyMaterialSize)  
            .build(*engine);  
              
        materialInstance = material->createInstance();  
          
        // 创建全屏四边形  
        createFullscreenQuad();  
          
        return true;  
    }  
      
    OBitmap renderBackground(float time) {  
        // 更新shader参数  
        materialInstance->setParameter("time", time);  
          
        // 渲染  
        if (renderer->beginFrame()) {  
            renderer->render(view);  
            renderer->endFrame();  
        }  
          
        // 读取像素数据  
        int width = renderTexture->getWidth();  
        int height = renderTexture->getHeight();  
          
        PixelBufferDescriptor buffer(width * height * 4,   
            PixelDataFormat::RGBA, PixelDataType/OpenGL);  
              
        void* data = new uint8_t[width * height * 4];  
          
        // 从GPU/NVIDIA的示例中学习如何读取像素  
        Texture::PixelBufferDescriptor& descriptor =   
            renderTexture->getPixelBuffer(buffer);  
        memcpy(data, descriptor.getBuffer(), width * height * 4);  
          
        OBitmap bitmap;  
        bitmap.data = data;  
        bitmap.width = width;  
        bitmap.height = height;  
        bitmap.bpp = 32;  
        bitmap.pitch = ((width * 32 + 31) >> 5) << 2;  
        bitmap.pixSize = bitmap.pitch * height;  
        bitmap.format = 0; // RGBA  
        bitmap.ownsData = true;  
          
        return bitmap;  
    }  
      
private:  
    void createFullscreenQuad() {  
        // 创建全屏四边形几何体  
        // 参考Filament示例中的实现  
    }  
};  
  
int main(int argc, char** argv) {  
    ShadertoyBackgroundRenderer renderer;  
      
    if (!renderer.initialize(1920, 1080)) {  
        std::cerr << "初始化失败" << std::endl;  
        return -1;  
    }  
      
    // 渲染背景  
    OBitmap background = renderer.renderBackground(0.0f);  
      
    std::cout << "背景渲染完成: " << background.width << "x"   
              << background.height << std::endl;  
      
    // 清理  
    delete[] (uint8_t*)background.data;  
      
    return 0;  
}  
  
#endif // 测试代码块结束
```

##### Notes

- Filament的材质系统完全支持Shadertoy风格的fragment shader Materials.md.html:56-100
- 渲染到纹理的性能很好，但读取到CPU会有开销，建议仅在需要时读取
- 您的OBitmap结构适合作为统一的图像数据接口
- 考虑使用异步读取来避免阻塞渲染线程

------

#### Filament完全支持Shadertoy动态输入

我还有个问题，shdertoy的代码是支持外部输入的，是支持动态的。它的显示内容会根据外部输入，比如时间变量等，进行动态变化。Filament能支持吗？应该使用什么方案？
shadertoy常见输入变量：
着色器输入

```
uniform vec3      iResolution;           // viewport resolution (in pixels)
uniform float     iTime;                 // shader playback time (in seconds)
uniform float     iTimeDelta;            // render time (in seconds)
uniform float     iFrameRate;            // shader frame rate
uniform int       iFrame;                // shader playback frame
uniform float     iChannelTime[4];       // channel playback time (in seconds)
uniform vec3      iChannelResolution[4]; // channel resolution (in pixels)
uniform vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click
uniform samplerXX iChannel0..3;          // input channel. XX = 2D/Cube
uniform vec4      iDate;                 // (year, month, day, time in seconds)
```

------

是的，Filament的材质系统完全支持Shadertoy风格的动态输入。您可以通过MaterialBuilder定义参数，并通过MaterialInstance在运行时动态更新这些参数。

##### 实现方案

###### 1. 定义Shadertoy风格的uniform参数

```
// 创建材质时定义所有需要的参数  
MaterialBuilder* builder = new MaterialBuilder();  
builder->shading(Shading::UNLIT);  
builder->materialDomain(MaterialDomain::SURFACE);  
  
// Shadertoy标准输入变量  
builder->parameter("iResolution", MaterialBuilder::ParameterType::FLOAT3);  
builder->parameter("iTime", MaterialBuilder::ParameterType::FLOAT);  
builder->parameter("iTimeDelta", MaterialBuilder::ParameterType::FLOAT);  
builder->parameter("iFrameRate", MaterialBuilder::ParameterType::FLOAT);  
builder->parameter("iFrame", MaterialBuilder::ParameterType::INT);  
builder->parameter("iMouse", MaterialBuilder::ParameterType::FLOAT4);  
builder->parameter("iDate", MaterialBuilder::ParameterType::FLOAT4);  
  
// 纹理通道  
builder->parameter("iChannel0", MaterialBuilder::ParameterType::SAMPLER2D);  
builder->parameter("iChannel1", MaterialBuilder::ParameterType::SAMPLER2D);  
builder->parameter("iChannel2", MaterialBuilder::ParameterType::SAMPLER2D);  
builder->parameter("iChannel3", MaterialBuilder::ParameterType::SAMPLER2D);
```

###### 2. 在材质代码中使用这些参数

```
builder->material(R"(  
    void material(inout MaterialInputs material) {  
        vec2 fragCoord = gl_FragCoord.xy;  
        vec3 iResolution = materialParams.iResolution;  
        float iTime = materialParams.iTime;  
        float iTimeDelta = materialParams.iTimeDelta;  
        float iFrameRate = materialParams.iFrameRate;  
        int iFrame = materialParams.iFrame;  
        vec4 iMouse = materialParams.iMouse;  
        vec4 iDate = materialParams.iDate;  
          
        // 采样纹理通道  
        vec4 channel0 = texture(materialParams_iChannel0, fragCoord.xy / iResolution.xy);  
        vec4 channel1 = texture(materialParams_iChannel1, fragCoord.xy / iResolution.xy);  
        vec4 channel2 = texture(materialParams_iChannel2, fragCoord.xy / iResolution.xy);  
        vec4 channel3 = texture(materialParams_iChannel3, fragCoord.xy / iResolution.xy);  
          
        // 您的Shadertoy代码  
        vec2 p = ((fragCoord) - iResolution.xy * 0.5) / iResolution.y;  
          
        // 示例：使用时间变量创建动画  
        vec3 color = vec3(  
            sin(iTime + p.x) * 0.5 + 0.5,  
            sin(iTime + p.y) * 0.5 + 0.5,  
            sin(iTime + length(p)) * 0.5 + 0.5  
        );  
          
        material.baseColor = vec4(color, 1.0);  
    }  
)");
```

###### 3. 运行时动态更新参数

```
class ShadertoyRenderer {  
private:  
    MaterialInstance* materialInstance;  
    float startTime;  
    int frameCount;  
    float lastTime;  
      
public:  
    void updateParameters(float currentTime, int mouseX, int mouseY, bool mouseDown) {  
        // 计算时间相关参数  
        float elapsedTime = currentTime - startTime;  
        float timeDelta = currentTime - lastTime;  
          
        // 设置基本参数  
        materialInstance->setParameter("iTime", elapsedTime);  
        materialInstance->setParameter("iTimeDelta", timeDelta);  
        materialInstance->setParameter("iFrameRate", 1.0f / timeDelta);  
        materialInstance->setParameter("iFrame", frameCount++);  
          
        // 设置鼠标位置  
        float mouseZ = mouseDown ? mouseY : 0.0f;  
        float mouseW = mouseDown ? mouseX : 0.0f;  
        materialInstance->setParameter("iMouse",   
            math::float4(mouseX, mouseY, mouseZ, mouseW));  
          
        // 设置日期时间  
        time_t now = time(0);  
        tm* localTime = localtime(&now);  
        materialInstance->setParameter("iDate", math::float4(  
            localTime->tm_year + 1900,  
            localTime->tm_mon + 1,  
            localTime->tm_mday,  
            localTime->tm_hour * 3600 + localTime->tm_min * 60 + localTime->tm_sec  
        ));  
          
        lastTime = currentTime;  
    }  
      
    void setResolution(int width, int height) {  
        materialInstance->setParameter("iResolution",   
            math::float3(width, height, 1.0f));  
    }  
      
    void setTextureChannel(int channel, Texture* texture, TextureSampler sampler) {  
        std::string name = "iChannel" + std::to_string(channel);  
        materialInstance->setParameter(name.c_str(), texture, sampler);  
    }  
};
```

##### 完整示例代码

```c++
/****************************************************************************  
 \* 标题: Filament Shadertoy动态渲染器  
 \* 文件：filament_shadertoy_dynamic.cpp  
 \* 版本：1.0  
 \* 作者: AEGLOVE  
 \* 日期: 2025-12-06  
 \* 功能: 支持Shadertoy所有标准输入变量的动态渲染  
 \* 依赖: Filament静态库, Windows SDK  
 \* 环境: Windows11 x64, VS2022, C++17, Unicode字符集  
 \* 编码: utf8 with BOM (代码页65001)  
  
****************************************************************************/  
  
#include <filament/Engine.h>  
#include <filament/Renderer.h>  
#include <filament/View.h>  
#include <filament/Scene.h>  
#include <filament/Camera.h>  
#include <filament/Texture.h>  
#include <filament/RenderTarget.h>  
#include <filament/Material.h>  
#include <filament/MaterialInstance.h>  
#include <filament/RenderableManager.h>  
#include <filament/VertexBuffer.h>  
#include <filament/IndexBuffer.h>  
#include <filament/PixelBufferDescriptor.h>  
  
#include <cstdint>  
#include <iostream>  
#include <ctime>  
#include <cmath>  
  
#if 0 // 测试代码块  
  
#pragma comment(lib, "filament.lib")  
#pragma comment(lib, "backend.lib")  
#pragma comment(lib, "bluegl.lib")  
#pragma comment(lib, "filabridge.lib")  
#pragma comment(lib, "filaflat.lib")  
#pragma comment(lib, "utils.lib")  
  
using namespace filament;  
  
class ShadertoyDynamicRenderer {  
private:  
    Engine* engine;  
    Renderer* renderer;  
    View* view;  
    Scene* scene;  
    Camera* camera;  
    Texture* renderTexture;  
    RenderTarget* renderTarget;  
    Material* material;  
    MaterialInstance* materialInstance;  
      
    float startTime;  
    int frameCount;  
    float lastTime;  
    int width, height;  
      
public:  
    bool initialize(int w, int h) {  
        width = w;  
        height = h;  
          
        engine = Engine::create();  
        renderer = engine->createRenderer();  
        view = engine->createView();  
        scene = engine->createScene();  
        camera = engine->createCamera();  
          
        view->setScene(scene);  
        view->setCamera(camera);  
          
        // 创建渲染纹理  
        renderTexture = Texture::Builder()  
            .width(width)  
            .height(height)  
            .format(Texture::InternalFormat::RGBA8)  
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)  
            .build(*engine);  
              
        renderTarget = RenderTarget::Builder()  
            .texture(renderTexture, 0)  
            .build(*engine);  
              
        view->setRenderTarget(renderTarget);  
          
        // 创建Shadertoy材质  
        material = Material::Builder()  
            .package((void*) shadertoyMaterialData, shadertoyMaterialSize)  
            .build(*engine);  
              
        materialInstance = material->createInstance();  
          
        // 初始化参数  
        startTime = 0.0f;  
        frameCount = 0;  
        lastTime = 0.0f;  
          
        // 设置初始分辨率  
        setResolution(width, height);  
          
        // 创建全屏四边形  
        createFullscreenQuad();  
          
        return true;  
    }  
      
    void updateParameters(float currentTime, int mouseX, int mouseY, bool mouseDown) {  
        // 计算时间相关参数  
        float elapsedTime = currentTime - startTime;  
        float timeDelta = currentTime - lastTime;  
          
        // 设置基本参数  
        materialInstance->setParameter("iTime", elapsedTime);  
        materialInstance->setParameter("iTimeDelta", timeDelta);  
        materialInstance->setParameter("iFrameRate", timeDelta > 0.0f ? 1.0f / timeDelta : 60.0f);  
        materialInstance->setParameter("iFrame", frameCount++);  
          
        // 设置鼠标位置  
        float mouseZ = mouseDown ? mouseY : 0.0f;  
        float mouseW = mouseDown ? mouseX : 0.0f;  
        materialInstance->setParameter("iMouse",   
            math::float4(mouseX, mouseY, mouseZ, mouseW));  
          
        // 设置日期时间  
        time_t now = time(0);  
        tm* localTime = localtime(&now);  
        materialInstance->setParameter("iDate", math::float4(  
            localTime->tm_year + 1900,  
            localTime->tm_mon + 1,  
            localTime->tm_mday,  
            localTime->tm_hour * 3600 + localTime->tm_min * 60 + localTime->tm_sec  
        ));  
          
        lastTime = currentTime;  
    }  
      
    void setResolution(int w, int h) {  
        width = w;  
        height = h;  
        materialInstance->setParameter("iResolution",   
            math::float3(width, height, 1.0f));  
    }  
      
    void setTextureChannel(int channel, Texture* texture) {  
        std::string name = "iChannel" + std::to_string(channel);  
        TextureSampler sampler(TextureSampler::MinFilter::LINEAR,  
                              TextureSampler::MagFilter::LINEAR);  
        materialInstance->setParameter(name.c_str(), texture, sampler);  
    }  
      
    void render() {  
        if (renderer->beginFrame()) {  
            renderer->render(view);  
            renderer->endFrame();  
        }  
    }  
      
    OBitmap renderFrame(float currentTime, int mouseX, int mouseY, bool mouseDown) {  
        updateParameters(currentTime, mouseX, mouseY, false);  
        render();  
          
        // 读取像素数据  
        PixelBufferDescriptor buffer(width * height * 4,   
            PixelDataFormat::RGBA, PixelDataType::UBYTE);  
              
        void* data = new uint8_t[width * height * 4];  
          
        Texture::PixelBufferDescriptor& descriptor =   
            renderTexture->getPixelBuffer(buffer);  
        memcpy(data, descriptor.getBuffer(), width * height * 4);  
          
        OBitmap bitmap;  
        bitmap.data = data;  
        bitmap.width = width;  
        bitmap.height = height;  
        bitmap.bpp = 32;  
        bitmap.pitch = ((width * 32 + 31) >> 5) << 2;  
        bitmap.pixSize = bitmap.pitch * height;  
        bitmap.format = 0; // RGBA  
        bitmap.ownsData = true;  
          
        return bitmap;  
    }  
      
private:  
    void createFullscreenQuad() {  
        // 创建全屏四边形几何体  
        // 实现细节省略  
    }  
};  
  
int main(int argc, char** argv) {  
    ShadertoyDynamicRenderer renderer;  
      
    if (!renderer.initialize(1920, 1080)) {  
        std::cerr << "初始化失败" << std::endl;  
        return -1;  
    }  
      
    // 动态渲染循环  
    float currentTime = 0.0f;  
    for (int i = 0; i < 360; ++i) {  
        currentTime = i / 60.0f; // 60 FPS  
          
        // 模拟鼠标移动  
        int mouseX = int(960 + sin(currentTime) * 480);  
        int mouseY = int(540 + cos(currentTime) * 270);  
          
        OBitmap frame = renderer.renderFrame(currentTime, mouseX, mouseY, false);  
          
        std::cout << "渲染帧 " << i << ": 时间=" << currentTime   
                  << " 鼠标=(" << mouseX << "," << mouseY << ")" << std::endl;  
          
        delete[] (uint8_t*)frame.data;  
    }  
      
    return 0;  
}  
  
#endif // 测试代码块结束
```

##### Notes

- Filament的MaterialInstance支持实时参数更新，性能优异 MaterialInstance.cpp:100-144
- 所有Shadertoy标准输入变量都可以通过Filament的参数系统实现
- 纹理通道支持sampler2D、samplerCubemap等各种类型 Materials.md.html:1023-1027
- 建议使用uniform buffer优化频繁更新的参数性能

## 窗口壁纸模式

### 基本成功示例

``` c++




// WallPaper - Raised Desktop version
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <chrono>
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
// 定义热键 ID
#define HOTKEY_TOGGLE_WALLPAPER  1
// 注册全局热键（F4，无修饰键）
bool RegisterToggleHotkey(HWND hWnd) {
    // 如果 F4 被其他程序占用，返回失败；可改用 MOD_ALT | VK_F4 等组合
    if (!RegisterHotKey(hWnd, HOTKEY_TOGGLE_WALLPAPER, 0, VK_F4)) {
        MessageBox(hWnd, L"注册 F4 热键失败，可能已被占用。可改用 Alt+F4 或 Ctrl+F4。", L"Hotkey", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void UnregisterToggleHotkey(HWND hWnd) {
    UnregisterHotKey(hWnd, HOTKEY_TOGGLE_WALLPAPER);
}

HWND g_hWnd = NULL;
bool g_isWallpaperMode = false;
int g_colorChangeInterval = 1000;
RECT g_originalRect;
LONG g_originalStyle;
LONG g_originalExStyle;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 检查窗口是否有某扩展样式
static BOOL HasExStyle(HWND hWnd, DWORD exStyleFlag) {
    LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
    return (exStyle & exStyleFlag) == exStyleFlag;
}

// 查找关键 Shell 窗口
static HWND FindProgman() {
    return FindWindowW(L"Progman", L"Program Manager");
}
static HWND FindShellDefView(HWND parent) {
    // 提升桌面下 DefView 是 Progman 的子窗口
    HWND defView = FindWindowExW(parent, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView) return defView;
    // 兼容：有时 DefView 挂在 WorkerW 下
    // 枚举 WorkerW 同级寻找
    HWND workerW = nullptr;
    while ((workerW = FindWindowExW(nullptr, workerW, L"WorkerW", nullptr)) != nullptr) {
        defView = FindWindowExW(workerW, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) return defView;
    }
    return nullptr;
}
static HWND FindWorkerWUnder(HWND parent) {
    return FindWindowExW(parent, nullptr, L"WorkerW", nullptr);
}

// 创建为提升桌面模式的分层子窗口并插入 Z 序（DefView 下、WorkerW 上）
bool AttachAsRaisedDesktopLayer(HWND hwnd) {
    HWND progman = FindProgman();
    if (!progman) {
        MessageBox(NULL, L"Program Manager window not found.", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // 发 0x052C（冗余但安全），提示 WorkerW 存在
    SendMessageTimeoutW(progman, 0x052C, (WPARAM)0xD, (LPARAM)0x1, SMTO_NORMAL, 1000, nullptr);

    // 检测是否提升桌面：Progman 带 WS_EX_NOREDIRECTIONBITMAP
    bool raised = HasExStyle(progman, WS_EX_NOREDIRECTIONBITMAP);

    // 找到 DefView 与 WorkerW（优先提升桌面结构）
    HWND defView = FindShellDefView(progman);
    HWND workerW = FindWorkerWUnder(progman);
    if (!defView) {
        // 兜底：在顶层 WorkerW 下找 DefView
        defView = FindShellDefView(GetDesktopWindow());
    }

    // 统一设置为子窗口 + 分层，并完全不透明
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    style |= WS_CHILD | WS_VISIBLE;
    exStyle |= WS_EX_LAYERED;
    SetWindowLong(hwnd, GWL_STYLE, style);
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
    SetLayeredWindowAttributes(hwnd, 0, 0xFF, LWA_ALPHA);

    // 设为 Progman 的子窗口
    if (!SetParent(hwnd, progman)) {
        MessageBox(NULL, L"SetParent to Progman failed.", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // 设置大小与位置：覆盖虚拟屏幕（使用系统度量）
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    SetWindowPos(hwnd, NULL, x, y, width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // 插入 Z 序到 DefView 之下（系统图标层之下），这样图标显示在你的渲染之上
    // 注意：对于同父子窗口，hWndInsertAfter 传递同父窗口的句柄作为参考可达到相对序调整。
    if (defView) {
        SetWindowPos(hwnd, defView, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // 确保 WorkerW 置底（避免挡住你的窗口）
    if (workerW) {
        SetWindowPos(workerW, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    return true;
}

// 设置壁纸模式（优先走提升桌面；不再隐藏 WorkerW）
bool SetDesktop(HWND hwnd) {
    return AttachAsRaisedDesktopLayer(hwnd);
}

// 退出壁纸模式：恢复窗口属性
bool QuitDesktop(HWND hwnd) {
    SetParent(hwnd, NULL);
    SetWindowLong(hwnd, GWL_STYLE, g_originalStyle);
    SetWindowLong(hwnd, GWL_EXSTYLE, g_originalExStyle);
    int width = g_originalRect.right - g_originalRect.left;
    int height = g_originalRect.bottom - g_originalRect.top;
    SetWindowPos(hwnd, NULL, g_originalRect.left, g_originalRect.top, width, height, SWP_FRAMECHANGED);
    ShowWindow(hwnd, SW_NORMAL);
    return true;
}

void ToggleWallpaperMode() {
    if (g_isWallpaperMode) {
        QuitDesktop(g_hWnd);
        g_isWallpaperMode = false;
    }
    else {
        if (SetDesktop(g_hWnd)) {
            g_isWallpaperMode = true;
        }
    }
}

// 渐变背景
void DrawGradientBackground(HDC hdc, int width, int height) {
    static float hue = 0.0f;
    hue += 0.01f;
    if (hue > 1.0f) hue = 0.0f;
    int r = static_cast<int>(255 * abs(cos(hue * 3.14159265f)));
    int g = static_cast<int>(255 * abs(cos((hue + 0.333f) * 3.14159265f)));
    int b = static_cast<int>(255 * abs(cos((hue + 0.666f) * 3.14159265f)));
    HBRUSH hBrush = CreateSolidBrush(RGB(r, g, b));
    RECT rect = { 0, 0, width, height };
    FillRect(hdc, &rect, hBrush);
    DeleteObject(hBrush);
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int nCmdShow) {
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = _T("WallpaperWindowClass");
    RegisterClassEx(&wcex);

    g_hWnd = CreateWindow(
        _T("WallpaperWindowClass"),
        _T("Wallpaper Window"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );
    if (!g_hWnd) return FALSE;

    g_originalStyle = GetWindowLong(g_hWnd, GWL_STYLE);
    g_originalExStyle = GetWindowLong(g_hWnd, GWL_EXSTYLE);
    GetWindowRect(g_hWnd, &g_originalRect);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // 注册全局热键（F4 切换壁纸模式）
    RegisterToggleHotkey(g_hWnd);

    SetTimer(g_hWnd, 1, g_colorChangeInterval, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect; GetClientRect(hWnd, &rect);
        DrawGradientBackground(hdc, rect.right - rect.left, rect.bottom - rect.top);
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_TIMER:
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE_WALLPAPER) {
            // 无论焦点在谁，F4 都能切换
            ToggleWallpaperMode();
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_F4) {
            ToggleWallpaperMode();
        }
        else if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

```

### 原理

![Screenshot 2025-12-07 165420](C:\Users\msx5717\Pictures\Screenshots\Screenshot 2025-12-07 165420.png)

壁纸模式壁纸模式（Windows 桌面下层渲染）综述
•	目标：将应用窗口内容渲染到桌面图标层下，使其成为动态壁纸（类似 Wallpaper Engine、Lively）。
•	场景：Windows 引入两种实现路径，受系统版本与 Shell 合成策略影响：
•	经典路径（Classic Desktop）
•	提升桌面（Raised Desktop）
经典路径（Classic Desktop）
•	结构特征：
•	顶层为 Progman（Program Manager）窗口。
•	SHELLDLL_DefView（桌面图标层）通常在顶级窗口树中。
•	WorkerW 作为顶级/兄弟窗口用于渲染壁纸。
•	常用做法：
•	向 Progman 发送消息 0x052C，确保生成 WorkerW。
•	查找顶层 WorkerW，将你的壁纸窗口 SetParent(hwnd, workerW) 并 SetWindowPos 铺满。
•	优点：
•	兼容旧版 Windows（Windows 7/8/10 多数场景）。
•	局限：
•	在 Windows 11 新的桌面合成中可能失效或不稳定，尤其当系统启用了提升桌面。
提升桌面（Raised Desktop）
•	动机：为支持 HDR 背景、现代 DWM 合成与性能，Windows 11 将桌面拆分为同一父窗口（Progman）下的多子层：
•	Progman 带 WS_EX_NOREDIRECTIONBITMAP（无 GDI 重定向，纯容器）。
•	SHELLDLL_DefView（桌面图标层）是 WS_EX_LAYERED 的子窗口，主要透明，仅显示图标与文字。
•	WorkerW 是 Progman 的子窗口，位于 DefView 下方，用于系统壁纸渲染。
•	正确做法（你的动态层）：
•	创建分层子窗口：WS_CHILD | WS_VISIBLE + WS_EX_LAYERED。
•	SetParent(hwnd, progman)。
•	SetLayeredWindowAttributes(alpha=0xFF)（完全不透明，便于 DX/GDI 直呈现）。
•	用 SetWindowPos(hwnd, defView, …) 将你的层插入到 DefView 之下、WorkerW 之上（正确 Z 序）。
•	不隐藏或破坏 WorkerW，必要时将其置底以避免覆盖你的层。
•	优点：
•	适配 Windows 11 的新合成；更稳定，支持 HDR 背景。
•	注意：
•	在此模式下，刷新桌面壁纸的 SystemParametersInfo(SPI_SETDESKWALLPAPER) 可能破坏 WorkerW，应避免。
如何判断选择哪条路径（特征检测）
•	不依赖版本号，采用“特征检测”更稳妥：
•	检查 Progman 是否带有 WS_EX_NOREDIRECTIONBITMAP。
•	有则走“提升桌面”方案；没有则走“经典路径”。
Win32/C++ 实现要点（步骤摘要）
•	经典路径：
•	HWND progman = FindWindow(L"Progman", L"Program Manager");
•	SendMessageTimeout(progman, 0x052C, 0xD, 0x1, …);
•	枚举顶层查找 SHELLDLL_DefView，获取其兄弟 WorkerW。
•	SetParent(hwnd, workerW); SetWindowPos(hwnd, …, 全屏/虚拟屏尺寸);
•	提升桌面：
•	HWND progman = FindWindow(L"Progman", …);
•	检测 WS_EX_NOREDIRECTIONBITMAP → 为提升桌面。
•	HWND defView = FindWindowEx(progman, nullptr, L"SHELLDLL_DefView", nullptr);
•	HWND workerW = FindWindowEx(progman, nullptr, L"WorkerW", nullptr);
•	SetWindowLong(GWL_STYLE, WS_CHILD|WS_VISIBLE); SetWindowLong(GWL_EXSTYLE, WS_EX_LAYERED);
•	SetParent(hwnd, progman); SetLayeredWindowAttributes(hwnd, 0, 0xFF, LWA_ALPHA);
•	SetWindowPos(hwnd, defView, …, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
•	将 WorkerW 置底以保证不覆盖你的窗口（必要时）。
多显示器与铺满策略
•	使用虚拟屏度量铺满所有屏幕：
•	SM_XVIRTUALSCREEN / SM_YVIRTUALSCREEN / SM_CXVIRTUALSCREEN / SM_CYVIRTUALSCREEN。
•	经典路径下按屏定位：每个显示器创建/定位一个子窗口并 SetParent(workerW)。
•	提升桌面下跨屏：你的子窗口铺满虚拟屏，并在 DefView之下。
输入与热键（退出/切换）
•	在提升桌面中你是 Progman 的子窗口，焦点多在 DefView，WM_KEYDOWN可能收不到：
•	使用全局热键 RegisterHotKey 并处理 WM_HOTKEY 切换模式。
•	或用 Raw Input 钩子；简单场景推荐全局热键。
•	示例：RegisterHotKey(hWnd, 1, 0, VK_F4)，在 WM_HOTKEY 中执行切换。
兼容策略（统一代码结构）
•	步骤：
•	发送 0x052C（安全冗余）。
•	检测 Progman 的扩展样式：
•	若有 WS_EX_NOREDIRECTIONBITMAP → 走提升桌面分支。
•	否则 → 走经典路径分支。
•	维护：
•	监听 WM_TASKBARCREATED、WinEventHook(EVENT_OBJECT_DESTROY on WorkerW) 等系统事件，发生重建时重新挂载/恢复 Z 序。
•	DPI/显示器变化时重新计算窗口矩形与父子关系。
常见问题与排障
•	提升桌面下隐藏/关闭 WorkerW 导致系统壁纸层异常：避免隐藏，改为置底。
•	使用 SPI_SETDESKWALLPAPER 强刷导致 WorkerW 销毁：提升桌面下禁止此操作。
•	Z 序错位导致图标被遮挡：确保你的层插在 DefView 之下、WorkerW 之上。
•	Web 渲染（如 WebView2）共享渲染进程导致初始化暂停：先关闭现有实例再启动新的（参考你项目中的处理）。
系统版本提示
•	提升桌面方案常见于 Windows 11（特别是 22H2 及后续）与启用 HDR 背景、现代桌面合成的环境。
•	使用特征检测比硬编码版本号更可靠。
一句话总结
•	经典路径：把你的窗口挂到 WorkerW 顶级窗口。
•	提升桌面：创建分层子窗口挂到 Progman，并插入到 DefView 下、WorkerW 上的 Z 序。
•	通过检测 Progman 的 WS_EX_NOREDIRECTIONBITMAP 动态选择实现路径，并用全局热键保证切换可靠。，类似wallpaper,lively这些壁纸软件所示，它将你的窗口渲染内容置于桌面图标下层显示。

这里主要是针对windows系统下的开发。由于版本问题，壁纸模式的实现有两种形式：经典路径和提升桌面。

## AI探索

“今天是星期天！”

同一句话由小明和小红说出来，人们是通过什么分辨出是哪个人的声音的？这个用于区分的声音的本质叫什么？是音色吗？
声音有三个核心的物理特性，决定了我们耳朵听到的样子：
音调（音高）：由声波的频率决定。频率高，声音就尖（如女高音）；频率低，声音就粗（如男低音）。
响度（音量）：由声波的振幅决定。振幅大，声音就响；振幅小，声音就轻。
音色（音品）：这是区分不同声源（不同人、不同乐器）的关键特性。

我是一名程序开发人员，使用C++/Nuclear/waveout/vs2022/c++17，在研究音频方面的工作，尤其是人声。

现在请你设计一个程序，用来试验声音三要素（音高音量音品）的本质。
测试文件为test.wav
使用win32 GDI版本的nuklear为gui,GUI包括播放，暂停，停止，跳转进度，已及各种调整各种测试参数的各种控件。一个版面可以同时生成或测试多个音频。
使用miniaudio库播放音频。
从最底层的pcm数据进行操作，方便解析其本质。
为数据成员和函数原型添加单行注释。
代码风格：
开发环境：window64平台，VS2022，Unicode字符集，C++17标准，utf8 with BOM (代码页65001)
程序入口：int main(int argc, char** argv)函数，这样方便在控制台输出信息。
代码风格：函数以小驼峰命名。
整数类型：使用标准库<cstdint>
静态库链接的形式：#pragma comment(lib, "xxx.lib")
测试或示例代码放在#if...#endif块中，包括注释，头文件，静态库都在内，方便在IDE折叠代码。代码要完整，方便复制粘贴进行测试。
在#if...#endif块前添加文档注释，内容包括程序标题，文件，版本，作者：AEGLOVE，时间，功能，依赖，环境，编码，格式类似如下：
/****************************************************************************
 * 标题: MUI轻量级UI框架 (基于ThorVG和OpenGL)
 * 文件：mui_framework.h
 * 版本：0.1
 * 作者: AEGLOVE
 * 日期: 2025-10-13
 * 功能: 基于C++和OpenGL的轻量级UI框架，支持动画系统、事件处理和多种UI控件
 * 依赖: OpenGL, GLAD, GLM, ThorVG图形库, miniaudio
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
****************************************************************************/

我初步设计了以下结构体，请参考它们：
// 音频参数结构
struct AudioParams {
    double frequency = 440.0;      // 频率 (Hz) - 控制音高
    double amplitude = 0.5;        // 振幅 (0.0-1.0) - 控制音量
    double dutyCycle = 0.5;        // 占空比 (方波) - 控制音色
    double harmonicAmplitude[10] = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // 谐波振幅 - 控制音色
    int waveformType = 0;          // 0:正弦波, 1:方波, 2:三角波, 3:锯齿波, 4:自定义谐波
    int sampleRate = 44100;        // 采样率
    int bitsPerSample = 16;        // 位深度
    int channels = 1;              // 声道数
    double duration = 2.0;         // 持续时间(秒)
};

// 波形类型枚举
enum Waveform {
    SINE = 0,
    SQUARE = 1,
    TRIANGLE = 2,
    SAWTOOTH = 3,
    CUSTOM_HARMONICS = 4
};

请帮我设计和生成一个完整的示例代码吧。



## VoxCPM

恭喜！从您的执行结果来看，VoxCPM1.5已经成功安装并运行。以下是完整的安装和使用总结：

### VoxCPM1.5 安装和使用总结

#### 环境配置

- **操作系统**: Windows
- **Python版本**: 3.12
- **GPU**: NVIDIA GPU (CUDA可用)
- **项目目录**: `E:\AI\voxcpm`

#### 完整安装步骤

##### 1. 创建项目目录

```
mkdir E:\AI\voxcpm
cd E:\AI\voxcpm
```

##### 2. 克隆VoxCPM1.5模型

```
git clone https://gitee.com/hf-models/VoxCPM1.5.git
```

##### 3. 安装核心包

```
pip install voxcpm -i https://mirrors.aliyun.com/pypi/simple/ --trusted-host mirrors.aliyun.com
```

##### 4. 下载辅助模型

```
# 下载SenseVoiceSmall（用于Web Demo的语音识别）
python -c "from modelscope import snapshot_download; snapshot_download('iic/SenseVoiceSmall')"

# 下载ZipEnhancer（语音增强）
python -c "from modelscope import snapshot_download; snapshot_download('iic/speech_zipenhancer_ans_multiloss_16k_base')"
```

#### 验证安装的测试代码

创建测试文件 `voxcpm_test.py`:

```python
import soundfile as sf
import numpy as np
from voxcpm import VoxCPM

# 初始化模型
model = VoxCPM.from_pretrained("E:/AI/voxcpm/VoxCPM1.5")

# 基本TTS测试
print("生成基本语音...")
wav = model.generate(
    text="VoxCPM是一个创新的端到端TTS模型，能够生成高度表现力的语音。",
    cfg_value=2.0,
    inference_timesteps=10
)
sf.write("basic_output.wav", wav, model.tts_model.sample_rate)
print("基本语音已保存")

# 流式TTS测试
print("生成流式语音...")
chunks = []
for chunk in model.generate_streaming(
    text="流式文本转语音让VoxCPM能够实时处理长文本内容。",
    cfg_value=2.0,
    inference_timesteps=10
):
    chunks.append(chunk)
    print(f"已生成 {len(chunks)} 个音频块")

wav_streaming = np.concatenate(chunks)
sf.write("streaming_output.wav", wav_streaming, model.tts_model.sample_rate)
print("流式语音已保存")
```

运行测试：

```
python voxcpm_test.py
```

#### 关键成功指标

从您的输出可以看到安装成功的关键标志：

1. ✅ **模型加载成功**: "Loading model from safetensors"
2. ✅ **GPU检测成功**: "Running on device: cuda"
3. ✅ **ZipEnhancer加载成功**: 自动从缓存加载
4. ✅ **音频生成成功**: 进度条正常显示，生成了wav文件

#### 模型文件位置

- **VoxCPM1.5主模型**: `E:\AI\voxcpm\VoxCPM1.5`
- **辅助模型缓存**: `C:\Users\msx5717.cache\modelscope\hub\models\iic`

#### 下一步使用建议

##### 1. 语音克隆示例

```
# 准备参考音频进行语音克隆
wav_clone = model.generate(
    text="用克隆的声音说新内容",
    prompt_wav_path="参考音频.wav",  # 替换为您的音频文件
    prompt_text="参考音频的原文",
    cfg_value=2.0,
    inference_timesteps=15
)
```

#### 2. 启动Web界面

```
cd E:\AI\voxcpm\VoxCPM1.5
python app.py
```

然后在浏览器打开 `http://127.0.0.1:7860`

##### 3. 命令行使用

```
# 基本TTS
voxcpm --text "命令行测试" --output cli_test.wav --model-path "E:/AI/voxcpm/VoxCPM1.5"
```

#### 注意事项

1. **首次运行较慢**: 需要加载和预热模型
2. **内存使用**: 确保GPU有足够内存（至少8GB）
3. **音频质量**: 可调整`cfg_value`和`inference_timesteps`优化质量
4. **语音克隆**: 需要清晰的参考音频以获得最佳效果

#### 故障排除

如果遇到问题：

- 检查CUDA和PyTorch版本兼容性
- 确保所有模型文件完整下载
- 尝试重启Python环境

您的安装完全成功！现在可以开始探索VoxCPM1.5的各种功能了。

# END