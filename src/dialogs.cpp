#include "dialogs.h"
#include <commctrl.h>
#include <shlobj.h>
#include <wctype.h>
#include <vector>
#include "game.h"
#include "resource.h"
#include "util.h"

namespace {

std::wstring GetText(HWND dlg, int id) {
    HWND h = GetDlgItem(dlg, id);
    std::wstring s(GetWindowTextLengthW(h) + 1, L'\0');
    GetWindowTextW(h, &s[0], (int)s.size());
    s.resize(wcslen(s.c_str()));
    return s;
}

/// host:port with no spaces, as the Tauri launcher requires (^[^\s:]+:\d+$).
bool ValidHost(const std::wstring& h) {
    size_t colon = h.find(L':');
    if (colon == 0 || colon == std::wstring::npos || colon + 1 == h.size()) return false;
    for (size_t i = 0; i < colon; ++i)
        if (iswspace(h[i])) return false;
    for (size_t i = colon + 1; i < h.size(); ++i)
        if (h[i] < L'0' || h[i] > L'9') return false;
    return true;
}

/// Working copy of a server in the dialog; only applied on Save.
struct ServerRow {
    Server server;
    std::wstring password;  // newly entered; empty keeps whatever is saved
    bool hasPassword = false;
    bool forget = false;
};

struct SettingsState {
    Settings* settings;
    std::wstring draftDir;
    std::vector<ServerRow> rows;
    std::vector<std::wstring> removedIds;
};

// ---------- server dialog ----------

struct ServerDialogState {
    ServerRow* row;
    bool isNew;
};

void UpdatePasswordNote(HWND dlg, const ServerRow& row) {
    bool saved = row.hasPassword;
    EnableWindow(GetDlgItem(dlg, IDC_FORGET), saved);
    SetDlgItemTextW(dlg, IDC_PWNOTE,
                    saved ? L"A password is saved. Leave the field blank to keep it."
                          : L"Optional. Saved passwords are only used together with a username.");
}

INT_PTR CALLBACK ServerProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (ServerDialogState*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            st = (ServerDialogState*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)st);
            SetWindowTextW(dlg, st->isNew ? L"Add server" : L"Edit server");
            SetDlgItemTextW(dlg, IDC_NAME, st->row->server.name.c_str());
            SetDlgItemTextW(dlg, IDC_HOST, st->row->server.host.c_str());
            SetDlgItemTextW(dlg, IDC_USER, st->row->server.username.c_str());
            SetDlgItemTextW(dlg, IDC_PASSWORD, st->row->password.c_str());
            CheckDlgButton(dlg, IDC_FORGET, st->row->forget ? BST_CHECKED : BST_UNCHECKED);
            UpdatePasswordNote(dlg, *st->row);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                std::wstring name = Trim(GetText(dlg, IDC_NAME)), host = Trim(GetText(dlg, IDC_HOST));
                if (name.empty() || !ValidHost(host)) {
                    MessageBoxW(dlg, L"Each server needs a name and an address like host:2106.", L"Server",
                                MB_ICONWARNING);
                    return TRUE;
                }
                st->row->server.name = name;
                st->row->server.host = host;
                st->row->server.username = Trim(GetText(dlg, IDC_USER));
                st->row->password = GetText(dlg, IDC_PASSWORD);
                st->row->forget = IsDlgButtonChecked(dlg, IDC_FORGET) == BST_CHECKED;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// ---------- settings dialog ----------

const wchar_t* PasswordState(const ServerRow& r) {
    if (!r.password.empty()) return r.server.username.empty() ? L"needs a username" : L"will be saved";
    if (r.hasPassword && r.forget) return L"will be forgotten";
    return r.hasPassword ? L"saved" : L"-";
}

void RenderRows(HWND dlg, SettingsState* st, int select) {
    HWND lv = GetDlgItem(dlg, IDC_SERVERS);
    ListView_DeleteAllItems(lv);
    for (int i = 0; i < (int)st->rows.size(); ++i) {
        auto& r = st->rows[i];
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(r.server.name.c_str());
        ListView_InsertItem(lv, &item);
        ListView_SetItemText(lv, i, 1, const_cast<wchar_t*>(r.server.host.c_str()));
        ListView_SetItemText(lv, i, 2, const_cast<wchar_t*>(r.server.username.c_str()));
        ListView_SetItemText(lv, i, 3, const_cast<wchar_t*>(PasswordState(r)));
    }
    if (select >= (int)st->rows.size()) select = (int)st->rows.size() - 1;
    if (select >= 0) ListView_SetItemState(lv, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    EnableWindow(GetDlgItem(dlg, IDC_EDIT), select >= 0);
    EnableWindow(GetDlgItem(dlg, IDC_REMOVE), select >= 0);
}

int SelectedRow(HWND dlg) { return ListView_GetNextItem(GetDlgItem(dlg, IDC_SERVERS), -1, LVNI_SELECTED); }

void EditRow(HWND dlg, SettingsState* st, int i) {
    if (i < 0) return;
    ServerRow copy = st->rows[i];
    ServerDialogState ds{&copy, false};
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SERVER), dlg, ServerProc, (LPARAM)&ds) == IDOK) {
        st->rows[i] = copy;
        RenderRows(dlg, st, i);
    }
}

bool Save(HWND dlg, SettingsState* st) {
    if (st->rows.empty()) {
        MessageBoxW(dlg, L"Keep at least one server.", L"Settings", MB_ICONWARNING);
        return false;
    }
    Settings& s = *st->settings;
    std::wstring error;
    for (auto& id : st->removedIds)
        if (!SetPassword(id, L"", &error)) break;
    for (auto& row : st->rows) {
        if (!error.empty()) break;
        const Server* old = nullptr;
        for (auto& sv : s.servers)
            if (sv.id == row.server.id) old = &sv;
        bool usernameChanged = old && old->username != row.server.username;
        if (!row.password.empty() && !row.server.username.empty()) {
            SetPassword(row.server.id, row.password, &error);
        } else if (row.forget || usernameChanged || row.server.username.empty()) {
            SetPassword(row.server.id, L"", &error);
        }
    }
    if (!error.empty()) {
        MessageBoxW(dlg, error.c_str(), L"Settings", MB_ICONERROR);
        return false;
    }

    Settings next = s;
    next.game_dir = st->draftDir;
    next.servers.clear();
    bool selectedKept = false;
    for (auto& row : st->rows) {
        next.servers.push_back(row.server);
        selectedKept |= row.server.id == s.selected_server;
    }
    if (!selectedKept) next.selected_server = st->rows[0].server.id;
    next.close_on_launch = IsDlgButtonChecked(dlg, IDC_CLOSEONLAUNCH) == BST_CHECKED;
    if (!SaveSettings(next, &error)) {
        MessageBoxW(dlg, error.c_str(), L"Settings", MB_ICONERROR);
        return false;
    }
    s = next;
    if (!st->draftDir.empty()) {
        // Like the Tauri launcher, a client.cfg that cannot be written does not block saving.
        SetSkipIntro(st->draftDir, IsDlgButtonChecked(dlg, IDC_SKIPINTRO) == BST_CHECKED, &error);
    }
    return true;
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (SettingsState*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            st = (SettingsState*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)st);
            SetDlgItemTextW(dlg, IDC_GAMEDIR, st->draftDir.empty() ? L"Tabula Rasa folder not set" : st->draftDir.c_str());
            CheckDlgButton(dlg, IDC_SKIPINTRO, !st->draftDir.empty() && GetSkipIntro(st->draftDir));
            EnableWindow(GetDlgItem(dlg, IDC_SKIPINTRO), !st->draftDir.empty());
            CheckDlgButton(dlg, IDC_CLOSEONLAUNCH, st->settings->close_on_launch);

            HWND lv = GetDlgItem(dlg, IDC_SERVERS);
            ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT);
            RECT rc;
            GetClientRect(lv, &rc);
            int w = rc.right - GetSystemMetrics(SM_CXVSCROLL);
            const wchar_t* titles[] = {L"Name", L"Auth server", L"Username", L"Password"};
            int widths[] = {w * 26 / 100, w * 34 / 100, w * 20 / 100, w - w * 80 / 100};
            for (int i = 0; i < 4; ++i) {
                LVCOLUMNW col = {LVCF_TEXT | LVCF_WIDTH};
                col.pszText = const_cast<wchar_t*>(titles[i]);
                col.cx = widths[i];
                ListView_InsertColumn(lv, i, &col);
            }
            RenderRows(dlg, st, 0);
            SetFocus(lv);  // not the read-only folder box, which would show its text selected
            return FALSE;
        }
        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lp;
            if (hdr->idFrom == IDC_SERVERS && hdr->code == NM_DBLCLK) EditRow(dlg, st, SelectedRow(dlg));
            if (hdr->idFrom == IDC_SERVERS && hdr->code == LVN_ITEMCHANGED) {
                bool any = SelectedRow(dlg) >= 0;
                EnableWindow(GetDlgItem(dlg, IDC_EDIT), any);
                EnableWindow(GetDlgItem(dlg, IDC_REMOVE), any);
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BROWSE: {
                    std::wstring dir = PickGameDir(dlg, st->draftDir);
                    if (dir.empty()) return TRUE;
                    st->draftDir = dir;
                    SetDlgItemTextW(dlg, IDC_GAMEDIR, dir.c_str());
                    EnableWindow(GetDlgItem(dlg, IDC_SKIPINTRO), TRUE);
                    CheckDlgButton(dlg, IDC_SKIPINTRO, GetSkipIntro(dir));
                    return TRUE;
                }
                case IDC_VERIFY:
                    // Verifies the saved game folder; unsaved changes are dropped, as in the Tauri launcher.
                    EndDialog(dlg, (INT_PTR)SettingsResult::FullVerify);
                    return TRUE;
                case IDC_ADD: {
                    ServerRow row;
                    row.server = {NewServerId(), L"Local", L"localhost:2106", L""};
                    ServerDialogState ds{&row, true};
                    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SERVER), dlg, ServerProc,
                                        (LPARAM)&ds) == IDOK) {
                        st->rows.push_back(row);
                        RenderRows(dlg, st, (int)st->rows.size() - 1);
                    }
                    return TRUE;
                }
                case IDC_EDIT:
                    EditRow(dlg, st, SelectedRow(dlg));
                    return TRUE;
                case IDC_REMOVE: {
                    int i = SelectedRow(dlg);
                    if (i < 0) return TRUE;
                    st->removedIds.push_back(st->rows[i].server.id);
                    st->rows.erase(st->rows.begin() + i);
                    RenderRows(dlg, st, i);
                    return TRUE;
                }
                case IDOK:
                    if (Save(dlg, st)) EndDialog(dlg, (INT_PTR)SettingsResult::Saved);
                    return TRUE;
                case IDCANCEL:
                    EndDialog(dlg, (INT_PTR)SettingsResult::Cancelled);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

int CALLBACK BrowseProc(HWND hwnd, UINT msg, LPARAM, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data) SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

}  // namespace

SettingsResult ShowSettingsDialog(HWND owner, Settings* settings) {
    SettingsState st{settings, settings->game_dir};
    for (auto& sv : settings->servers) {
        ServerRow row;
        row.server = sv;
        row.hasPassword = HasPassword(sv.id);
        st.rows.push_back(row);
    }
    INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsProc, (LPARAM)&st);
    for (auto& row : st.rows) SecureZeroMemory(&row.password[0], row.password.size() * sizeof(wchar_t));
    return r < 0 ? SettingsResult::Cancelled : (SettingsResult)r;
}

std::wstring PickGameDir(HWND owner, const std::wstring& start) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select your Tabula Rasa folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    bi.lpfn = BrowseProc;
    bi.lParam = (LPARAM)(start.empty() ? nullptr : start.c_str());
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH] = {};
    BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok) return {};
    std::wstring dir = path;
    if (dir.size() > 3 && dir.back() == L'\\') dir.pop_back();
    if (!IsGameDir(dir)) {
        MessageBoxW(owner, L"tabula_rasa.exe was not found in that folder.", L"Infinite Rasa", MB_ICONWARNING);
        return {};
    }
    return dir;
}
