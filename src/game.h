#pragma once
#include <functional>
#include <string>
#include <vector>

extern const wchar_t kGameExe[];

struct VerifyResult {
    std::wstring version;
    size_t checked = 0;
    std::vector<std::wstring> missing;
    std::vector<std::wstring> corrupt;
};

bool IsGameDir(const std::wstring& dir);
/// Looks in "Programs and Features" and the folders the NCsoft installer used, on every drive.
std::wstring DetectGameDir();

/// Compares the install against the embedded manifest. `full` hashes every file;
/// otherwise only presence and size are checked. `progress` gets a 0..1 fraction.
bool VerifyGame(const std::wstring& dir, bool full, const std::function<void(double)>& progress,
                VerifyResult* result, std::wstring* error);

/// True if client.cfg has the intro movie turned off.
bool GetSkipIntro(const std::wstring& dir);
/// Sets ShowIntroMovie in client.cfg, leaving every other line untouched.
bool SetSkipIntro(const std::wstring& dir, bool skip, std::wstring* error);

/// Starts the client against `host`, logging in automatically when a username
/// (and optionally a password) is given.
bool LaunchGame(const std::wstring& dir, const std::wstring& host, const std::wstring& username,
                const std::wstring& password, std::wstring* error);
