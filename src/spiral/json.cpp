// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small JSON reader (json.hpp).
#include "json.hpp"

#include <cstdlib>

namespace spiral::json {

const Value* Value::find(const std::string& key) const {
    if (kind != Kind::Object) {
        return nullptr;
    }
    for (const auto& [k, v] : object) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

namespace {

struct Reader {
    const std::string& s;
    std::size_t i = 0;
    std::string why;
    int depth = 0;

    bool fail(const char* what) {
        int line = 1;
        for (std::size_t k = 0; k < i && k < s.size(); ++k) {
            line += s[k] == '\n';
        }
        why = std::string(what) + " at line " + std::to_string(line);
        return false;
    }
    void space() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
            ++i;
        }
    }
    bool literal(const char* word) {
        std::size_t n = 0;
        while (word[n] != 0) {
            ++n;
        }
        if (s.compare(i, n, word) != 0) {
            return false;
        }
        i += n;
        return true;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& cp) {
        if (i + 4 > s.size()) {
            return false;
        }
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            cp <<= 4;
            if (c >= '0' && c <= '9') {
                cp |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                cp |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                cp |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }
    bool string(std::string& out) {
        ++i;  // the opening quote
        while (i < s.size() && s[i] != '"') {
            const char c = s[i++];
            if (static_cast<unsigned char>(c) < 0x20) {
                return fail("a control character in a string");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) {
                break;
            }
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) {
                        return fail("a bad \\u escape");
                    }
                    if (cp >= 0xD800 && cp < 0xDC00 && s.compare(i, 2, "\\u") == 0) {
                        i += 2;
                        unsigned low = 0;
                        if (!hex4(low) || low < 0xDC00 || low >= 0xE000) {
                            return fail("a bad surrogate pair");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("a bad escape");
            }
        }
        if (i >= s.size()) {
            return fail("a string without its end");
        }
        ++i;
        return true;
    }
    bool value(Value& v) {
        space();
        if (i >= s.size()) {
            return fail("no value");
        }
        if (++depth > 64) {
            return fail("too deep");
        }
        bool ok = true;
        const char c = s[i];
        if (c == '{') {
            v.kind = Value::Kind::Object;
            ++i;
            space();
            if (i < s.size() && s[i] == '}') {
                ++i;
            } else {
                for (;;) {
                    space();
                    if (i >= s.size() || s[i] != '"') {
                        ok = fail("a key that is not a string");
                        break;
                    }
                    std::string key;
                    if (!string(key)) {
                        ok = false;
                        break;
                    }
                    space();
                    if (i >= s.size() || s[i] != ':') {
                        ok = fail("no ':' after a key");
                        break;
                    }
                    ++i;
                    Value member;
                    if (!value(member)) {
                        ok = false;
                        break;
                    }
                    v.object.emplace_back(std::move(key), std::move(member));
                    space();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == '}') {
                        ++i;
                        break;
                    }
                    ok = fail("no ',' or '}' in an object");
                    break;
                }
            }
        } else if (c == '[') {
            v.kind = Value::Kind::Array;
            ++i;
            space();
            if (i < s.size() && s[i] == ']') {
                ++i;
            } else {
                for (;;) {
                    Value element;
                    if (!value(element)) {
                        ok = false;
                        break;
                    }
                    v.array.push_back(std::move(element));
                    space();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == ']') {
                        ++i;
                        break;
                    }
                    ok = fail("no ',' or ']' in an array");
                    break;
                }
            }
        } else if (c == '"') {
            v.kind = Value::Kind::String;
            ok = string(v.string);
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            v.kind = Value::Kind::Number;
            const char* start = s.c_str() + i;
            char* end = nullptr;
            v.number = std::strtod(start, &end);
            if (end == start) {
                ok = fail("a bad number");
            } else {
                i += static_cast<std::size_t>(end - start);
            }
        } else if (literal("true")) {
            v.kind = Value::Kind::Bool;
            v.boolean = true;
        } else if (literal("false")) {
            v.kind = Value::Kind::Bool;
        } else if (literal("null")) {
            v.kind = Value::Kind::Null;
        } else {
            ok = fail("an unexpected character");
        }
        --depth;
        return ok;
    }
};

}  // namespace

bool parse(const std::string& text, Value& out, std::string& why) {
    Reader r{text, 0, {}, 0};
    out = Value{};
    // A UTF-8 byte order mark before it is allowed.
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        r.i = 3;
    }
    if (!r.value(out)) {
        why = r.why;
        return false;
    }
    r.space();
    if (r.i != text.size()) {
        r.fail("more after the value");
        why = r.why;
        return false;
    }
    return true;
}

}  // namespace spiral::json
