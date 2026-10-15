#pragma once

#include <windows.h>
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include "KugouSession.hpp"
#include "LyricLibrary.hpp"
#include "PlayerWindows.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>

struct MediaNowPlaying {
    bool available = false;
    bool playing = false;
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring sessionTitle;
    std::wstring appId;
    std::wstring appName;
    double positionSec = 0.0;
    double rawPositionSec = -1.0;
    double durationSec = 0.0;
    ULONGLONG stampMs = 0;
    bool fromBiliBar = false;
    bool fromKugou = false;
    bool fromKugouIdentify = false;
    bool waitingKugouIdentify = false;
    bool positionReliable = true;
};

inline std::wstring FriendlyPlayerName(std::wstring id) {
    std::wstring lower = id;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    auto has = [&](const wchar_t* s) { return lower.find(s) != std::wstring::npos; };
    if (has(L"spotify")) return L"Spotify";
    if (has(L"cloudmusic") || has(L"netease") || has(L"orpheus") || has(L"neteasemusic")) return L"网易云音乐";
    if (has(L"qqmusic") || has(L"qq音乐") || has(L"qqmusic")) return L"QQ音乐";
    if (has(L"kugou")) return L"酷狗音乐";
    if (has(L"kuwo")) return L"酷我音乐";
    if (has(L"applemusic") || has(L"itunes")) return L"Apple Music";
    if (has(L"chrome")) return L"Chrome";
    if (has(L"msedge") || has(L"edge")) return L"Edge";
    if (has(L"firefox")) return L"Firefox";
    if (has(L"zunemusic") || has(L"mediaplayer")) return L"媒体播放器";
    if (has(L"potplayer")) return L"PotPlayer";
    if (has(L"vlc")) return L"VLC";
    if (has(L"foobar")) return L"foobar2000";
    if (has(L"aimp")) return L"AIMP";
    if (has(L"musicbee")) return L"MusicBee";
    if (has(L"yesplaymusic")) return L"YesPlayMusic";
    if (has(L"lx-music") || has(L"lxmusic")) return L"洛雪音乐";
    if (has(L"spotifyab")) return L"Spotify";

    auto cut = id.find_last_of(L"\\/");
    if (cut != std::wstring::npos) id = id.substr(cut + 1);
    auto bang = id.find(L'!');
    if (bang != std::wstring::npos) id = id.substr(0, bang);
    auto dot = id.rfind(L'.');
    if (dot != std::wstring::npos && dot > 0) id = id.substr(0, dot);
    return id.empty() ? L"播放器" : id;
}

inline void MaybeSplitTitleArtist(std::wstring& title, std::wstring& artist) {
    if (title.empty()) return;
    std::wstring song, singer;
    if (!SplitArtistDashSong(title, song, singer)) return;
    if (!song.empty()) title = song;
    if (artist.empty() && !singer.empty()) artist = singer;
}

inline void StripAudioExtension(std::wstring& title) {
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

class MediaMonitor {
public:
    void Start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread([this] { Run(); });
    }

    void Stop() {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

    MediaNowPlaying Get() const {
        std::lock_guard<std::mutex> lock(mu_);
        return current_;
    }

    void RequestToggle() { command_.store(1); }
    void RequestPlay() { command_.store(2); }
    void RequestPause() { command_.store(3); }
    void RequestSkipPrevious() { command_.store(4); }
    void RequestSkipNext() { command_.store(5); }
    void OverridePosition(double sec) {
        std::lock_guard<std::mutex> lock(mu_);
        overridePos_ = std::max(0.0, sec);
        hasOverride_ = true;
        hasSeek_ = true;
    }

    ~MediaMonitor() { Stop(); }

private:
    void Publish(const MediaNowPlaying& np) {
        std::lock_guard<std::mutex> lock(mu_);
        current_ = np;
    }

    static double TimeSpanToSec(const winrt::Windows::Foundation::TimeSpan& ts) {
        return std::chrono::duration<double>(ts).count();
    }

    void Run() {
        using namespace winrt::Windows::Media::Control;

        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            running_ = false;
            return;
        }

        int missTicks = 0;
        std::wstring kugouTrack;
        double kugouHead = 0.0;
        ULONGLONG kugouTick = GetTickCount64();
        int kugouSilent = 0;
        bool kugouPlayingLatch = false;
        ULONGLONG lastIniWrite = 0;
        double lastSavedPos = -1.0;
        std::wstring smtcTrack;
        double smtcHead = 0.0;
        double smtcRaw = -1.0;
        ULONGLONG smtcStamp = GetTickCount64();
        bool videoPlayingLatch = false;
        int videoPauseTicks = 0;
        while (running_) {
            double overridePos = -1.0;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (hasOverride_) {
                    overridePos = overridePos_;
                    hasOverride_ = false;
                }
            }
            MediaNowPlaying np;
            winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session{nullptr};
            try {
                auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                session = manager.GetCurrentSession();

                auto sessions = manager.GetSessions();
                winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession musicSession{nullptr};
                winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession videoSession{nullptr};
                winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession playingSession{nullptr};
                for (auto const& s : sessions) {
                    auto info = s.GetPlaybackInfo();
                    auto status = info.PlaybackStatus();
                    std::wstring appId = s.SourceAppUserModelId().c_str();
                    const std::wstring appName = FriendlyPlayerName(appId);
                    const bool music = IsMusicPlayerApp(appName, appId);
                    const bool video = IsBrowserOrVideoApp(appName, appId) && !music;
                    const bool playing = (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
                    if (playing && video && !videoSession) videoSession = s;
                    if (playing && music && !musicSession) musicSession = s;
                    if (playing && !playingSession) playingSession = s;
                    if (music && !musicSession) musicSession = s;
                }
                if (videoSession) session = videoSession;
                else if (musicSession && (!playingSession || musicSession == playingSession)) session = musicSession;
                else if (playingSession) session = playingSession;
                else if (musicSession) session = musicSession;
                else if (!session) {
                    if (sessions.Size() > 0) session = sessions.GetAt(0);
                    else session = manager.GetCurrentSession();
                }

                if (session) {
                    auto info = session.GetPlaybackInfo();
                    auto status = info.PlaybackStatus();
                    auto timeline = session.GetTimelineProperties();
                    auto props = session.TryGetMediaPropertiesAsync().get();

                    np.available = true;
                    np.playing = (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
                    np.title = props.Title().c_str();
                    np.artist = props.Artist().c_str();
                    np.album = props.AlbumTitle().c_str();
                    np.sessionTitle = np.title;
                    np.appId = session.SourceAppUserModelId().c_str();
                    np.appName = FriendlyPlayerName(np.appId);
                    double rawPos = TimeSpanToSec(timeline.Position());
                    const double startSec = TimeSpanToSec(timeline.StartTime());
                    if (startSec > 0.25 && rawPos >= startSec + 0.5) rawPos -= startSec;
                    np.durationSec = TimeSpanToSec(timeline.EndTime());
                    if (np.durationSec <= 0.0) {
                        np.durationSec = TimeSpanToSec(timeline.MaxSeekTime());
                    }
                    if (startSec > 0.25 && np.durationSec >= startSec + 0.5) np.durationSec -= startSec;
                    const ULONGLONG nowTick = GetTickCount64();
                    const bool videoSmtc = IsBrowserOrVideoApp(np.appName, np.appId) &&
                                           !IsMusicPlayerApp(np.appName, np.appId);
                    const std::wstring trackKey = videoSmtc
                        ? np.appId
                        : (np.title + L"|" + np.artist + L"|" + np.appId);
                    if (trackKey != smtcTrack) {
                        smtcTrack = trackKey;
                        smtcHead = std::max(0.0, rawPos);
                        smtcRaw = rawPos;
                        smtcStamp = nowTick;
                        videoPlayingLatch = np.playing;
                        videoPauseTicks = 0;
                    } else if (videoSmtc) {
                        const bool rawMoved = (smtcRaw >= 0.0 && std::abs(rawPos - smtcRaw) > 0.25);
                        const bool jumpedToStart = (smtcRaw > 5.0 && rawPos < 1.5 &&
                                                    std::abs(rawPos - smtcRaw) > 2.0);
                        if (np.playing || (rawMoved && rawPos > smtcRaw + 0.1)) {
                            videoPlayingLatch = true;
                            videoPauseTicks = 0;
                        } else if (++videoPauseTicks >= 6) {
                            videoPlayingLatch = false;
                        }
                        np.playing = videoPlayingLatch;
                        double display = rawPos;
                        try {
                            const auto last = timeline.LastUpdatedTime();
                            const double elapsed = std::chrono::duration<double>(winrt::clock::now() - last).count();
                            if (np.playing && elapsed >= 0.0 && elapsed < 1.6) display = rawPos + elapsed;
                        } catch (...) {}
                        if (overridePos >= 0.0) {
                            smtcHead = overridePos;
                        } else if (jumpedToStart) {
                            smtcHead = std::max(0.0, rawPos);
                        } else if (rawMoved && rawPos > 0.4) {
                            smtcHead = std::max(0.0, display);
                        } else if (videoPlayingLatch) {
                            const double elapsed = (nowTick - smtcStamp) / 1000.0;
                            smtcHead += std::clamp(elapsed, 0.0, 0.35);
                            if (display > 0.4 && std::abs(display - smtcHead) < 1.2) {
                                smtcHead += (display - smtcHead) * 0.35;
                            }
                        }
                        smtcRaw = rawPos;
                        smtcStamp = nowTick;
                    } else if (np.playing) {
                        const double elapsed = (nowTick - smtcStamp) / 1000.0;
                        const double expected = smtcHead + std::clamp(elapsed, 0.0, 0.8);
                        const bool rawMoved = (smtcRaw >= 0.0 && std::abs(rawPos - smtcRaw) > 0.35);
                        if (rawMoved && std::abs(rawPos - expected) > 1.2) {
                            smtcHead = std::max(0.0, rawPos);
                        } else {
                            smtcHead = expected;
                        }
                        smtcRaw = rawPos;
                        smtcStamp = nowTick;
                    } else {
                        smtcHead = std::max(0.0, rawPos);
                        smtcRaw = rawPos;
                        smtcStamp = nowTick;
                    }
                    if (np.durationSec > smtcHead + 2.0) smtcHead = std::min(smtcHead, np.durationSec);
                    np.positionSec = smtcHead;
                    np.rawPositionSec = rawPos;
                    np.stampMs = nowTick;
                    np.positionReliable = true;
                    StripAudioExtension(np.title);
                    if (LooksLikeBrowserChromeTitle(np.title)) np.title.clear();
                    if (LooksLikeBrowserChromeTitle(np.sessionTitle)) np.sessionTitle.clear();
                    if (!np.title.empty() && IsBrowserOrVideoApp(np.appName, np.appId) &&
                        !IsMusicPlayerApp(np.appName, np.appId)) {
                        GuessSongFromMedia(np.title, np.artist, np.appName, np.appId);
                    }
                    if (np.artist.empty()) MaybeSplitTitleArtist(np.title, np.artist);
                    if (np.title.empty() && np.artist.empty() && np.sessionTitle.empty()) np.available = false;
                }
            } catch (...) {
                np = {};
            }

            const bool smtcIsMusic = np.available && IsMusicPlayerApp(np.appName, np.appId);
            const bool smtcIsVideo = np.available && IsBrowserOrVideoApp(np.appName, np.appId) && !smtcIsMusic;
            PlayerNowPlaying player;
            const bool playerOk = ProbeMusicPlayerNowPlaying(player);
            if (playerOk && !smtcIsMusic && !smtcIsVideo) {
                np.available = true;
                np.playing = true;
                np.title = player.title;
                np.artist = player.artist;
                np.sessionTitle = player.title;
                np.appId = player.appId;
                np.appName = player.appName;
                np.fromKugou = false;
                np.fromKugouIdentify = false;
                np.waitingKugouIdentify = false;
                np.positionReliable = np.positionSec > 0.0;
            }

            KugouNowPlaying kg;
            const bool kugouOk = ProbeKugouNowPlaying(kg);
            const bool smtcPlaying = np.available && np.playing;
            const bool videoSession = np.available && IsBrowserOrVideoApp(np.appName, np.appId);
            const bool useKugou = kugouOk && !smtcIsMusic && !playerOk && !videoSession &&
                                  (kg.playing || !smtcPlaying);
            if (useKugou) {
                const ULONGLONG nowTick = GetTickCount64();
                const std::wstring key = kg.title + L"|" + kg.artist;
                if (overridePos >= 0.0) kugouHead = overridePos;
                if (key != kugouTrack) {
                    kugouTrack = key;
                    kugouHead = (overridePos >= 0.0) ? overridePos
                        : ((kg.savedPositionSec >= 0.0) ? kg.savedPositionSec : 0.0);
                    kugouSilent = 0;
                    kugouPlayingLatch = kg.playing;
                    lastIniWrite = kg.iniWriteMs;
                    lastSavedPos = kg.savedPositionSec;
                }
                const bool iniChanged = (kg.iniWriteMs != 0 && kg.iniWriteMs != lastIniWrite);
                const bool posJumped = (kg.savedPositionSec >= 0.0 && lastSavedPos >= 0.0 &&
                                        std::abs(kg.savedPositionSec - lastSavedPos) > 1.2);
                if (overridePos < 0.0 && (iniChanged || posJumped) && kg.savedPositionSec >= 0.0) {
                    kugouHead = kg.savedPositionSec;
                    lastIniWrite = kg.iniWriteMs;
                    lastSavedPos = kg.savedPositionSec;
                } else if (kg.savedPositionSec >= 0.0) {
                    lastSavedPos = kg.savedPositionSec;
                    if (kg.iniWriteMs) lastIniWrite = kg.iniWriteMs;
                }
                if (kg.playing) {
                    kugouSilent = 0;
                    kugouPlayingLatch = true;
                    kugouHead += (nowTick - kugouTick) / 1000.0;
                } else if (++kugouSilent >= 8) {
                    kugouPlayingLatch = false;
                    if (kg.savedPositionSec >= 0.0) kugouHead = kg.savedPositionSec;
                } else if (kugouPlayingLatch) {
                    kugouHead += (nowTick - kugouTick) / 1000.0;
                }
                kugouTick = nowTick;
                np = {};
                np.available = true;
                np.playing = kugouPlayingLatch;
                np.title = kg.title;
                np.artist = kg.artist;
                np.appId = L"KuGou.exe";
                np.appName = L"酷狗音乐";
                np.positionSec = kugouHead;
                np.durationSec = 0.0;
                np.stampMs = nowTick;
                np.fromKugou = true;
                np.positionReliable = false;
            }

            int cmd = command_.exchange(0);
            double seekTo = -1.0;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (hasSeek_) {
                    seekTo = overridePos_;
                    hasSeek_ = false;
                }
            }
            if (cmd && useKugou) {
                INPUT inputs[2]{};
                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = VK_MEDIA_PLAY_PAUSE;
                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = VK_MEDIA_PLAY_PAUSE;
                inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, inputs, sizeof(INPUT));
            } else if (session) {
                try {
                    if (cmd == 1) session.TryTogglePlayPauseAsync().get();
                    else if (cmd == 2) session.TryPlayAsync().get();
                    else if (cmd == 3) session.TryPauseAsync().get();
                    else if (cmd == 4) session.TrySkipPreviousAsync().get();
                    else if (cmd == 5) session.TrySkipNextAsync().get();
                    if (seekTo >= 0.0) {
                        const int64_t ticks = static_cast<int64_t>(seekTo * 10000000.0);
                        session.TryChangePlaybackPositionAsync(ticks).get();
                    }
                } catch (...) {
                }
            }

            if (np.available) {
                missTicks = 0;
                Publish(np);
            } else if (++missTicks >= 6) {
                Publish({});
            }

            for (int i = 0; i < 5 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    mutable std::mutex mu_;
    MediaNowPlaying current_{};
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<int> command_{0};
    double overridePos_ = 0.0;
    bool hasOverride_ = false;
    bool hasSeek_ = false;
};
