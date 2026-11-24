// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral weapon wheel, game-agnostic: the items it shows, the pointer the stick turns
// (spiral.hpp), and what is drawn where, as "Prey Menu" lays it out (PreyMenu.xml; spiral.hpp
// says whose and from where). A game's layer (src/gc/rac1_shim.*) fills the items, feeds the stick,
// draws the pictures (the mod's overlay) and the icons and text (the game's own), and equips the
// pick. Sizes and places are in the screen's heights (1 is the screen's height), from its middle,
// y down.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "spiral.hpp"

namespace spiral {

// One item the wheel offers.
struct Item {
    std::int32_t id = 0;      // the game's item
    std::int32_t icon = 0;    // the game's icon for it
    int hot = 0;              // its place on the game's own quick select, 1 to 8; 0 none
    bool equipped = false;    // in hand
};

// The pictures of PreyMenu.xml (textures/interface/PreyMenu/), each drawn centred on the wheel.
enum class Art {
    Sector,       // sector.dds: the slot's backing
    Border,       // border.dds: an item's frame
    BorderEq,     // borderEq.dds: the equipped item's
    BorderHot,    // borderHot.dds: an item on a hotkey (here: on the game's quick select)
    BorderEqHot,  // borderEqHot.dds: both
    Pointer,      // pointer.dds
    Info,         // info.dds: the line under the name
    Badge,        // hotkeys/<n>.dds: the hotkey's number (here: the quick select's place)
};

// A picture to draw: centred at (x, y) from the wheel's middle (0, 0 for the wheel's own), `w` by
// `h` screen heights, turned `angle_deg` counter-clockwise, its colour `grey` (0 to 1: the XML's
// brightness / 255; 0 is black) at `alpha` (0 to 1).
struct Sprite {
    Art art = Art::Sector;
    int badge = 0;  // Art::Badge: 1 to 8
    float x = 0, y = 0, w = 0, h = 0;
    float angle_deg = 0;
    float grey = 1;
    float alpha = 1;
};

// An item's icon: centred at (x, y), `size` screen heights square, at `alpha`.
struct Icon {
    int item = 0;  // the index into the wheel's items
    float x = 0, y = 0, size = 0;
    float alpha = 1;
};

// What a frame draws, in the order drawn (later over earlier).
struct Frame {
    std::vector<Sprite> sprites;
    std::vector<Icon> icons;
    // The pointed-at item (an index into the items), -1 "None", below -1 a blank; where the name
    // goes (above the info bar) and the line below it (the ammo), and the text's height.
    int selected = -1;
    float name_y = 0;
    float detail_y = 0;
    float text_height = 0;
};

// The look: the mod's settings (General: fScale and the spiral's), and PreyMenu.xml's sizes in its
// HUD units (960 the screen's height).
struct Look {
    Options spiral;
    float scale = 1.0f;  // fScale
};

class Wheel {
public:
    // UpdateMenu.gek: the items (UpdateMenu's order), the pointer on the equipped one, the
    // opening's zoom from nothing (_Prey+Anim 0.001 to 1 over 0.1 s).
    void open(std::vector<Item> items, const Look& look);
    void close() { m_open = false; }
    bool is_open() const { return m_open; }

    // UpdatePointer.gek, once a tick: the stick (screen space, y down), within `deadzone` none.
    // True when the pointed-at slot changed (the mod plays its scroll sound for an item or "None").
    bool steer(float x, float y, float deadzone);
    // One slot on (+1) or back (-1), as the stick would turn it; the pointer on that slot's middle.
    bool step(int slots);
    // The opening's zoom, `seconds` on.
    void advance(float seconds);

    // The pointed-at item (an index into items()), -1 "None", below -1 a blank.
    int selected() const;
    const std::vector<Item>& items() const { return m_items; }
    float input() const { return m_input; }
    // Where the pointer picture points, degrees counter-clockwise from 3 o'clock.
    float pointer_deg() const;
    float anim() const { return m_anim; }

    // What to draw now.
    Frame frame() const;

private:
    std::vector<Item> m_items;
    Look m_look;
    float m_input = 0;
    Stick m_stick;
    float m_anim = 1;
    bool m_open = false;
};

// PreyMenu.xml's sizes, in screen heights (its HUD is 960 units tall).
constexpr float kHud = 960.0f;
constexpr float kSize = 640.0f / kHud;            // _Prey+Size
constexpr float kRadius = 0.41666666666f;        // _Prey+Radius, of the size
constexpr float kRadiusHotkey = 0.47f;           // _Prey+RadiusHotkey, of the size
constexpr float kIcon = 0.13333333333f;          // the icon's width, of the sector's
constexpr float kBadge = 20.0f / kHud;           // _Prey+SizeHotkey
constexpr float kBadgeTurn = 11.5f;              // the hotkey's _Angle: _AngleDeg - 11.5
constexpr float kInfoWidth = 320.0f / kHud;      // InfoBar
constexpr float kInfoHeight = 5.0f / kHud;
constexpr float kTextHeight = 22.0f / kHud;      // the name's line (the mod's font 1 at zoom 100)
constexpr float kOpenSeconds = 0.1f;             // SetUIFloatGradual _Prey+Anim 0.001 1 0.1
// The XML's colours: <brightness> / 255 and <alpha> / 255.
constexpr float kSectorAlpha = 165.0f / 255.0f;
constexpr float kSectorHover = 100.0f / 255.0f;
constexpr float kBorderGrey = 175.0f / 255.0f;
constexpr float kPointerGrey = 200.0f / 255.0f;
constexpr float kInfoGrey = 200.0f / 255.0f;

}  // namespace spiral
