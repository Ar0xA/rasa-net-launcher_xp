#include "settings.h"
#include <windows.h>
#include <wincred.h>
#include <wincrypt.h>
#include <rpc.h>
#include <wctype.h>
#include "json.h"
#include "util.h"

/// Credential Manager service name, shared with the Tauri launcher (keyring crate).
static const wchar_t kCredentialService[] = L"org.infiniterasa.launcher";

Settings Settings::Defaults() {
    Settings s;
    s.servers.push_back({L"infinite-rasa", L"Infinite Rasa", L"game.infiniterasa.org:2106", L""});
    s.selected_server = L"infinite-rasa";
    return s;
}

const Server* Settings::Selected() const {
    for (auto& sv : servers)
        if (sv.id == selected_server) return &sv;
    return nullptr;
}

static std::wstring SettingsPath() { return ConfigDir() + L"\\settings.json"; }

static Json LoadRaw() {
    std::string text;
    Json j;
    if (!ReadFileBytes(SettingsPath(), &text) || !Json::Parse(text, &j) || j.type != Json::Object) return Json::MakeObject();
    return j;
}

Settings LoadSettings() {
    Settings s = Settings::Defaults();
    Json j = LoadRaw();
    if (j.obj.empty()) return s;

    s.game_dir = Widen(j.StringOr("game_dir"));
    s.verified_dir = Widen(j.StringOr("verified_dir"));
    s.close_on_launch = j.BoolOr("close_on_launch", false);
    if (const Json* list = j.Get("servers"); list && list->type == Json::Array) {
        s.servers.clear();
        for (auto& v : list->arr) {
            Server sv{Widen(v.StringOr("id")), Widen(v.StringOr("name")), Widen(v.StringOr("host")),
                      Widen(v.StringOr("username"))};
            if (!sv.id.empty()) s.servers.push_back(sv);
        }
    }
    if (const Json* sel = j.Get("selected_server"); sel && sel->type == Json::String) s.selected_server = Widen(sel->str);
    return s;
}

bool SaveSettings(const Settings& s, std::wstring* error) {
    Json j = LoadRaw();
    auto pathOrNull = [](const std::wstring& p) { return p.empty() ? Json() : Json::MakeString(Narrow(p)); };
    j.Set("game_dir", pathOrNull(s.game_dir));
    j.Set("verified_dir", pathOrNull(s.verified_dir));
    Json list = Json::MakeArray();
    for (auto& sv : s.servers) {
        Json o = Json::MakeObject();
        o.Set("id", Json::MakeString(Narrow(sv.id)));
        o.Set("name", Json::MakeString(Narrow(sv.name)));
        o.Set("host", Json::MakeString(Narrow(sv.host)));
        o.Set("username", Json::MakeString(Narrow(sv.username)));
        list.arr.push_back(o);
    }
    j.Set("servers", list);
    j.Set("selected_server", Json::MakeString(Narrow(s.selected_server)));
    j.Set("close_on_launch", Json::MakeBool(s.close_on_launch));

    if (WriteFileAtomic(SettingsPath(), j.Dump(2))) return true;
    if (error) *error = L"Could not save settings: " + LastErrorText(GetLastError());
    return false;
}

// ---------- passwords ----------

/// keyring's default target name: "<user>.<service>".
static std::wstring CredentialTarget(const std::wstring& id) { return id + L"." + kCredentialService; }

static std::wstring DpapiPath(const std::wstring& id) {
    std::wstring dir = ConfigDir() + L"\\passwords";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring safe;
    for (wchar_t c : id) safe += iswalnum(c) || c == L'-' || c == L'_' ? c : L'_';
    return dir + L"\\" + safe + L".bin";
}

/// Highest persistence Credential Manager offers for generic credentials here.
static DWORD CredentialPersist() {
    DWORD types[CRED_TYPE_MAXIMUM] = {};
    if (!CredGetSessionTypes(CRED_TYPE_MAXIMUM, types)) return CRED_PERSIST_NONE;
    return types[CRED_TYPE_GENERIC];
}

static bool ReadCredential(const std::wstring& id, std::wstring* password) {
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(CredentialTarget(id).c_str(), CRED_TYPE_GENERIC, 0, &cred)) return false;
    // keyring stores the password as UTF-16LE.
    password->assign((const wchar_t*)cred->CredentialBlob, cred->CredentialBlobSize / sizeof(wchar_t));
    CredFree(cred);
    return true;
}

static bool ReadDpapi(const std::wstring& id, std::wstring* password) {
    std::string blob;
    if (!ReadFileBytes(DpapiPath(id), &blob)) return false;
    DATA_BLOB in = {(DWORD)blob.size(), (BYTE*)blob.data()}, out = {};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    password->assign((const wchar_t*)out.pbData, out.cbData / sizeof(wchar_t));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool HasPassword(const std::wstring& id) { return !GetPassword(id).empty(); }

std::wstring GetPassword(const std::wstring& id) {
    std::wstring pw;
    if (ReadCredential(id, &pw) || ReadDpapi(id, &pw)) return pw;
    return {};
}

bool SetPassword(const std::wstring& id, const std::wstring& password, std::wstring* error) {
    if (password.empty()) {
        if (!CredDeleteW(CredentialTarget(id).c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
            if (error) *error = L"Could not forget the password: " + LastErrorText(GetLastError());
            return false;
        }
        DeleteFileW(DpapiPath(id).c_str());
        return true;
    }

    DWORD persist = CredentialPersist();
    if (persist >= CRED_PERSIST_LOCAL_MACHINE) {
        std::wstring target = CredentialTarget(id);
        CREDENTIALW cred = {};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = &target[0];
        cred.CredentialBlobSize = (DWORD)(password.size() * sizeof(wchar_t));
        cred.CredentialBlob = (BYTE*)password.data();
        cred.Persist = persist >= CRED_PERSIST_ENTERPRISE ? CRED_PERSIST_ENTERPRISE : CRED_PERSIST_LOCAL_MACHINE;
        cred.UserName = const_cast<wchar_t*>(id.c_str());
        if (CredWriteW(&cred, 0)) {
            DeleteFileW(DpapiPath(id).c_str());
            return true;
        }
    }

    DATA_BLOB in = {(DWORD)(password.size() * sizeof(wchar_t)), (BYTE*)password.data()}, out = {};
    if (!CryptProtectData(&in, L"Infinite Rasa", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (error) *error = L"Could not save the password: " + LastErrorText(GetLastError());
        return false;
    }
    bool ok = WriteFileAtomic(DpapiPath(id), std::string((const char*)out.pbData, out.cbData));
    LocalFree(out.pbData);
    if (!ok && error) *error = L"Could not save the password: " + LastErrorText(GetLastError());
    return ok;
}

std::wstring NewServerId() {
    UUID uuid;
    UuidCreate(&uuid);
    RPC_WSTR str = nullptr;
    UuidToStringW(&uuid, &str);
    std::wstring id = str ? (const wchar_t*)str : L"server";
    RpcStringFreeW(&str);
    return id;
}
