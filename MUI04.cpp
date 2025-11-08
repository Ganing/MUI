

/****************************************************************************
 * 标题: MUI 框架使用示例 - 音乐播放器 UI (使用新的 PlayList 组件)
 * 文件: MUI04.cpp
 * 版本: 0.5
 * 作者: AEGLOVE
 * 日期: 2025-11-03
 * 说明: 使用新设计的 PlayList 组件,支持封面显示和收藏功能
 ****************************************************************************/

#if 0
 // 在包含Windows头文件之前定义NOMINMAX，以避免min和max宏
#define NOMINMAX
#include "mui.h"  
#include <cstdio>  
#include <vector>  
#include <string>  
#include <algorithm>  
#include <memory>  
#include <map>  
#include <conio.h>
#include <io.h>
#include <fcntl.h>

// 小工具：UTF-8  
static std::string ws2utf8(const std::wstring& ws) { return MUI::wideToUtf8(ws); }




// ============================================================================  
// PlayerBinder - 绑定播放器和UI控件  
// ============================================================================  
namespace {
    class PlayerBinder : public MUI::UIElement {
    public:
        MUI::MPlayer* player = nullptr;
        MUI::PlayList* list = nullptr;  // 改为 PlayList*  
        MUI::UILabel* lblTitle = nullptr;
        MUI::UILabel* lblArtist = nullptr;
        std::vector<MUI::Song>* songList = nullptr;
        MUI::UILabel* lblTimeL = nullptr;
        MUI::UILabel* lblTimeR = nullptr;
        MUI::UIButton* btnPrev = nullptr;
        MUI::UIButton* btnPlay = nullptr;
        MUI::UIButton* btnNext = nullptr;
        MUI::UISlider* sldProgress = nullptr;
        MUI::UISlider* sldVolume = nullptr;
        MUI::CoverImage* cover = nullptr;  // 改为 CoverImage* 
        MUI::LyricView* lyricView = nullptr;  // 添加歌词视图  
        MUI::UITextInput* search = nullptr;

        std::vector<MUI::Song> filtered;
        bool autoSeeking = false;

        void wire() {
            if (!player || !list) return;

            list->onSelect = [this](int index) {
                if (index < 0 || index >= (int)filtered.size()) return;
                const auto& song = filtered[index];
                int realIndex = -1;
                const auto& lib = player->getSongLibrary();
                for (size_t i = 0; i < lib.size(); ++i) {
                    if (lib[i].filePath == song.filePath) { realIndex = (int)i; break; }
                }
                if (realIndex >= 0) {
                    player->playSongByIndex(realIndex);
                    updateSongInfo();
                }
                };

            if (btnPrev) btnPrev->onClick = [this]() { if (player) { player->preSong(); updateSongInfo(); } };
            if (btnNext) btnNext->onClick = [this]() { if (player) { player->nextSong(); updateSongInfo(); } };
            if (btnPlay) btnPlay->onClick = [this]() {
                if (!player) return;
                auto st = player->getPlaybackState();
                if (st == MUI::PlaybackState::Playing) player->pauseAudio();
                else if (st == MUI::PlaybackState::Paused) player->resumeAudio();
                else player->playSongByIndex(player->getCurrentSongIndex());
                };

            if (sldProgress) {
                sldProgress->onValueChanged = [this](float v) {
                    if (!player) return;
                    if (autoSeeking) return;
                    float dur = player->getCurrentDuration();
                    if (dur > 0.0f) {
                        float pos = std::clamp(v, 0.0f, 100.0f) / 100.0f * dur;
                        player->seekAudio(pos);
                    }
                    };
            }

            if (sldVolume) {
                sldVolume->onValueChanged = [this](float v) {
                    if (player) player->setVolume(v);
                    };
            }

            if (search) {
                search->onTextChanged = [this](const std::string& q) {
                    applyFilter(q);
                    };
            }

            // 设置歌词时间提供器  
            if (lyricView && player) {
                lyricView->setTimeProvider([this]() {
                    return player->getCurrentPosition();  // 修正:使用 getCurrentPosition()  
                    });
            }
        }

        void applyFilter(const std::string& query) {
            if (!list || !songList) return;  // 添加 songList 检查  

            filtered.clear();
            auto toLower = [](std::wstring s) {
                std::transform(s.begin(), s.end(), s.begin(), ::towlower);
                return s;
                };
            std::wstring wq = MUI::utf8ToWide(query);
            wq = toLower(wq);

            // 从 songList 而不是 player->getSongLibrary() 获取  
            for (const auto& s : *songList) {
                std::wstring title = toLower(s.title);
                std::wstring artist = toLower(s.artist);
                std::wstring album = toLower(s.album);
                if (wq.empty() ||
                    title.find(wq) != std::wstring::npos ||
                    artist.find(wq) != std::wstring::npos ||
                    album.find(wq) != std::wstring::npos) {
                    filtered.push_back(s);
                }
            }
            list->setItems(filtered);
        }

        void updateSongInfo() {
            if (!player || !songList) return;

            int currentIndex = player->getCurrentSongIndex();
            if (currentIndex < 0 || currentIndex >= songList->size()) return;

            const auto& s = (*songList)[currentIndex];

            if (lblTitle) lblTitle->setText(ws2utf8(s.title).c_str());
            if (lblArtist) lblArtist->setText(ws2utf8(s.artist).c_str());

            // 加载大封面  
            if (cover) {
                std::wstring coverPath = L"";
                if (s.embeddedCoverCount > 0 && !s.coverPaths.empty()) {
                    coverPath = s.coverPaths[0];
                }
                cover->setImageFromFile(coverPath);
            }

            // 加载歌词  
            if (lyricView) {
                auto lyrics = MUI::loadLyricsFromFile(s.filePath);
                lyricView->setLyrics(lyrics);
            }
        }

        void update(float dt) override {
            if (!player) return;

            if (player->isPlaybackFinished() && player->getPlaybackState() == MUI::PlaybackState::Playing) {
                player->nextSong();
                updateSongInfo();
            }

            player->updateAudioPosition();

            float dur = player->getCurrentDuration();
            float pos = player->getCurrentPosition();

            if (lblTimeL) {
                lblTimeL->setText(ws2utf8(MUI::formatDuration(pos)).c_str());
            }
            if (lblTimeR) {
                std::wstring right = L"-" + MUI::formatDuration(std::max(0.0f, dur - pos));
                lblTimeR->setText(ws2utf8(right).c_str());
            }

            if (sldProgress && dur > 0.0f) {
                autoSeeking = true;
                float percent = std::clamp(pos / dur, 0.0f, 1.0f) * 100.0f;
                sldProgress->setValue(percent);
                autoSeeking = false;
            }

            if (btnPlay) {
                bool playing = (player->getPlaybackState() == MUI::PlaybackState::Playing);
                btnPlay->fontName = "fuhao.ttf";
                btnPlay->setLabel(playing ? u8"⏸" : u8"▶");
            }
        }

        void onSize(int w, int h) override {
            const float margin = 24.0f;
            float ww = std::max(640, w);
            float hh = std::max(480, h);

            float listW = std::clamp(ww * 0.16f, 180.0f, 260.0f);
            float listX = ww - listW - margin;
            float listY = margin + 8.0f;
            float listH = std::max(200.0f, hh - listY - 120.0f);
            if (list) list->rect = MUI::Rect(listX, listY, listW, listH);

            if (search) search->rect = MUI::Rect(listX, listY + listH + 8.0f, listW, 32.0f);

            float centerRight = listX - margin;
            float centerLeft = margin;
            float centerW = std::max(200.0f, centerRight - centerLeft);

            float coverSize = std::clamp(centerW * 0.42f, 180.0f, 300.0f);
            float coverX = centerLeft;
            float coverY = margin;
            if (cover) cover->rect = MUI::Rect(coverX, coverY, coverSize, coverSize);

            float stackX = coverX;
            float stackW = std::max(260.0f, centerW);
            float y = coverY + coverSize + 12.0f;

            if (sldVolume) {
                float volW = std::min(320.0f, stackW);
                sldVolume->rect = MUI::Rect(stackX, y, volW, 26.0f);
                y += 26.0f + 10.0f;
            }

            float btnW = 56.0f, btnH = 38.0f, gap = 12.0f;
            if (btnPrev) btnPrev->rect = MUI::Rect(stackX, y, btnW, btnH);
            if (btnPlay) btnPlay->rect = MUI::Rect(stackX + (btnW + gap), y, btnW, btnH);
            if (btnNext) btnNext->rect = MUI::Rect(stackX + (btnW + gap) * 2.0f, y, btnW, btnH);
            y += btnH + 12.0f;

            float timeW = 64.0f;
            float progX = stackX + timeW + 8.0f;
            float progW = std::min(std::max(260.0f, stackW - (timeW + 8.0f) * 2.0f), centerRight - progX);
            if (lblTimeL) lblTimeL->rect = MUI::Rect(stackX, y + 4.0f, timeW, 22.0f);
            if (sldProgress) sldProgress->rect = MUI::Rect(progX, y, std::max(200.0f, progW), 28.0f);
            if (lblTimeR) lblTimeR->rect = MUI::Rect(progX + std::max(200.0f, progW) + 8.0f, y + 4.0f, timeW, 22.0f);

            float textX = coverX + coverSize + 16.0f;
            float textW = std::max(180.0f, centerRight - textX);
            if (lblTitle)  lblTitle->rect = MUI::Rect(textX, coverY + 6.0f, textW, 28.0f);
            if (lblArtist) lblArtist->rect = MUI::Rect(textX, coverY + 40.0f, textW, 22.0f);
        }

        void render(tvg::Scene* parent) override {
            (void)parent;
        }
    };
} // namespace


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // 不创建控制台（发布时只保留窗口）
    // 1) 初始化 ThorVG 引擎
    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        ODD(L"ThorVG 引擎初始化失败\n");
        return -1;
    }

    // 2) 创建应用
    MUI::Application app;
    if (!app.init(L"MUI 音乐播放器 (新 PlayList)", 1280, 720, 1)) {
        ODD(L"应用初始化失败\n");
        tvg::Initializer::term();
        return -1;
    }

    // 3) 加载字体资源
    app.loadFontFromResource(IDR_FONT_SIYUAN, "siyuan.ttf");
    app.loadFontFromResource(IDR_FONT_FUHAO, "fuhao.ttf");

    // 4) 初始化播放器
    MUI::MPlayer player;
    if (!player.init()) {
        ODD(L"播放器初始化失败\n");
    }

    // 扫描音乐文件夹并提取封面
    std::wstring musicFolder = L"E:/xmusic";
    std::wstring saveFolder = MUI::getExecutableDirectory();
    std::vector<MUI::Song> songs = MUI::scanMusic(musicFolder, saveFolder);

    // 将扫描结果设置到播放器
    player.setSongLibrary(songs);

    // 5) 构建 UI（保持原有逻辑）
    auto* ui = app.getUIManager();

    auto cover = std::make_unique<MUI::CoverImage>();
    cover->rect = MUI::Rect(20, 80, 360, 360);
    auto coverRaw = cover.get();
    ui->addElement(std::move(cover));

    auto lyricView = std::make_unique<MUI::LyricView>();
    lyricView->rect = MUI::Rect(400, 80, 300, 400);
    lyricView->setFontName("siyuan.ttf");
    lyricView->setStyle(24.0f,
        MUI::Color(255, 255, 255, 255),
        MUI::Color(180, 180, 180, 200));
    auto lyricViewRaw = lyricView.get();
    ui->addElement(std::move(lyricView));

    auto lblTitle = std::make_unique<MUI::UILabel>(u8"未播放");
    lblTitle->rect = MUI::Rect(260, 24, 480, 28);
    lblTitle->fontName = "siyuan.ttf";
    lblTitle->setTextColor(MUI::Color(240, 240, 240));
    auto lblTitleRaw = lblTitle.get();
    ui->addElement(std::move(lblTitle));

    auto lblArtist = std::make_unique<MUI::UILabel>(u8"—");
    lblArtist->rect = MUI::Rect(260, 56, 480, 22);
    lblArtist->fontName = "siyuan.ttf";
    lblArtist->setTextColor(MUI::Color(200, 200, 200));
    auto lblArtistRaw = lblArtist.get();
    ui->addElement(std::move(lblArtist));

    auto playlist = std::make_unique<MUI::PlayList>();
    playlist->rect = MUI::Rect(950, 80, 310, 520);
    playlist->fontName = "siyuan.ttf";
    playlist->setItems(player.getSongLibrary());
    auto playlistRaw = playlist.get();
    ui->addElement(std::move(playlist));

    auto searchBox = std::make_unique<MUI::UITextInput>();
    searchBox->rect = MUI::Rect(740, 520, 320, 32);
    searchBox->fontName = "siyuan.ttf";
    searchBox->setText(u8"");
    auto searchRaw = searchBox.get();
    ui->addElement(std::move(searchBox));

    auto lblTimeL = std::make_unique<MUI::UILabel>(u8"0:00");
    lblTimeL->setTextColor(MUI::Color(210, 210, 210));
    lblTimeL->rect = MUI::Rect(260, 610, 60, 24);
    auto lblTimeLRaw = lblTimeL.get();
    ui->addElement(std::move(lblTimeL));

    auto sldProgress = std::make_unique<MUI::UISlider>();
    sldProgress->rect = MUI::Rect(320, 606, 420, 32);
    sldProgress->setRange(0.0f, 100.0f);
    auto sldProgressRaw = sldProgress.get();
    ui->addElement(std::move(sldProgress));

    auto lblTimeR = std::make_unique<MUI::UILabel>(u8"-0:00");
    lblTimeR->setTextColor(MUI::Color(210, 210, 210));
    lblTimeR->rect = MUI::Rect(745, 610, 70, 24);
    auto lblTimeRRaw = lblTimeR.get();
    ui->addElement(std::move(lblTimeR));

    auto btnPrev = std::make_unique<MUI::UIButton>(u8"⏮");
    btnPrev->fontName = "fuhao.ttf";
    btnPrev->rect = MUI::Rect(260, 560, 48, 36);
    auto btnPrevRaw = btnPrev.get();
    ui->addElement(std::move(btnPrev));

    auto btnPlay = std::make_unique<MUI::UIButton>(u8"▶");
    btnPlay->fontName = "fuhao.ttf";
    btnPlay->rect = MUI::Rect(316, 560, 60, 36);
    auto btnPlayRaw = btnPlay.get();
    ui->addElement(std::move(btnPlay));

    auto btnNext = std::make_unique<MUI::UIButton>(u8"⏭");
    btnNext->fontName = "fuhao.ttf";
    btnNext->rect = MUI::Rect(384, 560, 48, 36);
    auto btnNextRaw = btnNext.get();
    ui->addElement(std::move(btnNext));

    auto sldVolume = std::make_unique<MUI::UISlider>();
    sldVolume->rect = MUI::Rect(24, 260, 220, 26);
    sldVolume->setRange(0.0f, 100.0f);
    sldVolume->setValue((float)player.getVolume());
    auto sldVolumeRaw = sldVolume.get();
    ui->addElement(std::move(sldVolume));

    auto lblVol = std::make_unique<MUI::UILabel>(u8"音量");
    lblVol->setTextColor(MUI::Color(210, 210, 210));
    lblVol->rect = MUI::Rect(840, 560, 40, 20);
    ui->addElement(std::move(lblVol));

    auto binder = std::make_unique<PlayerBinder>();
    binder->player = &player;
    binder->list = playlistRaw;
    binder->lyricView = lyricViewRaw;
    binder->lblTitle = lblTitleRaw;
    binder->lblArtist = lblArtistRaw;
    binder->lblTimeL = lblTimeLRaw;
    binder->lblTimeR = lblTimeRRaw;
    binder->btnPrev = btnPrevRaw;
    binder->btnPlay = btnPlayRaw;
    binder->btnNext = btnNextRaw;
    binder->sldProgress = sldProgressRaw;
    binder->sldVolume = sldVolumeRaw;
    binder->cover = coverRaw;
    binder->search = searchRaw;

    binder->songList = &songs;
    binder->applyFilter("");
    binder->wire();
    ui->addElement(std::move(binder));

    if (!player.getSongLibrary().empty()) {
        player.playSongByIndex(0);
    }

    ODD(L"应用启动成功,进入主循环\n");
    app.run();

    MUI::CoverImage::clearCache();

    tvg::Initializer::term();
    ODD(L"应用正常退出\n");

    return 0;
}


//int main(int argc, char** argv) {
//    // Windows 控制台用 UTF-16 宽字符输出  
//    _setmode(_fileno(stdout), _O_U16TEXT);
//
//
//    // 1) 初始化 ThorVG 引擎  
//    if (tvg::Initializer::init(0) != tvg::Result::Success) {
//        ODD(L"ThorVG 引擎初始化失败\n");
//        return -1;
//    }
//
//    // 2) 创建应用  
//    MUI::Application app;
//    if (!app.init(L"MUI 音乐播放器 (新 PlayList)", 1280, 720, 1)) {
//        ODD(L"应用初始化失败\n");
//        tvg::Initializer::term();
//        return -1;
//    }
//
//    // 3) 加载字体资源  
//    app.loadFontFromResource(IDR_FONT_SIYUAN, "siyuan.ttf");
//    app.loadFontFromResource(IDR_FONT_FUHAO, "fuhao.ttf");
//
//    // 4) 初始化播放器  
//    MUI::MPlayer player;
//    if (!player.init()) {
//        ODD(L"播放器初始化失败\n");
//    }
//
//    // 扫描音乐文件夹并提取封面
//    std::wstring musicFolder = L"E:/xmusic";
//    std::wstring saveFolder = MUI::getExecutableDirectory(); // 或者指定其他保存路径
//
//    std::vector<MUI::Song> songs = MUI::scanMusic(musicFolder, saveFolder);
//
//    // 将扫描结果设置到播放器
//    player.setSongLibrary(songs);
//
//    // 5) 构建 UI  
//    auto* ui = app.getUIManager();
//
//    // 封面  
//    auto cover = std::make_unique<MUI::CoverImage>();
//    cover->rect = MUI::Rect(20, 80, 360, 360);
//    auto coverRaw = cover.get();
//    ui->addElement(std::move(cover));
//
//    // 创建歌词视图  
//    auto lyricView = std::make_unique<MUI::LyricView>();
//    lyricView->rect = MUI::Rect(400, 80, 300, 400);
//    lyricView->setFontName("siyuan.ttf");
//    lyricView->setStyle(24.0f,
//        MUI::Color(255, 255, 255, 255),  // 高亮颜色  
//        MUI::Color(180, 180, 180, 200)); // 普通颜色
//    auto lyricViewRaw = lyricView.get();
//    ui->addElement(std::move(lyricView));
//
//    // 标题  
//    auto lblTitle = std::make_unique<MUI::UILabel>(u8"未播放");
//    lblTitle->rect = MUI::Rect(260, 24, 480, 28);
//    lblTitle->fontName = "siyuan.ttf";
//    lblTitle->setTextColor(MUI::Color(240, 240, 240));
//    auto lblTitleRaw = lblTitle.get();
//    ui->addElement(std::move(lblTitle));
//
//    // 艺术家  
//    auto lblArtist = std::make_unique<MUI::UILabel>(u8"—");
//    lblArtist->rect = MUI::Rect(260, 56, 480, 22);
//    lblArtist->fontName = "siyuan.ttf";
//    lblArtist->setTextColor(MUI::Color(200, 200, 200));
//    auto lblArtistRaw = lblArtist.get();
//    ui->addElement(std::move(lblArtist));
//
//    // *** 使用新的 PlayList 组件 ***  
//    auto playlist = std::make_unique<MUI::PlayList>();
//    playlist->rect = MUI::Rect(950, 80, 310, 520);
//    playlist->fontName = "siyuan.ttf";
//    playlist->setItems(player.getSongLibrary());  // 现在有封面路径了  
//    auto playlistRaw = playlist.get();
//    ui->addElement(std::move(playlist));
//
//    // 搜索框  
//    auto searchBox = std::make_unique<MUI::UITextInput>();
//    searchBox->rect = MUI::Rect(740, 520, 320, 32);
//    searchBox->fontName = "siyuan.ttf";
//    searchBox->setText(u8"");
//    auto searchRaw = searchBox.get();
//    ui->addElement(std::move(searchBox));
//
//    // 时间标签  
//    auto lblTimeL = std::make_unique<MUI::UILabel>(u8"0:00");
//    lblTimeL->setTextColor(MUI::Color(210, 210, 210));
//    lblTimeL->rect = MUI::Rect(260, 610, 60, 24);
//    auto lblTimeLRaw = lblTimeL.get();
//    ui->addElement(std::move(lblTimeL));
//
//    // 进度条  
//    auto sldProgress = std::make_unique<MUI::UISlider>();
//    sldProgress->rect = MUI::Rect(320, 606, 420, 32);
//    sldProgress->setRange(0.0f, 100.0f);
//    auto sldProgressRaw = sldProgress.get();
//    ui->addElement(std::move(sldProgress));
//
//    // 剩余时间  
//    auto lblTimeR = std::make_unique<MUI::UILabel>(u8"-0:00");
//    lblTimeR->setTextColor(MUI::Color(210, 210, 210));
//    lblTimeR->rect = MUI::Rect(745, 610, 70, 24);
//    auto lblTimeRRaw = lblTimeR.get();
//    ui->addElement(std::move(lblTimeR));
//
//    // 播放控制按钮  
//    auto btnPrev = std::make_unique<MUI::UIButton>(u8"⏮");
//    btnPrev->fontName = "fuhao.ttf";
//    btnPrev->rect = MUI::Rect(260, 560, 48, 36);
//    auto btnPrevRaw = btnPrev.get();
//    ui->addElement(std::move(btnPrev));
//
//    auto btnPlay = std::make_unique<MUI::UIButton>(u8"▶");
//    btnPlay->fontName = "fuhao.ttf";
//    btnPlay->rect = MUI::Rect(316, 560, 60, 36);
//    auto btnPlayRaw = btnPlay.get();
//    ui->addElement(std::move(btnPlay));
//
//    auto btnNext = std::make_unique<MUI::UIButton>(u8"⏭");
//    btnNext->fontName = "fuhao.ttf";
//    btnNext->rect = MUI::Rect(384, 560, 48, 36);
//    auto btnNextRaw = btnNext.get();
//    ui->addElement(std::move(btnNext));
//
//    // 音量滑块  
//    auto sldVolume = std::make_unique<MUI::UISlider>();
//    sldVolume->rect = MUI::Rect(24, 260, 220, 26);
//    sldVolume->setRange(0.0f, 100.0f);
//    sldVolume->setValue((float)player.getVolume());
//    auto sldVolumeRaw = sldVolume.get();
//    ui->addElement(std::move(sldVolume));
//
//    // 音量标签  
//    auto lblVol = std::make_unique<MUI::UILabel>(u8"音量");
//    lblVol->setTextColor(MUI::Color(210, 210, 210));
//    lblVol->rect = MUI::Rect(840, 560, 40, 20);
//    ui->addElement(std::move(lblVol));
//
//    // 绑定器 (注意这里使用 PlayList* 而不是 ListPanel*)  
//    auto binder = std::make_unique<PlayerBinder>();
//    binder->player = &player;
//    binder->list = playlistRaw;
//    binder->lyricView = lyricViewRaw;
//    binder->lblTitle = lblTitleRaw;
//    binder->lblArtist = lblArtistRaw;
//    binder->lblTimeL = lblTimeLRaw;
//    binder->lblTimeR = lblTimeRRaw;
//    binder->btnPrev = btnPrevRaw;
//    binder->btnPlay = btnPlayRaw;
//    binder->btnNext = btnNextRaw;
//    binder->sldProgress = sldProgressRaw;
//    binder->sldVolume = sldVolumeRaw;
//    binder->cover = coverRaw;
//    binder->search = searchRaw;
//
//    // 修复: 先赋值 songList  
//    binder->songList = &songs;
//    binder->applyFilter("");
//    binder->wire();
//    ui->addElement(std::move(binder));
//
//    if (!player.getSongLibrary().empty()) {
//        player.playSongByIndex(0);
//    }
//
//    ODD(L"应用启动成功,进入主循环\n");
//    app.run();
//
//    // 清理缓存  
//    MUI::CoverImage::clearCache();
//
//    tvg::Initializer::term();
//    ODD(L"应用正常退出\n");
//    return 0;
//}

#endif // 1
