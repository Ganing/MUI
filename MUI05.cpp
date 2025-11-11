/****************************************************************************
 * 标题: MUI 框架使用示例 - 音乐播放器 UI (圆角+分层半透明方案)
 * 文件: MUI05.cpp
 * 版本: 0.7
 * 作者: AEGLOVE
 * 日期: 2025-11-11
 * 功能: 复刻 MUI04 示例, 使用 WS_EX_LAYERED + 全局 Alpha 实现半透明无边框圆角窗口
 * 依赖: mui.h (ThorVG, OpenGL, TagLib, miniaudio), Win32, Dwmapi(可选)
 * 环境: Windows11 x64, VS2022, C++17, Unicode 字符集
 * 编码: UTF-8 with BOM
 * 说明:
 *   - 由原先 DWM Blur Behind 方案改为分层窗口 (WS_EX_LAYERED + SetLayeredWindowAttributes)。
 *   - 当前方式为整窗统一 Alpha 半透明，并非逐像素真实透明；OpenGL 仍直接渲染到窗口。
 *   - 若需真正“只显示控件”/逐像素透明，应改为: FBO/离屏 -> 读取像素 -> UpdateLayeredWindow。
 *   - 初始播放时同步播放列表选中状态 & 封面/歌词显示（修复之前启动后未选中/未显示封面的问题）。
 *   - 后续可扩展: (1) 动态调节全局 Alpha (2) 加入阴影/窗口拖拽区域细化 (3) 离屏透明管线。
 ****************************************************************************/

#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>

#include "mui.h"  // 不修改此头

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ========== 分层半透明设置（参考 mp_mini.cpp 思路） ==========
static BYTE kLayeredAlpha = 230;                 // 0..255，255不透明
static COLORREF kLayeredTint = RGB(28, 30, 34);  // 背景刷颜色（与UI底色一致）
static HBRUSH g_hWindowBackground = nullptr;     // 类背景刷

// 圆角无边框
static void makeFramelessRounded(HWND hwnd) {
    if (!hwnd) return;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~WS_OVERLAPPEDWINDOW;
    style |= WS_POPUP; // 无标题栏无边框
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    RECT rc{}; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int radius = 14; // 圆角半径
    HRGN rgn = CreateRoundRectRgn(0, 0, w, h, radius, radius);
    SetWindowRgn(hwnd, rgn, TRUE); // 系统接管 rgn
}

// 应用窗口效果: 无边框圆角 + 分层半透明
static void applyWindowEffects(HWND hwnd) {
    if (!hwnd) return;

    // 1) 设置无边框圆角
    makeFramelessRounded(hwnd);

    // 2) 分层窗口 + 整窗 alpha
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetLayeredWindowAttributes(hwnd, 0, kLayeredAlpha, LWA_ALPHA);

    // 3) 设置类背景刷，避免闪烁（匹配 UI 背景色）
    if (!g_hWindowBackground) {
        g_hWindowBackground = CreateSolidBrush(kLayeredTint);
    }
    SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)g_hWindowBackground);

    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);
}

// UTF-8 helper
static std::string ws2utf8(const std::wstring& ws) { return MUI::wideToUtf8(ws); }

// 可拖动区域：在其区域内按下左键，触发窗口拖动
namespace {
    class DragRegion : public MUI::UIElement {
    public:
        HWND hwnd = nullptr;
        explicit DragRegion(HWND h) : hwnd(h) { rect = MUI::Rect(0, 0, 800, 48); }
        void render(tvg::Scene* parent) override { (void)parent; }
        void onMDown(float px, float py) override {
            if (!hwnd) return;
            if (hitTest(px, py)) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
        }
    };
}

// PlayerBinder (复刻自 MUI04, 放入匿名命名空间)
namespace {
    class PlayerBinder : public MUI::UIElement {
    public:
        MUI::MPlayer* player = nullptr;
        MUI::PlayList* list = nullptr;
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
        MUI::CoverImage* cover = nullptr;
        MUI::LyricView* lyricView = nullptr;
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
                    if (!player || autoSeeking) return;
                    float dur = player->getCurrentDuration();
                    if (dur > 0.0f) {
                        float pos = std::clamp(v, 0.0f, 100.0f) / 100.0f * dur;
                        player->seekAudio(pos);
                    }
                };
            }
            if (sldVolume) {
                sldVolume->onValueChanged = [this](float v) { if (player) player->setVolume(v); };
            }
            if (search) {
                search->onTextChanged = [this](const std::string& q) { applyFilter(q); };
            }
            if (lyricView && player) {
                lyricView->setTimeProvider([this]() { return player->getCurrentPosition(); });
            }
        }

        void applyFilter(const std::string& query) {
            if (!list || !songList) return;
            filtered.clear();
            auto toLower = [](std::wstring s) { std::transform(s.begin(), s.end(), s.begin(), ::towlower); return s; };
            std::wstring wq = MUI::utf8ToWide(query);
            wq = toLower(wq);
            for (const auto& s : *songList) {
                std::wstring title = toLower(s.title);
                std::wstring artist = toLower(s.artist);
                std::wstring album = toLower(s.album);
                if (wq.empty() || title.find(wq) != std::wstring::npos || artist.find(wq) != std::wstring::npos || album.find(wq) != std::wstring::npos) {
                    filtered.push_back(s);
                }
            }
            list->setItems(filtered);
        }

        void updateSongInfo() {
            if (!player || !songList) return;
            int currentIndex = player->getCurrentSongIndex();
            if (currentIndex < 0 || currentIndex >= (int)songList->size()) return;
            const auto& s = (*songList)[currentIndex];
            if (lblTitle) lblTitle->setText(ws2utf8(s.title).c_str());
            if (lblArtist) lblArtist->setText(ws2utf8(s.artist).c_str());
            if (cover) {
                std::wstring coverPath;
                if (s.embeddedCoverCount > 0 && !s.coverPaths.empty()) coverPath = s.coverPaths[0];
                cover->setImageFromFile(coverPath);
            }
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
            if (lblTimeL) lblTimeL->setText(ws2utf8(MUI::formatDuration(pos)).c_str());
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
            float centerRight = ww - margin;
            float centerLeft = margin;
            float centerW = std::max(200.0f, centerRight - centerLeft);
            float coverSize = std::clamp(centerW * 0.42f, 180.0f, 300.0f);
            float coverX = centerLeft;
            float coverY = margin;
            if (cover) cover->rect = MUI::Rect(coverX, coverY, coverSize, coverSize);
            float stackX = coverX;
            float stackW = std::max(260.0f, centerW);
            float y = coverY + coverSize + 12.0f;
            if (sldVolume) { float volW = std::min(320.0f, stackW); sldVolume->rect = MUI::Rect(stackX, y, volW, 26.0f); y += 36.0f; }
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
            if (lblTitle) lblTitle->rect = MUI::Rect(textX, coverY + 6.0f, textW, 28.0f);
            if (lblArtist) lblArtist->rect = MUI::Rect(textX, coverY + 40.0f, textW, 22.0f);
        }

        void render(tvg::Scene* parent) override { (void)parent; }
    };
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
//int main(int argc, char** argv) {
    //(void)argc; (void)argv;
    _setmode(_fileno(stdout), _O_U16TEXT);

    if (tvg::Initializer::init(0) != tvg::Result::Success) {
        ODD(L"ThorVG 引擎初始化失败\n");
        return -1;
    }

    MUI::Application app;
    if (!app.init(L"MUI 音乐播放器 (方案1圆角透明测试)", 1280, 720, 1)) {
        ODD(L"应用初始化失败\n");
        tvg::Initializer::term();
        return -1;
    }

    // 应用窗口效果（分层半透明 + 圆角）
    applyWindowEffects(app.getHwnd());

    app.loadFontFromResource(IDR_FONT_SIYUAN, "siyuan.ttf");
    app.loadFontFromResource(IDR_FONT_FUHAO, "fuhao.ttf");

    MUI::MPlayer player;
    if (!player.init()) {
        ODD(L"播放器初始化失败\n");
    }

    std::wstring musicFolder = L"E:/xmusic"; // 根据需要调整
    std::wstring saveFolder = MUI::getExecutableDirectory();
    std::vector<MUI::Song> songs = MUI::scanMusic(musicFolder, saveFolder);
    player.setSongLibrary(songs);

    auto* ui = app.getUIManager();

    // 顶部可拖动区域（高度 48px），避免遮挡控件可按需调整宽/高。
    auto drag = std::make_unique<DragRegion>(app.getHwnd());
    drag->rect = MUI::Rect(0, 0, 1280, 48);
    ui->addElement(std::move(drag));

    auto cover = std::make_unique<MUI::CoverImage>();
    cover->rect = MUI::Rect(20, 80, 360, 360);
    auto coverRaw = cover.get();
    ui->addElement(std::move(cover));

    auto lyricView = std::make_unique<MUI::LyricView>();
    lyricView->rect = MUI::Rect(400, 80, 300, 400);
    lyricView->setFontName("siyuan.ttf");
    lyricView->setStyle(24.0f, MUI::Color(255,255,255,255), MUI::Color(180,180,180,200));
    auto lyricViewRaw = lyricView.get();
    ui->addElement(std::move(lyricView));

    auto lblTitle = std::make_unique<MUI::UILabel>(u8"未播放");
    lblTitle->rect = MUI::Rect(260, 24, 480, 28); lblTitle->fontName = "siyuan.ttf"; lblTitle->setTextColor(MUI::Color(240,240,240));
    auto lblTitleRaw = lblTitle.get(); ui->addElement(std::move(lblTitle));

    auto lblArtist = std::make_unique<MUI::UILabel>(u8"—");
    lblArtist->rect = MUI::Rect(260, 56, 480, 22); lblArtist->fontName = "siyuan.ttf"; lblArtist->setTextColor(MUI::Color(200,200,200));
    auto lblArtistRaw = lblArtist.get(); ui->addElement(std::move(lblArtist));

    auto playlist = std::make_unique<MUI::PlayList>();
    playlist->rect = MUI::Rect(950, 80, 310, 520); playlist->fontName = "siyuan.ttf"; playlist->setItems(player.getSongLibrary());
    auto playlistRaw = playlist.get(); ui->addElement(std::move(playlist));

    auto searchBox = std::make_unique<MUI::UITextInput>();
    searchBox->rect = MUI::Rect(740, 520, 320, 32); searchBox->fontName = "siyuan.ttf"; searchBox->setText(u8"");
    auto searchRaw = searchBox.get(); ui->addElement(std::move(searchBox));

    auto lblTimeL = std::make_unique<MUI::UILabel>(u8"0:00");
    lblTimeL->setTextColor(MUI::Color(210,210,210)); lblTimeL->rect = MUI::Rect(260, 610, 60, 24);
    auto lblTimeLRaw = lblTimeL.get(); ui->addElement(std::move(lblTimeL));

    auto sldProgress = std::make_unique<MUI::UISlider>();
    sldProgress->rect = MUI::Rect(320, 606, 420, 32); sldProgress->setRange(0.0f, 100.0f);
    auto sldProgressRaw = sldProgress.get(); ui->addElement(std::move(sldProgress));

    auto lblTimeR = std::make_unique<MUI::UILabel>(u8"-0:00");
    lblTimeR->setTextColor(MUI::Color(210,210,210)); lblTimeR->rect = MUI::Rect(745, 610, 70, 24);
    auto lblTimeRRaw = lblTimeR.get(); ui->addElement(std::move(lblTimeR));

    auto btnPrev = std::make_unique<MUI::UIButton>(u8"⏮");
    btnPrev->fontName = "fuhao.ttf"; btnPrev->rect = MUI::Rect(260, 560, 48, 36);
    auto btnPrevRaw = btnPrev.get(); ui->addElement(std::move(btnPrev));

    auto btnPlay = std::make_unique<MUI::UIButton>(u8"▶");
    btnPlay->fontName = "fuhao.ttf"; btnPlay->rect = MUI::Rect(316, 560, 60, 36);
    auto btnPlayRaw = btnPlay.get(); ui->addElement(std::move(btnPlay));

    auto btnNext = std::make_unique<MUI::UIButton>(u8"⏭");
    btnNext->fontName = "fuhao.ttf"; btnNext->rect = MUI::Rect(384, 560, 48, 36);
    auto btnNextRaw = btnNext.get(); ui->addElement(std::move(btnNext));

    auto sldVolume = std::make_unique<MUI::UISlider>();
    sldVolume->rect = MUI::Rect(24, 260, 220, 26); sldVolume->setRange(0.0f, 100.0f); sldVolume->setValue((float)player.getVolume());
    auto sldVolumeRaw = sldVolume.get(); ui->addElement(std::move(sldVolume));

    auto lblVol = std::make_unique<MUI::UILabel>(u8"音量");
    lblVol->setTextColor(MUI::Color(210,210,210)); lblVol->rect = MUI::Rect(840, 560, 40, 20);
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
    auto binderRaw = binder.get();
    ui->addElement(std::move(binder));

    // 初始播放列表选中第一个歌，并更新信息和封面显示
    if (!player.getSongLibrary().empty()) {
        player.playSongByIndex(0);
        if (playlistRaw) playlistRaw->setSelectedIndex(0); // 播放列表选中第一项
        if (binderRaw) binderRaw->updateSongInfo();        // 更新封面/歌词/标题
    }

    ODD(L"应用启动成功,进入主循环\n");
    app.run();

    MUI::CoverImage::clearCache();
    tvg::Initializer::term();
    ODD(L"应用正常退出\n");
    // 释放背景刷
    if (g_hWindowBackground) { DeleteObject(g_hWindowBackground); g_hWindowBackground = nullptr; }
    return 0;
}
