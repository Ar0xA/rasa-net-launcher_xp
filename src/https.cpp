#include "https.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <vector>
#include "bearssl.h"
#include "util.h"

namespace {

#include "roots.inc"

std::vector<br_x509_trust_anchor> g_anchors;

void AppendDn(void* ctx, const void* buf, size_t len) {
    auto* v = (std::vector<unsigned char>*)ctx;
    v->insert(v->end(), (const unsigned char*)buf, (const unsigned char*)buf + len);
}

unsigned char* Copy(const unsigned char* p, size_t n) {
    auto* out = new unsigned char[n ? n : 1];
    memcpy(out, p, n);
    return out;
}

/// Decodes the bundled root certificates once; they live for the rest of the process.
const std::vector<br_x509_trust_anchor>& Anchors() {
    if (!g_anchors.empty()) return g_anchors;
    for (auto& root : kRoots) {
        std::vector<unsigned char> dn;
        br_x509_decoder_context dc;
        br_x509_decoder_init(&dc, AppendDn, &dn);
        br_x509_decoder_push(&dc, root.der, root.len);
        const br_x509_pkey* pk = br_x509_decoder_get_pkey(&dc);
        if (!pk) continue;
        br_x509_trust_anchor ta = {};
        ta.dn.data = Copy(dn.data(), dn.size());
        ta.dn.len = dn.size();
        ta.flags = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;
        ta.pkey.key_type = pk->key_type;
        if (pk->key_type == BR_KEYTYPE_RSA) {
            ta.pkey.key.rsa.n = Copy(pk->key.rsa.n, pk->key.rsa.nlen);
            ta.pkey.key.rsa.nlen = pk->key.rsa.nlen;
            ta.pkey.key.rsa.e = Copy(pk->key.rsa.e, pk->key.rsa.elen);
            ta.pkey.key.rsa.elen = pk->key.rsa.elen;
        } else if (pk->key_type == BR_KEYTYPE_EC) {
            ta.pkey.key.ec.curve = pk->key.ec.curve;
            ta.pkey.key.ec.q = Copy(pk->key.ec.q, pk->key.ec.qlen);
            ta.pkey.key.ec.qlen = pk->key.ec.qlen;
        } else {
            continue;
        }
        g_anchors.push_back(ta);
    }
    return g_anchors;
}

int SockRead(void* ctx, unsigned char* buf, size_t len) {
    int n = recv(*(SOCKET*)ctx, (char*)buf, (int)len, 0);
    return n > 0 ? n : -1;
}

int SockWrite(void* ctx, const unsigned char* buf, size_t len) {
    int n = send(*(SOCKET*)ctx, (const char*)buf, (int)len, 0);
    return n > 0 ? n : -1;
}

SOCKET Connect(const std::string& host, std::wstring* error) {
    static bool started = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    if (!started) {
        *error = L"Networking is unavailable.";
        return INVALID_SOCKET;
    }
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0) {
        *error = L"Could not resolve " + Widen(host) + L".";
        return INVALID_SOCKET;
    }
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        DWORD timeout = 15000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout);
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) *error = L"Could not connect to " + Widen(host) + L".";
    return s;
}

bool Dechunk(const std::string& in, std::string* out) {
    out->clear();
    size_t p = 0;
    for (;;) {
        size_t eol = in.find("\r\n", p);
        if (eol == std::string::npos) return false;
        unsigned long n = strtoul(in.substr(p, eol - p).c_str(), nullptr, 16);
        p = eol + 2;
        if (n == 0) return true;
        if (p + n > in.size()) return false;
        out->append(in, p, n);
        p += n + 2;
    }
}

}  // namespace

bool HttpsGet(const std::string& host, const std::string& path, std::string* body, std::wstring* error) {
    const auto& anchors = Anchors();
    SOCKET sock = Connect(host, error);
    if (sock == INVALID_SOCKET) return false;

    auto* sc = new br_ssl_client_context;
    auto* xc = new br_x509_minimal_context;
    std::vector<unsigned char> iobuf(BR_SSL_BUFSIZE_BIDI);
    br_ssl_client_init_full(sc, xc, anchors.data(), anchors.size());
    br_ssl_engine_set_buffer(&sc->eng, iobuf.data(), iobuf.size(), 1);
    br_ssl_client_reset(sc, host.c_str(), 0);
    br_sslio_context io;
    br_sslio_init(&io, &sc->eng, SockRead, &sock, SockWrite, &sock);

    // HTTP/1.0 so the reply is neither chunked nor kept alive.
    std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host +
                      "\r\nUser-Agent: InfiniteRasaLauncherXP/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    std::string raw;
    bool sent = br_sslio_write_all(&io, req.data(), req.size()) == 0 && br_sslio_flush(&io) == 0;
    if (sent) {
        char buf[8192];
        int n;
        while ((n = br_sslio_read(&io, buf, sizeof buf)) > 0) raw.append(buf, n);
    }
    int tlsErr = br_ssl_engine_last_error(&sc->eng);
    closesocket(sock);
    delete xc;
    delete sc;

    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        *error = tlsErr ? Format(L"Secure connection to %hs failed (BearSSL error %d).", host.c_str(), tlsErr)
                        : L"No response from " + Widen(host) + L".";
        return false;
    }
    std::string headers = raw.substr(0, headerEnd);
    std::string content = raw.substr(headerEnd + 4);
    int status = 0;
    sscanf_s(headers.c_str(), "HTTP/%*d.%*d %d", &status);
    if (status != 200) {
        *error = Format(L"%hs answered HTTP %d.", host.c_str(), status);
        return false;
    }

    std::string lower = headers;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    if (lower.find("transfer-encoding: chunked") != std::string::npos) {
        if (!Dechunk(content, body)) {
            *error = L"Incomplete response from " + Widen(host) + L".";
            return false;
        }
        return true;
    }
    // Servers often close without a TLS close_notify; Content-Length tells whether all arrived.
    size_t cl = lower.find("\r\ncontent-length:");
    if (cl != std::string::npos) {
        size_t want = strtoul(lower.c_str() + cl + 17, nullptr, 10);
        if (content.size() < want) {
            *error = L"Incomplete response from " + Widen(host) + L".";
            return false;
        }
        content.resize(want);
    }
    *body = content;
    return true;
}
