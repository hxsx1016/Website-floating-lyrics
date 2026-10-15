#pragma once

#include "LrcParser.hpp"
#include "KrcParser.hpp"

#include <shlobj.h>
#include <knownfolders.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

inline std::wstring NormalizeKey(std::wstring s) {
    TrimInPlace(s);
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (c >= 0xFF01 && c <= 0xFF5E) c = static_cast<wchar_t>(c - 0xFEE0);
        switch (c) {
        case L'鐵': c = L'铁'; break;
        case L'飛': c = L'飞'; break;
        case L'樂': c = L'乐'; break;
        case L'國': c = L'国'; break;
        case L'詞': c = L'词'; break;
        case L'語': c = L'语'; break;
        case L'愛': c = L'爱'; break;
        case L'風': c = L'风'; break;
        case L'東': c = L'东'; break;
        case L'長': c = L'长'; break;
        case L'時': c = L'时'; break;
        case L'無': c = L'无'; break;
        case L'為': c = L'为'; break;
        case L'與': c = L'与'; break;
        case L'這': c = L'这'; break;
        case L'後': c = L'后'; break;
        case L'來': c = L'来'; break;
        case L'對': c = L'对'; break;
        case L'開': c = L'开'; break;
        case L'關': c = L'关'; break;
        case L'點': c = L'点'; break;
        case L'萬': c = L'万'; break;
        case L'學': c = L'学'; break;
        case L'經': c = L'经'; break;
        case L'過': c = L'过'; break;
        case L'還': c = L'还'; break;
        case L'麼': c = L'么'; break;
        case L'們': c = L'们'; break;
        case L'個': c = L'个'; break;
        case L'會': c = L'会'; break;
        case L'說': c = L'说'; break;
        case L'讓': c = L'让'; break;
        case L'從': c = L'从'; break;
        case L'當': c = L'当'; break;
        case L'應': c = L'应'; break;
        case L'聲': c = L'声'; break;
        case L'夢': c = L'梦'; break;
        case L'離': c = L'离'; break;
        case L'歡': c = L'欢'; break;
        case L'聽': c = L'听'; break;
        case L'裏': case L'裡': c = L'里'; break;
        default: break;
        }
        c = static_cast<wchar_t>(towlower(c));
        if (iswalnum(c) ||
            (c >= 0x3040 && c <= 0x30FF) ||
            (c >= 0x4E00 && c <= 0x9FFF) ||
            (c >= 0x3400 && c <= 0x4DBF) ||
            (c >= 0x31F0 && c <= 0x31FF)) {
            out.push_back(c);
        }
    }
    return out;
}

inline std::wstring ExeDirectory() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash + 1);
    return path;
}

inline std::wstring MusicDirectory() {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &p)) && p) {
        std::wstring s(p);
        CoTaskMemFree(p);
        if (!s.empty() && s.back() != L'\\' && s.back() != L'/') s += L'\\';
        return s;
    }
    return {};
}

inline bool ContainsFold(const std::wstring& hay, const wchar_t* needle) {
    return hay.find(needle) != std::wstring::npos;
}

inline bool IsBrowserOrVideoApp(const std::wstring& appName, const std::wstring& appId) {
    std::wstring s = appName + L" " + appId;
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return ContainsFold(s, L"chrome") || ContainsFold(s, L"msedge") || ContainsFold(s, L"edge") ||
           ContainsFold(s, L"firefox") || ContainsFold(s, L"bilibili") || ContainsFold(s, L"youtube") ||
           ContainsFold(s, L"potplayer") || ContainsFold(s, L"vlc") || ContainsFold(s, L"mpv");
}

inline bool IsMusicPlayerApp(const std::wstring& appName, const std::wstring& appId) {
    std::wstring s = appName + L" " + appId;
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return ContainsFold(s, L"cloudmusic") || ContainsFold(s, L"netease") || ContainsFold(s, L"orpheus") ||
           ContainsFold(s, L"neteasemusic") || ContainsFold(s, L"网易云") ||
           ContainsFold(s, L"qqmusic") || ContainsFold(s, L"qq音乐") || ContainsFold(s, L"qqmusic") ||
           ContainsFold(s, L"kugou") || ContainsFold(s, L"酷狗") ||
           ContainsFold(s, L"kuwo") || ContainsFold(s, L"kwmusic") || ContainsFold(s, L"酷我") ||
           ContainsFold(s, L"spotify") || ContainsFold(s, L"applemusic") || ContainsFold(s, L"itunes") ||
           ContainsFold(s, L"yesplaymusic") || ContainsFold(s, L"lx-music") || ContainsFold(s, L"lxmusic") ||
           ContainsFold(s, L"musicbee") || ContainsFold(s, L"foobar") || ContainsFold(s, L"aimp") ||
           ContainsFold(s, L"sodamusic") || ContainsFold(s, L"listen1") || ContainsFold(s, L"dopamine");
}

inline bool EndsWithFold(const std::wstring& lower, const wchar_t* tail) {
    const size_t n = wcslen(tail);
    return lower.size() >= n && lower.compare(lower.size() - n, n, tail) == 0;
}

inline void StripBrowserChromeNoise(std::wstring& title) {
    TrimInPlace(title);
    std::wstring lower = title;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    size_t cut = std::wstring::npos;
    auto mark = [&](const wchar_t* needle) {
        size_t p = lower.find(needle);
        if (p != std::wstring::npos && (cut == std::wstring::npos || p < cut)) cut = p;
    };
    mark(L"和另外");
    mark(L" - 配置文件");
    mark(L" – 配置文件");
    mark(L" - microsoft edge");
    mark(L" - google chrome");
    mark(L" - mozilla firefox");
    mark(L" - profile ");
    mark(L" and ");
    if (cut != std::wstring::npos) {
        title.resize(cut);
        TrimInPlace(title);
        while (!title.empty() && (title.back() == L'-' || title.back() == L'|' || title.back() == L'_')) {
            title.pop_back();
            TrimInPlace(title);
        }
    }
}

inline bool LooksLikeChannelHandle(const std::wstring& title) {
    int cjk = 0;
    for (wchar_t c : title) {
        if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF)) ++cjk;
    }
    if (cjk >= 2) return false;
    std::wstring n = NormalizeKey(title);
    if (n.empty()) return true;
    std::wstring lower = title;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    if (lower.find(L"official") != std::wstring::npos) return true;
    if (title.find(L'_') != std::wstring::npos &&
        title.find(L" - ") == std::wstring::npos &&
        title.find(L" – ") == std::wstring::npos) {
        return true;
    }
    return false;
}

inline bool LooksLikeBrowserChromeTitle(const std::wstring& title) {
    if (title.empty()) return false;
    std::wstring body = title;
    StripBrowserChromeNoise(body);
    if (body.empty()) return true;
    if (LooksLikeChannelHandle(body)) return true;
    std::wstring n = title;
    for (auto& c : n) c = static_cast<wchar_t>(towlower(c));
    static const wchar_t* exact[] = {
        L"new tab", L"新标签页", L"扩展程序", L"extensions", L"settings", L"设置"
    };
    for (auto j : exact) {
        if (n == j) return true;
    }
    return false;
}

inline bool IsVideoNoiseToken(std::wstring token) {
    TrimInPlace(token);
    if (token.empty()) return true;
    std::wstring n = NormalizeKey(token);
    if (n.empty()) return true;
    static const wchar_t* exact[] = {
        L"4k", L"8k", L"2k", L"hd", L"fhd", L"uhd", L"hdr", L"sdr",
        L"60fps", L"30fps", L"120fps", L"144fps", L"24fps",
        L"1080p", L"720p", L"480p", L"2160p", L"4k60fps", L"4k60",
        L"mv", L"amv", L"pv", L"official", L"lyrics", L"lyric", L"subtitle",
        L"cover", L"live", L"ver", L"version", L"full", L"hq", L"hires",
        L"中字", L"中日", L"歌词", L"字幕", L"高音质", L"无损", L"完整版",
        L"官方", L"现场", L"翻唱", L"自制", L"必剪", L"纯享", L"纯音乐",
        L"动态歌词", L"合唱", L"钢琴", L"演奏", L"封面", L"竖屏", L"横屏",
        L"超清", L"高清", L"蓝光", L"补帧", L"60帧", L"120帧", L"4k修复",
        L"双语", L"中英", L"日字", L"空耳", L"听歌向", L"音质", L"remix",
        L"bilibili", L"youtube", L"哔哩哔哩", L"b站",
        L"ep", L"ost", L"op", L"ed", L"sp", L"album", L"single",
        L"专辑", L"单曲", L"原声", L"主题曲", L"本家", L"カバー", L"cover"
    };
    for (auto w : exact) {
        if (n == w) return true;
    }
    static const wchar_t* part[] = {
        L"歌词", L"字幕", L"fps", L"1080", L"2160", L"高音质", L"完整版",
        L"官方mv", L"中日", L"动态", L"补帧", L"听歌"
    };
    for (auto w : part) {
        if (n.find(w) != std::wstring::npos) return true;
    }
    if (n.size() <= 6) {
        bool digitsOrQuality = true;
        for (wchar_t c : n) {
            if (!(iswdigit(c) || c == L'k' || c == L'p')) {
                digitsOrQuality = false;
                break;
            }
        }
        if (digitsOrQuality) return true;
    }
    return false;
}

inline void CollapseSpaces(std::wstring& s) {
    TrimInPlace(s);
    size_t w = 0;
    bool prevSpace = false;
    for (wchar_t c : s) {
        if (iswspace(c)) {
            if (!prevSpace) s[w++] = L' ';
            prevSpace = true;
        } else {
            s[w++] = c;
            prevSpace = false;
        }
    }
    s.resize(w);
}

inline void StripWrappedNoise(std::wstring& s, wchar_t openCh, wchar_t closeCh, bool stripAllShort) {
    for (int guard = 0; guard < 8; ++guard) {
        size_t a = s.find(openCh);
        if (a == std::wstring::npos) break;
        size_t b = s.find(closeCh, a + 1);
        if (b == std::wstring::npos) break;
        std::wstring inner = s.substr(a + 1, b - a - 1);
        if (IsVideoNoiseToken(inner) || (stripAllShort && inner.size() <= 12)) {
            s.erase(a, b - a + 1);
            continue;
        }
        break;
    }
}

inline void StripAllWrapped(std::wstring& s, wchar_t openCh, wchar_t closeCh) {
    for (int guard = 0; guard < 8; ++guard) {
        size_t a = s.find(openCh);
        if (a == std::wstring::npos) break;
        size_t b = s.find(closeCh, a + 1);
        if (b == std::wstring::npos) break;
        s.erase(a, b - a + 1);
    }
    CollapseSpaces(s);
}

inline void StripSiteSuffixes(std::wstring& title) {
    for (int guard = 0; guard < 8; ++guard) {
        TrimInPlace(title);
        std::wstring lower = title;
        for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
        static const wchar_t* tails[] = {
            L"_哔哩哔哩_bilibili", L"_哔哩哔哩", L"_bilibili", L"_youtube",
            L" - youtube", L" | youtube", L" - bilibili", L" | bilibili",
            L" - 哔哩哔哩弹幕网", L" - 哔哩哔哩", L" | 哔哩哔哩",
            L" - microsoft edge", L" - google chrome"
        };
        bool hit = false;
        for (auto t : tails) {
            const size_t n = wcslen(t);
            if (lower.size() >= n && lower.compare(lower.size() - n, n, t) == 0) {
                title.resize(title.size() - n);
                hit = true;
                break;
            }
        }
        if (!hit) break;
        while (!title.empty() && (title.back() == L'_' || title.back() == L'-' ||
                                  title.back() == L'|' || title.back() == L'·')) {
            title.pop_back();
        }
    }
}

inline void DropNoiseUnderscoreTails(std::wstring& s) {
    for (int guard = 0; guard < 6; ++guard) {
        size_t p = s.find_last_of(L"_-|");
        if (p == std::wstring::npos || p == 0) break;
        std::wstring tail = s.substr(p + 1);
        TrimInPlace(tail);
        if (!IsVideoNoiseToken(tail)) break;
        s.resize(p);
        TrimInPlace(s);
    }
}

inline std::wstring CleanVideoTitle(std::wstring title) {
    TrimInPlace(title);
    StripSiteSuffixes(title);
    DropNoiseUnderscoreTails(title);
    StripAllWrapped(title, L'【', L'】');
    StripAllWrapped(title, L'[', L']');
    StripWrappedNoise(title, L'(', L')', false);
    StripWrappedNoise(title, L'（', L'）', false);
    CollapseSpaces(title);
    DropNoiseUnderscoreTails(title);
    return title;
}

inline std::wstring ExtractWrapped(const std::wstring& s, wchar_t openCh, wchar_t closeCh) {
    size_t a = s.find(openCh);
    if (a == std::wstring::npos) return {};
    size_t b = s.find(closeCh, a + 1);
    if (b == std::wstring::npos || b <= a + 1) return {};
    std::wstring inner = s.substr(a + 1, b - a - 1);
    TrimInPlace(inner);
    if (inner.size() < 1 || IsVideoNoiseToken(inner)) return {};
    return inner;
}

inline void StripTrailingReleaseTags(std::wstring& s) {
    CollapseSpaces(s);
    static const wchar_t* tags[] = {
        L" EP", L" OST", L" OP", L" ED", L" MV", L" PV", L" SP", L" HQ",
        L"专辑", L"单曲", L"原声", L"完整版", L"官方版"
    };
    bool again = true;
    while (again) {
        again = false;
        for (auto tag : tags) {
            size_t n = wcslen(tag);
            if (s.size() > n) {
                std::wstring tail = s.substr(s.size() - n);
                std::wstring want = tag;
                for (auto& c : tail) c = static_cast<wchar_t>(towlower(c));
                for (auto& c : want) c = static_cast<wchar_t>(towlower(c));
                if (tail == want) {
                    s.resize(s.size() - n);
                    TrimInPlace(s);
                    again = true;
                }
            }
        }
        if (!s.empty()) {
            size_t sp = s.find_last_of(L" \t");
            if (sp != std::wstring::npos && sp + 1 < s.size()) {
                std::wstring last = s.substr(sp + 1);
                if (IsVideoNoiseToken(last)) {
                    s.resize(sp);
                    TrimInPlace(s);
                    again = true;
                }
            }
        }
    }
}

inline bool LooksLikeKnownSinger(std::wstring name) {
    TrimInPlace(name);
    std::wstring n = NormalizeKey(name);
    if (n.empty()) return false;
    static const wchar_t* singers[] = {
        L"初音ミク", L"初音未来", L"hatsunemiku", L"miku",
        L"镜音リン", L"鏡音リン", L"镜音レン", L"鏡音レン", L"kagamine",
        L"巡音ルカ", L"巡音流歌", L"megurine", L"luka",
        L"kaito", L"meiko", L"重音テト", L"kasane",
        L"可不", L"kafu", L"星界", L"vocaloid", L"ボーカロイド",
        L"カササギ", L"kasasagi"
    };
    for (auto s : singers) {
        std::wstring k = NormalizeKey(s);
        if (n == k || n.find(k) != std::wstring::npos || k.find(n) != std::wstring::npos) return true;
    }
    return false;
}

inline int CountCjk(const std::wstring& s) {
    int n = 0;
    for (wchar_t c : s) {
        if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF)) ++n;
    }
    return n;
}

inline std::wstring StripFeatSuffix(std::wstring s) {
    TrimInPlace(s);
    std::wstring lower = s;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    const wchar_t* feats[] = {
        L" feat.", L" feat ", L" ft.", L" ft ", L" featuring ",
        L" feat．", L"（feat", L"(feat", L"（ft", L"(ft"
    };
    size_t cut = std::wstring::npos;
    for (auto f : feats) {
        size_t p = lower.find(f);
        if (p != std::wstring::npos && (cut == std::wstring::npos || p < cut)) cut = p;
    }
    if (cut != std::wstring::npos) {
        s.resize(cut);
        TrimInPlace(s);
    }
    return s;
}

inline bool SplitArtistDashSong(const std::wstring& s, std::wstring& song, std::wstring& singer) {
    const wchar_t* seps[] = {L" - ", L" – ", L" — ", L"-"};
    size_t bestPos = std::wstring::npos;
    size_t bestLen = 0;
    for (auto sep : seps) {
        size_t pos = s.find(sep);
        if (pos == std::wstring::npos || pos == 0) continue;
        if (wcscmp(sep, L"-") == 0) {
            if (pos > 0 && (s[pos - 1] == L' ' || iswalnum(s[pos - 1]) || (s[pos - 1] >= 0x4E00 && s[pos - 1] <= 0x9FFF)) &&
                pos + 1 < s.size() && s[pos + 1] != L'-') {
                bestPos = pos;
                bestLen = 1;
                break;
            }
            continue;
        }
        bestPos = pos;
        bestLen = wcslen(sep);
        break;
    }
    if (bestPos == std::wstring::npos) return false;
    singer = s.substr(0, bestPos);
    song = StripFeatSuffix(s.substr(bestPos + bestLen));
    singer = StripFeatSuffix(singer);
    TrimInPlace(singer);
    TrimInPlace(song);
    if (singer.empty() || song.empty()) return false;

    std::wstring rightRaw = song;
    for (auto& c : rightRaw) c = static_cast<wchar_t>(towlower(c));
    const bool rightLooksCover = rightRaw.find(L"cover") != std::wstring::npos ||
                                 rightRaw.find(L"カバー") != std::wstring::npos ||
                                 rightRaw.find(L"歌ってみた") != std::wstring::npos ||
                                 rightRaw.find(L"self") != std::wstring::npos;

    StripAllWrapped(song, L'【', L'】');
    StripAllWrapped(song, L'《', L'》');
    StripAllWrapped(song, L'「', L'」');
    StripAllWrapped(song, L'『', L'』');
    StripAllWrapped(song, L'(', L')');
    StripAllWrapped(song, L'（', L'）');
    StripAllWrapped(song, L'[', L']');
    StripTrailingReleaseTags(song);
    StripTrailingReleaseTags(singer);
    if (LooksLikeKnownSinger(song) && !LooksLikeKnownSinger(singer) && CountCjk(song) <= 8) {
        std::swap(song, singer);
    } else if (rightLooksCover) {
        std::swap(song, singer);
    }
    return !song.empty();
}

inline bool LooksLikeVideoTitle(const std::wstring& title, const std::wstring& appName, const std::wstring& appId) {
    if (title.find(L'【') != std::wstring::npos || title.find(L'《') != std::wstring::npos ||
        title.find(L'「') != std::wstring::npos || title.find(L'『') != std::wstring::npos) {
        return true;
    }
    if (title.find(L" - YouTube") != std::wstring::npos || title.find(L"哔哩哔哩") != std::wstring::npos) {
        return true;
    }
    if (IsBrowserOrVideoApp(appName, appId)) {
        if (title.find(L'[') != std::wstring::npos || title.find(L" - ") != std::wstring::npos || title.size() >= 18) {
            return true;
        }
        std::wstring n = NormalizeKey(title);
        static const wchar_t* hints[] = {
            L"歌词", L"字幕", L"4k", L"1080", L"bilibili", L"youtube", L"完整版", L"官方"
        };
        for (auto h : hints) {
            if (n.find(h) != std::wstring::npos) return true;
        }
    }
    return false;
}

inline bool ExtractOriginalSongName(const std::wstring& raw, std::wstring& song, std::wstring& singer) {
    song.clear();
    singer.clear();
    std::wstring work = raw;
    StripBrowserChromeNoise(work);
    StripSiteSuffixes(work);
    DropNoiseUnderscoreTails(work);
    if (work.empty() || LooksLikeChannelHandle(work)) return false;

    auto takeArtist = [&](const std::wstring& inner) {
        if (inner.empty() || IsVideoNoiseToken(inner)) return;
        if (LooksLikeKnownSinger(inner)) {
            singer = inner;
            return;
        }
        if (singer.empty() && inner.size() >= 2 && inner.size() <= 24) singer = inner;
    };
    for (int guard = 0; guard < 6; ++guard) {
        size_t a = work.find(L'【');
        if (a == std::wstring::npos) break;
        size_t b = work.find(L'】', a + 1);
        if (b == std::wstring::npos) break;
        takeArtist(work.substr(a + 1, b - a - 1));
        work.erase(a, b - a + 1);
    }

    std::wstring cleaned = CleanVideoTitle(work);
    StripAllWrapped(cleaned, L'【', L'】');
    StripAllWrapped(cleaned, L'[', L']');
    StripAllWrapped(cleaned, L'《', L'》');
    CollapseSpaces(cleaned);
    DropNoiseUnderscoreTails(cleaned);
    if (cleaned.empty()) return false;

    song = ExtractWrapped(cleaned, L'「', L'」');
    if (song.empty()) song = ExtractWrapped(cleaned, L'『', L'』');
    if (!song.empty() && song.size() >= 2) {
        size_t dash = cleaned.find(L" - ");
        if (dash == std::wstring::npos) dash = cleaned.find(L" – ");
        if (dash != std::wstring::npos) {
            std::wstring right = cleaned.substr(dash + 3);
            TrimInPlace(right);
            StripAllWrapped(right, L'(', L')');
            StripAllWrapped(right, L'（', L'）');
            StripTrailingReleaseTags(right);
            if (!right.empty() && right.size() <= 28) singer = right;
        }
        return true;
    }
    if (SplitArtistDashSong(cleaned, song, singer) && !song.empty()) {
        song = StripFeatSuffix(song);
        return !song.empty();
    }
    if (!cleaned.empty()) {
        song = StripFeatSuffix(cleaned);
        StripTrailingReleaseTags(song);
        return song.size() >= 2;
    }
    return false;
}

inline bool TitleSearchTokenOk(std::wstring s) {
    TrimInPlace(s);
    s = StripFeatSuffix(s);
    StripTrailingReleaseTags(s);
    if (s.size() < 2 || s.size() > 48) return false;
    if (LooksLikeChannelHandle(s) || LooksLikeBrowserChromeTitle(s) || IsVideoNoiseToken(s)) return false;
    return true;
}

inline void AddTitleSearchToken(std::vector<std::wstring>& out, std::wstring s) {
    TrimInPlace(s);
    s = StripFeatSuffix(s);
    StripTrailingReleaseTags(s);
    StripAllWrapped(s, L'【', L'】');
    StripAllWrapped(s, L'[', L']');
    CollapseSpaces(s);
    if (!TitleSearchTokenOk(s)) return;
    const std::wstring key = NormalizeKey(s);
    for (const auto& e : out) {
        if (NormalizeKey(e) == key) return;
    }
    out.push_back(std::move(s));
}

inline std::vector<std::wstring> CollectTitleSearchKeys(std::wstring raw) {
    std::vector<std::wstring> out;
    StripBrowserChromeNoise(raw);
    StripSiteSuffixes(raw);
    DropNoiseUnderscoreTails(raw);
    if (raw.empty() || LooksLikeChannelHandle(raw)) return out;

    AddTitleSearchToken(out, ExtractWrapped(raw, L'《', L'》'));
    AddTitleSearchToken(out, ExtractWrapped(raw, L'「', L'」'));
    AddTitleSearchToken(out, ExtractWrapped(raw, L'『', L'』'));

    std::wstring cleaned = raw;
    StripAllWrapped(cleaned, L'【', L'】');
    StripAllWrapped(cleaned, L'[', L']');
    CollapseSpaces(cleaned);

    size_t dash = cleaned.find(L" - ");
    if (dash == std::wstring::npos) dash = cleaned.find(L" – ");
    if (dash == std::wstring::npos) dash = cleaned.find(L" — ");
    if (dash != std::wstring::npos && dash > 0 && dash + 3 < cleaned.size()) {
        AddTitleSearchToken(out, cleaned.substr(0, dash));
        AddTitleSearchToken(out, cleaned.substr(dash + 3));
    }
    while (out.size() > 3) out.pop_back();
    return out;
}

inline void GuessSongFromMedia(std::wstring& title, std::wstring& artist,
                               const std::wstring& appName, const std::wstring& appId) {
    if (title.empty()) return;
    const bool videoTitle = LooksLikeVideoTitle(title, appName, appId);
    if (!videoTitle && !IsBrowserOrVideoApp(appName, appId) &&
        title.find(L" - ") == std::wstring::npos && title.find(L'-') == std::wstring::npos) {
        return;
    }
    std::wstring cleaned = CleanVideoTitle(title);
    std::wstring song, singer;

    if (SplitArtistDashSong(cleaned, song, singer)) {
        // Artist - Song《Album》EP
    } else {
        song = ExtractWrapped(cleaned, L'「', L'」');
        if (song.empty()) song = ExtractWrapped(cleaned, L'『', L'』');
        if (song.empty()) song = ExtractWrapped(cleaned, L'《', L'》');
        if (!song.empty() && singer.empty()) {
            size_t mark = cleaned.find(song);
            if (mark != std::wstring::npos && mark > 1) {
                std::wstring before = cleaned.substr(0, mark);
                while (!before.empty() && (before.back() == L'《' || before.back() == L'「' || before.back() == L'『' ||
                                           before.back() == L' ' || before.back() == L'-' || before.back() == L'/' ||
                                           before.back() == L'|')) {
                    before.pop_back();
                }
                TrimInPlace(before);
                if (!before.empty() && before.size() <= 24 && !IsVideoNoiseToken(before)) singer = before;
            }
        }
        if (song.empty() && !cleaned.empty()) {
            song = cleaned;
            StripAllWrapped(song, L'《', L'》');
            StripAllWrapped(song, L'「', L'」');
            StripAllWrapped(song, L'『', L'』');
            StripTrailingReleaseTags(song);
        }
    }

    auto stripWork = [](std::wstring& s) {
        StripWrappedNoise(s, L'『', L'』', true);
        StripWrappedNoise(s, L'「', L'」', true);
        CollapseSpaces(s);
        while (!s.empty() && (s.back() == L'-' || s.back() == L'/' || s.back() == L'|' || s.back() == L'_')) {
            s.pop_back();
            TrimInPlace(s);
        }
    };
    stripWork(song);
    stripWork(singer);

    if (!song.empty()) title = song;
    if (!singer.empty()) {
        artist = singer;
    } else if (videoTitle) {
        std::wstring an = NormalizeKey(artist);
        std::wstring tn = NormalizeKey(title);
        if (!an.empty() && tn.find(an) == std::wstring::npos && an.size() > 2) {
            artist.clear();
        }
    }
}

inline bool IsWeakMatchKey(const std::wstring& n) {
    if (n.size() < 2) return true;
    return IsVideoNoiseToken(n);
}

inline bool TitlesCompatible(const std::wstring& a, const std::wstring& b) {
    const std::wstring na = NormalizeKey(a);
    const std::wstring nb = NormalizeKey(b);
    if (na.empty() || nb.empty()) return false;
    if (IsWeakMatchKey(na) || IsWeakMatchKey(nb)) return na == nb;
    if (na == nb) return true;
    if (na.find(nb) == std::wstring::npos && nb.find(na) == std::wstring::npos) return false;
    const size_t shorter = std::min(na.size(), nb.size());
    const size_t longer = std::max(na.size(), nb.size());
    return shorter >= 2 && shorter * 2 >= longer;
}

inline int ScoreLyricName(const std::wstring& stem, const std::wstring& title, const std::wstring& artist) {
    const std::wstring ns = NormalizeKey(stem);
    const std::wstring nt = NormalizeKey(title);
    const std::wstring na = NormalizeKey(artist);
    if (nt.empty() || ns.empty()) return 0;
    if (IsWeakMatchKey(nt) || IsWeakMatchKey(ns)) {
        return (ns == nt) ? 40 : 0;
    }
    if (ns == nt || ns == na + nt || ns == nt + na) return 100;
    if (!na.empty() && ns.find(nt) != std::wstring::npos && ns.find(na) != std::wstring::npos) return 90;
    if (nt.size() >= 2 && ns.find(nt) != std::wstring::npos) {
        if (TitlesCompatible(stem, title)) return nt.size() >= 4 ? 75 : 65;
        if (CountCjk(title) >= 2) return 65;
        return 0;
    }
    if (ns.size() >= 2 && nt.find(ns) != std::wstring::npos) {
        if (!TitlesCompatible(stem, title)) return 0;
        return 70;
    }
    return 0;
}

inline int ScoreSearchHit(const std::wstring& hitTitle, const std::wstring& hitArtist,
                          const std::wstring& title, const std::wstring& artist) {
    int score = ScoreLyricName(hitTitle, title, artist);
    const std::wstring na = NormalizeKey(hitArtist);
    const std::wstring nb = NormalizeKey(artist);
    const bool artistHit = !na.empty() && !nb.empty() &&
        (na == nb || na.find(nb) != std::wstring::npos || nb.find(na) != std::wstring::npos);
    const bool titleHit = score >= 60 || TitlesCompatible(hitTitle, title);
    // 同歌手不能当歌名。否则 DECO*27 的「メランコリー」会顶替「モニタリング」。
    if (!titleHit) return artistHit ? 10 : 0;
    if (artistHit) score += 25;
    return score;
}

inline void CollectLrcFiles(const std::wstring& root, bool recursive, int cap, std::vector<std::wstring>& out) {
    std::error_code existsEc;
    if (root.empty() || !std::filesystem::exists(root, existsEc)) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (recursive) {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end && static_cast<int>(out.size()) < cap; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            auto ext = it->path().extension().wstring();
            for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
            if (ext == L".lrc" || ext == L".krc") out.push_back(it->path().wstring());
        }
    } else {
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        fs::directory_iterator end;
        for (; it != end && static_cast<int>(out.size()) < cap; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            auto ext = it->path().extension().wstring();
            for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
            if (ext == L".lrc" || ext == L".krc") out.push_back(it->path().wstring());
        }
    }
}

inline std::wstring LyricStemForScore(const std::filesystem::path& p) {
    auto ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext == L".krc") return KrcDisplayStem(p.filename().wstring());
    return p.stem().wstring();
}

inline bool LoadLyricFile(const std::wstring& path, LrcSong& song) {
    auto ext = std::filesystem::path(path).extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext == L".krc") return LoadKrcFile(path, song);
    return LoadLrcFile(path, song);
}

inline bool FindMatchingLrc(const std::wstring& title,
                            const std::wstring& artist,
                            const std::vector<std::wstring>& folders,
                            LrcSong& song) {
    if (title.empty()) return false;

    std::vector<std::wstring> files;
    files.reserve(256);
    for (size_t i = 0; i < folders.size(); ++i) {
        CollectLrcFiles(folders[i], true, 800, files);
    }

    int bestScore = 0;
    std::wstring bestPath;
    for (const auto& path : files) {
        std::filesystem::path p(path);
        int score = ScoreLyricName(LyricStemForScore(p), title, artist);
        if (score > bestScore) {
            bestScore = score;
            bestPath = path;
        }
    }

    if (bestScore < 60) {
        for (const auto& path : files) {
            LrcSong probe;
            if (!LoadLyricFile(path, probe)) continue;
            int score = 0;
            if (!probe.title.empty()) score = std::max(score, ScoreLyricName(probe.title, title, artist));
            if (!probe.artist.empty() && NormalizeKey(probe.artist) == NormalizeKey(artist) &&
                !probe.title.empty() && NormalizeKey(probe.title) == NormalizeKey(title)) {
                score = 100;
            }
            if (score > bestScore) {
                bestScore = score;
                bestPath = path;
            }
            if (bestScore >= 100) break;
        }
    }

    if (bestScore < 60 || bestPath.empty()) return false;
    return LoadLyricFile(bestPath, song);
}
