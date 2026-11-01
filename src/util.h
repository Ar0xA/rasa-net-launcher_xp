#pragma once
#include <windows.h>
#include <string>
#include <vector>

std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);

/// Bytes of an RCDATA resource compiled into the exe.
bool LoadResourceBytes(int id, const void** data, size_t* size);

bool ReadFileBytes(const std::wstring& path, std::string* out);
/// Writes through a temporary file so a crash never leaves a half-written file.
bool WriteFileAtomic(const std::wstring& path, const std::string& data);

/// %APPDATA%\org.infiniterasa.launcher, the folder the Tauri launcher uses too.
std::wstring ConfigDir();

/// One argument quoted the way CommandLineToArgvW / the MSVC runtime split it.
std::wstring QuoteArg(const std::wstring& arg);

std::wstring Format(const wchar_t* fmt, ...);
std::wstring LastErrorText(DWORD err);
std::wstring Trim(const std::wstring& s);
