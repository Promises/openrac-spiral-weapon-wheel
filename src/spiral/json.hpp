// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small JSON reader for icon packs' icons.json (icon_packs.hpp): objects, arrays, strings (the
// usual escapes, \u as UTF-8), numbers, true, false and null. Reads a whole text or fails with a
// reason and where.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace spiral::json {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;  // in the text's order

    // The member `key` of an object; nullptr when it has none (or is not an object).
    const Value* find(const std::string& key) const;
    bool is_string() const { return kind == Kind::String; }
    bool is_number() const { return kind == Kind::Number; }
};

// Parses `text`; false (and `why` says what and at which line) when it is not JSON.
bool parse(const std::string& text, Value& out, std::string& why);

}  // namespace spiral::json
