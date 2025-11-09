

/****************************************************************************
 * 标题: ANI02.cpp - StarBorder 按钮示例（沿边框流动的星光/流光）
 * 文件: ANI02.cpp
 * 版本: 0.1
 * 作者: AEGLOVE (示例整合)
 * 日期: 2025-11-09
 *
 * 简要说明:
 *   本示例实现一个自定义 UI 元素 `MUI::StarBorderButton`，演示如何使用
 *   manimation_ext.h 中的 `Animator/Transition` 将光晕和“主星光”沿按钮的
 *   圆角矩形边框流动。
 *
 * 关键点:
 *   - 顶部/底部使用两条沿边内侧滑动的柔和径向渐变（RadialGradient）；
 *   - 主星光沿圆角矩形周长参数化（0..1）循环移动，真实贴合边框路径；
 *   - 动画由 manim::Animator 管理；每个属性通过 addProperty 绑定到 setter；
 *   - 文本显示需在 Application 初始化后调用 app.loadFontFromResource(...) 加载字体。
 *
 * 使用/集成:
 *   - 将该文件编译到项目中（要求 ThorVG/GL/Glad、mui.h、manimation_ext.h 可用）；
 *   - 程序启动后会创建窗口并展示若干 StarBorderButton 实例。
 *
 * 注意:
 *   - 当前使用 ThorVG 的 RadialGradient 实现局部发光；可依据需要替换为预生成纹理
 *     或更多粒子以提升视觉效果或性能；
 *   - 请保证资源文件（resource.rc）中包含字体资源 IDR_FONT_SIYUAN，并在运行时
 *     调用 `app.loadFontFromResource(IDR_FONT_SIYUAN, "siyuan.ttf")`。
 ****************************************************************************/


#if 0
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include "mui.h"  
#include "manimation_ext.h"  
#include <cstdio>  

namespace MUI {

    class StarBorderButton : public UIElement {
    private:
        // 动画参数
        manim::Animator animatorBottom;
        manim::Animator animatorTop;

        // 光晕位置和透明度
        float bottomGradientX = 0.0f;
        float bottomGradientOpacity = 1.0f;
        float topGradientX = 0.0f;
        float topGradientOpacity = 1.0f;

        // 样式参数
        manim::ColorF gradientColor{ 0.0f, 1.0f, 1.0f, 1.0f };  // cyan
        float cornerRadius = 20.0f;
        float borderThickness = 2.0f;
        std::string buttonText = "Button";

        // 内容样式
        MUI::Color bgColor{ 0, 0, 0, 255 };
        MUI::Color borderColor{ 34, 34, 34, 255 };
        MUI::Color textColor{ 255, 255, 255, 255 };

    public:
        StarBorderButton() {
            rect = { 100, 100, 200, 60 };
            setupAnimations();
        }

        void setupAnimations() {
            // 底部光晕动画：从右向左移动
            manim::Transition<float> bottomXTrans;
            bottomXTrans.addKeyframe({ 0.0f, 1.0f });      // 从右侧开始
            bottomXTrans.addKeyframe({ 1.0f, -1.0f });     // 移动到左侧

            manim::Transition<float> bottomOpacityTrans;
            bottomOpacityTrans.addKeyframe({ 0.0f, 1.0f });
            bottomOpacityTrans.addKeyframe({ 1.0f, 0.0f });

            animatorBottom.addProperty<float>(bottomXTrans,
                [this](const float& v) { bottomGradientX = v; }, "bottomX");
            animatorBottom.addProperty<float>(bottomOpacityTrans,
                [this](const float& v) { bottomGradientOpacity = v; }, "bottomOpacity");

            animatorBottom.getTimeline().setDuration(6.0f);
            animatorBottom.getTimeline().setLoop(true);
            animatorBottom.getTimeline().play();

            // 顶部光晕动画：从左向右移动
            manim::Transition<float> topXTrans;
            topXTrans.addKeyframe({ 0.0f, -1.0f });        // 从左侧开始
            topXTrans.addKeyframe({ 1.0f, 1.0f });         // 移动到右侧

            manim::Transition<float> topOpacityTrans;
            topOpacityTrans.addKeyframe({ 0.0f, 1.0f });
            topOpacityTrans.addKeyframe({ 1.0f, 0.0f });

            animatorTop.addProperty<float>(topXTrans,
                [this](const float& v) { topGradientX = v; }, "topX");
            animatorTop.addProperty<float>(topOpacityTrans,
                [this](const float& v) { topGradientOpacity = v; }, "topOpacity");

            animatorTop.getTimeline().setDuration(6.0f);
            animatorTop.getTimeline().setLoop(true);
            animatorTop.getTimeline().play();
        }

        void setText(const std::string& text) { buttonText = text; }
        void setGradientColor(float r, float g, float b) {
            gradientColor = manim::ColorF(r, g, b, 1.0f);
        }
        void setSpeed(float seconds) {
            animatorBottom.getTimeline().setDuration(seconds);
            animatorTop.getTimeline().setDuration(seconds);
        }

        void update(float dt) override {
            animatorBottom.update(dt);
            animatorTop.update(dt);
        }

        void render(tvg::Scene* parent) override {
            if (!visible) return;

            // 1. 渲染光晕效果，但只显示在边框裁剪区域内
            renderGradientEffects(parent);

            // 2. 渲染内部按钮内容（覆盖光晕的内部部分）
            renderButtonContent(parent);
        }

    private:
        void renderGradientEffects(tvg::Scene* parent) {
            float gradientSize = rect.w * 3.0f;

            // 底部光晕  
            renderSingleGradient(parent, bottomGradientX, bottomGradientOpacity,
                rect.y + rect.h, gradientSize);

            // 顶部光晕  
            renderSingleGradient(parent, topGradientX, topGradientOpacity,
                rect.y, gradientSize);
        }

        void renderSingleGradient(tvg::Scene* parent, float xProgress,
            float opacity, float centerY, float size) {
            float centerX = rect.x + rect.w * 0.5f + xProgress * rect.w * 1.5f;

            // 创建径向渐变  
            auto gradient = tvg::RadialGradient::gen();
            gradient->radial(centerX, centerY, size * 0.5f, centerX, centerY, 0.0f);

            tvg::Fill::ColorStop stops[3];
            stops[0] = { 0.0f,
                        static_cast<uint8_t>(gradientColor.r * 255),
                        static_cast<uint8_t>(gradientColor.g * 255),
                        static_cast<uint8_t>(gradientColor.b * 255),
                        static_cast<uint8_t>(opacity * 0.7f * 255) };
            stops[1] = { 0.2f,
                        static_cast<uint8_t>(gradientColor.r * 255),
                        static_cast<uint8_t>(gradientColor.g * 255),
                        static_cast<uint8_t>(gradientColor.b * 255), 0 };
            stops[2] = { 1.0f, 0, 0, 0, 0 };
            gradient->colorStops(stops, 3);

            // 创建光晕形状  
            auto glow = tvg::Shape::gen();
            glow->appendCircle(centerX, centerY, size * 0.5f, size * 0.15f);
            glow->fill(std::move(gradient));

            // 创建裁剪区域（边框区域的圆角矩形）  
            auto clipper = tvg::Shape::gen();
            float clipX = rect.x - borderThickness;
            float clipY = rect.y - borderThickness;
            float clipW = rect.w + 2 * borderThickness;
            float clipH = rect.h + 2 * borderThickness;
            clipper->appendRect(clipX, clipY, clipW, clipH, cornerRadius, cornerRadius);

            // 应用裁剪  
            glow->clip(clipper);
            parent->push(std::move(glow));
        }

        void renderButtonContent(tvg::Scene* parent) {
            // 渲染按钮背景  
            auto bg = tvg::Shape::gen();
            bg->appendRect(rect.x, rect.y, rect.w, rect.h, cornerRadius, cornerRadius);
            bg->fill(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            parent->push(std::move(bg));

            // 渲染边框  
            auto border = tvg::Shape::gen();
            border->appendRect(rect.x, rect.y, rect.w, rect.h, cornerRadius, cornerRadius);
            border->strokeFill(borderColor.r, borderColor.g, borderColor.b, borderColor.a);
            border->strokeWidth(borderThickness);
            border->fill(0, 0, 0, 0);
            parent->push(std::move(border));

            renderText(parent);
        }

        void renderText(tvg::Scene* parent) {
            auto text = tvg::Text::gen();
            text->font(u8"siyuan.ttf");
            text->size(16);
            text->text(u8"按钮文本");  // 使用 u8 前缀  
            text->fill(textColor.r, textColor.g, textColor.b);

            float tw, th;
            text->bounds(nullptr, nullptr, &tw, &th);
            text->translate(rect.x + (rect.w - tw) / 2, rect.y + (rect.h - th) / 2);
            parent->push(std::move(text));
        }

    public:
        bool hitTest(float px, float py) override {
            return rect.contains(px, py);
        }
    };

} // namespace MUI

int main(int argc, char** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);

    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        fprintf(stderr, "ThorVG 初始化失败\n");
        return -1;
    }

    MUI::Application app;
    if (!app.init(L"StarBorder 动画示例", 800, 600, 1)) {
        fprintf(stderr, "应用初始化失败\n");
        tvg::Initializer::term();
        return -1;
    }

    app.loadFontFromResource(IDR_FONT_SIYUAN, "siyuan.ttf");
    auto* ui = app.getUIManager();

    auto btn1 = std::make_unique<MUI::StarBorderButton>();
    btn1->rect = MUI::Rect(100, 100, 200, 60);
    btn1->setText(u8"青色流光边框");
    btn1->setGradientColor(0.0f, 1.0f, 1.0f);
    btn1->setSpeed(6.0f);
    ui->addElement(std::move(btn1));

    auto btn2 = std::make_unique<MUI::StarBorderButton>();
    btn2->rect = MUI::Rect(100, 200, 200, 60);
    btn2->setText(u8"紫色流光边框");
    btn2->setGradientColor(1.0f, 0.0f, 1.0f);
    btn2->setSpeed(4.0f);
    ui->addElement(std::move(btn2));

    auto btn3 = std::make_unique<MUI::StarBorderButton>();
    btn3->rect = MUI::Rect(100, 300, 200, 60);
    btn3->setText(u8"黄色流光边框");
    btn3->setGradientColor(1.0f, 1.0f, 0.0f);
    btn3->setSpeed(8.0f);
    ui->addElement(std::move(btn3));

    app.run();
    tvg::Initializer::term();
    return 0;
}

#endif // 1




