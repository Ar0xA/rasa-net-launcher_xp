#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const Json* Json::Get(const char* key) const {
    if (type != Object) return nullptr;
    for (auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

void Json::Set(const std::string& key, Json value) {
    type = Object;
    for (auto& kv : obj)
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    obj.emplace_back(key, std::move(value));
}

std::string Json::StringOr(const char* key, const std::string& fallback) const {
    const Json* v = Get(key);
    return v && v->type == String ? v->str : fallback;
}

bool Json::BoolOr(const char* key, bool fallback) const {
    const Json* v = Get(key);
    return v && v->type == Bool ? v->b : fallback;
}

namespace {

struct Parser {
    const char* p;
    const char* end;

    void Ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    }
    bool Lit(const char* s) {
        const char* q = p;
        for (; *s; ++s, ++q)
            if (q >= end || *q != *s) return false;
        p = q;
        return true;
    }
    static void PutUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool Hex4(unsigned* v) {
        if (end - p < 4) return false;
        *v = 0;
        for (int i = 0; i < 4; ++i, ++p) {
            char c = *p;
            *v <<= 4;
            if (c >= '0' && c <= '9') *v |= c - '0';
            else if (c >= 'a' && c <= 'f') *v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') *v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool Str(std::string* out) {
        if (p >= end || *p != '"') return false;
        ++p;
        out->clear();
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') {
                *out += c;
                continue;
            }
            if (p >= end) return false;
            switch (char e = *p++) {
                case '"': case '\\': case '/': *out += e; break;
                case 'b': *out += '\b'; break;
                case 'f': *out += '\f'; break;
                case 'n': *out += '\n'; break;
                case 'r': *out += '\r'; break;
                case 't': *out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!Hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo;
                        if (!Hex4(&lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    PutUtf8(*out, cp);
                    break;
                }
                default: return false;
            }
        }
        if (p >= end) return false;
        ++p;
        return true;
    }
    bool Value(Json* v, int depth) {
        if (depth > 64) return false;
        Ws();
        if (p >= end) return false;
        if (*p == '{') {
            ++p;
            v->type = Json::Object;
            Ws();
            if (p < end && *p == '}') return ++p, true;
            for (;;) {
                std::string key;
                Ws();
                if (!Str(&key)) return false;
                Ws();
                if (p >= end || *p++ != ':') return false;
                Json child;
                if (!Value(&child, depth + 1)) return false;
                v->obj.emplace_back(std::move(key), std::move(child));
                Ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') return ++p, true;
                return false;
            }
        }
        if (*p == '[') {
            ++p;
            v->type = Json::Array;
            Ws();
            if (p < end && *p == ']') return ++p, true;
            for (;;) {
                Json child;
                if (!Value(&child, depth + 1)) return false;
                v->arr.push_back(std::move(child));
                Ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') return ++p, true;
                return false;
            }
        }
        if (*p == '"') {
            v->type = Json::String;
            return Str(&v->str);
        }
        if (Lit("true")) { v->type = Json::Bool; v->b = true; return true; }
        if (Lit("false")) { v->type = Json::Bool; v->b = false; return true; }
        if (Lit("null")) { v->type = Json::Null; return true; }
        std::string num;
        while (p < end && (strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) num += *p++;
        if (num.empty()) return false;
        v->type = Json::Number;
        v->num = strtod(num.c_str(), nullptr);
        return true;
    }
};

void Quote(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    out += '"';
}

void DumpTo(const Json& v, std::string& out, int indent, int level) {
    auto newline = [&](int lvl) {
        if (!indent) return;
        out += '\n';
        out.append((size_t)lvl * indent, ' ');
    };
    switch (v.type) {
        case Json::Null: out += "null"; break;
        case Json::Bool: out += v.b ? "true" : "false"; break;
        case Json::Number: {
            char buf[32];
            if (v.num == (double)(long long)v.num) sprintf_s(buf, "%lld", (long long)v.num);
            else sprintf_s(buf, "%.17g", v.num);
            out += buf;
            break;
        }
        case Json::String: Quote(out, v.str); break;
        case Json::Array:
            out += '[';
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                DumpTo(v.arr[i], out, indent, level + 1);
            }
            if (!v.arr.empty()) newline(level);
            out += ']';
            break;
        case Json::Object:
            out += '{';
            for (size_t i = 0; i < v.obj.size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                Quote(out, v.obj[i].first);
                out += indent ? ": " : ":";
                DumpTo(v.obj[i].second, out, indent, level + 1);
            }
            if (!v.obj.empty()) newline(level);
            out += '}';
            break;
    }
}

}  // namespace

bool Json::Parse(const std::string& text, Json* out) {
    size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;  // PowerShell writes a BOM
    Parser ps{text.data() + start, text.data() + text.size()};
    *out = Json();
    if (!ps.Value(out, 0)) return false;
    ps.Ws();
    return ps.p == ps.end;
}

std::string Json::Dump(int indent) const {
    std::string out;
    DumpTo(*this, out, indent, 0);
    return out;
}
