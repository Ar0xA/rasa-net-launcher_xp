#include "util.h"
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

bool LoadResourceBytes(int id, const void** data, size_t* size) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HGLOBAL h = res ? LoadResource(nullptr, res) : nullptr;
    if (!h) return false;
    *data = LockResource(h);
    *size = SizeofResource(nullptr, res);
    return *data != nullptr;
}

bool ReadFileBytes(const std::wstring& path, std::string* out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    out->clear();
    char buf[16384];
    DWORD n;
    bool ok = true;
    while ((ok = ReadFile(f, buf, sizeof buf, &n, nullptr) != 0) && n > 0) out->append(buf, n);
    CloseHandle(f);
    return ok;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    if (ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DWORD err = GetLastError();
    DeleteFileW(tmp.c_str());
    SetLastError(err);
    return false;
}

std::wstring ConfigDir() {
    wchar_t path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, path))) return {};
    std::wstring dir = std::wstring(path) + L"\\org.infiniterasa.launcher";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') ++i, ++slashes;
        if (i == arg.size()) {
            out.append(slashes * 2, L'\\');  // they precede the closing quote
            break;
        }
        if (arg[i] == L'"') {
            out.append(slashes * 2 + 1, L'\\');
        } else {
            out.append(slashes, L'\\');
        }
        out += arg[i];
    }
    return out + L"\"";
}

std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
}

std::wstring LastErrorText(DWORD err) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, (LPWSTR)&msg, 0, nullptr);
    std::wstring text = msg ? Trim(msg) : Format(L"error %lu", err);
    LocalFree(msg);
    return text;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
