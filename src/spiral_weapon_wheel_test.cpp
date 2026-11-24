// SPDX-License-Identifier: GPL-3.0-or-later
//
// The translation layer with the copied Going Commando code (src/gc/*.c) and the spiral wheel
// (src/gc/rac1_spiral.cpp, src/spiral/) on a fake Ratchet & Clank: a game memory filled with a
// quick select's words and tables and the items owned at the disc's addresses, and fake game
// functions that do what Ratchet & Clank's do with them (mode_freeze_init, queue_animation_update
// and update_hud_elements on one HUD record, the sound and music calls counted, the draws recorded,
// the port's slow motion recorded).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gc/rac1_shim.hpp"
#include "layout.hpp"

namespace {

namespace gcw = gc_weapon_wheel;
namespace gc = gc_weapon_wheel::gc;

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        ++g_checks;                                                                                 \
        const long long va = static_cast<long long>(a), vb = static_cast<long long>(b);             \
        if (va != vb) {                                                                             \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va, vb); \
        }                                                                                           \
    } while (0)

constexpr float kPi = 3.1415927f;
constexpr std::uint32_t kMemory = 0x200000;
// The quick select's HUD record (hud_chan_slots + 3 * 0x90 on PAL) and a place for the game's texts.
constexpr std::uint32_t kRecord = 0x00199C60u + 3 * 0x90;
constexpr std::uint32_t kTexts = 0x001E0000u;
constexpr std::uint32_t kStack = 0x001F0000u;
// The fake level's callbacks (as queue_animation_update stores them).
constexpr std::uint32_t kInitCode = 0x00238478u, kUpdateCode = 0x00238530u, kDrawCode = 0x00238C30u;

struct Sprite {
    int frame, x, y, w, h, alpha;
    std::uint32_t tint;
};
struct Text {
    int x, y;
    std::uint32_t colour;
    std::string text;
};

// The fake game.
struct Fake {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(kMemory, 0);
    gcw::Disc d{};
    gc::Memory m{};
    int handle_next = 100;
    int pause_group = 0, continue_group = 0, music_pauses = 0, music_unpauses = 0, flushes = 0, clicks = 0;
    int sound_updates = 0, play_ticks = 0, help_updates = 0, health_shown = 0, bolts_shown = 0;
    int removed = 0, freeze_inits = 0;
    std::uint32_t tint = UINT32_MAX;
    std::vector<Sprite> sprites;
    std::vector<Text> texts;
    std::vector<int> frames_drawn;
    int renders = 0;
    std::uint32_t next_text = kTexts;
    bool opener = false;       // Ratchet & Clank's own Triangle opener in the play tick
    bool rac1_timers = false;  // Ratchet & Clank's own double-tap timers in the play tick
    bool rac1_switch = false;  // Ratchet & Clank's hero_switch_item after them, the same tick
    bool match_code = true;    // whether the function table has the callbacks at their places
    bool block_selector = false;  // queue_animation_update refuses the quick select's record
    int level = 0;
    int swaps = 0;
    std::vector<float> stretches;  // time_stretch's calls
    std::vector<int> images, destroyed;  // the overlay pictures made and let go
    int image_next = 500;
    float stretch = 1.0f;
    std::int32_t last_mode = 0;
    std::vector<std::string> logs;
    // The logs with the request trace's "(tap timer,window)" groups left out.
    static std::string plain(const std::string& l) {
        std::string out;
        int depth = 0;
        for (char c : l) {
            if (c == '(' && !out.empty() && (out.back() == 'H' || out.back() == '-')) {
                ++depth;
                continue;
            }
            if (depth > 0) {
                depth -= c == ')' ? 1 : 0;
                continue;
            }
            out += c;
        }
        return out;
    }
    bool traced(const char* what) const {
        for (const std::string& l : logs) {
            if (plain(l).find(what) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    int count_traced(const char* what) const {
        int n = 0;
        for (const std::string& l : logs) {
            n += plain(l).find(what) != std::string::npos ? 1 : 0;
        }
        return n;
    }
    bool logged(const char* what) const {
        for (const std::string& l : logs) {
            if (l.find(what) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    int count_logs(const char* what) const {
        int n = 0;
        for (const std::string& l : logs) {
            n += l.find(what) != std::string::npos ? 1 : 0;
        }
        return n;
    }

    template <class T> T rd(std::uint32_t a) const { return m.read<T>(a); }
    template <class T> void wr(std::uint32_t a, T v) const { m.write<T>(a, v); }

    std::uint32_t text(const char* s) {
        const std::uint32_t at = next_text;
        std::memcpy(ram.data() + at, s, std::strlen(s) + 1);
        next_text += static_cast<std::uint32_t>(std::strlen(s) + 1);
        return at;
    }

    // Ratchet & Clank's queue_animation_update on the one record of slot 3 (others: a handle).
    int queue(int key, int, std::uint32_t init, std::uint32_t update, std::uint32_t draw, std::uint32_t, int) {
        if (key != gc::kSelectorKey) {
            return handle_next++;
        }
        if (block_selector) {
            return 0;
        }
        if (rd<std::uint32_t>(kRecord + 0x30) == init && rd<std::uint32_t>(kRecord + 0x34) == update &&
            rd<std::uint32_t>(kRecord + 0x38) == draw) {
            return rd<std::int32_t>(kRecord + 0x64);
        }
        wr<std::uint32_t>(kRecord + 0x30, init);
        wr<std::uint32_t>(kRecord + 0x34, update);
        wr<std::uint32_t>(kRecord + 0x38, draw);
        wr<std::int32_t>(kRecord + 0x64, handle_next);
        wr<std::int32_t>(kRecord + 0x68, 1);
        wr<std::int32_t>(kRecord + 0x7C, 0);
        wr<std::int32_t>(kRecord + 0x70, 0);
        return handle_next++;
    }
    // The health meter's record, as Ratchet & Clank's queue_animation_update keeps it: slot 1 (two
    // before the quick select's), its id 0x7535 at +0x20, its handle a running number at +0x64
    // (a new one when it is queued again after a close); hud_keep_visible keeps it for at least that
    // long, remove_hud_item (by handle) closes it.
    static constexpr std::uint32_t kHealth = kRecord - 2 * 0x90;
    int health_removed = 0;
    int health_handle_next = 900;
    bool health_on_triangle = false;  // Ratchet & Clank's own Triangle shows it (scale_ticks(0xB4))
    bool health_open() const { return rd<std::uint32_t>(kHealth + 0x34) != 0; }
    int show_health() {
        ++health_shown;
        if (!health_open()) {
            wr<std::int32_t>(kHealth + 0x64, health_handle_next++);
            wr<std::int32_t>(kHealth + 0x7C, 0);
        }
        wr<std::int32_t>(kHealth + 0x20, gcw::kHealthHandle);
        wr<std::uint32_t>(kHealth + 0x34, 0x00235000u);
        wr<std::int32_t>(kHealth + 0x6C, 0);
        return rd<std::int32_t>(kHealth + 0x64);
    }
    int remove(int handle) {
        if (handle == rd<std::int32_t>(kHealth + 0x64) && health_open()) {
            wr<std::uint32_t>(kHealth + 0x34, 0);
            wr<std::int32_t>(kHealth + 0x20, 0xFFFF);
            ++health_removed;
            return 1;
        }
        if (rd<std::int32_t>(kRecord + 0x64) == handle) {
            wr<std::uint32_t>(kRecord + 0x30, 0xFFFF);
            wr<std::uint32_t>(kRecord + 0x34, 0);
            wr<std::uint32_t>(kRecord + 0x38, 0);
            ++removed;
            return 1;
        }
        ++others_removed;
        return 0;
    }
    int others_removed = 0;  // a close of any other HUD element (the ammo, the bolts): never
    bool open() const { return rd<std::uint32_t>(kRecord + 0x34) != 0; }
    // update_hud_elements(1): the record's pending init, then its update.
    int update_hud(int) {
        if (rd<std::int32_t>(kRecord + 0x68) != 0) {
            wr<std::int32_t>(kRecord + 0x68, 0);
            gc::shim().selector_init(kRecord);
        }
        if (open()) {
            gc::shim().selector_update(kRecord);
        }
        return 0;
    }
    // mode_freeze_init as freeze.c:45 does it.
    void freeze_init(int kind, int, int) {
        ++freeze_inits;
        if (rd<std::int32_t>(d.game_mode) != 3) {
            ++pause_group;
            ++music_pauses;
        }
        wr<std::int32_t>(d.mode_freeze_info + 0x14, rd<std::int32_t>(d.game_mode));
        wr<std::int32_t>(d.game_mode, 4);
        wr<std::int32_t>(d.mode_freeze_info, kind);
    }
    // update_mode_freeze for a kind it has no case for: the sound, then the tail once the mode is left.
    void freeze_original() {
        ++sound_updates;
        const std::int32_t mode = rd<std::int32_t>(d.game_mode);
        if (mode != 3 && mode != 4) {
            ++continue_group;
            ++music_unpauses;
            ++flushes;
        }
    }
    // handle_pause_input: Ratchet & Clank's own Triangle opener (when asked), then the HUD's update.
    void play_original() {
        ++play_ticks;
        if (rac1_timers) {
            hero_timers();
        }
        if (rac1_switch) {
            hero_switch();
        }
        // Ratchet & Clank's Triangle shows the health meter for scale_ticks(0xB4) on each press
        // (hero_update_transitions), whether or not its quick select opens.
        if (health_on_triangle && (rd<std::uint32_t>(d.pad + gcw::kPadPressed) & 0x10) != 0) {
            show_health();
            wr<std::int32_t>(kHealth + 0x7C, std::max(rd<std::int32_t>(kHealth + 0x7C), d.ntsc ? 180 : 150));
        }
        if (opener && (rd<std::uint32_t>(d.pad + gcw::kPadPressed) & 0x10) != 0) {
            const gcw::LevelCallbacks cb = gcw::level_callbacks(d.ntsc, level);
            queue(3, 0, cb.selector_init, cb.selector_update, cb.selector_draw, 0, 0);
        }
        update_hud(1);
    }
    // Ratchet & Clank's hero_switch_item for slot 0 (hero_animation_slots.c): a pending item other than
    // the one taken becomes it; the quick swap item takes the last weapon, the last weapon the new
    // item (here the hand changes at once).
    void hero_switch() {
        const std::uint32_t h = d.hero;
        const std::int32_t u = rd<std::int32_t>(h + gcw::kHeroPendingItem);
        if (u == 0) {
            return;
        }
        if (u != rd<std::int32_t>(h + gcw::kHeroRequestedItem)) {
            if (u != 0x24 && rd<std::int32_t>(h + gcw::kHeroItem) != 8) {
                wr<std::int32_t>(d.quick_swap_item, rd<std::int32_t>(d.last_weapon));
            }
            wr<std::int32_t>(h + gcw::kHeroRequestedItem, u);
            wr<std::int32_t>(d.last_weapon, u);
            wr<std::int32_t>(h + gcw::kHeroItem, u);
        }
        wr<std::int32_t>(h + gcw::kHeroPendingItem, 0);
    }
    // Ratchet & Clank's own double tap (hero_update_transitions, hero_item_input_and_hud.c:142-170).
    void hero_timers() {
        const std::uint32_t h = d.hero;
        const int twenty = d.ntsc ? 20 : 17;
        for (std::uint32_t t : {h + gcw::kHeroTapTimer, h + gcw::kHeroTapWindow}) {
            const std::int16_t v = rd<std::int16_t>(t);
            if (v != 0) {
                wr<std::int16_t>(t, static_cast<std::int16_t>(v - 1));
            }
        }
        if (rd<std::uint32_t>(d.pad + gcw::kPadPressed) & 0x10) {
            wr<std::int16_t>(h + gcw::kHeroTapTimer, static_cast<std::int16_t>(twenty));
        }
        if ((rd<std::int16_t>(h + gcw::kHeroTapTimer) != 0 || rd<std::int16_t>(h + gcw::kHeroTapWindow) != 0) &&
            (rd<std::uint32_t>(d.pad + gcw::kPadRawReleased) & 0x10)) {
            if (rd<std::int16_t>(h + gcw::kHeroTapWindow) == 0) {
                wr<std::int16_t>(h + gcw::kHeroTapWindow, static_cast<std::int16_t>(twenty));
            } else {
                const std::int32_t swap = rd<std::int32_t>(d.quick_swap_item);
                const std::int32_t item = rd<std::int32_t>(h + gcw::kHeroItem);
                const std::int32_t next = item == 8 ? rd<std::int32_t>(d.last_weapon) : swap;
                if (next != 0 && item != swap) {
                    wr<std::int32_t>(h + gcw::kHeroPendingItem, next);
                    wr<std::int16_t>(h + gcw::kHeroTapTimer, 0);
                    wr<std::int16_t>(h + gcw::kHeroTapWindow, 0);
                    ++swaps;
                }
            }
        }
    }

    // The pad, as UpdatePad leaves it after a poll: held and the edges, and the history.
    std::uint32_t last_held = 0;
    void poll(std::uint32_t held, float x = 0.0f, float y = 0.0f) {
        // The level's main loop: the ticks since the game mode changed start over at a change.
        const std::int32_t mode = rd<std::int32_t>(d.game_mode);
        if (mode != last_mode) {
            wr<std::int32_t>(d.cinematic_frames, 0);
            last_mode = mode;
        }
        const std::uint32_t pressed = held & ~last_held;
        const std::uint32_t released = last_held & ~held;
        last_held = held;
        const std::uint32_t p = d.pad;
        wr<std::uint32_t>(p + gcw::kPadHeld, held);
        wr<std::uint32_t>(p + gcw::kPadPressed, pressed);
        wr<std::uint32_t>(p + 0x1A8, released);
        wr<std::uint32_t>(p + gcw::kPadRawReleased, released);
        wr<std::uint32_t>(p + gcw::kPadHeldUnmasked, held);
        wr<std::uint32_t>(p + gcw::kPadPressedUnmasked, pressed);
        wr<float>(p + gcw::kPadStickX, x);
        wr<float>(p + gcw::kPadStickY, y);
        wr<std::int32_t>(p + gcw::kPadStickMoved, (x != 0.0f || y != 0.0f) ? 1 : 0);
        std::int16_t head = rd<std::int16_t>(p + gcw::kPadHistIdx);
        wr<std::uint32_t>(p + gcw::kPadHistory + 4u * static_cast<std::uint32_t>(head), pressed);
        wr<std::int16_t>(p + gcw::kPadHistIdx, static_cast<std::int16_t>((head + 1) % 30));
        const std::int32_t count = rd<std::int32_t>(p + gcw::kPadHistCount);
        wr<std::int32_t>(p + gcw::kPadHistCount, count < 30 ? count + 1 : 30);
        wr<std::int32_t>(d.cinematic_frames, rd<std::int32_t>(d.cinematic_frames) + 1);
        wr<std::int32_t>(d.level_frame_counter, rd<std::int32_t>(d.level_frame_counter) + 1);
        wr<std::int32_t>(d.rendered_frame_count, rd<std::int32_t>(d.rendered_frame_count) + 1);
    }
};

Fake* g_fake = nullptr;

// The fake HUD's icon frames: small numbers (the icon's low byte, 64 more a variant).
int fake_frame(int icon, int variant) { return (icon & 0xFF) + 1 + variant * 64; }

// The fake HUD set (hud_state, layout.hpp): every frame's image is the same synthetic icon (a dark
// ring on a blue ground, 32 x 32 indices into a palette of 256 shades from the ground to the dark,
// stored in CSM1 order), but item 0x0C's, whose bank is not linked (bit 31 set): its glyph fails and
// the game's own icon stays. Nothing of the game's: a test picture.
constexpr std::uint32_t kHudTables = 0x001D8000u;  // frames, images, palettes, then the pixels
void fake_hud(Fake& f) {
    const std::uint32_t frames = kHudTables, images = kHudTables + 0x800, palettes = kHudTables + 0x1800;
    const std::uint32_t pixels = kHudTables + 0x2000, unlinked_pixels = kHudTables + 0x2400, colours = kHudTables + 0x2800;
    f.wr<std::uint32_t>(f.d.hud_state + gcw::kHudFrames, frames);
    f.wr<std::uint32_t>(f.d.hud_state + gcw::kHudImages, images);
    f.wr<std::uint32_t>(f.d.hud_state + gcw::kHudPalettes, palettes);
    for (int frame = 0; frame < 0x1FF; ++frame) {
        f.wr<std::int16_t>(frames + 4u * static_cast<std::uint32_t>(frame), 0);
        f.wr<std::int16_t>(frames + 4u * static_cast<std::uint32_t>(frame) + 2, static_cast<std::int16_t>(frame));
        const bool unlinked = frame == fake_frame(0xE900 + 0x0C, 0);
        const std::uint32_t page = images + 8u * static_cast<std::uint32_t>(frame);
        f.wr<std::uint32_t>(page, unlinked ? (unlinked_pixels | 0x80000000u) : pixels);
        f.wr<std::uint8_t>(page + 6, 5);
        f.wr<std::uint8_t>(page + 7, 5);
    }
    f.wr<std::uint32_t>(palettes, colours);
    auto csm1 = [](int i) { return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); };
    for (int i = 0; i < 256; ++i) {
        const float t = static_cast<float>(i) / 255.0f;  // 0 the ground, 1 the glyph
        const std::uint32_t c = colours + 4u * static_cast<std::uint32_t>(csm1(i));
        f.wr<std::uint8_t>(c, static_cast<std::uint8_t>(110 - 95 * t));
        f.wr<std::uint8_t>(c + 1, static_cast<std::uint8_t>(115 - 100 * t));
        f.wr<std::uint8_t>(c + 2, static_cast<std::uint8_t>(221 - 206 * t));
        f.wr<std::uint8_t>(c + 3, 0x80);
    }
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float r = std::sqrt((x - 15.5f) * (x - 15.5f) + (y - 15.5f) * (y - 15.5f));
            const float ink = std::fmin(1.0f, std::fmax(0.0f, 1.0f - std::fabs(r - 9.5f) + 1.0f));
            f.wr<std::uint8_t>(pixels + static_cast<std::uint32_t>(y * 32 + x), static_cast<std::uint8_t>(ink * 255.0f));
        }
    }
}

// The slot assignment: eight items, slot 3 empty; item 0x18 in slot 7.
constexpr std::int32_t kItems[8] = {0x10, 0x11, 0x12, 0, 0x13, 0x14, 0x15, 0x18};
// The Quick Select page's grids as the NTSC-U executable has them (pause_item_candidate_entries at
// 0x001CF120, pause_hand_gadget_cells at 0x001CEC20): the weapons' 15 cells, the hand gadgets' 6.
constexpr std::int32_t kWeaponCellItems[15] = {0x0A, 0x10, 0x0F, 0x14, 0x11, 0x0E, 0x09, 0x0B,
                                               0x12, 0x0D, 0x19, 0x18, 0x13, 0x15, 0x17};
constexpr std::int32_t kGadgetCellItems[6] = {0x1A, 0x16, 0x0C, 0x20, 0x1B, 0x1F};
// The items owned: 0x09 to 0x15 and 0x18 (14 of the grids: a spiral of two loops), and of the Items
// page, never offered: the wrench (8), the Heli-Pack (2), the Sonic Summoner (5), the O2 Mask (6),
// the Magneboots (0x1C), the Hoverboard (0x1E), the Persuader (0x23).
constexpr std::int32_t kOwned[] = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
                                   0x12, 0x13, 0x14, 0x15, 0x18, 0x02, 0x05, 0x06, 0x1C, 0x1E, 0x23};
constexpr std::int32_t kItemsPage[] = {0x08, 0x02, 0x05, 0x06, 0x1C, 0x1E, 0x23};
constexpr int kWheelItems = 14;
// The wheel's order (arrange.hpp): the quick select (kItems, slot 3 empty) at its ring's directions
// on the first loop, item 0 on the start angle (0x10 up at 0, 0x11 at 11, 0x12 at 9, 0x13 at 6,
// 0x14 at 5, 0x15 at 3, 0x18 at 1), then the page's others (weapons grid, then gadgets) in the free
// slots 2, 4, 7, 8, 10, then 12 and 13.
constexpr std::int32_t kWheelOrder[kWheelItems] = {0x10, 0x18, 0x0A, 0x15, 0x0F, 0x14, 0x13,
                                                   0x0E, 0x09, 0x12, 0x0B, 0x11, 0x0D, 0x0C};
int wheel_index(std::int32_t id) {
    for (int i = 0; i < kWheelItems; ++i) {
        if (kWheelOrder[i] == id) {
            return i;
        }
    }
    return -100;
}

// exe_shift: the program loaded keeps the executable's game mode word that much further on (its
// own word left at the executable's address holds a 5).
void set_up(Fake& f, bool ntsc, bool pause, std::uint32_t exe_shift = 0) {
    g_fake = &f;
    gc::shim() = gc::Shim{};
    f.d = gcw::disc_for(ntsc);
    const std::uint32_t exe_mode = f.d.game_mode;
    f.d.game_mode += exe_shift;
    f.m = gc::Memory{f.ram.data()};
    const gcw::Disc& d = f.d;
    f.wr<float>(d.frame_scale, ntsc ? 1.0f : 50.0f / 60.0f);
    for (int i = 0; i < 8; ++i) {
        f.wr<std::int32_t>(d.slot_items + 4u * static_cast<std::uint32_t>(i), kItems[i]);
    }
    for (std::uint32_t id = 1; id < 0x20; ++id) {
        f.wr<std::uint16_t>(d.item_defs + id * gcw::kItemDefStride + gcw::kItemDefIcon, static_cast<std::uint16_t>(0xE900 + id));
        f.wr<std::int16_t>(d.item_defs + id * gcw::kItemDefStride + gcw::kItemDefName, static_cast<std::int16_t>(0x100 + id));
        f.wr<std::int16_t>(d.item_defs + id * gcw::kItemDefStride + gcw::kItemDefGoldName, static_cast<std::int16_t>(0x200 + id));
        f.wr<std::uint16_t>(d.item_price + id * gcw::kPriceStride + gcw::kPriceUsesAmmo, id == 0x12 ? 0 : 1);
        f.wr<std::uint16_t>(d.item_price + id * gcw::kPriceStride + gcw::kPriceMaxAmmo, 40);
        f.wr<std::int32_t>(d.ammo + 4 * id, 12);
    }
    // Page 0's entries: ring neighbours (left -1, right +1), up and down across the ring.
    for (std::uint32_t i = 0; i < 8; ++i) {
        const std::uint32_t e = d.page0_entries + i * 28;
        f.wr<std::int32_t>(e + 0x08, static_cast<std::int32_t>((i + 7) % 8));
        f.wr<std::int32_t>(e + 0x0C, static_cast<std::int32_t>((i + 1) % 8));
        f.wr<std::int32_t>(e + 0x10, 0);
        f.wr<std::int32_t>(e + 0x14, 4);
    }
    // The page table quick_select_page + 12 holds page 0's entries.
    f.wr<std::uint32_t>(d.page + 12, d.page0_entries);
    // Defaults with nothing selected: left 6, right 2, up 0, down 4.
    f.wr<std::int32_t>(d.default_left, 6);
    f.wr<std::int32_t>(d.default_left + 4, 2);
    f.wr<std::int32_t>(d.default_left + 8, 0);
    f.wr<std::int32_t>(d.default_left + 12, 4);
    for (std::int32_t id : kOwned) {
        f.wr<std::uint8_t>(d.item_available + static_cast<std::uint32_t>(id), 1);
    }
    // The grids' cells: the icon (the item's), its kind 0, the item.
    for (int i = 0; i < 15; ++i) {
        const std::uint32_t cell = d.weapon_cells + static_cast<std::uint32_t>(i) * gcw::kCellStride;
        f.wr<std::uint16_t>(cell + gcw::kCellIcon, static_cast<std::uint16_t>(0xE900 + kWeaponCellItems[i]));
        f.wr<std::int16_t>(cell + gcw::kCellItem, static_cast<std::int16_t>(kWeaponCellItems[i]));
    }
    for (int i = 0; i < 6; ++i) {
        const std::uint32_t cell = d.gadget_cells + static_cast<std::uint32_t>(i) * gcw::kCellStride;
        f.wr<std::uint16_t>(cell + gcw::kCellIcon, static_cast<std::uint16_t>(0xE900 + kGadgetCellItems[i]));
        f.wr<std::int16_t>(cell + gcw::kCellItem, static_cast<std::int16_t>(kGadgetCellItems[i]));
    }
    std::memcpy(f.ram.data() + d.ammo_format, "%d/%d", 6);
    fake_hud(f);
    f.wr<std::int32_t>(d.width, 200);
    f.wr<std::int32_t>(d.height, 180);
    f.wr<std::int32_t>(d.center_x, 100);
    f.wr<std::int32_t>(d.center_y, 90);
    f.wr<float>(d.ring_radius, 70.0f);
    f.wr<float>(d.ring_aspect, 1.0f);
    f.wr<float>(d.unselected_alpha, 0.5f);
    f.wr<std::int32_t>(d.cinematic_frames, 100);
    f.wr<std::int32_t>(d.quick_swap_item, 0x11);
    f.wr<std::int32_t>(d.last_weapon, 0x13);
    f.wr<std::int32_t>(d.hero + gcw::kHeroItem, 0x10);
    f.wr<std::int32_t>(d.level_index, 3);
    f.wr<std::int32_t>(d.event_frames, 6000);
    // The record's place and size on the screen (hud_screen_position's input).
    f.wr<std::int32_t>(kRecord + 0x50, 256);
    f.wr<std::int32_t>(kRecord + 0x54, 224);
    f.wr<std::int32_t>(kRecord + 0x60, 0);

    gc::Shim& s = gc::shim();
    gc::Game g;
    g.memory = f.m;
    g.disc = d;
    g.relocate = [](std::uint32_t a) { return a; };
    static std::uint32_t s_exe_mode, s_shift;
    s_exe_mode = exe_mode;
    s_shift = exe_shift;
    if (exe_shift != 0) {
        f.wr<std::int32_t>(exe_mode, 5);
    }
    g.relocate_exe = [](std::uint32_t a) { return a == s_exe_mode ? a + s_shift : a; };
    g.code_matches = [](std::uint32_t address, const char* name) {
        const gcw::LevelCallbacks cb = gcw::level_callbacks(g_fake->d.ntsc, g_fake->level);
        const char* const* names = g_fake->d.ntsc ? gcw::kCallbackNamesNtsc : gcw::kCallbackNamesPal;
        const std::uint32_t at[6] = {cb.selector_init, cb.selector_update, cb.selector_draw,
                                     cb.bolt_init, cb.bolt_update, cb.bolt_draw};
        bool known = false;
        for (int i = 0; i < 6; ++i) {
            known = known || (at[i] == address && std::strcmp(names[i], name) == 0);
        }
        return g_fake->match_code && known;
    };
    g.mode_freeze_init = [](int k, int a, int b) { g_fake->freeze_init(k, a, b); };
    g.update_hud_elements = [](int w) { return g_fake->update_hud(w); };
    g.help_update = [] { ++g_fake->help_updates; };
    g.music_unpause = [] { ++g_fake->music_unpauses; };
    g.music_pause = [](int) { ++g_fake->music_pauses; };
    g.snd_pause_group = [](int group) {
        CHECK_EQ(group, 0x1D);
        ++g_fake->pause_group;
    };
    g.snd_continue_group = [](int group) {
        CHECK_EQ(group, 0x1D);
        ++g_fake->continue_group;
    };
    g.snd_flush = [] { ++g_fake->flushes; };
    g.draw_hud_sprite = [](int frame, int x, int y, int w, int h, int alpha) {
        g_fake->sprites.push_back({frame, x, y, w, h, alpha, g_fake->tint});
    };
    g.get_icon_frame = [](int icon, int variant) { return fake_frame(icon, variant); };
    g.hud_frame = [](std::uint32_t rec, int frame, int, int, std::uint32_t, int) {
        CHECK_EQ(rec, kRecord);
        g_fake->frames_drawn.push_back(frame);
    };
    g.font_print_center = [](int x, int y, std::uint32_t colour, const char* text, int length) {
        g_fake->texts.push_back({x, y, colour, length < 0 ? std::string(text) : std::string(text, static_cast<std::size_t>(length))});
    };
    g.msg_string = [](int id) -> std::uint32_t {
        char name[64];
        if (id == 0x110) {
            std::snprintf(name, sizeof name, "Bomb Glove");
        } else if (id == 0x111) {
            std::snprintf(name, sizeof name, "Pyro-citor");
        } else if (id == 0x112) {
            std::snprintf(name, sizeof name, "Suck");
        } else {
            std::snprintf(name, sizeof name, "Item%x", id);
        }
        return g_fake->text(name);
    };
    g.show_health_hud = [] { return g_fake->show_health(); };
    g.hud_keep_visible = [](int handle, int ticks) {
        if (handle == g_fake->rd<std::int32_t>(Fake::kHealth + 0x64) && g_fake->health_open()) {
            const std::uint32_t at = Fake::kHealth + 0x7C;
            g_fake->wr<std::int32_t>(at, std::max(g_fake->rd<std::int32_t>(at), ticks));
        }
        return 0;
    };
    g.create_image = [](int width, int height, const std::uint8_t* rgba) {
        // A glyph: a picture of its own, with something drawn in it.
        int ink = 0;
        for (int i = 0; i < width * height; ++i) {
            ink += rgba[i * 4 + 3] > 128 ? 1 : 0;
        }
        CHECK(width > 0 && height > 0 && ink > 0);
        g_fake->images.push_back(++g_fake->image_next);
        return g_fake->image_next;
    };
    g.destroy_image = [](int image) { g_fake->destroyed.push_back(image); };
    g.queue_animation_update = [](int key, int a, std::uint32_t i, std::uint32_t u, std::uint32_t dr, std::uint32_t data, int b) {
        if (key == 2) {
            ++g_fake->bolts_shown;
        }
        return g_fake->queue(key, a, i, u, dr, data, b);
    };
    g.remove_hud_item = [](int h) { return g_fake->remove(h); };
    g.append_gif_transfer_packet = [] {};
    g.draw_debug_profiler = [] {
        // The world's render: the HUD's draw callbacks.
        ++g_fake->renders;
        if (g_fake->open()) {
            gc::shim().selector_draw(kRecord);
        }
    };
    g.play_click = [] { ++g_fake->clicks; };
    g.time_stretch = [](float stretch) {
        g_fake->stretches.push_back(stretch);
        g_fake->stretch = stretch;
    };
    g.hud_screen_position = [](std::uint32_t rec, int& x, int& y) {
        // Ratchet & Clank's (weapon_hud_and_counters.c): centred by the record's size with flags 0.
        x -= g_fake->rd<std::int32_t>(rec + 0x58) >> 1;
        y -= g_fake->rd<std::int32_t>(rec + 0x5C) >> 1;
    };
    g.log = [](const char* what) { g_fake->logs.push_back(std::string("WARN ") + what); };
    g.info = [](const char* what) { g_fake->logs.push_back(what); };
    s.game = g;
    s.set_options(pause, true);
    // The spiral's defaults, but no slow-down (the tests of the pause count its ticks; the
    // slow-down's own test turns it on).
    gc::WheelOptions w;
    w.ramp_frames = gc::kRampOff;
    s.set_wheel_options(w);
    s.connect(-2);
    s.connect(f.level);
}

// A play tick: the pad polled, then the level's tick through the hook; the world's render after.
void play(Fake& f, std::uint32_t held, float x = 0.0f, float y = 0.0f) {
    f.poll(held, x, y);
    gc::shim().update_play([&] { f.play_original(); });
}

// Triangle pressed, then held, until the freeze is entered (at most 20 ticks): the ticks it took.
int hold_until_freeze(Fake& f) {
    int n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 0 && n < 20) {
        play(f, 0x10);
        ++n;
    }
    return n;
}

// Ratchet & Clank's letterbox words (letterbox_bar_height, letterbox_enabled): never written.
constexpr std::int32_t kLetterboxSentinel = 0x5A5A5A5A;
void mark_letterbox(Fake& f) {
    const std::uint32_t at = f.d.ntsc ? 0x0015F444u : 0x0015F544u;
    f.wr<std::int32_t>(at, kLetterboxSentinel);
    f.wr<std::int32_t>(at + 4, kLetterboxSentinel);
}
bool letterbox_untouched(const Fake& f) {
    const std::uint32_t at = f.d.ntsc ? 0x0015F444u : 0x0015F544u;
    return f.rd<std::int32_t>(at) == kLetterboxSentinel && f.rd<std::int32_t>(at + 4) == kLetterboxSentinel;
}

// A freeze tick: the pad polled, the freeze mode's update and picture through the hooks.
void freeze(Fake& f, std::uint32_t held, float x = 0.0f, float y = 0.0f) {
    f.poll(held, x, y);
    gc::shim().freeze_update([&] { f.freeze_original(); });
    gc::shim().freeze_draw([] {});
}

std::int32_t selected(const Fake& f) { return f.rd<std::int32_t>(kRecord + gcw::kRecordSelected); }
std::uint8_t fade(const Fake& f, int i) { return f.rd<std::uint8_t>(kRecord + gcw::kRecordFades + static_cast<std::uint32_t>(i)); }
std::int32_t modal(std::uint32_t offset) {
    std::int32_t v;
    std::memcpy(&v, gc_shim_modal_state + offset, sizeof v);
    return v;
}

// The stick at `angle` (the angle fast_arc_tan(x, y) gives: atan2(y, x) here), full tilt.
void stick_at(float angle, float& x, float& y) {
    x = std::cos(angle);
    y = std::sin(angle);
}

// The stick pushed toward `degrees` on the screen (counter-clockwise from 3 o'clock). Ratchet &
// Clank's stick reads y downward: pushed up, y is -1 (as Going Commando's quick select takes it).
void stick_toward(float degrees, float& x, float& y) {
    x = std::cos(degrees * kPi / 180.0f);
    y = -std::sin(degrees * kPi / 180.0f);
}

// The stick pointing at wheel item `index`'s slot (the default spiral: counter-clockwise from 12
// o'clock, item 0 there, item i at i * 30 + 90 degrees).
void stick_at_item(int index, float& x, float& y) {
    stick_toward(static_cast<float>(index * 30 + 90), x, y);
}

// --- Tests ---

void test_frames_and_ticks() {
    Fake f;
    set_up(f, false, true);
    gc::Shim& s = gc::shim();
    CHECK_EQ(s.frames(180), 150);
    CHECK_EQ(s.frames(30), 25);
    CHECK_EQ(s.frames(20), 17);
    CHECK_EQ(s.frames(10), 8);
    CHECK_EQ(s.frames(6), 5);
    CHECK_EQ(s.ticks(180), 150);
    CHECK_EQ(s.ticks(360), 300);
    Fake n;
    set_up(n, true, true);
    CHECK_EQ(s.frames(180), 180);
    CHECK_EQ(s.frames(20), 20);
    CHECK_EQ(s.ticks(360), 360);
    // frame_scale out of range (NTSC's address unchecked): the tick rate's factor.
    n.wr<float>(n.d.frame_scale, 0.0f);
    CHECK_EQ(s.ticks(360), 360);
}

void test_pad_history_and_press_window() {
    Fake f;
    set_up(f, true, true);
    f.poll(0x10);
    f.poll(0x10);
    f.poll(0);
    f.poll(0x40);
    std::int32_t age = -1;
    // Triangle was pressed 3 polls back (the newest first): within 4 polls, not within 3.
    CHECK_EQ(PadPressedWithin(0x10, 3, &age), 0);
    CHECK_EQ(PadPressedWithin(0x10, 4, &age), 0x10);
    CHECK_EQ(age, 3);
    CHECK_EQ(PadPressedWithin(0x40, 1, &age), 0x40);
    CHECK_EQ(age, 0);
    // The window: +1 a tick up to 10, 0 while input is blocked.
    gc_shim_press_window = 0;
    for (int i = 0; i < 15; ++i) {
        hero_update_hit_streak_effect();
    }
    CHECK_EQ(gc_shim_press_window, 10);
    gc_shim_input_block = 1;
    hero_update_hit_streak_effect();
    CHECK_EQ(gc_shim_press_window, 0);
    gc_shim_input_block = 0;
}

// The HUD quick select ("Pause while open" off): Ratchet & Clank's own Triangle opens it, Going
// Commando's code runs it, no freeze.
void open_hud(Fake& f) {
    f.opener = true;
    play(f, 0x10);
    CHECK(f.open());
    // Going Commando's quick select takes the stick once its HUD-open request is done: Triangle held
    // its few frames.
    for (int i = 0; i < 6; ++i) {
        play(f, 0x10);
    }
    CHECK_EQ(gc_shim_hud_flag_b, 3);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
}

void test_items() {
    Fake f;
    set_up(f, true, false);
    const std::vector<spiral::Item> items = gc::shim().collect_items();
    CHECK_EQ(items.size(), static_cast<std::size_t>(kWheelItems));
    // The Quick Select page's owned items, arranged around the quick select.
    for (std::size_t i = 0; i < items.size() && i < static_cast<std::size_t>(kWheelItems); ++i) {
        CHECK_EQ(items[i].id, kWheelOrder[i]);
        CHECK_EQ(items[i].icon, 0xE900 + kWheelOrder[i]);
    }
    // Nothing of the Items page, though owned: the wrench, packs, helmets, boots, the Hoverboard,
    // the Persuader.
    for (const spiral::Item& it : items) {
        for (std::int32_t not_offered : kItemsPage) {
            CHECK(it.id != not_offered);
        }
    }
    // A cell whose item is not owned is left out (0x16, 0x19, 0x17, 0x1A ... here).
    for (const spiral::Item& it : items) {
        CHECK(it.id != 0x16 && it.id != 0x19 && it.id != 0x17 && it.id != 0x1A);
    }
    // A cell of another kind (a special item's) is not an item to equip.
    f.wr<std::int16_t>(f.d.weapon_cells + gcw::kCellKind, 1);
    CHECK_EQ(gc::shim().collect_items().size(), static_cast<std::size_t>(kWheelItems - 1));
    f.wr<std::int16_t>(f.d.weapon_cells + gcw::kCellKind, 0);
    // Their places on the game's quick select (kItems: 0x10 in slot 1 .. 0x18 in slot 8), the one
    // in hand.
    CHECK_EQ(items[static_cast<std::size_t>(wheel_index(0x10))].hot, 1);
    CHECK_EQ(items[static_cast<std::size_t>(wheel_index(0x13))].hot, 5);
    CHECK_EQ(items[static_cast<std::size_t>(wheel_index(0x18))].hot, 8);
    CHECK_EQ(items[static_cast<std::size_t>(wheel_index(0x0A))].hot, 0);
    CHECK(items[static_cast<std::size_t>(wheel_index(0x10))].equipped && !items[1].equipped);
    // The opening's line lists them in order.
    open_hud(f);
    CHECK(f.logged("the wheel opens with 14 items: 0x10 0x18 0xA 0x15 0xF 0x14 0x13 0xE 0x9 0x12 0xB 0x11 0xD 0xC"));
    // Every item has its glyph (a picture made from its icon; the cell its ground's tint) but 0x0C,
    // whose icon's bank is not linked: the game's own icon for it.
    for (const spiral::Item& it : gc::shim().wheel().items()) {
        CHECK((it.glyph != 0) == (it.id != 0x0C));
        CHECK(it.tinted == (it.id != 0x0C));
        if (it.tinted) {
            CHECK(it.tint[2] > it.tint[0]);  // the fake's blue ground
        }
    }
    CHECK(f.logged("(frame 13, 32x32): not a glyph") == false);
    // Each quick select item is drawn where the game's ring has it (slot i at 90 - 45 * i).
    for (int slot = 0; slot < 8; ++slot) {
        if (kItems[slot] == 0) {
            continue;
        }
        const int index = wheel_index(kItems[slot]);
        const float drawn = static_cast<float>(index * 30 + 90);
        CHECK(std::fabs(std::remainder(drawn - (90.0f - 45.0f * static_cast<float>(slot)), 360.0f)) <= 15.01f);
    }
}

void test_spiral_selection() {
    Fake f;
    set_up(f, true, false);
    open_hud(f);
    // menu_init_panel_layout: 210 x 200, the arming countdown; the wheel open on the item in hand.
    CHECK_EQ(f.rd<std::int32_t>(kRecord + 0x58), 210);
    CHECK(gc::shim().wheel().is_open());
    CHECK_EQ(selected(f), wheel_index(0x10));
    // The stick idle arms it.
    play(f, 0x10);
    CHECK_EQ(f.rd<std::int32_t>(kRecord + 0x78), 0x10000FF);
    float x, y;
    // The item in hand, 0x10, is on 12 o'clock (index 0). The first push points on the first loop,
    // where the game's quick select is: 9 o'clock is 3. Then the stick turns: a quarter turn
    // counter-clockwise at a time to 6 and 9, then round to 11, on to 12 and 13 (the next loop) and
    // into a blank; back clockwise to 10.
    const int e = wheel_index(0x10);
    CHECK_EQ(e, 0);
    for (int target : {3, 6, 9, 11, 12, 13}) {
        stick_at_item(target, x, y);
        play(f, 0x10, x, y);
        CHECK_EQ(selected(f), target);
    }
    stick_at_item(14, x, y);  // the slot after the last item: a blank
    play(f, 0x10, x, y);
    CHECK_EQ(selected(f), -10);
    for (int target : {13, 10}) {
        stick_at_item(target, x, y);
        play(f, 0x10, x, y);
        CHECK_EQ(selected(f), target);
    }
    // The stick let go: the pointer stays.
    play(f, 0x10);
    CHECK_EQ(selected(f), 10);
    // The d-pad: right one on, left one back.
    play(f, 0x10 | gcw::kDpadRight);
    CHECK_EQ(selected(f), 11);
    play(f, 0x10);
    play(f, 0x10 | gcw::kDpadLeft);
    CHECK_EQ(selected(f), 10);
    // A click on each change (the setting on).
    CHECK(f.clicks > 0);
    // The overlay's picture while it shows.
    CHECK(gc::shim().picture().shown && gc::shim().picture().frame.selected == 10);
}

// The stick pushed toward a slot on the screen points at that slot, as drawn: the lit sector's
// angle is the stick's, for up, down, left, right and the diagonals, counter-clockwise and
// clockwise, at the default start and others. Each direction is reached through its neighbours (the
// spiral counts the turn, so a jump of half a circle could go either way).
void test_stick_points_where_it_is_pushed() {
    for (bool clockwise : {false, true}) {
        for (float start : {90.0f, 0.0f, -45.0f, 200.0f}) {
            Fake f;
            set_up(f, true, false);
            gc::WheelOptions w = gc::shim().wheel_options();
            w.look.spiral.clockwise = clockwise;
            w.look.spiral.start_deg = start;
            gc::shim().set_wheel_options(w);
            open_hud(f);
            play(f, 0x10);
            float x, y;
            for (float target : {90.0f, 45.0f, 0.0f, -45.0f, -90.0f, -135.0f, 180.0f, 135.0f, 90.0f, 135.0f, 180.0f,
                                 -135.0f, -90.0f, -45.0f, 0.0f, 45.0f, 90.0f}) {
                stick_toward(target, x, y);
                play(f, 0x10, x, y);
                // The pointed-at slot (an item, "None" or a blank) is drawn where the stick points: its
                // sector's angle (_AngleDeg) as the frame draws it; for an item or "None", the lit one.
                const int slot = selected(f);
                const int sign = clockwise ? -1 : 1;
                const float drawn = static_cast<float>(slot * sign * 30) + start;
                for (const spiral::Sprite& sp : gc::shim().picture().frame.sprites) {
                    if (sp.art == spiral::Art::Sector && sp.grey > 0.0f) {
                        CHECK(slot >= 0 && std::fabs(std::remainder(sp.angle_deg - drawn, 360.0f)) < 0.01f);
                    }
                }
                const float lit = drawn;
                const float off = std::remainder(lit - target, 360.0f);
                if (!(std::fabs(off) <= 15.01f)) {
                    std::fprintf(stderr, "stick toward %.0f deg (clockwise %d, start %.0f): sector at %.0f deg\n", target,
                                 clockwise ? 1 : 0, start, lit);
                }
                CHECK(std::fabs(off) <= 15.01f);
                // And the pointer picture too.
                CHECK(std::fabs(std::remainder(gc::shim().wheel().pointer_deg() - target, 360.0f)) <= 0.01f);
            }
            // The log has the first turns of the opening: the stick as read, its direction, the slot.
            CHECK(f.logged("0.00, -1.00), 90 deg on the screen"));
            CHECK_EQ(f.count_logs("of the opening: raw"), 5);
        }
    }
}

// Opened with the stick held: it does not pick until the stick rests (Going Commando's arming).
void test_arming() {
    Fake f;
    set_up(f, true, false);
    f.opener = true;
    float x, y;
    stick_at_item(9, x, y);
    play(f, 0x10, x, y);
    for (int i = 0; i < 6; ++i) {
        play(f, 0x10, x, y);
    }
    CHECK_EQ(selected(f), wheel_index(0x10));
    play(f, 0x10);
    play(f, 0x10, x, y);
    CHECK_EQ(selected(f), 9);
}

// The d-pad, one slot at a time, to `target` (an item, "None" -1, or a blank below), the short way
// in the spiral's order (blanks, "None", the items).
void dpad_to(Fake& f, int target) {
    for (int guard = 0; guard < 40 && selected(f) != target; ++guard) {
        play(f, 0x10 | (target > selected(f) ? gcw::kDpadRight : gcw::kDpadLeft));
        play(f, 0x10);
    }
    CHECK_EQ(selected(f), target);
}

// Let go: the open fade counts down, the pick at 0; then each kind of pick.
void commit(Fake& f, int target, std::uint32_t extra = 0) {
    open_hud(f);
    play(f, 0x10);
    dpad_to(f, target);
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10 | extra);
    }
    for (int i = 0; i < 10; ++i) {
        play(f, 0);
    }
}

void test_fades_and_commit() {
    Fake f;
    set_up(f, true, false);
    open_hud(f);
    play(f, 0x10);
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    CHECK_EQ(fade(f, 0), 8);
    // A new selection: the selection fade starts over, then grows to 8.
    dpad_to(f, wheel_index(0x0D) - 1);
    play(f, 0x10 | gcw::kDpadRight);
    CHECK_EQ(selected(f), wheel_index(0x0D));
    CHECK_EQ(fade(f, 1), 0);
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    CHECK_EQ(fade(f, 1), 8);
    f.wr<std::uint16_t>(f.d.item_price + 0x0D * gcw::kPriceStride + gcw::kPriceUsesAmmo, 0);
    f.wr<std::int32_t>(f.d.ammo + 4 * 0x0D, 55);  // more than the 40 it can hold (and it uses none)
    const std::uint32_t usage = f.d.usage_stats + gcw::kUsageStride * gcw::kUsageQuickSelect;
    for (int i = 0; i < 7; ++i) {
        play(f, 0);
        CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);
    }
    play(f, 0);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0x0D);
    CHECK(f.logged("picked item 0xD (Item10d), equipped 0xD"));
    CHECK_EQ(f.rd<std::uint16_t>(usage), 1);
    CHECK_EQ(f.rd<std::uint16_t>(usage + 2), 6000 / 600);
    CHECK_EQ(f.rd<std::uint32_t>(usage + 4), (1u << 3) | 0x80000000u);
    CHECK_EQ(f.rd<std::int32_t>(f.d.ammo + 4 * 0x0D), 55);  // no ammo: not clamped
    CHECK(!f.open());
    CHECK(!gc::shim().wheel().is_open() && !gc::shim().picture().shown);
    CHECK_EQ(f.rd<std::int32_t>(kRecord + gcw::kRecordSlide), -6);

    // Item 0x18: hero+0x1FF5, no pending item (Ratchet & Clank's rule).
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroPendingItem, 0);
    commit(f, wheel_index(0x18));
    CHECK_EQ(f.rd<std::uint8_t>(f.d.hero + gcw::kHeroItem18), 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);

    // An ammo weapon is held to its most.
    f.wr<std::int32_t>(f.d.ammo + 4 * 0x11, 55);
    commit(f, wheel_index(0x11));
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0x11);
    CHECK_EQ(f.rd<std::int32_t>(f.d.ammo + 4 * 0x11), 40);

    // No "None": the slot before the first item is a blank, which picks nothing.
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroPendingItem, 0);
    commit(f, -1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);

    // A blank: nothing.
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroPendingItem, 0);
    commit(f, -4);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);
    CHECK(!f.open());

    // Let go before the pointer moved: nothing picked (Going Commando's opens on nothing).
    commit(f, wheel_index(0x10));
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);
    CHECK(f.logged("cancelled (the pointer never moved; 0x10 in hand)"));
    // Moved away and back to the item in hand: nothing to equip.
    open_hud(f);
    play(f, 0x10);
    play(f, 0x10 | gcw::kDpadRight);
    play(f, 0x10);
    play(f, 0x10 | gcw::kDpadLeft);
    for (int i = 0; i < 10; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);
    // Circle: closed, nothing picked.
    commit(f, wheel_index(0x13), gcw::kCircle);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);
    CHECK(!f.open());
    CHECK(f.logged("cancelled (Circle; 0x10 in hand)"));
    CHECK(f.logged("cancelled (let go on a blank; 0x10 in hand)"));
    CHECK(f.logged("picked item 0x10 (Bomb Glove), equipped 0x10 (already in hand)"));
}

// Toggle: the wheel stays up when Triangle is let go; pressed again, it picks.
void test_toggle() {
    Fake f;
    set_up(f, true, false);
    gc::WheelOptions w = gc::shim().wheel_options();
    w.toggle = true;
    gc::shim().set_wheel_options(w);
    open_hud(f);
    play(f, 0x10);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    CHECK(f.open());
    float x, y;
    stick_at_item(9, x, y);
    play(f, 0, x, y);
    CHECK_EQ(selected(f), 9);
    play(f, 0x10);
    for (int i = 0; i < 10; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), kWheelOrder[9]);
    CHECK(!f.open());
}

void test_draw() {
    Fake f;
    set_up(f, true, false);
    open_hud(f);
    play(f, 0x10);
    dpad_to(f, wheel_index(0x12));  // item 0x12, no ammo, "Suck"
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    f.sprites.clear();
    f.texts.clear();
    const int w = gc::shim().selector_draw(kRecord);
    CHECK_EQ(w, 210);
    // The glyphs are the overlay's (Art::Glyph sprites, each inside its sector's square); the game's
    // own sprites only for the item without one (0x0C), when it is seen.
    const spiral::Frame frame = gc::shim().wheel().frame();
    CHECK_EQ(f.sprites.size(), frame.icons.size());
    for (std::size_t i = 0; i < f.sprites.size(); ++i) {
        const spiral::Item& item = gc::shim().wheel().items()[static_cast<std::size_t>(frame.icons[i].item)];
        CHECK(item.id == 0x0C && f.sprites[i].frame == fake_frame(item.icon, 0));
    }
    bool pointed = false;
    for (const spiral::Sprite& sp : gc::shim().picture().frame.sprites) {
        if (sp.art != spiral::Art::Glyph) {
            continue;
        }
        const float box = spiral::kSize * spiral::kGlyphSquare * (1.0f - 2.0f * spiral::kGlyphPadding);
        CHECK(sp.w <= box * 1.5f + 1e-4f && sp.h <= box * 1.5f + 1e-4f);  // within its loop's scale
        const float r = std::sqrt(sp.x * sp.x + sp.y * sp.y);
        if (std::fabs(r - spiral::kSize * spiral::kRadius) < 1e-3f && std::fabs(std::max(sp.w, sp.h) - box) < 1e-3f) {
            pointed = true;  // the pointed-at one: on the radius, the box's size
        }
    }
    CHECK(pointed);
    // No ammo: the name alone, above the middle, in Going Commando's white.
    CHECK_EQ(f.texts.size(), 1u);
    CHECK(f.texts[0].text == "Suck");
    CHECK_EQ(f.texts[0].x, 256);
    CHECK(f.texts[0].y < 208 && f.texts[0].y > 208 - 30);
    CHECK_EQ(f.texts[0].colour, 0x80f0f0f0u);

    // An ammo weapon: its name, and "12/40" below the middle.
    dpad_to(f, wheel_index(0x11));
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    f.texts.clear();
    gc::shim().selector_draw(kRecord);
    CHECK_EQ(f.texts.size(), 2u);
    if (f.texts.size() == 2) {
        CHECK(f.texts[0].text == "Pyro-citor");
        CHECK(f.texts[1].text == "12/40" && f.texts[1].y > 208);
    }
    // No ammo left: red.
    f.wr<std::int32_t>(f.d.ammo + 4 * 0x11, 0);
    f.texts.clear();
    gc::shim().selector_draw(kRecord);
    CHECK(f.texts.size() == 2 && f.texts[1].text == "0/40" && f.texts[1].colour == 0x804040ffu);
    // A gold weapon: its glyph from the gold icon frame (4), as the Quick Select page draws it; the
    // same pictures again on the next opening (kept while their pixels stay the same).
    f.wr<std::uint8_t>(f.d.gold + 0x11, 1);
    const std::size_t made = f.images.size();
    const std::vector<spiral::Item> again = gc::shim().collect_items();
    CHECK(gc::shim().glyph_image(fake_frame(0xE900 + 0x11, 4)) != 0);
    CHECK_EQ(f.images.size(), made + 1);
    for (const spiral::Item& it : again) {
        if (it.id == 0x11) {
            CHECK_EQ(it.glyph, gc::shim().glyph_image(fake_frame(0xE900 + 0x11, 4)));
        }
    }
    // Pixels changed (a new level's HUD): that frame's glyph made again, the old picture let go.
    const int old = gc::shim().glyph_image(fake_frame(0xE900 + 0x12, 0));
    f.wr<std::uint8_t>(kHudTables + 0x2000 + 5 * 32 + 5, 255);
    gc::shim().collect_items();
    CHECK(gc::shim().glyph_image(fake_frame(0xE900 + 0x12, 0)) != old);
    CHECK(!f.destroyed.empty() && f.destroyed.back() == old ? true : std::find(f.destroyed.begin(), f.destroyed.end(), old) != f.destroyed.end());
}

// "Slow-down before pausing": the hold done, the wheel opens in play and the game slows down (the
// port's time_stretch going up to 8, the scale down evenly), then Going Commando's pause; let go,
// and (with "Ease back on close") the time comes back up.
void test_slow_down() {
    Fake f;
    set_up(f, true, true);
    gc::WheelOptions w = gc::shim().wheel_options();
    w.ramp_frames = gc::kRampShort;
    w.ease_back = true;
    gc::shim().set_wheel_options(w);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    // The hold: the request's 4 frames; then the wheel in play, the world going on.
    int n = 0;
    while (!f.open() && n < 20) {
        play(f, 0x10);
        ++n;
    }
    CHECK(f.open() && f.rd<std::int32_t>(f.d.game_mode) == 0);
    CHECK(gc::shim().ramping_down());
    const int world_ticks = f.play_ticks;
    std::vector<float> seen;
    n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 0 && n < 30) {
        play(f, 0x10);
        seen.push_back(f.stretch);
        ++n;
    }
    // A few slower and slower ticks of the world, then the pause; the time at its pace again.
    CHECK(n >= 3 && n < 12);
    CHECK(f.play_ticks > world_ticks);
    for (std::size_t i = 1; i + 1 < seen.size(); ++i) {
        CHECK(seen[i] >= seen[i - 1]);
    }
    // The scale goes down evenly in the game's own time: its last ticks, the slowest, are long.
    CHECK(seen.size() >= 2 && seen[seen.size() - 2] > 2.0f && seen[seen.size() - 2] <= gc::kStretchMax);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    CHECK(gc::shim().in_wheel_freeze() && !gc::shim().ramping());
    CHECK(f.stretch == 1.0f);
    // The same record goes on in the pause; no watchdog.
    for (int i = 0; i < 40; ++i) {
        freeze(f, 0x10);
    }
    CHECK(f.open() && gc::shim().in_wheel_freeze());
    CHECK(!f.logged("watchdog"));
    dpad_to(f, wheel_index(0x13));
    // Let go: the pause ends, the time eases back up (8 down to 1), the pick made.
    n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 4 && n < 20) {
        freeze(f, 0);
        ++n;
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    CHECK(gc::shim().ramping() && f.stretch == gc::kStretchMax);
    seen.clear();
    for (int i = 0; i < 30; ++i) {
        play(f, 0);
        seen.push_back(f.stretch);
    }
    for (std::size_t i = 1; i < seen.size(); ++i) {
        CHECK(seen[i] <= seen[i - 1]);
    }
    CHECK(seen.back() == 1.0f && !gc::shim().ramping());
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0x13);

    // Let go in the slow-down: the pick at once, no pause, the time back up from where it got to.
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroPendingItem, 0);
    const int freezes = f.freeze_inits;
    for (int i = 0; i < 30; ++i) {
        play(f, 0);
    }
    n = 0;
    while (!gc::shim().ramping_down() && n < 20) {
        play(f, 0x10);
        ++n;
    }
    f.wr<std::uint8_t>(f.d.hero + gcw::kHeroItem18, 0);
    play(f, 0x10 | gcw::kDpadRight);
    for (int i = 0; i < 40; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.freeze_inits, freezes);
    CHECK(!f.open() && !gc::shim().ramping() && f.stretch == 1.0f);
    // One on from 0x10: 0x18, picked by its own rule (hero+0x1FF5).
    CHECK_EQ(f.rd<std::uint8_t>(f.d.hero + gcw::kHeroItem18), 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);

    // Without the ease back: the time at full pace as the pause ends.
    w.ease_back = false;
    gc::shim().set_wheel_options(w);
    for (int i = 0; i < 30; ++i) {
        play(f, 0);
    }
    n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 0 && n < 40) {
        play(f, 0x10);
        ++n;
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 4 && n < 20) {
        freeze(f, 0);
        ++n;
    }
    CHECK(!gc::shim().ramping() && f.stretch == 1.0f);
}

// A tick of whichever mode the game is in.
void tick(Fake& f, std::uint32_t held) {
    if (f.rd<std::int32_t>(f.d.game_mode) == 4) {
        freeze(f, held);
    } else {
        play(f, held);
    }
}

// The health meter hidden while the wheel shows (Prey hides its HUD): Ratchet & Clank's own
// Triangle shows it on every press, and it is closed on the wheel's first tick and every tick after,
// so no picture of the wheel has it; when the wheel closes, whichever way (a pick, Circle, after the
// watchdog), it is shown again as a Triangle press shows it, for scale_ticks(0xB4). The bolt counter
// is shown as before; nothing else of the HUD is closed.
void test_health_hidden() {
    for (int way = 0; way < 3; ++way) {
        Fake f;
        set_up(f, true, true);
        f.health_on_triangle = true;
        for (int i = 0; i < 12; ++i) {
            play(f, 0);
        }
        const int handle_before = f.health_handle_next;
        // Held: the game's Triangle shows the health meter at once (before the wheel), the wheel opens.
        int n = 0;
        while (!f.open() && n < 20) {
            tick(f, 0x10);
            ++n;
        }
        CHECK(f.open());
        CHECK(!f.health_open() && gc::shim().health_hidden());  // the wheel's first tick closed it
        CHECK(f.logged("the health meter (slot 1 at "));
        CHECK(f.logged(("id 0x7535, handle " + std::to_string(handle_before) + ",").c_str()));
        CHECK(f.logged("up, closed (remove_hud_item 1)"));
        // And kept closed every tick while the wheel shows, though the game shows it again.
        for (int i = 0; i < 6; ++i) {
            f.show_health();
            tick(f, 0x10);
            CHECK(!f.health_open() || !f.open());
            CHECK(!f.health_open());
        }
        CHECK(f.bolts_shown > 0);
        if (way == 2) {
            // The watchdog ends the pause: the wheel goes on in play until it is let go.
            gc::shim().end_paused("a test");
            CHECK(f.rd<std::int32_t>(f.d.game_mode) == 0 && f.open());
            play(f, 0x10);
            CHECK(!f.health_open());
        }
        // Let go (or Circle): the wheel closes, the health meter shows again for scale_ticks(0xB4).
        for (int i = 0; i < 20 && f.open(); ++i) {
            tick(f, way == 1 ? (i == 0 ? 0x10 | gcw::kCircle : 0x10) : 0);
            if (f.open()) {
                CHECK(!f.health_open());
            }
        }
        for (int i = 0; i < 20 && f.open(); ++i) {
            tick(f, 0);
        }
        CHECK(!f.open());
        CHECK(f.health_open() && !gc::shim().health_hidden());
        CHECK_EQ(f.rd<std::int32_t>(Fake::kHealth + 0x7C), 180);
        CHECK_EQ(f.others_removed, 0);
    }
    // PAL: scale_ticks(0xB4) is 150.
    {
        Fake f;
        set_up(f, false, true);
        f.health_on_triangle = true;
        for (int i = 0; i < 12; ++i) {
            play(f, 0);
        }
        for (int i = 0; i < 20 && !f.open(); ++i) {
            tick(f, 0x10);
        }
        CHECK(f.open() && !f.health_open());
        for (int i = 0; i < 40 && (f.open() || f.rd<std::int32_t>(f.d.game_mode) == 4); ++i) {
            tick(f, 0);
        }
        CHECK(f.health_open());
        CHECK_EQ(f.rd<std::int32_t>(Fake::kHealth + 0x7C), 150);
    }
    // Not up while the wheel showed: not shown after it either.
    Fake f;
    set_up(f, true, true);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    hold_until_freeze(f);
    for (int i = 0; i < 4; ++i) {
        freeze(f, 0x10);
    }
    CHECK(f.logged("not up"));
    for (int i = 0; i < 40 && (f.open() || f.rd<std::int32_t>(f.d.game_mode) == 4); ++i) {
        tick(f, 0);
    }
    CHECK(!f.open() && !f.health_open() && f.health_shown == 0);
}

void test_paused_path() {
    Fake f;
    set_up(f, false, true);  // PAL: the frame counts scaled
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    CHECK_EQ(gc_shim_press_window, 10);
    mark_letterbox(f);
    // Triangle pressed and held: the HUD-open request counts its 4 frames (3 ticks) down, then the
    // gate opens game mode 4, mode 7; that play tick is skipped.
    const int ticks_before = f.play_ticks;
    const int held = hold_until_freeze(f);
    CHECK_EQ(held, 5);
    CHECK_EQ(f.play_ticks, ticks_before + held - 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    CHECK(f.traced("done: the paused open after 4 ticks (state before/after, Triangle held, tap timer, window): 01H 12H 22H 22H 23H"));
    CHECK_EQ(f.rd<std::int32_t>(f.d.mode_freeze_info), gc::kFreezeKind);
    CHECK_EQ(f.rd<std::int32_t>(f.d.mode_freeze_info + 0x14), 0);
    CHECK_EQ(modal(gc::kModalMode), 7);
    CHECK_EQ(modal(gc::kModalWorldDrawn), 1);
    CHECK(gc::shim().in_wheel_freeze());
    // mode_freeze_init paused the music; mode 7 does not: unpaused after it.
    CHECK_EQ(f.music_pauses, 1);
    CHECK_EQ(f.music_unpauses, 1);
    CHECK_EQ(f.pause_group, 2);  // mode_freeze_init's, and menu_set_mode's cmd22(0x5D)
    // Phase 0, then 1: the wheel queued and updated in the freeze (update_hud_elements(1)), the
    // health meter and bolt counter shown.
    freeze(f, 0x10);
    CHECK_EQ(modal(gc::kModalPhase), 1);
    CHECK(!f.open());
    freeze(f, 0x10);
    CHECK(f.open());
    CHECK_EQ(modal(gc::kModalPhase), 1);
    // The bolt counter shown, the health meter not (hidden while the wheel shows).
    CHECK(f.health_shown == 0 && f.bolts_shown > 0);
    CHECK(f.help_updates > 0);
    CHECK_EQ(f.sound_updates, 2);
    // Held: phase 1 holds (the layer keeps g_modal_timer armed), the world too.
    float x, y;
    stick_toward(60.0f, x, y);
    for (int i = 0; i < 30; ++i) {
        freeze(f, 0x10, i < 2 ? 0.0f : x, i < 2 ? 0.0f : y);
    }
    CHECK_EQ(modal(gc::kModalPhase), 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    // The stick at 60 degrees: 0x11, the game's slot 1 (up-right, nearer the vertical), one slot
    // clockwise of 0x10 in hand at 12 o'clock.
    CHECK_EQ(selected(f), wheel_index(0x11));
    CHECK_EQ(f.play_ticks, ticks_before + held - 1);
    // Let go: phase 2 counts down, phase 3 ends: back to play, the sounds and the music on, the
    // pad's history cleared.
    const int unpauses = f.music_unpauses;
    int n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 4 && n < 20) {
        freeze(f, 0);
        ++n;
    }
    CHECK(n >= 2 && n < 20);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    CHECK_EQ(modal(gc::kModalMode), 0);
    CHECK(f.continue_group >= 1);
    CHECK(f.music_unpauses > unpauses);
    CHECK(f.flushes >= 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.pad + gcw::kPadHistCount), 0);
    // The wheel fades out in play and commits there.
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0x11);
    CHECK(!f.open());
    // Going Commando's bars are left out: Ratchet & Clank's letterbox is never written.
    CHECK(letterbox_untouched(f));
    // The request's line once, though it stays done while the wheel is up.
    CHECK_EQ(f.count_logs("HUD-open request asked"), 1);
    // No second open from the same press: the history was cleared.
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
}

void test_gate_conditions() {
    Fake f;
    set_up(f, true, true);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    // A refused press stays in the pad's history for the window: cleared before the next case.
    auto forget = [&] { f.wr<std::int32_t>(f.d.pad + gcw::kPadHistCount, 0); };
    // In Clank's mode (1): Ratchet & Clank's four-slot radial is the game's; no paused open.
    f.wr<std::uint8_t>(f.d.hero + gcw::kHeroVehicleMode, 1);
    play(f, 0x10);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    forget();
    f.wr<std::uint8_t>(f.d.hero + gcw::kHeroVehicleMode, 0);
    play(f, 0);
    // The slot locked, then fewer than 8 ticks since the level's mode changed: no open.
    f.wr<std::uint8_t>(f.d.hero + gcw::kHeroSwitchLock, 2);
    play(f, 0x10);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    forget();
    f.wr<std::uint8_t>(f.d.hero + gcw::kHeroSwitchLock, 0);
    play(f, 0);
    f.wr<std::int32_t>(f.d.cinematic_frames, 5);
    play(f, 0x10);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    CHECK_EQ(gc_shim_hud_flag_b, 0);
    // The press is still within the window when the frames reach 8: the request starts then, and
    // the freeze once it is done, Triangle held.
    for (int i = 0; i < 10 && f.rd<std::int32_t>(f.d.game_mode) == 0; ++i) {
        play(f, 0x10);
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
}

// The executable's game mode word where the level's program keeps it (Mod::relocate): read and
// written there, not at the executable's address.
void test_relocated_game_mode() {
    Fake f;
    set_up(f, true, true, 0x40);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    hold_until_freeze(f);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    CHECK_EQ(f.rd<std::int32_t>(gcw::disc_for(true).game_mode), 5);
    CHECK(gc::shim().in_wheel_freeze());
}

// Each level's own callbacks, from the catalogues' places, checked against the function table.
void test_level_callbacks() {
    Fake f;
    f.level = 17;
    set_up(f, true, true);
    CHECK_EQ(gc::shim().level().selector_init, 0x00232DD0u);
    CHECK_EQ(gc::shim().level().selector_update, 0x00232E88u);
    CHECK_EQ(gc::shim().level().selector_draw, 0x00233588u);
    CHECK_EQ(gc::shim().level().bolt_draw, 0x002350E0u);
    CHECK(gc::shim().paused_path_available());
    CHECK(f.logged("level 17: the HUD callbacks found"));
    Fake p;
    p.level = 18;
    set_up(p, false, true);
    CHECK_EQ(gc::shim().level().selector_init, 0x0023B9E8u);
    Fake n;
    n.level = 17;
    n.match_code = false;
    set_up(n, true, true);
    CHECK(!gc::shim().paused_path_available());
    CHECK(n.logged("WARN level 17: the HUD callbacks NOT all found"));
    CHECK(n.logged("NOT that function"));
}

// "Pause while open" on: the game's own Triangle opener is always refused; a tap does nothing and
// never freezes; a release before the request is done cancels it; a double tap within 20 ticks is
// the game's own quick swap, once.
void test_taps() {
    Fake f;
    f.opener = true;
    f.rac1_timers = true;
    set_up(f, true, true);
    for (int i = 0; i < 25; ++i) {
        play(f, 0);
    }
    // A tap: refused quick select, request cancelled as Triangle is let go, no freeze, no switch.
    play(f, 0x10);
    CHECK(!f.open());
    CHECK(f.logged("the game's own quick select refused"));
    CHECK_EQ(gc_shim_hud_flag_b, 1);
    play(f, 0);
    for (int i = 0; i < 25; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.freeze_inits, 0);
    CHECK_EQ(gc_shim_hud_flag_b, 0);
    CHECK(f.logged("cancelled: Triangle let go"));
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);
    CHECK_EQ(f.swaps, 0);
    // Held a little, let go before the request is done: cancelled, no freeze.
    play(f, 0x10);
    play(f, 0x10);
    play(f, 0x10);
    play(f, 0);
    for (int i = 0; i < 25; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.freeze_inits, 0);
    CHECK_EQ(f.swaps, 0);
    // A double tap: the swap, once; the request cancelled; no freeze.
    play(f, 0x10);
    play(f, 0);
    play(f, 0x10);
    play(f, 0);
    for (int i = 0; i < 25; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.swaps, 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0x11);
    CHECK_EQ(f.freeze_inits, 0);
    // (The gate asks again each tick the press is within its window, so the request ends for good
    // once the presses are out of it.)
    CHECK(f.count_logs("cancelled: Triangle let go") >= 3);
    CHECK(f.logged("double tap: swap from item 0x10 to 0x11 (the quick swap item"));
    // Held past the request's frames: the freeze, once.
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroPendingItem, 0);
    const int held = hold_until_freeze(f);
    CHECK_EQ(held, 6);  // NTSC: 4 frames counted after the tick it is asked
    CHECK_EQ(f.freeze_inits, 1);
    // Let go at once, nothing selected: the freeze ends, nothing committed.
    int n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 4 && n < 20) {
        freeze(f, 0);
        ++n;
    }
    CHECK(n < 10);
    CHECK(!f.logged("watchdog"));
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroPendingItem), 0);
    CHECK_EQ(f.swaps, 1);
    CHECK_EQ(gc_shim_hud_flag_b, 0);
}

// The paused open not possible (the callbacks not found), the pause on: nothing opens; the double
// tap is still the game's.
void test_unavailable_paused_open() {
    Fake f;
    f.level = 17;
    f.match_code = false;
    f.opener = true;
    f.rac1_timers = true;
    set_up(f, true, true);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    CHECK(!f.open());
    CHECK(f.logged("Triangle: the gate does not open: the quick select's callbacks were not found"));
    play(f, 0);
    for (int i = 0; i < 25; ++i) {
        play(f, 0);
    }
    play(f, 0x10);
    play(f, 0);
    play(f, 0x10);
    play(f, 0);
    CHECK_EQ(f.swaps, 1);
    CHECK_EQ(f.count_logs("Triangle: "), 3);
}

// The freeze's tick hook bypassed (a port that calls the game's update_mode_freeze directly): two
// pictures with no tick end the freeze, the paused open goes off. The picture of the mod's kind is
// Going Commando's, not the dialog.
void test_watchdog_update_bypassed() {
    Fake f;
    set_up(f, true, true);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    hold_until_freeze(f);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    bool dialog = false;
    for (int i = 0; i < 2; ++i) {
        f.poll(0x10);
        gc::shim().freeze_draw([&] { dialog = true; });
    }
    CHECK(!dialog);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    CHECK(gc::shim().paused_disabled());
    CHECK(f.logged("WARN rac1.freeze.update did not run"));
    CHECK(f.logged("WARN the paused quick select ended by its watchdog: rac1.freeze.update did not run"));
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    // Another freeze kind is the game's: its own picture.
    f.wr<std::int32_t>(f.d.mode_freeze_info, 2);
    dialog = false;
    gc::shim().freeze_draw([&] { dialog = true; });
    CHECK(dialog);
}

// The wheel's record never opens in the freeze: the watchdog ends it.
void test_watchdog_record_never_opens() {
    Fake f;
    f.block_selector = true;
    set_up(f, false, true);
    for (int i = 0; i < 12; ++i) {
        play(f, 0);
    }
    hold_until_freeze(f);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    int n = 0;
    while (f.rd<std::int32_t>(f.d.game_mode) == 4 && n < 60) {
        freeze(f, 0x10);
        ++n;
    }
    // Going Commando's own phases end it: with no quick select to set its timer again
    // (menu_set_state_word_three), phase 1 ends after 3 frames and the counter after 6.
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
    CHECK(n < 12);
    CHECK(!f.logged("watchdog"));
    CHECK(f.continue_group >= 1);
}

// A held press opens at the same tick every time (NTSC: the state machine's 4 frames after the
// tick it is asked, tick 5), and a request ends only on a real release.
void test_hold_timing() {
    Fake f;
    f.opener = true;
    f.rac1_timers = true;
    set_up(f, true, true);
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < 30; ++i) {
            play(f, 0);
        }
        const int logs = f.count_traced("after 5 ticks (state before/after, Triangle held, tap timer, window): 01H 12H 22H 22H 22H 23H");
        CHECK_EQ(hold_until_freeze(f), 6);
        CHECK_EQ(f.count_traced("after 5 ticks (state before/after, Triangle held, tap timer, window): 01H 12H 22H 22H 22H 23H"), logs + 1);
        int n = 0;
        while (f.rd<std::int32_t>(f.d.game_mode) == 4 && n < 20) {
            freeze(f, 0);
            ++n;
        }
    }
    CHECK_EQ(f.count_logs("cancelled:"), 0);
    CHECK_EQ(f.freeze_inits, 3);
    // Held 3 ticks, let go: cancelled on the release (the gate asks again while the press is in its
    // window, and the request ends once it is out), no freeze.
    for (int i = 0; i < 30; ++i) {
        play(f, 0);
    }
    play(f, 0x10);
    play(f, 0x10);
    play(f, 0x10);
    for (int i = 0; i < 15; ++i) {
        play(f, 0);
    }
    CHECK_EQ(f.freeze_inits, 3);
    CHECK_EQ(f.count_logs("cancelled: Triangle let go"), 1);
    CHECK(f.traced(" 01H 12H 22H 21-"));

    // A tap in the 8 ticks after a freeze: the gate does not open, the game's own double tap gets it.
    hold_until_freeze(f);
    while (f.rd<std::int32_t>(f.d.game_mode) == 4) {
        freeze(f, 0);
    }
    play(f, 0x10);
    CHECK(f.logged("fewer than 8 ticks since the game mode changed"));
    CHECK_EQ(f.rd<std::int16_t>(f.d.hero + gcw::kHeroTapTimer), 20);
}

// The double tap's logs: a swap (from, to, its timers), and a second release that does not swap
// and why.
void test_double_tap_logs() {
    Fake f;
    f.opener = true;
    f.rac1_timers = true;
    set_up(f, true, true);
    for (int i = 0; i < 30; ++i) {
        play(f, 0);
    }
    play(f, 0x10);
    play(f, 0);
    play(f, 0x10);
    play(f, 0);
    CHECK_EQ(f.swaps, 1);
    CHECK(f.logged("double tap: swap from item 0x10 to 0x11 (the quick swap item; tap timer 20, window 19 before)"));
    // The quick swap item already in hand: no swap, and why.
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroItem, 0x11);
    for (int i = 0; i < 30; ++i) {
        play(f, 0);
    }
    play(f, 0x10);
    play(f, 0);
    play(f, 0x10);
    play(f, 0);
    CHECK_EQ(f.swaps, 1);
    CHECK(f.logged("no swap: the quick swap item is in hand (item 0x11, quick swap 0x11"));
}

// Weapons picked with the wheel go through Ratchet & Clank's own change of item (the pending item,
// hero_switch_item's bookkeeping), so the double tap swaps between the last two: A, then B, picked;
// a double tap goes back to A, another to B.
void test_wheel_picks_then_double_tap() {
    Fake f;
    f.opener = true;
    f.rac1_timers = true;
    f.rac1_switch = true;
    set_up(f, true, true);
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroItem, 0x10);
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroRequestedItem, 0x10);
    f.wr<std::int32_t>(f.d.last_weapon, 0x10);
    f.wr<std::int32_t>(f.d.quick_swap_item, 0);
    auto pick = [&](float angle) {
        for (int i = 0; i < 30; ++i) {
            play(f, 0);
        }
        hold_until_freeze(f);
        float x, y;
        stick_at(angle, x, y);
        for (int i = 0; i < 6; ++i) {
            freeze(f, 0x10, i < 2 ? 0.0f : x, i < 2 ? 0.0f : y);
        }
        int n = 0;
        while (f.rd<std::int32_t>(f.d.game_mode) == 4 && n < 20) {
            freeze(f, 0);
            ++n;
        }
        for (int i = 0; i < 30; ++i) {
            play(f, 0);
        }
    };
    auto double_tap = [&] {
        for (int i = 0; i < 30; ++i) {
            play(f, 0);
        }
        play(f, 0x10);
        play(f, 0);
        play(f, 0x10);
        play(f, 0);
        for (int i = 0; i < 30; ++i) {
            play(f, 0);
        }
    };
    // The pad's angle (atan2(y, x), y down) of the item's slot on the screen.
    auto angle_of = [](std::int32_t id) { return -static_cast<float>(wheel_index(id) * 30 + 90) * kPi / 180.0f; };
    pick(angle_of(0x11));  // item 0x11 (A)
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem), 0x11);
    pick(angle_of(0x12));  // item 0x12 (B)
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem), 0x12);
    CHECK_EQ(f.rd<std::int32_t>(f.d.quick_swap_item), 0x11);
    CHECK_EQ(f.rd<std::int32_t>(f.d.last_weapon), 0x12);
    double_tap();
    CHECK_EQ(f.swaps, 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem), 0x11);
    CHECK(f.logged("double tap: swap from item 0x12 to 0x11 (the quick swap item"));
    double_tap();
    CHECK_EQ(f.swaps, 2);
    CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem), 0x12);
    CHECK(f.logged("double tap: swap from item 0x11 to 0x12 (the quick swap item"));
    CHECK_EQ(f.freeze_inits, 2);
}


// A natural double tap with "Pause while open" on: two presses of `hold` ticks, `gap` ticks apart.
// Going Commando's mode 7 runs the double tap's timers too (its level programs' UpdateModalState
// calls hero_update_button_hold_timers where rac2-decomp's C calls stub_empty_20), so a second press
// long enough to open the paused quick select still swaps as it is let go.
void test_natural_double_tap() {
    for (int hold : {2, 3, 4, 5, 6}) {
        for (int gap : {3, 5, 7}) {
            Fake f;
            f.opener = true;
            f.rac1_timers = true;
            f.rac1_switch = true;
            set_up(f, true, true);
            f.wr<std::int32_t>(f.d.hero + gcw::kHeroItem, 0x12);
            f.wr<std::int32_t>(f.d.hero + gcw::kHeroRequestedItem, 0x12);
            f.wr<std::int32_t>(f.d.last_weapon, 0x12);
            f.wr<std::int32_t>(f.d.quick_swap_item, 0x11);
            for (int i = 0; i < 30; ++i) {
                play(f, 0);
            }
            for (int i = 0; i < hold; ++i) tick(f, 0x10);
            for (int i = 0; i < gap; ++i) tick(f, 0);
            for (int i = 0; i < hold; ++i) tick(f, 0x10);
            for (int i = 0; i < 40; ++i) tick(f, 0);
            if (f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem) != 0x11) {
                std::fprintf(stderr, "natural double tap: hold %d, gap %d: item 0x%X, freezes %d\n", hold, gap,
                             f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem), f.freeze_inits);
                if (std::getenv("SPIRAL_DEBUG")) {
                    for (const std::string& l : f.logs) {
                        std::fprintf(stderr, "  %s\n", l.c_str());
                    }
                }
            }
            CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem), 0x11);
            CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 0);
        }
    }
}

// "Hold before pausing": the countdown's start only. "A little longer" (the default, 8 frames):
// a 6-tick tap never pauses, a natural double tap swaps with no pause, a hold opens at tick 9;
// "Longer" (12) at tick 13; "Going Commando" (4) at tick 5; PAL scaled (8 -> 7: tick 8).
void test_open_delay_setting() {
    struct Case {
        bool ntsc;
        int frames;
        int opens_at;
    };
    for (const Case c : {Case{true, gc::kOpenDelayLittleLonger, 9}, Case{true, gc::kOpenDelayLonger, 13},
                         Case{true, gc::kGcOpenDelay, 5}, Case{false, gc::kOpenDelayLittleLonger, 8}}) {
        Fake f;
        f.opener = true;
        f.rac1_timers = true;
        f.rac1_switch = true;
        set_up(f, c.ntsc, true);
        gc::shim().set_options(true, true, c.frames);
        f.wr<std::int32_t>(f.d.hero + gcw::kHeroItem, 0x12);
        f.wr<std::int32_t>(f.d.hero + gcw::kHeroRequestedItem, 0x12);
        f.wr<std::int32_t>(f.d.last_weapon, 0x12);
        f.wr<std::int32_t>(f.d.quick_swap_item, 0x11);
        for (int i = 0; i < 30; ++i) {
            play(f, 0);
        }
        CHECK_EQ(hold_until_freeze(f) - 1, c.opens_at);
        while (f.rd<std::int32_t>(f.d.game_mode) == 4) {
            freeze(f, 0);
        }
        if (c.frames != gc::kOpenDelayLittleLonger || !c.ntsc) {
            continue;
        }
        // A 6-tick tap: no pause.
        for (int i = 0; i < 30; ++i) play(f, 0);
        const int freezes = f.freeze_inits;
        for (int i = 0; i < 6; ++i) tick(f, 0x10);
        for (int i = 0; i < 30; ++i) tick(f, 0);
        CHECK_EQ(f.freeze_inits, freezes);
        // A double tap of 6-tick presses, 5 apart: the swap, no pause.
        for (int i = 0; i < 6; ++i) tick(f, 0x10);
        for (int i = 0; i < 5; ++i) tick(f, 0);
        for (int i = 0; i < 6; ++i) tick(f, 0x10);
        for (int i = 0; i < 30; ++i) tick(f, 0);
        CHECK_EQ(f.freeze_inits, freezes);
        CHECK_EQ(f.rd<std::int32_t>(f.d.hero + gcw::kHeroItem), 0x11);
        CHECK_EQ(f.swaps, 1);
    }
}

void test_quick_swap() {
    Fake f;
    set_up(f, false, true);
    gc::Shim& s = gc::shim();
    auto timers = [&] {
        gc::CopyScope scope(s);
        hero_update_button_hold_timers();
    };
    const std::uint32_t h = f.d.hero;
    // First press: the tap timer (20 frames: 17 ticks on PAL).
    f.poll(0x10);
    timers();
    // (Both timers count down first, then the press sets the tap timer.)
    CHECK_EQ(f.rd<std::int16_t>(h + gcw::kHeroTapTimer), 17);
    // Released: the window opens.
    f.poll(0);
    timers();
    CHECK_EQ(f.rd<std::int16_t>(h + gcw::kHeroTapWindow), 17);
    // Pressed and released again in the window: the quick swap item pending, the timers cleared,
    // the swap counted.
    f.poll(0x10);
    timers();
    f.poll(0);
    timers();
    CHECK_EQ(f.rd<std::int32_t>(h + gcw::kHeroPendingItem), 0x11);
    CHECK_EQ(f.rd<std::int16_t>(h + gcw::kHeroTapTimer), 0);
    CHECK_EQ(f.rd<std::int16_t>(h + gcw::kHeroTapWindow), 0);
    CHECK_EQ(f.rd<std::uint16_t>(f.d.usage_stats + gcw::kUsageStride * gcw::kUsageQuickSwap), 1);
    // With the wrench in hand: the last weapon.
    f.wr<std::int32_t>(h + gcw::kHeroItem, 8);
    f.wr<std::int32_t>(h + gcw::kHeroPendingItem, 0);
    f.poll(0x10);
    timers();
    f.poll(0);
    timers();
    f.poll(0x10);
    timers();
    f.poll(0);
    timers();
    CHECK_EQ(f.rd<std::int32_t>(h + gcw::kHeroPendingItem), 0x13);
    // The window runs out: no swap.
    f.wr<std::int32_t>(h + gcw::kHeroPendingItem, 0);
    f.poll(0x10);
    timers();
    f.poll(0);
    timers();
    for (int i = 0; i < 40; ++i) {
        f.poll(0);
        timers();
    }
    f.poll(0x10);
    timers();
    f.poll(0);
    timers();
    CHECK_EQ(f.rd<std::int32_t>(h + gcw::kHeroPendingItem), 0);
}

}  // namespace

int main() {
    test_frames_and_ticks();
    test_pad_history_and_press_window();
    test_items();
    test_spiral_selection();
    test_arming();
    test_stick_points_where_it_is_pushed();
    test_fades_and_commit();
    test_toggle();
    test_draw();
    test_paused_path();
    test_health_hidden();
    test_slow_down();
    test_gate_conditions();
    test_relocated_game_mode();
    test_level_callbacks();
    test_taps();
    test_hold_timing();
    test_double_tap_logs();
    test_wheel_picks_then_double_tap();
    test_natural_double_tap();
    test_open_delay_setting();
    test_unavailable_paused_open();
    test_watchdog_update_bypassed();
    test_watchdog_record_never_opens();
    test_quick_swap();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
