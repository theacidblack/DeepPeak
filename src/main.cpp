// DeepPeak - a tiny Windows tray widget that shows when DeepSeek V4.1 Flash is
// billed at peak vs off-peak rates (OpenCode Go / CommandCode GOAT passthrough).
//
// Peak: 01:00-04:00 and 06:00-10:00 UTC, Monday-Friday. Everything else,
// including all weekend, is off-peak. Peak tokens cost exactly 2x.
//
// All peak math is done in UTC. Local time is only used for display, so the
// widget works in every time zone and follows DST automatically.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <ctime>
#include <cstdio>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

// ---------------------------------------------------------------- constants

static const wchar_t kAppName[]    = L"DeepPeak";
static const wchar_t kClassPanel[] = L"DeepPeakPanel";
static const wchar_t kRunKey[]     = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t kPersonalize[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

static const UINT WM_TRAYICON = WM_APP + 1;
static const UINT WM_TOGGLE   = WM_APP + 2;
static const UINT_PTR kTimerId = 1;

static const int kPanelW = 340;
static const int kPanelH = 196;

enum MenuId {
    kMenuToggle  = 1001,
    kMenuStartup = 1002,
    kMenuCopy    = 1003,
    kMenuExit    = 1004,
    kMenuSound   = 1005,
    kMenuTest    = 1006
};

// Peak windows, minutes after UTC midnight
static const int kPeak1Start = 1 * 60,  kPeak1End = 4 * 60;
static const int kPeak2Start = 6 * 60,  kPeak2End = 10 * 60;

// DWM attributes (not in older SDK headers)
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

// ------------------------------------------------------------------ theme

struct Theme {
    bool     dark;
    COLORREF bg;
    COLORREF border;
    COLORREF text;
    COLORREF secondary;
    COLORREF tertiary;
    COLORREF divider;
    COLORREF green;
    COLORREF red;
    COLORREF chipPeakBg;
    COLORREF chipOffBg;
};

static Theme g_theme;

static COLORREF Mix(COLORREF base, COLORREF over, double amount) {
    const auto ch = [&](int a, int b) {
        return static_cast<BYTE>(a + (b - a) * amount + 0.5);
    };
    return RGB(ch(GetRValue(base), GetRValue(over)),
               ch(GetGValue(base), GetGValue(over)),
               ch(GetBValue(base), GetBValue(over)));
}

static void ApplyPalette(bool dark);

// Reads the Windows 11 app theme (Settings > Personalization > Colors).
// DEEPPEAK_THEME=light|dark overrides it, which is handy for testing.
static void LoadTheme() {
    bool dark = false;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalize, 0, KEY_QUERY_VALUE, &key) ==
        ERROR_SUCCESS) {
        DWORD value = 1, bytes = sizeof(value), type = 0;
        if (RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                             reinterpret_cast<LPBYTE>(&value), &bytes) == ERROR_SUCCESS) {
            dark = (value == 0);
        }
        RegCloseKey(key);
    }

    wchar_t forced[16] = {};
    if (GetEnvironmentVariableW(L"DEEPPEAK_THEME", forced, 16)) {
        if (_wcsicmp(forced, L"dark") == 0)  dark = true;
        if (_wcsicmp(forced, L"light") == 0) dark = false;
    }

    ApplyPalette(dark);
}

// Windows 11 palette, light and dark.
static void ApplyPalette(bool dark) {
    g_theme.dark = dark;
    if (dark) {
        g_theme.bg        = RGB(0x20, 0x20, 0x20);
        g_theme.border    = RGB(0x33, 0x33, 0x33);
        g_theme.text      = RGB(0xFF, 0xFF, 0xFF);
        g_theme.secondary = RGB(0xC5, 0xC5, 0xC5);
        g_theme.tertiary  = RGB(0x9A, 0x9A, 0x9A);
        g_theme.divider   = RGB(0x3A, 0x3A, 0x3A);
        g_theme.green     = RGB(0x6C, 0xCB, 0x5F);
        g_theme.red       = RGB(0xFF, 0x99, 0xA4);
    } else {
        g_theme.bg        = RGB(0xF9, 0xF9, 0xF9);
        g_theme.border    = RGB(0xE3, 0xE3, 0xE3);
        g_theme.text      = RGB(0x1A, 0x1A, 0x1A);
        g_theme.secondary = RGB(0x5D, 0x5D, 0x5D);
        g_theme.tertiary  = RGB(0x8A, 0x8A, 0x8A);
        g_theme.divider   = RGB(0xE8, 0xE8, 0xE8);
        g_theme.green     = RGB(0x0F, 0x7B, 0x0F);
        g_theme.red       = RGB(0xC4, 0x2B, 0x1C);
    }
    g_theme.chipPeakBg = Mix(g_theme.bg, g_theme.red, 0.16);
    g_theme.chipOffBg  = Mix(g_theme.bg, g_theme.green, 0.16);
}

// ------------------------------------------------------------------ globals

static HINSTANCE g_inst;
static HWND      g_hwnd;
static HICON     g_iconPeak;
static HICON     g_iconOff;
static bool      g_peak = false;
static bool      g_panelVisible = false;
static bool      g_startupOn = false;
static std::wstring g_lastTip;
static HFONT     g_fCaption;
static HFONT     g_fStatus;
static HFONT     g_fBody;
static HFONT     g_fSmall;
static int       g_dpi = 96;
static RECT      g_panelRect = {0, 0, 0, 0};
static bool      g_panelPlaced = false;
static HHOOK     g_kbHook = nullptr;
static DWORD     g_lastDChord = 0;
static bool      g_dwmRounded = false;
static bool      g_soundOn = true;
static bool      g_lastPeakState = false;
static ULONGLONG g_lastTickMs = 0;
static const wchar_t kSoundAlias[] = L"dpNotify";
static const wchar_t kSettingsKey[] = L"Software\\DeepPeak";
static wchar_t   g_textFace[LF_FACESIZE] = L"Segoe UI";
static wchar_t   g_displayFace[LF_FACESIZE] = L"Segoe UI";

static int S(int px) { return MulDiv(px, g_dpi, 96); }

// Optional debug log: set DEEPPEAK_DEBUG=1 to write %TEMP%\DeepPeak.log
static void LogDebug(const wchar_t* msg) {
    wchar_t path[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"DEEPPEAK_DEBUG", nullptr, 0)) return;
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"DeepPeak.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a") != 0 || !f) return;
    fwprintf(f, L"%s\n", msg);
    fclose(f);
}

// -------------------------------------------------------------- peak logic

struct Status {
    bool   peak;
    time_t next;        // UTC epoch of the next flip
    int    secondsLeft;
};

static bool IsPeak(time_t t) {
    struct tm g;
    if (gmtime_s(&g, &t) != 0) return false;
    if (g.tm_wday == 0 || g.tm_wday == 6) return false;  // weekend
    const int m = g.tm_hour * 60 + g.tm_min;
    return (m >= kPeak1Start && m < kPeak1End) ||
           (m >= kPeak2Start && m < kPeak2End);
}

static Status ComputeStatus() {
    Status s;
    const time_t now = time(nullptr);
    s.peak = IsPeak(now);

    // Step forward minute-aligned until the state flips (max 8 days).
    time_t t = now - (now % 60) + 60;
    const time_t limit = now + 8 * 86400;
    while (t < limit && IsPeak(t) == s.peak) t += 60;

    s.next = t;
    s.secondsLeft = (int)(t - now);
    if (s.secondsLeft < 0) s.secondsLeft = 0;
    return s;
}

// UTC epoch of the next moment the peak price applies.
static time_t NextPeakStart() {
    const time_t now = time(nullptr);
    time_t t = now - (now % 60) + 60;
    const time_t limit = now + 8 * 86400;
    while (t < limit && !IsPeak(t)) t += 60;
    return t;
}

// -------------------------------------------------------------- formatting

static std::wstring FormatDuration(int sec) {
    wchar_t buf[64];
    if (sec < 60) return L"<1m";
    const int totalMin = sec / 60;
    const int h = totalMin / 60;
    const int m = totalMin % 60;
    if (h > 0) swprintf_s(buf, L"%dh %02dm", h, m);
    else       swprintf_s(buf, L"%dm", m);
    return buf;
}

static std::wstring LocalClock(time_t t) {
    struct tm lt;
    if (localtime_s(&lt, &t) != 0) return L"--:--";
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", lt.tm_hour, lt.tm_min);
    return buf;
}

static std::wstring LocalWhen(time_t t) {
    static const wchar_t* kDays[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    const time_t now = time(nullptr);
    struct tm a, b;
    if (localtime_s(&a, &now) != 0 || localtime_s(&b, &t) != 0) return LocalClock(t);
    const bool sameDay = (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday);
    if (sameDay) return LocalClock(t);
    return std::wstring(kDays[b.tm_wday]) + L" " + LocalClock(t);
}

// Current UTC offset of this machine, e.g. "UTC+2" or "UTC+5:30".
static std::wstring UtcOffsetLabel() {
    const time_t now = time(nullptr);
    struct tm l, g;
    if (localtime_s(&l, &now) != 0 || gmtime_s(&g, &now) != 0) return L"";
    int diffMin = (l.tm_hour * 60 + l.tm_min) - (g.tm_hour * 60 + g.tm_min);
    if (diffMin > 720) diffMin -= 1440;
    if (diffMin < -720) diffMin += 1440;
    const int h = diffMin / 60;
    const int m = diffMin % 60;
    wchar_t buf[32];
    if (m == 0)      swprintf_s(buf, L"UTC%+d", h);
    else             swprintf_s(buf, L"UTC%+d:%02d", h, m < 0 ? -m : m);
    return buf;
}

// Peak schedule rendered in local time for the day of the next peak window.
static std::wstring LocalSchedule() {
    struct tm g;
    const time_t nextPeak = NextPeakStart();
    if (gmtime_s(&g, &nextPeak) != 0) return L"";
    g.tm_hour = 0; g.tm_min = 0; g.tm_sec = 0;
    const time_t midnight = _mkgmtime(&g);

    const std::wstring a1 = LocalClock(midnight + kPeak1Start * 60);
    const std::wstring a2 = LocalClock(midnight + kPeak1End * 60);
    const std::wstring b1 = LocalClock(midnight + kPeak2Start * 60);
    const std::wstring b2 = LocalClock(midnight + kPeak2End * 60);
    return L"Peak " + a1 + L"-" + a2 + L", " + b1 + L"-" + b2;
}

// --------------------------------------------------------------- resources

static int CALLBACK FontEnumProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) {
    *reinterpret_cast<bool*>(p) = true;
    return 0;
}

static bool FontExists(const wchar_t* name) {
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, name, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, FontEnumProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

static void PickFonts() {
    // Windows 11 ships Segoe UI Variable; older systems fall back to Segoe UI.
    if (FontExists(L"Segoe UI Variable Text")) {
        wcscpy_s(g_textFace, L"Segoe UI Variable Text");
    } else {
        wcscpy_s(g_textFace, L"Segoe UI");
    }
    if (FontExists(L"Segoe UI Variable Display")) {
        wcscpy_s(g_displayFace, L"Segoe UI Variable Display");
    } else {
        wcscpy_s(g_displayFace, g_textFace);
    }
}

static HFONT MakeFont(const wchar_t* face, int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

// Draws an anti-aliased filled circle into a 32-bit icon with an alpha channel.
static HICON MakeDotIcon(int size, COLORREF c) {
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi),
                                     DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        DeleteDC(dc);
        return nullptr;
    }

    BYTE* px = static_cast<BYTE*>(bits);
    const double cx = size / 2.0, cy = size / 2.0;
    const double radius = size * 0.45;
    const double r2 = radius * radius;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            const double d2 = dx * dx + dy * dy;

            double coverage = 1.0;
            if (d2 > r2) {
                const double over = d2 - r2;
                coverage = 1.0 - over / (radius * 2.0);
            }
            if (coverage < 0.0) coverage = 0.0;

            const BYTE a = static_cast<BYTE>(coverage * 255.0 + 0.5);
            BYTE* p = px + (y * size + x) * 4;
            // premultiplied BGRA, which is what CreateIconIndirect expects
            p[0] = static_cast<BYTE>(GetBValue(c) * a / 255);
            p[1] = static_cast<BYTE>(GetGValue(c) * a / 255);
            p[2] = static_cast<BYTE>(GetRValue(c) * a / 255);
            p[3] = a;
        }
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(dc);
    return icon;
}

static void RebuildIcons() {
    HICON peak = MakeDotIcon(32, g_theme.red);
    HICON off  = MakeDotIcon(32, g_theme.green);
    if (peak) { if (g_iconPeak) DestroyIcon(g_iconPeak); g_iconPeak = peak; }
    if (off)  { if (g_iconOff)  DestroyIcon(g_iconOff);  g_iconOff = off; }
}

static void CreateFonts() {
    if (g_fCaption) DeleteObject(g_fCaption);
    if (g_fStatus)  DeleteObject(g_fStatus);
    if (g_fBody)    DeleteObject(g_fBody);
    if (g_fSmall)   DeleteObject(g_fSmall);
    g_fCaption = MakeFont(g_textFace, 12, FW_SEMIBOLD);
    g_fStatus  = MakeFont(g_displayFace, 22, FW_SEMIBOLD);
    g_fBody    = MakeFont(g_textFace, 14, FW_NORMAL);
    g_fSmall   = MakeFont(g_textFace, 12, FW_NORMAL);
}

static void DestroyFonts() {
    if (g_fCaption) { DeleteObject(g_fCaption); g_fCaption = nullptr; }
    if (g_fStatus)  { DeleteObject(g_fStatus);  g_fStatus = nullptr; }
    if (g_fBody)    { DeleteObject(g_fBody);    g_fBody = nullptr; }
    if (g_fSmall)   { DeleteObject(g_fSmall);   g_fSmall = nullptr; }
}

static std::wstring ExePath();

// ------------------------------------------------------------- notification
// Two short voice clips next to the exe: sounds\peak.mp3 and sounds\offpeak.mp3.
// Playback goes through MCI (DirectShow), so no third-party decoder is needed.

static bool LoadSoundSetting() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return true;
    DWORD value = 1, bytes = sizeof(value), type = 0;
    bool on = true;
    if (RegQueryValueExW(key, L"SoundNotifications", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&value), &bytes) == ERROR_SUCCESS) {
        on = (value != 0);
    }
    RegCloseKey(key);
    return on;
}

static void SaveSoundSetting(bool on) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    const DWORD value = on ? 1 : 0;
    RegSetValueExW(key, L"SoundNotifications", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

static std::wstring ExeDir() {
    std::wstring path = ExePath();
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash + 1);
}

static std::wstring SoundPath(bool peak) {
    const std::wstring name = peak ? L"peak.mp3" : L"offpeak.mp3";
    const std::wstring withFolder = ExeDir() + L"sounds\\" + name;
    if (GetFileAttributesW(withFolder.c_str()) != INVALID_FILE_ATTRIBUTES) return withFolder;
    return ExeDir() + name;
}

static void StopNotification() {
    std::wstring cmd = std::wstring(L"close ") + kSoundAlias;
    mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
}

static void PlayNotification(bool peak) {
    if (!g_soundOn) return;

    const std::wstring path = SoundPath(peak);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LogDebug(L"sound file missing");
        return;
    }

    StopNotification();
    std::wstring open = L"open \"" + path + L"\" type mpegvideo alias " + kSoundAlias;
    if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) != 0) {
        LogDebug(L"mci open failed");
        return;
    }
    LogDebug(peak ? L"playing peak.mp3" : L"playing offpeak.mp3");
    std::wstring play = std::wstring(L"play ") + kSoundAlias + L" notify";
    mciSendStringW(play.c_str(), nullptr, 0, g_hwnd);
}

// Fires once per price change while the app is running. A long gap between
// ticks (sleep, hibernate) only updates the state without playing a stale cue.
static void CheckPriceTransition() {
    const ULONGLONG nowMs = GetTickCount64();
    const bool resumed = (g_lastTickMs != 0) && (nowMs - g_lastTickMs > 15000);
    g_lastTickMs = nowMs;

    const Status s = ComputeStatus();
    if (s.peak == g_lastPeakState) return;

    g_lastPeakState = s.peak;
    if (!resumed) PlayNotification(s.peak);
}
// ------------------------------------------------------------------ tray

static std::wstring BuildTip(const Status& s) {
    std::wstring text = s.peak
        ? L"DeepPeak - Peak 2x - expensive for " + FormatDuration(s.secondsLeft)
        : L"DeepPeak - Off-peak 1x - cheap for " + FormatDuration(s.secondsLeft);
    if (text.size() > 120) text.resize(120);
    return text;
}

static void UpdateTray(bool force) {
    const Status s = ComputeStatus();
    const std::wstring tip = BuildTip(s);
    if (!force && tip == g_lastTip && s.peak == g_peak) return;
    g_lastTip = tip;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    if (s.peak != g_peak) {
        g_peak = s.peak;
        nid.uFlags |= NIF_ICON;
        nid.hIcon = g_peak ? g_iconPeak : g_iconOff;
    }
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);

    if (g_panelVisible) InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void AddTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_peak ? g_iconPeak : g_iconOff;
    wcsncpy_s(nid.szTip, BuildTip(ComputeStatus()).c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

// ------------------------------------------------------------- autostart

static std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return buf;
}

static bool StartupEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD bytes = sizeof(buf);
    DWORD type = 0;
    const bool ok = RegQueryValueExW(key, kAppName, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buf), &bytes) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok && bytes > 0;
}

static void SetStartup(bool on) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    if (on) {
        const std::wstring value = L"\"" + ExePath() + L"\"";
        RegSetValueExW(key, kAppName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
}

// If the exe was moved while autostart was on, rewrite the Run entry so the
// next logon still finds it.
static void HealStartupPath() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                      &key) != ERROR_SUCCESS)
        return;
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD bytes = sizeof(buf), type = 0;
    if (RegQueryValueExW(key, kAppName, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf), &bytes) == ERROR_SUCCESS && bytes > 0) {
        const std::wstring expected = L"\"" + ExePath() + L"\"";
        if (expected != buf) {
            RegSetValueExW(key, kAppName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(expected.c_str()),
                           static_cast<DWORD>((expected.size() + 1) * sizeof(wchar_t)));
            LogDebug(L"startup path repaired");
        }
    }
    RegCloseKey(key);
}
// ------------------------------------------------------------- dwm / theme

static void ApplyDwmTheme(HWND hwnd) {
    const BOOL dark = g_theme.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    int pref = DWMWCP_ROUND;
    const HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                             &pref, sizeof(pref));
    g_dwmRounded = SUCCEEDED(hr);
    if (!g_dwmRounded) {
        // Windows 10 fallback: clip the window ourselves.
        HRGN region = CreateRoundRectRgn(0, 0, S(kPanelW) + 1, S(kPanelH) + 1,
                                         S(16), S(16));
        SetWindowRgn(hwnd, region, TRUE);
    }
}

// ------------------------------------------------------------------ drawing

static void FillRoundRect(HDC dc, const RECT& r, int radius, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(br);
}

static void DrawTextIn(HDC dc, const std::wstring& text, RECT r, HFONT font,
                       COLORREF color, UINT flags) {
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &r, flags);
}

// 24h strip: one slot per half hour, green = cheap, red = peak.
static void DrawTimeline(HDC dc, const RECT& bar, time_t now, COLORREF cheap,
                         COLORREF peak) {
    const int kSlots = 48;
    const int kSlotSecs = 1800;

    HRGN clip = CreateRoundRectRgn(bar.left, bar.top, bar.right + 1, bar.bottom + 1,
                                   bar.bottom - bar.top, bar.bottom - bar.top);
    SelectClipRgn(dc, clip);
    HBRUSH cheapBrush = CreateSolidBrush(cheap);
    HBRUSH peakBrush = CreateSolidBrush(peak);
    const double step = (bar.right - bar.left) / static_cast<double>(kSlots);
    for (int i = 0; i < kSlots; ++i) {
        RECT seg;
        seg.left = bar.left + static_cast<LONG>(i * step);
        seg.right = bar.left + static_cast<LONG>((i + 1) * step) + 1;
        seg.top = bar.top;
        seg.bottom = bar.bottom;
        FillRect(dc, &seg, IsPeak(now + i * kSlotSecs) ? peakBrush : cheapBrush);
    }
    DeleteObject(cheapBrush);
    DeleteObject(peakBrush);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

static void PaintPanel(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ oldBitmap = SelectObject(mem, back);

    const Status st = ComputeStatus();
    const Theme& th = g_theme;
    const COLORREF accent = st.peak ? th.red : th.green;

    // Windows 11 flyout surface
    HBRUSH bgBrush = CreateSolidBrush(th.bg);
    FillRect(mem, &rc, bgBrush);
    DeleteObject(bgBrush);
    {
        HBRUSH border = CreateSolidBrush(th.border);
        FrameRect(mem, &rc, border);
        DeleteObject(border);
    }

    const int pad = S(18);
    const int right = w - pad;

    // Header
    RECT header = {pad, S(15), right - S(54), S(33)};
    DrawTextIn(mem, L"DeepSeek V4.1 Flash", header, g_fCaption, th.secondary,
               DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Cost chip
    const std::wstring chip = st.peak ? L"2x" : L"1x";
    SelectObject(mem, g_fCaption);
    SIZE chipSize = {0, 0};
    GetTextExtentPoint32W(mem, chip.c_str(), static_cast<int>(chip.size()), &chipSize);
    const int chipW = chipSize.cx + S(22);
    const int chipH = S(22);
    RECT chipRc = {right - chipW, S(13), right, S(13) + chipH};
    FillRoundRect(mem, chipRc, S(11), st.peak ? th.chipPeakBg : th.chipOffBg);
    DrawTextIn(mem, chip, chipRc, g_fCaption, accent,
               DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // Status
    RECT statusRc = {pad, S(40), right, S(72)};
    DrawTextIn(mem, st.peak ? L"Peak" : L"Off-peak", statusRc, g_fStatus, accent,
               DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Time left in this price tier
    const std::wstring head = st.peak
        ? L"Expensive for " + FormatDuration(st.secondsLeft)
        : L"Cheap for " + FormatDuration(st.secondsLeft);
    RECT leftRc = {pad, S(74), right, S(96)};
    DrawTextIn(mem, head, leftRc, g_fBody, th.text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // What happens next
    const std::wstring nextLine = st.peak
        ? L"Cheap price returns " + LocalWhen(st.next)
        : L"Peak price starts " + LocalWhen(st.next);
    RECT nextRc = {pad, S(96), right, S(114)};
    DrawTextIn(mem, nextLine, nextRc, g_fSmall, th.secondary,
               DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // 24h timeline
    RECT bar = {pad, S(122), right, S(130)};
    const COLORREF cheapBar = Mix(th.bg, th.green, 0.85);
    const COLORREF peakBar = th.dark ? Mix(th.bg, RGB(0xB8, 0x4A, 0x52), 0.95)
                                     : Mix(th.bg, th.red, 0.85);
    DrawTimeline(mem, bar, time(nullptr), cheapBar, peakBar);
    RECT lblL = {pad, S(130), pad + S(60), S(144)};
    DrawTextIn(mem, L"now", lblL, g_fSmall, th.tertiary,
               DT_LEFT | DT_SINGLELINE | DT_TOP);
    RECT lblR = {right - S(60), S(130), right, S(144)};
    DrawTextIn(mem, L"+24h", lblR, g_fSmall, th.tertiary,
               DT_RIGHT | DT_SINGLELINE | DT_TOP);

    // Divider
    RECT div = {pad, S(150), right, S(151)};
    HBRUSH divBr = CreateSolidBrush(th.divider);
    FillRect(mem, &div, divBr);
    DeleteObject(divBr);

    // Footer
    RECT f1 = {pad, S(156), right, S(174)};
    DrawTextIn(mem, LocalSchedule(), f1, g_fSmall, th.secondary,
               DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const std::wstring f2 = L"Local time " + UtcOffsetLabel() + L" - weekends off-peak";
    RECT f2r = {pad, S(172), right, S(188)};
    DrawTextIn(mem, f2, f2r, g_fSmall, th.tertiary,
               DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ------------------------------------------------------------------- panel

static void ShowPanel(bool show) {
    if (show) {
        if (!g_panelPlaced) {
            POINT pt;
            GetCursorPos(&pt);
            MONITORINFO mi = {sizeof(mi)};
            HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            GetMonitorInfoW(mon, &mi);
            RECT wr;
            GetWindowRect(g_hwnd, &wr);
            const int w = wr.right - wr.left, h = wr.bottom - wr.top;
            int x = pt.x + S(8);
            int y = pt.y - h - S(8);
            if (x + w > mi.rcWork.right) x = mi.rcWork.right - w - S(8);
            if (y < mi.rcWork.top) y = mi.rcWork.top + S(8);
            g_panelRect = {x, y, x + w, y + h};
            g_panelPlaced = true;
        }
        SetWindowPos(g_hwnd, HWND_TOPMOST,
                     g_panelRect.left, g_panelRect.top,
                     g_panelRect.right - g_panelRect.left,
                     g_panelRect.bottom - g_panelRect.top,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        InvalidateRect(g_hwnd, nullptr, FALSE);
    } else {
        ShowWindow(g_hwnd, SW_HIDE);
    }
    g_panelVisible = show;
}

static void CopyStatusToClipboard() {
    const Status st = ComputeStatus();
    std::wstring text = st.peak ? L"Peak" : L"Off-peak";
    text += st.peak ? L" (2x cost)" : L" (1x cost)";
    text += st.peak ? L", peak ends " : L", peak starts ";
    text += LocalWhen(st.next);
    text += L" local. ";
    text += LocalSchedule();
    text += L" (";
    text += UtcOffsetLabel();
    text += L")";

    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dst = GlobalLock(mem);
        if (dst) {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

// Global Ctrl+D then Ctrl+P chord. RegisterHotKey cannot express a two-key
// chord, so a low-level keyboard hook watches for it. The trailing P is
// swallowed so it does not trigger a print dialog in the focused app.
static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;

        if (down && kb->vkCode == 'D' && ctrl) {
            g_lastDChord = GetTickCount();
        } else if (down && kb->vkCode == 'P' && ctrl) {
            const DWORD now = GetTickCount();
            if (g_lastDChord != 0 && now - g_lastDChord <= 800) {
                g_lastDChord = 0;
                LogDebug(L"chord: ctrl+d+p");
                PostMessage(g_hwnd, WM_TOGGLE, 0, 0);
                return 1;  // swallow P
            }
        } else if (up && (kb->vkCode == VK_CONTROL || kb->vkCode == VK_LCONTROL ||
                          kb->vkCode == VK_RCONTROL)) {
            g_lastDChord = 0;
        }
    }
    return CallNextHookEx(g_kbHook, code, wParam, lParam);
}

static void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuToggle,
                g_panelVisible ? L"Hide panel\tCtrl+D, P" : L"Show panel\tCtrl+D, P");
    AppendMenuW(menu, MF_STRING, kMenuCopy, L"Copy status");
    AppendMenuW(menu, MF_STRING | (g_soundOn ? MF_CHECKED : 0), kMenuSound,
                L"Sound notification");
    AppendMenuW(menu, MF_STRING, kMenuTest, L"Test sound");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    g_startupOn = StartupEnabled();
    AppendMenuW(menu, MF_STRING | (g_startupOn ? MF_CHECKED : 0), kMenuStartup,
                L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                    pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessage(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
        case kMenuToggle:  ShowPanel(!g_panelVisible); break;
        case kMenuCopy:    CopyStatusToClipboard();    break;
        case kMenuStartup: SetStartup(!g_startupOn);   break;
        case kMenuSound:
            g_soundOn = !g_soundOn;
            SaveSoundSetting(g_soundOn);
            if (!g_soundOn) StopNotification();
            break;
        case kMenuTest:    PlayNotification(ComputeStatus().peak); break;
        case kMenuExit:    DestroyWindow(g_hwnd);      break;
        default: break;
    }
}

// ---------------------------------------------------------------- windows

static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            PaintPanel(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_NCHITTEST:
            return HTCAPTION;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && g_panelVisible)
                ShowPanel(false);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) ShowPanel(false);
            return 0;

        case WM_TIMER:
            if (wParam == kTimerId) {
                CheckPriceTransition();
                UpdateTray(false);
            }
            return 0;

        case WM_SETTINGCHANGE: {
            LogDebug(L"WM_SETTINGCHANGE");
            const bool wasDark = g_theme.dark;
            LoadTheme();
            LogDebug(g_theme.dark ? L"theme now dark" : L"theme now light");
            if (wasDark != g_theme.dark) {
                RebuildIcons();
                CreateFonts();
                ApplyDwmTheme(hwnd);
                UpdateTray(true);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_TIMECHANGE:
        case WM_DISPLAYCHANGE:
            UpdateTray(true);
            return 0;

        case MM_MCINOTIFY:
            if (wParam == MCI_NOTIFY_SUCCESSFUL) StopNotification();
            return 0;

        case WM_TOGGLE:
            ShowPanel(!g_panelVisible);
            return 0;

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowPanel(!g_panelVisible);
            } else if (lParam == WM_RBUTTONUP) {
                if (g_panelVisible) ShowPanel(false);
                ShowTrayMenu();
            }
            return 0;

        case WM_CLOSE:
            ShowPanel(false);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_dpi = 96;
            HDC dc = GetDC(hwnd);
            if (dc) {
                g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
                ReleaseDC(hwnd, dc);
            }
            CreateFonts();
            SetTimer(hwnd, kTimerId, 1000, nullptr);
            return 0;
        }

        case WM_DESTROY: {
            KillTimer(hwnd, kTimerId);
            if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
            StopNotification();
            NOTIFYICONDATAW nid = {};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            DestroyFonts();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    g_inst = inst;

    // Per-monitor DPI awareness (v2) when available.
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetCtxFn = BOOL(WINAPI*)(HANDLE);
        auto setCtx = reinterpret_cast<SetCtxFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx) setCtx(reinterpret_cast<HANDLE>(-4));  // PER_MONITOR_AWARE_V2
    }

    // Single instance
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"DeepPeak.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    LoadTheme();
    PickFonts();
    g_iconPeak = MakeDotIcon(32, g_theme.red);
    g_iconOff  = MakeDotIcon(32, g_theme.green);
    g_peak = ComputeStatus().peak;
    g_lastPeakState = g_peak;
    g_soundOn = LoadSoundSetting();
    HealStartupPath();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kAppName;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = g_iconOff;
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    WNDCLASSEXW pc = {sizeof(pc)};
    pc.lpfnWndProc = PanelProc;
    pc.hInstance = inst;
    pc.lpszClassName = kClassPanel;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.hbrBackground = nullptr;
    pc.style = CS_DROPSHADOW;
    RegisterClassExW(&pc);

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassPanel, kAppName,
                             WS_POPUP, 0, 0, kPanelW, kPanelH,
                             nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    // Scale the panel to the current DPI.
    {
        HDC dc = GetDC(g_hwnd);
        if (dc) {
            g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
            ReleaseDC(g_hwnd, dc);
        }
        SetWindowPos(g_hwnd, nullptr, 0, 0, S(kPanelW), S(kPanelH),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ApplyDwmTheme(g_hwnd);

    AddTrayIcon();

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, inst, 0);
    if (!g_kbHook) LogDebug(L"hook install FAILED"); else LogDebug(L"hook installed");

    // `--show` opens the panel immediately (handy for testing / shortcuts).
    if (cmdLine && wcsstr(cmdLine, L"--show")) ShowPanel(true);
    if (cmdLine && wcsstr(cmdLine, L"--test-sound")) PlayNotification(ComputeStatus().peak);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_iconPeak) DestroyIcon(g_iconPeak);
    if (g_iconOff)  DestroyIcon(g_iconOff);
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}
