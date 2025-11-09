


#if 0

#include <windows.h>
#include <memory>
#include <iostream>
#include <sstream>
#include <vector>

#include "mui.h"
#include "manimation_ext.h"
#include "resource.h"

// StarBorderElement: simulates the JS/CSS StarBorder effect using generated radial gradient bitmaps
class StarBorderElement : public MUI::UIElement {
public:
    MUI::Color color = MUI::Color(255, 255, 255, 200);
    float speed = 6.0f; // seconds
    float thickness = 1.0f;

    // internal animated offsets [0..1]
    float bottomOffset = 0.0f;
    float topOffset = 0.0f;

    manim::Animator animator;

    StarBorderElement() {
        rect = { 0,0,200,64 };
        // prepare timeline and transitions
        manim::Transition<float> tb;
        tb.addKeyframe(manim::Keyframe<float>(0.0f, 0.0f));
        tb.addKeyframe(manim::Keyframe<float>(1.0f, 1.0f));

        manim::Transition<float> tt = tb; // same pattern (can differ)

        animator.getTimeline().setDuration(speed);
        animator.getTimeline().setLoop(true);

        animator.addProperty<float>(std::move(tb), [this](const float& v) { bottomOffset = v; }, "StarBottom");
        animator.addProperty<float>(std::move(tt), [this](const float& v) { topOffset = v; }, "StarTop");

        animator.getTimeline().play();
    }

    void setColor(const MUI::Color& c) { color = c; }
    void setSpeed(float s) { speed = s; animator.getTimeline().setDuration(speed); }
    void setThickness(float t) { thickness = t; }

    // generate a simple radial gradient RGBA buffer (center->transparent)
    static std::vector<unsigned char> makeRadialRGBA(int w, int h, const MUI::Color& c) {
        std::vector<unsigned char> buf(w * h * 4);
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float maxr = std::sqrt(cx*cx + cy*cy);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float dx = x - cx;
                float dy = y - cy;
                float d = std::sqrt(dx*dx + dy*dy);
                float t = std::clamp(1.0f - d / maxr, 0.0f, 1.0f);
                // apply ease to alpha for smoother falloff
                float alpha = t * t; // quadratic falloff
                unsigned char a = static_cast<unsigned char>(alpha * c.a);
                int idx = (y * w + x) * 4;
                // store as RGBA like stb_image produces
                buf[idx + 0] = c.r; // R
                buf[idx + 1] = c.g; // G
                buf[idx + 2] = c.b; // B
                buf[idx + 3] = a;   // A
            }
        }
        return buf;
    }

    void update(float dt) override {
        animator.update(dt);
    }

    void render(tvg::Scene* parent) override {
        if (!visible) return;

        float w = rect.w, h = rect.h;
        // generate gradient textures (small size, will be scaled)
        const int TEX_W = 256;
        const int TEX_H = 256;
        auto buf = makeRadialRGBA(TEX_W, TEX_H, color);

        // bottom: move right -> left (offset 0..1)
        float bigW = std::max(w, h) * 1.8f;
        float startXb = rect.x + w + bigW * 0.5f;
        float endXb = rect.x - bigW * 0.5f;
        float cxB = startXb + (endXb - startXb) * bottomOffset;
        float cyB = rect.y + h - h * 0.15f;

        // top: move left -> right
        float startXt = rect.x - bigW * 0.5f;
        float endXt = rect.x + w + bigW * 0.5f;
        float cxT = startXt + (endXt - startXt) * topOffset;
        float cyT = rect.y + h * 0.15f;

        // create picture for bottom glow (copy data into tvg)
        {
            auto picB = tvg::Picture::gen();
            auto result = picB->load(reinterpret_cast<const uint32_t*>(buf.data()), TEX_W, TEX_H, tvg::ColorSpace::ABGR8888, true);
            if (result == tvg::Result::Success) {
                float scale = bigW / (float)TEX_W;
                picB->size(TEX_W * scale, TEX_H * scale);
                picB->translate(cxB - (TEX_W * scale) * 0.5f, cyB - (TEX_H * scale) * 0.5f);
                parent->push(picB);
            }
            else {
                // fallback: draw simple circle
                auto shB = tvg::Shape::gen();
                shB->appendCircle(cxB, cyB, bigW * 0.5f, bigW * 0.5f);
                shB->fill(color.r, color.g, color.b, 120);
                parent->push(std::move(shB));
            }
        }

        // top glow
        {
            auto picT = tvg::Picture::gen();
            auto result = picT->load(reinterpret_cast<const uint32_t*>(buf.data()), TEX_W, TEX_H, tvg::ColorSpace::ABGR8888, true);
            if (result == tvg::Result::Success) {
                float scale = bigW / (float)TEX_W;
                picT->size(TEX_W * scale, TEX_H * scale);
                picT->translate(cxT - (TEX_W * scale) * 0.5f, cyT - (TEX_H * scale) * 0.5f);
                parent->push(picT);
            }
            else {
                auto shT = tvg::Shape::gen();
                shT->appendCircle(cxT, cyT, bigW * 0.5f, bigW * 0.5f);
                shT->fill(color.r, color.g, color.b, 100);
                parent->push(std::move(shT));
            }
        }

        // inner rounded rect (content area)
        float corner = std::min(16.0f, h * 0.5f);
        auto bg = tvg::Shape::gen();
        bg->appendRect(rect.x, rect.y, w, h, corner, corner);
        bg->fill(10, 10, 10, 220);
        bg->strokeFill(30, 30, 30, 200);
        bg->strokeWidth(static_cast<float>(thickness));
        parent->push(std::move(bg));

        // inner text centered
        auto text = tvg::Text::gen();
        text->font("siyuan.ttf");
        text->size(14.0f);
        text->text("StarBorder (C++ demo)");
        text->fill(255, 255, 255);
        text->layout(w - 12.0f, h);
        text->align(0.5f, 0.5f);
        text->translate(rect.x + 6.0f, rect.y + h * 0.5f);
        parent->push(std::move(text));
    }
};

// Demo element that contains buttons and labels animated by manim::Animator
class DemoAnimElement : public MUI::UIElement {
public:
    std::unique_ptr<MUI::UIButton> btn;
    std::unique_ptr<MUI::UIButton> btn2;
    std::unique_ptr<MUI::UILabel> titleLabel;
    std::unique_ptr<MUI::UILabel> infoLabel;
    std::unique_ptr<MUI::UILabel> debugLabel;

    std::unique_ptr<StarBorderElement> starBorder;

    manim::Animator animator;

    DemoAnimElement() {
        // Title label (top)
        titleLabel = std::make_unique<MUI::UILabel>(u8"ANI01 - manim + MUI Demo");
        titleLabel->rect = { 20.0f, 8.0f, 760.0f, 32.0f };
        titleLabel->fontSize = 18.0f;
        titleLabel->fontName = "siyuan.ttf";

        // Info label (left)
        infoLabel = std::make_unique<MUI::UILabel>(u8"点击按钮可重启对应动画\n示例显示动画库的用法与调试信息。");
        infoLabel->rect = { 20.0f, 48.0f, 380.0f, 80.0f };
        infoLabel->fontSize = 12.0f;
        infoLabel->fontName = "siyuan.ttf";
        // Make info text bright orange for visibility
        infoLabel->setTextColor(MUI::Color(255, 165, 0, 255));

        // Debug label (right)
        debugLabel = std::make_unique<MUI::UILabel>(u8"Debug:");
        debugLabel->rect = { 420.0f, 48.0f, 360.0f, 120.0f };
        debugLabel->fontSize = 12.0f;
        debugLabel->fontName = "siyuan.ttf";

        // First button and horizontal animation
        btn = std::make_unique<MUI::UIButton>(u8"Animate X");
        btn->rect = { 100.0f, 220.0f, 140.0f, 40.0f };
        btn->fontSize = 16.0f;
        btn->fontName = "siyuan.ttf";

        manim::Transition<float> t;
        t.addKeyframe(manim::Keyframe<float>(0.0f, 100.0f, manim::EasingType::EaseInOut));
        t.addKeyframe(manim::Keyframe<float>(0.5f, 520.0f, manim::EasingType::EaseInOut));
        t.addKeyframe(manim::Keyframe<float>(1.0f, 100.0f, manim::EasingType::EaseInOut));

        // Second button and vertical animation
        btn2 = std::make_unique<MUI::UIButton>(u8"Animate Y");
        btn2->rect = { 100.0f, 300.0f, 140.0f, 40.0f };
        btn2->fontSize = 16.0f;
        btn2->fontName = "siyuan.ttf";

        manim::Transition<float> t2;
        t2.addKeyframe(manim::Keyframe<float>(0.0f, 300.0f, manim::EasingType::EaseInOut));
        t2.addKeyframe(manim::Keyframe<float>(0.4f, 140.0f, manim::EasingType::EaseInOut));
        t2.addKeyframe(manim::Keyframe<float>(1.0f, 300.0f, manim::EasingType::EaseInOut));

        // Timeline configuration
        animator.getTimeline().setDuration(4.0f); // 4 seconds loop
        animator.getTimeline().setLoop(true);

        // Add properties (binders)
        animator.addProperty<float>(std::move(t), [this](const float& v) {
            if (btn) btn->rect.x = v;
        }, "Button X");

        animator.addProperty<float>(std::move(t2), [this](const float& v) {
            if (btn2) btn2->rect.y = v;
        }, "Button Y");

        // clicking buttons will restart the animation
        btn->onClick = [this]() {
            animator.getTimeline().restart();
        };
        btn2->onClick = [this]() {
            animator.getTimeline().restart();
        };

        // start playing
        animator.getTimeline().play();

        // create star border to the right of buttons
        starBorder = std::make_unique<StarBorderElement>();
        starBorder->rect = { 280.0f, 220.0f, 320.0f, 64.0f };
        starBorder->setColor(MUI::Color(0, 255, 255, 200)); // cyan by default
        starBorder->setSpeed(5.0f);
        starBorder->setThickness(1.0f);
    }

    void update(float deltaTime) override {
        // update the animator which will call the setters
        animator.update(deltaTime);

        // update child elements that need time-based updates
        if (btn) btn->update(deltaTime);
        if (btn2) btn2->update(deltaTime);
        if (starBorder) starBorder->update(deltaTime);

        // update debug/info labels
        if (debugLabel) {
            std::ostringstream oss;
            float p = animator.getTimeline().progress();
            oss.setf(std::ios::fixed); oss.precision(3);
            oss << "Timeline progress: " << p << "\n";
            oss << "Playing: " << (animator.getTimeline().isPlaying() ? "yes" : "no") << "  Loop: " << (/*private*/ true ? "yes" : "no") << "\n";

            auto bindings = animator.getBindings();
            oss << "Bindings: " << bindings.size() << "\n";
            for (size_t i = 0; i < bindings.size(); ++i) {
                oss << i << ") " << bindings[i].getName() << " : " << (bindings[i].active ? "active" : "inactive") << "\n";
            }

            debugLabel->setText(oss.str().c_str());
        }

        if (infoLabel) {
            std::string s = u8"示例说明:\n";
            s += u8"- 'Animate X' 在水平方向往返移动。\n";
            s += u8"- 'Animate Y' 在垂直方向往返移动。\n";
            s += u8"- 点击任一按钮可重启动画。\n";
            infoLabel->setText(s.c_str());
        }
    }

    void render(tvg::Scene* parent) override {
        // Render labels and buttons in a safe order
        if (titleLabel) titleLabel->render(parent);
        if (infoLabel) infoLabel->render(parent);
        if (debugLabel) debugLabel->render(parent);
        if (btn) btn->render(parent);
        if (btn2) btn2->render(parent);
        if (starBorder) starBorder->render(parent);
    }

    bool hitTest(float px, float py) override {
        // Check interactive elements: buttons first
        if (btn && btn->hitTest(px, py)) return true;
        if (btn2 && btn2->hitTest(px, py)) return true;
        return false;
    }

    void onMDown(float px, float py) override { if (btn && btn->hitTest(px, py)) btn->onMDown(px, py); if (btn2 && btn2->hitTest(px, py)) btn2->onMDown(px, py); }
    void onMUp(float px, float py) override { if (btn && btn->hitTest(px, py)) btn->onMUp(px, py); if (btn2 && btn2->hitTest(px, py)) btn2->onMUp(px, py); }
    void onMMove(float px, float py) override { if (btn) btn->onMMove(px, py); if (btn2) btn2->onMMove(px, py); }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    MUI::Application app;

    if (!app.init(L"ANI01 - manim + MUI Demo", 800, 600)) {
        std::wcerr << L"Failed to initialize application\n";
        return -1;
    }

    // Load fonts used by UI (from resources)
    app.loadFontFromResource(IDR_FONT_SIYUAN, "siyuan.ttf");
    app.loadFontFromResource(IDR_FONT_FUHAO, "fuhao.ttf");

    // Create demo element and add to UIManager
    auto demo = std::make_unique<DemoAnimElement>();
    demo->visible = true;

    if (auto ui = app.getUIManager()) {
        ui->addElement(std::move(demo));
    }

    // Run application (blocks until quit)
    app.run();

    return 0;
}


#endif // 0
