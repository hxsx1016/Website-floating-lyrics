#pragma once

#include "KugouSession.hpp"
#include "LyricLibrary.hpp"

#include <tlhelp32.h>

#include <string>
#include <vector>

struct PlayerNowPlaying {
    bool available = false;
    bool playing = false;
    std::wstring title;
    std::wstring artist;
    std::wstring appId;
    std::wstring appName;
};

inline std::wstring ExpandEnvPath(const wchar_t* spec) {
    wchar_t buf[MAX_PATH * 2]{};
    DWORD n = ExpandEnvironmentStringsW(spec, buf, MAX_PATH * 2);
    if (!n) return {};
    return buf;
}

inline std::wstring DirIfExists(std::wstring p) {
    if (p.empty()) return {};
    if (p.back() != L'\\' && p.back() != L'/') p += L'\\';
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    return {};
}

inline std::vector<std::wstring> MusicPlayerLyricFolders() {
    std::vector<std::wstring> out;
    auto add = [&](std::wstring p) {
        p = DirIfExists(std::move(p));
        if (p.empty()) return;
        for (const auto& e : out) if (e == p) return;
        out.push_back(std::move(p));
    };
    add(ExpandEnvPath(L"%LOCALAPPDATA%\\Netease\\CloudMusic\\webdata\\lyric"));
    add(ExpandEnvPath(L"%LOCALAPPDATA%\\Netease\\CloudMusic\\Download\\Lyric"));
    add(ExpandEnvPath(L"%LOCALAPPDATA%\\Netease\\CloudMusic\\lyric"));
    add(ExpandEnvPath(L"%APPDATA%\\Tencent\\QQMusic\\Lyric"));
    add(ExpandEnvPath(L"%USERPROFILE%\\Documents\\QQMusic\\Lyric"));
    add(ExpandEnvPath(L"%APPDATA%\\kuwo\\kwmusic\\lyric"));
    add(ExpandEnvPath(L"%APPDATA%\\KuwoMusic\\lyrics"));
    add(GetKugouLyricDirectory());
    return out;
}

inline bool ProcessNameContains(const wchar_t* name, const wchar_t* needle) {
    if (!name || !needle) return false;
    std::wstring s(name);
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s.find(needle) != std::wstring::npos;
}

inline std::vector<DWORD> FindPidsByNeedle(const wchar_t* needle) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (ProcessNameContains(pe.szExeFile, needle)) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

inline bool TitleLooksLikePlayerSong(const std::wstring& title, const wchar_t* brand) {
    if (title.empty()) return false;
    const wchar_t* junk[] = {
        L"正在连接", L"信息提示", L"登录", L"设置", L"下载", L"消息",
        L"ActiveMovie", L"Default IME", L"MSCTFIME", L"Chrome Legacy Window",
        L"GDI+ Window", L"DesktopLyric", L"桌面歌词"
    };
    for (auto j : junk) {
        if (title.find(j) != std::wstring::npos) return false;
    }
    if (brand && title == brand) return false;
    if (title.find(L" - ") != std::wstring::npos) return true;
    if (brand && title.find(brand) != std::wstring::npos && title.size() > wcslen(brand) + 3) return true;
    return title.size() >= 2 && title.size() <= 80;
}

inline void StripPlayerBrandSuffix(std::wstring& title) {
    const wchar_t* suffixes[] = {
        L" - 网易云音乐", L" - 网易云", L" - QQ音乐", L" - QQ 音乐",
        L" - 酷狗音乐", L" - 酷我音乐", L" - Spotify", L" - Apple Music",
        L" - YesPlayMusic", L" - 洛雪音乐"
    };
    for (auto s : suffixes) {
        auto pos = title.rfind(s);
        if (pos != std::wstring::npos && pos + wcslen(s) >= title.size() - 2) {
            title = title.substr(0, pos);
            TrimInPlace(title);
            break;
        }
    }
}

struct PlayerWindowProbe {
    std::vector<DWORD> pids;
    const wchar_t* brand = nullptr;
    std::wstring bestTitle;
};

inline BOOL CALLBACK PlayerEnumProc(HWND hwnd, LPARAM lp) {
    auto* probe = reinterpret_cast<PlayerWindowProbe*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    bool ours = false;
    for (DWORD p : probe->pids) {
        if (p == pid) {
            ours = true;
            break;
        }
    }
    if (!ours) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t text[512]{};
    GetWindowTextW(hwnd, text, 512);
    std::wstring title(text);
    TrimInPlace(title);
    if (!TitleLooksLikePlayerSong(title, probe->brand)) return TRUE;
    if (title.size() > probe->bestTitle.size()) probe->bestTitle = title;
    return TRUE;
}

inline bool ProbeNamedPlayer(const wchar_t* processNeedle, const wchar_t* brand,
                             const wchar_t* appId, const wchar_t* appName,
                             PlayerNowPlaying& np) {
    auto pids = FindPidsByNeedle(processNeedle);
    if (pids.empty()) return false;
    PlayerWindowProbe probe;
    probe.pids = pids;
    probe.brand = brand;
    EnumWindows(PlayerEnumProc, reinterpret_cast<LPARAM>(&probe));
    if (probe.bestTitle.empty()) return false;

    std::wstring body = probe.bestTitle;
    StripPlayerBrandSuffix(body);
    if (body.empty()) return false;
    std::wstring title = body;
    std::wstring artist;
    SplitArtistDashSong(body, title, artist);
    if (title.empty()) title = body;
    StripAudioExtensionLocal(title);
    if (title.empty()) return false;

    np.available = true;
    np.playing = true;
    np.title = title;
    np.artist = artist;
    np.appId = appId;
    np.appName = appName;
    return true;
}

inline bool BrowserTitleLooksLikePage(const std::wstring& title) {
    if (title.size() < 4) return false;
    if (LooksLikeBrowserChromeTitle(title)) return false;
    const wchar_t* junk[] = {
        L"ActiveMovie", L"Default IME", L"MSCTFIME", L"Chrome Legacy Window",
        L"GDI+ Window", L"桌面歌词", L"New Tab", L"新标签页", L"Settings", L"设置"
    };
    for (auto j : junk) {
        if (title.find(j) != std::wstring::npos) return false;
    }
    return true;
}

inline int ScoreBrowserPageTitle(const std::wstring& title, const std::wstring& hint, bool foreground) {
    std::wstring body = title;
    StripBrowserChromeNoise(body);
    StripSiteSuffixes(body);
    if (body.empty() || LooksLikeChannelHandle(body)) return 0;
    if (!BrowserTitleLooksLikePage(title) && !BrowserTitleLooksLikePage(body)) return 0;
    int score = CountCjk(body) * 6 + static_cast<int>(body.size());
    if (body.find(L'【') != std::wstring::npos) score += 40;
    if (body.find(L'「') != std::wstring::npos || body.find(L'『') != std::wstring::npos) score += 35;
    if ((body.find(L" - ") != std::wstring::npos || body.find(L" – ") != std::wstring::npos) &&
        CountCjk(body) >= 2) {
        score += 50;
    }
    if (title.find(L"哔哩哔哩") != std::wstring::npos || title.find(L"YouTube") != std::wstring::npos) score += 10;
    if (foreground) score += 40;
    const std::wstring nh = NormalizeKey(hint);
    const std::wstring nt = NormalizeKey(body);
    if (!LooksLikeChannelHandle(hint) && nh.size() >= 2 && nt.find(nh) != std::wstring::npos) score += 70;
    return score;
}

inline bool WindowLooksLikeBrowser(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t path[MAX_PATH]{};
    DWORD n = MAX_PATH;
    QueryFullProcessImageNameW(h, 0, path, &n);
    CloseHandle(h);
    std::wstring exe(path);
    for (auto& c : exe) c = static_cast<wchar_t>(towlower(c));
    return exe.find(L"msedge") != std::wstring::npos ||
           exe.find(L"chrome") != std::wstring::npos ||
           exe.find(L"firefox") != std::wstring::npos ||
           exe.find(L"bilibili") != std::wstring::npos;
}

inline std::wstring ProbeBrowserPageTitle(const std::wstring& hint = {}) {
    struct BrowserProbe {
        std::wstring hint;
        HWND foreground = nullptr;
        std::wstring best;
        int bestScore = 0;
    } probe;
    probe.hint = hint;
    probe.foreground = GetForegroundWindow();
    if (probe.foreground && WindowLooksLikeBrowser(probe.foreground)) {
        wchar_t text[1024]{};
        GetWindowTextW(probe.foreground, text, 1024);
        std::wstring title(text);
        TrimInPlace(title);
        const int score = ScoreBrowserPageTitle(title, hint, true);
        if (score > 0) {
            probe.best = title;
            probe.bestScore = score + 20;
        }
    }
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* p = reinterpret_cast<BrowserProbe*>(lp);
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
        if (!WindowLooksLikeBrowser(hwnd)) return TRUE;
        wchar_t text[1024]{};
        GetWindowTextW(hwnd, text, 1024);
        std::wstring title(text);
        TrimInPlace(title);
        const int score = ScoreBrowserPageTitle(title, p->hint, hwnd == p->foreground);
        if (score > p->bestScore) {
            p->bestScore = score;
            p->best = title;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&probe));
    return probe.best;
}

inline bool PreferOriginalTitle(const std::wstring& candidate, const std::wstring& current) {
    if (candidate.empty() || LooksLikeBrowserChromeTitle(candidate)) return false;
    if (NormalizeKey(candidate) == NormalizeKey(current)) return false;
    if (LooksLikeBrowserChromeTitle(current)) return true;
    const int cjk = CountCjk(candidate);
    if (cjk >= 2 && CountCjk(current) < 2) return true;
    if (cjk >= 2 && candidate.size() + 6 < current.size()) return true;
    return false;
}

inline bool ProbeMusicPlayerNowPlaying(PlayerNowPlaying& np) {
    np = {};
    if (ProbeNamedPlayer(L"cloudmusic", L"网易云音乐", L"cloudmusic.exe", L"网易云音乐", np)) return true;
    if (ProbeNamedPlayer(L"qqmusic", L"QQ音乐", L"QQMusic.exe", L"QQ音乐", np)) return true;
    if (ProbeNamedPlayer(L"kwmusic", L"酷我音乐", L"kwmusic.exe", L"酷我音乐", np)) return true;
    if (ProbeNamedPlayer(L"spotify", L"Spotify", L"Spotify.exe", L"Spotify", np)) return true;
    if (ProbeNamedPlayer(L"yesplaymusic", L"YesPlayMusic", L"YesPlayMusic.exe", L"YesPlayMusic", np)) return true;
    if (ProbeNamedPlayer(L"lx-music", L"洛雪音乐", L"lx-music-desktop.exe", L"洛雪音乐", np)) return true;
    return false;
}
