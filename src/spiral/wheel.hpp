// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral weapon wheel, game-agnostic: the items it shows, the pointer the stick turns
// (spiral.hpp: "Prey Menu"'s spiral, whose and from where), and what is drawn where, as the user's
// design lays it out (look.hpp: a continuous spiral of menu panels). A game's layer
// (src/gc/rac1_shim.*) fills the items, feeds the stick, draws the panels (the mod's overlay, by
// paint.hpp) and the icons and text (the game's own), and equips the pick. Places are in the
// screen's heights (1 is the screen's height), from its middle, y down.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "look.hpp"
#include "spiral.hpp"

namespace spiral {

// One item the wheel offers.
struct Item {
    std::int32_t id = 0;      // the game's item; 0 a hole (arrange.hpp)
    std::int32_t icon = 0;    // the game's icon for it
    int hot = 0;              // its place on the game's own quick select, 1 to 8; 0 none
    bool equipped = false;    // in hand
    // Its glyph (glyph.hpp) as the overlay's picture, and that picture's size; 0: none, the game's
    // own icon is drawn instead.
    int glyph = 0;
    int glyph_width = 0;
    int glyph_height = 0;
    // Its category (look.hpp: the game's menu groups): its slot's wash and its glyph's colour.
    look::Category category = look::Category::Weapon;
    // An icon pack's own colours for it, over its category's (icon_packs.hpp: wash, glyph_color,
    // selected_glyph_color).
    struct Colours {
        bool has_wash = false, has_glyph = false, has_selected = false;
        look::Rgb wash{}, glyph{}, selected{};
    } colours;
    // An icon pack's hint where the name may break (icon_packs.hpp: "wrap"), "" none.
    std::string wrap;
    // Drawn as its gold version (the layer's: owned, or a debug preview's).
    bool gold = false;
    // A debug preview's entry (an item not owned, or a gold version shown beside its weapon): drawn
    // and stepped to like any item, never picked (letting go on it picks nothing).
    bool preview = false;
};

// One slot as drawn (look.hpp): a menu panel bent into a sector of the spiral.
struct Slot {
    int index = 0;           // the item (an index into the wheel's items), or a blank's index
    bool empty = false;      // no item: a blank or a hole
    look::Category category = look::Category::Weapon;  // its item's
    // Its colours (its category's, or its pack's): the wash, the glyph, the glyph when selected.
    look::Rgb wash{}, glyph_colour{}, selected_colour{};
    bool selected = false;   // the pointer is on it
    float angle_deg = 0;     // its middle, degrees counter-clockwise from 3 o'clock
    float radius = 0;        // to the panel's middle, the design's pixels (before the push)
    float push = 0;          // pushed out by, the design's pixels
    float opacity = 1;
    // Its glyph (Item::glyph) and that picture's size; 0: none (the game's icon is drawn instead).
    int glyph = 0;
    int glyph_width = 0;
    int glyph_height = 0;
    int badge = 0;           // its quick select number, 1 to 8; 0 none
    // Its flash of static now (look.hpp: 0 none to 1), and the noise's offset in texels.
    float flash = 0;
    int flash_u = 0, flash_v = 0;
};

// The pictures of the game's own menu panel (look.hpp) as the layer made them from the game's
// textures, as overlay pictures (0: not there: the painter's own stand-ins, or none).
struct PanelArt {
    int scan = 0;      // the scanlines: a column covering the screen's height, a row a tile row
    int glint = 0;     // FX 0x19, 64 x 64
    int vignette = 0;  // HUD icon 0xE99E frame 7
    int noise = 0;     // FX 0x1A repeated to kNoiseTexels square
};

// An item's icon, the game's own, where its slot has no glyph: centred at (x, y), `size` screen
// heights square, at `alpha`.
struct Icon {
    int item = 0;  // the index into the wheel's items
    float x = 0, y = 0, size = 0;
    float alpha = 1;
};

// A quick select number on its badge: the badge's middle at (x, y), at `alpha`.
struct Badge {
    int number = 0;
    float x = 0, y = 0;
    float alpha = 1;
};

// What a frame draws. Places are screen heights from the screen's middle, y down; the slots'
// radii and the look's sizes are the design's pixels, times `unit`.
struct Frame {
    std::vector<Slot> slots;  // in the order drawn: the outer first, the selected last
    std::vector<Icon> icons;
    std::vector<Badge> badges;
    float unit = 0;           // screen heights a design pixel: the size setting and the opening's zoom
    float centre_y = 0;       // the wheel's middle below the screen's (negative: above)
    float lines = 448.0f;     // the game's lines a screen height (look.hpp: NTSC 416, PAL 448)
    PanelArt art;             // the panel's pictures (the layer's)
    // The middle disc's flash of static (as a slot's).
    float centre_flash = 0;
    int centre_flash_u = 0, centre_flash_v = 0;
    float pointer_deg = 0;    // where the pointer points
    // The pointed-at item (an index into the items), below 0 a blank; the middles of its name's
    // line and of the line under it (the ammo).
    int selected = -1;
    float name_y = 0;
    float detail_y = 0;
};

// The look: the mod's settings (the size, the spiral's start, direction and depth, and how it
// moves).
struct Look {
    Options spiral;
    float scale = 1.0f;  // 1: the design's size (its 720 pixels a screen's height)
    // How the spiral moves with the pointer ("Spiral motion"). Continuous: it follows the pointer
    // at all times, as Prey Menu's does, the pointed-at slot kContinuousIn slots into the window
    // the design shows whole (each slot's radius and opacity by its distance from the pointer).
    // Loop steps: the pointer's loop is the window, and the spiral eases to the next loop as the
    // pointer crosses into it.
    bool continuous = true;
    // Which way the spiral grows ("Spiral grows"). Outward (the default, as 1.0 to 1.1 had it): the
    // first loop, the game's quick select's home ring, innermost, the later loops further out.
    // Inward (the design's drawing): the first loop outermost, the later loops further in.
    bool outward = true;
};

// Continuous motion: where the pointer sits in the 12 slots shown whole (6: the 6 before it and
// the 5 after it whole, then the design's fades either side). Inward; outward (1.4.5) the ring
// holds on the first loop and the pointer sits here past it (wheel.cpp ring_place).
constexpr float kContinuousIn = 6.0f;

// Lines of `heights` (screen heights) stacked `gap` apart, the block centred on `centre`: each
// line's middle into `out`.
void stack_lines(float centre, const float* heights, int count, float gap, float* out);

// The radius step between slots for the depth setting (Prey Menu's fDepth): the design's 11 pixels
// at the default 0.75, otherwise Prey Menu's per-slot shrink (1 - shallowness) in proportion, held
// to 5 to 11 (the design's loops already fill the screen).
float radius_step(const Options& o);
// A slot's opacity `k` slots on from the first of the loop shown (look.hpp: 1 in the loop, the
// loops either side fading), in between whole slots linearly; `spiral` false: 1 for the one loop.
float slot_opacity(float k, bool spiral);
// Outward and continuous: the pointer's place in the ring shown for it `at` slots along the path.
float ring_place(float at, float total, int items);

class Wheel {
public:
    // UpdateMenu.gek: the items (UpdateMenu's order), the pointer on the equipped one, the
    // opening's zoom from nothing (_Prey+Anim 0.001 to 1 over 0.1 s).
    void open(std::vector<Item> items, const Look& look);
    void close() { m_open = false; }
    bool is_open() const { return m_open; }

    // UpdatePointer.gek, once a tick: the stick (screen space, y down), within `deadzone` none.
    // True when the pointed-at slot changed to an item (the mod's scroll sound).
    bool steer(float x, float y, float deadzone);
    // One item on (+1) or back (-1), past empty slots, as the stick would turn it; the pointer on
    // that slot's middle.
    bool step(int slots);
    // The opening's zoom and the spiral's move to the pointer's loop, `seconds` on.
    void advance(float seconds);
    // The spiral's motion, changed while open (the setting is read live).
    void set_continuous(bool continuous);
    // The flashes of static (look.hpp), a game frame on: each panel's chance, each flash's timer.
    // `seed` starts the wheel's own random numbers (the tests'); the layer gives a new one each
    // opening.
    void flash_frame();
    void seed(std::uint32_t seed) { m_random = seed != 0 ? seed : 1; }

    // The pointed-at item (an index into items()), below 0 a blank.
    int selected() const;
    // Whether slot `index` holds an item (not a blank or a hole: arrange.hpp).
    bool is_item(int index) const;
    // The spiral's own options for the look's: the first item on the start angle (Prey Menu's
    // "None" slot, one before it, is a blank here: the mod has no "None").
    Options spiral_options() const;
    const std::vector<Item>& items() const { return m_items; }
    const Look& look() const { return m_look; }
    float input() const { return m_input; }
    // Where the pointer points, degrees counter-clockwise from 3 o'clock.
    float pointer_deg() const;
    float anim() const { return m_anim; }
    // The first slot of the window the spiral shows whole, in slots from item 0 (0 up to the
    // spiral's slots): continuous, kContinuousIn before the pointer; in loop steps, the pointer's
    // loop's first, eased to as the pointer moves into another.
    float anchor() const;
    // The loop's first slot the anchor eases to: the pointer's loop's.
    float anchor_target() const;
    // Where the pointer is along the path, in slots from item 0 (0 up to the spiral's slots).
    float pointer_at() const;

    // What to draw now.
    Frame frame() const;

private:
    std::vector<Item> m_items;
    Look m_look;
    float m_input = 0;
    Stick m_stick;
    float m_anim = 1;
    float m_anchor = 0;  // unwrapped: the loops the spiral has turned since the opening
    float m_loop = 0;    // unwrapped: the loop's first slot it eases to
    bool m_open = false;
    struct Flash {
        int t = 0;
        int u = 0, v = 0;
    };
    std::vector<std::pair<int, Flash>> m_flashes;  // by slot index, while they last
    std::uint32_t m_random = 0x9E3779B9u;
    std::uint32_t random();
    bool step_one(int slots);
    float target_loop() const;
};

// PreyMenu.xml's opening: SetUIFloatGradual _Prey+Anim 0.001 1 0.1.
constexpr float kOpenSeconds = 0.1f;
// The spiral's move to another loop: eased, its time constant (the mod's).
constexpr float kLoopSeconds = 0.06f;

}  // namespace spiral
