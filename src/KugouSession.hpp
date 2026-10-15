#pragma once

#include "LrcParser.hpp"

#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

struct KugouNowPlaying {
    bool available = false;
    bool playing = false;
    std::wstring title;
    std::wstring artist;
    std::wstring rawTitle;
    std::wstring lyricDir;
    double savedPositionSec = -1.0;
    ULONGLONG iniWriteMs = 0;
};

inline std::wstring ExpandEnvWide(const wchar_t* spec) {
    wchar_t buf[MAX_PATH * 2]{};
    DWORD n = ExpandEnvironmentStringsW(spec, buf, MAX_PATH * 2);
    if (!n) return {};
    return buf;
}

inline std::wstring GetKugouIniPath() {
    std::wstring appdata = ExpandEnvWide(L"%APPDATA%\\KuGou8\\KuGou.ini");
    if (!appdata.empty() && GetFileAttributesW(appdata.c_str()) != INVALID_FILE_ATTRIBUTES) return appdata;
    appdata = ExpandEnvWide(L"%APPDATA%\\KuGou\\KuGou.ini");
    if (!appdata.empty() && GetFileAttributesW(appdata.c_str()) != INVALID_FILE_ATTRIBUTES) return appdata;
    return {};
}

inline std::wstring GetKugouLyricDirectory() {
    const std::wstring ini = GetKugouIniPath();
    if (!ini.empty()) {
        char pathA[MAX_PATH]{};
        char iniPathA[MAX_PATH]{};
        WideCharToMultiByte(CP_ACP, 0, ini.c_str(), -1, iniPathA, MAX_PATH, nullptr, nullptr);
        GetPrivateProfileStringA("LyricConfigSection", "LyricPath", "", pathA, MAX_PATH, iniPathA);
        if (pathA[0]) {
            std::wstring w = MultiByteToWide(CP_ACP, pathA, static_cast<int>(strlen(pathA)));
            if (!w.empty() && w.back() != L'\\' && w.back() != L'/') w += L'\\';
            if (GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES) return w;
        }
    }
    const wchar_t* fallbacks[] = {L"D:\\KuGou\\Lyric\\", L"C:\\KuGou\\Lyric\\"};
    for (auto p : fallbacks) {
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return {};
}

inline ULONGLONG FileWriteTickMs(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    ULARGE_INTEGER u{};
    u.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return u.QuadPart / 10000ULL;
}

inline std::wstring ReadKugouIniTitle() {
    const std::wstring ini = GetKugouIniPath();
    if (ini.empty()) return {};
    wchar_t title[512]{};
    GetPrivateProfileStringW(L"PlaybackState", L"LastPlayingTitleName", L"", title, 512, ini.c_str());
    if (!title[0]) {
        char iniPathA[MAX_PATH]{};
        WideCharToMultiByte(CP_ACP, 0, ini.c_str(), -1, iniPathA, MAX_PATH, nullptr, nullptr);
        char titleA[512]{};
        GetPrivateProfileStringA("PlaybackState", "LastPlayingTitleName", "", titleA, sizeof(titleA), iniPathA);
        if (titleA[0]) return MultiByteToWide(CP_ACP, titleA, static_cast<int>(strlen(titleA)));
        return {};
    }
    return title;
}

inline double ReadKugouSavedPositionSec() {
    const std::wstring ini = GetKugouIniPath();
    if (ini.empty()) return -1.0;
    const int pos = GetPrivateProfileIntW(L"PlaybackState", L"LastPlayingSongPos", -1, ini.c_str());
    if (pos < 0) {
        char iniPathA[MAX_PATH]{};
        WideCharToMultiByte(CP_ACP, 0, ini.c_str(), -1, iniPathA, MAX_PATH, nullptr, nullptr);
        const int posA = GetPrivateProfileIntA("PlaybackState", "LastPlayingSongPos", -1, iniPathA);
        if (posA < 0) return -1.0;
        if (posA > 24 * 3600) return posA / 1000.0;
        if (posA > 3600) return posA / 1000.0;
        return posA / 1000.0;
    }
    // KuGou stores milliseconds.
    if (pos > 24 * 3600 * 1000) return -1.0;
    return pos / 1000.0;
}

inline bool IsKugouProcessName(const wchar_t* name) {
    if (!name) return false;
    std::wstring s(name);
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s.find(L"kugou") != std::wstring::npos;
}

inline std::vector<DWORD> FindKugouPids() {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (IsKugouProcessName(pe.szExeFile)) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

struct KugouWindowProbe {
    std::vector<DWORD> pids;
    std::wstring bestTitle;
};

inline bool KugouTitleLooksLikeSong(const std::wstring& title) {
    if (title.find(L"酷狗音乐") == std::wstring::npos &&
        title.find(L"KuGou") == std::wstring::npos &&
        title.find(L"kugou") == std::wstring::npos) {
        return false;
    }
    if (title.find(L" - ") == std::wstring::npos) return false;
    const wchar_t* junk[] = {
        L"正在连接", L"信息提示", L"手机互联", L"下载到设备", L"登录", L"设置",
        L"ActiveMovie", L"Default IME", L"MSCTFIME"
    };
    for (auto j : junk) {
        if (title.find(j) != std::wstring::npos) return false;
    }
    return true;
}

inline void StripAudioExtensionLocal(std::wstring& title) {
    static const wchar_t* exts[] = {
        L".mp3", L".flac", L".wav", L".m4a", L".aac", L".ogg",
        L".wma", L".ape", L".alac", L".dsf", L".dff", L".opus"
    };
    if (title.size() < 5) return;
    std::wstring lower = title;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    for (auto ext : exts) {
        size_t n = wcslen(ext);
        if (lower.size() > n && lower.compare(lower.size() - n, n, ext) == 0) {
            title.resize(title.size() - n);
            return;
        }
    }
}

inline std::wstring StripKugouSuffix(std::wstring title) {
    TrimInPlace(title);
    const wchar_t* suffixes[] = {
        L" - 酷狗音乐", L"- 酷狗音乐", L" - KuGou Music", L" - KuGou"
    };
    for (auto s : suffixes) {
        auto pos = title.rfind(s);
        if (pos != std::wstring::npos && pos + wcslen(s) >= title.size() - 2) {
            title = title.substr(0, pos);
            TrimInPlace(title);
            break;
        }
    }
    return title;
}

inline BOOL CALLBACK KugouEnumProc(HWND hwnd, LPARAM lp) {
    auto* probe = reinterpret_cast<KugouWindowProbe*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    bool ours = probe->pids.empty();
    for (DWORD p : probe->pids) {
        if (p == pid) {
            ours = true;
            break;
        }
    }
    if (!ours) return TRUE;

    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    wchar_t text[512]{};
    GetWindowTextW(hwnd, text, 512);
    std::wstring title(text);
    TrimInPlace(title);
    if (!KugouTitleLooksLikeSong(title)) return TRUE;

    std::wstring clsName(cls);
    for (auto& c : clsName) c = static_cast<wchar_t>(towlower(c));
    const bool kugouUi = clsName.find(L"kugou") != std::wstring::npos;
    if (!kugouUi && title.size() < 8) return TRUE;
    if (title.size() > probe->bestTitle.size()) probe->bestTitle = title;
    return TRUE;
}

inline bool KugouAudioIsPlaying(const std::vector<DWORD>& pids) {
    if (pids.empty()) return true;
    IMMDeviceEnumerator* enumer = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumer));
    if (FAILED(hr) || !enumer) return true;

    IMMDevice* device = nullptr;
    hr = enumer->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    enumer->Release();
    if (FAILED(hr) || !device) return true;

    IAudioSessionManager2* mgr = nullptr;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr));
    device->Release();
    if (FAILED(hr) || !mgr) return true;

    IAudioSessionEnumerator* list = nullptr;
    hr = mgr->GetSessionEnumerator(&list);
    mgr->Release();
    if (FAILED(hr) || !list) return true;

    int count = 0;
    list->GetCount(&count);
    bool sawKugou = false;
    float peakMax = 0.0f;
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl* ctl = nullptr;
        if (FAILED(list->GetSession(i, &ctl)) || !ctl) continue;
        IAudioSessionControl2* ctl2 = nullptr;
        DWORD pid = 0;
        if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&ctl2))) && ctl2) {
            ctl2->GetProcessId(&pid);
            ctl2->Release();
        }
        bool match = false;
        for (DWORD p : pids) {
            if (p == pid) {
                match = true;
                break;
            }
        }
        if (match) {
            sawKugou = true;
            IAudioMeterInformation* meter = nullptr;
            if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioMeterInformation), reinterpret_cast<void**>(&meter))) && meter) {
                float peak = 0;
                if (SUCCEEDED(meter->GetPeakValue(&peak)) && peak > peakMax) peakMax = peak;
                meter->Release();
            }
        }
        ctl->Release();
    }
    list->Release();
    if (!sawKugou) return true;
    return peakMax > 0.002f;
}

inline bool ParseKugouSongLine(std::wstring raw, std::wstring& title, std::wstring& artist) {
    std::wstring body = StripKugouSuffix(raw);
    if (body.empty()) body = raw;
    TrimInPlace(body);
    if (body.empty()) return false;
    title = body;
    artist.clear();
    auto pos = body.rfind(L" - ");
    if (pos != std::wstring::npos && pos > 0 && pos + 3 < body.size()) {
        artist = body.substr(0, pos);
        title = body.substr(pos + 3);
        TrimInPlace(artist);
        TrimInPlace(title);
    }
    StripAudioExtensionLocal(title);
    return !title.empty();
}

inline bool ReadKugouIdentify(KugouNowPlaying& np) {
    np = {};
    auto pids = FindKugouPids();
    if (pids.empty()) return false;
    std::wstring raw = ReadKugouIniTitle();
    if (raw.empty()) return false;
    std::wstring title, artist;
    if (!ParseKugouSongLine(raw, title, artist)) return false;
    np.available = true;
    np.rawTitle = raw;
    np.title = title;
    np.artist = artist;
    np.lyricDir = GetKugouLyricDirectory();
    np.savedPositionSec = ReadKugouSavedPositionSec();
    np.iniWriteMs = FileWriteTickMs(GetKugouIniPath());
    np.playing = false;
    return true;
}

inline bool ProbeKugouNowPlaying(KugouNowPlaying& np) {
    np = {};
    auto pids = FindKugouPids();
    if (pids.empty()) return false;
    KugouWindowProbe probe;
    probe.pids = pids;
    EnumWindows(KugouEnumProc, reinterpret_cast<LPARAM>(&probe));

    std::wstring raw = probe.bestTitle;
    if (raw.empty()) raw = ReadKugouIniTitle();
    if (raw.empty()) return false;

    std::wstring title, artist;
    if (!ParseKugouSongLine(raw, title, artist)) return false;

    np.available = true;
    np.rawTitle = raw;
    np.title = title;
    np.artist = artist;
    np.lyricDir = GetKugouLyricDirectory();
    np.playing = pids.empty() ? true : KugouAudioIsPlaying(pids);
    np.savedPositionSec = ReadKugouSavedPositionSec();
    np.iniWriteMs = FileWriteTickMs(GetKugouIniPath());
    return true;
}
