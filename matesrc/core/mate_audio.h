/****************************************************************************
 * mate_audio.h - 引擎音频循环（miniaudio）
 *
 * 只暴露一个无缝循环音轨：加载文件 -> 解码 -> 播放，回调里到达末尾自动回绕。
 * 场景层不包含 miniaudio 头，避免 4MB 头文件扩散到每个编译单元。
 ****************************************************************************/
#pragma once

#include <string>

namespace mate {

class AudioLoop {
public:
    AudioLoop();
    ~AudioLoop();
    AudioLoop(const AudioLoop&) = delete;
    AudioLoop& operator=(const AudioLoop&) = delete;

    bool start(const std::string& path, float volume);
    void stop();

    void setVolume(float volume);   // 0..1，可在任意线程调用
    void setPaused(bool paused);
    bool active() const;
    const char* error() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace mate
