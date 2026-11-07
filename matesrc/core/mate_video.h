/****************************************************************************
 * mate_video.h - 视频帧源（引擎无关，header-only）
 *
 * 只管"解码 + 取帧"，不碰任何图形 API：
 *   解码（独立线程，可关） -> 帧队列 -> advance() 摘一帧到 display_
 * 适配层拿到 plane()/planeWidth() 后自己决定怎么上传（threepp / glTexSubImage2D）。
 *
 * 三个已实现的新特性：
 *   1. 解码时直接缩放：内部调 AVDecoder::setOutputWidth()，sws 直接输出小图，
 *      4K 视频解成 720p 后单帧只有 3.7MB，上传/拷贝都按小图算。
 *   2. YUV420P 三平面：解码直接输出 Y/U/V，不再做 sws 的 YUV→RGB，
 *      GPU 采样时做颜色换算；720p 一帧 1.38MB（RGBA 是 3.69MB）。
 *   3. 行序翻转放在解码线程里做，advance() 只是 std::move，渲染线程零拷贝。
 *
 * 用法：
 *   mate::VideoSource video;
 *   video.open("xxx.mp4", mate::VideoSource::Mode::Threaded, 1280);
 *   // 每帧
 *   if (video.advance(dt)) upload(video.plane(0), ...);   // YUV 时三个平面
 *
 * 依赖: MO/xav.h + FFmpeg 静态库（见 matex2.bat / mate.bat 的 FF_* 变量）
 ****************************************************************************/
#ifndef MATE_VIDEO_H
#define MATE_VIDEO_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xav.h"

namespace mate {

class VideoSource {
public:
    enum class Mode {
        Sync,      // 解码留在调用线程：实现简单，但每解一帧都会占住渲染线程
        Threaded   // 解码在独立线程：渲染线程只摘帧，跟不上时保持上一帧
    };

    struct Stats {
        float decodeMs  = 0.f;   // 单帧解码 + sws 转换耗时（EMA）
        int   decoded   = 0;     // 累计解码帧数
        int   presented = 0;     // 累计提交给渲染线程的帧数
    };

    VideoSource() = default;
    ~VideoSource() { close(); }
    VideoSource(const VideoSource&) = delete;
    VideoSource& operator=(const VideoSource&) = delete;

    // targetWidth > 0 时解码直接输出该宽度（高度按比例）；大于原宽则按原尺寸。
    // yuv = true 时输出 YUV420P 三平面（默认，最省带宽）；false 回退打包 RGBA。
    bool open(const std::string& path, Mode mode = Mode::Threaded,
              int targetWidth = 0, bool yuv = true) {
        close();

        decoder_.setOutputWidth(targetWidth);
        decoder_.setOutputYuv420p(yuv);
        if (!decoder_.open(path.c_str())) return false;

        w_ = decoder_.getWidth();
        h_ = decoder_.getHeight();
        if (!decoder_.hasVideo() || w_ <= 0 || h_ <= 0) {
            decoder_.close();
            return false;
        }

        yuv_    = yuv;
        mode_   = mode;
        clock_  = 0.0;
        lastPts_ = -1.0;
        opened_ = true;

        if (mode_ == Mode::Threaded) {
            // 先同步解一帧垫底，避免开局显示占位黑图，再交给解码线程
            if (!decodeAndStore()) {
                decoder_.seek(0.0);
                audioQueue_.clear();
                decodeAndStore();
            }
            running_ = true;
            worker_  = std::thread([this] { workerLoop(); });
        } else {
            decodeAndStore();
        }
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            running_ = false;
        }
        cvSpace_.notify_all();
        if (worker_.joinable()) worker_.join();

        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.clear();
        }
        decoder_.close();
        display_ = Frame{};
        w_ = h_ = 0;
        opened_ = false;

        std::lock_guard<std::mutex> lock(mtx_);
        audioQueue_.clear();   // 别把上一条视频没播完的音轨留给下一条
    }

    // 每帧调用一次（渲染线程）：推进时钟，摘到期的帧。返回 true 表示有新画面
    bool advance(float dt) {
        if (!opened_) return false;

        if (audioClock_.load()) {
            // 音频已开播：进度跟"音频已消费位置"，而不是墙钟。
            // 否则解码产出的音频比设备消费快，队列写满后丢样本 = 断音/沙沙声。
            clock_ = audioQueue_.consumedSeconds();
        } else {
            clock_ += double(dt);
        }
        return mode_ == Mode::Threaded ? advanceThreaded() : advanceSync();
    }

    // 由音频模块在 attach 时打开：视频进度以音频播放位置为准，并对解码线程做水位反压
    void setAudioClock(bool on) { audioClock_.store(on); }
    [[nodiscard]] bool hasAudio() const { return decoder_.hasAudio(); }

    // 最近一帧：YUV420P 时三个平面各自紧凑存放，第 0 行 = 画面底部（GL 行序）
    [[nodiscard]] bool isYuv() const { return yuv_; }
    [[nodiscard]] const unsigned char* plane(int i) const {
        return (i >= 0 && i < 3) ? display_.planes[i].data() : nullptr;
    }
    [[nodiscard]] int planeWidth(int i) const {
        return (i >= 0 && i < 3) ? display_.w[i] : 0;
    }
    [[nodiscard]] int planeHeight(int i) const {
        return (i >= 0 && i < 3) ? display_.h[i] : 0;
    }

    // 输出尺寸（Y 平面）
    [[nodiscard]] int width() const { return w_; }
    [[nodiscard]] int height() const { return h_; }
    [[nodiscard]] bool isOpen() const { return opened_; }

    [[nodiscard]] Stats stats() const {
        std::lock_guard<std::mutex> lock(statsMtx_);
        return stats_;
    }

    // 视频音轨（固定 48k/2ch float32）：交给外部音频消费者（miniaudio 数据源）拉取。
    // 视频循环/切源时内部会清空队列，消费者读到空时补静音即可。
    [[nodiscard]] AudioQueue& audioQueue() { return audioQueue_; }
    [[nodiscard]] int audioSampleRate() const { return kAudioSampleRate; }
    [[nodiscard]] int audioChannels() const { return kAudioChannels; }

private:
    struct Frame {
        double pts = 0.0;
        bool   yuv = false;
        std::vector<unsigned char> planes[3];   // 已翻成 GL 行序（第 0 行 = 画面底部）
        int    w[3] = {0, 0, 0};                // 平面宽（= 紧凑行宽）
        int    h[3] = {0, 0, 0};
    };

    static constexpr size_t kQueueMax = 2;   // 一帧 720p YUV 1.4MB / RGBA 3.7MB
    // 音频高水位（0.375s @ 48k/2ch 的样本数）：解码产出的音轨超过它就让线程等，
    // 宁可视频慢半拍，也不能写满 AudioQueue 后丢样本（丢样本 = 断音/沙沙声）
    static constexpr size_t kAudioHighWater = 36000;
    // 帧"到期"容差：必须小于半帧间隔（30fps≈33ms），否则两帧会同时到期，
    // 循环里一次弹出两帧却只显示最后一帧 → 每隔一帧丢一帧（画面顿）
    static constexpr double kDueSlack = 0.012;
    std::atomic<bool> audioClock_{false};

    // 同步模式：到点就解一帧，播完回到开头
    bool advanceSync() {
        bool got  = false;
        int  guard = 0;
        while (lastPts_ <= clock_ + kDueSlack && guard++ < 4) {
            if (decodeAndStore()) { got = true; continue; }

            decoder_.seek(0.0);              // nullptr = 视频到头
            audioQueue_.clear();
            clock_   = 0.0;
            lastPts_ = -0.04;
            if (!decodeAndStore()) break;
            got = true;
        }
        return got;
    }

    // 线程模式：只从队列里摘"已经到期"的帧；队列空就继续显示上一帧
    bool advanceThreaded() {
        bool  got = false;
        Frame frame;

        {
            std::lock_guard<std::mutex> lock(mtx_);
            // 一次只摘最早的到期帧：多摘几帧再"只显示最后一帧"等于主动丢帧，
            // 30fps 素材在大屏上会看起来一顿一顿的。剩下的留给下一个渲染帧（6ms 后）。
            if (!queue_.empty() && queue_.front().pts <= clock_ + kDueSlack) {
                frame = std::move(queue_.front());
                queue_.pop_front();
                got = true;
                cvSpace_.notify_one();
            }
        }

        if (!got) return false;   // 解码没跟上：这一帧跳过，画面保持

        // pts 往回跳 = 视频循环播放，把时钟对齐到新的 pts，避免"追帧"
        if (lastPts_ >= 0.0 && frame.pts < lastPts_) clock_ = frame.pts;

        lastPts_ = frame.pts;
        display_ = std::move(frame);   // 解码线程已翻转行序，这里零拷贝
        recordPresent();
        return true;
    }

    bool decodeAndStore() {
        const auto t0 = std::chrono::steady_clock::now();

        double pts = 0.0;
        if (!decoder_.decodeNextFrame(pts, audioQueue_)) return false;

        fillFrame(display_, pts);
        lastPts_ = pts;
        recordDecode(elapsedMs(t0));
        recordPresent();
        return true;
    }

    // 解码线程：有空格就解一帧塞进队列，播完自动 seek 回 0
    void workerLoop() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cvSpace_.wait(lock, [this] { return !running_ || queue_.size() < kQueueMax; });
                if (!running_) return;
            }

            // 音频水位反压：等设备把积压消费下去再继续解，保证不丢音频样本
            while (running_ && audioClock_.load() &&
                   audioQueue_.size() >= kAudioHighWater) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!running_) return;

            const auto t0 = std::chrono::steady_clock::now();
            double pts = 0.0;
            if (!decoder_.decodeNextFrame(pts, audioQueue_)) {   // 到头：回到开头接着解
                decoder_.seek(0.0);
                audioQueue_.clear();
                continue;
            }

            Frame frame;
            fillFrame(frame, pts);
            const float decodeMs = elapsedMs(t0);

            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (!running_) return;
                queue_.push_back(std::move(frame));
            }
            recordDecode(decodeMs);
        }
    }

    // 把解码器当前帧拷进 Frame：逐平面拷贝 + 翻行序（解码线程里做，渲染线程零拷贝）
    void fillFrame(Frame& f, double pts) {
        f.pts = pts;
        f.yuv = yuv_;
        if (!yuv_) {
            const int W = w_, H = h_;
            f.w[0] = W;
            f.h[0] = H;
            f.planes[0].resize((size_t)W * H * 4);
            flipRows(f.planes[0].data(), decoder_.getFramePlane(0), (size_t)W * 4, H);
            return;
        }
        for (int p = 0; p < 3; ++p) {
            const int pw = decoder_.planeWidth(p), ph = decoder_.planeHeight(p);
            f.w[p] = pw;
            f.h[p] = ph;
            f.planes[p].resize((size_t)pw * ph);
            flipRows(f.planes[p].data(), decoder_.getFramePlane(p), (size_t)pw, ph);
        }
    }

    // FFmpeg 行序自上而下，GL 纹理第 0 行在 v=0（画面下方）：
    // 翻成"第 0 行 = 画面底部"，采样时就能按常规 uv 直接用
    static void flipRows(unsigned char* dst, const unsigned char* src,
                         size_t rowBytes, int rows) {
        if (!src) return;
        for (int y = 0; y < rows; ++y) {
            std::memcpy(dst + (size_t)(rows - 1 - y) * rowBytes,
                        src + (size_t)y * rowBytes, rowBytes);
        }
    }

    static float elapsedMs(std::chrono::steady_clock::time_point t0) {
        return float(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count());
    }

    void recordDecode(float ms) {
        std::lock_guard<std::mutex> lock(statsMtx_);
        stats_.decodeMs = stats_.decodeMs * 0.9f + ms * 0.1f;
        ++stats_.decoded;
    }

    void recordPresent() {
        std::lock_guard<std::mutex> lock(statsMtx_);
        ++stats_.presented;
    }

    AVDecoder  decoder_;
    AudioQueue audioQueue_;   // 本类不出声；队列满了 write 会自己丢包

    Mode   mode_   = Mode::Threaded;
    bool   yuv_    = true;
    int    w_ = 0, h_ = 0;
    bool   opened_ = false;
    double clock_  = 0.0;
    double lastPts_ = -1.0;   // <0：还没显示过任何帧

    Frame display_;           // 最近一帧（GL 行序）

    std::thread             worker_;
    std::mutex              mtx_;
    std::condition_variable cvSpace_;
    std::deque<Frame>       queue_;
    bool                    running_ = false;

    mutable std::mutex statsMtx_;
    Stats              stats_;
};

}// namespace mate

#endif//MATE_VIDEO_H
