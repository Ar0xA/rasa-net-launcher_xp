// Infinite Rasa Launcher for Windows XP: a native Win32 port of the Tauri launcher
// (https://github.com/InfiniteRasa/rasa-net-launcher) with the same look and behaviour.

#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <memory>
#include <vector>
#include "dialogs.h"
#include "game.h"
#include "news.h"
#include "resource.h"
#include "settings.h"
#include "util.h"

namespace {

// ---------- look ----------

// Early/mid-2000s MMO launcher look: gunmetal frame, bevelled panels, inset readouts
// and a big glowing PLAY button. The same palette as the Tauri launcher's styles.css.
const COLORREF kMetalHi = RGB(0x4a, 0x56, 0x68), kMetal = RGB(0x2a, 0x32, 0x40), kMetalLo = RGB(0x16, 0x1b, 0x24);
const COLORREF kInset = RGB(0x08, 0x0b, 0x10), kEdgeLight = RGB(0x6b, 0x7a, 0x90), kEdgeDark = RGB(0x05, 0x07, 0x0a);
const COLORREF kText = RGB(0xd8, 0xe2, 0xee), kMuted = RGB(0x87, 0x96, 0xaa), kBlue = RGB(0x49, 0xb0, 0xff);
const COLORREF kOrange = RGB(0xf0, 0x8a, 0x3c), kOk = RGB(0x7e, 0xe0, 0x8f), kBad = RGB(0xff, 0x7a, 0x66);
const COLORREF kFrameTop = RGB(0x1c, 0x23, 0x30), kFrameBottom = RGB(0x0d, 0x11, 0x18);

const wchar_t kTitle[] = L"Infinite Rasa Launcher";
const wchar_t kNotice[] = L"Infinite Rasa is a non-commercial, community-run fan project.";
const wchar_t kLegal[] =
    L"Richard Garriott’s Tabula Rasa and its derivatives are Copyright of NC Interactive, LLC. "
    L"Tabula Rasa and all associated logos or designs are trademarks or registered trademarks of "
    L"NC Corporation. All rights reserved.\n\n"
    L"Infinite Rasa is a non-commercial, community-run fan project. It is not affiliated with, "
    L"endorsed by, or operated under license from NC Corporation, NC Interactive, LLC or Destination Games.\n\n"
    L"The launcher source code is released under the MIT License. No game files are included.";

enum : UINT { WM_APP_PROGRESS = WM_APP + 1, WM_APP_VERIFIED, WM_APP_NEWS };
enum : int {
    ID_TAB_NEWS = 1001, ID_TAB_PATCH, ID_NEWS, ID_SERVER, ID_WEBSITE, ID_FORUMS, ID_SETTINGS, ID_PLAY, ID_PROBLEMS,
    ID_LEGAL,
};

// Layout in 96-DPI pixels; S() scales to the screen.
const int kWidth = 780, kHeight = 566;
const RECT kBanner = {0, 0, kWidth, 150};
const RECT kNewsPanel = {12, 156, 528, 424};
const RECT kNewsList = {20, 192, 520, 416};
const RECT kServerPanel = {540, 156, 768, 304};
const RECT kLinksPanel = {540, 312, 768, 424};
const RECT kStatusPanel = {12, 432, 600, 538};
const RECT kStatusText = {22, 438, 590, 470};
const RECT kBar = {22, 474, 590, 484};
const RECT kProblems = {22, 490, 590, 530};
const RECT kPlay = {612, 432, 768, 538};
const int kFooterY = 546;

int g_dpi = 96;
int S(int v) { return MulDiv(v, g_dpi, 96); }
RECT S(const RECT& r) { return {S(r.left), S(r.top), S(r.right), S(r.bottom)}; }

HINSTANCE g_inst;
HWND g_wnd, g_news, g_server, g_play, g_problems, g_tabNews, g_tabPatch, g_legal;
HFONT g_font, g_bold, g_small, g_heading, g_big;
HBRUSH g_insetBrush;
Gdiplus::Bitmap* g_logo;

// ---------- state ----------

Settings g_settings;
bool g_busy = false;
bool g_filesOk = false;
std::wstring g_status = L"Starting…";
COLORREF g_statusColor = kText;
double g_progress = 0;
std::wstring g_account;

enum class NewsState { Loading, Failed, Loaded } g_newsState = NewsState::Loading;
std::vector<NewsItem> g_newsItems;
bool g_patchTab = false;
std::vector<int> g_shown;  // indexes into g_newsItems for the active tab; -1 is a placeholder line
std::wstring g_placeholder = L"Loading news…";

void Invalidate(const RECT& r) {
    RECT s = S(r);
    InvalidateRect(g_wnd, &s, FALSE);
}

// ---------- status / progress ----------

void SetStatus(const std::wstring& text, COLORREF color = kText) {
    g_status = text;
    g_statusColor = color;
    Invalidate(kStatusText);
}

void SetProgress(double fraction) {
    g_progress = fraction;
    Invalidate(kBar);
}

const Server* CurrentServer() { return g_settings.Selected(); }

/// With no game folder the big button becomes "Select Game".
void RefreshPlay() {
    bool selecting = g_settings.game_dir.empty();
    SetWindowTextW(g_play, selecting ? L"SELECT GAME" : L"PLAY");
    EnableWindow(g_play, !(g_busy || (!selecting && (!g_filesOk || !CurrentServer()))));
    InvalidateRect(g_play, nullptr, FALSE);
}

// ---------- servers / account ----------

void RenderAccount() {
    const Server* sv = CurrentServer();
    if (!sv) g_account = L"No server selected.";
    else if (sv->username.empty()) g_account = L"Log in at the game's login screen.";
    else if (HasPassword(sv->id)) g_account = L"Logging in as " + sv->username + L" automatically.";
    else g_account = L"Username " + sv->username + L" — enter your password in-game.";
    Invalidate(kServerPanel);
    RefreshPlay();
}

void RenderServers() {
    SendMessageW(g_server, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_settings.servers.size(); ++i) {
        SendMessageW(g_server, CB_ADDSTRING, 0, (LPARAM)g_settings.servers[i].name.c_str());
        if (g_settings.servers[i].id == g_settings.selected_server) SendMessageW(g_server, CB_SETCURSEL, i, 0);
    }
    RenderAccount();
}

void Save() {
    std::wstring error;
    if (!SaveSettings(g_settings, &error)) SetStatus(error, kBad);
}

// ---------- game files ----------

struct VerifyJob {
    std::wstring dir;
    bool full;
    bool ok = false;
    VerifyResult result;
    std::wstring error;
};

DWORD WINAPI VerifyThread(void* param) {
    auto* job = (VerifyJob*)param;
    job->ok = VerifyGame(job->dir, job->full,
                         [](double f) { PostMessageW(g_wnd, WM_APP_PROGRESS, (WPARAM)(f * 1000), 0); },
                         &job->result, &job->error);
    PostMessageW(g_wnd, WM_APP_VERIFIED, 0, (LPARAM)job);
    return 0;
}

void ShowProblems(const VerifyResult& r) {
    SendMessageW(g_problems, LB_RESETCONTENT, 0, 0);
    std::vector<std::wstring> lines;
    for (auto& p : r.missing) lines.push_back(L"Missing: " + p);
    for (auto& p : r.corrupt) lines.push_back(L"Modified: " + p);
    for (size_t i = 0; i < lines.size() && i < 50; ++i) SendMessageW(g_problems, LB_ADDSTRING, 0, (LPARAM)lines[i].c_str());
    if (lines.size() > 50)
        SendMessageW(g_problems, LB_ADDSTRING, 0, (LPARAM)Format(L"…and %u more", (unsigned)(lines.size() - 50)).c_str());
    ShowWindow(g_problems, lines.empty() ? SW_HIDE : SW_SHOW);
}

void Verify(bool full) {
    const std::wstring& dir = g_settings.game_dir;
    g_filesOk = false;
    ShowWindow(g_problems, SW_HIDE);
    SetProgress(0);
    if (dir.empty()) {
        SetStatus(L"Tabula Rasa 1.16.5.0 is required. Click Select Game and choose the folder where it is installed.");
        RefreshPlay();
        return;
    }
    g_busy = true;
    RefreshPlay();
    bool firstTime = _wcsicmp(dir.c_str(), g_settings.verified_dir.c_str()) != 0;
    SetStatus(full ? std::wstring(firstTime ? L"First-time check: verifying" : L"Verifying") +
                         L" all game files (this can take several minutes)…"
                   : L"Checking game files…");
    auto* job = new VerifyJob{dir, full};
    HANDLE t = CreateThread(nullptr, 0, VerifyThread, job, 0, nullptr);
    if (t) {
        CloseHandle(t);
    } else {
        delete job;
        g_busy = false;
        SetStatus(L"Could not start the file check.", kBad);
        RefreshPlay();
    }
}

void OnVerified(VerifyJob* job) {
    std::unique_ptr<VerifyJob> owned(job);
    g_busy = false;
    if (!job->ok) {
        SetStatus(job->error, kBad);
        RefreshPlay();
        return;
    }
    const VerifyResult& r = job->result;
    size_t bad = r.missing.size() + r.corrupt.size();
    g_filesOk = bad == 0;
    // Only a clean full verify is remembered, so later starts only need the quick check.
    bool same = _wcsicmp(job->dir.c_str(), g_settings.verified_dir.c_str()) == 0;
    if (job->full && g_filesOk) g_settings.verified_dir = job->dir;
    else if (!g_filesOk && same) g_settings.verified_dir.clear();
    Save();
    if (g_filesOk) {
        SetStatus(Format(L"Tabula Rasa %s — %u files OK. Ready to play.", r.version.c_str(), (unsigned)r.checked), kOk);
    } else {
        SetStatus(Format(L"%u file(s) are missing or modified. Please reinstall a clean Tabula Rasa %s client.",
                         (unsigned)bad, r.version.c_str()),
                  kBad);
        ShowProblems(r);
    }
    RefreshPlay();
}

/// Quick check for a folder that already passed a full verify; full verify otherwise.
void CheckGame() {
    Verify(g_settings.game_dir.empty() || _wcsicmp(g_settings.game_dir.c_str(), g_settings.verified_dir.c_str()) != 0);
}

void OnPlay() {
    if (g_settings.game_dir.empty()) {
        std::wstring dir = PickGameDir(g_wnd, L"");
        if (dir.empty()) return;
        g_settings.game_dir = dir;
        Save();
        CheckGame();
        return;
    }
    const Server* sv = CurrentServer();
    std::wstring error;
    if (!IsGameDir(g_settings.game_dir)) error = L"Game folder is not set.";
    else if (!sv) error = L"Select a server first.";
    if (error.empty()) {
        std::wstring password = sv->username.empty() ? L"" : GetPassword(sv->id);
        bool ok = LaunchGame(g_settings.game_dir, sv->host, sv->username, password, &error);
        SecureZeroMemory(&password[0], password.size() * sizeof(wchar_t));
        if (ok) {
            SetStatus(L"Launching Tabula Rasa…", kOk);
            if (g_settings.close_on_launch) DestroyWindow(g_wnd);
            return;
        }
    }
    SetStatus(error, kBad);
}

// ---------- news ----------

struct NewsJob {
    bool ok;
    std::vector<NewsItem> items;
    std::wstring error;
};

DWORD WINAPI NewsThread(void*) {
    auto* job = new NewsJob;
    job->ok = FetchNews(&job->items, &job->error);
    PostMessageW(g_wnd, WM_APP_NEWS, 0, (LPARAM)job);
    return 0;
}

void RenderNews() {
    InvalidateRect(g_tabNews, nullptr, FALSE);
    InvalidateRect(g_tabPatch, nullptr, FALSE);
    g_shown.clear();
    if (g_newsState == NewsState::Loaded) {
        for (int i = 0; i < (int)g_newsItems.size(); ++i)
            if (g_newsItems[i].patch_notes == g_patchTab) g_shown.push_back(i);
        if (g_shown.empty()) g_placeholder = L"No news yet.";
    }
    if (g_shown.empty()) g_shown.push_back(-1);

    SendMessageW(g_news, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_news, LB_RESETCONTENT, 0, 0);
    for (int idx : g_shown) SendMessageW(g_news, LB_ADDSTRING, 0, (LPARAM)idx);
    SendMessageW(g_news, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_news, nullptr, TRUE);
}

void OnNews(NewsJob* job) {
    std::unique_ptr<NewsJob> owned(job);
    if (job->ok) {
        g_newsState = NewsState::Loaded;
        g_newsItems = std::move(job->items);
        // The Patch Notes tab only appears when the feed has some.
        bool anyPatch = false;
        for (auto& n : g_newsItems) anyPatch |= n.patch_notes;
        ShowWindow(g_tabPatch, anyPatch ? SW_SHOW : SW_HIDE);
    } else {
        g_newsState = NewsState::Failed;
        g_placeholder = L"News is unavailable right now.";
    }
    RenderNews();
}

std::wstring NewsDate(const NewsItem& n) {
    if (!n.date.dwLowDateTime && !n.date.dwHighDateTime) return {};
    FILETIME local;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&n.date, &local) || !FileTimeToSystemTime(&local, &st)) return {};
    wchar_t buf[64];
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"d MMM yyyy", buf, _countof(buf))) return {};
    return buf;
}

void OpenUrl(const std::wstring& url) {
    if (!url.empty()) ShellExecuteW(g_wnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

int ItemAt(HWND list, LPARAM lp) {
    DWORD r = (DWORD)SendMessageW(list, LB_ITEMFROMPOINT, 0, lp);
    return HIWORD(r) ? -1 : (int)LOWORD(r);
}

void OpenNewsItem(int row) {
    if (row < 0 || row >= (int)g_shown.size() || g_shown[row] < 0) return;
    OpenUrl(g_newsItems[g_shown[row]].link);
}

/// Clicking a post opens it, as on the Tauri launcher; Enter does too.
LRESULT CALLBACK NewsListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            int row = ItemAt(h, MAKELPARAM(pt.x, pt.y));
            if (LOWORD(lp) == HTCLIENT && row >= 0 && row < (int)g_shown.size() && g_shown[row] >= 0) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONUP:
            OpenNewsItem(ItemAt(h, lp));
            break;
        case WM_GETDLGCODE:
            if (lp && ((MSG*)lp)->message == WM_KEYDOWN && ((MSG*)lp)->wParam == VK_RETURN) return DLGC_WANTALLKEYS;
            break;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                OpenNewsItem((int)SendMessageW(h, LB_GETCURSEL, 0, 0));
                return 0;
            }
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

int NewsTextWidth() {
    RECT rc;
    GetClientRect(g_news, &rc);
    return rc.right - S(20);
}

/// Up to three lines of summary; list box rows cannot be taller than 255 pixels.
int SummaryHeight(HDC dc, const std::wstring& text, int width) {
    if (text.empty()) return 0;
    HGDIOBJ old = SelectObject(dc, g_font);
    RECT r = {0, 0, width, 0};
    DrawTextW(dc, text.c_str(), -1, &r, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX | DT_EDITCONTROL);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return min((int)r.bottom, tm.tmHeight * 3);
}

void MeasureNews(MEASUREITEMSTRUCT* mi) {
    int idx = (int)mi->itemData;
    if (idx < 0 || idx >= (int)g_newsItems.size()) {
        mi->itemHeight = S(30);
        return;
    }
    HDC dc = GetDC(g_news);
    int h = S(8) + S(18) + SummaryHeight(dc, g_newsItems[idx].summary, NewsTextWidth()) + S(10);
    ReleaseDC(g_news, dc);
    mi->itemHeight = min(h, 255);
}

void DrawNews(DRAWITEMSTRUCT* di) {
    if (di->itemID == (UINT)-1) return;
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    int idx = (int)di->itemData;
    bool selected = (di->itemState & ODS_SELECTED) && idx >= 0;
    HBRUSH bg = CreateSolidBrush(selected ? RGB(0x14, 0x1c, 0x28) : kInset);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    if (idx < 0 || idx >= (int)g_newsItems.size()) {
        SelectObject(dc, g_font);
        SetTextColor(dc, kMuted);
        RECT t = {r.left + S(10), r.top, r.right - S(10), r.bottom};
        DrawTextW(dc, g_placeholder.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        return;
    }
    const NewsItem& n = g_newsItems[idx];
    int x = r.left + S(10), y = r.top + S(8);
    SIZE sz;
    std::wstring date = NewsDate(n);
    if (!date.empty()) {
        SelectObject(dc, g_small);
        SetTextColor(dc, kMuted);
        TextOutW(dc, x, y + S(1), date.c_str(), (int)date.size());
        GetTextExtentPoint32W(dc, date.c_str(), (int)date.size(), &sz);
        x += sz.cx + S(8);
    }
    if (!n.category.empty() && !n.patch_notes) {
        SelectObject(dc, g_small);
        GetTextExtentPoint32W(dc, n.category.c_str(), (int)n.category.size(), &sz);
        RECT tag = {x, y, x + sz.cx + S(10), y + S(15)};
        HBRUSH b = CreateSolidBrush(RGB(0x3a, 0x26, 0x14));
        FillRect(dc, &tag, b);
        DeleteObject(b);
        SetTextColor(dc, kOrange);
        DrawTextW(dc, n.category.c_str(), -1, &tag, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        x = tag.right + S(8);
    }
    SelectObject(dc, g_bold);
    SetTextColor(dc, selected ? kBlue : kText);
    RECT title = {x, y, r.right - S(10), y + S(18)};
    DrawTextW(dc, n.title.c_str(), -1, &title, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, g_font);
    SetTextColor(dc, kMuted);
    RECT sum = {r.left + S(10), y + S(19), r.right - S(10), r.bottom - S(6)};
    DrawTextW(dc, n.summary.c_str(), -1, &sum, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x1a, 0x21, 0x2c));
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, r.left + S(6), r.bottom - 1, nullptr);
    LineTo(dc, r.right - S(6), r.bottom - 1);
    SelectObject(dc, old);
    DeleteObject(pen);
    if ((di->itemState & ODS_FOCUS) && selected) DrawFocusRect(dc, &r);
}

// ---------- painting ----------

void Gradient(HDC dc, const RECT& r, COLORREF top, COLORREF bottom) {
    TRIVERTEX v[2] = {
        {r.left, r.top, (COLOR16)(GetRValue(top) << 8), (COLOR16)(GetGValue(top) << 8), (COLOR16)(GetBValue(top) << 8), 0},
        {r.right, r.bottom, (COLOR16)(GetRValue(bottom) << 8), (COLOR16)(GetGValue(bottom) << 8),
         (COLOR16)(GetBValue(bottom) << 8), 0},
    };
    GRADIENT_RECT gr = {0, 1};
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

void Bevel(HDC dc, const RECT& r, COLORREF light, COLORREF dark) {
    HPEN pl = CreatePen(PS_SOLID, 1, light), pd = CreatePen(PS_SOLID, 1, dark);
    HGDIOBJ old = SelectObject(dc, pl);
    MoveToEx(dc, r.left, r.bottom - 1, nullptr);
    LineTo(dc, r.left, r.top);
    LineTo(dc, r.right - 1, r.top);
    SelectObject(dc, pd);
    LineTo(dc, r.right - 1, r.bottom - 1);
    LineTo(dc, r.left, r.bottom - 1);
    SelectObject(dc, old);
    DeleteObject(pl);
    DeleteObject(pd);
}

void Panel(HDC dc, const RECT& r96) {
    RECT r = S(r96);
    Gradient(dc, r, kMetal, kMetalLo);
    Bevel(dc, r, kEdgeLight, kEdgeDark);
}

void Inset(HDC dc, const RECT& r96) {
    RECT r = S(r96);
    InflateRect(&r, 1, 1);
    FillRect(dc, &r, g_insetBrush);
    Bevel(dc, r, kEdgeDark, kEdgeLight);
}

void Heading(HDC dc, int x, int y, const wchar_t* text) {
    SelectObject(dc, g_heading);
    SetTextColor(dc, kMuted);
    TextOutW(dc, S(x), S(y), text, (int)wcslen(text));
}

void Paint(HDC dc, const RECT& client) {
    Gradient(dc, client, kFrameTop, kFrameBottom);
    SetBkMode(dc, TRANSPARENT);

    // Banner with the logo; a soft blue glow behind it.
    RECT banner = S(kBanner);
    Gradient(dc, {banner.left, banner.top, banner.right, banner.bottom / 2}, RGB(0x1f, 0x33, 0x4a), kFrameTop);
    if (g_logo) {
        int lw = S(380), lh = S(140);
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(g_logo, (client.right - lw) / 2, S(6), lw, lh);
    }

    Panel(dc, kNewsPanel);
    Inset(dc, kNewsList);

    Panel(dc, kServerPanel);
    Heading(dc, 552, 166, L"SERVER");
    SelectObject(dc, g_font);
    SetTextColor(dc, kText);
    RECT acct = S(RECT{552, 220, 756, 296});
    DrawTextW(dc, g_account.c_str(), -1, &acct, DT_WORDBREAK | DT_NOPREFIX);

    Panel(dc, kLinksPanel);

    Panel(dc, kStatusPanel);
    SelectObject(dc, g_font);
    SetTextColor(dc, g_statusColor);
    RECT st = S(kStatusText);
    DrawTextW(dc, g_status.c_str(), -1, &st, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    Inset(dc, kBar);
    RECT bar = S(kBar);
    if (g_progress > 0) {
        RECT fill = bar;
        fill.right = bar.left + (LONG)((bar.right - bar.left) * min(g_progress, 1.0));
        Gradient(dc, fill, RGB(0x7c, 0xc8, 0xff), RGB(0x1d, 0x6f, 0xb8));
    }

    SelectObject(dc, g_small);
    SetTextColor(dc, kMuted);
    TextOutW(dc, S(14), S(kFooterY), kNotice, (int)wcslen(kNotice));
    std::wstring version = L"v" APP_VERSION_STR L" (XP)";
    RECT vr = {0, S(kFooterY), client.right - S(12), client.bottom};
    DrawTextW(dc, version.c_str(), -1, &vr, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

    RECT frame = client;
    Bevel(dc, frame, kEdgeLight, kEdgeDark);
}

void DrawButton(DRAWITEMSTRUCT* di) {
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    bool pressed = di->itemState & ODS_SELECTED, disabled = di->itemState & ODS_DISABLED;
    wchar_t text[64];
    GetWindowTextW(di->hwndItem, text, _countof(text));
    SetBkMode(dc, TRANSPARENT);
    int id = (int)di->CtlID;

    if (id == ID_LEGAL) {
        Gradient(dc, r, kFrameBottom, kFrameBottom);
        SelectObject(dc, g_small);
        SetTextColor(dc, kBlue);
        DrawTextW(dc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SIZE sz;
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
        RECT u = {r.left, (r.top + r.bottom + sz.cy) / 2 - 1, r.left + sz.cx, (r.top + r.bottom + sz.cy) / 2};
        HBRUSH b = CreateSolidBrush(kBlue);
        FillRect(dc, &u, b);
        DeleteObject(b);
    } else if (id == ID_TAB_NEWS || id == ID_TAB_PATCH) {
        bool active = (id == ID_TAB_PATCH) == g_patchTab;
        Gradient(dc, r, active ? kMetalHi : kMetal, active ? kMetal : kMetalLo);
        Bevel(dc, r, kEdgeLight, kEdgeDark);
        if (active) {
            RECT line = {r.left + 1, r.bottom - 3, r.right - 1, r.bottom - 1};
            HBRUSH b = CreateSolidBrush(kBlue);
            FillRect(dc, &line, b);
            DeleteObject(b);
        }
        SelectObject(dc, g_bold);
        SetTextColor(dc, active ? kText : kMuted);
        DrawTextW(dc, text, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    } else if (id == ID_PLAY) {
        if (disabled) Gradient(dc, r, RGB(0x3a, 0x42, 0x50), RGB(0x22, 0x28, 0x32));
        else if (pressed) Gradient(dc, r, RGB(0x1d, 0x6f, 0xb8), RGB(0x49, 0xb0, 0xff));
        else Gradient(dc, r, RGB(0x6c, 0xc4, 0xff), RGB(0x1d, 0x6f, 0xb8));
        Bevel(dc, r, disabled ? kEdgeLight : RGB(0xb8, 0xe2, 0xff), kEdgeDark);
        SelectObject(dc, g_big);
        RECT t = r;
        if (pressed) OffsetRect(&t, 1, 1);
        if (!disabled) {
            RECT shadow = t;
            OffsetRect(&shadow, 1, 2);
            SetTextColor(dc, RGB(0x0a, 0x2a, 0x48));
            DrawTextW(dc, text, -1, &shadow, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        }
        SetTextColor(dc, disabled ? kMuted : RGB(0xff, 0xff, 0xff));
        DrawTextW(dc, text, -1, &t, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    } else {
        Gradient(dc, r, pressed ? kMetalLo : kMetalHi, pressed ? kMetal : kMetalLo);
        Bevel(dc, r, pressed ? kEdgeDark : kEdgeLight, pressed ? kEdgeLight : kEdgeDark);
        SelectObject(dc, g_bold);
        SetTextColor(dc, disabled ? kMuted : kText);
        RECT t = r;
        if (pressed) OffsetRect(&t, 1, 1);
        DrawTextW(dc, text, -1, &t, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT) && id != ID_LEGAL) {
        RECT f = r;
        InflateRect(&f, -S(4), -S(4));
        SetTextColor(dc, kText);
        DrawFocusRect(dc, &f);
    }
}

// ---------- window ----------

HWND Child(const wchar_t* cls, const wchar_t* text, DWORD style, const RECT& r96, int id, HFONT font) {
    RECT r = S(r96);
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left,
                             r.bottom - r.top, g_wnd, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, FALSE);
    return h;
}

HWND Button(const wchar_t* text, const RECT& r96, int id) {
    return Child(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, r96, id, g_bold);
}

void CreateControls() {
    g_tabNews = Button(L"Community News", RECT{20, 164, 160, 188}, ID_TAB_NEWS);
    g_tabPatch = Button(L"Patch Notes", RECT{164, 164, 284, 188}, ID_TAB_PATCH);
    ShowWindow(g_tabPatch, SW_HIDE);
    g_news = Child(L"LISTBOX", L"",
                   LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, kNewsList,
                   ID_NEWS, g_font);
    SetWindowSubclass(g_news, NewsListProc, 0, 0);

    g_server = Child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, RECT{552, 186, 756, 400}, ID_SERVER,
                     g_font);
    Button(L"Website", RECT{552, 322, 756, 350}, ID_WEBSITE);
    Button(L"Forums", RECT{552, 356, 756, 384}, ID_FORUMS);
    Button(L"Settings", RECT{552, 390, 756, 418}, ID_SETTINGS);

    g_problems = Child(L"LISTBOX", L"", LBS_NOINTEGRALHEIGHT | LBS_NOSEL | WS_VSCROLL, kProblems, ID_PROBLEMS, g_small);
    ShowWindow(g_problems, SW_HIDE);
    g_play = Child(L"BUTTON", L"PLAY", BS_OWNERDRAW | WS_TABSTOP, kPlay, ID_PLAY, g_big);

    HDC dc = GetDC(g_wnd);
    SelectObject(dc, g_small);
    SIZE notice, legal;
    GetTextExtentPoint32W(dc, kNotice, (int)wcslen(kNotice), &notice);
    GetTextExtentPoint32W(dc, L"Legal notice", 12, &legal);
    ReleaseDC(g_wnd, dc);
    RECT lr = {S(14) + notice.cx + S(6), S(kFooterY) - S(1), S(14) + notice.cx + S(6) + legal.cx, S(kFooterY) + S(15)};
    g_legal = CreateWindowExW(0, L"BUTTON", L"Legal notice", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                              lr.left, lr.top, lr.right - lr.left, lr.bottom - lr.top, g_wnd, (HMENU)ID_LEGAL, g_inst,
                              nullptr);
}

void OnCommand(int id, int code) {
    switch (id) {
        case ID_TAB_NEWS:
        case ID_TAB_PATCH:
            g_patchTab = id == ID_TAB_PATCH;
            RenderNews();
            break;
        case ID_SERVER:
            if (code == CBN_SELCHANGE) {
                int i = (int)SendMessageW(g_server, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < (int)g_settings.servers.size()) {
                    g_settings.selected_server = g_settings.servers[i].id;
                    Save();
                    RenderAccount();
                }
            }
            break;
        case ID_WEBSITE: OpenUrl(L"https://infiniterasa.org"); break;
        case ID_FORUMS: OpenUrl(L"https://infiniterasa.org/forum"); break;
        case ID_LEGAL: MessageBoxW(g_wnd, kLegal, L"Legal Notice", MB_OK | MB_ICONINFORMATION); break;
        case ID_PLAY:
            if (!g_busy) OnPlay();
            break;
        case ID_SETTINGS: {
            std::wstring oldDir = g_settings.game_dir;
            switch (ShowSettingsDialog(g_wnd, &g_settings)) {
                case SettingsResult::Saved:
                    RenderServers();
                    if (_wcsicmp(oldDir.c_str(), g_settings.game_dir.c_str()) != 0) CheckGame();
                    break;
                case SettingsResult::FullVerify:
                    if (!g_busy) Verify(true);
                    break;
                case SettingsResult::Cancelled:
                    RenderAccount();  // a password may have been saved or forgotten before an error
                    break;
            }
            break;
        }
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_wnd = h;
            CreateControls();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            Paint(mem, rc);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_CTLCOLORLISTBOX:
            SetBkColor((HDC)wp, kInset);
            SetTextColor((HDC)wp, (HWND)lp == g_problems ? kBad : kText);
            return (LRESULT)g_insetBrush;
        case WM_MEASUREITEM:
            if (wp == ID_NEWS) {
                MeasureNews((MEASUREITEMSTRUCT*)lp);
                return TRUE;
            }
            break;
        case WM_DRAWITEM: {
            auto* di = (DRAWITEMSTRUCT*)lp;
            if (di->CtlID == ID_NEWS) DrawNews(di);
            else DrawButton(di);
            return TRUE;
        }
        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_APP_PROGRESS:
            SetProgress(wp / 1000.0);
            return 0;
        case WM_APP_VERIFIED:
            OnVerified((VerifyJob*)lp);
            return 0;
        case WM_APP_NEWS:
            OnNews((NewsJob*)lp);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HFONT MakeFont(int px, int weight, const wchar_t* face = L"Tahoma") {
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}

Gdiplus::Bitmap* LoadLogo() {
    const void* data;
    size_t size;
    if (!LoadResourceBytes(IDR_LOGO, &data, &size)) return nullptr;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!mem) return nullptr;
    memcpy(GlobalLock(mem), data, size);
    GlobalUnlock(mem);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
        GlobalFree(mem);
        return nullptr;
    }
    // Decode once into memory so the stream can be released.
    Gdiplus::Bitmap src(stream);
    Gdiplus::Bitmap* logo = nullptr;
    if (src.GetLastStatus() == Gdiplus::Ok) logo = src.Clone(0, 0, src.GetWidth(), src.GetHeight(), PixelFormat32bppPARGB);
    stream->Release();
    return logo;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst;
    // No "There is no disk in the drive" boxes while looking for the game on every drive.
    SetErrorMode(SEM_FAILCRITICALERRORS);
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    g_font = MakeFont(12, FW_NORMAL);
    g_bold = MakeFont(12, FW_BOLD);
    g_small = MakeFont(11, FW_NORMAL);
    g_heading = MakeFont(11, FW_BOLD);
    g_big = MakeFont(24, FW_BOLD);
    g_insetBrush = CreateSolidBrush(kInset);
    g_logo = LoadLogo();

    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"InfiniteRasaLauncherXP";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = {0, 0, S(kWidth), S(kHeight)};
    AdjustWindowRect(&r, style, FALSE);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = max(work.left, work.left + (work.right - work.left - w) / 2);
    int y = max(work.top, work.top + (work.bottom - work.top - h) / 2);
    CreateWindowExW(0, wc.lpszClassName, kTitle, style, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!g_wnd) return 1;

    RenderNews();
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);

    HANDLE t = CreateThread(nullptr, 0, NewsThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);

    // Settings, with the game folder auto-detected (and remembered) when unset or no longer valid.
    g_settings = LoadSettings();
    if (!IsGameDir(g_settings.game_dir)) {
        g_settings.game_dir = DetectGameDir();
        if (!g_settings.game_dir.empty()) Save();
    }
    RenderServers();
    CheckGame();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete g_logo;
    Gdiplus::GdiplusShutdown(gdiToken);
    OleUninitialize();
    // Background threads (news, verify) may still be running; ExitProcess ends them.
    return (int)msg.wParam;
}
