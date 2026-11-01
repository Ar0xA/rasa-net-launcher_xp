#pragma once
#include <string>
#include <vector>

struct Server {
    std::wstring id;
    std::wstring name;
    /// Auth server as host:port, passed to the client as /AuthServer=.
    std::wstring host;
    std::wstring username;
};

/// Same file and format as the Tauri launcher (%APPDATA%\org.infiniterasa.launcher\settings.json),
/// so both can be used on one PC.
struct Settings {
    std::wstring game_dir;  // empty: not set
    /// Folder that last passed a full (hashed) verify; a different folder needs one first.
    std::wstring verified_dir;
    std::vector<Server> servers;
    std::wstring selected_server;
    bool close_on_launch = false;

    static Settings Defaults();
    const Server* Selected() const;
};

Settings LoadSettings();
/// Writes the settings, keeping any keys this launcher does not know about.
bool SaveSettings(const Settings& s, std::wstring* error);

/// Saved passwords live in Windows Credential Manager, under the same names the Tauri
/// launcher uses. Where Credential Manager cannot persist them (XP Home), they are
/// encrypted with DPAPI for the current user and kept next to settings.json.
bool HasPassword(const std::wstring& server_id);
std::wstring GetPassword(const std::wstring& server_id);
/// Stores the password, or forgets it when `password` is empty.
bool SetPassword(const std::wstring& server_id, const std::wstring& password, std::wstring* error);

std::wstring NewServerId();
