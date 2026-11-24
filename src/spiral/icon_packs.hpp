// SPDX-License-Identifier: GPL-3.0-or-later
//
// Icon packs: the wheel's glyphs from files (ICONS.md is the guide for their authors). The mod's
// own glyphs are a pack like any other (assets/glyphs/rac1/), read the same way. Game-agnostic:
// the layer names the folders, in the order they count (a later pack's icon over an earlier's).
//
// A pack is a folder with an icons.json:
//
//   {"format": 1, "name": "...", "author": "...", "license": "...",
//    "icons": [{"game": "rac1", "item": "0x0A", "glyph": "bomb-glove.svg", "category": "weapon"}]}
//
// each icon: its game, its item (a number, or hexadecimal as a string: "0x0A", "0a"; or, for a thing
// the game keeps as a flag of its own rather than an item, "special": its name, kept with the pack
// but never on the wheel), its glyph (a
// file in the pack's folder: SVG, one shape in fill="currentColor" on a 256 view box, or PNG, white
// on transparent), its category (look.hpp: weapon, gold, gadget, pack, head, feet, item; optional:
// the item's own otherwise), and optional colours over the category's: "wash", "glyph_color",
// "selected_glyph_color" ("#RRGGBB"). Or, without an icons.json, plain files: <game>/<item>.svg or
// .png (<item> in hexadecimal), each taking its item's category and colours.
//
// The folders are read at a level's load: a pack added, changed or removed counts from that level
// on. A glyph's picture is read (an SVG drawn at kGlyphRaster pixels) when first asked for, and kept
// while its file is unchanged.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "glyph.hpp"
#include "look.hpp"
#include "wheel.hpp"

namespace spiral {

// icons.json's format this reader knows.
constexpr int kPackFormat = 1;
// An SVG glyph is drawn this many pixels square (the largest a slot shows it is about 170 on a 1080
// line screen at the largest size: the overlay's smooth sampling scales it down from here).
constexpr int kGlyphRaster = 256;

struct PackIcon {
    std::string game;
    int item = 0;
    std::string file;  // the glyph's path
    bool has_category = false;
    look::Category category = look::Category::Weapon;
    Item::Colours colours;
    std::string pack;  // its pack's name (or folder)
    std::string special;  // "special": a thing that is not an item (never looked up by item)
};

struct IconPack {
    std::string folder, name, author, license;
    int format = 0;
    bool manifest = false;  // an icons.json (else plain files)
    std::vector<PackIcon> icons;
};

// An item as icons.json or a file name writes it: a number, or hexadecimal ("0x0A", "0a", "A").
bool parse_item(const std::string& text, int& item);
// A colour as "#RRGGBB".
bool parse_colour(const std::string& text, look::Rgb& out);
// A category's name (look.hpp's kCategoryColours names).
bool parse_category(const std::string& text, look::Category& out);

// Reads the pack in `folder`. False when there is none there (`problems` empty) or its icons.json
// cannot be read; an icon that is not right is left out, and `problems` says why.
bool read_pack(const std::string& folder, IconPack& out, std::vector<std::string>& problems);

// A glyph's picture from its file: white, its shape in the alpha, cropped to the shape with a
// pixel of room. False (and `why`) when it cannot be read or has no shape.
bool load_glyph(const std::string& path, Picture& out, std::string& why);

class IconPacks {
public:
    // Reads the packs in `folders` (in the order they count) for `game`, at a level's load. Lines
    // for the log: each pack read (its name, author, licence and icons), each icon a later pack
    // takes over, and the problems.
    void refresh(const std::vector<std::string>& folders, const std::string& game, std::vector<std::string>* log = nullptr);

    // The icon for `item` (the last pack's that has one), and its picture; nullptr for none.
    const PackIcon* icon(int item) const;
    const Picture* picture(const PackIcon& icon, std::string& why);
    std::size_t icons() const { return m_icons.size(); }

private:
    struct Cached {
        std::uintmax_t size = 0;
        long long time = 0;
        bool ok = false;
        std::string why;
        Picture picture;
    };
    std::map<int, PackIcon> m_icons;
    std::map<std::string, Cached> m_pictures;  // by path
};

}  // namespace spiral
