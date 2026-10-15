#pragma once

#include "LrcParser.hpp"
#include "LyricSearch.hpp"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <wincrypt.h>
#include <wininet.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

constexpr UINT WM_SONG_IDENTIFIED = WM_APP + 3;

struct SongIdentifyResult {
    bool ok = false;
    std::wstring title;
    std::wstring artist;
    std::wstring source;
    double songPositionSec = -1.0;
    ULONGLONG capturedAtMs = 0;
};

inline bool ExtractJsonFloatAfter(const std::string& json, size_t from, const char* key, double& out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat, from);
    if (pos == std::string::npos) return false;
    size_t c = json.find(':', pos + pat.size());
    if (c == std::string::npos) return false;
    size_t i = c + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    if (i >= json.size()) return false;
    char* end = nullptr;
    const double v = std::strtod(json.c_str() + i, &end);
    if (end == json.c_str() + i) return false;
    out = v;
    return true;
}

inline double ParseClockString(const std::string& s) {
    int parts[3] = {0, 0, 0};
    int n = 0;
    int cur = 0;
    int frac = 0, fracDigits = 0;
    bool inFrac = false;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            if (inFrac) {
                if (fracDigits < 3) {
                    frac = frac * 10 + (ch - '0');
                    ++fracDigits;
                }
            } else {
                cur = cur * 10 + (ch - '0');
            }
        } else if (ch == ':' && n < 2 && !inFrac) {
            parts[n++] = cur;
            cur = 0;
        } else if (ch == '.' && !inFrac) {
            inFrac = true;
        }
    }
    parts[n] = cur;
    double sec = 0;
    if (n == 2) sec = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
    else if (n == 1) sec = parts[0] * 60.0 + parts[1];
    else sec = parts[0];
    if (fracDigits == 1) sec += frac / 10.0;
    else if (fracDigits == 2) sec += frac / 100.0;
    else if (fracDigits >= 3) sec += frac / 1000.0;
    return sec;
}

inline uint32_t Crc32Buf(const uint8_t* data, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int b = 0; b < 8; ++b) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
    }
    return ~c;
}

inline std::string Base64Encode(const std::string& in) {
    DWORD len = 0;
    if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(in.data()),
                              static_cast<DWORD>(in.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len)) {
        return {};
    }
    std::string out(len, '\0');
    if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(in.data()),
                              static_cast<DWORD>(in.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &len)) {
        return {};
    }
    while (!out.empty() && (out.back() == '\0' || out.back() == '\r' || out.back() == '\n')) out.pop_back();
    return out;
}

inline std::string MakeUuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> d(0, 15);
    char hex[] = "0123456789ABCDEF";
    std::string s = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    for (char& c : s) {
        if (c == 'x') c = hex[d(gen)];
        else if (c == 'y') c = hex[(d(gen) & 0x3) | 0x8];
    }
    return s;
}

inline bool HttpPostJson(const std::string& host, const std::string& path, const std::string& json,
                         std::string& bodyOut) {
    bodyOut.clear();
    HINTERNET session = InternetOpenA("DesktopLyrics/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!session) return false;
    DWORD timeout = 6000;
    InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET conn = InternetConnectA(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr,
                                      INTERNET_SERVICE_HTTP, 0, 0);
    if (!conn) {
        InternetCloseHandle(session);
        return false;
    }
    const char* accept[] = {"application/json", "*/*", nullptr};
    HINTERNET req = HttpOpenRequestA(conn, "POST", path.c_str(), nullptr, nullptr, accept,
                                     INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                         INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI,
                                     0);
    if (!req) {
        InternetCloseHandle(conn);
        InternetCloseHandle(session);
        return false;
    }
    const char* headers = "Content-Type: application/json\r\n";
    BOOL ok = HttpSendRequestA(req, headers, static_cast<DWORD>(strlen(headers)),
                               const_cast<char*>(json.data()), static_cast<DWORD>(json.size()));
    if (ok) {
        char buf[4096];
        DWORD read = 0;
        while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0) {
            bodyOut.append(buf, buf + read);
            if (bodyOut.size() > 2 * 1024 * 1024) break;
        }
    }
    InternetCloseHandle(req);
    InternetCloseHandle(conn);
    InternetCloseHandle(session);
    return ok && !bodyOut.empty();
}

inline void FftRadix2(std::vector<float>& re, std::vector<float>& im) {
    const int n = static_cast<int>(re.size());
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
        const float wlenRe = std::cos(ang);
        const float wlenIm = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float wRe = 1, wIm = 0;
            for (int k = 0; k < len / 2; ++k) {
                const float uRe = re[i + k];
                const float uIm = im[i + k];
                const float vRe = re[i + k + len / 2] * wRe - im[i + k + len / 2] * wIm;
                const float vIm = re[i + k + len / 2] * wIm + im[i + k + len / 2] * wRe;
                re[i + k] = uRe + vRe;
                im[i + k] = uIm + vIm;
                re[i + k + len / 2] = uRe - vRe;
                im[i + k + len / 2] = uIm - vIm;
                const float nRe = wRe * wlenRe - wIm * wlenIm;
                wIm = wRe * wlenIm + wIm * wlenRe;
                wRe = nRe;
            }
        }
    }
}

struct FrequencyPeak {
    int fftPass = 0;
    int magnitude = 0;
    int freqBin = 0;
};

inline std::string EncodeShazamSignature(int sampleRate, int numberSamples,
                                         const std::vector<FrequencyPeak> bands[4]) {
    std::string contents;
    auto put32 = [&](uint32_t v) {
        contents.push_back(static_cast<char>(v & 255));
        contents.push_back(static_cast<char>((v >> 8) & 255));
        contents.push_back(static_cast<char>((v >> 16) & 255));
        contents.push_back(static_cast<char>((v >> 24) & 255));
    };
    for (int band = 0; band < 4; ++band) {
        if (bands[band].empty()) continue;
        std::string peaks;
        int fftPass = 0;
        auto put16 = [&](uint16_t v) {
            peaks.push_back(static_cast<char>(v & 255));
            peaks.push_back(static_cast<char>((v >> 8) & 255));
        };
        auto put32p = [&](uint32_t v) {
            peaks.push_back(static_cast<char>(v & 255));
            peaks.push_back(static_cast<char>((v >> 8) & 255));
            peaks.push_back(static_cast<char>((v >> 16) & 255));
            peaks.push_back(static_cast<char>((v >> 24) & 255));
        };
        for (const auto& p : bands[band]) {
            if (p.fftPass - fftPass >= 255) {
                peaks.push_back('\xff');
                put32p(static_cast<uint32_t>(p.fftPass));
                fftPass = p.fftPass;
            }
            peaks.push_back(static_cast<char>(p.fftPass - fftPass));
            put16(static_cast<uint16_t>(p.magnitude));
            put16(static_cast<uint16_t>(p.freqBin));
            fftPass = p.fftPass;
        }
        put32(0x60030040u + static_cast<uint32_t>(band));
        put32(static_cast<uint32_t>(peaks.size()));
        contents += peaks;
        while (contents.size() % 4) contents.push_back('\0');
    }

    uint32_t sampleRateId = 3;
    if (sampleRate == 8000) sampleRateId = 1;
    else if (sampleRate == 11025) sampleRateId = 2;
    else if (sampleRate == 16000) sampleRateId = 3;
    else if (sampleRate == 32000) sampleRateId = 4;
    else if (sampleRate == 44100) sampleRateId = 5;
    else if (sampleRate == 48000) sampleRateId = 6;

    std::string header(48, '\0');
    auto w32 = [&](int off, uint32_t v) {
        header[off] = static_cast<char>(v & 255);
        header[off + 1] = static_cast<char>((v >> 8) & 255);
        header[off + 2] = static_cast<char>((v >> 16) & 255);
        header[off + 3] = static_cast<char>((v >> 24) & 255);
    };
    w32(0, 0xCAFE2580);
    w32(8, static_cast<uint32_t>(contents.size() + 8));
    w32(12, 0x94119C00);
    w32(28, sampleRateId << 27);
    w32(40, static_cast<uint32_t>(numberSamples + sampleRate * 0.24));
    w32(44, (15u << 19) + 0x40000);

    std::string tlv;
    auto t32 = [&](uint32_t v) {
        tlv.push_back(static_cast<char>(v & 255));
        tlv.push_back(static_cast<char>((v >> 8) & 255));
        tlv.push_back(static_cast<char>((v >> 16) & 255));
        tlv.push_back(static_cast<char>((v >> 24) & 255));
    };
    t32(0x40000000);
    t32(static_cast<uint32_t>(contents.size() + 8));
    tlv += contents;

    std::string msg = header + tlv;
    uint32_t crc = Crc32Buf(reinterpret_cast<const uint8_t*>(msg.data() + 8), msg.size() - 8);
    msg[4] = static_cast<char>(crc & 255);
    msg[5] = static_cast<char>((crc >> 8) & 255);
    msg[6] = static_cast<char>((crc >> 16) & 255);
    msg[7] = static_cast<char>((crc >> 24) & 255);
    return msg;
}

inline bool BuildShazamSignature(const std::vector<int16_t>& pcm16k, std::string& uriOut, int& sampleMs) {
    uriOut.clear();
    sampleMs = 0;
    if (pcm16k.size() < 16000) return false;

    std::vector<float> hanning(2048);
    for (int i = 0; i < 2048; ++i) {
        hanning[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f * static_cast<float>(i + 1) / 2049.0f));
    }

    std::vector<int16_t> ring(2048, 0);
    int ringPos = 0;
    std::vector<std::vector<float>> fftOut(256, std::vector<float>(1025, 0.0f));
    std::vector<std::vector<float>> spread(256, std::vector<float>(1025, 0.0f));
    int fftPos = 0, spreadPos = 0, fftWritten = 0, spreadWritten = 0;

    std::vector<FrequencyPeak> bands[4];
    const int total = static_cast<int>(pcm16k.size());
    int consumed = 0;
    auto doFft = [&](const int16_t* batch, int n) {
        for (int i = 0; i < n; ++i) {
            ring[ringPos] = batch[i];
            ringPos = (ringPos + 1) % 2048;
        }
        std::vector<float> excerpt(2048);
        for (int i = 0; i < 2048; ++i) excerpt[i] = static_cast<float>(ring[(ringPos + i) % 2048]) * hanning[i];
        std::vector<float> re(2048), im(2048, 0.0f);
        re = excerpt;
        FftRadix2(re, im);
        std::vector<float> mag(1025);
        for (int i = 0; i < 1025; ++i) {
            mag[i] = (re[i] * re[i] + im[i] * im[i]) / static_cast<float>(1 << 17);
            if (mag[i] < 1e-10f) mag[i] = 1e-10f;
        }
        fftOut[fftPos] = mag;
        fftPos = (fftPos + 1) % 256;
        ++fftWritten;

        std::vector<float> spreadLast = mag;
        for (int position = 0; position < 1025; ++position) {
            if (position < 1023) {
                spreadLast[position] = std::max(spreadLast[position],
                                                std::max(spreadLast[position + 1], spreadLast[position + 2]));
            }
            float maxValue = spreadLast[position];
            for (int former : {-1, -3, -6}) {
                int idx = (spreadPos + former) % 256;
                if (idx < 0) idx += 256;
                spread[idx][position] = maxValue = std::max(spread[idx][position], maxValue);
            }
        }
        spread[spreadPos] = spreadLast;
        spreadPos = (spreadPos + 1) % 256;
        ++spreadWritten;

        if (spreadWritten < 46) return;
        const auto& fftMinus46 = fftOut[(fftPos - 46 + 256) % 256];
        const auto& fftMinus49 = spread[(spreadPos - 49 + 256) % 256];
        for (int bin = 10; bin < 1015; ++bin) {
            if (fftMinus46[bin] < 1.0f / 64.0f) continue;
            if (fftMinus46[bin] < fftMinus49[bin - 1]) continue;
            float maxN = 0;
            const int offs[] = {-10, -7, -4, -3, 1, 2, 5, 8};
            for (int o : offs) maxN = std::max(maxN, fftMinus49[bin + o]);
            if (fftMinus46[bin] <= maxN) continue;
            float maxO = maxN;
            const int toffs[] = {-53, -45, 165, 172, 179, 186, 193, 200, 214, 221, 228, 235, 242, 249};
            for (int o : toffs) {
                int idx = (spreadPos + o) % 256;
                if (idx < 0) idx += 256;
                maxO = std::max(maxO, spread[idx][bin - 1]);
            }
            if (fftMinus46[bin] <= maxO) continue;
            const int fftNumber = spreadWritten - 46;
            auto magLog = [](float v) { return std::log(std::max(1.0f / 64.0f, v)) * 1477.3f + 6144.0f; };
            const float peak = magLog(fftMinus46[bin]);
            const float before = magLog(fftMinus46[bin - 1]);
            const float after = magLog(fftMinus46[bin + 1]);
            const float var1 = peak * 2 - before - after;
            if (var1 <= 0) continue;
            const float var2 = (after - before) * 32.0f / var1;
            const float corrected = bin * 64.0f + var2;
            const float hz = corrected * (16000.0f / 2.0f / 1024.0f / 64.0f);
            int band = -1;
            if (hz >= 250 && hz < 520) band = 0;
            else if (hz < 1450) band = 1;
            else if (hz < 3500) band = 2;
            else if (hz <= 5500) band = 3;
            if (band < 0) continue;
            bands[band].push_back({fftNumber, static_cast<int>(peak), static_cast<int>(corrected)});
        }
    };

    while (consumed + 128 <= total) {
        doFft(pcm16k.data() + consumed, 128);
        consumed += 128;
        int peaks = 0;
        for (int i = 0; i < 4; ++i) peaks += static_cast<int>(bands[i].size());
        if (consumed >= 16000 * 4 && peaks >= 80) break;
        if (consumed >= 16000 * 8) break;
    }
    int peaks = 0;
    for (int i = 0; i < 4; ++i) peaks += static_cast<int>(bands[i].size());
    if (peaks < 20) return false;

    const std::string raw = EncodeShazamSignature(16000, consumed, bands);
    uriOut = "data:audio/vnd.shazam.sig;base64," + Base64Encode(raw);
    sampleMs = static_cast<int>(consumed * 1000.0 / 16000.0);
    return !uriOut.empty();
}

inline bool CaptureLoopbackPcm16k(std::vector<int16_t>& pcm, int seconds) {
    pcm.clear();
    IMMDeviceEnumerator* enumer = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumer));
    if (FAILED(hr) || !enumer) return false;
    IMMDevice* device = nullptr;
    hr = enumer->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    enumer->Release();
    if (FAILED(hr) || !device) return false;

    IAudioClient* client = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
    device->Release();
    if (FAILED(hr) || !client) return false;

    WAVEFORMATEX* mix = nullptr;
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        client->Release();
        return false;
    }
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10 * 1000 * 1000, 0, mix, nullptr);
    if (FAILED(hr)) {
        CoTaskMemFree(mix);
        client->Release();
        return false;
    }
    IAudioCaptureClient* cap = nullptr;
    hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cap));
    if (FAILED(hr) || !cap) {
        CoTaskMemFree(mix);
        client->Release();
        return false;
    }
    client->Start();

    const int srcRate = mix->nSamplesPerSec ? mix->nSamplesPerSec : 48000;
    const int ch = mix->nChannels ? mix->nChannels : 2;
    const int bits = mix->wBitsPerSample;
    bool isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix);
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    const int want = 16000 * seconds;
    pcm.reserve(want + 16000);
    const double step = static_cast<double>(srcRate) / 16000.0;
    std::vector<float> mono;
    mono.reserve(srcRate * seconds + 1024);

    const ULONGLONG startTick = GetTickCount64();
    const ULONGLONG deadline = startTick + static_cast<ULONGLONG>(seconds * 1000 + 400);
    bool heard = false;
    while (GetTickCount64() < deadline && static_cast<int>(mono.size()) < srcRate * seconds + srcRate / 4) {
        UINT32 packet = 0;
        if (FAILED(cap->GetNextPacketSize(&packet))) break;
        if (!packet) {
            Sleep(4);
            continue;
        }
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && frames) {
            for (UINT32 i = 0; i < frames; ++i) {
                float s = 0;
                if (isFloat) {
                    const float* f = reinterpret_cast<const float*>(data);
                    for (int c = 0; c < ch; ++c) s += f[i * ch + c];
                    s /= static_cast<float>(ch);
                } else if (bits == 16) {
                    const int16_t* p = reinterpret_cast<const int16_t*>(data);
                    for (int c = 0; c < ch; ++c) s += p[i * ch + c] / 32768.0f;
                    s /= static_cast<float>(ch);
                } else if (bits == 32) {
                    const int32_t* p = reinterpret_cast<const int32_t*>(data);
                    for (int c = 0; c < ch; ++c) s += p[i * ch + c] / 2147483648.0f;
                    s /= static_cast<float>(ch);
                }
                if (!heard) {
                    if (std::fabs(s) > 0.012f) heard = true;
                    else if (GetTickCount64() - startTick < 700) continue;
                    else heard = true;
                }
                mono.push_back(s);
            }
        }
        cap->ReleaseBuffer(frames);
    }
    client->Stop();
    cap->Release();
    client->Release();
    CoTaskMemFree(mix);

    if (mono.size() < static_cast<size_t>(srcRate / 3)) return false;
    double resamplePos = 0;
    while (static_cast<int>(pcm.size()) < want && resamplePos + 1.0 < static_cast<double>(mono.size())) {
        const int i = static_cast<int>(resamplePos);
        const float t = static_cast<float>(resamplePos - i);
        const float s = mono[i] * (1.0f - t) + mono[i + 1] * t;
        float v = s * 32767.0f;
        v = std::clamp(v, -32767.0f, 32767.0f);
        pcm.push_back(static_cast<int16_t>(v));
        resamplePos += step;
    }
    double energy = 0;
    for (int16_t s : pcm) energy += static_cast<double>(s) * s;
    energy = std::sqrt(energy / std::max<size_t>(1, pcm.size()));
    return pcm.size() >= 12000 && energy > 18.0;
}

inline bool RecognizeFromPcm(const std::vector<int16_t>& pcm, SongIdentifyResult& result) {
    result = {};
    std::string uri;
    int sampleMs = 0;
    if (!BuildShazamSignature(pcm, uri, sampleMs)) return false;

    const std::string uuid1 = MakeUuid();
    const std::string uuid2 = MakeUuid();
    const std::string path = "/discovery/v5/zh/CN/android/-/tag/" + uuid1 + "/" + uuid2 +
                             "?sync=true&webv3=true&sampling=true&connected=&shazamapiversion=v3&sharehub=true&video=v3";
    const unsigned long long ts = static_cast<unsigned long long>(time(nullptr)) * 1000ULL;
    std::ostringstream json;
    json << "{\"signature\":{\"samplems\":" << sampleMs
         << ",\"timestamp\":" << ts
         << ",\"uri\":\"" << uri
         << "\"},\"timestamp\":" << ts
         << ",\"timezone\":\"Asia/Shanghai\"}";
    std::string body;
    if (!HttpPostJson("amp.shazam.com", path, json.str(), body)) return false;

    std::string title, artist;
    const size_t track = body.find("\"track\"");
    const std::string slice = (track == std::string::npos) ? body : body.substr(track, 8000);
    ExtractJsonString(slice, "title", title);
    ExtractJsonString(slice, "subtitle", artist);
    if (title.empty()) return false;
    result.ok = true;
    result.title = Utf8ToWide(title);
    result.artist = Utf8ToWide(artist);
    result.source = L"识曲";
    TrimInPlace(result.title);
    TrimInPlace(result.artist);

    double offset = -1.0;
    const size_t matches = body.find("\"matches\"");
    if (matches != std::string::npos) ExtractJsonFloatAfter(body, matches, "offset", offset);
    if (offset < 0.0) ExtractJsonFloatAfter(body, 0, "offset", offset);
    if (offset > 10000.0) offset /= 1000.0;
    if (offset < 0.0) {
        std::string tc;
        if (ExtractJsonString(body, "timecode", tc) || ExtractJsonString(slice, "timecode", tc)) {
            offset = ParseClockString(tc);
        }
    }
    if (offset >= 0.0 && offset < 24 * 3600.0) result.songPositionSec = offset;
    return true;
}

class SongIdentifier {
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

    void Request(const std::wstring& videoKey) {
        std::lock_guard<std::mutex> lock(mu_);
        pendingKey_ = videoKey;
        pending_ = true;
        searching_.store(true);
    }

    bool IsSearching() const { return searching_.load(); }

    SongIdentifyResult Consume() {
        std::lock_guard<std::mutex> lock(mu_);
        SongIdentifyResult r = result_;
        result_ = {};
        return r;
    }

    ~SongIdentifier() { Stop(); }

private:
    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (running_) {
            std::wstring key;
            bool work = false;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (pending_) {
                    key = pendingKey_;
                    pending_ = false;
                    work = true;
                }
            }
            if (!work) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }

            SongIdentifyResult r;
            const ULONGLONG capStart = GetTickCount64();
            auto tryOnce = [&](int seconds) {
                std::vector<int16_t> pcm;
                if (!CaptureLoopbackPcm16k(pcm, seconds)) return false;
                if (!RecognizeFromPcm(pcm, r)) return false;
                r.capturedAtMs = capStart;
                return r.ok;
            };
            if (!tryOnce(3)) tryOnce(4);
            r.ok = r.ok && !r.title.empty();
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (!pending_ || pendingKey_ == key) {
                    result_ = r;
                    resultKey_ = key;
                    searching_.store(false);
                    HWND h = hwnd_;
                    if (h) PostMessageW(h, WM_SONG_IDENTIFIED, 0, 0);
                }
            }
        }
        CoUninitialize();
    }

    HWND hwnd_ = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> searching_{false};
    mutable std::mutex mu_;
    bool pending_ = false;
    std::wstring pendingKey_;
    std::wstring resultKey_;
    SongIdentifyResult result_;
};
