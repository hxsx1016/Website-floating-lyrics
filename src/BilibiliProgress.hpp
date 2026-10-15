#pragma once

#include <windows.h>
#include <UIAutomation.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "UIAutomationCore.lib")

struct BiliProgress {
    bool ok = false;
    double positionSec = 0.0;
    double durationSec = 0.0;
};

inline bool ParseClockToken(const std::wstring& s, double& secOut) {
    int parts[3] = {0, 0, 0};
    int n = 0;
    int cur = 0;
    bool any = false;
    for (wchar_t c : s) {
        if (c >= L'0' && c <= L'9') {
            cur = cur * 10 + (c - L'0');
            any = true;
        } else if (c == L':' && n < 2) {
            parts[n++] = cur;
            cur = 0;
        } else if (c == L'.') {
            break;
        } else if (!iswspace(c) && c != L'/' && c != L'-') {
            return false;
        }
    }
    if (!any || n < 1) return false;
    parts[n] = cur;
    if (n == 1) secOut = parts[0] * 60.0 + parts[1];
    else secOut = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
    if (secOut < 0.0 || secOut > 24 * 3600.0) return false;
    return true;
}

inline bool ParseProgressPair(const std::wstring& text, double& pos, double& dur) {
    std::wstring s;
    s.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L'／') s.push_back(L'/');
        else s.push_back(c);
    }
    size_t slash = s.find(L'/');
    if (slash == std::wstring::npos) return false;
    std::wstring a = s.substr(0, slash);
    std::wstring b = s.substr(slash + 1);
    TrimInPlace(a);
    TrimInPlace(b);
    double p = 0, d = 0;
    if (!ParseClockToken(a, p) || !ParseClockToken(b, d)) return false;
    if (d < 3.0 || p > d + 1.5) return false;
    pos = std::min(p, d);
    dur = d;
    return true;
}

inline bool LooksLikeClockOnly(const std::wstring& s) {
    int digits = 0, colons = 0;
    for (wchar_t c : s) {
        if (c >= L'0' && c <= L'9') ++digits;
        else if (c == L':') ++colons;
        else if (!iswspace(c)) return false;
    }
    return colons == 1 && digits >= 3 && digits <= 6;
}

inline bool WindowLooksLikeBiliHost(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return false;
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
    if (exe.find(L"msedge") == std::wstring::npos &&
        exe.find(L"chrome") == std::wstring::npos &&
        exe.find(L"firefox") == std::wstring::npos &&
        exe.find(L"bilibili") == std::wstring::npos) {
        return false;
    }
    wchar_t title[1024]{};
    GetWindowTextW(hwnd, title, 1024);
    std::wstring t(title);
    for (auto& c : t) c = static_cast<wchar_t>(towlower(c));
    return t.find(L"哔哩哔哩") != std::wstring::npos ||
           t.find(L"bilibili") != std::wstring::npos ||
           t.find(L"b站") != std::wstring::npos ||
           exe.find(L"bilibili") != std::wstring::npos;
}

struct BiliScan {
    double pos = -1.0;
    double dur = -1.0;
    double slider = -1.0;
    std::vector<double> clocks;
    int visited = 0;
};

inline void ConsiderName(BiliScan& scan, const std::wstring& name) {
    if (name.empty() || name.size() > 40) return;
    double p = 0, d = 0;
    if (ParseProgressPair(name, p, d)) {
        scan.pos = p;
        scan.dur = d;
        return;
    }
    if (LooksLikeClockOnly(name) && ParseClockToken(name, p)) {
        if (scan.clocks.size() < 8) scan.clocks.push_back(p);
    }
}

inline void WalkBiliTree(IUIAutomation* uia, IUIAutomationCondition* any,
                         IUIAutomationElement* el, BiliScan& scan, int depth) {
    if (!el || !uia || !any || depth > 12 || scan.visited > 220) return;
    ++scan.visited;

    BSTR name = nullptr;
    if (SUCCEEDED(el->get_CurrentName(&name)) && name) {
        ConsiderName(scan, name);
        SysFreeString(name);
    }

    CONTROLTYPEID type = 0;
    if (SUCCEEDED(el->get_CurrentControlType(&type)) && type == UIA_SliderControlTypeId) {
        IUnknown* unk = nullptr;
        if (SUCCEEDED(el->GetCurrentPattern(UIA_RangeValuePatternId, &unk)) && unk) {
            IUIAutomationRangeValuePattern* range = nullptr;
            if (SUCCEEDED(unk->QueryInterface(IID_IUIAutomationRangeValuePattern,
                                              reinterpret_cast<void**>(&range))) && range) {
                double v = 0, mn = 0, mx = 0;
                range->get_CurrentValue(&v);
                range->get_CurrentMinimum(&mn);
                range->get_CurrentMaximum(&mx);
                range->Release();
                const double span = mx - mn;
                if (span > 0.5) {
                    const double ratio = (v - mn) / span;
                    if (ratio >= 0.0 && ratio <= 1.02) scan.slider = std::clamp(ratio, 0.0, 1.0);
                }
            }
            unk->Release();
        }
    }

    IUIAutomationElementArray* kids = nullptr;
    if (FAILED(el->FindAll(TreeScope_Children, any, &kids)) || !kids) return;
    int count = 0;
    kids->get_Length(&count);
    const int cap = std::min(count, depth < 4 ? 36 : 16);
    for (int i = 0; i < cap && scan.visited <= 220; ++i) {
        IUIAutomationElement* child = nullptr;
        if (FAILED(kids->GetElement(i, &child)) || !child) continue;
        WalkBiliTree(uia, any, child, scan, depth + 1);
        child->Release();
        if (scan.pos >= 0.0 && scan.dur >= 3.0) break;
    }
    kids->Release();
}

inline HWND FindBiliWindow() {
    HWND fg = GetForegroundWindow();
    if (fg && WindowLooksLikeBiliHost(fg)) return fg;
    HWND best = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!WindowLooksLikeBiliHost(hwnd)) return TRUE;
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        const int w = static_cast<int>(rc.right - rc.left);
        const int h = static_cast<int>(rc.bottom - rc.top);
        const int area = (w > 0 ? w : 0) * (h > 0 ? h : 0);
        auto* slot = reinterpret_cast<HWND*>(lp);
        RECT cur{};
        if (*slot) GetWindowRect(*slot, &cur);
        int bestArea = -1;
        if (*slot) {
            const int bw = static_cast<int>(cur.right - cur.left);
            const int bh = static_cast<int>(cur.bottom - cur.top);
            bestArea = (bw > 0 ? bw : 0) * (bh > 0 ? bh : 0);
        }
        if (area > bestArea) *slot = hwnd;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&best));
    return best;
}

inline bool ProbeBilibiliProgress(BiliProgress& out) {
    out = {};
    HWND hwnd = FindBiliWindow();
    if (!hwnd) return false;

    IUIAutomation* uia = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, reinterpret_cast<void**>(&uia))) || !uia) {
        return false;
    }
    IUIAutomationCondition* any = nullptr;
    IUIAutomationElement* root = nullptr;
    HRESULT hrRoot = uia->ElementFromHandle(hwnd, &root);
    HRESULT hrAny = uia->CreateTrueCondition(&any);
    if (FAILED(hrRoot) || !root || FAILED(hrAny) || !any) {
        if (root) root->Release();
        if (any) any->Release();
        uia->Release();
        return false;
    }

    BiliScan scan;
    WalkBiliTree(uia, any, root, scan, 0);
    root->Release();
    any->Release();
    uia->Release();

    if (scan.pos < 0.0 && scan.clocks.size() >= 2) {
        double a = scan.clocks[0], b = scan.clocks[1];
        if (b < a) std::swap(a, b);
        if (b >= 3.0 && a <= b + 1.0) {
            scan.pos = a;
            scan.dur = b;
        }
    }
    if (scan.dur < 3.0) return false;
    double pos = scan.pos;
    if (pos < 0.0 && scan.slider >= 0.0) pos = scan.slider * scan.dur;
    else if (pos >= 0.0 && scan.slider >= 0.0) {
        const double fromBar = scan.slider * scan.dur;
        if (std::abs(fromBar - pos) < 2.5) pos = fromBar;
    }
    if (pos < 0.0) return false;
    out.ok = true;
    out.positionSec = std::clamp(pos, 0.0, scan.dur);
    out.durationSec = scan.dur;
    return true;
}

class BiliProgressMonitor {
public:
    void Start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread([this] { Run(); });
    }
    void Stop() {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }
    BiliProgress Get() const {
        std::lock_guard<std::mutex> lock(mu_);
        return current_;
    }
    ~BiliProgressMonitor() { Stop(); }

private:
    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (running_) {
            BiliProgress bp;
            ProbeBilibiliProgress(bp);
            {
                std::lock_guard<std::mutex> lock(mu_);
                current_ = bp;
            }
            for (int i = 0; i < 20 && running_; ++i) {
                Sleep(50);
            }
        }
        CoUninitialize();
    }

    mutable std::mutex mu_;
    BiliProgress current_{};
    std::thread worker_;
    std::atomic<bool> running_{false};
};
