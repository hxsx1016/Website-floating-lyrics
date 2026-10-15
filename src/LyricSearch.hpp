#pragma once

#include "LrcParser.hpp"
#include "KrcParser.hpp"
#include "LyricLibrary.hpp"

#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

constexpr UINT WM_LYRIC_READY = WM_APP + 2;

struct LyricSearchResult {
    bool ok = false;
    std::wstring title;
    std::wstring artist;
    std::wstring source;
    std::string lrcUtf8;
};

inline std::string UrlEncodeUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            char buf[8]{};
            sprintf_s(buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

inline std::string UnescapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        const char n = s[++i];
        switch (n) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'u':
            if (i + 4 < s.size()) {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return 0;
                };
                const int cp = (hex(s[i + 1]) << 12) | (hex(s[i + 2]) << 8) |
                               (hex(s[i + 3]) << 4) | hex(s[i + 4]);
                i += 4;
                wchar_t w = static_cast<wchar_t>(cp);
                out += WideToUtf8(std::wstring(1, w));
            }
            break;
        default:
            out.push_back(n);
            break;
        }
    }
    return out;
}

inline bool ExtractJsonString(const std::string& json, const char* key, std::string& out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(pat, pos)) != std::string::npos) {
        size_t c = json.find(':', pos + pat.size());
        if (c == std::string::npos) return false;
        size_t i = c + 1;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i + 4 <= json.size() && json.compare(i, 4, "null") == 0) {
            pos = i + 4;
            continue;
        }
        if (i >= json.size() || json[i] != '"') {
            pos = i;
            continue;
        }
        ++i;
        std::string raw;
        while (i < json.size()) {
            if (json[i] == '\\' && i + 1 < json.size()) {
                raw.push_back(json[i]);
                raw.push_back(json[i + 1]);
                i += 2;
                continue;
            }
            if (json[i] == '"') break;
            raw.push_back(json[i]);
            ++i;
        }
        out = UnescapeJsonString(raw);
        return true;
    }
    return false;
}

inline bool ParseJsonNumberAt(const std::string& json, size_t pos, long long& out) {
    size_t c = json.find(':', pos);
    if (c == std::string::npos) return false;
    size_t i = c + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    if (i >= json.size() || (!std::isdigit(static_cast<unsigned char>(json[i])) && json[i] != '-')) {
        return false;
    }
    bool neg = json[i] == '-';
    if (neg) ++i;
    long long v = 0;
    while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) {
        v = v * 10 + (json[i] - '0');
        ++i;
    }
    out = neg ? -v : v;
    return true;
}

inline bool ExtractJsonNumber(const std::string& json, const char* key, long long& out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return false;
    return ParseJsonNumberAt(json, pos + pat.size(), out);
}

inline bool ExtractTopLevelJsonNumber(const std::string& json, const char* key, long long& out) {
    const std::string pat = std::string("\"") + key + "\"";
    int depth = 0;
    bool inStr = false;
    size_t best = std::string::npos;
    for (size_t i = 0; i < json.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(json[i]);
        if (inStr) {
            if (ch == '\\') {
                ++i;
                continue;
            }
            if (ch == '"') inStr = false;
            continue;
        }
        if (ch == '"') {
            if (depth == 1 && json.compare(i, pat.size(), pat) == 0) best = i;
            inStr = true;
            continue;
        }
        if (ch == '{' || ch == '[') ++depth;
        else if (ch == '}' || ch == ']') --depth;
    }
    if (best == std::string::npos) return false;
    return ParseJsonNumberAt(json, best + pat.size(), out);
}

inline bool ExtractJsonScalar(const std::string& json, const char* key, std::string& out) {
    if (ExtractJsonString(json, key, out) && !out.empty()) return true;
    long long n = 0;
    if (ExtractJsonNumber(json, key, n) && n != 0) {
        out = std::to_string(n);
        return true;
    }
    return false;
}

inline std::string StripJsonp(std::string s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    s = s.substr(a, b - a);
    if (s.empty()) return s;
    if (s.front() != '{' && s.front() != '[') {
        const size_t p = s.find('(');
        const size_t e = s.rfind(')');
        if (p != std::string::npos && e != std::string::npos && e > p) {
            s = s.substr(p + 1, e - p - 1);
        }
    }
    return s;
}

inline std::vector<std::string> SplitAllJsonObjects(const std::string& json) {
    std::vector<std::string> objs;
    std::vector<size_t> starts;
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '"') {
            ++i;
            while (i < json.size()) {
                if (json[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (json[i] == '"') break;
                ++i;
            }
            continue;
        }
        if (json[i] == '{') {
            starts.push_back(i);
        } else if (json[i] == '}' && !starts.empty()) {
            const size_t start = starts.back();
            starts.pop_back();
            objs.push_back(json.substr(start, i - start + 1));
        }
    }
    return objs;
}

inline std::vector<std::string> SplitJsonObjects(const std::string& json) {
    std::vector<std::string> objs;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '"') {
            ++i;
            while (i < json.size()) {
                if (json[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (json[i] == '"') break;
                ++i;
            }
            continue;
        }
        if (json[i] == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objs.push_back(json.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objs;
}

inline int CountTimestamps(const std::string& lrc) {
    int n = 0;
    for (size_t i = 0; i + 5 < lrc.size(); ++i) {
        if (lrc[i] == '[' && std::isdigit(static_cast<unsigned char>(lrc[i + 1]))) ++n;
    }
    return n;
}

inline bool HttpGetUtf8(const std::string& url, std::string& body, const char* extraHeaders = nullptr) {
    body.clear();
    HINTERNET session = InternetOpenA(
        "DesktopLyrics/1.0 (Windows desktop lyrics)",
        INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!session) return false;

    DWORD timeout = 4000;
    InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                  INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_COOKIES;
    if (url.rfind("https://", 0) == 0) flags |= INTERNET_FLAG_SECURE;

    HINTERNET req = InternetOpenUrlA(
        session, url.c_str(), extraHeaders,
        extraHeaders ? static_cast<DWORD>(-1) : 0,
        flags, 0);
    if (!req) {
        InternetCloseHandle(session);
        return false;
    }

    char buf[4096];
    DWORD read = 0;
    while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0) {
        body.append(buf, buf + read);
        if (body.size() > 2 * 1024 * 1024) break;
    }
    InternetCloseHandle(req);
    InternetCloseHandle(session);
    return !body.empty();
}

inline bool DecodeBase64(const std::string& in, std::string& out) {
    DWORD len = 0;
    if (!CryptStringToBinaryA(in.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr)) {
        return false;
    }
    out.assign(len, '\0');
    if (!CryptStringToBinaryA(in.c_str(), 0, CRYPT_STRING_BASE64,
                              reinterpret_cast<BYTE*>(out.data()), &len, nullptr, nullptr)) {
        return false;
    }
    out.resize(len);
    return true;
}

inline std::wstring SanitizeFilename(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (wcschr(L"\\/:*?\"<>|", c)) out.push_back(L' ');
        else out.push_back(c);
    }
    TrimInPlace(out);
    while (out.find(L"  ") != std::wstring::npos) {
        auto p = out.find(L"  ");
        out.replace(p, 2, L" ");
    }
    if (out.size() > 80) out.resize(80);
    return out.empty() ? L"lyrics" : out;
}

inline bool SaveUtf8File(const std::wstring& path, const std::string& utf8) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, f);
    fwrite(utf8.data(), 1, utf8.size(), f);
    fclose(f);
    return true;
}

inline bool AcceptLrc(const std::string& lrc, const std::wstring& title, const std::wstring& artist, LrcSong& song) {
    if (CountTimestamps(lrc) < 3) return false;
    song = {};
    if (!LoadLrcFromUtf8(lrc, song) || song.lines.size() < 3) return false;
    if (!song.title.empty()) {
        const bool sameScript = (CountCjk(song.title) >= 2) == (CountCjk(title) >= 2);
        if (sameScript && !TitlesCompatible(song.title, title) &&
            ScoreLyricName(song.title, title, artist) < 60) {
            return false;
        }
    }
    if (song.title.empty()) song.title = title;
    if (song.artist.empty()) song.artist = artist;
    return true;
}

inline bool TryLrclib(const std::wstring& title, const std::wstring& artist, double durationSec,
                      std::string& lrcOut, std::wstring& source) {
    const std::string t = WideToUtf8(title);
    const std::string a = WideToUtf8(artist);
    if (t.empty()) return false;

    auto takeSynced = [&](const std::string& json) -> bool {
        std::string synced;
        if (ExtractJsonString(json, "syncedLyrics", synced) && CountTimestamps(synced) >= 3) {
            lrcOut = synced;
            source = L"lrclib";
            return true;
        }
        return false;
    };

    if (!a.empty()) {
        std::string url = "https://lrclib.net/api/get?track_name=" + UrlEncodeUtf8(t) +
                          "&artist_name=" + UrlEncodeUtf8(a);
        if (durationSec > 10.0) {
            url += "&duration=" + std::to_string(static_cast<int>(durationSec + 0.5));
        }
        std::string body;
        if (HttpGetUtf8(url, body) && takeSynced(body)) return true;
    }

    std::string q = t;
    if (!a.empty()) q += " " + a;
    std::string url = "https://lrclib.net/api/search?q=" + UrlEncodeUtf8(q);
    std::string body;
    if (!HttpGetUtf8(url, body)) return false;

    int best = 0;
    std::string bestLrc;
    for (const auto& obj : SplitJsonObjects(body)) {
        std::string name, ar, synced;
        ExtractJsonString(obj, "trackName", name);
        ExtractJsonString(obj, "artistName", ar);
        if (!ExtractJsonString(obj, "syncedLyrics", synced) || CountTimestamps(synced) < 3) continue;
        const std::wstring hitName = Utf8ToWide(name);
        if (!TitlesCompatible(hitName, title) && ScoreLyricName(hitName, title, artist) < 60) continue;
        int score = ScoreSearchHit(hitName, Utf8ToWide(ar), title, artist);
        if (score > best) {
            best = score;
            bestLrc = std::move(synced);
        }
    }
    if (best >= 50 && !bestLrc.empty()) {
        lrcOut = bestLrc;
        source = L"lrclib";
        return true;
    }
    return false;
}

inline bool TryNetease(const std::wstring& title, const std::wstring& artist,
                       std::string& lrcOut, std::wstring& source) {
    std::string q = WideToUtf8(title);
    if (q.empty()) return false;

    const char* headers =
        "Referer: https://music.163.com/\r\n"
        "Origin: https://music.163.com/\r\n"
        "Accept: application/json\r\n";

    std::string searchUrl =
        "https://music.163.com/api/search/get/web?s=" + UrlEncodeUtf8(q) +
        "&type=1&offset=0&limit=8";
    std::string body;
    if (!HttpGetUtf8(searchUrl, body, headers)) return false;

    int best = 0;
    long long bestId = 0;
    for (const auto& obj : SplitJsonObjects(body)) {
        std::string name;
        if (!ExtractJsonString(obj, "name", name)) continue;
        if (obj.find("\"artists\"") == std::string::npos) continue;
        long long id = 0;
        if (!ExtractTopLevelJsonNumber(obj, "id", id) || id <= 0) continue;
        const std::wstring hitName = Utf8ToWide(name);
        if (!TitlesCompatible(hitName, title) && ScoreLyricName(hitName, title, artist) < 60) continue;
        int score = ScoreSearchHit(hitName, {}, title, artist);
        if (!artist.empty() && obj.find(WideToUtf8(artist)) != std::string::npos) score += 15;
        if (score > best) {
            best = score;
            bestId = id;
        }
    }
    if (best < 50 || bestId <= 0) return false;

    std::string lyricUrl =
        "https://music.163.com/api/song/lyric?os=pc&id=" + std::to_string(bestId) +
        "&lv=-1&kv=-1&tv=-1";
    std::string lyricBody;
    if (!HttpGetUtf8(lyricUrl, lyricBody, headers)) return false;
    std::string lrc;
    if (!ExtractJsonString(lyricBody, "lyric", lrc) || CountTimestamps(lrc) < 3) return false;
    lrcOut = lrc;
    source = L"网易云";
    return true;
}

inline int DurationToMs(long long duration, double fallbackSec) {
    if (duration > 10000) return static_cast<int>(duration);
    if (duration > 0) return static_cast<int>(duration * 1000);
    if (fallbackSec > 10.0) return static_cast<int>(fallbackSec * 1000.0 + 0.5);
    return 0;
}

inline bool KugouDownloadLrc(const std::string& id, const std::string& accesskey, std::string& lrcOut) {
    if (id.empty() || accesskey.empty()) return false;
    const char* headers = "Referer: https://www.kugou.com/\r\n";
    auto fetch = [&](const char* fmt) -> bool {
        std::string url =
            "https://lyrics.kugou.com/download?ver=1&client=pc&fmt=" + std::string(fmt) +
            "&charset=utf8&id=" + UrlEncodeUtf8(id) + "&accesskey=" + UrlEncodeUtf8(accesskey);
        std::string body;
        if (!HttpGetUtf8(url, body, headers)) return false;
        body = StripJsonp(body);
        std::string content;
        if (!ExtractJsonString(body, "content", content) || content.empty()) return false;
        std::string decoded;
        if (!DecodeBase64(content, decoded)) decoded = content;
        if (std::string(fmt) == "krc") {
            std::wstring text;
            if (!DecodeKrcBytes(decoded, text)) return false;
            LrcSong song;
            if (!ParseKrcContent(text, song) || song.lines.size() < 3) return false;
            auto stamp = [](double t, char* buf, size_t n) {
                if (t < 0) t = 0;
                const int mm = static_cast<int>(t / 60.0);
                sprintf_s(buf, n, "%02d:%05.2f", mm, t - mm * 60.0);
            };
            std::string out;
            for (const auto& line : song.lines) {
                if (line.text.empty()) continue;
                char ts[32]{};
                stamp(line.timeSec, ts, sizeof(ts));
                out += "[";
                out += ts;
                out += "]";
                if (!line.words.empty()) {
                    for (const auto& w : line.words) {
                        stamp(w.timeSec, ts, sizeof(ts));
                        out += "<";
                        out += ts;
                        out += ">";
                        out += WideToUtf8(w.text);
                    }
                } else {
                    out += WideToUtf8(line.text);
                }
                out += "\n";
            }
            if (CountTimestamps(out) < 3) return false;
            lrcOut = std::move(out);
            return true;
        }
        std::wstring wide = DecodeLyricBytes(decoded);
        std::string utf8 = WideToUtf8(wide);
        if (CountTimestamps(utf8) < 3) return false;
        lrcOut = utf8;
        return true;
    };
    if (fetch("krc")) return true;
    return fetch("lrc");
}

inline bool KugouSearchCandidates(const std::string& keyword, const std::string& hash, int durationMs,
                                  const std::wstring& title, const std::wstring& artist,
                                  std::string& lrcOut) {
    const char* headers = "Referer: https://www.kugou.com/\r\n";
    std::string url = "https://lyrics.kugou.com/search?ver=1&man=yes&client=pc&keyword=" +
                      UrlEncodeUtf8(keyword);
    if (durationMs > 30000 && durationMs < 8 * 60 * 1000) {
        url += "&duration=" + std::to_string(durationMs);
    }
    if (!hash.empty()) url += "&hash=" + UrlEncodeUtf8(hash);
    std::string body;
    if (!HttpGetUtf8(url, body, headers)) return false;
    body = StripJsonp(body);

    int best = 0;
    std::string bestId, bestKey;
    for (const auto& obj : SplitAllJsonObjects(body)) {
        std::string id, accesskey, song, singer;
        ExtractJsonScalar(obj, "id", id);
        ExtractJsonString(obj, "accesskey", accesskey);
        ExtractJsonString(obj, "song", song);
        ExtractJsonString(obj, "singer", singer);
        if (id.empty() || accesskey.empty()) continue;
        if (song.empty() || id.empty() || accesskey.empty()) continue;
        const std::wstring hitSong = Utf8ToWide(song);
        if (!TitlesCompatible(hitSong, title) && ScoreLyricName(hitSong, title, artist) < 60) continue;
        int score = ScoreSearchHit(hitSong, Utf8ToWide(singer), title, artist);
        if (TitlesCompatible(hitSong, title)) score = std::max(score, 90);
        if (score > best) {
            best = score;
            bestId = std::move(id);
            bestKey = std::move(accesskey);
        }
    }
    if (bestId.empty()) return false;
    if (best < 50) return false;
    return KugouDownloadLrc(bestId, bestKey, lrcOut);
}

inline std::wstring PrimaryArtist(std::wstring artist) {
    TrimInPlace(artist);
    const wchar_t* cuts[] = {L" & ", L"＆", L"/", L"、", L"；", L";", L"feat.", L"Feat."};
    for (auto cut : cuts) {
        size_t p = artist.find(cut);
        if (p != std::wstring::npos) artist = artist.substr(0, p);
    }
    TrimInPlace(artist);
    return artist;
}

inline bool TryKugou(const std::wstring& title, const std::wstring& artist, double durationSec,
                     std::string& lrcOut, std::wstring& source) {
    std::string q = WideToUtf8(title);
    if (q.empty()) return false;
    (void)durationSec;
    auto take = [&](const std::string& keyword, const std::string& hash) {
        if (KugouSearchCandidates(keyword, hash, 0, title, artist, lrcOut)) {
            source = L"酷狗音乐";
            return true;
        }
        return false;
    };

    if (take(q, "")) return true;
    const std::wstring ar = PrimaryArtist(artist);
    if (!ar.empty() && take(WideToUtf8(ar) + " " + q, "")) return true;

    const char* headers = "Referer: https://www.kugou.com/\r\n";
    const std::string searchUrls[] = {
        "https://songsearch.kugou.com/song_search_v2?page=1&pagesize=8&userid=0&clientver=2000&platform=WebFilter&keyword=" + UrlEncodeUtf8(q),
        "https://mobilecdn.kugou.com/api/v3/search/song?format=json&page=1&pagesize=8&keyword=" + UrlEncodeUtf8(q),
    };
    int best = 0;
    std::string bestHash, bestName;
    for (const auto& searchUrl : searchUrls) {
        std::string body;
        if (!HttpGetUtf8(searchUrl, body, headers)) continue;
        body = StripJsonp(body);
        for (const auto& obj : SplitAllJsonObjects(body)) {
            std::string name, singer, hash;
            ExtractJsonString(obj, "SongName", name);
            if (name.empty()) ExtractJsonString(obj, "songname", name);
            if (name.empty()) ExtractJsonString(obj, "OriSongName", name);
            ExtractJsonString(obj, "SingerName", singer);
            if (singer.empty()) ExtractJsonString(obj, "singername", singer);
            ExtractJsonString(obj, "FileHash", hash);
            if (hash.empty()) ExtractJsonString(obj, "hash", hash);
            if (name.empty() || hash.empty()) continue;
            const std::wstring hitName = Utf8ToWide(name);
            if (!TitlesCompatible(hitName, title) && ScoreLyricName(hitName, title, artist) < 60) continue;
            int score = ScoreSearchHit(hitName, Utf8ToWide(singer), title, artist);
            if (score > best) {
                best = score;
                bestHash = hash;
                bestName = name;
            }
        }
        if (best >= 70) break;
    }
    if (!bestHash.empty() && best >= 50) {
        if (take(bestName.empty() ? q : bestName, bestHash)) return true;
        if (take(q, bestHash)) return true;
    }
    return false;
}

inline bool TryQqMusic(const std::wstring& title, const std::wstring& artist,
                       std::string& lrcOut, std::wstring& source) {
    std::string q = WideToUtf8(title);
    if (q.empty()) return false;

    std::string searchUrl =
        "https://c.y.qq.com/soso/fcgi-bin/client_search_cp?format=json&n=8&w=" + UrlEncodeUtf8(q);
    std::string body;
    if (!HttpGetUtf8(searchUrl, body)) return false;

    int best = 0;
    std::string bestMid;
    for (const auto& obj : SplitJsonObjects(body)) {
        std::string name, mid;
        ExtractJsonString(obj, "songname", name);
        ExtractJsonString(obj, "songmid", mid);
        if (name.empty() || mid.empty()) continue;
        const std::wstring hitName = Utf8ToWide(name);
        if (!TitlesCompatible(hitName, title) && ScoreLyricName(hitName, title, artist) < 60) continue;
        int score = ScoreSearchHit(hitName, {}, title, artist);
        if (!artist.empty() && obj.find(WideToUtf8(artist)) != std::string::npos) score += 15;
        if (score > best) {
            best = score;
            bestMid = std::move(mid);
        }
    }
    if (best < 50 || bestMid.empty()) return false;

    std::string lyricUrl =
        "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?format=json&nobase64=0&songmid=" +
        UrlEncodeUtf8(bestMid);
    const char* headers = "Referer: https://y.qq.com/\r\n";
    std::string lyricBody;
    if (!HttpGetUtf8(lyricUrl, lyricBody, headers)) return false;
    std::string b64;
    if (!ExtractJsonString(lyricBody, "lyric", b64) || b64.empty()) return false;
    std::string decoded;
    if (!DecodeBase64(b64, decoded)) return false;
    std::wstring wide = DecodeLyricBytes(decoded);
    std::string utf8 = WideToUtf8(wide);
    if (CountTimestamps(utf8) < 3) return false;
    lrcOut = utf8;
    source = L"QQ音乐";
    return true;
}

inline std::vector<std::pair<std::wstring, std::wstring>> LyricQueryVariants(
    const std::wstring& title, const std::wstring& artist) {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    auto add = [&](std::wstring t, std::wstring a) {
        TrimInPlace(t);
        TrimInPlace(a);
        if (t.empty()) return;
        for (const auto& p : out) {
            if (p.first == t && p.second == a) return;
        }
        out.push_back({std::move(t), std::move(a)});
    };
    const auto keys = CollectTitleSearchKeys(title);
    for (const auto& k : keys) add(k, L"");
    if (out.empty()) add(StripFeatSuffix(title), L"");
    return out;
}

inline bool SearchOnlineLyrics(const std::wstring& title, const std::wstring& artist, double durationSec,
                               LyricSearchResult& result) {
    result = {};
    result.title = title;
    result.artist = artist;
    for (const auto& q : LyricQueryVariants(title, artist)) {
        std::string lrc;
        std::wstring source;
        if (TryKugou(q.first, q.second, durationSec, lrc, source)) {
            LrcSong probe;
            if (!AcceptLrc(lrc, q.first, q.second, probe)) continue;
            result.ok = true;
            result.source = source;
            result.lrcUtf8 = lrc;
            result.title = probe.title.empty() ? q.first : probe.title;
            if (!probe.artist.empty()) result.artist = probe.artist;
            return true;
        }
    }
    return false;
}

class LyricSearcher {
public:
    void SetNotifyHwnd(HWND hwnd) { hwnd_ = hwnd; }

    void Start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread([this] { Run(); });
    }

    void Stop() {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

    void Request(const std::wstring& title, const std::wstring& artist, double durationSec,
                 const std::wstring& altTitle = {}, const std::wstring& altArtist = {}) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            pendingTitle_ = title;
            pendingArtist_ = artist;
            pendingAltTitle_ = altTitle;
            pendingAltArtist_ = altArtist;
            pendingDuration_ = durationSec;
            pending_ = true;
        }
        searching_.store(true);
    }

    bool IsSearching() const { return searching_.load(); }

    LyricSearchResult Consume() {
        std::lock_guard<std::mutex> lock(mu_);
        LyricSearchResult r = result_;
        result_ = {};
        return r;
    }

    ~LyricSearcher() { Stop(); }

private:
    void Run() {
        while (running_) {
            std::wstring title, artist, altTitle, altArtist;
            double duration = 0;
            bool work = false;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (pending_) {
                    title = pendingTitle_;
                    artist = pendingArtist_;
                    altTitle = pendingAltTitle_;
                    altArtist = pendingAltArtist_;
                    duration = pendingDuration_;
                    pending_ = false;
                    work = true;
                }
            }
            if (!work) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }

            LyricSearchResult r;
            SearchOnlineLyrics(title, artist, duration, r);
            if (!r.ok && !altTitle.empty() && NormalizeKey(altTitle) != NormalizeKey(title)) {
                SearchOnlineLyrics(altTitle, altArtist.empty() ? artist : altArtist, duration, r);
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (!pending_) {
                    result_ = r;
                    searching_.store(false);
                    HWND h = hwnd_;
                    if (h) PostMessageW(h, WM_LYRIC_READY, 0, 0);
                }
            }
        }
    }

    HWND hwnd_ = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> searching_{false};
    mutable std::mutex mu_;
    bool pending_ = false;
    std::wstring pendingTitle_;
    std::wstring pendingArtist_;
    std::wstring pendingAltTitle_;
    std::wstring pendingAltArtist_;
    double pendingDuration_ = 0;
    LyricSearchResult result_;
};
