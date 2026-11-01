#pragma once
#include <windows.h>
#include <string>
#include "settings.h"

enum class SettingsResult { Cancelled, Saved, FullVerify };

/// The Settings dialog. Edits a copy and applies it to `settings` (and saves it) only on Save.
SettingsResult ShowSettingsDialog(HWND owner, Settings* settings);

/// Folder picker; returns the chosen folder only if it contains the client, else empty.
std::wstring PickGameDir(HWND owner, const std::wstring& start);
