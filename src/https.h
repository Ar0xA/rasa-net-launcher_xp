#pragma once
#include <string>

/// GET https://host/path with TLS 1.2 from the bundled BearSSL. XP's own TLS stack
/// (SChannel) stops at TLS 1.0 without modern ciphers or SNI, which today's sites refuse.
/// Returns the response body; follows no redirects.
bool HttpsGet(const std::string& host, const std::string& path, std::string* body, std::wstring* error);
