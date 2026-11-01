// Console checks for the launcher's logic. Built and run by `build.cmd test`.
// Pass --offline to skip the live HTTPS fetch of the news feed.

#include <windows.h>
#include <stdio.h>
#include <string>
#include "../src/game.h"
#include "../src/https.h"
#include "../src/json.h"
#include "../src/news.h"
#include "../src/sha256.h"
#include "../src/util.h"

static int g_failed = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failed;                                                   \
        }                                                                 \
    } while (0)

static std::string Sha(const std::string& s) {
    Sha256 h;
    h.Update(s.data(), s.size());
    return h.HexDigest();
}

static void TestSha256() {
    CHECK(Sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string fips;  // "abcdbcdecdef...nopq": 56 bytes, so the padding needs a second block
    for (int i = 0; i < 14; ++i) fips += std::string("abcdefghijklmnopq").substr(i, 4);
    CHECK(fips.size() == 56);
    CHECK(Sha(fips) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Fed in uneven pieces across block boundaries.
    std::string million(1000000, 'a');
    Sha256 h;
    for (size_t i = 0; i < million.size(); i += 777) h.Update(million.data() + i, min((size_t)777, million.size() - i));
    CHECK(h.HexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void TestJson() {
    Json j;
    CHECK(Json::Parse("\xEF\xBB\xBF{\"a\": [1, 2.5, true, null], \"s\": \"x\\u00e9\\ud83d\\ude00\\n\"}", &j));
    CHECK(j.Get("a") && j.Get("a")->arr.size() == 4 && j.Get("a")->arr[1].num == 2.5);
    CHECK(j.StringOr("s") == "x\xC3\xA9\xF0\x9F\x98\x80\n");
    Json again;
    CHECK(Json::Parse(j.Dump(2), &again) && again.StringOr("s") == j.StringOr("s"));
    j.Set("s", Json::MakeString("y"));
    CHECK(j.obj.size() == 2 && j.obj[1].first == "s" && j.StringOr("s") == "y");
    Json bad;
    CHECK(!Json::Parse("{\"a\":}", &bad));
    CHECK(!Json::Parse("[1,2", &bad));
}

static void TestQuoteArg() {
    CHECK(QuoteArg(L"/NoPatch") == L"/NoPatch");
    CHECK(QuoteArg(L"/user=Joe Bloggs") == L"\"/user=Joe Bloggs\"");
    CHECK(QuoteArg(L"C:\\Program Files\\x\\") == L"\"C:\\Program Files\\x\\\\\"");
    CHECK(QuoteArg(L"a\"b") == L"\"a\\\"b\"");
    CHECK(QuoteArg(L"") == L"\"\"");
    // Round trip through Windows' own splitter.
    std::wstring line = L"x " + QuoteArg(L"/password=p a\"s\\\\\"s\\") + L" " + QuoteArg(L"/user=o'neil");
    int argc;
    LPWSTR* argv = CommandLineToArgvW(line.c_str(), &argc);
    CHECK(argc == 3 && std::wstring(argv[1]) == L"/password=p a\"s\\\\\"s\\" && std::wstring(argv[2]) == L"/user=o'neil");
    LocalFree(argv);
}

static void TestSkipIntro() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + Format(L"irl-xp-test-%lu", GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring cfg = dir + L"\\client.cfg";
    std::wstring err;
    std::string text;

    CHECK(!GetSkipIntro(dir));
    CHECK(SetSkipIntro(dir, true, &err));
    CHECK(ReadFileBytes(cfg, &text) && text == "[ConfigInfo]\r\nVersion = 2\r\n\r\n[Options]\r\nClient.UserInterface.ShowIntroMovie = False\r\n");
    CHECK(GetSkipIntro(dir));

    WriteFileAtomic(cfg, "[Options]\nA = 1\nClient.UserInterface.ShowIntroMovie = false\nB = 2\n");
    CHECK(GetSkipIntro(dir));
    CHECK(SetSkipIntro(dir, false, &err));
    CHECK(ReadFileBytes(cfg, &text) && text == "[Options]\nA = 1\nClient.UserInterface.ShowIntroMovie = True\nB = 2\n");
    CHECK(!GetSkipIntro(dir));

    WriteFileAtomic(cfg, "[Display]\r\nX = 1\r\n[Options]\r\nY = 2\r\n");
    CHECK(SetSkipIntro(dir, true, &err));
    CHECK(ReadFileBytes(cfg, &text) &&
          text == "[Display]\r\nX = 1\r\n[Options]\r\nClient.UserInterface.ShowIntroMovie = False\r\nY = 2\r\n");

    CHECK(!IsGameDir(dir));
    WriteFileAtomic(dir + L"\\tabula_rasa.exe", "");
    CHECK(IsGameDir(dir));

    // A fake install: everything missing except one file of the wrong size.
    VerifyResult r;
    CHECK(VerifyGame(dir, true, [](double) {}, &r, &err));
    CHECK(r.version == L"1.16.5.0" && r.checked > 500);
    CHECK(r.missing.size() + r.corrupt.size() == r.checked);

    DeleteFileW((dir + L"\\tabula_rasa.exe").c_str());
    DeleteFileW(cfg.c_str());
    RemoveDirectoryW(dir.c_str());
}

static void TestRss() {
    std::string xml =
        "<?xml version=\"1.0\"?><rss><channel><title>T</title>"
        "<item><title>Old &amp; gold</title><link>https://x/old</link>"
        "<pubDate>Wed, 07 Oct 2026 18:38:58 GMT</pubDate><category>Announcement</category>"
        "<description>&lt;p&gt;Hello   &lt;b&gt;world&lt;/b&gt;&lt;/p&gt; it&apos;s</description></item>"
        "<item><title><![CDATA[Patch <1>]]></title><link>https://x/patch</link>"
        "<pubDate>Tue, 27 Oct 2026 16:16:57 +0200</pubDate><category>Patch notes</category>"
        "<description><![CDATA[<ul><li>Fix</li></ul>]]></description></item>"
        "<item><title>No date</title></item>"
        "</channel></rss>";
    auto items = ParseRss(xml);
    CHECK(items.size() == 3);
    if (items.size() != 3) return;
    CHECK(items[0].title == L"Patch <1>" && items[0].patch_notes && items[0].summary == L"Fix");
    CHECK(items[1].title == L"Old & gold" && !items[1].patch_notes && items[1].category == L"Announcement");
    CHECK(items[1].summary == L"Hello world it's");
    CHECK(items[2].title == L"No date" && items[2].date.dwHighDateTime == 0);
    SYSTEMTIME st;
    FileTimeToSystemTime(&items[0].date, &st);
    CHECK(st.wYear == 2026 && st.wMonth == 10 && st.wDay == 27 && st.wHour == 14 && st.wMinute == 16);

    std::string many = "<rss>";
    for (int i = 0; i < 12; ++i) many += "<item><title>n</title></item><item><title>p</title><category>Patch notes</category></item>";
    CHECK(ParseRss(many + "</rss>").size() == 16);  // 8 of each kind
}

static void TestLiveFeed() {
    std::vector<NewsItem> items;
    std::wstring err;
    bool ok = FetchNews(&items, &err);
    printf("live feed: %s, %u items%s%ls\n", ok ? "ok" : "FAILED", (unsigned)items.size(), ok ? "" : ": ", err.c_str());
    CHECK(ok && !items.empty());
    for (size_t i = 0; i < items.size() && i < 3; ++i)
        printf("  [%ls] %ls\n", items[i].patch_notes ? L"patch" : items[i].category.c_str(), items[i].title.c_str());

    std::string body;
    CHECK(!HttpsGet("expired.badssl.com", "/", &body, &err));
    printf("expired certificate rejected: %ls\n", err.c_str());
    CHECK(!HttpsGet("self-signed.badssl.com", "/", &body, &err));
    CHECK(!HttpsGet("wrong.host.badssl.com", "/", &body, &err));
}

int wmain(int argc, wchar_t** argv) {
    TestSha256();
    TestJson();
    TestQuoteArg();
    TestSkipIntro();
    TestRss();
    if (!(argc > 1 && wcscmp(argv[1], L"--offline") == 0)) TestLiveFeed();
    printf(g_failed ? "%d check(s) failed\n" : "all checks passed\n", g_failed);
    return g_failed ? 1 : 0;
}
