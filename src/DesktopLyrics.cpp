#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "LrcParser.hpp"
#include "MediaSession.hpp"
#include "LyricLibrary.hpp"
#include "LyricSearch.hpp"
#include "SongIdentify.hpp"
#include "PlayerWindows.hpp"
#include "BilibiliProgress.hpp"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "WindowsApp.lib")
#pragma comment(lib, "runtimeobject.lib")

namespace {

constexpr wchar_t kWndClass[] = L"DesktopLyricsOverlayClass";
constexpr wchar_t kCtrlClass[] = L"DesktopLyricsControlClass";
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT_PTR kCtrlTimerId = 2;
constexpr UINT kHotkeyPrev = 1;
constexpr UINT kHotkeyPlay = 2;
constexpr UINT kHotkeyNext = 3;
constexpr UINT kHotkeyLock = 4;
constexpr UINT kHotkeyOpen = 5;
constexpr UINT kHotkeyFloat = 6;
constexpr UINT kHotkeyPanel = 7;

constexpr float kLimbusCharInterval = 0.100f;
constexpr float kLimbusFade = 0.28f;
constexpr float kLimbusOverlap = 0.45f;
constexpr float kLimbusMinLife = 4.8f;
constexpr float kLimbusMaxLife = 6.5f;
constexpr int kLimbusMaxLines = 5;
constexpr float kLimbusShake = 2.4f;
constexpr float kLimbusShakeFollow = 0.22f;
constexpr ULONGLONG kLimbusShakeMs = 90;
constexpr float kLimbusCharSpace = 5.0f;
constexpr float kLimbusMargin = 35.0f;
constexpr COLORREF kLimbusShadow = RGB(216, 165, 35);

constexpr int ID_TRAY_OPEN = 1001;
constexpr int ID_TRAY_PLAY = 1002;
constexpr int ID_TRAY_LOCK = 1003;
constexpr int ID_TRAY_FONT = 1004;
constexpr int ID_TRAY_COLOR = 1005;
constexpr int ID_TRAY_EXIT = 1006;
constexpr int ID_TRAY_RESET = 1007;
constexpr int ID_TRAY_FOLLOW = 1008;
constexpr int ID_TRAY_FOLDER = 1009;
constexpr int ID_TRAY_ONLINE = 1010;
constexpr int ID_TRAY_SEARCH = 1011;
constexpr int ID_TRAY_RESYNC = 1012;
constexpr int ID_TRAY_FLOAT = 1013;
constexpr int ID_TRAY_PANEL = 1014;

constexpr int CID_NONE = 0;
constexpr int CID_CLOSE = 1;
constexpr int CID_MIN = 2;
constexpr int CID_PREV = 3;
constexpr int CID_PLAY = 4;
constexpr int CID_NEXT = 5;
constexpr int CID_FLOAT = 6;
constexpr int CID_LOCK = 7;
constexpr int CID_FOLLOW = 8;
constexpr int CID_ONLINE = 9;
constexpr int CID_FONT_MINUS = 10;
constexpr int CID_FONT_PLUS = 11;
constexpr int CID_COLOR = 12;
constexpr int CID_RESET = 13;
constexpr int CID_OPEN = 14;
constexpr int CID_SEARCH = 15;
constexpr int CID_RESYNC = 16;
constexpr int CID_FOLDER = 17;
constexpr int CID_EXIT = 18;
constexpr int CID_SEEK = 19;

constexpr int kCtrlW = 392;
constexpr int kCtrlH = 752;
constexpr COLORREF kCtrlAccent = RGB(232, 236, 244);

enum class MatchState { Idle, Searching, Found, NotFound };

struct CtrlLayout {
    RECT close{};
    RECT min{};
    RECT seek{};
    RECT prev{};
    RECT play{};
    RECT next{};
    RECT floatBtn{};
    RECT lockBtn{};
    RECT followBtn{};
    RECT onlineBtn{};
    RECT fontMinus{};
    RECT fontPlus{};
    RECT color{};
    RECT reset{};
    RECT open{};
    RECT search{};
    RECT resync{};
    RECT folder{};
    RECT exit{};
};

struct AppState {
    HWND hwnd = nullptr;
    HWND hwndCtrl = nullptr;
    HINSTANCE inst = nullptr;
    NOTIFYICONDATA nid{};
    ULONG_PTR gdiplusToken = 0;
    LrcSong song;
    MediaMonitor media;
    BiliProgressMonitor bili;
    LyricSearcher searcher;
    SongIdentifier identifier;
    MediaNowPlaying now{};
    std::wstring lastAudioIdentifyKey;
    std::wstring identifyVideoKey;
    std::wstring lastIdentifyTitle;
    std::wstring lastIdentifyArtist;
    std::wstring lastSearchKey;
    bool vocalClock = false;
    double vocalOffset = 0.0;
    double vocalRate = 1.0;
    double lastVideoPos = -1.0;
    double lastVideoDuration = 0.0;
    ULONGLONG lastVideoPosMs = 0;
    double lastRawVideoPos = -1.0;
    double videoClockPos = 0.0;
    ULONGLONG videoClockMs = 0;
    ULONGLONG lastTitleRefreshMs = 0;
    ULONGLONG lastVocalAlignMs = 0;
    double pendingVocalSongPos = -1.0;
    ULONGLONG pendingVocalAtMs = 0;
    bool followSystem = true;
    bool onlineSearch = true;
    bool playing = false;
    bool locked = false;
    bool dragging = false;
    bool lrcMatched = false;
    MatchState matchState = MatchState::Idle;
    std::wstring lyricSource;
    POINT dragOffset{};
    bool seekingBar = false;
    double playhead = 0.0;
    double mediaDuration = 0.0;
    double lastSyncedPos = -1.0;
    ULONGLONG lastTick = 0;
    ULONGLONG lastManualSeekMs = 0;
    double lastManualSeekTarget = -1.0;
    int currentIndex = -1;
    float fontSize = 42.0f;
    COLORREF lyricColor = RGB(255, 236, 160);
    COLORREF shadowColor = RGB(0, 0, 0);
    int windowW = 1100;
    int windowH = 220;
    int compactX = 0;
    int compactY = 0;
    int compactW = 1100;
    int compactH = 220;
    bool compactSaved = false;
    bool floatMode = false;
    bool ctrlVisible = true;
    bool ctrlDragging = false;
    bool ctrlSeeking = false;
    POINT ctrlDragOffset{};
    int ctrlHot = CID_NONE;
    CtrlLayout ctrlLay{};
    std::wstring lastTrackKey;
    struct FloatLineVisual {
        int index = -1;
        float x = 0;
        float y = 0;
        float angle = 0;
        float size = 42;
        float boxW = 220;
        float boxH = 60;
        std::vector<float> widths;
        std::vector<float> shakeX;
        std::vector<float> shakeY;
        std::vector<float> targetX;
        std::vector<float> targetY;
        ULONGLONG lastShakeMs = 0;
    };
    std::vector<FloatLineVisual> floatVis;
    std::wstring floatVisKey;
    HDC overlayMem = nullptr;
    HBITMAP overlayBmp = nullptr;
    HGDIOBJ overlayOld = nullptr;
    void* overlayBits = nullptr;
    int overlayW = 0;
    int overlayH = 0;
};

AppState g;

std::wstring LyricsFolder() {
    return ExeDirectory() + L"lyrics\\";
}

void EnsureLyricsFolder() {
    std::wstring dir = LyricsFolder();
    CreateDirectoryW(dir.c_str(), nullptr);
}

std::vector<std::wstring> SearchFolders() {
    std::vector<std::wstring> folders;
    folders.push_back(LyricsFolder());
    folders.push_back(ExeDirectory());
    std::wstring kugouLyric = GetKugouLyricDirectory();
    if (!kugouLyric.empty()) folders.push_back(kugouLyric);
    for (const auto& extra : MusicPlayerLyricFolders()) folders.push_back(extra);
    std::wstring music = MusicDirectory();
    if (!music.empty()) folders.push_back(music);
    if (!g.song.filePath.empty()) {
        std::filesystem::path p(g.song.filePath);
        folders.push_back(p.parent_path().wstring());
    }
    return folders;
}

void ShowBalloon(const wchar_t* title, const wchar_t* text) {
    g.nid.uFlags = NIF_INFO | NIF_ICON | NIF_MESSAGE | NIF_TIP;
    wcsncpy_s(g.nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(g.nid.szInfo, text, _TRUNCATE);
    g.nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
    g.nid.szInfo[0] = 0;
    g.nid.szInfoTitle[0] = 0;
}

void UpdateTrayTip() {
    std::wstring tip = L"桌面歌词";
    if (g.now.available) {
        tip += L"\n";
        tip += g.now.playing ? L"正在播放" : L"已暂停";
        if (g.lrcMatched && !g.song.title.empty()) {
            tip += L": ";
            tip += g.song.title;
        }
        if (!g.now.appName.empty()) {
            tip += L"\n来源: ";
            tip += g.now.appName;
        }
    } else if (!g.song.title.empty()) {
        tip += L" - ";
        tip += g.song.title;
        tip += g.playing ? L"  [本地计时]" : L"  [暂停]";
    } else {
        tip += L"\n未检测到正在播放的歌曲";
    }
    if (g.floatMode) tip += L"\n[全屏漂浮]";
    if (g.locked) tip += L"\n[锁定]";
    wcsncpy_s(g.nid.szTip, tip.c_str(), _TRUNCATE);
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void SetClickThrough(bool enable) {
    LONG_PTR ex = GetWindowLongPtrW(g.hwnd, GWL_EXSTYLE);
    if (enable) ex |= WS_EX_TRANSPARENT;
    else ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(g.hwnd, GWL_EXSTYLE, ex);
}

std::wstring IdleMainText() {
    if (g.lrcMatched && !g.song.title.empty()) return g.song.title;
    if (g.now.available) {
        if (g.matchState == MatchState::Searching || g.searcher.IsSearching() ||
            g.now.waitingKugouIdentify || g.identifier.IsSearching()) {
            return L"正在检索歌词";
        }
        return L"正在播放";
    }
    if (!g.song.title.empty()) return g.song.title;
    if (g.followSystem) return L"未检测到正在播放的歌曲";
    return L"拖入 LRC 文件，或右键托盘打开歌词";
}

std::wstring IdleSubText() {
    if (g.now.available) {
        std::wstring s = g.now.artist;
        if (!g.now.appName.empty()) {
            if (!s.empty()) s += L"  ·  ";
            s += g.now.appName;
        }
        if ((g.now.waitingKugouIdentify || g.identifier.IsSearching()) && !g.lrcMatched) {
            if (!s.empty()) s += L"  ·  ";
            s += L"正在识别背景音乐";
        } else if (g.matchState == MatchState::Searching || g.searcher.IsSearching()) {
            if (!s.empty()) s += L"  ·  ";
            s += L"正在检索歌词...";
        } else if (!g.lrcMatched) {
            if (!s.empty()) s += L"  ·  ";
            s += g.onlineSearch ? L"未检索到可用歌词" : L"未找到歌词，可打开在线检索";
        }
        return s;
    }
    if (g.followSystem) return L"用网易云 / QQ 音乐 / 酷狗 / Spotify / 浏览器播放即可同步";
    return g.song.artist;
}

std::wstring DisplayLine(int index) {
    if (index < 0 || index >= static_cast<int>(g.song.lines.size())) {
        return IdleMainText();
    }
    const auto& t = g.song.lines[index].text;
    if (t.empty()) return L"♪";
    return t;
}

std::wstring NeighborLine(int index) {
    if (index < 0 || index + 1 >= static_cast<int>(g.song.lines.size())) {
        return IdleSubText();
    }
    return g.song.lines[index + 1].text;
}

double EffectiveDuration() {
    if (g.mediaDuration > 1.0) return g.mediaDuration;
    return SongDuration(g.song);
}

bool UsingVideoTimeline() {
    return g.followSystem &&
           ((g.now.available && IsBrowserOrVideoApp(g.now.appName, g.now.appId) &&
             !IsMusicPlayerApp(g.now.appName, g.now.appId)) ||
            !g.identifyVideoKey.empty());
}

double LyricTime() {
    return g.playhead + g.song.offsetMs / 1000.0 +
           (UsingVideoTimeline() && !g.now.fromBiliBar ? 0.35 : 0.0);
}

float LineProgress() {
    if (g.currentIndex < 0 || g.song.lines.empty()) return 0.0f;
    double start = g.song.lines[g.currentIndex].timeSec;
    double end = start + 4.0;
    if (g.currentIndex + 1 < static_cast<int>(g.song.lines.size())) {
        end = g.song.lines[g.currentIndex + 1].timeSec;
    }
    if (end <= start) return 1.0f;
    double t = LyricTime();
    float p = static_cast<float>((t - start) / (end - start));
    return std::clamp(p, 0.0f, 1.0f);
}

double DisplayPosition() {
    if (UsingVideoTimeline()) {
        if (g.vocalClock) return std::max(0.0, g.playhead - g.vocalOffset);
        return std::max(0.0, g.playhead);
    }
    return g.playhead;
}

double DisplayDuration() {
    if (UsingVideoTimeline()) {
        if (g.mediaDuration > 1.0) return g.mediaDuration;
        if (g.lastVideoDuration > 1.0) return g.lastVideoDuration;
    }
    return EffectiveDuration();
}

Gdiplus::Color MakeColor(COLORREF c, BYTE a) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

float Hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (x & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

float Mix01(int a, int b) {
    return Hash01(static_cast<uint32_t>(a * 73856093u) ^ static_cast<uint32_t>(b * 19349663u));
}

float EaseInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void DrawOutlinedText(Gdiplus::Graphics& gp,
                      const std::wstring& text,
                      const Gdiplus::FontFamily& family,
                      Gdiplus::REAL size,
                      const Gdiplus::RectF& layout,
                      const Gdiplus::StringFormat& fmt,
                      COLORREF fill,
                      BYTE alpha,
                      float outlineWidth = 6.0f) {
    if (text.empty() || alpha == 0) return;
    Gdiplus::GraphicsPath path;
    path.AddString(text.c_str(), -1, &family, Gdiplus::FontStyleBold, size, layout, &fmt);

    const float ow = std::max(2.0f, outlineWidth);
    Gdiplus::Pen outline(MakeColor(g.shadowColor, static_cast<BYTE>(std::min(255, static_cast<int>(alpha) + 40))), ow);
    outline.SetLineJoin(Gdiplus::LineJoinRound);
    gp.DrawPath(&outline, &path);

    Gdiplus::SolidBrush brush(MakeColor(fill, alpha));
    gp.FillPath(&brush, &path);
}

float MeasureGlyph(Gdiplus::Graphics& gp, const Gdiplus::FontFamily& family, float size, const std::wstring& ch) {
    static float cacheSize = 0;
    static float cache[128]{};
    const wchar_t c = ch.empty() ? L' ' : ch[0];
    if (std::abs(cacheSize - size) > 0.4f) {
        cacheSize = size;
        for (float& v : cache) v = 0;
    }
    if (c < 128 && cache[c] > 0.1f) return cache[c];
    Gdiplus::Font font(&family, size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat typo(Gdiplus::StringFormat::GenericTypographic());
    typo.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::RectF bb;
    gp.MeasureString(ch.c_str(), 1, &font, Gdiplus::PointF(0, 0), &typo, &bb);
    const float w = std::max(size * 0.28f, bb.Width);
    if (c < 128) cache[c] = w;
    return w;
}

void DrawOffsetGlyph(Gdiplus::Graphics& gp,
                     const std::wstring& ch,
                     const Gdiplus::FontFamily& family,
                     float size,
                     float x,
                     float y,
                     float angle,
                     COLORREF fill,
                     BYTE alpha,
                     float pop,
                     bool solid = false) {
    if (ch.empty() || alpha == 0) return;
    Gdiplus::GraphicsState st = gp.Save();
    gp.TranslateTransform(x, y);
    gp.RotateTransform(angle);
    gp.ScaleTransform(pop, pop);

    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
    Gdiplus::RectF layout(0, -size, size * 4.0f, size * 2.0f);

    Gdiplus::GraphicsPath path;
    path.AddString(ch.c_str(), -1, &family, Gdiplus::FontStyleBold, size, layout, &fmt);

    if (!solid) {
        const float shift = std::max(3.0f, size * 0.08f);
        Gdiplus::SolidBrush shadowBrush(MakeColor(g.shadowColor, alpha));
        gp.TranslateTransform(shift, shift);
        gp.FillPath(&shadowBrush, &path);
        gp.TranslateTransform(-shift, -shift);

        Gdiplus::Pen outline(MakeColor(g.shadowColor, alpha), std::max(2.0f, size * 0.06f));
        outline.SetLineJoin(Gdiplus::LineJoinRound);
        gp.DrawPath(&outline, &path);
    }

    Gdiplus::SolidBrush brush(MakeColor(fill, alpha));
    gp.FillPath(&brush, &path);
    gp.Restore(st);
}

void DrawLimbusGlyph(Gdiplus::Graphics& gp,
                     const std::wstring& ch,
                     const Gdiplus::FontFamily& family,
                     float size,
                     float x,
                     float y,
                     BYTE alpha) {
    if (ch.empty() || alpha == 0) return;
    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
    Gdiplus::RectF layout(0, 0, size * 4.0f, size * 2.0f);
    Gdiplus::GraphicsPath path;
    path.AddString(ch.c_str(), -1, &family, Gdiplus::FontStyleBold, size, layout, &fmt);

    Gdiplus::GraphicsState st = gp.Save();
    gp.TranslateTransform(x + 3.0f, y + 3.0f);
    Gdiplus::SolidBrush shadow(MakeColor(RGB(255, 255, 255), alpha));
    gp.FillPath(&shadow, &path);
    gp.Restore(st);

    st = gp.Save();
    gp.TranslateTransform(x, y);
    Gdiplus::SolidBrush fill(MakeColor(g.lyricColor, alpha));
    gp.FillPath(&fill, &path);
    gp.Restore(st);
}

AppState::FloatLineVisual& EnsureFloatVisual(int index, int w, int h, float size, float boxW, float boxH) {
    for (auto& v : g.floatVis) {
        if (v.index == index) return v;
    }
    AppState::FloatLineVisual v;
    v.index = index;
    v.size = size;
    v.boxW = boxW;
    v.boxH = boxH;
    v.angle = Mix01(index, 19) * 16.0f - 8.0f;
    const float minX = kLimbusMargin;
    const float minY = kLimbusMargin;
    const float maxX = std::max(minX, static_cast<float>(w) - boxW - kLimbusMargin);
    const float maxY = std::max(minY, static_cast<float>(h) - boxH - kLimbusMargin);
    float bestX = minX, bestY = minY, bestScore = -1e9f;
    for (int tryN = 0; tryN < 80; ++tryN) {
        const float x = minX + Mix01(index, 11 + tryN * 3) * (maxX - minX);
        const float y = minY + Mix01(index, 29 + tryN * 7) * (maxY - minY);
        float score = 0.0f;
        for (const auto& other : g.floatVis) {
            const float ox = other.x - 30.0f;
            const float oy = other.y - 30.0f;
            const float ow = other.boxW + 60.0f;
            const float oh = other.boxH + 60.0f;
            const float ix = std::max(0.0f, std::min(x + boxW, ox + ow) - std::max(x, ox));
            const float iy = std::max(0.0f, std::min(y + boxH, oy + oh) - std::max(y, oy));
            score -= ix * iy;
        }
        if (score > -1.0f) {
            bestX = x;
            bestY = y;
            break;
        }
        if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
        }
    }
    v.x = bestX;
    v.y = bestY;
    g.floatVis.push_back(v);
    return g.floatVis.back();
}

void ClampLineOnScreen(AppState::FloatLineVisual& vis, float total, float size, int w, int h) {
    const float pad = kLimbusMargin + kLimbusShake + 8.0f;
    auto measure = [&](float ang, float& bw, float& bh) {
        const float rad = ang * 3.14159265f / 180.0f;
        bw = std::abs(total * std::cos(rad)) + size * std::abs(std::sin(rad)) + 18.0f;
        bh = std::abs(total * std::sin(rad)) + size * std::abs(std::cos(rad)) + 18.0f;
    };
    float bw = 0, bh = 0;
    measure(vis.angle, bw, bh);
    const float maxW = std::max(48.0f, static_cast<float>(w) - 2.0f * pad);
    const float maxH = std::max(48.0f, static_cast<float>(h) - 2.0f * pad);
    for (int i = 0; i < 8 && (bw > maxW || bh > maxH); ++i) {
        vis.angle *= 0.62f;
        measure(vis.angle, bw, bh);
    }
    vis.boxW = bw;
    vis.boxH = bh;

    const float rad = vis.angle * 3.14159265f / 180.0f;
    const float dx = std::cos(rad);
    const float dy = std::sin(rad);
    float minX = vis.x, minY = vis.y, maxX = vis.x + size, maxY = vis.y + size;
    const float endX = vis.x + total * dx;
    const float endY = vis.y + total * dy;
    minX = std::min(minX, endX);
    minY = std::min(minY, endY);
    maxX = std::max(maxX, endX + size);
    maxY = std::max(maxY, endY + size);
    float ox = 0, oy = 0;
    if (minX < pad) ox = pad - minX;
    if (minY < pad) oy = pad - minY;
    if (maxX + ox > w - pad) ox -= (maxX + ox) - (w - pad);
    if (maxY + oy > h - pad) oy -= (maxY + oy) - (h - pad);
    vis.x += ox;
    vis.y += oy;
}

void DrawPopLine(Gdiplus::Graphics& gp,
                 const LyricLine& line,
                 const Gdiplus::FontFamily& family,
                 float size,
                 float originX,
                 float originY,
                 float angle,
                 COLORREF fill,
                 BYTE alpha,
                 double t,
                 Gdiplus::StringAlignment align,
                 bool solid = false) {
    if (line.words.empty() || alpha == 0) return;
    float total = 0;
    std::vector<float> widths;
    widths.reserve(line.words.size());
    for (const auto& w : line.words) {
        float cw = MeasureGlyph(gp, family, size, w.text);
        widths.push_back(cw);
        total += cw;
    }
    float x = originX;
    if (align == Gdiplus::StringAlignmentCenter) x -= total * 0.5f;
    else if (align == Gdiplus::StringAlignmentFar) x -= total;
    const float rad = angle * 3.14159265f / 180.0f;
    const float dx = std::cos(rad);
    const float dy = std::sin(rad);
    float cursor = 0;
    for (size_t i = 0; i < line.words.size(); ++i) {
        const auto& w = line.words[i];
        if (t + 0.01 < w.timeSec) {
            cursor += widths[i];
            continue;
        }
        const float px = x + cursor * dx;
        const float py = originY + cursor * dy;
        DrawOffsetGlyph(gp, w.text, family, size, px, py, 0.0f, fill, alpha, 1.0f, solid);
        cursor += widths[i];
    }
}

void DrawFloatLyrics(Gdiplus::Graphics& gp, int w, int h, const Gdiplus::FontFamily& family) {
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);

    if (g.song.lines.empty()) {
        Gdiplus::SolidBrush mainBr(MakeColor(RGB(245, 245, 245), 220));
        Gdiplus::SolidBrush subBr(MakeColor(RGB(220, 228, 240), 170));
        Gdiplus::Font mainFont(&family, std::max(28.0f, g.fontSize * 0.9f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font subFont(&family, 22.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::RectF box(w * 0.12f, h * 0.38f, w * 0.76f, h * 0.24f);
        Gdiplus::RectF sub(w * 0.18f, h * 0.58f, w * 0.64f, h * 0.12f);
        gp.DrawString(IdleMainText().c_str(), -1, &mainFont, box, &fmt, &mainBr);
        gp.DrawString(IdleSubText().c_str(), -1, &subFont, sub, &fmt, &subBr);
        return;
    }

    const std::wstring visKey = g.lastTrackKey + L"|" + g.song.filePath;
    if (visKey != g.floatVisKey) {
        g.floatVis.clear();
        g.floatVisKey = visKey;
    }

    const double t = LyricTime();
    const int n = static_cast<int>(g.song.lines.size());
    const float size = std::clamp(g.fontSize * 0.95f, 28.0f, 62.0f);

    auto lineLife = [&](int i, double& start, double& end) {
        start = g.song.lines[i].timeSec - 0.2;
        if (i + 1 < n) {
            const double desired = g.song.lines[i + 1].timeSec + kLimbusOverlap;
            end = std::clamp(desired, start + kLimbusMinLife, start + kLimbusMaxLife);
        } else {
            end = start + kLimbusMaxLife;
        }
    };

    std::vector<int> visible;
    for (int i = 0; i < n; ++i) {
        double start = 0, end = 0;
        lineLife(i, start, end);
        if (t >= start && t <= end + kLimbusFade) visible.push_back(i);
        if (g.song.lines[i].timeSec > t + 0.4) break;
    }
    if (static_cast<int>(visible.size()) > kLimbusMaxLines) {
        visible.erase(visible.begin(), visible.end() - kLimbusMaxLines);
    }

    g.floatVis.erase(std::remove_if(g.floatVis.begin(), g.floatVis.end(), [&](const AppState::FloatLineVisual& v) {
        return std::find(visible.begin(), visible.end(), v.index) == visible.end();
    }), g.floatVis.end());

    const ULONGLONG nowMs = GetTickCount64();
    for (int i : visible) {
        const auto& line = g.song.lines[i];
        double start = 0, end = 0;
        lineLife(i, start, end);
        float opacity = 1.0f;
        if (t >= end) {
            opacity = std::clamp(1.0f - static_cast<float>((t - end) / kLimbusFade), 0.0f, 1.0f);
        }
        if (opacity <= 0.01f) continue;

        auto& vis = EnsureFloatVisual(i, w, h, size, 220.0f, size * 1.8f + 16.0f);
        if (vis.widths.size() != line.words.size() || std::abs(vis.size - size) > 0.4f) {
            vis.widths.resize(line.words.size());
            float total = 0;
            for (size_t k = 0; k < line.words.size(); ++k) {
                vis.widths[k] = MeasureGlyph(gp, family, size, line.words[k].text);
                total += vis.widths[k] + kLimbusCharSpace;
            }
            vis.size = size;
            vis.boxW = std::max(80.0f, total + 24.0f);
            vis.boxH = size * 1.8f + 16.0f;
        }
        {
            float total = 0;
            for (float cw : vis.widths) total += cw + kLimbusCharSpace;
            ClampLineOnScreen(vis, total, size, w, h);
        }
        if (vis.shakeX.size() != line.words.size()) {
            vis.shakeX.assign(line.words.size(), 0.0f);
            vis.shakeY.assign(line.words.size(), 0.0f);
            vis.targetX.assign(line.words.size(), 0.0f);
            vis.targetY.assign(line.words.size(), 0.0f);
        }
        if (nowMs - vis.lastShakeMs >= kLimbusShakeMs) {
            vis.lastShakeMs = nowMs;
            const int tick = static_cast<int>(nowMs / kLimbusShakeMs);
            for (size_t k = 0; k < line.words.size(); ++k) {
                vis.targetX[k] = Mix01(i + tick * 17, static_cast<int>(k) + 3) * (kLimbusShake * 2.0f) - kLimbusShake;
                vis.targetY[k] = Mix01(i + tick * 23, static_cast<int>(k) + 9) * (kLimbusShake * 2.0f) - kLimbusShake;
            }
        }
        for (size_t k = 0; k < line.words.size(); ++k) {
            vis.shakeX[k] += (vis.targetX[k] - vis.shakeX[k]) * kLimbusShakeFollow;
            vis.shakeY[k] += (vis.targetY[k] - vis.shakeY[k]) * kLimbusShakeFollow;
        }

        const float rad = vis.angle * 3.14159265f / 180.0f;
        const float dx = std::cos(rad);
        const float dy = std::sin(rad);
        float cursor = 0;
        int shown = 0;
        for (size_t k = 0; k < line.words.size(); ++k) {
            const auto& word = line.words[k];
            const double appear = start + static_cast<double>(shown) * kLimbusCharInterval;
            if (!word.text.empty() && !iswspace(word.text[0])) ++shown;
            const float age = static_cast<float>(t - appear);
            if (age < -0.02f) {
                cursor += vis.widths[k] + kLimbusCharSpace;
                continue;
            }
            const float appearK = EaseInOut(std::clamp(age / 0.26f, 0.0f, 1.0f));
            const BYTE glyphA = static_cast<BYTE>(std::clamp(opacity * appearK * 255.0f, 0.0f, 255.0f));
            const float px = vis.x + cursor * dx + vis.shakeX[k];
            const float py = vis.y + cursor * dy + vis.shakeY[k];
            DrawLimbusGlyph(gp, word.text, family, size, px, py, glyphA);
            cursor += vis.widths[k] + kLimbusCharSpace;
        }
    }
}

void ProgressBarRect(int w, int h, float& x, float& y, float& barW, float& barH);
bool HitProgressBar(POINT pt, int w, int h, double& ratioOut);
void DrawProgressPill(Gdiplus::Graphics& gp, const RECT& r, float progress, COLORREF accent);
void PaintControl();
void ShowTrayMenu();
void ResetPosition();
void ToggleControlPanel();
void ShowControlPanel();
void HideControlPanel();
void TogglePlay();
void ToggleLock();
void ToggleFollow();
void ToggleOnline();
void ToggleFloatMode();
void PrevLine();
void NextLine();
void SeekTo(double sec);
void OpenLrcDialog();
void OpenLyricsFolder();
void ManualSearch();
void ChooseColorDialog();
void CreateControlPanel(HINSTANCE inst);

void EnsureOverlayBuffer(HDC screen, int w, int h) {
    if (g.overlayMem && g.overlayW == w && g.overlayH == h && g.overlayBmp) return;
    if (g.overlayMem) {
        SelectObject(g.overlayMem, g.overlayOld);
        DeleteObject(g.overlayBmp);
        DeleteDC(g.overlayMem);
        g.overlayMem = nullptr;
        g.overlayBmp = nullptr;
        g.overlayOld = nullptr;
        g.overlayBits = nullptr;
    }
    g.overlayMem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g.overlayBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &g.overlayBits, nullptr, 0);
    g.overlayOld = SelectObject(g.overlayMem, g.overlayBmp);
    g.overlayW = w;
    g.overlayH = h;
}

void PaintOverlay() {
    RECT rc{};
    GetClientRect(g.hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC screen = GetDC(g.hwnd);
    EnsureOverlayBuffer(screen, w, h);
    HDC mem = g.overlayMem;

    {
        Gdiplus::Graphics gp(mem);
        gp.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gp.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        gp.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        gp.Clear(Gdiplus::Color(0, 0, 0, 0));

        Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
        Gdiplus::FontFamily segoe(L"Segoe UI");
        const Gdiplus::FontFamily& useFamily =
            (yahei.GetLastStatus() == Gdiplus::Ok) ? yahei : segoe;

        if (g.floatMode) {
            DrawFloatLyrics(gp, w, h, useFamily);
        } else {
            Gdiplus::GraphicsPath panel;
            Gdiplus::RectF panelRect(16.0f, 14.0f, static_cast<Gdiplus::REAL>(w - 32), static_cast<Gdiplus::REAL>(h - 28));
            const float r = 26.0f;
            panel.AddArc(panelRect.X, panelRect.Y, r, r, 180, 90);
            panel.AddArc(panelRect.GetRight() - r, panelRect.Y, r, r, 270, 90);
            panel.AddArc(panelRect.GetRight() - r, panelRect.GetBottom() - r, r, r, 0, 90);
            panel.AddArc(panelRect.X, panelRect.GetBottom() - r, r, r, 90, 90);
            panel.CloseFigure();

            Gdiplus::SolidBrush glass(Gdiplus::Color(g.locked ? 22 : 48, 10, 12, 20));
            gp.FillPath(&glass, &panel);
            Gdiplus::Pen border(Gdiplus::Color(g.locked ? 28 : 58, 255, 255, 255), 1.0f);
            gp.DrawPath(&border, &panel);

            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

            const double t = LyricTime();
            if (g.currentIndex >= 0 && g.currentIndex < static_cast<int>(g.song.lines.size()) &&
                !g.song.lines[g.currentIndex].words.empty()) {
                DrawPopLine(gp, g.song.lines[g.currentIndex], useFamily, g.fontSize,
                            static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.38f, 0.0f,
                            g.lyricColor, 255, t, Gdiplus::StringAlignmentCenter);
                if (g.currentIndex + 1 < static_cast<int>(g.song.lines.size()) &&
                    !g.song.lines[g.currentIndex + 1].words.empty()) {
                    DrawPopLine(gp, g.song.lines[g.currentIndex + 1], useFamily, g.fontSize * 0.52f,
                                static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.62f, 0.0f,
                                RGB(220, 228, 240), 150, t, Gdiplus::StringAlignmentCenter);
                }
            } else {
                std::wstring current = DisplayLine(g.currentIndex);
                std::wstring next = NeighborLine(g.currentIndex);
                Gdiplus::RectF mainRect(40.0f, 28.0f, static_cast<Gdiplus::REAL>(w - 80), static_cast<Gdiplus::REAL>(h * 0.52f));
                DrawOutlinedText(gp, current, useFamily, g.fontSize, mainRect, fmt, RGB(245, 245, 245), 210);
                if (!next.empty()) {
                    Gdiplus::RectF nextRect(60.0f, h * 0.52f, static_cast<Gdiplus::REAL>(w - 120), static_cast<Gdiplus::REAL>(h * 0.28f));
                    DrawOutlinedText(gp, next, useFamily, g.fontSize * 0.55f, nextRect, fmt, RGB(220, 228, 240), 170);
                }
            }

            float progress = 0.0f;
            double duration = DisplayDuration();
            if (duration > 0.1) progress = static_cast<float>(std::clamp(DisplayPosition() / duration, 0.0, 1.0));
            float barX, barY, barW, barH;
            ProgressBarRect(w, h, barX, barY, barW, barH);
            RECT pill{
                static_cast<LONG>(barX),
                static_cast<LONG>(barY + 4.0f),
                static_cast<LONG>(barX + barW),
                static_cast<LONG>(barY + 12.0f)
            };
            DrawProgressPill(gp, pill, progress, g.lyricColor);

            std::wstring status = g.playing ? L"播放中" : L"已暂停";
            if (g.followSystem) {
                if ((g.now.waitingKugouIdentify || g.identifier.IsSearching()) && !g.lrcMatched) {
                    status += L"  ·  正在识别背景音乐";
                } else if (g.now.fromBiliBar) {
                    status += L"  ·  B站进度条";
                } else if (g.now.fromKugouIdentify) {
                    status += L"  ·  听歌识曲";
                } else if (g.now.fromKugou) {
                    status += g.now.positionReliable ? L"  ·  酷狗" : L"  ·  酷狗  点进度条对齐";
                } else {
                    status += g.now.available ? L"  ·  跟随播放器" : L"  ·  等待播放";
                }
            } else {
                status += L"  ·  本地计时";
            }
            if (g.matchState == MatchState::Searching || g.searcher.IsSearching()) status += L"  ·  检索中";
            else if (g.lrcMatched && !g.lyricSource.empty()) {
                status += L"  ·  ";
                status += g.lyricSource;
            }
            if (g.locked) status += L"  ·  锁定";
            Gdiplus::RectF st(60.0f, static_cast<float>(h - 76), static_cast<Gdiplus::REAL>(w - 120), 22.0f);
            Gdiplus::StringFormat left;
            left.SetAlignment(Gdiplus::StringAlignmentNear);
            left.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            DrawOutlinedText(gp, status, useFamily, 13.0f, st, left, RGB(210, 216, 228), 185);
        }
    }

    POINT ptSrc{0, 0};
    SIZE sizeWnd{w, h};
    POINT ptDst{};
    RECT wr{};
    GetWindowRect(g.hwnd, &wr);
    ptDst.x = wr.left;
    ptDst.y = wr.top;
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g.hwnd, screen, &ptDst, &sizeWnd, mem, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(g.hwnd, screen);
}

bool SameTrack(const LrcSong& song, const MediaNowPlaying& np) {
    if (!np.available || (song.title.empty() && song.filePath.empty())) return false;
    int byFile = song.filePath.empty() ? 0
        : ScoreLyricName(LyricStemForScore(std::filesystem::path(song.filePath)), np.title, np.artist);
    int byTag = 0;
    if (!song.title.empty()) byTag = ScoreLyricName(song.title, np.title, np.artist);
    return std::max(byFile, byTag) >= 60;
}

void ApplyVocalAlign() {
    if (g.pendingVocalSongPos < 0.0) return;
    const ULONGLONG nowMs = GetTickCount64();
    double songNow = g.pendingVocalSongPos;
    if (g.pendingVocalAtMs && nowMs > g.pendingVocalAtMs) {
        songNow += (nowMs - g.pendingVocalAtMs) / 1000.0;
    }
    const double videoNow = std::max(0.0, g.now.positionSec);
    const bool video = IsBrowserOrVideoApp(g.now.appName, g.now.appId) &&
                       !IsMusicPlayerApp(g.now.appName, g.now.appId);
    const double delta = songNow - videoNow;
    // 卡拉OK视频歌词按画面走。识曲偏移会把歌词整段拖快或拖慢。
    if (video) {
        g.vocalOffset = 0.0;
        g.vocalClock = false;
        if (!(videoNow < 1.2 && g.playhead > 3.0)) {
            g.playhead = videoNow;
        }
    } else if (delta > -12.0 && delta < 8.0) {
        g.vocalOffset = delta;
        g.vocalClock = true;
        g.playhead = std::max(0.0, videoNow + g.vocalOffset);
    } else {
        g.vocalOffset = 0.0;
        g.vocalClock = false;
        g.playhead = videoNow;
    }
    g.lastSyncedPos = g.playhead;
    g.currentIndex = FindLyricIndex(g.song, g.playhead);
    g.lastVocalAlignMs = nowMs;
    g.pendingVocalSongPos = -1.0;
}

void ApplySong(LrcSong song, bool matched, const std::wstring& source = L"本地") {
    g.song = std::move(song);
    g.lrcMatched = matched;
    g.matchState = matched ? MatchState::Found : MatchState::Idle;
    g.lyricSource = matched ? source : L"";
    g.currentIndex = FindLyricIndex(g.song, g.playhead);
    if (matched) ApplyVocalAlign();
    UpdateTrayTip();
    PaintOverlay();
    PaintControl();
}

bool ApplySongNameFromVideoTitle(MediaNowPlaying& np, bool force) {
    const std::wstring sources[] = {
        ProbeBrowserPageTitle(np.sessionTitle.empty() ? np.title : np.sessionTitle),
        np.sessionTitle,
        np.title
    };
    std::wstring song, singer;
    for (const auto& src : sources) {
        if (src.empty()) continue;
        std::wstring cleaned = src;
        StripBrowserChromeNoise(cleaned);
        if (cleaned.empty() || LooksLikeChannelHandle(cleaned) || LooksLikeBrowserChromeTitle(cleaned)) continue;
        if (ExtractOriginalSongName(src, song, singer) && !song.empty() && !LooksLikeChannelHandle(song)) break;
        song.clear();
        singer.clear();
    }
    if (song.empty()) return false;
    if (!force && !PreferOriginalTitle(song, np.title) && !np.title.empty()) return false;
    np.title = song;
    if (!singer.empty()) np.artist = singer;
    return true;
}

void PreferPageOriginalName(MediaNowPlaying& np) {
    const bool video = IsBrowserOrVideoApp(np.appName, np.appId) &&
                       !IsMusicPlayerApp(np.appName, np.appId);
    ApplySongNameFromVideoTitle(np, video);
}

void RequestOnlineSearch(const MediaNowPlaying& np) {
    if (!g.onlineSearch || !np.available) return;
    std::wstring raw = ProbeBrowserPageTitle(np.sessionTitle.empty() ? np.title : np.sessionTitle);
    if (raw.empty()) raw = np.sessionTitle.empty() ? np.title : np.sessionTitle;
    StripBrowserChromeNoise(raw);
    if (raw.empty() || LooksLikeBrowserChromeTitle(raw) || LooksLikeChannelHandle(raw)) {
        raw = np.title;
        StripBrowserChromeNoise(raw);
    }
    if (raw.empty() || LooksLikeBrowserChromeTitle(raw)) return;
    const auto keys = CollectTitleSearchKeys(raw);
    std::wstring query = keys.empty() ? StripFeatSuffix(raw) : keys.front();
    if (query.empty()) return;
    std::wstring key;
    for (const auto& k : keys) key += NormalizeKey(k) + L"|";
    if (key.empty()) key = NormalizeKey(query);
    if (key == g.lastSearchKey && (g.searcher.IsSearching() || g.lrcMatched)) return;
    g.lastSearchKey = key;
    g.matchState = MatchState::Searching;
    g.lrcMatched = false;
    g.lyricSource.clear();
    const bool video = IsBrowserOrVideoApp(np.appName, np.appId) &&
                       !IsMusicPlayerApp(np.appName, np.appId);
    g.searcher.Request(raw, PrimaryArtist(np.artist), video ? 0.0 : np.durationSec);
    PaintOverlay();
    PaintControl();
}

void TryMatchCurrentTrack(const MediaNowPlaying& np, bool notify) {
    if (!np.available || np.title.empty()) return;
    if (LooksLikeBrowserChromeTitle(np.title)) return;
    if (SameTrack(g.song, np) && !g.song.lines.empty()) {
        g.lrcMatched = true;
        g.matchState = MatchState::Found;
        if (g.lyricSource.empty()) g.lyricSource = L"本地";
        ApplyVocalAlign();
        return;
    }

    LrcSong found;
    if (FindMatchingLrc(np.title, np.artist, SearchFolders(), found)) {
        std::wstring source = L"本地";
        auto ext = std::filesystem::path(found.filePath).extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        if (ext == L".krc") source = L"酷狗歌词";
        ApplySong(std::move(found), true, source);
        if (notify) {
            std::wstring msg = g.song.title.empty() ? np.title : g.song.title;
            ShowBalloon(L"已匹配本地歌词", msg.c_str());
        }
        return;
    }

    g.lrcMatched = false;
    g.lyricSource.clear();
    if (!g.song.lines.empty() && !SameTrack(g.song, np)) {
        g.song = {};
        g.currentIndex = -1;
    }
    RequestOnlineSearch(np);
}

void HandleOnlineResult() {
    LyricSearchResult r = g.searcher.Consume();
    if (!g.now.available) return;
    if (!r.ok || r.lrcUtf8.empty()) {
        g.matchState = MatchState::NotFound;
        g.lrcMatched = false;
        g.lyricSource.clear();
        PaintOverlay();
        PaintControl();
        UpdateTrayTip();
        return;
    }

    LrcSong song;
    const std::wstring expect = g.lastIdentifyTitle.empty() ? g.now.title : g.lastIdentifyTitle;
    if (!AcceptLrc(r.lrcUtf8, expect, g.now.artist, song)) {
        g.matchState = MatchState::NotFound;
        g.lrcMatched = false;
        g.lyricSource.clear();
        PaintOverlay();
        PaintControl();
        UpdateTrayTip();
        return;
    }

    EnsureLyricsFolder();
    std::wstring name = SanitizeFilename(
        song.artist.empty() ? song.title : (song.artist + L" - " + song.title));
    std::wstring path = LyricsFolder() + name + L".lrc";
    if (SaveUtf8File(path, r.lrcUtf8)) song.filePath = path;

    ApplySong(std::move(song), true, r.source.empty() ? L"在线" : r.source);
    std::wstring msg = g.now.title;
    if (!r.source.empty()) {
        msg += L"  ·  ";
        msg += r.source;
    }
    ShowBalloon(L"已检索到歌词", msg.c_str());
}

void RequestAudioIdentify(const std::wstring& key) {
    if (key.empty() || key == g.lastAudioIdentifyKey) return;
    if (g.identifier.IsSearching()) return;
    g.lastAudioIdentifyKey = key;
    g.now.waitingKugouIdentify = true;
    g.matchState = MatchState::Searching;
    g.identifier.Request(key);
}

void HandleIdentifyResult() {
    SongIdentifyResult r = g.identifier.Consume();
    if (!g.now.available) return;
    g.now.waitingKugouIdentify = false;
    const bool video = IsBrowserOrVideoApp(g.now.appName, g.now.appId) &&
                       !IsMusicPlayerApp(g.now.appName, g.now.appId);
    if (!video) return;

    MediaNowPlaying named = g.now;
    ApplySongNameFromVideoTitle(named, true);
    const bool pageOk = !named.title.empty() &&
                        !LooksLikeBrowserChromeTitle(named.title) &&
                        !LooksLikeChannelHandle(named.title);

    if (!r.ok || r.title.empty()) {
        if (pageOk) {
            g.now.title = named.title;
            if (!named.artist.empty()) g.now.artist = named.artist;
            g.lastIdentifyTitle = g.now.title;
            g.lastIdentifyArtist = g.now.artist;
            g.lastSearchKey.clear();
            TryMatchCurrentTrack(g.now, true);
        } else {
            g.matchState = MatchState::NotFound;
        }
        PaintOverlay();
        PaintControl();
        UpdateTrayTip();
        return;
    }

    if (pageOk && CountCjk(named.title) >= 2) {
        g.now.title = StripFeatSuffix(named.title);
        if (!named.artist.empty()) g.now.artist = PrimaryArtist(named.artist);
        else if (!r.artist.empty()) g.now.artist = PrimaryArtist(r.artist);
    } else {
        g.now.title = StripFeatSuffix(r.title);
        if (!r.artist.empty()) g.now.artist = PrimaryArtist(r.artist);
    }
    g.now.fromKugouIdentify = true;
    g.lastIdentifyTitle = g.now.title;
    g.lastIdentifyArtist = g.now.artist;
    g.lastSearchKey.clear();
    TryMatchCurrentTrack(g.now, true);
    PaintOverlay();
    PaintControl();
    UpdateTrayTip();
}

void ProgressBarRect(int w, int h, float& x, float& y, float& barW, float& barH) {
    x = 60.0f;
    y = static_cast<float>(h - 56);
    barW = static_cast<float>(w - 120);
    barH = 16.0f;
}

bool HitProgressBar(POINT pt, int w, int h, double& ratioOut) {
    float x, y, barW, barH;
    ProgressBarRect(w, h, x, y, barW, barH);
    if (pt.x < x || pt.x > x + barW || pt.y < y || pt.y > y + barH) return false;
    ratioOut = std::clamp((pt.x - x) / barW, 0.0f, 1.0f);
    return true;
}

void SeekTo(double sec) {
    sec = std::max(0.0, sec);
    const ULONGLONG now = GetTickCount64();
    if (UsingVideoTimeline()) {
        g.lastVideoPos = sec;
        g.lastVideoPosMs = now;
        if (g.vocalClock) g.playhead = std::max(0.0, sec + g.vocalOffset);
        else g.playhead = sec;
    } else {
        g.playhead = sec;
        if (g.vocalClock) g.vocalOffset = g.playhead - g.now.positionSec;
    }
    g.videoClockPos = g.playhead;
    g.videoClockMs = now;
    g.lastRawVideoPos = -1.0;
    g.lastSyncedPos = g.playhead;
    g.lastManualSeekMs = now;
    g.lastManualSeekTarget = sec;
    g.currentIndex = FindLyricIndex(g.song, g.playhead);
    g.media.OverridePosition(UsingVideoTimeline() ? sec : g.playhead);
    PaintOverlay();
    PaintControl();
}

void TogglePlay() {
    if (g.followSystem && g.now.available) {
        g.media.RequestToggle();
        g.playing = !g.playing;
        g.lastTick = GetTickCount64();
        PaintOverlay();
        PaintControl();
        return;
    }
    g.playing = !g.playing;
    g.lastTick = GetTickCount64();
    UpdateTrayTip();
    PaintOverlay();
    PaintControl();
}

void NextLine() {
    if (g.followSystem && UsingVideoTimeline() && g.now.available) {
        g.media.RequestSkipNext();
        return;
    }
    if (g.song.lines.empty()) return;
    int i = std::min(g.currentIndex + 1, static_cast<int>(g.song.lines.size()) - 1);
    if (i < 0) i = 0;
    SeekTo(g.song.lines[i].timeSec);
}

void PrevLine() {
    if (g.followSystem && UsingVideoTimeline() && g.now.available) {
        g.media.RequestSkipPrevious();
        return;
    }
    if (g.song.lines.empty()) return;
    int i = std::max(g.currentIndex - 1, 0);
    SeekTo(g.song.lines[i].timeSec);
}

void ApplyWindowClickThrough() {
    SetClickThrough(g.locked || g.floatMode);
}

void EnterFloatMode() {
    if (g.floatMode) return;
    RECT wr{};
    GetWindowRect(g.hwnd, &wr);
    g.compactX = wr.left;
    g.compactY = wr.top;
    g.compactW = wr.right - wr.left;
    g.compactH = wr.bottom - wr.top;
    g.compactSaved = true;
    g.floatMode = true;

    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    g.windowW = sw;
    g.windowH = sh;
    SetWindowPos(g.hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
    ApplyWindowClickThrough();
    UpdateTrayTip();
    PaintOverlay();
    PaintControl();
}

void LeaveFloatMode() {
    if (!g.floatMode) return;
    g.floatMode = false;
    int x = g.compactX;
    int y = g.compactY;
    int w = g.compactW > 200 ? g.compactW : 1100;
    int h = g.compactH > 120 ? g.compactH : 220;
    if (!g.compactSaved) {
        const int sw = GetSystemMetrics(SM_CXSCREEN);
        const int sh = GetSystemMetrics(SM_CYSCREEN);
        w = 1100;
        h = 220;
        x = (sw - w) / 2;
        y = sh - h - 80;
    }
    g.windowW = w;
    g.windowH = h;
    SetWindowPos(g.hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    ApplyWindowClickThrough();
    UpdateTrayTip();
    PaintOverlay();
    PaintControl();
}

void ToggleFloatMode() {
    if (g.floatMode) LeaveFloatMode();
    else EnterFloatMode();
}

void ToggleLock() {
    g.locked = !g.locked;
    ApplyWindowClickThrough();
    UpdateTrayTip();
    PaintOverlay();
    PaintControl();
}

void ToggleFollow() {
    g.followSystem = !g.followSystem;
    g.lastTrackKey.clear();
    g.lastTick = GetTickCount64();
    UpdateTrayTip();
    PaintOverlay();
    PaintControl();
}

void ToggleOnline() {
    g.onlineSearch = !g.onlineSearch;
    UpdateTrayTip();
    PaintOverlay();
    PaintControl();
}

void ManualSearch() {
    if (!g.now.available) {
        ShowBalloon(L"桌面歌词", L"还没有检测到正在播放的歌曲");
        return;
    }
    g.song = {};
    g.lrcMatched = false;
    g.currentIndex = -1;
    g.lastSearchKey.clear();
    ApplySongNameFromVideoTitle(g.now, true);
    const bool video = IsBrowserOrVideoApp(g.now.appName, g.now.appId) &&
                       !IsMusicPlayerApp(g.now.appName, g.now.appId);
    if (video && (g.now.title.empty() || LooksLikeBrowserChromeTitle(g.now.title) ||
                  LooksLikeChannelHandle(g.now.title))) {
        g.lastAudioIdentifyKey.clear();
        RequestAudioIdentify(g.now.sessionTitle + L"|" + g.now.appId + L"|manual");
        PaintOverlay();
        PaintControl();
        return;
    }
    RequestOnlineSearch(g.now);
}

bool OpenLrcPath(const std::wstring& path) {
    LrcSong song;
    if (!LoadLrcFile(path, song)) {
        MessageBoxW(g.hwnd, L"无法读取歌词文件。请确认是 UTF-8 / ANSI 的 .lrc 文件。", L"桌面歌词", MB_ICONWARNING);
        return false;
    }
    g.playhead = g.followSystem ? g.playhead : 0.0;
    const bool matched = SameTrack(song, g.now);
    ApplySong(std::move(song), matched);
    if (!g.followSystem) {
        g.playing = true;
        g.lastTick = GetTickCount64();
    }
    std::wstring msg = g.song.title.empty() ? path : g.song.title;
    ShowBalloon(L"已加载歌词", msg.c_str());
    return true;
}

void OpenLrcDialog() {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"LRC 歌词 (*.lrc)\0*.lrc\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"打开歌词文件";
    if (GetOpenFileNameW(&ofn)) OpenLrcPath(file);
}

void OpenLyricsFolder() {
    EnsureLyricsFolder();
    ShellExecuteW(nullptr, L"open", LyricsFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ChooseColorDialog() {
    CHOOSECOLORW cc{};
    static COLORREF custom[16]{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g.hwnd;
    cc.lpCustColors = custom;
    cc.rgbResult = g.lyricColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        g.lyricColor = cc.rgbResult;
        PaintOverlay();
        PaintControl();
    }
}

RECT MakeRect(int x, int y, int w, int h) {
    return RECT{x, y, x + w, y + h};
}

bool PtIn(const RECT& r, POINT pt) {
    return pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom;
}

void LayoutControl(int w, int h) {
    auto& L = g.ctrlLay;
    const int pad = 28;
    const int inner = w - pad * 2;
    const int col = (inner - 8) / 2;
    L.close = MakeRect(w - 44, 14, 26, 26);
    L.min = MakeRect(w - 74, 14, 26, 26);
    L.seek = MakeRect(pad, 176, inner, 10);
    const int playSize = 58;
    const int skipSize = 42;
    const int playX = (w - playSize) / 2;
    const int playY = 228;
    L.play = MakeRect(playX, playY, playSize, playSize);
    L.prev = MakeRect(playX - 28 - skipSize, playY + (playSize - skipSize) / 2, skipSize, skipSize);
    L.next = MakeRect(playX + playSize + 28, playY + (playSize - skipSize) / 2, skipSize, skipSize);
    L.floatBtn = MakeRect(pad, 304, inner, 40);
    L.lockBtn = MakeRect(pad, 368, inner, 36);
    L.followBtn = MakeRect(pad, 408, inner, 36);
    L.onlineBtn = MakeRect(pad, 448, inner, 36);
    L.fontMinus = MakeRect(w - 132, 492, 44, 32);
    L.fontPlus = MakeRect(w - 72, 492, 44, 32);
    L.color = MakeRect(pad, 548, col, 36);
    L.reset = MakeRect(pad + col + 8, 548, col, 36);
    L.open = MakeRect(pad, 592, col, 36);
    L.search = MakeRect(pad + col + 8, 592, col, 36);
    L.resync = MakeRect(pad, 636, col, 36);
    L.folder = MakeRect(pad + col + 8, 636, col, 36);
    L.exit = MakeRect(pad, 688, inner, 32);
}

int HitControl(POINT pt) {
    const auto& L = g.ctrlLay;
    if (PtIn(L.close, pt)) return CID_CLOSE;
    if (PtIn(L.min, pt)) return CID_MIN;
    if (PtIn(L.seek, pt)) return CID_SEEK;
    if (PtIn(L.prev, pt)) return CID_PREV;
    if (PtIn(L.play, pt)) return CID_PLAY;
    if (PtIn(L.next, pt)) return CID_NEXT;
    if (PtIn(L.floatBtn, pt)) return CID_FLOAT;
    if (PtIn(L.lockBtn, pt)) return CID_LOCK;
    if (PtIn(L.followBtn, pt)) return CID_FOLLOW;
    if (PtIn(L.onlineBtn, pt)) return CID_ONLINE;
    if (PtIn(L.fontMinus, pt)) return CID_FONT_MINUS;
    if (PtIn(L.fontPlus, pt)) return CID_FONT_PLUS;
    if (PtIn(L.color, pt)) return CID_COLOR;
    if (PtIn(L.reset, pt)) return CID_RESET;
    if (PtIn(L.open, pt)) return CID_OPEN;
    if (PtIn(L.search, pt)) return CID_SEARCH;
    if (PtIn(L.resync, pt)) return CID_RESYNC;
    if (PtIn(L.folder, pt)) return CID_FOLDER;
    if (PtIn(L.exit, pt)) return CID_EXIT;
    return CID_NONE;
}

void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF rc, float radius) {
    const float rad = std::min(radius, std::min(rc.Width, rc.Height) * 0.5f);
    if (rad <= 0.5f) {
        path.AddRectangle(rc);
        return;
    }
    path.AddArc(rc.X, rc.Y, rad, rad, 180, 90);
    path.AddArc(rc.GetRight() - rad, rc.Y, rad, rad, 270, 90);
    path.AddArc(rc.GetRight() - rad, rc.GetBottom() - rad, rad, rad, 0, 90);
    path.AddArc(rc.X, rc.GetBottom() - rad, rad, rad, 90, 90);
    path.CloseFigure();
}

void FillRound(Gdiplus::Graphics& gp, const RECT& r, float radius, Gdiplus::Color c) {
    Gdiplus::GraphicsPath path;
    Gdiplus::RectF rc(static_cast<float>(r.left), static_cast<float>(r.top),
                      static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top));
    AddRoundRect(path, rc, radius);
    Gdiplus::SolidBrush br(c);
    gp.FillPath(&br, &path);
}

void StrokeRound(Gdiplus::Graphics& gp, const RECT& r, float radius, Gdiplus::Color c, float width = 1.0f) {
    Gdiplus::GraphicsPath path;
    Gdiplus::RectF rc(static_cast<float>(r.left), static_cast<float>(r.top),
                      static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top));
    AddRoundRect(path, rc, radius);
    Gdiplus::Pen pen(c, width);
    gp.DrawPath(&pen, &path);
}

void FillRoundF(Gdiplus::Graphics& gp, Gdiplus::RectF rc, float radius, Gdiplus::Color c) {
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, rc, radius);
    Gdiplus::SolidBrush br(c);
    gp.FillPath(&br, &path);
}

void DrawProgressPill(Gdiplus::Graphics& gp, const RECT& r, float progress, COLORREF accent) {
    Gdiplus::RectF track(static_cast<float>(r.left), static_cast<float>(r.top),
                         static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top));
    FillRoundF(gp, track, track.Height, Gdiplus::Color(38, 255, 255, 255));
    const float fillW = track.Width * std::clamp(progress, 0.0f, 1.0f);
    if (fillW > 1.0f) {
        Gdiplus::RectF fill = track;
        fill.Width = fillW;
        FillRoundF(gp, fill, track.Height, MakeColor(kCtrlAccent, 220));
    }
    const float knob = 14.0f;
    float kx = track.X + fillW - knob * 0.5f;
    kx = std::clamp(kx, track.X - 2.0f, track.GetRight() - knob + 2.0f);
    const float ky = track.Y + (track.Height - knob) * 0.5f;
    Gdiplus::SolidBrush glow(MakeColor(kCtrlAccent, 40));
    gp.FillEllipse(&glow, kx - 3.0f, ky - 3.0f, knob + 6.0f, knob + 6.0f);
    Gdiplus::SolidBrush knobBr(Gdiplus::Color(250, 255, 255, 255));
    gp.FillEllipse(&knobBr, kx, ky, knob, knob);
}

void DrawCtrlText(Gdiplus::Graphics& gp, const std::wstring& text, const RECT& r,
                  const Gdiplus::FontFamily& family, float size, COLORREF color, BYTE alpha,
                  Gdiplus::StringAlignment align = Gdiplus::StringAlignmentCenter) {
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::RectF box(static_cast<float>(r.left), static_cast<float>(r.top),
                       static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top));
    Gdiplus::SolidBrush br(MakeColor(color, alpha));
    Gdiplus::Font font(&family, size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    gp.DrawString(text.c_str(), -1, &font, box, &fmt, &br);
}

void DrawCtrlButton(Gdiplus::Graphics& gp, const RECT& r, const std::wstring& text,
                    const Gdiplus::FontFamily& family, int id, bool active = false, float fontSize = 13.0f) {
    const bool hot = (g.ctrlHot == id);
    const bool danger = (id == CID_CLOSE || id == CID_EXIT);
    Gdiplus::Color fill;
    if (danger && hot) {
        fill = Gdiplus::Color(220, 168, 52, 56);
    } else if (active) {
        fill = MakeColor(kCtrlAccent, hot ? 200 : 160);
    } else if (hot) {
        fill = Gdiplus::Color(175, 48, 54, 74);
    } else {
        fill = Gdiplus::Color(95, 28, 32, 48);
    }
    FillRound(gp, r, 12.0f, fill);
    StrokeRound(gp, r, 12.0f, Gdiplus::Color(active ? 70 : 28, 255, 255, 255), 1.0f);
    COLORREF textColor = (active && !danger) ? RGB(22, 20, 26) : RGB(236, 238, 246);
    if (danger && hot) textColor = RGB(255, 236, 236);
    DrawCtrlText(gp, text, r, family, fontSize, textColor, 240);
}

void DrawGhostButton(Gdiplus::Graphics& gp, const RECT& r, const std::wstring& text,
                     const Gdiplus::FontFamily& family, int id) {
    const bool hot = (g.ctrlHot == id);
    FillRound(gp, r, 13.0f, Gdiplus::Color(hot ? 160 : 70, 255, 255, 255));
    StrokeRound(gp, r, 13.0f, Gdiplus::Color(hot ? 70 : 32, 255, 255, 255), 1.0f);
    DrawCtrlText(gp, text, r, family, 13.0f, RGB(236, 238, 246), 235);
}

void DrawCircleButton(Gdiplus::Graphics& gp, const RECT& r, int id, bool primary) {
    const bool hot = (g.ctrlHot == id);
    const float x = static_cast<float>(r.left);
    const float y = static_cast<float>(r.top);
    const float w = static_cast<float>(r.right - r.left);
    const float h = static_cast<float>(r.bottom - r.top);
    const float d = std::min(w, h);
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    if (primary) {
        Gdiplus::SolidBrush glow(MakeColor(kCtrlAccent, hot ? 70 : 40));
        gp.FillEllipse(&glow, cx - d * 0.62f, cy - d * 0.62f, d * 1.24f, d * 1.24f);
        Gdiplus::SolidBrush fill(MakeColor(kCtrlAccent, hot ? 250 : 230));
        gp.FillEllipse(&fill, cx - d * 0.5f, cy - d * 0.5f, d, d);
    } else {
        Gdiplus::SolidBrush fill(Gdiplus::Color(hot ? 180 : 95, 255, 255, 255));
        gp.FillEllipse(&fill, cx - d * 0.5f, cy - d * 0.5f, d, d);
    }
}

void DrawPlayGlyph(Gdiplus::Graphics& gp, const RECT& r, bool playing, COLORREF color) {
    const float cx = (r.left + r.right) * 0.5f;
    const float cy = (r.top + r.bottom) * 0.5f;
    Gdiplus::SolidBrush br(MakeColor(color, 245));
    if (playing) {
        gp.FillRectangle(&br, cx - 8.0f, cy - 10.0f, 5.0f, 20.0f);
        gp.FillRectangle(&br, cx + 3.0f, cy - 10.0f, 5.0f, 20.0f);
    } else {
        Gdiplus::PointF pts[3] = {
            {cx - 6.0f, cy - 11.0f},
            {cx - 6.0f, cy + 11.0f},
            {cx + 11.0f, cy},
        };
        gp.FillPolygon(&br, pts, 3);
    }
}

void DrawSkipGlyph(Gdiplus::Graphics& gp, const RECT& r, bool next, COLORREF color) {
    const float cx = (r.left + r.right) * 0.5f;
    const float cy = (r.top + r.bottom) * 0.5f;
    Gdiplus::SolidBrush br(MakeColor(color, 235));
    const float dir = next ? 1.0f : -1.0f;
    Gdiplus::PointF a[3] = {
        {cx - 7.0f * dir, cy - 8.0f},
        {cx - 7.0f * dir, cy + 8.0f},
        {cx + 4.0f * dir, cy},
    };
    gp.FillPolygon(&br, a, 3);
    gp.FillRectangle(&br, cx + (next ? 5.0f : -8.0f), cy - 8.0f, 3.0f, 16.0f);
}

void DrawToggleRow(Gdiplus::Graphics& gp, const RECT& r, const std::wstring& label, bool on,
                   const Gdiplus::FontFamily& family, int id) {
    const bool hot = (g.ctrlHot == id);
    FillRound(gp, r, 12.0f, Gdiplus::Color(hot ? 42 : 22, 255, 255, 255));
    RECT textR{r.left + 14, r.top, r.right - 58, r.bottom};
    DrawCtrlText(gp, label, textR, family, 13.0f, RGB(228, 232, 242), 235, Gdiplus::StringAlignmentNear);

    const float trackW = 38.0f;
    const float trackH = 20.0f;
    const float tx = static_cast<float>(r.right) - 18.0f - trackW;
    const float ty = r.top + (r.bottom - r.top - trackH) * 0.5f;
    Gdiplus::RectF track(tx, ty, trackW, trackH);
    FillRoundF(gp, track, trackH, on ? MakeColor(kCtrlAccent, 220) : Gdiplus::Color(90, 255, 255, 255));
    const float knob = 16.0f;
    const float kx = on ? (tx + trackW - knob - 2.0f) : (tx + 2.0f);
    Gdiplus::SolidBrush knobBr(Gdiplus::Color(250, 255, 255, 255));
    gp.FillEllipse(&knobBr, kx, ty + 2.0f, knob, knob);
}

void DrawSectionLabel(Gdiplus::Graphics& gp, const RECT& r, const std::wstring& text,
                      const Gdiplus::FontFamily& family) {
    DrawCtrlText(gp, text, r, family, 11.0f, RGB(140, 148, 168), 210, Gdiplus::StringAlignmentNear);
}

std::wstring FormatClock(double sec) {
    if (sec < 0) sec = 0;
    int t = static_cast<int>(sec + 0.5);
    wchar_t buf[32];
    swprintf_s(buf, L"%d:%02d", t / 60, t % 60);
    return buf;
}

void ChangeFontSize(float delta) {
    g.fontSize = std::clamp(g.fontSize + delta, 18.0f, 96.0f);
    PaintOverlay();
    PaintControl();
}

void HideControlPanel() {
    if (!g.hwndCtrl) return;
    g.ctrlVisible = false;
    ShowWindow(g.hwndCtrl, SW_HIDE);
}

void ShowControlPanel() {
    if (!g.hwndCtrl) return;
    g.ctrlVisible = true;
    ShowWindow(g.hwndCtrl, SW_SHOW);
    SetForegroundWindow(g.hwndCtrl);
    PaintControl();
}

void ToggleControlPanel() {
    if (g.ctrlVisible) HideControlPanel();
    else ShowControlPanel();
}

void PaintControl() {
    if (!g.hwndCtrl || !IsWindowVisible(g.hwndCtrl)) return;
    RECT rc{};
    GetClientRect(g.hwndCtrl, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    LayoutControl(w, h);

    HDC screen = GetDC(g.hwndCtrl);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);

    {
        Gdiplus::Graphics gp(mem);
        gp.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gp.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        gp.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        gp.Clear(Gdiplus::Color(0, 0, 0, 0));

        RECT shadow{12, 14, w - 6, h - 6};
        FillRound(gp, shadow, 32.0f, Gdiplus::Color(28, 0, 0, 0));
        RECT body{8, 8, w - 8, h - 8};
        FillRound(gp, body, 26.0f, Gdiplus::Color(168, 14, 16, 24));
        StrokeRound(gp, body, 26.0f, Gdiplus::Color(36, 255, 255, 255), 1.0f);

        Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
        Gdiplus::FontFamily segoe(L"Segoe UI");
        const Gdiplus::FontFamily& family = (yahei.GetLastStatus() == Gdiplus::Ok) ? yahei : segoe;

        RECT brand{28, 16, 140, 42};
        DrawCtrlText(gp, L"桌面歌词", brand, family, 12.0f, RGB(150, 158, 178), 220, Gdiplus::StringAlignmentNear);
        DrawGhostButton(gp, g.ctrlLay.min, L"—", family, CID_MIN);
        DrawGhostButton(gp, g.ctrlLay.close, L"×", family, CID_CLOSE);

        std::wstring titleText;
        if (g.lrcMatched && !g.song.title.empty()) {
            titleText = g.song.title;
        } else if (g.now.available) {
            if (g.matchState == MatchState::Searching || g.searcher.IsSearching() ||
                g.now.waitingKugouIdentify || g.identifier.IsSearching()) {
                titleText = L"正在检索歌词";
            } else {
                titleText = L"正在播放";
            }
        } else {
            titleText = L"未检测到歌曲";
        }
        RECT song{28, 44, w - 28, 82};
        DrawCtrlText(gp, titleText, song, family, 21.0f, RGB(252, 252, 255), 250, Gdiplus::StringAlignmentNear);

        std::wstring sub = g.now.artist;
        if (!g.now.appName.empty()) {
            if (!sub.empty()) sub += L"  ·  ";
            sub += g.now.appName;
        }
        if ((g.now.waitingKugouIdentify || g.identifier.IsSearching()) && !g.lrcMatched) {
            if (!sub.empty()) sub += L"  ·  ";
            sub += L"正在识别背景音乐";
        } else if (g.matchState == MatchState::Searching || g.searcher.IsSearching()) {
            if (!sub.empty()) sub += L"  ·  ";
            sub += L"检索歌词中";
        } else if (g.lrcMatched && !g.lyricSource.empty()) {
            if (!sub.empty()) sub += L"  ·  ";
            sub += g.lyricSource;
        } else if (g.now.available && !g.lrcMatched) {
            if (!sub.empty()) sub += L"  ·  ";
            sub += L"未找到歌词";
        }
        RECT artist{28, 80, w - 28, 106};
        DrawCtrlText(gp, sub.empty() ? L"用播放器放歌即可同步" : sub, artist, family, 12.0f,
                     RGB(156, 164, 186), 220, Gdiplus::StringAlignmentNear);

        RECT lyricCard{24, 112, w - 24, 154};
        FillRound(gp, lyricCard, 14.0f, Gdiplus::Color(28, 255, 255, 255));
        RECT lyric{36, 112, w - 36, 154};
        DrawCtrlText(gp, DisplayLine(g.currentIndex), lyric, family, 14.0f, g.lyricColor, 245, Gdiplus::StringAlignmentNear);

        const double dur = DisplayDuration();
        float progress = (dur > 0.1) ? static_cast<float>(std::clamp(DisplayPosition() / dur, 0.0, 1.0)) : 0.0f;
        DrawProgressPill(gp, g.ctrlLay.seek, progress, g.lyricColor);

        RECT timeL{28, 190, 110, 214};
        RECT timeR{w - 110, 190, w - 28, 214};
        DrawCtrlText(gp, FormatClock(DisplayPosition()), timeL, family, 11.0f, RGB(168, 176, 196), 220, Gdiplus::StringAlignmentNear);
        DrawCtrlText(gp, FormatClock(dur), timeR, family, 11.0f, RGB(168, 176, 196), 220, Gdiplus::StringAlignmentFar);

        DrawCircleButton(gp, g.ctrlLay.prev, CID_PREV, false);
        DrawSkipGlyph(gp, g.ctrlLay.prev, false, RGB(236, 238, 246));
        DrawCircleButton(gp, g.ctrlLay.play, CID_PLAY, true);
        DrawPlayGlyph(gp, g.ctrlLay.play, g.playing, RGB(22, 20, 26));
        DrawCircleButton(gp, g.ctrlLay.next, CID_NEXT, false);
        DrawSkipGlyph(gp, g.ctrlLay.next, true, RGB(236, 238, 246));

        if (g.floatMode) {
            DrawCtrlButton(gp, g.ctrlLay.floatBtn, L"退出全屏漂浮", family, CID_FLOAT, true, 14.0f);
        } else {
            DrawGhostButton(gp, g.ctrlLay.floatBtn, L"全屏漂浮歌词", family, CID_FLOAT);
        }

        RECT modeLabel{28, 348, w - 28, 366};
        DrawSectionLabel(gp, modeLabel, L"模式", family);
        DrawToggleRow(gp, g.ctrlLay.lockBtn, L"锁定穿透", g.locked, family, CID_LOCK);
        DrawToggleRow(gp, g.ctrlLay.followBtn, L"跟随播放器", g.followSystem, family, CID_FOLLOW);
        DrawToggleRow(gp, g.ctrlLay.onlineBtn, L"自动检索歌词", g.onlineSearch, family, CID_ONLINE);

        RECT fontLabelR{28, 492, w - 140, 524};
        wchar_t fontLabel[32];
        swprintf_s(fontLabel, L"字号  %.0f", g.fontSize);
        DrawCtrlText(gp, fontLabel, fontLabelR, family, 13.0f, RGB(210, 216, 230), 230, Gdiplus::StringAlignmentNear);
        DrawGhostButton(gp, g.ctrlLay.fontMinus, L"A−", family, CID_FONT_MINUS);
        DrawGhostButton(gp, g.ctrlLay.fontPlus, L"A+", family, CID_FONT_PLUS);

        RECT actionLabel{28, 528, w - 28, 546};
        DrawSectionLabel(gp, actionLabel, L"操作", family);
        DrawGhostButton(gp, g.ctrlLay.color, L"歌词颜色", family, CID_COLOR);
        DrawGhostButton(gp, g.ctrlLay.reset, L"重置位置", family, CID_RESET);
        DrawGhostButton(gp, g.ctrlLay.open, L"打开歌词", family, CID_OPEN);
        DrawGhostButton(gp, g.ctrlLay.search, L"立即检索", family, CID_SEARCH);
        DrawGhostButton(gp, g.ctrlLay.resync, L"对齐开头", family, CID_RESYNC);
        DrawGhostButton(gp, g.ctrlLay.folder, L"歌词文件夹", family, CID_FOLDER);
        DrawCtrlButton(gp, g.ctrlLay.exit, L"退出", family, CID_EXIT, false, 13.0f);
    }

    POINT ptSrc{0, 0};
    SIZE sizeWnd{w, h};
    POINT ptDst{};
    RECT wr{};
    GetWindowRect(g.hwndCtrl, &wr);
    ptDst.x = wr.left;
    ptDst.y = wr.top;
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g.hwndCtrl, screen, &ptDst, &sizeWnd, mem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(g.hwndCtrl, screen);
}

void SeekFromControl(POINT pt) {
    const RECT& r = g.ctrlLay.seek;
    const double dur = DisplayDuration();
    if (dur <= 0.5) return;
    const float ratio = std::clamp((pt.x - r.left) / static_cast<float>(r.right - r.left), 0.0f, 1.0f);
    SeekTo(dur * ratio);
}

void HandleControlClick(int id) {
    switch (id) {
    case CID_CLOSE:
    case CID_MIN:
        HideControlPanel();
        break;
    case CID_PREV: PrevLine(); break;
    case CID_PLAY: TogglePlay(); break;
    case CID_NEXT: NextLine(); break;
    case CID_FLOAT: ToggleFloatMode(); break;
    case CID_LOCK: ToggleLock(); break;
    case CID_FOLLOW: ToggleFollow(); break;
    case CID_ONLINE: ToggleOnline(); break;
    case CID_FONT_MINUS: ChangeFontSize(-2.0f); break;
    case CID_FONT_PLUS: ChangeFontSize(2.0f); break;
    case CID_COLOR: ChooseColorDialog(); break;
    case CID_RESET: ResetPosition(); break;
    case CID_OPEN: OpenLrcDialog(); break;
    case CID_SEARCH: ManualSearch(); break;
    case CID_RESYNC: SeekTo(0.0); break;
    case CID_FOLDER: OpenLyricsFolder(); break;
    case CID_EXIT: DestroyWindow(g.hwnd); break;
    default: break;
    }
    PaintControl();
}

LRESULT CALLBACK CtrlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == kCtrlTimerId) PaintControl();
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int id = HitControl(pt);
        if (id == CID_SEEK) {
            g.ctrlSeeking = true;
            SetCapture(hwnd);
            SeekFromControl(pt);
            return 0;
        }
        if (id != CID_NONE) {
            HandleControlClick(id);
            return 0;
        }
        SetCapture(hwnd);
        g.ctrlDragging = true;
        g.ctrlDragOffset = pt;
        return 0;
    }
    case WM_LBUTTONUP:
        if (g.ctrlDragging || g.ctrlSeeking) {
            ReleaseCapture();
            g.ctrlDragging = false;
            g.ctrlSeeking = false;
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g.ctrlSeeking && (wParam & MK_LBUTTON)) {
            SeekFromControl(pt);
            return 0;
        }
        if (g.ctrlDragging && (wParam & MK_LBUTTON)) {
            POINT screen;
            GetCursorPos(&screen);
            SetWindowPos(hwnd, nullptr,
                         screen.x - g.ctrlDragOffset.x,
                         screen.y - g.ctrlDragOffset.y,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER);
            return 0;
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        const int hot = HitControl(pt);
        if (hot != g.ctrlHot) {
            g.ctrlHot = hot;
            PaintControl();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g.ctrlHot != CID_NONE) {
            g.ctrlHot = CID_NONE;
            PaintControl();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        ChangeFontSize(delta > 0 ? 2.0f : -2.0f);
        return 0;
    }
    case WM_RBUTTONUP:
        ShowTrayMenu();
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, 0, path, MAX_PATH)) OpenLrcPath(path);
        DragFinish(drop);
        PaintControl();
        return 0;
    }
    case WM_CLOSE:
        HideControlPanel();
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void CreateControlPanel(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CtrlProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kCtrlClass;
    RegisterClassExW(&wc);

    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    const int x = sw - kCtrlW - 36;
    const int y = (sh - kCtrlH) / 2;

    g.hwndCtrl = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES,
        kCtrlClass,
        L"桌面歌词控制台",
        WS_POPUP,
        x, y, kCtrlW, kCtrlH,
        nullptr, nullptr, inst, nullptr);
    if (!g.hwndCtrl) return;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g.hwndCtrl, 20, &dark, sizeof(dark));
    SetTimer(g.hwndCtrl, kCtrlTimerId, 500, nullptr);
    ShowWindow(g.hwndCtrl, SW_SHOW);
    g.ctrlVisible = true;
    PaintControl();
}

void ShowTrayMenu() {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g.followSystem ? MF_CHECKED : 0), ID_TRAY_FOLLOW, L"跟随系统正在播放的歌曲");
    AppendMenuW(menu, MF_STRING | (g.onlineSearch ? MF_CHECKED : 0), ID_TRAY_ONLINE, L"本地没有时自动检索歌词");
    AppendMenuW(menu, MF_STRING, ID_TRAY_SEARCH, L"立即检索当前歌曲");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RESYNC, L"歌词重新对齐到开头");
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"打开歌词文件...\tCtrl+O");
    AppendMenuW(menu, MF_STRING, ID_TRAY_FOLDER, L"打开歌词库文件夹");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_PLAY, g.playing ? L"暂停\tCtrl+空格" : L"播放\tCtrl+空格");
    AppendMenuW(menu, MF_STRING, ID_TRAY_LOCK, g.locked ? L"解锁窗口\tCtrl+L" : L"锁定（点击穿透）\tCtrl+L");
    AppendMenuW(menu, MF_STRING | (g.floatMode ? MF_CHECKED : 0), ID_TRAY_FLOAT,
                g.floatMode ? L"退出全屏漂浮\tCtrl+F" : L"全屏漂浮歌词\tCtrl+F");
    AppendMenuW(menu, MF_STRING | (g.ctrlVisible ? MF_CHECKED : 0), ID_TRAY_PANEL,
                g.ctrlVisible ? L"隐藏控制台\tCtrl+P" : L"显示控制台\tCtrl+P");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_COLOR, L"歌词颜色...");
    AppendMenuW(menu, MF_STRING, ID_TRAY_FONT, L"字号 + / -  （鼠标滚轮）");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RESET, L"重置位置与大小");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");
    SetForegroundWindow(g.hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
}

void ResetPosition() {
    if (g.floatMode) {
        LeaveFloatMode();
        return;
    }
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    g.windowW = 1100;
    g.windowH = 220;
    int x = (sw - g.windowW) / 2;
    int y = sh - g.windowH - 80;
    SetWindowPos(g.hwnd, HWND_TOPMOST, x, y, g.windowW, g.windowH, SWP_SHOWWINDOW);
    PaintOverlay();
    PaintControl();
}

void SyncFromSystem() {
    MediaNowPlaying np = g.media.Get();
    const bool videoNow = np.available && IsBrowserOrVideoApp(np.appName, np.appId) &&
                          !IsMusicPlayerApp(np.appName, np.appId);
    if (videoNow) {
        const BiliProgress bp = g.bili.Get();
        if (bp.ok && bp.durationSec > 3.0) {
            np.positionSec = bp.positionSec;
            np.rawPositionSec = bp.positionSec;
            np.durationSec = bp.durationSec;
            np.fromBiliBar = true;
        }
    }
    const std::wstring key = np.available
        ? (videoNow
               ? np.appId
               : (np.title + L"|" + np.artist + L"|" + np.appId + L"|" + np.sessionTitle))
        : L"";
    const bool trackChanged = (key != g.lastTrackKey);
    g.lastTrackKey = key;
    g.now = np;

    const ULONGLONG now = GetTickCount64();
    const double dt = (g.lastTick && now > g.lastTick)
        ? std::clamp((now - g.lastTick) / 1000.0, 0.0, 0.25)
        : 0.0;

    if (np.available) {
        double reported = np.positionSec;
        const bool videoAppEarly = IsBrowserOrVideoApp(np.appName, np.appId) &&
                                   !IsMusicPlayerApp(np.appName, np.appId);
        if (!videoAppEarly && np.playing && np.stampMs) {
            reported += std::clamp((now - np.stampMs) / 1000.0, 0.0, 0.25);
        }
        const bool manualHold = (now - g.lastManualSeekMs) < 450;
        if (!manualHold) {
            const double lyricReported = reported + (g.vocalClock ? g.vocalOffset : 0.0);
            if (videoAppEarly) {
                const double raw = np.fromBiliBar ? reported : np.rawPositionSec;
                const double probePos = (raw >= 0.0) ? raw : reported;
                const bool staleAfterSeek = g.lastManualSeekTarget >= 0.0 &&
                    std::abs(probePos - g.lastManualSeekTarget) > 1.5 &&
                    std::abs(probePos - g.playhead) > 1.8;
                const bool rawMoved = (g.lastRawVideoPos >= 0.0 && raw >= 0.0 &&
                                       std::abs(raw - g.lastRawVideoPos) > (np.fromBiliBar ? 0.45 : 0.28));
                const bool frozenZero = !np.fromBiliBar && raw >= 0.0 && raw < 0.8 &&
                                        g.playhead > 3.0 && !rawMoved;
                const bool seekBack = rawMoved && !frozenZero && g.playhead > 3.0 &&
                                      raw + 1.2 < g.playhead;
                const bool seekFwd = rawMoved && raw > g.playhead + 1.8;
                const double target = std::max(0.0, reported);
                if (staleAfterSeek && !rawMoved) {
                    if (np.playing || g.playing) {
                        const double elapsed = (now - g.videoClockMs) / 1000.0;
                        g.playhead = g.videoClockPos + std::max(0.0, elapsed);
                    } else {
                        g.videoClockPos = g.playhead;
                        g.videoClockMs = now;
                    }
                } else {
                    if (g.lastManualSeekTarget >= 0.0 &&
                        (std::abs(probePos - g.lastManualSeekTarget) <= 1.5 ||
                         std::abs(probePos - g.playhead) <= 1.8 || rawMoved)) {
                        g.lastManualSeekTarget = -1.0;
                    }
                    if (g.lastSyncedPos < 0.0 || g.videoClockMs == 0) {
                        g.playhead = target;
                        g.videoClockPos = g.playhead;
                        g.videoClockMs = now;
                    } else if (seekBack || seekFwd) {
                        g.playhead = std::max(0.0, raw);
                        g.videoClockPos = g.playhead;
                        g.videoClockMs = now;
                    } else if (np.playing || g.playing) {
                        const double elapsed = (now - g.videoClockMs) / 1000.0;
                        g.playhead = g.videoClockPos + std::max(0.0, elapsed);
                        if (np.fromBiliBar) {
                            // B站时间常按整秒跳。整秒内自己匀速，只在明显落后/超前时轻校正。
                            if (g.playhead < target - 0.08) {
                                g.playhead += (target - g.playhead) * 0.22;
                                g.videoClockPos = g.playhead;
                                g.videoClockMs = now;
                            } else if (g.playhead > target + 1.12) {
                                g.playhead = target + 0.92;
                                g.videoClockPos = g.playhead;
                                g.videoClockMs = now;
                            }
                        } else {
                            const double err = target - g.playhead;
                            if (!frozenZero && std::abs(err) > 0.12 && std::abs(err) < 1.6) {
                                g.playhead += err * 0.45;
                                g.videoClockPos = g.playhead;
                                g.videoClockMs = now;
                            }
                        }
                    } else {
                        g.videoClockPos = g.playhead;
                        g.videoClockMs = now;
                    }
                    if (raw >= 0.0) g.lastRawVideoPos = raw;
                }
            } else {
                const bool staleAfterSeek = g.lastManualSeekTarget >= 0.0 &&
                    std::abs(lyricReported - g.lastManualSeekTarget) > 1.5 &&
                    std::abs(lyricReported - g.playhead) > 1.8;
                if (staleAfterSeek) {
                    if (np.playing) g.playhead += dt;
                } else {
                    if (g.lastManualSeekTarget >= 0.0) g.lastManualSeekTarget = -1.0;
                    if (trackChanged || g.lastSyncedPos < 0.0) {
                        g.playhead = std::max(0.0, lyricReported);
                    } else if (std::abs(lyricReported - g.playhead) > 1.6) {
                        g.playhead = std::max(0.0, lyricReported);
                    } else if (np.playing) {
                        g.playhead += dt;
                    } else {
                        g.playhead = std::max(0.0, lyricReported);
                    }
                }
            }
            if (!g.vocalClock && !videoAppEarly) {
                const double duration = (np.durationSec > 1.0) ? np.durationSec : EffectiveDuration();
                if (duration > 1.0) g.playhead = std::min(g.playhead, duration);
            }
            g.lastSyncedPos = g.playhead;
        }
        g.lastVideoPos = reported;
        g.lastVideoPosMs = now;
        g.mediaDuration = np.durationSec;
        if (np.durationSec > 1.0) g.lastVideoDuration = np.durationSec;
        g.playing = np.playing;
        const bool videoApp = IsBrowserOrVideoApp(np.appName, np.appId) &&
                              !IsMusicPlayerApp(np.appName, np.appId);
        std::wstring videoKey;
        if (videoApp) {
            std::wstring page = np.sessionTitle;
            if (LooksLikeBrowserChromeTitle(page) || LooksLikeChannelHandle(page)) page.clear();
            if (page.empty() && !g.lastIdentifyTitle.empty()) page = g.lastIdentifyTitle;
            if (page.empty() && !g.identifyVideoKey.empty() &&
                g.identifyVideoKey.find(L"|pending") == std::wstring::npos) {
                videoKey = g.identifyVideoKey;
            } else {
                videoKey = page.empty() ? (np.appId + L"|pending") : (page + L"|" + np.appId);
            }
        }
        if (videoApp) {
            const bool needTitle = (now - g.lastTitleRefreshMs > 1800) || g.lastIdentifyTitle.empty();
            const bool pendingUpgrade = (g.identifyVideoKey.find(L"|pending") != std::wstring::npos &&
                                         videoKey.find(L"|pending") == std::wstring::npos &&
                                         videoKey.find(np.appId) != std::wstring::npos);
            if (videoKey != g.identifyVideoKey && pendingUpgrade) {
                g.identifyVideoKey = videoKey;
            } else if (videoKey != g.identifyVideoKey) {
                g.identifyVideoKey = videoKey;
                g.lastIdentifyTitle.clear();
                g.lastIdentifyArtist.clear();
                g.lastSearchKey.clear();
                g.vocalClock = false;
                g.vocalOffset = 0.0;
                g.vocalRate = 1.0;
                g.pendingVocalSongPos = -1.0;
                g.now.waitingKugouIdentify = false;
                g.now.fromKugouIdentify = false;
                g.lrcMatched = false;
                g.matchState = MatchState::Searching;
                g.lyricSource.clear();
                g.song = {};
                g.currentIndex = -1;
                g.lastTitleRefreshMs = now;
                g.lastAudioIdentifyKey.clear();
                g.lastRawVideoPos = -1.0;
                g.videoClockPos = 0.0;
                g.videoClockMs = 0;
                ApplySongNameFromVideoTitle(g.now, true);
                if (!g.now.title.empty() && !LooksLikeBrowserChromeTitle(g.now.title) &&
                    !LooksLikeChannelHandle(g.now.title)) {
                    g.lastIdentifyTitle = g.now.title;
                    g.lastIdentifyArtist = g.now.artist;
                    TryMatchCurrentTrack(g.now, true);
                } else {
                    RequestAudioIdentify(videoKey);
                }
            } else if (needTitle) {
                g.lastTitleRefreshMs = now;
                MediaNowPlaying named = g.now;
                if (ApplySongNameFromVideoTitle(named, true) &&
                    !named.title.empty() && !LooksLikeBrowserChromeTitle(named.title) &&
                    !LooksLikeChannelHandle(named.title)) {
                    const bool titleChanged = NormalizeKey(named.title) != NormalizeKey(g.lastIdentifyTitle);
                    g.now.title = named.title;
                    if (!named.artist.empty()) g.now.artist = named.artist;
                    g.lastIdentifyTitle = named.title;
                    g.lastIdentifyArtist = g.now.artist;
                    if (titleChanged) {
                        g.lastSearchKey.clear();
                        TryMatchCurrentTrack(g.now, false);
                    }
                } else if (!g.lrcMatched && !g.identifier.IsSearching()) {
                    RequestAudioIdentify(videoKey);
                }
            } else if (!g.lastIdentifyTitle.empty()) {
                g.now.title = g.lastIdentifyTitle;
                g.now.artist = g.lastIdentifyArtist;
            }
        } else if (trackChanged) {
            g.identifyVideoKey.clear();
            g.vocalClock = false;
            g.vocalOffset = 0.0;
            g.vocalRate = 1.0;
            TryMatchCurrentTrack(np, true);
        }
        g.currentIndex = FindLyricIndex(g.song, LyricTime());
    } else {
        g.playing = false;
        g.mediaDuration = 0.0;
        if (trackChanged) {
            g.lrcMatched = false;
            g.matchState = MatchState::Idle;
            g.lyricSource.clear();
        }
    }
}

void TickPlayback() {
    ULONGLONG now = GetTickCount64();
    if (g.followSystem) {
        SyncFromSystem();
        PaintOverlay();
        if ((now / 1000) != (g.lastTick / 1000)) UpdateTrayTip();
        g.lastTick = now;
        return;
    }

    if (g.playing) {
        double dt = (now - g.lastTick) / 1000.0;
        g.playhead += dt;
        double duration = EffectiveDuration();
        if (duration > 0 && g.playhead > duration) g.playhead = 0.0;
        g.currentIndex = FindLyricIndex(g.song, g.playhead);
        PaintOverlay();
    }
    g.lastTick = now;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_TIMER:
        if (wParam == kTimerId) TickPlayback();
        return 0;
    case WM_LYRIC_READY:
        HandleOnlineResult();
        return 0;
    case WM_SONG_IDENTIFIED:
        HandleIdentifyResult();
        return 0;

    case WM_LBUTTONDOWN: {
        if (g.locked || g.floatMode) return 0;
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT rc{};
        GetClientRect(hwnd, &rc);
        double ratio = 0;
        if (HitProgressBar(pt, rc.right - rc.left, rc.bottom - rc.top, ratio)) {
            const double dur = DisplayDuration();
            if (dur > 0.5) SeekTo(dur * ratio);
            g.seekingBar = true;
            SetCapture(hwnd);
            return 0;
        }
        SetCapture(hwnd);
        g.dragging = true;
        g.dragOffset = pt;
        return 0;
    }
    case WM_LBUTTONUP:
        if (g.dragging || g.seekingBar) {
            ReleaseCapture();
            g.dragging = false;
            g.seekingBar = false;
        }
        return 0;
    case WM_MOUSEMOVE:
        if (g.seekingBar && (wParam & MK_LBUTTON)) {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc{};
            GetClientRect(hwnd, &rc);
            float x, y, barW, barH;
            ProgressBarRect(rc.right - rc.left, rc.bottom - rc.top, x, y, barW, barH);
            double ratio = std::clamp((pt.x - x) / barW, 0.0f, 1.0f);
            const double dur = DisplayDuration();
            if (dur > 0.5) SeekTo(dur * ratio);
            return 0;
        }
        if (g.dragging && (wParam & MK_LBUTTON)) {
            POINT pt;
            GetCursorPos(&pt);
            SetWindowPos(hwnd, nullptr,
                         pt.x - g.dragOffset.x,
                         pt.y - g.dragOffset.y,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        if (g.floatMode) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g.fontSize = std::clamp(g.fontSize + (delta > 0 ? 2.0f : -2.0f), 18.0f, 96.0f);
        PaintOverlay();
        PaintControl();
        return 0;
    }
    case WM_RBUTTONUP:
        ShowTrayMenu();
        return 0;
    case WM_LBUTTONDBLCLK:
        TogglePlay();
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, 0, path, MAX_PATH)) OpenLrcPath(path);
        DragFinish(drop);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN: OpenLrcDialog(); break;
        case ID_TRAY_PLAY: TogglePlay(); break;
        case ID_TRAY_LOCK: ToggleLock(); break;
        case ID_TRAY_FLOAT: ToggleFloatMode(); break;
        case ID_TRAY_PANEL: ToggleControlPanel(); break;
        case ID_TRAY_COLOR: ChooseColorDialog(); break;
        case ID_TRAY_RESET: ResetPosition(); break;
        case ID_TRAY_FOLLOW: ToggleFollow(); break;
        case ID_TRAY_ONLINE: ToggleOnline(); break;
        case ID_TRAY_SEARCH: ManualSearch(); break;
        case ID_TRAY_RESYNC: SeekTo(0.0); break;
        case ID_TRAY_FOLDER: OpenLyricsFolder(); break;
        case ID_TRAY_EXIT: DestroyWindow(hwnd); break;
        default: break;
        }
        return 0;
    case WM_HOTKEY:
        switch (wParam) {
        case kHotkeyPrev: PrevLine(); break;
        case kHotkeyPlay: TogglePlay(); break;
        case kHotkeyNext: NextLine(); break;
        case kHotkeyLock: ToggleLock(); break;
        case kHotkeyFloat: ToggleFloatMode(); break;
        case kHotkeyPanel: ToggleControlPanel(); break;
        case kHotkeyOpen: OpenLrcDialog(); break;
        default: break;
        }
        return 0;
    case WM_TRAY:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu();
        else if (lParam == WM_LBUTTONDBLCLK) TogglePlay();
        else if (lParam == WM_LBUTTONUP) ShowControlPanel();
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (g.locked || g.floatMode) return hit;
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int grip = 16;
        bool right = pt.x >= rc.right - grip;
        bool bottom = pt.y >= rc.bottom - grip;
        if (right && bottom) return HTBOTTOMRIGHT;
        if (right) return HTRIGHT;
        if (bottom) return HTBOTTOM;
        return hit;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g.windowW = LOWORD(lParam);
            g.windowH = HIWORD(lParam);
            PaintOverlay();
            PaintControl();
        }
        return 0;
    case WM_DESTROY:
        if (g.overlayMem) {
            SelectObject(g.overlayMem, g.overlayOld);
            DeleteObject(g.overlayBmp);
            DeleteDC(g.overlayMem);
            g.overlayMem = nullptr;
        }
        g.searcher.Stop();
        g.media.Stop();
        g.bili.Stop();
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        UnregisterHotKey(hwnd, kHotkeyPrev);
        UnregisterHotKey(hwnd, kHotkeyPlay);
        UnregisterHotKey(hwnd, kHotkeyNext);
        UnregisterHotKey(hwnd, kHotkeyLock);
        UnregisterHotKey(hwnd, kHotkeyFloat);
        UnregisterHotKey(hwnd, kHotkeyPanel);
        UnregisterHotKey(hwnd, kHotkeyOpen);
        if (g.hwndCtrl) {
            KillTimer(g.hwndCtrl, kCtrlTimerId);
            DestroyWindow(g.hwndCtrl);
            g.hwndCtrl = nullptr;
        }
        KillTimer(hwnd, kTimerId);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool InitTray(HINSTANCE) {
    g.nid.cbSize = sizeof(g.nid);
    g.nid.hWnd = g.hwnd;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g.nid.szTip, L"桌面歌词", _TRUNCATE);
    return Shell_NotifyIconW(NIM_ADD, &g.nid) == TRUE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&g.gdiplusToken, &gdiInput, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - g.windowW) / 2;
    int y = sh - g.windowH - 80;

    g.hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES,
        kWndClass,
        L"桌面歌词",
        WS_POPUP,
        x, y, g.windowW, g.windowH,
        nullptr, nullptr, inst, nullptr);

    if (!g.hwnd) return 1;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g.hwnd, 20, &dark, sizeof(dark));

    RegisterHotKey(g.hwnd, kHotkeyPrev, MOD_CONTROL, VK_LEFT);
    RegisterHotKey(g.hwnd, kHotkeyPlay, MOD_CONTROL, VK_SPACE);
    RegisterHotKey(g.hwnd, kHotkeyNext, MOD_CONTROL, VK_RIGHT);
    RegisterHotKey(g.hwnd, kHotkeyLock, MOD_CONTROL, 'L');
    RegisterHotKey(g.hwnd, kHotkeyFloat, MOD_CONTROL, 'F');
    RegisterHotKey(g.hwnd, kHotkeyPanel, MOD_CONTROL, 'P');
    RegisterHotKey(g.hwnd, kHotkeyOpen, MOD_CONTROL, 'O');

    EnsureLyricsFolder();
    g.inst = inst;
    InitTray(inst);
    CreateControlPanel(inst);
    g.searcher.SetNotifyHwnd(g.hwnd);
    g.identifier.SetNotifyHwnd(g.hwnd);
    g.searcher.Start();
    g.identifier.Start();
    g.media.Start();
    g.bili.Start();
    SetTimer(g.hwnd, kTimerId, 24, nullptr);
    ShowWindow(g.hwnd, SW_SHOW);
    UpdateWindow(g.hwnd);

    if (cmdLine && cmdLine[0]) {
        std::wstring startPath = cmdLine;
        if (startPath.size() >= 2 && startPath.front() == L'"' && startPath.back() == L'"') {
            startPath = startPath.substr(1, startPath.size() - 2);
        }
        OpenLrcPath(startPath);
    }

    g.lastTick = GetTickCount64();
    PaintOverlay();
    PaintControl();
    UpdateTrayTip();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g.searcher.Stop();
    g.identifier.Stop();
    g.media.Stop();
    g.bili.Stop();
    Gdiplus::GdiplusShutdown(g.gdiplusToken);
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
