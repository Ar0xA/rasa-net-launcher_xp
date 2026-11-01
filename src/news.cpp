#include "news.h"
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "https.h"
#include "util.h"

static const char kFeedHost[] = "infiniterasa.org";
static const char kFeedPath[] = "/news/rss.xml";
static const size_t kMaxPerKind = 8;

static void PutUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6)), out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12)), out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18)), out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
    }
}

/// Element text with CDATA sections unwrapped and XML entities decoded.
static std::string XmlText(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size();) {
        if (raw.compare(i, 9, "<![CDATA[") == 0) {
            size_t end = raw.find("]]>", i + 9);
            if (end == std::string::npos) end = raw.size();
            out.append(raw, i + 9, end - i - 9);
            i = end + 3;
        } else if (raw[i] == '&') {
            size_t semi = raw.find(';', i);
            std::string ent = semi == std::string::npos ? "" : raw.substr(i + 1, semi - i - 1);
            if (ent == "amp") out += '&';
            else if (ent == "lt") out += '<';
            else if (ent == "gt") out += '>';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else if (ent.size() > 1 && ent[0] == '#')
                PutUtf8(out, ent[1] == 'x' || ent[1] == 'X' ? strtoul(ent.c_str() + 2, nullptr, 16)
                                                            : strtoul(ent.c_str() + 1, nullptr, 10));
            else {
                out += '&';
                ++i;
                continue;
            }
            i = semi + 1;
        } else {
            out += raw[i++];
        }
    }
    return out;
}

/// Text of the first <tag>…</tag> in `item`, or empty.
static std::string Element(const std::string& item, const char* tag) {
    std::string open = std::string("<") + tag;
    for (size_t p = item.find(open); p != std::string::npos; p = item.find(open, p + 1)) {
        char next = item[p + open.size()];
        if (next != '>' && next != ' ' && next != '/') continue;  // e.g. <title> vs <titles>
        size_t start = item.find('>', p);
        if (start == std::string::npos || item[start - 1] == '/') return {};
        size_t end = item.find(std::string("</") + tag + ">", start);
        if (end == std::string::npos) return {};
        return XmlText(item.substr(start + 1, end - start - 1));
    }
    return {};
}

static std::wstring StripHtml(const std::string& s) {
    std::string text;
    bool inTag = false;
    for (char c : s) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) text += c;
    }
    std::wstring w = Widen(text), out;
    for (wchar_t c : w) {
        bool space = c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
        if (!space) out += c;
        else if (!out.empty() && out.back() != L' ') out += L' ';
    }
    return Trim(out);
}

/// RFC 2822 date ("Wed, 07 Oct 2026 18:38:58 GMT" or "+0200") to UTC.
static bool ParseRfc2822(const std::string& s, FILETIME* out) {
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char* p = s.c_str();
    if (const char* comma = strchr(p, ',')) p = comma + 1;
    int day, year, h, m, sec = 0;
    char mon[4] = {}, zone[8] = {};
    int n = sscanf_s(p, " %d %3s %d %d:%d:%d %7s", &day, mon, (unsigned)sizeof mon, &year, &h, &m, &sec, zone,
                     (unsigned)sizeof zone);
    if (n < 5) return false;
    SYSTEMTIME st = {};
    for (int i = 0; i < 12; ++i)
        if (_stricmp(mon, kMonths[i]) == 0) st.wMonth = (WORD)(i + 1);
    if (!st.wMonth) return false;
    st.wYear = (WORD)(year < 100 ? year + 2000 : year);
    st.wDay = (WORD)day, st.wHour = (WORD)h, st.wMinute = (WORD)m, st.wSecond = (WORD)sec;
    if (!SystemTimeToFileTime(&st, out)) return false;
    if ((zone[0] == '+' || zone[0] == '-') && strlen(zone) == 5) {
        int offset = ((zone[1] - '0') * 10 + (zone[2] - '0')) * 60 + (zone[3] - '0') * 10 + (zone[4] - '0');
        ULARGE_INTEGER t = {out->dwLowDateTime, out->dwHighDateTime};
        long long delta = (long long)offset * 60 * 10000000;
        t.QuadPart = zone[0] == '+' ? t.QuadPart - delta : t.QuadPart + delta;
        out->dwLowDateTime = t.LowPart, out->dwHighDateTime = t.HighPart;
    }
    return true;
}

static ULONGLONG Ticks(const FILETIME& f) { return (ULONGLONG)f.dwHighDateTime << 32 | f.dwLowDateTime; }

std::vector<NewsItem> ParseRss(const std::string& xml) {
    std::vector<NewsItem> items;
    for (size_t p = xml.find("<item"); p != std::string::npos; p = xml.find("<item", p + 1)) {
        size_t end = xml.find("</item>", p);
        if (end == std::string::npos) break;
        std::string item = xml.substr(p, end - p);
        NewsItem n;
        std::string title = Element(item, "title");
        n.title = title.empty() ? L"Untitled" : Widen(title);
        n.link = Widen(Element(item, "link"));
        n.summary = StripHtml(Element(item, "description"));
        n.category = Widen(Element(item, "category"));
        ParseRfc2822(Element(item, "pubDate"), &n.date);
        // Any category mentioning "patch" makes it patch notes, as in the Tauri launcher.
        for (size_t c = item.find("<category"); c != std::string::npos; c = item.find("<category", c + 1)) {
            std::string cat = Element(item.substr(c), "category");
            for (auto& ch : cat) ch = (char)tolower((unsigned char)ch);
            if (cat.find("patch") != std::string::npos) n.patch_notes = true;
        }
        items.push_back(n);
        p = end;
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const NewsItem& a, const NewsItem& b) { return Ticks(a.date) > Ticks(b.date); });

    // Keep the newest few of each kind so a burst of one never hides the other.
    size_t news = 0, patches = 0;
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const NewsItem& i) { return ++(i.patch_notes ? patches : news) > kMaxPerKind; }),
                items.end());
    return items;
}

bool FetchNews(std::vector<NewsItem>* items, std::wstring* error) {
    std::string xml;
    if (!HttpsGet(kFeedHost, kFeedPath, &xml, error)) return false;
    *items = ParseRss(xml);
    return true;
}
