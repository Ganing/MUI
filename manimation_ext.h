/****************************************************************************
 * 标题: manimation_ext.h - 简易动画库扩展
 * 文件: manimation_ext.h
 * 版本: 0.1
 * 作者: AEGLOVE
 * 日期: 2025-10-26
 * 功能: 提供 Timeline/Transition/Animator/type-safe 绑定（float, glm::vec2, ColorF）
 * 依赖: C++17, 可选 glm
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: GB2312 (代码页936)
 ****************************************************************************/

#pragma once

#include <vector>
#include <functional>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <cmath>
#include <cstdint>
#include <string>
#include <sstream>

// optional glm
#if __has_include(<glm/vec2.hpp>)
#include <glm/vec2.hpp>
#define MANIM_HAS_GLM 1
#else
#define MANIM_HAS_GLM 0
#endif

namespace manim {

// easing
enum class EasingType { Linear, EaseIn, EaseOut, EaseInOut, EaseInBack, EaseOutBack };
inline float applyEasing(EasingType type, float t) {
    switch (type) {
    case EasingType::EaseIn: return t * t;
    case EasingType::EaseOut: return 1 - (1 - t) * (1 - t);
    case EasingType::EaseInOut: return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2) / 2.f;
    case EasingType::EaseInBack: { const float c1 = 1.70158f; const float c3 = c1 + 1.f; return c3 * t * t * t - c1 * t * t; }
    case EasingType::EaseOutBack: { const float c1 = 1.70158f; const float c3 = c1 + 1.f; return 1.f + c3 * std::pow(t - 1.f, 3) + c1 * std::pow(t - 1.f, 2); }
    default: return t;
    }
}

// ColorF
struct ColorF { float r,g,b,a; ColorF():r(0),g(0),b(0),a(1){} ColorF(float R,float G,float B,float A=1.0f):r(R),g(G),b(B),a(A){} };

// lerp implementations
inline float lerp_impl(float a, float b, float t) { return a + (b - a) * t; }
inline ColorF lerp_impl(const ColorF& a, const ColorF& b, float t) { return ColorF(a.r + (b.r - a.r)*t, a.g + (b.g - a.g)*t, a.b + (b.b - a.b)*t, a.a + (b.a - a.a)*t); }
#if MANIM_HAS_GLM
inline glm::vec2 lerp_impl(const glm::vec2& a, const glm::vec2& b, float t) { return a + (b - a) * t; }
#endif

// generic fallback: return a when unsupported
template<typename T>
inline T lerp_impl(const T& a, const T& /*b*/, float /*t*/) { return a; }

// Keyframe & Transition
template<typename T>
struct Keyframe { float time; T value; EasingType ease; Keyframe(float t, const T& v, EasingType e = EasingType::Linear) : time(t), value(v), ease(e) {} };

template<typename T>
class Transition {
public:
    void addKeyframe(const Keyframe<T>& k) { keys.push_back(k); std::sort(keys.begin(), keys.end(), [](auto&a, auto&b){ return a.time < b.time; }); }
    T getValueAt(float p) const {
        if (keys.empty()) return T();
        if (p <= keys.front().time) return keys.front().value;
        if (p >= keys.back().time) return keys.back().value;
        auto it = std::lower_bound(keys.begin(), keys.end(), p, [](const Keyframe<T>& k, float v){ return k.time < v; });
        if (it == keys.begin()) return it->value;
        auto prev = it - 1; auto next = it;
        float local = (p - prev->time) / (next->time - prev->time);
        float eased = applyEasing(next->ease, local);
        return lerp_impl(prev->value, next->value, eased);
    }
    bool empty() const { return keys.empty(); }
    // set easing for all keyframes
    void setEasing(EasingType e) { for (auto& k : keys) k.ease = e; }
private:
    std::vector<Keyframe<T>> keys;
};

// Timeline
class Timeline {
public:
    Timeline(float dur = 1.0f) : duration(dur), time(0.0f), playing(false), loop(false) {}
    void play() { playing = true; }
    void pause() { playing = false; }
    void stop() { playing = false; time = 0.0f; }
    void restart() { playing = true; time = 0.0f; } // 新增重启功能
    void setLoop(bool v) { loop = v; }
    void setDuration(float d) { duration = d; }
    void setProgress(float p) { if (duration > 0.f) time = std::max(0.f, std::min(1.f, p)) * duration; }
    void update(float dt) { if (!playing) return; time += dt; if (time > duration) { if (loop) time = std::fmod(time, duration); else { time = duration; playing = false; } } }
    float progress() const { if (duration <= 0.f) return 0.f; return std::max(0.f, std::min(1.f, time / duration)); }
    bool isPlaying() const { return playing; }
private:
    float duration; float time; bool playing; bool loop;
};

// BindingInfo for external inspection
struct BindingInfo { std::string name; bool active; std::string getName() const { return name; } bool isActive() const { return active; } };

// Binder type-erasure
struct BinderBase {
    std::string label;
    std::string lastValue;
    BinderBase(const std::string& l = std::string()) : label(l), lastValue() {}
    virtual ~BinderBase() = default;
    virtual void apply(float p) = 0;
    virtual std::string getName() const { return label; }
    virtual bool isActive() const { return false; }
    virtual std::string getStatus() const { return lastValue; }
};

template<typename T>
struct Binder : BinderBase {
    Transition<T> trans;
    std::function<void(const T&)> setter;
    Binder(Transition<T> t, std::function<void(const T&)> s, const std::string& l = "") : BinderBase(l), trans(std::move(t)), setter(std::move(s)) {}
    void apply(float p) override { 
        T v = trans.getValueAt(p);
        if (setter) setter(v);
        // store lastValue as string
        std::ostringstream oss; 
        if constexpr (std::is_same_v<T, float>) {
            oss.setf(std::ios::fixed); oss.precision(3); oss << v;
        }
        else if constexpr (std::is_same_v<T, ColorF>) {
            oss.setf(std::ios::fixed); oss.precision(2); oss << "r="<<v.r<<",g="<<v.g<<",b="<<v.b<<",a="<<v.a;
        }
        else if constexpr (MANIM_HAS_GLM && std::is_same_v<T, glm::vec2>) {
            oss.setf(std::ios::fixed); oss.precision(2); oss << "x="<<v.x<<",y="<<v.y;
        }
        else {
            // fallback
            oss << "value";
        }
        lastValue = oss.str();
    }
    bool isActive() const override { return !trans.empty(); } // active if has keyframes
    std::string getStatus() const override { return lastValue; }
};

// Animator - manages one timeline and multiple binders
class Animator {
public:
    Animator() { }
    Timeline& getTimeline() { return timeline; }
    size_t getBindingCount() const { return binders.size(); }
    std::string getBindingName(size_t index) const { if (index < binders.size()) return binders[index]->getName(); return std::string(); }
    std::string getBindingStatus(size_t index) const { if (index < binders.size()) return binders[index]->getStatus(); return std::string(); }
    std::vector<BindingInfo> getBindings() const {
        std::vector<BindingInfo> infos;
        for (const auto& b : binders) {
            infos.push_back({ b->getName(), b->isActive() });
        }
        return infos;
    }
    template<typename T>
    void addProperty(Transition<T> t, std::function<void(const T&)> setter, const std::string& name = "") {
        binders.push_back(std::make_unique<Binder<T>>(std::move(t), std::move(setter), name));
    }
    void update(float dt) {
        timeline.update(dt);
        float p = timeline.progress();
        for (auto& b : binders) b->apply(p);
    }
private:
    Timeline timeline;
    std::vector<std::unique_ptr<BinderBase>> binders;
};

} // namespace manim

/****************************************************************************
 * 增强说明：
 * - 新增绑定名称支持（每个 BinderBase 包含 label 字段），便于外部查询/显示。
 * - Animator 提供 getBindingCount/getBindingName 用于 UI 可视化绑定列表。
 * - addProperty 增加可选参数 `name`（默认空），调用方可传入友好名称。
 * - Timeline 新增 restart() 方法用于从头再播放一次。
 * - BindingInfo 供外部查询当前绑定状态（激活/非激活）
 *
 * 注意：此头为轻量级动画辅助库，适合将其直接包含到项目中。若需要封装为静态库，可
 * 将本文件编译为一个源文件导出接口并生成 .lib 然后在项目中以 #pragma comment(lib, "manimation_ext.lib") 链接。
 ****************************************************************************/
