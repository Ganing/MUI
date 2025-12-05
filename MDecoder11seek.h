enum class VideoFormat : std::uint32_t {
    Unknown = 0,
    ARGB32 = 1,    // 8:8:8:8
    YUV420P = 2
};

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

    void requestSeek(double seconds) {
        seekTargetSeconds.store(seconds);
        seekRequested.store(true);
        seekGeneration.store(seekGeneration.load() + 1);
    }

    // 新增：快速查询音视频时长的静态函数  
    struct DurationInfo {
        double videoDuration = -1.0;  // 视频时长（秒），-1表示无视频流  
        double audioDuration = -1.0;  // 音频时长（秒），-1表示无音频流  
        int videoWidth = 0;
        int videoHeight = 0;
        double videoFps = 0.0;
        int audioFreq = 0;
        int audioChannels = 0;
    };

    static bool queryDuration(FILE* infile, DurationInfo& info) {
        if (!infile) return false;

        // 保存当前位置  
        long originalPos = ftell(infile);

        // 临时状态变量  
        ogg_sync_state oy{};
        ogg_page og{};
        ogg_packet op{};
        ogg_stream_state to{}, vo{};
        th_info ti{};
        th_comment tc{};
        th_setup_info* ts = nullptr;
        vorbis_info vi{};
        vorbis_comment vc{};

        bool hasTheora = false, hasVorbis = false;
        int theoraHeaders = 0, vorbisHeaders = 0;

        // 初始化  
        ogg_sync_init(&oy);
        th_info_init(&ti);
        th_comment_init(&tc);
        vorbis_info_init(&vi);
        vorbis_comment_init(&vc);

        // 1. 快速扫描头部信息  
        fseek(infile, 0, SEEK_SET);

        while (!hasTheora || !hasVorbis) {
            char* buffer = ogg_sync_buffer(&oy, 4096);
            int bytes = (int)fread(buffer, 1, 4096, infile);
            if (bytes == 0) break;
            ogg_sync_wrote(&oy, bytes);

            while (ogg_sync_pageout(&oy, &og) > 0) {
                if (ogg_page_bos(&og)) {
                    ogg_stream_state test{};
                    ogg_stream_init(&test, ogg_page_serialno(&og));
                    ogg_stream_pagein(&test, &og);
                    ogg_stream_packetout(&test, &op);

                    if (!hasTheora && th_decode_headerin(&ti, &tc, &ts, &op) >= 0) {
                        to = test;
                        hasTheora = true;
                        theoraHeaders = 1;
                    }
                    else if (!hasVorbis && vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {
                        vo = test;
                        hasVorbis = true;
                        vorbisHeaders = 1;
                    }
                    else {
                        ogg_stream_clear(&test);
                    }
                }
                else {
                    if (hasTheora) ogg_stream_pagein(&to, &og);
                    if (hasVorbis) ogg_stream_pagein(&vo, &og);
                }
            }
        }

        // 2. 读取剩余头部  
        while ((hasTheora && theoraHeaders < 3) || (hasVorbis && vorbisHeaders < 3)) {
            while (hasTheora && theoraHeaders < 3 && ogg_stream_packetout(&to, &op)) {
                if (th_decode_headerin(&ti, &tc, &ts, &op) > 0) theoraHeaders++;
            }
            while (hasVorbis && vorbisHeaders < 3 && ogg_stream_packetout(&vo, &op)) {
                if (vorbis_synthesis_headerin(&vi, &vc, &op) == 0) vorbisHeaders++;
            }

            if (ogg_sync_pageout(&oy, &og) > 0) {
                if (hasTheora) ogg_stream_pagein(&to, &og);
                if (hasVorbis) ogg_stream_pagein(&vo, &og);
            }
            else {
                char* buffer = ogg_sync_buffer(&oy, 4096);
                int bytes = (int)fread(buffer, 1, 4096, infile);
                if (bytes == 0) break;
                ogg_sync_wrote(&oy, bytes);
            }
        }

        // 3. 保存基本信息  
        if (hasTheora) {
            info.videoWidth = ti.pic_width;
            info.videoHeight = ti.pic_height;
            info.videoFps = (double)ti.fps_numerator / ti.fps_denominator;
        }
        if (hasVorbis) {
            info.audioFreq = vi.rate;
            info.audioChannels = vi.channels;
        }

        // 4. 快速扫描文件末尾获取最后的 granulepos  
        ogg_int64_t lastVideoGranule = -1;
        ogg_int64_t lastAudioGranule = -1;

        // 跳转到文件末尾向前搜索  
        fseek(infile, 0, SEEK_END);
        long fileSize = ftell(infile);
        long searchPos = fileSize - 65536; // 从最后64KB开始搜索  
        if (searchPos < 0) searchPos = 0;

        fseek(infile, searchPos, SEEK_SET);
        ogg_sync_reset(&oy);

        // 读取剩余数据  
        while (!feof(infile)) {
            char* buffer = ogg_sync_buffer(&oy, 4096);
            int bytes = (int)fread(buffer, 1, 4096, infile);
            if (bytes == 0) break;
            ogg_sync_wrote(&oy, bytes);

            while (ogg_sync_pageout(&oy, &og) > 0) {
                if (hasTheora && ogg_page_serialno(&og) == to.serialno) {
                    ogg_int64_t granule = ogg_page_granulepos(&og);
                    if (granule > lastVideoGranule) {
                        lastVideoGranule = granule;
                    }
                }
                if (hasVorbis && ogg_page_serialno(&og) == vo.serialno) {
                    ogg_int64_t granule = ogg_page_granulepos(&og);
                    if (granule > lastAudioGranule) {
                        lastAudioGranule = granule;
                    }
                }
            }
        }

        // 5. 计算时长  
        if (hasTheora && lastVideoGranule >= 0) {
            // 创建临时解码器用于时间转换<cite repo="xiph/theora" path="include/theora/codec.h" line="475" line="496" />  
            th_dec_ctx* td = th_decode_alloc(&ti, ts);
            if (td) {
                info.videoDuration = th_granule_time(td, lastVideoGranule);
                th_decode_free(td);
            }
        }

        if (hasVorbis && lastAudioGranule >= 0) {
            // Vorbis 时长 = granulepos / 采样率<cite repo="xiph/theora" path="win32/experimental/transcoder/transcoder_example.c" line="596" line="605" />  
            info.audioDuration = (double)lastAudioGranule / vi.rate;
        }

        // 6. 清理并恢复文件位置  
        if (hasTheora) {
            ogg_stream_clear(&to);
            if (ts) th_setup_free(ts);
            th_info_clear(&ti);
            th_comment_clear(&tc);
        }
        if (hasVorbis) {
            ogg_stream_clear(&vo);
            vorbis_info_clear(&vi);
            vorbis_comment_clear(&vc);
        }
        ogg_sync_clear(&oy);

        fseek(infile, originalPos, SEEK_SET);

        return (hasTheora || hasVorbis);
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

    // 修改：音频包处理 - 使用 granulepos 计算精确时间戳  
    void processAudioPacket(ogg_packet& op, std::uint32_t gen) {
        if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
        float** pcm = nullptr; int samples = 0;

        // 使用 granulepos 计算时间基准  
        static double audioTimeBase = 0.0;
        if (op.granulepos >= 0) {
            audioTimeBase = (double)op.granulepos / aFreq;
        }

        while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
            int outCh = aChannels;
            std::size_t floats = (std::size_t)samples * (std::size_t)outCh;
            float* buf = (float*)std::malloc(sizeof(float) * floats);
            if (!buf) { vorbis_synthesis_read(&vd, samples); break; }

            // 音频重采样和声道混合（优化版本）  
            float* dst = buf;
            if (vi.channels == 1) {
                for (int i = 0; i < samples; ++i) {
                    float val = pcm[0][i];
                    if (outCh == 1) *dst++ = val;
                    else { *dst++ = val; *dst++ = val; }
                }
            }
            else {
                for (int i = 0; i < samples; ++i) {
                    float L = pcm[0][i];
                    float R = (vi.channels > 1) ? pcm[1][i] : L;

                    // 多声道混合优化  
                    if (vi.channels > 2) {
                        float mixL = L, mixR = R;
                        float scale = 0.5f / (vi.channels - 1);
                        for (int ch = 2; ch < vi.channels; ++ch) {
                            float v = pcm[ch][i];
                            mixL += v * scale;
                            mixR += v * scale;
                        }
                        L = mixL; R = mixR;
                    }

                    if (outCh == 1) *dst++ = (L + R) * 0.5f;
                    else { *dst++ = L; *dst++ = R; }
                }
            }

            // 使用 granulepos 计算精确时间戳  
            std::uint32_t playms = (std::uint32_t)(audioTimeBase * 1000.0);
            audioTimeBase += (double)samples / aFreq;

            AudioPacket* ap = (AudioPacket*)std::malloc(sizeof(AudioPacket));
            if (!ap) { std::free(buf); vorbis_synthesis_read(&vd, samples); break; }

            ap->seek_generation = gen;
            ap->playms = playms;
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

    // 修改：视频包处理 - 添加 TH_DUPFRAME 处理  
    void processVideoPacket(const ogg_packet& op, std::uint32_t gen) {
        ogg_int64_t gran = -1;
        int ret = th_decode_packetin(td, &op, &gran);

        if (ret == 0) {
            // 正常解码  
            double vtime = th_granule_time(td, gran);
            if (vtime < 0.0) vtime = 0.0;

            th_ycbcr_buffer yuv;
            th_decode_ycbcr_out(td, yuv);

            // 分配像素缓冲区（添加错误检查）  
            std::uint32_t stride = (std::uint32_t)vWidth * 4u;
            std::uint32_t bytes = stride * (std::uint32_t)vHeight;
            std::uint8_t* px = (std::uint8_t*)std::malloc(bytes);
            if (!px) return; // OOM，跳过此帧  

            yuv420ToARGB(yuv, px, stride);

            std::uint32_t playms = (std::uint32_t)std::llround(vtime * 1000.0);
            VideoFrame* vf = (VideoFrame*)std::malloc(sizeof(VideoFrame));
            if (!vf) {
                std::free(px);
                return;
            }

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
        else if (ret == TH_DUPFRAME) {
            // 处理重复帧 - 复制前一帧  
            VideoFrame* prev = getLastVideoFrame();
            if (prev) {
                VideoFrame* vf = duplicateVideoFrame(prev, gen);
                if (vf) {
                    vf->playms = (std::uint32_t)std::llround(th_granule_time(td, gran) * 1000.0);
                    pushVideoFrame(vf);
                }
            }
        }
    }

    // 新增：执行 seek 操作  
    void performSeek(double targetSeconds) {
        if (!hasTheora && !hasVorbis) return;

        // 计算目标 granulepos  
        ogg_int64_t targetGranule = -1;
        if (hasTheora) {
            targetGranule = (ogg_int64_t)(targetSeconds * vFps);
        }
        else if (hasVorbis) {
            targetGranule = (ogg_int64_t)(targetSeconds * aFreq);
        }

        // 文件定位  
        if (!seekToPosition(targetGranule)) return;

        // 重置解码器状态  
        resetDecoderState(targetGranule);

        // 清空输出队列  
        clearQueues();
    }

    // 新增：文件定位到指定 granulepos  
    bool seekToPosition(ogg_int64_t targetGranule) {
        // 获取文件大小  
        fseek(infile, 0, SEEK_END);
        long fileSize = ftell(infile);

        // 简化实现：线性搜索（生产环境建议用二分查找+索引）  
        fseek(infile, 0, SEEK_SET);
        ogg_sync_reset(&oy);

        while (!feof(infile)) {
            if (bufferData() == 0) break;

            while (ogg_sync_pageout(&oy, &og) > 0) {
                ogg_int64_t pageGranule = ogg_page_granulepos(&og);

                // 检查是否为目标流  
                if (hasTheora && ogg_page_serialno(&og) == to.serialno) {
                    if (pageGranule >= targetGranule) {
                        // 找到目标位置，回退到页面开始  
                        long pagePos = ftell(infile) - og.header_len - og.body_len;
                        fseek(infile, pagePos, SEEK_SET);
                        ogg_sync_reset(&oy);
                        return true;
                    }
                }
                else if (hasVorbis && ogg_page_serialno(&og) == vo.serialno) {
                    if (pageGranule >= targetGranule) {
                        long pagePos = ftell(infile) - og.header_len - og.body_len;
                        fseek(infile, pagePos, SEEK_SET);
                        ogg_sync_reset(&oy);
                        return true;
                    }
                }
            }
        }

        return false;
    }

    // 新增：重置解码器状态  
    void resetDecoderState(ogg_int64_t targetGranule) {
        if (hasTheora && td) {
            // 设置 Theora 解码器的 granule 位置<cite repo="xiph/theora" path="include/theora/theoradec.h" line="67" line="78" />  
            th_decode_ctl(td, TH_DECCTL_SET_GRANPOS, &targetGranule, sizeof(targetGranule));
        }

        if (hasVorbis) {
            // Vorbis 需要重新初始化解码状态  
            vorbis_synthesis_restart(&vd);
        }
    }

    // 新增：清空输出队列  
    void clearQueues() {
        {
            std::lock_guard<std::mutex> lock(mtxVideo);
            freeVideoFrameChain(videoHead);
            videoHead = nullptr;
            videoTail = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(mtxAudio);
            freeAudioPacketChain(audioHead);
            audioHead = nullptr;
            audioTail = nullptr;
        }
    }

    // 修改：主解码循环 - 添加 seek 处理  
    void decodeLoop() {
        ogg_packet op{};
        bool alive = true;
        std::uint32_t gen = seekGeneration.load();

        while (!quit.load() && alive) {
            // 检查 seek 请求  
            std::uint32_t currentGen = seekGeneration.load();
            if (currentGen != gen) {
                gen = currentGen;
                // seek 已请求，继续解码新位置数据  
            }

            // 处理 seek 请求  
            if (seekRequested.load()) {
                performSeek(seekTargetSeconds.load());
                seekRequested.store(false);
                gen = seekGeneration.load();
                continue;
            }

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
                    if (hasTheora && ogg_page_serialno(&og) == to.serialno)
                        ogg_stream_pagein(&to, &og);
                    if (hasVorbis && ogg_page_serialno(&og) == vo.serialno)
                        ogg_stream_pagein(&vo, &og);
                }

                if (eof.load()) {
                    if (hasVorbis && ogg_stream_packetout(&vo, &op) > 0) aPkt = true;
                    else if (hasTheora && ogg_stream_packetout(&to, &op) > 0) vPkt = true;
                    if (!aPkt && !vPkt) alive = false;
                }
            }

            // 处理音频包  
            if (aPkt) {
                processAudioPacket(op, gen);
            }

            // 处理视频包  
            if (vPkt) {
                processVideoPacket(op, gen);
            }
        }
    }

    // 辅助函数：获取最后一帧（用于重复帧处理）  
    VideoFrame* getLastVideoFrame() {
        std::lock_guard<std::mutex> lock(mtxVideo);
        VideoFrame* current = videoHead;
        VideoFrame* last = nullptr;
        while (current) {
            last = current;
            current = current->next;
        }
        return last;
    }

    // 辅助函数：复制视频帧  
    VideoFrame* duplicateVideoFrame(VideoFrame* src, std::uint32_t gen) {
        if (!src) return nullptr;

        VideoFrame* vf = (VideoFrame*)std::malloc(sizeof(VideoFrame));
        if (!vf) return nullptr;

        std::uint8_t* px = (std::uint8_t*)std::malloc(src->dataBytes);
        if (!px) {
            std::free(vf);
            return nullptr;
        }

        std::memcpy(px, src->pixels, src->dataBytes);

        vf->seek_generation = gen;
        vf->playms = src->playms;
        vf->fps = src->fps;
        vf->width = src->width;
        vf->height = src->height;
        vf->format = src->format;
        vf->stride = src->stride;
        vf->dataBytes = src->dataBytes;
        vf->pixels = px;
        vf->next = nullptr;

        return vf;
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
    std::atomic<bool> seekRequested{ false };
    std::atomic<double> seekTargetSeconds{ 0.0 };

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