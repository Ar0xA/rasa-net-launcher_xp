#include "game.h"
#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "json.h"
#include "resource.h"
#include "sha256.h"
#include "util.h"

const wchar_t kGameExe[] = L"tabula_rasa.exe";

bool IsGameDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    DWORD attr = GetFileAttributesW((dir + L"\\" + kGameExe).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/// Install locations recorded in Windows' "Programs and Features" list.
static void RegistryCandidates(std::vector<std::wstring>* out) {
    const wchar_t* kUninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    // The 32-bit and 64-bit registry views; on 32-bit Windows both flags are ignored.
    for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (REGSAM view : {(REGSAM)KEY_WOW64_32KEY, (REGSAM)KEY_WOW64_64KEY}) {
            HKEY root;
            if (RegOpenKeyExW(hive, kUninstall, 0, KEY_READ | view, &root) != ERROR_SUCCESS) continue;
            wchar_t name[256];
            for (DWORD i = 0;; ++i) {
                DWORD len = _countof(name);
                if (RegEnumKeyExW(root, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                HKEY app;
                if (RegOpenKeyExW(root, name, 0, KEY_READ | view, &app) != ERROR_SUCCESS) continue;
                wchar_t value[MAX_PATH * 2];
                DWORD size = sizeof value - sizeof(wchar_t), type;
                if (RegQueryValueExW(app, L"DisplayName", nullptr, &type, (BYTE*)value, &size) == ERROR_SUCCESS &&
                    type == REG_SZ) {
                    value[size / sizeof(wchar_t)] = 0;
                    _wcslwr_s(value);
                    size = sizeof value - sizeof(wchar_t);
                    if (wcsstr(value, L"tabula rasa") &&
                        RegQueryValueExW(app, L"InstallLocation", nullptr, &type, (BYTE*)value, &size) == ERROR_SUCCESS &&
                        (type == REG_SZ || type == REG_EXPAND_SZ)) {
                        value[size / sizeof(wchar_t)] = 0;
                        std::wstring loc = Trim(value);
                        if (loc.size() >= 2 && loc.front() == L'"' && loc.back() == L'"') loc = loc.substr(1, loc.size() - 2);
                        out->push_back(loc);
                    }
                }
                RegCloseKey(app);
            }
            RegCloseKey(root);
        }
    }
}

std::wstring DetectGameDir() {
    std::vector<std::wstring> dirs;
    RegistryCandidates(&dirs);
    static const wchar_t* kSubdirs[] = {
        L"Program Files (x86)\\NCsoft\\Tabula Rasa", L"Program Files\\NCsoft\\Tabula Rasa", L"NCsoft\\Tabula Rasa",
        L"Games\\Tabula Rasa", L"Tabula Rasa",
    };
    DWORD drives = GetLogicalDrives();
    for (wchar_t d = L'C'; d <= L'Z'; ++d) {
        if (!(drives & (1u << (d - L'A')))) continue;
        wchar_t root[] = {d, L':', L'\\', 0};
        UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOTE && type != DRIVE_REMOVABLE) continue;
        for (auto* sub : kSubdirs) dirs.push_back(std::wstring(root) + sub);
    }
    for (auto& d : dirs)
        if (IsGameDir(d)) return d;
    return {};
}

static bool HashFile(const std::wstring& path, std::string* hex, const std::function<void(uint64_t)>& onBytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    static const DWORD kChunk = 1 << 20;
    std::vector<char> buf(kChunk);
    Sha256 sha;
    DWORD n;
    bool ok;
    while ((ok = ReadFile(f, buf.data(), kChunk, &n, nullptr) != 0) && n > 0) {
        sha.Update(buf.data(), n);
        onBytes(n);
    }
    CloseHandle(f);
    if (ok) *hex = sha.HexDigest();
    return ok;
}

bool VerifyGame(const std::wstring& dir, bool full, const std::function<void(double)>& progress,
                VerifyResult* result, std::wstring* error) {
    const void* data;
    size_t size;
    Json manifest;
    const Json* files;
    if (!LoadResourceBytes(IDR_MANIFEST, &data, &size) ||
        !Json::Parse(std::string((const char*)data, size), &manifest) ||
        !(files = manifest.Get("files")) || files->type != Json::Array) {
        *error = L"Bad embedded manifest.";
        return false;
    }

    struct Entry { std::wstring path; uint64_t size; std::string sha256; };
    std::vector<Entry> entries;
    uint64_t totalBytes = 0;
    for (auto& f : files->arr) {
        const Json* sz = f.Get("size");
        Entry e{Widen(f.StringOr("path")), sz ? (uint64_t)sz->num : 0, f.StringOr("sha256")};
        totalBytes += e.size;
        entries.push_back(e);
    }

    *result = VerifyResult();
    result->version = Widen(manifest.StringOr("version"));
    result->checked = entries.size();

    // A full verify reports progress by bytes hashed, so the large .glm archives don't stall the bar.
    uint64_t doneBytes = 0;
    DWORD lastReport = 0;
    auto report = [&](size_t i) {
        double f = full ? (totalBytes ? (double)doneBytes / totalBytes : 1) : (double)i / entries.size();
        DWORD now = GetTickCount();
        if (now - lastReport >= 50 || f >= 1) {
            lastReport = now;
            progress(f);
        }
    };

    for (size_t i = 0; i < entries.size(); ++i) {
        report(i);
        const Entry& e = entries[i];
        std::wstring path = dir + L"\\" + e.path;
        for (auto& c : path)
            if (c == L'/') c = L'\\';
        WIN32_FILE_ATTRIBUTE_DATA attr;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr) ||
            (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            result->missing.push_back(e.path);
            doneBytes += e.size;
            continue;
        }
        uint64_t actual = (uint64_t)attr.nFileSizeHigh << 32 | attr.nFileSizeLow;
        bool ok = actual == e.size;
        if (ok && full) {
            std::string hex;
            uint64_t before = doneBytes;
            ok = HashFile(path, &hex, [&](uint64_t n) { doneBytes += n; report(i); }) && hex == e.sha256;
            doneBytes = before;
        }
        doneBytes += e.size;
        if (!ok) result->corrupt.push_back(e.path);
    }
    progress(1);
    return true;
}

// ---------- client.cfg ----------

static const char kClientCfg[] = "client.cfg";
static const char kIntroKey[] = "Client.UserInterface.ShowIntroMovie";

static std::string TrimA(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

static bool IsIntroLine(const std::string& line) {
    size_t eq = line.find('=');
    return eq != std::string::npos && TrimA(line.substr(0, eq)) == kIntroKey;
}

static std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

bool GetSkipIntro(const std::wstring& dir) {
    std::string cfg;
    if (!ReadFileBytes(dir + L"\\" + Widen(kClientCfg), &cfg)) return false;
    for (auto& line : SplitLines(cfg)) {
        if (!IsIntroLine(line)) continue;
        return _stricmp(TrimA(line.substr(line.find('=') + 1)).c_str(), "false") == 0;
    }
    return false;  // missing key: the game default (intro on)
}

bool SetSkipIntro(const std::wstring& dir, bool skip, std::wstring* error) {
    std::wstring path = dir + L"\\" + Widen(kClientCfg);
    std::string line = std::string(kIntroKey) + " = " + (skip ? "False" : "True");
    std::string cfg;
    ReadFileBytes(path, &cfg);
    const char* nl = cfg.find("\r\n") != std::string::npos || cfg.empty() ? "\r\n" : "\n";

    std::vector<std::string> lines = SplitLines(cfg);
    bool done = false;
    for (auto& l : lines)
        if (!done && IsIntroLine(l)) l = line, done = true;
    if (!done) {
        for (size_t i = 0; i < lines.size() && !done; ++i)
            if (TrimA(lines[i]) == "[Options]") lines.insert(lines.begin() + i + 1, line), done = true;
    }
    if (!done) {
        // Missing keys fall back to game defaults, so a minimal file is valid.
        if (lines.empty()) lines = {"[ConfigInfo]", "Version = 2"};
        lines.insert(lines.end(), {"", "[Options]", line});
    }

    std::string out;
    for (auto& l : lines) out += l + nl;
    if (WriteFileAtomic(path, out)) return true;
    *error = L"Could not update client.cfg: " + LastErrorText(GetLastError());
    return false;
}

bool LaunchGame(const std::wstring& dir, const std::wstring& host, const std::wstring& username,
                const std::wstring& password, std::wstring* error) {
    std::wstring exe = dir + L"\\" + kGameExe;
    std::wstring cmd = QuoteArg(exe) + L" /NoPatch " + QuoteArg(L"/AuthServer=" + host);
    if (!username.empty()) {
        cmd += L" " + QuoteArg(L"/user=" + username);
        if (!password.empty()) cmd += L" " + QuoteArg(L"/password=" + password);
    }
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi);
    SecureZeroMemory(&cmd[0], cmd.size() * sizeof(wchar_t));
    if (!ok) {
        *error = L"Failed to start the game: " + LastErrorText(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
