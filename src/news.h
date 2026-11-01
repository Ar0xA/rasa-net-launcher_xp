#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct NewsItem {
    std::wstring title;
    std::wstring link;
    std::wstring summary;
    /// Publication time (UTC); zero when the feed date is missing or unparseable.
    FILETIME date = {};
    /// Post type from the feed: Announcement, Patch notes, Community, Event, Dev blog.
    std::wstring category;
    /// True for posts in the feed's "Patch notes" category.
    bool patch_notes = false;
};

/// Latest posts from the site's RSS feed, newest first, at most 8 of each kind.
bool FetchNews(std::vector<NewsItem>* items, std::wstring* error);
/// Parses an RSS 2.0 document (exposed for testing).
std::vector<NewsItem> ParseRss(const std::string& xml);
