#pragma once

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct LyricWord {
    double timeSec = 0.0;
    double durationSec = 0.0;
    std::wstring text;
};

struct LyricLine {
    double timeSec = 0.0;
    std::wstring text;
    std::vector<LyricWord> words;
};

struct LrcSong {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    int offsetMs = 0;
    std::vector<LyricLine> lines;
    std::wstring filePath;
};

inline std::wstring MultiByteToWide(UINT codePage, const char* data, int bytes, DWORD flags = 0) {
    if (!data || bytes <= 0) return {};
    int n = MultiByteToWideChar(codePage, flags, data, bytes, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(codePage, flags, data, bytes, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

inline std::wstring DecodeLyricBytes(const std::string& raw) {
    const char* p = raw.data();
    int n = static_cast<int>(raw.size());
    if (n >= 3 &&
        static_cast<unsigned char>(p[0]) == 0xEF &&
        static_cast<unsigned char>(p[1]) == 0xBB &&
        static_cast<unsigned char>(p[2]) == 0xBF) {
        p += 3;
        n -= 3;
        return MultiByteToWide(CP_UTF8, p, n);
    }
    if (n >= 2 &&
        static_cast<unsigned char>(p[0]) == 0xFF &&
        static_cast<unsigned char>(p[1]) == 0xFE) {
        return std::wstring(reinterpret_cast<const wchar_t*>(p + 2),
                            static_cast<size_t>((n - 2) / 2));
    }

    std::wstring utf8 = MultiByteToWide(CP_UTF8, p, n, MB_ERR_INVALID_CHARS);
    if (!utf8.empty() || n == 0) return utf8;
    return MultiByteToWide(CP_ACP, p, n);
}

inline std::string ReadFileBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline void StripEnhancedTags(std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] == L'<') {
            size_t end = text.find(L'>', i);
            if (end != std::wstring::npos) {
                i = end + 1;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    text.swap(out);
}



inline void TrimInPlace(std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && iswspace(s[a])) ++a;
    size_t b = s.size();
    while (b > a && iswspace(s[b - 1])) --b;
    s = s.substr(a, b - a);
}

inline bool StartsWithFold(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (towlower(s[i]) != towlower(prefix[i])) return false;
    }
    return true;
}

inline bool IsMetaLyricLine(std::wstring text) {
    TrimInPlace(text);
    if (text.empty()) return true;
    static const wchar_t* colonHeads[] = {
        L"作词", L"作曲", L"编曲", L"作詞", L"詞", L"曲", L"编", L"編曲", L"編",
        L"演唱", L"歌手", L"演唱者", L"原唱", L"翻唱", L"混音", L"混缩", L"制作人",
        L"监制", L"出品", L"出品人", L"录音", L"和声", L"吉他", L"贝斯", L"鼓",
        L"弦乐", L"封面", L"插画", L"策划", L"制作", L"特别鸣谢", L"词", L"OP", L"ED"
    };
    for (auto h : colonHeads) {
        const size_t n = wcslen(h);
        if (text.size() > n && StartsWithFold(text, h) &&
            (text[n] == L':' || text[n] == L'：')) {
            return true;
        }
    }
    static const wchar_t* enHeads[] = {
        L"Lyrics by", L"Lyricist", L"Composed by", L"Composer",
        L"Arranged by", L"Arranger", L"Produced by", L"Producer",
        L"Music by", L"Mix by", L"Written by", L"Recorded by"
    };
    for (auto h : enHeads) {
        if (StartsWithFold(text, h)) return true;
    }
    if (text.find(L"未经著作权人许可") != std::wstring::npos) return true;
    return false;
}

inline bool IsOpeningTitleLine(double timeSec, const std::wstring& text) {
    if (timeSec > 1.05) return false;
    if (text.size() < 3 || text.size() > 80) return false;
    return text.find(L" - ") != std::wstring::npos ||
           text.find(L" – ") != std::wstring::npos;
}

inline bool ParseTimestamp(const std::wstring& tag, double& secOut) {
    // mm:ss.xx  or mm:ss.xxx  or mm:ss
    int mm = 0, ss = 0, frac = 0, fracDigits = 0;
    size_t i = 0;
    auto readInt = [&](int& v) {
        if (i >= tag.size() || !iswdigit(tag[i])) return false;
        v = 0;
        while (i < tag.size() && iswdigit(tag[i])) {
            v = v * 10 + (tag[i] - L'0');
            ++i;
        }
        return true;
    };

    if (!readInt(mm)) return false;
    if (i >= tag.size() || tag[i] != L':') return false;
    ++i;
    if (!readInt(ss)) return false;
    if (i < tag.size() && (tag[i] == L'.' || tag[i] == L':')) {
        ++i;
        while (i < tag.size() && iswdigit(tag[i]) && fracDigits < 3) {
            frac = frac * 10 + (tag[i] - L'0');
            ++fracDigits;
            ++i;
        }
    }
    if (i != tag.size()) return false;

    double fracSec = 0.0;
    if (fracDigits == 1) fracSec = frac / 10.0;
    else if (fracDigits == 2) fracSec = frac / 100.0;
    else if (fracDigits == 3) fracSec = frac / 1000.0;
    secOut = mm * 60.0 + ss + fracSec;
    return true;
}

inline bool ParseEnhancedWords(const std::wstring& raw, double lineStart, std::vector<LyricWord>& words, std::wstring& plain) {
    words.clear();
    plain.clear();
    size_t i = 0;
    double lastTime = lineStart;
    bool hasTag = false;
    while (i < raw.size()) {
        if (raw[i] == L'<') {
            size_t end = raw.find(L'>', i);
            if (end == std::wstring::npos) break;
            std::wstring tag = raw.substr(i + 1, end - i - 1);
            double t = 0;
            if (ParseTimestamp(tag, t)) {
                lastTime = t;
                hasTag = true;
            } else {
                try {
                    lastTime = lineStart + std::stod(tag) / 1000.0;
                    hasTag = true;
                } catch (...) {
                }
            }
            i = end + 1;
            continue;
        }
        size_t next = raw.find(L'<', i);
        if (next == std::wstring::npos) next = raw.size();
        std::wstring chunk = raw.substr(i, next - i);
        for (wchar_t ch : chunk) {
            if (ch == L'\r' || ch == L'\n') continue;
            LyricWord w;
            w.timeSec = lastTime;
            w.text = std::wstring(1, ch);
            words.push_back(std::move(w));
            plain.push_back(ch);
        }
        i = next;
    }
    TrimInPlace(plain);
    return hasTag && !words.empty();
}

inline void StaggerLetterTimes(LyricLine& line, double endSec) {
    if (line.words.size() < 2) return;
    size_t i = 0;
    while (i < line.words.size()) {
        size_t j = i + 1;
        while (j < line.words.size() &&
               std::abs(line.words[j].timeSec - line.words[i].timeSec) < 0.012) {
            ++j;
        }
        const size_t count = j - i;
        if (count >= 2) {
            const double base = line.words[i].timeSec;
            const double nextT = (j < line.words.size()) ? line.words[j].timeSec : endSec;
            double clusterDur = nextT - base;
            if (clusterDur < 0.06) clusterDur = std::max(0.06, line.words[i].durationSec);
            clusterDur = std::clamp(clusterDur, 0.032 * static_cast<double>(count), 0.086 * static_cast<double>(count));
            const double step = clusterDur / static_cast<double>(count);
            for (size_t k = 0; k < count; ++k) {
                line.words[i + k].timeSec = base + static_cast<double>(k) * step;
                line.words[i + k].durationSec = step;
            }
        }
        i = j;
    }
}

inline void EnsureLineWords(LyricLine& line, double endSec) {
    if (!line.words.empty()) {
        for (size_t i = 0; i < line.words.size(); ++i) {
            double next = (i + 1 < line.words.size()) ? line.words[i + 1].timeSec : endSec;
            if (next <= line.words[i].timeSec) next = line.words[i].timeSec + 0.08;
            if (line.words[i].durationSec < 0.03) {
                line.words[i].durationSec = std::max(0.04, next - line.words[i].timeSec);
            }
        }
        StaggerLetterTimes(line, endSec);
        return;
    }
    const double dur = std::max(0.28, (endSec - line.timeSec) * 0.72);
    size_t n = 0;
    for (wchar_t ch : line.text) {
        if (!iswspace(ch)) ++n;
    }
    if (n == 0) n = std::max<size_t>(1, line.text.size());
    const double step = dur / static_cast<double>(n);
    double t = line.timeSec;
    for (wchar_t ch : line.text) {
        LyricWord w;
        w.timeSec = t;
        w.durationSec = iswspace(ch) ? step * 0.35 : step;
        w.text = std::wstring(1, ch);
        line.words.push_back(std::move(w));
        t += w.durationSec;
    }
}

inline void FinalizeLineWords(LrcSong& song) {
    for (size_t i = 0; i < song.lines.size(); ++i) {
        double end = song.lines[i].timeSec + 4.0;
        if (i + 1 < song.lines.size()) end = song.lines[i + 1].timeSec;
        EnsureLineWords(song.lines[i], end);
    }
}

inline bool ParseLrcContent(const std::wstring& content, LrcSong& song) {
    song.lines.clear();
    song.title.clear();
    song.artist.clear();
    song.album.clear();
    song.offsetMs = 0;

    std::wistringstream stream(content);
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        TrimInPlace(line);
        if (line.empty()) continue;

        std::vector<double> times;
        size_t pos = 0;
        while (pos < line.size() && line[pos] == L'[') {
            size_t close = line.find(L']', pos);
            if (close == std::wstring::npos) break;
            std::wstring tag = line.substr(pos + 1, close - pos - 1);
            pos = close + 1;

            auto colon = tag.find(L':');
            if (colon != std::wstring::npos && colon > 0 && !iswdigit(tag[0])) {
                std::wstring key = tag.substr(0, colon);
                std::wstring val = tag.substr(colon + 1);
                TrimInPlace(val);
                if (key == L"ti") song.title = val;
                else if (key == L"ar") song.artist = val;
                else if (key == L"al") song.album = val;
                else if (key == L"offset") {
                    try {
                        song.offsetMs = std::stoi(val);
                    } catch (...) {
                        song.offsetMs = 0;
                    }
                }
                continue;
            }

            double t = 0;
            if (ParseTimestamp(tag, t)) times.push_back(t);
        }

        std::wstring raw = line.substr(pos);
        TrimInPlace(raw);
        std::wstring plain;
        std::vector<LyricWord> words;
        const double lineStart = times.empty() ? 0.0 : times.front();
        if (!ParseEnhancedWords(raw, lineStart, words, plain)) {
            plain = raw;
            StripEnhancedTags(plain);
            TrimInPlace(plain);
            words.clear();
        }
        if (IsMetaLyricLine(plain)) continue;
        for (double t : times) {
            if (IsOpeningTitleLine(t, plain)) continue;
            LyricLine item;
            item.timeSec = t;
            item.text = plain;
            item.words = words;
            if (!item.words.empty() && std::abs(item.words.front().timeSec - t) > 2.0) {
                const double delta = t - lineStart;
                for (auto& w : item.words) w.timeSec += delta;
            }
            song.lines.push_back(std::move(item));
        }
    }

    std::sort(song.lines.begin(), song.lines.end(),
              [](const LyricLine& a, const LyricLine& b) { return a.timeSec < b.timeSec; });
    FinalizeLineWords(song);
    return !song.lines.empty() || !song.title.empty();
}

inline bool LoadLrcFromUtf8(const std::string& bytes, LrcSong& song) {
    if (bytes.empty()) return false;
    std::wstring content = DecodeLyricBytes(bytes);
    const std::wstring pathKeep = song.filePath;
    bool ok = ParseLrcContent(content, song);
    song.filePath = pathKeep;
    return ok;
}

inline bool LoadLrcFile(const std::wstring& path, LrcSong& song) {
    std::string bytes = ReadFileBytes(path);
    if (bytes.empty()) return false;
    song = {};
    song.filePath = path;
    return LoadLrcFromUtf8(bytes, song);
}

inline std::wstring Utf8ToWide(const std::string& utf8) {
    return MultiByteToWide(CP_UTF8, utf8.data(), static_cast<int>(utf8.size()));
}

inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

inline int FindLyricIndex(const LrcSong& song, double timeSec) {
    if (song.lines.empty()) return -1;
    double t = timeSec + song.offsetMs / 1000.0;
    int idx = -1;
    for (int i = 0; i < static_cast<int>(song.lines.size()); ++i) {
        if (song.lines[i].timeSec <= t + 1e-6) idx = i;
        else break;
    }
    return idx;
}

inline double SongDuration(const LrcSong& song) {
    if (song.lines.empty()) return 0.0;
    return song.lines.back().timeSec + 5.0;
}
