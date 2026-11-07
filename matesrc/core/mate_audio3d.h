/****************************************************************************
 * mate_audio3d.h - 视频音轨的空间音源（miniaudio ma_engine）
 *
 * 把 mate::VideoSource 解码出的 48k/2ch float 音轨包成自定义 ma_data_source，
 * 挂到 ma_sound 上走引擎的混音 + 3D 空间化：
 *   - 音源位置由调用方每帧给（例如视频立方体的世界坐标）
 *   - 听者位置/朝向由调用方每帧给（例如相机）
 *   - 距离衰减用 inverse 模型，近处不衰减、远处按 rolloff 衰减
 * 场景层只看到这个类，头文件里不含 miniaudio（4MB 头别扩散到每个 TU）。
 ****************************************************************************/
#ifndef MATE_AUDIO3D_H
#define MATE_AUDIO3D_H

namespace mate {

class VideoSource;

class SpatialVideoAudio {
public:
    SpatialVideoAudio();
    ~SpatialVideoAudio();
    SpatialVideoAudio(const SpatialVideoAudio&) = delete;
    SpatialVideoAudio& operator=(const SpatialVideoAudio&) = delete;

    // 创建 ma_engine（默认设备）。失败时 error() 给出原因，后续 attach 返回 false
    bool init();
    void shutdown();

    // 把视频的音轨接上（会自动 detach 上一个音源）
    bool attach(VideoSource& video);
    void detach();
    bool active() const;

    // 每帧调用一次：把听者/音源位置与音量写进引擎
    void update();

    void setListener(float px, float py, float pz, float dx, float dy, float dz);
    void setSourcePosition(float x, float y, float z);
    void setVolume(float volume);        // 0..1，任意线程可调
    void setSpatialized(bool on);        // false 时退回普通 2D 播放

    float distance() const;              // 听者到音源的直线距离
    const char* error() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace mate

#endif // MATE_AUDIO3D_H
