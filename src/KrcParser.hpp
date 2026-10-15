#pragma once

#include "LrcParser.hpp"
#include "ZlibInflate.hpp"

#include <cwctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

inline std::wstring KrcDisplayStem(const std::wstring& filename) {
    std::wstring stem = filename;
    auto dot = stem.rfind(L'.');
    if (dot != std::wstring::npos) stem = stem.substr(0, dot);

    // Artist - Title-<32 hex>-<id>-<pad>
    for (size_t i = 0; i + 33 < stem.size(); ++i) {
        if (stem[i] != L'-') continue;
        bool hex32 = true;
        for (int k = 1; k <= 32; ++k) {
            wchar_t c = stem[i + k];
            const bool ok = (c >= L'0' && c <= L'9') ||
                            (c >= L'a' && c <= L'f') ||
                            (c >= L'A' && c <= L'F');
            if (!ok) {
                hex32 = false;
                break;
            }
        }
        if (hex32 && i + 33 < stem.size() && stem[i + 33] == L'-') {
            return stem.substr(0, i);
        }
    }
    return stem;
}

inline std::wstring StripKrcWordTags(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == L'<') {
            size_t end = raw.find(L'>', i);
            if (end != std::wstring::npos) {
                i = end + 1;
                continue;
            }
        }
        out.push_back(raw[i]);
        ++i;
    }
    TrimInPlace(out);
    return out;
}

inline bool DecodeKrcBytes(const std::string& raw, std::wstring& textOut) {
    if (raw.size() < 8) return false;
    if (raw.compare(0, 4, "krc1") != 0) return false;

    static const unsigned char kKey[16] = {
        0x40, 0x47, 0x61, 0x77, 0x5E, 0x32, 0x74, 0x47,
        0x51, 0x36, 0x31, 0x2D, 0xCE, 0xD2, 0x6E, 0x69
    };
    std::vector<uint8_t> xored(raw.size() - 4);
    for (size_t i = 4; i < raw.size(); ++i) {
        xored[i - 4] = static_cast<uint8_t>(raw[i]) ^ kKey[(i - 4) & 15];
    }
    std::vector<uint8_t> inflated;
    if (!zlib_inflate::InflateZlib(xored.data(), xored.size(), inflated) || inflated.empty()) {
        return false;
    }
    std::string utf8(reinterpret_cast<const char*>(inflated.data()), inflated.size());
    textOut = DecodeLyricBytes(utf8);
    return !textOut.empty();
}

inline bool ParseKrcContent(const std::wstring& content, LrcSong& song) {
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

        if (line.size() >= 4 && line.front() == L'[' && iswalpha(line[1])) {
            size_t close = line.find(L']');
            if (close != std::wstring::npos) {
                std::wstring tag = line.substr(1, close - 1);
                auto colon = tag.find(L':');
                if (colon != std::wstring::npos) {
                    std::wstring key = tag.substr(0, colon);
                    std::wstring val = tag.substr(colon + 1);
                    TrimInPlace(val);
                    if (key == L"ti") song.title = val;
                    else if (key == L"ar") song.artist = val;
                    else if (key == L"al") song.album = val;
                    else if (key == L"offset") {
                        try { song.offsetMs = std::stoi(val); } catch (...) { song.offsetMs = 0; }
                    }
                }
            }
            continue;
        }

        if (line.size() < 5 || line.front() != L'[') continue;
        // [startMs,durationMs]words
        if (!iswdigit(line[1])) continue;
        size_t comma = line.find(L',', 1);
        size_t close = line.find(L']', 1);
        if (comma == std::wstring::npos || close == std::wstring::npos || comma > close) continue;
        long startMs = 0;
        try {
            startMs = std::stol(line.substr(1, comma - 1));
        } catch (...) {
            continue;
        }
        const std::wstring body = line.substr(close + 1);
        LyricLine item;
        item.timeSec = startMs / 1000.0;
        std::wstring plain;
        size_t i = 0;
        while (i < body.size()) {
            if (body[i] == L'<') {
                size_t end = body.find(L'>', i);
                if (end == std::wstring::npos) break;
                std::wstring tag = body.substr(i + 1, end - i - 1);
                long off = 0, dur = 80;
                size_t c1 = tag.find(L',');
                try {
                    if (c1 != std::wstring::npos) {
                        off = std::stol(tag.substr(0, c1));
                        size_t c2 = tag.find(L',', c1 + 1);
                        dur = std::stol(tag.substr(c1 + 1, (c2 == std::wstring::npos) ? std::wstring::npos : c2 - c1 - 1));
                    }
                } catch (...) {
                    off = 0;
                    dur = 80;
                }
                size_t next = body.find(L'<', end + 1);
                if (next == std::wstring::npos) next = body.size();
                std::wstring chunk = body.substr(end + 1, next - end - 1);
                std::vector<wchar_t> letters;
                letters.reserve(chunk.size());
                for (wchar_t ch : chunk) {
                    if (ch == L'\r' || ch == L'\n') continue;
                    letters.push_back(ch);
                }
                const double wordStart = (startMs + off) / 1000.0;
                const double wordDur = std::max(0.04, dur / 1000.0);
                const double letterStep = (letters.empty())
                    ? wordDur
                    : std::clamp(wordDur / static_cast<double>(letters.size()), 0.022, 0.068);
                for (size_t li = 0; li < letters.size(); ++li) {
                    LyricWord w;
                    w.timeSec = wordStart + static_cast<double>(li) * letterStep;
                    w.durationSec = letterStep;
                    w.text = std::wstring(1, letters[li]);
                    item.words.push_back(std::move(w));
                    plain.push_back(letters[li]);
                }
                i = next;
                continue;
            }
            plain.push_back(body[i]);
            ++i;
        }
        TrimInPlace(plain);
        if (plain.empty()) continue;
        if (IsMetaLyricLine(plain)) continue;
        if (IsOpeningTitleLine(item.timeSec, plain)) continue;
        item.text = std::move(plain);
        song.lines.push_back(std::move(item));
    }

    std::sort(song.lines.begin(), song.lines.end(),
              [](const LyricLine& a, const LyricLine& b) { return a.timeSec < b.timeSec; });
    FinalizeLineWords(song);
    if (!song.artist.empty()) {
        for (auto& c : song.artist) {
            if (c == L'/') c = L'、';
        }
    }
    return !song.lines.empty();
}

inline bool LoadKrcFile(const std::wstring& path, LrcSong& song) {
    std::string bytes = ReadFileBytes(path);
    std::wstring text;
    if (!DecodeKrcBytes(bytes, text)) return false;
    song = {};
    song.filePath = path;
    if (!ParseKrcContent(text, song)) return false;
    if (song.title.empty()) song.title = KrcDisplayStem(std::filesystem::path(path).filename().wstring());
    return true;
}
