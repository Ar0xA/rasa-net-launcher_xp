#pragma once
#include <string>
#include <utility>
#include <vector>

/// Just enough JSON for settings.json and the embedded file manifest. Strings are UTF-8.
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    static Json MakeString(const std::string& s) { Json j; j.type = String; j.str = s; return j; }
    static Json MakeBool(bool v) { Json j; j.type = Bool; j.b = v; return j; }
    static Json MakeArray() { Json j; j.type = Array; return j; }
    static Json MakeObject() { Json j; j.type = Object; return j; }

    /// Member of an object, or nullptr.
    const Json* Get(const char* key) const;
    /// Sets (or adds) an object member, keeping the order of existing keys.
    void Set(const std::string& key, Json value);

    std::string StringOr(const char* key, const std::string& fallback = {}) const;
    bool BoolOr(const char* key, bool fallback) const;

    /// Parses `text`; on failure returns false and leaves `out` unspecified.
    static bool Parse(const std::string& text, Json* out);
    std::string Dump(int indent = 0) const;
};
