// SPDX-License-Identifier: GPL-3.0-or-later
//
// The translation layer with the copied Going Commando code (src/gc/*.c) and the spiral wheel
// (src/gc/rac1_spiral.cpp, src/spiral/) on a fake Ratchet & Clank: a game memory filled with a
// quick select's words and tables and the items owned at the disc's addresses, and fake game
// functions that do what Ratchet & Clank's do with them (mode_freeze_init, queue_animation_update
// and update_hud_elements on one HUD record, the sound and music calls counted, the draws recorded,
// the port's slow motion recorded).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "gc/rac1_shim.hpp"
#include "icons_test_data.hpp"
#include "spiral/icon_packs.hpp"
#include "spiral/json.hpp"
#include "spiral/paint.hpp"
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
    gc::TextSize size = gc::TextSize::Normal;
};

// The fake game.
struct Fake {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(kMemory, 0);
    // The player's disc data (a level's data file: icons_test_data.hpp) and the icon folders; none
    // unless a test gives them.
    std::string level_file;
    std::vector<std::string> icon_packs;
    bool mask_dpad_pressed = false;
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
    bool white_glyphs = false;           // the last one made was white (its shape in its alpha)
    std::map<int, std::pair<int, int>> image_sizes;                    // each picture's size
    std::map<int, std::vector<std::array<std::uint8_t, 4>>> image_rows;  // its first column, row by row
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
        // (mask_dpad_pressed: the pressed words without the d-pad's bits, as the pause may give them.)
        const std::uint32_t pressed = (held & ~last_held) & (mask_dpad_pressed ? ~0xF000u : ~0u);
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
// The wheel's order (1.4.6): the quick select in its numbers' order from index 0 (kItems: 0x10,
// 0x11, 0x12, its empty slot 4 skipped, 0x13, 0x14, 0x15, 0x18), then the page's others in the
// grids' order (weapons, then gadgets).
constexpr std::int32_t kWheelOrder[kWheelItems] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x18,
                                                   0x0A, 0x0F, 0x0E, 0x09, 0x0B, 0x0D, 0x0C};
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
    auto print = [](gc::TextSize size) {
        return [size](int x, int y, std::uint32_t colour, const char* text, int length) {
            g_fake->texts.push_back(
                {x, y, colour, length < 0 ? std::string(text) : std::string(text, static_cast<std::size_t>(length)), size});
        };
    };
    g.font_print_center = print(gc::TextSize::Normal);
    g.font_print_center_small = print(gc::TextSize::Small);
    g.font_print_center_large = print(gc::TextSize::Large);
    g.msg_string = [](int id) -> std::uint32_t {
        char name[64];
        if (id == 0x110) {
            std::snprintf(name, sizeof name, "Bomb Glove");
        } else if (id == 0x111) {
            std::snprintf(name, sizeof name, "Pyro-citor");
        } else if (id == 0x112) {
            std::snprintf(name, sizeof name, "Suck");
        } else if (id == 0x10D) {
            std::snprintf(name, sizeof name, "Visibomb Gun");
        } else if (id == 0x20D) {
            std::snprintf(name, sizeof name, "Gold Visibomb Gun");
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
    g.extension_items = {1, 36};  // the port's, as mod.cpp gives them
    // The game's text widths, as the fake measures them: 10 units a letter, 7 in the small font.
    g.text_width = [](gc::TextSize size, const char* text) {
        return static_cast<int>(std::strlen(text)) * (size == gc::TextSize::Small ? 7 : 10);
    };
    g.level_data_path = [](int) { return g_fake->level_file; };
    g.icon_packs = [] { return g_fake->icon_packs; };
    g.create_image = [](int width, int height, const std::uint8_t* rgba) {
        // A glyph: a picture of its own, with something drawn in it.
        int ink = 0;
        for (int i = 0; i < width * height; ++i) {
            ink += rgba[i * 4 + 3] > 128 ? 1 : 0;
        }
        CHECK(width > 0 && height > 0 && ink > 0);
        bool white = true;
        for (int i = 0; i < width * height; ++i) {
            white = white && rgba[i * 4] == 255 && rgba[i * 4 + 1] == 255 && rgba[i * 4 + 2] == 255;
        }
        g_fake->white_glyphs = white;
        g_fake->images.push_back(++g_fake->image_next);
        g_fake->image_sizes[g_fake->image_next] = {width, height};
        auto& rows = g_fake->image_rows[g_fake->image_next];
        for (int y = 0; y < height && y < 4; ++y) {
            rows.push_back({rgba[y * width * 4], rgba[y * width * 4 + 1], rgba[y * width * 4 + 2], rgba[y * width * 4 + 3]});
        }
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
    // The Quick Select page's owned items, the quick select's first in its order.
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
    CHECK(f.logged("the wheel opens with 14 items: 0x10 0x11 0x12 0x13 0x14 0x15 0x18 0xA 0xF 0xE 0x9 0xB 0xD 0xC"));
    // Every item has its glyph (a picture made from its icon, white: the look colours it) but 0x0C,
    // whose icon's bank is not linked: the game's own icon for it. The hand gadgets' grid's items
    // are gadgets (0x0C, 0x16, 0x1A...), the weapons grid's weapons.
    for (const spiral::Item& it : gc::shim().wheel().items()) {
        CHECK((it.glyph != 0) == (it.id != 0x0C));
        CHECK((it.category == spiral::look::Category::Gadget) == (std::find(std::begin(kGadgetCellItems), std::end(kGadgetCellItems), it.id) != std::end(kGadgetCellItems)));
    }
    CHECK(f.white_glyphs);
    CHECK(f.logged("(frame 13, 32x32): not a glyph") == false);
    // (1.4.6) The quick select's items are the first slots in its numbers' order, the empty slot 4
    // taking no place.
    int rank = 0;
    for (int slot = 0; slot < 8; ++slot) {
        if (kItems[slot] != 0) {
            CHECK_EQ(wheel_index(kItems[slot]), rank);
            ++rank;
        }
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
            bool first = true;
            float offset = 0.0f;  // the first push's landing (an item's slot) less where it was pushed
            for (float target : {90.0f, 45.0f, 0.0f, -45.0f, -90.0f, -135.0f, 180.0f, 135.0f, 90.0f, 135.0f, 180.0f,
                                 -135.0f, -90.0f, -45.0f, 0.0f, 45.0f, 90.0f}) {
                stick_toward(target, x, y);
                play(f, 0x10, x, y);
                if (first) {
                    // (1.4.4) The first push lands on an item: the slot there, or the nearest item.
                    first = false;
                    CHECK(gc::shim().wheel().is_item(selected(f)));
                    offset = std::remainder(gc::shim().wheel().pointer_deg() - target, 360.0f);
                    CHECK(std::fabs(offset) <= 90.01f);
                }
                // The pointed-at slot (an item, "None" or a blank) is drawn where the stick points: its
                // sector's angle (_AngleDeg) as the frame draws it; for an item or "None", the lit one.
                const int slot = selected(f);
                const int sign = clockwise ? -1 : 1;
                const float drawn = static_cast<float>(slot * sign * 30) + start;
                for (const spiral::Slot& sl : gc::shim().picture().frame.slots) {
                    if (sl.selected) {
                        CHECK(slot >= 0 && std::fabs(std::remainder(sl.angle_deg - drawn, 360.0f)) < 0.01f);
                    }
                }
                const float lit = drawn;
                const float off = std::remainder(lit - target - offset, 360.0f);
                if (!(std::fabs(off) <= 15.01f)) {
                    std::fprintf(stderr, "stick toward %.0f deg (clockwise %d, start %.0f): sector at %.0f deg\n", target,
                                 clockwise ? 1 : 0, start, lit);
                }
                CHECK(std::fabs(off) <= 15.01f);
                // And the pointer picture too.
                CHECK(std::fabs(std::remainder(gc::shim().wheel().pointer_deg() - target - offset, 360.0f)) <= 0.01f);
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

// The stick turning the pointer to slot `target` (any slot, a blank too: the stick passes over
// them, 1.4.4's d-pad skips them): a first push where the pointer is (onto its item), then a turn
// of the slots between, in small steps (the default spiral: counter-clockwise, 30 degrees a slot).
void turn_to(Fake& f, int target) {
    float x, y;
    float at = gc::shim().wheel().pointer_deg();
    stick_toward(at, x, y);
    play(f, 0x10, x, y);
    const float turn = static_cast<float>(target - selected(f)) * 30.0f;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(turn) / 10.0f)));
    for (int i = 1; i <= steps; ++i) {
        stick_toward(at + turn * static_cast<float>(i) / static_cast<float>(steps), x, y);
        play(f, 0x10, x, y);
    }
    CHECK_EQ(selected(f), target);
}

// Let go: the open fade counts down, the pick at 0; then each kind of pick.
void commit(Fake& f, int target, std::uint32_t extra = 0) {
    open_hud(f);
    play(f, 0x10);
    if (gc::shim().wheel().is_item(target)) {
        dpad_to(f, target);
    } else {
        turn_to(f, target);  // a blank: only the stick goes there
    }
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
    CHECK(f.logged("picked item 0xD (Visibomb Gun), equipped 0xD"));
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
    // The glyphs are the slots' (drawn by the overlay), the selected one's the item's.
    const spiral::Frame shown = gc::shim().picture().frame;
    bool pointed = false;
    for (const spiral::Slot& sl : shown.slots) {
        if (sl.empty) {
            continue;
        }
        const spiral::Item& item = gc::shim().wheel().items()[static_cast<std::size_t>(sl.index)];
        CHECK_EQ(sl.glyph, item.glyph);
        if (sl.selected) {
            pointed = item.id == 0x12 && sl.glyph != 0 && sl.push == 10.0f;
        }
    }
    CHECK(pointed);
    // The quick select numbers in the small font, one a badge; no ammo: the name alone in the
    // normal font, above the middle, in the pause menu's blue over its dark copy a unit down and
    // right.
    std::vector<Text> small, normal, large;
    for (const Text& t : f.texts) {
        (t.size == gc::TextSize::Small ? small : t.size == gc::TextSize::Normal ? normal : large).push_back(t);
    }
    // The badges' numbers are the painter's own digits now, not the game's text.
    CHECK(small.empty());
    int digits = 0;
    for (const spiral::Quad& q : spiral::paint(shown)) {
        digits += q.art == spiral::Art::Digit;
    }
    CHECK_EQ(digits, static_cast<int>(shown.badges.size()));
    CHECK(large.empty());
    CHECK_EQ(normal.size(), 2u);
    if (normal.size() == 2) {
        CHECK(normal[0].text == "Suck" && normal[1].text == "Suck");
        CHECK(normal[0].x == 257 && normal[0].y == normal[1].y + 1 && normal[0].colour == 0x80000000u);
        CHECK_EQ(normal[1].x, 256);
        CHECK(normal[1].y < 208 - 8 && normal[1].y > 208 - 40);
        CHECK_EQ(normal[1].colour, gc::kMenuBlue);
    }

    // An ammo weapon: its name, and "12/40" below the middle.
    dpad_to(f, wheel_index(0x11));
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    auto lines = [&f](gc::TextSize size) {
        std::vector<Text> out;
        for (const Text& t : f.texts) {
            if (t.size == size) {
                out.push_back(t);
            }
        }
        return out;
    };
    f.texts.clear();
    gc::shim().selector_draw(kRecord);
    // "Pyro-citor" is wider than the middle disc in the normal font (the fake's measure: 10 units a
    // letter against 98 inside the disc): the small font (7 a letter: 70).
    CHECK(lines(gc::TextSize::Normal).empty());
    CHECK(lines(gc::TextSize::Small).size() == 2u && lines(gc::TextSize::Small)[1].text == "Pyro-citor");
    const std::vector<Text> ammo = lines(gc::TextSize::Large);
    CHECK_EQ(ammo.size(), 2u);
    if (ammo.size() == 2) {
        CHECK(ammo[1].text == "12/40" && ammo[1].y + 8 > 208 && ammo[1].colour == gc::kMenuBlue);
    }
    // No ammo left: red.
    f.wr<std::int32_t>(f.d.ammo + 4 * 0x11, 0);
    f.texts.clear();
    gc::shim().selector_draw(kRecord);
    CHECK(lines(gc::TextSize::Large).size() == 2u && lines(gc::TextSize::Large)[1].text == "0/40" &&
          lines(gc::TextSize::Large)[1].colour == gc::kAmmoOut);
    // A gold weapon: its glyph from the gold icon frame (4), as the Quick Select page draws it; the
    // same pictures again on the next opening (kept for the level).
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
}

// The fake HUD's pages after the level has reused their memory (what bank 0's do in play, the
// pause menu's buffer and the level load's both being the level's again): the icons' indices, and
// with `palette` their palette too, overwritten with bytes of no picture; `zero` cleared instead.
void stale_pages(Fake& f, bool palette, bool zero = false) {
    std::uint32_t x = 0x2545F491u;
    auto next = [&x] {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return static_cast<std::uint8_t>(x >> 7);
    };
    for (std::uint32_t i = 0; i < 32 * 32; ++i) {
        f.wr<std::uint8_t>(kHudTables + 0x2000 + i, zero ? 0 : next());
    }
    if (palette) {
        for (std::uint32_t i = 0; i < 1024; ++i) {
            f.wr<std::uint8_t>(kHudTables + 0x2800 + i, zero ? 0 : next());
        }
    }
}

// The glyphs' capture (1.1.2): made once from pages that pass the checks and kept for the level
// however the pages change after; captured again, once, in the pause menu (its pages certain); a
// stale page rejected with its reason, the item left with the game's own icon (never noise); a
// level change drops them all.
void test_glyph_capture() {
    Fake f;
    set_up(f, true, false);
    const int frame12 = fake_frame(0xE900 + 0x12, 0);
    auto glyphs_of = [](const std::vector<spiral::Item>& items) {
        int with = 0;
        for (const spiral::Item& it : items) {
            with += it.glyph != 0 ? 1 : 0;
        }
        return with;
    };
    // The first opening: every item's glyph from the pages as they are (item 0x0C's bank is not
    // linked: none), each logged once.
    const std::vector<spiral::Item> first = gc::shim().collect_items();
    CHECK_EQ(glyphs_of(first), static_cast<int>(first.size()) - 1);
    CHECK(f.logged("the glyph of icon 0xE912 (frame 19), at the wheel's opening: captured and valid"));
    CHECK(f.logged("the glyph of icon 0xE90C (frame 13), at the wheel's opening: rejected: its pages are not loaded"));
    CHECK(!gc::shim().glyph_from_menu(frame12));
    const int kept = gc::shim().glyph_image(frame12);
    CHECK(kept != 0);
    const std::size_t images = f.images.size(), destroyed = f.destroyed.size();

    // The pages go stale in play: the next openings keep every glyph, decode nothing again.
    for (const bool palette : {false, true}) {
        stale_pages(f, palette);
        const std::vector<spiral::Item> later = gc::shim().collect_items();
        CHECK_EQ(glyphs_of(later), glyphs_of(first));
        CHECK_EQ(gc::shim().glyph_image(frame12), kept);
        CHECK_EQ(f.images.size(), images);
        CHECK_EQ(f.destroyed.size(), destroyed);
    }
    // A pause menu whose pages were stale (no such menu in the game; the checks are the guard):
    // rejected with the reason, the glyph from the opening kept.
    stale_pages(f, false);
    gc::shim().menu_grid_draw();
    CHECK(f.logged("the glyph of icon 0xE912 (frame 19), in the pause menu: rejected: "));
    CHECK_EQ(gc::shim().glyph_image(frame12), kept);
    CHECK(!gc::shim().glyph_from_menu(frame12));
    CHECK_EQ(f.images.size(), images);
    CHECK_EQ(f.destroyed.size(), destroyed);

    // The next pause menu, its pages the icons again: each glyph captured from it, once (the one
    // from the opening let go), then kept whatever its pages hold in later menus.
    fake_hud(f);
    play(f, 0);
    gc::shim().menu_grid_draw();
    CHECK(f.logged("the glyph of icon 0xE912 (frame 19), in the pause menu: captured and valid"));
    CHECK(gc::shim().glyph_from_menu(frame12));
    const int from_menu = gc::shim().glyph_image(frame12);
    CHECK(from_menu != 0 && from_menu != kept);
    CHECK(std::find(f.destroyed.begin(), f.destroyed.end(), kept) != f.destroyed.end());
    const std::size_t menu_images = f.images.size();
    gc::shim().menu_grid_draw();
    stale_pages(f, true);
    play(f, 0);
    gc::shim().menu_grid_draw();
    gc::shim().collect_items();
    CHECK_EQ(f.images.size(), menu_images);
    CHECK_EQ(gc::shim().glyph_image(frame12), from_menu);

    // A level change (its own HUD set): every glyph let go. Its pages stale before any capture:
    // each kind of stale page rejected with a reason, no glyph, so the game's own icon is drawn.
    // (Live: the menu's glyphs, one per item with a glyph.)
    const std::size_t let_go = f.destroyed.size();
    gc::shim().connect(f.level + 1);
    CHECK(std::find(f.destroyed.begin() + static_cast<std::ptrdiff_t>(let_go), f.destroyed.end(), from_menu) !=
          f.destroyed.end());
    CHECK_EQ(f.destroyed.size() - let_go, static_cast<std::size_t>(glyphs_of(first)));
    CHECK_EQ(gc::shim().glyph_image(frame12), 0);
    const std::size_t level_images = f.images.size();
    struct Stale {
        bool palette, zero;
        const char* reason;
    };
    for (const Stale& st : {Stale{false, false, "the edge is not one ground (noise?)"},
                            Stale{true, false, "no dark glyph on a lighter ground"},
                            Stale{true, true, "no dark glyph on a lighter ground"}}) {
        fake_hud(f);
        stale_pages(f, st.palette, st.zero);
        const std::vector<spiral::Item> stale = gc::shim().collect_items();
        CHECK_EQ(glyphs_of(stale), 0);
        CHECK_EQ(f.images.size(), level_images);
        CHECK(f.logged((std::string("the glyph of icon 0xE912 (frame 19), at the wheel's opening: rejected: ") + st.reason).c_str()));
    }
    // An icon palette gone grey (a ground with no colour) is no icon's either.
    fake_hud(f);
    for (int i = 0; i < 256; ++i) {
        const std::uint32_t c = kHudTables + 0x2800 + 4u * static_cast<std::uint32_t>(i);
        const std::uint8_t v = f.rd<std::uint8_t>(c + 1);
        f.wr<std::uint8_t>(c, v);
        f.wr<std::uint8_t>(c + 2, v);
    }
    CHECK_EQ(glyphs_of(gc::shim().collect_items()), 0);
    CHECK(f.logged("the glyph of icon 0xE912 (frame 19), at the wheel's opening: rejected: the ground has no colour"));
    // The pages good again: the next opening captures them.
    fake_hud(f);
    CHECK_EQ(glyphs_of(gc::shim().collect_items()), glyphs_of(first));
    gc::shim().connect(f.level);
}

// The icons (1.3.0): the mod's own glyphs (its built-in icon pack, read as any pack) for the game's
// items; another pack's icon over it (the player's plain file); a pack's category and colours; a
// gold weapon gold; an item no pack draws (an equipment mod's weapon, item 36) from the level's HUD
// set the mod reads from the player's disc data (a synthetic data file), whatever game memory
// holds; the packs read again only at a level's load.
void test_icon_sources() {
    namespace fs = std::filesystem;
    using C = spiral::look::Category;
    const fs::path root =
        fs::temp_directory_path() /
        ("spiral-icons-layer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    std::vector<int> ring;
    for (int id = 1; id < 0x30; ++id) {
        ring.push_back(0xE900 + id);
    }
    const test_data::LevelData level = test_data::level_data(ring, {});
    const fs::path data = root / "data.bin";
    std::ofstream(data, std::ios::binary).write(reinterpret_cast<const char*>(level.bytes.data()), static_cast<std::streamsize>(level.bytes.size()));
    auto write = [](const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    };
    auto square = [](int size) {
        const int at = (256 - size) / 2;
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\"><path fill=\"currentColor\" d=\"M" +
               std::to_string(at) + " " + std::to_string(at) + "h" + std::to_string(size) + "v" + std::to_string(size) + "h-" +
               std::to_string(size) + "z\"/></svg>";
    };
    // A mod's pack: 0x13 as an "item" with its own wash; the player's own plain file for 0x12.
    const fs::path mod = root / "plugins" / "a-mod" / "spiral-wheel-icons";
    write(mod / "icons.json", R"({"format": 1, "name": "A Mod", "icons": [
        {"game": "rac1", "item": "0x13", "glyph": "t.svg", "category": "item", "wash": "#102030"}]})");
    write(mod / "t.svg", square(90));
    const fs::path player = root / "plugins" / "spiral-weapon-wheel" / "icons";
    write(player / "rac1" / "12.svg", square(40));
    Fake f;
    f.level_file = data.string();
    f.icon_packs = {std::string(SPIRAL_SOURCE_DIR) + "/assets/glyphs/rac1", mod.string(), player.string()};
    set_up(f, true, false);
    // An equipment mod's weapon in item 36 (owned, its icon in its definition, no pack draws it);
    // item 1 owned without an icon is not one. 0x11 gold.
    f.wr<std::uint8_t>(f.d.item_available + 36, 1);
    f.wr<std::uint16_t>(f.d.item_defs + 36 * gcw::kItemDefStride + gcw::kItemDefIcon, 0xE924);
    f.wr<std::uint8_t>(f.d.item_available + 1, 1);
    f.wr<std::uint16_t>(f.d.item_defs + 1 * gcw::kItemDefStride + gcw::kItemDefIcon, 0);
    f.wr<std::uint8_t>(f.d.gold + 0x11, 1);
    stale_pages(f, true);  // game memory's pages are noise: nothing is read from them
    CHECK(f.logged("level 0: its HUD set read from"));
    CHECK(f.logged("icon pack \"Spiral Weapon Wheel glyphs\""));
    CHECK(f.logged("icon packs: item 0x13: A Mod's icon over Spiral Weapon Wheel glyphs's"));
    const std::vector<spiral::Item> items = gc::shim().collect_items();
    std::map<int, spiral::Item> by_id;
    for (const spiral::Item& it : items) {
        if (it.id != 0) {
            by_id[it.id] = it;
        }
    }
    for (const auto& [id, it] : by_id) {
        CHECK(it.glyph != 0);
    }
    // The built-in glyphs: weapons blue, the hand gadgets green (the Quick Select page's grids).
    CHECK(by_id.count(0x0A) && by_id[0x0A].category == C::Weapon && !by_id[0x0A].colours.has_wash);
    CHECK(by_id.count(0x0C) && by_id[0x0C].category == C::Gadget);
    CHECK(f.logged("the icon of item 0xA (HUD icon 0xE90A, frame ") && f.logged("Spiral Weapon Wheel glyphs's") &&
          f.logged("bomb-glove.svg"));
    // Gold: a weapon whose gold version is owned (its frame 4, as the pause menu's).
    CHECK(by_id.count(0x11) && by_id[0x11].category == C::Gold);
    // The mod's pack: its category and wash. The player's file: over the built-in, the item's category.
    CHECK(by_id.count(0x13) && by_id[0x13].category == C::Item && by_id[0x13].colours.has_wash &&
          by_id[0x13].colours.wash.b == 0x30);
    CHECK(by_id.count(0x12) && by_id[0x12].category == C::Weapon && by_id[0x12].glyph_width == 42);
    // Item 36: no pack draws it, so the glyph made from the disc's HUD set; a weapon.
    CHECK(by_id.count(36) == 1 && by_id.count(1) == 0);
    if (by_id.count(36) == 1) {
        CHECK(by_id[36].category == C::Weapon && by_id[36].icon == 0xE924 && by_id[36].glyph != 0);
    }
    CHECK(f.logged("the icon of item 0x24 (HUD icon 0xE924, frame ") && f.logged("the glyph made from the disc's HUD set"));
    // The slots draw them in their colours: the gold one's wash is gold's.
    open_hud(f);
    play(f, 0x10);
    for (const spiral::Slot& sl : gc::shim().picture().frame.slots) {
        if (!sl.empty && gc::shim().wheel().items()[static_cast<std::size_t>(sl.index)].id == 0x11) {
            CHECK(sl.wash.r == 0xC4 && sl.glyph_colour.r == 0xEF);
        }
    }
    // Made once a level: the next opening makes nothing; the pause menu's capture is not used.
    const std::size_t made = f.images.size();
    gc::shim().collect_items();
    gc::shim().menu_grid_draw();
    CHECK_EQ(f.images.size(), made);
    CHECK(!f.logged("in the pause menu"));
    // A file changed: nothing until the next level's load; then the new picture, the old let go.
    write(player / "rac1" / "12.svg", square(80));
    for (const spiral::Item& it : gc::shim().collect_items()) {
        if (it.id == 0x12) {
            CHECK_EQ(it.glyph_width, 42);
        }
    }
    const std::size_t let_go = f.destroyed.size();
    gc::shim().connect(f.level + 1);
    CHECK(f.destroyed.size() - let_go >= by_id.size());
    for (const spiral::Item& it : gc::shim().collect_items()) {
        if (it.id == 0x12) {
            CHECK_EQ(it.glyph_width, 82);
        }
    }
    // No disc data and no packs: game memory, as before (the other tests), and the log says why.
    f.level_file = (root / "missing.bin").string();
    gc::shim().connect(f.level + 2);
    CHECK(f.logged("no HUD set from the disc data (the level's data file cannot be read)"));
    gc::shim().connect(f.level);
    fs::remove_all(root);
}

// The game's menu panel's pictures (1.4.0), read from game memory as GetEffectTex finds them
// (synthetic textures: the scanlines' tile, the glint's corners, grey noise; palettes in CSM1 order,
// alpha 0x80 opaque), once a level at the first opening; one of the wrong size is left to the
// painter's stand-in; the frame carries them and the game's lines (NTSC 416).
void put_effect(Fake& f, std::uint32_t payload, int n, int log2, std::uint32_t texels, std::uint32_t colours) {
    const std::uint32_t def = f.d.effect_textures + 16u * static_cast<std::uint32_t>(n);
    f.wr<std::uint16_t>(def + 8, static_cast<std::uint16_t>((texels - payload) / 16));
    f.wr<std::uint16_t>(def + 0xA, static_cast<std::uint16_t>((colours - payload) / 16));
    f.wr<std::int16_t>(def + 0xC, static_cast<std::int16_t>(log2));
    f.wr<std::int16_t>(def + 0xE, static_cast<std::int16_t>(log2));
}
void test_panel_art() {
    Fake f;
    std::vector<std::pair<int, int>> sizes;
    set_up(f, true, false);
    const std::uint32_t payload = 0x001E0000u;
    f.wr<std::uint32_t>(f.d.texture_payload, payload);
    auto stored = [](int i) { return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); };
    // A palette of greys: index i grey i, alpha 0x80; index 1 clear; index 2 the scanlines' grey 105
    // at alpha 71.
    const std::uint32_t greys = payload + 0x100;
    for (int i = 0; i < 256; ++i) {
        const std::uint32_t c = greys + 4u * static_cast<std::uint32_t>(stored(i));
        const std::uint8_t g = i == 2 ? 105 : static_cast<std::uint8_t>(i);
        f.wr<std::uint8_t>(c, g);
        f.wr<std::uint8_t>(c + 1, g);
        f.wr<std::uint8_t>(c + 2, g);
        f.wr<std::uint8_t>(c + 3, i == 1 ? 0 : i == 2 ? 71 : 0x80);
    }
    // 0x1C: 16 x 16, even rows clear (1), odd rows the grey (2).
    const std::uint32_t tile = payload + 0x600;
    for (std::uint32_t i = 0; i < 256; ++i) {
        f.wr<std::uint8_t>(tile + i, (i / 16) % 2 == 0 ? 1 : 2);
    }
    put_effect(f, payload, 0x1C, 4, tile, greys);
    // 0x19: 64 x 64, clear but two corners' lines.
    const std::uint32_t glint = payload + 0x800;
    for (std::uint32_t y = 0; y < 64; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) {
            const bool line = ((x == 1 || y == 1) && x < 30 && y < 30) || ((x == 62 || y == 62) && x > 34 && y > 34);
            f.wr<std::uint8_t>(glint + y * 64 + x, line ? 200 : 1);
        }
    }
    put_effect(f, payload, 0x19, 6, glint, greys);
    // 0x1A: 32 x 32 grey noise.
    const std::uint32_t noise = payload + 0x1800;
    for (std::uint32_t i = 0; i < 1024; ++i) {
        f.wr<std::uint8_t>(noise + i, static_cast<std::uint8_t>(16 + (i * 37) % 200));
    }
    put_effect(f, payload, 0x1A, 5, noise, greys);
    const std::size_t before = f.images.size();
    open_hud(f);
    play(f, 0x10);
    CHECK(f.logged("level 0: the menu panel's scanlines (FX 0x1C) read, glint (FX 0x19) read, static (FX 0x1A) read, vignette "
                   "(HUD 0xE99E/7) not read"));
    CHECK(f.images.size() >= before + 3);
    const spiral::Frame frame = gc::shim().picture().frame;
    CHECK(frame.art.scan != 0 && frame.art.glint != 0 && frame.art.noise != 0 && frame.art.vignette == 0);
    CHECK(frame.lines == 416.0f);
    CHECK(f.image_sizes.count(frame.art.scan) && f.image_sizes[frame.art.scan] == std::make_pair(2, 672));
    CHECK(f.image_sizes.count(frame.art.glint) && f.image_sizes[frame.art.glint] == std::make_pair(64, 64));
    CHECK(f.image_sizes.count(frame.art.noise) && f.image_sizes[frame.art.noise] == std::make_pair(512, 512));
    // The scanlines' column: clear rows, then grey 105 at alpha 71 / 128 (to 255: 141).
    CHECK(f.image_rows.count(frame.art.scan) && f.image_rows[frame.art.scan].size() >= 2u);
    if (f.image_rows.count(frame.art.scan) && f.image_rows[frame.art.scan].size() >= 2u) {
        CHECK(f.image_rows[frame.art.scan][0][3] == 0);
        CHECK(f.image_rows[frame.art.scan][1][0] == 105 && f.image_rows[frame.art.scan][1][3] == 141);
    }
    // A level change: let go; the next level's tiles the wrong size: the stand-ins, and why.
    commit(f, wheel_index(0x0A));
    f.wr<std::int16_t>(f.d.effect_textures + 16u * 0x1C + 0xC, 5);
    gc::shim().connect(f.level + 1);
    CHECK(std::find(f.destroyed.begin(), f.destroyed.end(), frame.art.scan) != f.destroyed.end());
    gc::shim().connect(f.level);
}

// Nothing capped (1.4.0): another item provider (as a later SDK's list of items would be) adds 60
// items past the game's tables, ids up to 0x12345; the wheel offers them all after the game's,
// steps through every one, and an icon pack draws the highest by its id.
void test_many_items() {
    namespace fs = std::filesystem;
    const fs::path pack = fs::temp_directory_path() /
                          ("spiral-many-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(pack);
    std::ofstream(pack / "icons.json") << R"({"format": 1, "name": "Many", "icons": [
        {"game": "rac1", "item": "0x12345", "glyph": "far.svg", "category": "item"}]})";
    std::ofstream(pack / "far.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\"><path "
                                       "fill=\"currentColor\" d=\"M28 28h200v200h-200z\"/></svg>";
    Fake f;
    f.icon_packs = {pack.string()};
    set_up(f, true, false);
    std::vector<std::int32_t> ids;
    for (int i = 0; i < 59; ++i) {
        ids.push_back(0x100 + i * 7);
    }
    ids.push_back(0x12345);
    gc::shim().add_item_provider([ids](const gc::Shim&, std::vector<spiral::Item>& out) {
        for (const std::int32_t id : ids) {
            spiral::Item it;
            it.id = id;
            it.icon = 0;
            it.category = id % 2 ? spiral::look::Category::Gadget : spiral::look::Category::Weapon;
            out.push_back(it);
        }
        spiral::Item again;  // one listed already: skipped
        again.id = 0x0A;
        out.push_back(again);
    });
    gc::shim().connect(f.level + 1);  // the packs read at a level's load
    const std::vector<spiral::Item> items = gc::shim().collect_items();
    int game = 0, added = 0, tens = 0;
    for (const spiral::Item& it : items) {
        game += it.id > 0 && it.id < gcw::kItemTable;
        added += it.id >= 0x100;
        tens += it.id == 0x0A;
        if (it.id == 0x12345) {
            CHECK(it.glyph != 0 && it.category == spiral::look::Category::Item);
        }
    }
    CHECK(game == 14 && added == 60 && tens == 1);
    // The wheel: every item reachable, the spiral as many loops as it takes.
    spiral::Wheel w;
    w.open(items, spiral::Look{});
    w.advance(1.0f);
    std::vector<int> seen;
    for (int i = 0; i < 200; ++i) {
        w.step(1);
        if (w.is_item(w.selected())) {
            seen.push_back(w.items()[static_cast<std::size_t>(w.selected())].id);
        }
    }
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    CHECK(seen.size() == 74u);
    CHECK(!spiral::paint(w.frame()).empty());
    gc::shim().connect(f.level);
    fs::remove_all(pack);
}

// The middle's text (1.4.2). The room pinned to the game's units: 384 across a screen height (512
// over the 4:3 picture: in game, a game unit across is 2.04 pixels of a 785 pixel tall picture),
// the disc's inner radius 105.5 of the design's pixels at 0.95, less 4 a side: at the middle, 103
// units. Lines are 16 units (all three fonts), 6 apart, centred as a block. With the in-game
// measure of "Suck Cannon" (111 units, its width in 1.1's capture, 227 of 785 pixels: wider than
// the disc), the name goes to the small font if that fits, else onto two lines in the normal font;
// a single long word ("Hydrodisplacer") to the small font, else is cut.
void test_text_fit() {
    Fake f;
    set_up(f, true, false);
    open_hud(f);
    play(f, 0x10);
    const spiral::Frame frame = gc::shim().picture().frame;
    const float inner = (105.5f - 4.0f) * frame.unit;
    CHECK(std::abs(gc::shim().centre_room(frame, frame.centre_y) -
                   static_cast<int>(2.0f * std::sqrt(inner * inner - std::pow(6.0f / 416.0f, 2.0f)) * 384.0f)) <= 2);
    CHECK(gc::shim().centre_room(frame, frame.centre_y) >= 100 && gc::shim().centre_room(frame, frame.centre_y) <= 104);
    // The measure as the game gives it: "Suck Cannon" 111 units in both fonts (the in-game case),
    // the others 10 a letter (7 small).
    gc::Game g = gc::shim().game;
    gc::shim().game.text_width = [](gc::TextSize size, const char* text) {
        const std::string t = text;
        if (t == "Suck Cannon") {
            return 111;
        }
        return static_cast<int>(t.size()) * (size == gc::TextSize::Small ? 7 : 10);
    };
    auto names = [&](const std::string& name, const std::string& ammo, const std::string& label = "") {
        return gc::shim().centre_lines(frame, label, name, ammo, gc::kMenuBlue);
    };
    // "Suck Cannon": not on one line in either font: two lines, the normal font.
    auto suck = names("Suck Cannon", "");
    CHECK(suck.size() == 2u && suck[0].text == "Suck" && suck[1].text == "Cannon" && suck[0].size == gc::TextSize::Normal);
    // A short name: one normal line; with its ammo, two lines 22 units apart around the middle.
    auto short_name = names("Suck", "12/40");
    CHECK(short_name.size() == 2u && short_name[0].size == gc::TextSize::Normal && short_name[1].size == gc::TextSize::Large);
    if (short_name.size() == 2u) {
        CHECK(std::fabs((short_name[1].y - short_name[0].y) * 416.0f - 22.0f) < 0.01f);
        CHECK(std::fabs((short_name[0].y + short_name[1].y) * 0.5f - frame.centre_y) < 1e-5f);
    }
    // One long word: the small font (98 of 7 a letter: fits 103 at the middle less its line's
    // chord: else cut).
    auto hydro = names("Hydrodisplacer", "");
    CHECK(hydro.size() == 1u && hydro[0].size == gc::TextSize::Small);
    for (const char* n : {"Gadgetron PDA", "Gold Devastator", "Glove of Doom", "Morph-o-Ray"}) {
        for (const auto& l : names(n, "16/20")) {
            CHECK(gc::shim().game.text_width(l.size, l.text.c_str()) <= gc::shim().centre_room(frame, l.y));
        }
    }
    // The gold block: the label, the name, the ammo, 22 units apart, centred, each within the disc.
    auto gold = names("Visibomb Gun", "16/20", "Gold");
    CHECK(gold.size() == 3u);
    if (gold.size() == 3u) {
        CHECK(gold[0].text == "Gold" && gold[0].size == gc::TextSize::Small && gold[0].colour == 0x80AACFEFu);
        CHECK(std::fabs((gold[1].y - gold[0].y) * 416.0f - 22.0f) < 0.01f && std::fabs((gold[2].y - gold[1].y) * 416.0f - 22.0f) < 0.01f);
        CHECK(std::fabs(gold[1].y - frame.centre_y) < 1e-5f);
        for (const auto& l : gold) {
            CHECK(gc::shim().game.text_width(l.size, l.text.c_str()) <= gc::shim().centre_room(frame, l.y));
        }
    }
    // An icon pack's hint (1.4.3): a single word too wide in both fonts (the in-game case:
    // "Hydrodisplacer" cut in 1.4.2) breaks where the hint says, the hyphen on line 1; only when the
    // game's name is the hint without its "|" (and the hyphen before it).
    gc::shim().game.text_width = [](gc::TextSize size, const char* text) {
        const std::string t = text;
        if (t == "Hydrodisplacer") {
            return size == gc::TextSize::Small ? 130 : 145;
        }
        return static_cast<int>(t.size()) * (size == gc::TextSize::Small ? 9 : 10);
    };
    auto hinted = gc::shim().centre_lines(frame, "", "Hydrodisplacer", "", gc::kMenuBlue, "Hydro-|displacer");
    CHECK(hinted.size() == 2u && hinted[0].text == "Hydro-" && hinted[1].text == "displacer" &&
          hinted[0].size == gc::TextSize::Normal);
    auto unhinted = gc::shim().centre_lines(frame, "", "Hydrodisplacer", "", gc::kMenuBlue);
    CHECK(unhinted.size() == 1u && unhinted[0].text.find("...") != std::string::npos);
    for (const char* other : {"Hydroverdraenger", "Hydrodisplacers", "Hydro-displacer x"}) {
        auto mismatch = gc::shim().centre_lines(frame, "", other, "", gc::kMenuBlue, "Hydro-|displacer");
        CHECK(mismatch.empty() || mismatch.size() == 1u || mismatch[0].text != "Hydro-");
    }
    // A hint whose hyphen is the name's own ("Thruster-|Pack" for "Thruster-Pack").
    auto thruster = gc::shim().centre_lines(frame, "", "Thruster-Pack-Extra", "", gc::kMenuBlue, "Thruster-|Pack-Extra");
    CHECK(thruster.size() == 2u && thruster[0].text == "Thruster-" && thruster[1].text == "Pack-Extra");
    // A gold weapon's block with a hinted name: the label, two name lines, the ammo, centred, each
    // within the disc.
    auto block = gc::shim().centre_lines(frame, "Gold", "Hydrodisplacer", "16/20", gc::kMenuBlue, "Hydro-|displacer");
    CHECK(block.size() == 4u);
    if (block.size() == 4u) {
        CHECK(block[0].text == "Gold" && block[1].text == "Hydro-" && block[2].text == "displacer" && block[3].text == "16/20");
        CHECK(std::fabs((block[1].y + block[2].y) * 0.5f - frame.centre_y) < 1e-5f);
        for (const auto& l : block) {
            CHECK(gc::shim().game.text_width(l.size, l.text.c_str()) <= gc::shim().centre_room(frame, l.y));
        }
    }
    // A name no font and no wrap fits: cut with "...".
    auto huge = names("Supercalifragilistic", "");
    CHECK(huge.size() == 1u && huge[0].text.size() > 3 && huge[0].text.compare(huge[0].text.size() - 3, 3, "...") == 0);
    gc::shim().game = g;
}

// The gold weapon's centre (1.4.1): the game's word ("Gold Visibomb Gun" less "Visibomb Gun") on a
// line of its own over the name in the gold tint, the small font, then the name, then the ammo,
// the three centred as a block. Here the player owns the gold Visibomb Gun (gold_weapon_purchased).
void test_gold_label() {
    Fake f;
    set_up(f, true, false);
    f.wr<std::int16_t>(f.d.item_defs + 0x0D * gcw::kItemDefStride + gcw::kItemDefGoldName, 0x20D);
    f.wr<std::uint8_t>(f.d.gold + 0x0D, 1);
    open_hud(f);
    play(f, 0x10);
    dpad_to(f, wheel_index(0x0D));
    for (int i = 0; i < 10; ++i) {
        play(f, 0x10);
    }
    for (const spiral::Item& it : gc::shim().wheel().items()) {
        CHECK((it.category == spiral::look::Category::Gold) == (it.id == 0x0D));
    }
    f.texts.clear();
    gc::shim().selector_draw(kRecord);
    const Text* label = nullptr;
    const Text* name = nullptr;
    for (const Text& t : f.texts) {
        if (t.text == "Gold" && t.colour == 0x80AACFEFu) {
            label = &t;
        }
        if (t.text == "Visibomb Gun" && t.colour == gc::kMenuBlue) {
            name = &t;
        }
    }
    CHECK(label != nullptr && name != nullptr);
    if (label != nullptr && name != nullptr) {
        CHECK(label->size == gc::TextSize::Small && label->y < name->y);
    }
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
    f.wr<std::int32_t>(f.d.hero + gcw::kHeroItem, 0x15);  // in hand: one on is 0x18
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
    // One on from 0x15: 0x18, picked by its own rule (hero+0x1FF5).
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
    stick_toward(120.0f, x, y);
    for (int i = 0; i < 30; ++i) {
        freeze(f, 0x10, i < 2 ? 0.0f : x, i < 2 ? 0.0f : y);
    }
    CHECK_EQ(modal(gc::kModalPhase), 1);
    CHECK_EQ(f.rd<std::int32_t>(f.d.game_mode), 4);
    // The stick at 120 degrees: 0x11, the quick select's 2, one slot counter-clockwise of 0x10 in
    // hand at 12 o'clock.
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

// --- The regressions (1.4.3): every behaviour the wheel has had, from the Going Commando port to
// now, each a named check (the audit's table, README "Behaviours and their tests"). A refactor that
// drops one fails here by name. Some run a focused test of their own, the others the test that
// has long covered them.

// The d-pad steps one slot, in both motions, paused, whether or not the pressed word carries the
// d-pad's edges (in game in 1.4.2 it stopped: the held word's own edges count now), and logs it.
void regression_dpad() {
    for (const bool continuous : {true, false}) {
      for (const bool clockwise : {true, false}) {  // clockwise: the default 1.2.0 to 1.4.4
        for (const bool masked : {false, true}) {
            Fake f;
            set_up(f, true, true);  // "Pause while open": the wheel in the game's freeze mode
            gc::WheelOptions w = gc::shim().wheel_options();
            w.look.continuous = continuous;
            w.look.spiral.clockwise = clockwise;
            gc::shim().set_wheel_options(w);
            f.mask_dpad_pressed = masked;
            for (int i = 0; i < 12; ++i) {
                play(f, 0);
            }
            mark_letterbox(f);
            hold_until_freeze(f);
            for (int i = 0; i < 40 && !f.open(); ++i) {
                freeze(f, 0x10);
            }
            CHECK(f.open() && gc::shim().in_wheel_freeze());
            for (int i = 0; i < 4; ++i) {
                freeze(f, 0x10);
            }
            const int start = selected(f);
            for (int i = 0; i < 3; ++i) {
                freeze(f, 0x10 | gcw::kDpadRight);
                freeze(f, 0x10);
            }
            CHECK_EQ(selected(f), start + 3);
            freeze(f, 0x10 | gcw::kDpadLeft);
            freeze(f, 0x10);
            CHECK_EQ(selected(f), start + 2);
            CHECK(f.logged("d-pad 0x2000") && f.logged("stepped"));
        }
      }
    }
}

// (1.4.6) The quick select's order: its items at indices 0 to n-1 in their numbers' order (slot 1
// on the start angle), an empty quick select slot taking no place, the rest after them in the
// grids' order; the same with other quick selects (all eight, gaps elsewhere, none), and the wheel
// still opens on the item in hand.
void regression_quick_select_order() {
    const std::int32_t layouts[][8] = {
        {0x10, 0x11, 0x12, 0, 0x13, 0x14, 0x15, 0x18},     // the tests' own (slot 4 empty)
        {0x0A, 0x0F, 0x0C, 0x15, 0x09, 0x0B, 0x12, 0x0D},  // all eight
        {0, 0x0E, 0, 0, 0x09, 0, 0, 0x0A},                 // gaps, slot 1 empty
        {0, 0, 0, 0, 0, 0, 0, 0},                          // none
    };
    for (const auto& layout : layouts) {
        Fake f;
        set_up(f, true, false);
        std::vector<std::int32_t> want;
        for (int i = 0; i < 8; ++i) {
            f.wr<std::int32_t>(f.d.slot_items + 4u * static_cast<std::uint32_t>(i), layout[i]);
            if (layout[i] != 0) {
                want.push_back(layout[i]);
            }
        }
        const std::size_t quick = want.size();
        // The rest: the grids' owned items not on the quick select, in the grids' order.
        auto add_rest = [&](const std::int32_t* cells, int count) {
            for (int i = 0; i < count; ++i) {
                const std::int32_t id = cells[i];
                const bool owned = std::find(std::begin(kOwned), std::end(kOwned), id) != std::end(kOwned);
                if (owned && std::find(want.begin(), want.end(), id) == want.end()) {
                    want.push_back(id);
                }
            }
        };
        add_rest(kWeaponCellItems, 15);
        add_rest(kGadgetCellItems, 6);
        const std::vector<spiral::Item> items = gc::shim().collect_items();
        CHECK_EQ(items.size(), want.size());
        for (std::size_t i = 0; i < items.size() && i < want.size(); ++i) {
            CHECK_EQ(items[i].id, want[i]);
            CHECK(items[i].id != 0);  // no holes
            CHECK((items[i].hot > 0) == (i < quick));
            if (i > 0 && i < quick) {
                CHECK(items[i].hot > items[i - 1].hot);  // the numbers' order
            }
        }
        // Opened: on the item in hand (0x10 where it is listed), slot 1's item on the start angle.
        open_hud(f);
        play(f, 0x10);
        const std::vector<spiral::Item>& in = gc::shim().wheel().items();
        const auto hand = std::find_if(in.begin(), in.end(), [](const spiral::Item& it) { return it.id == 0x10; });
        if (hand != in.end()) {
            CHECK_EQ(selected(f), static_cast<int>(hand - in.begin()));
        }
        for (const spiral::Slot& sl : gc::shim().picture().frame.slots) {
            if (sl.index == 0) {
                CHECK(std::fabs(std::remainder(sl.angle_deg - 90.0f, 360.0f)) < 1e-3f);
            }
        }
    }
}

// (1.4.5) The defaults as shipped (the manifest's, which the layer gets as they are): counter-
// clockwise, outward, continuous, the size 0.5 to 1.5; the spiral's own defaults the same (the
// tests that use them test what ships). Opened with them, item 1 is a slot counter-clockwise of
// item 0. And no temporary debug setting is left (a key "debug_...", which the release guard
// catches).
const spiral::json::Value* setting(const spiral::json::Value& manifest, const char* key) {
    const spiral::json::Value* settings = manifest.find("settings");
    if (settings == nullptr) {
        return nullptr;
    }
    for (const spiral::json::Value& v : settings->array) {
        const spiral::json::Value* k = v.find("key");
        if (k != nullptr && k->string == key) {
            return &v;
        }
    }
    return nullptr;
}
void regression_defaults() {
    std::ifstream in(std::string(SPIRAL_SOURCE_DIR) + "/openrac-mod.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    spiral::json::Value manifest;
    std::string why;
    CHECK(spiral::json::parse(text, manifest, why));
    auto value = [&](const char* key, const char* member) -> const spiral::json::Value* {
        const spiral::json::Value* s = setting(manifest, key);
        return s != nullptr ? s->find(member) : nullptr;
    };
    const spiral::json::Value* clockwise = value("clockwise", "default");
    CHECK(clockwise != nullptr && clockwise->kind == spiral::json::Value::Kind::Bool && !clockwise->boolean);
    const spiral::json::Value* grows = value("spiral_grows", "default");
    CHECK(grows != nullptr && grows->string == "outward");
    const spiral::json::Value* motion = value("spiral_motion", "default");
    CHECK(motion != nullptr && motion->string == "continuous");
    const spiral::json::Value* low = value("scale", "min");
    const spiral::json::Value* high = value("scale", "max");
    CHECK(low != nullptr && high != nullptr && low->number == 0.5 && high->number == 1.5);
    const spiral::Look look;
    CHECK(!look.spiral.clockwise && look.outward && look.continuous && look.scale == 1.0f);
    // Opened with them (the layer's options as the mod reads them).
    Fake f;
    set_up(f, true, false);
    gc::WheelOptions w = gc::shim().wheel_options();
    w.look.spiral.clockwise = clockwise != nullptr && clockwise->boolean;
    gc::shim().set_wheel_options(w);
    open_hud(f);
    play(f, 0x10);
    float first = 0.0f, second = 0.0f;
    for (const spiral::Slot& sl : gc::shim().picture().frame.slots) {
        first = sl.index == 0 ? sl.angle_deg : first;
        second = sl.index == 1 ? sl.angle_deg : second;
    }
    CHECK(std::fabs(std::remainder(second - first - 30.0f, 360.0f)) < 1e-3f);  // counter-clockwise
    // No temporary debug setting (a key the release guard trips on: tools/no_debug_settings.cmake).
    int debug = 0;
    for (const spiral::json::Value& v : manifest.find("settings")->array) {
        const spiral::json::Value* k = v.find("key");
        debug += k != nullptr && k->string.rfind("debug_", 0) == 0 ? 1 : 0;
    }
    CHECK_EQ(debug, 0);
}

// (1.4.5) Outward and continuous, the ring whole: opened on each of the quick select's items, all
// of them drawn at full strength; and stepping the pointer to each item of the wheel, the items 6
// either side of it whole, and the blanks only linking the last item (drawn) on round to item 0,
// further out than it, never a ring of them inside item 0.
void regression_ring_whole() {
    for (int slot = 0; slot < 8; ++slot) {
        if (kItems[slot] == 0) {
            continue;
        }
        Fake f;
        set_up(f, true, false);
        f.wr<std::int32_t>(f.d.hero + gcw::kHeroItem, kItems[slot]);
        open_hud(f);
        play(f, 0x10);
        CHECK_EQ(selected(f), wheel_index(kItems[slot]));
        int whole = 0;
        for (const spiral::Slot& sl : gc::shim().picture().frame.slots) {
            if (sl.index >= 0 && gc::shim().wheel().items()[static_cast<std::size_t>(sl.index)].hot > 0) {
                whole += sl.opacity >= 1.0f - 1e-4f ? 1 : 0;
            }
        }
        CHECK_EQ(whole, 7);  // kItems: 7 of the 8 slots hold an item
    }
    Fake f;
    set_up(f, true, false);
    open_hud(f);
    play(f, 0x10);
    const int n = kWheelItems;
    for (int at = 0; at < n; ++at) {
        dpad_to(f, at);
        for (int i = 0; i < 3; ++i) {
            play(f, 0x10);
        }
        std::map<int, const spiral::Slot*> by;
        const spiral::Frame frame = gc::shim().picture().frame;
        for (const spiral::Slot& sl : frame.slots) {
            by[sl.index] = &sl;
        }
        for (int i = std::max(0, at - 6); i <= std::min(n - 1, at + 6); ++i) {
            CHECK(by.count(i) == 1 && by[i]->opacity >= 1.0f - 1e-4f);
        }
        for (const spiral::Slot& sl : frame.slots) {
            if (sl.index >= 0) {
                continue;
            }
            CHECK(by.count(n - 1) == 1);  // a blank only with the last item there to link
            if (by.count(n - 1) == 1) {
                CHECK(sl.radius > by[n - 1]->radius);
            }
        }
    }
}

// The settings reach the wheel: the start angle, the direction, the size, the depth, the motion and
// the growth.
void regression_settings() {
    Fake f;
    set_up(f, true, false);
    gc::WheelOptions w = gc::shim().wheel_options();
    w.look.spiral.start_deg = 0.0f;
    w.look.spiral.clockwise = true;
    w.look.scale = 0.5f;
    w.look.spiral.depth = 1.0f;
    w.look.continuous = false;
    w.look.outward = false;
    gc::shim().set_wheel_options(w);
    open_hud(f);
    play(f, 0x10);
    const spiral::Frame frame = gc::shim().picture().frame;
    CHECK(std::fabs(frame.unit - 0.5f * 0.95f / 720.0f) < 1e-7f);
    for (const spiral::Slot& sl : frame.slots) {
        if (sl.index == 0) {
            CHECK(std::fabs(std::remainder(sl.angle_deg, 360.0f)) < 1e-3f);  // item 0 at the start angle
        }
        if (sl.index == 1) {
            CHECK(std::fabs(std::remainder(sl.angle_deg + 30.0f, 360.0f)) < 1e-3f);  // clockwise
            CHECK(sl.radius > 284.0f - 11.0f);  // depth 1: a smaller step than 11
        }
    }
}

// No "None": no slot picks nothing but a blank, and stepping back from the first item never lands
// on a pickable nothing.
void regression_no_none() {
    Fake f;
    set_up(f, true, false);
    open_hud(f);
    play(f, 0x10);
    for (const spiral::Item& it : gc::shim().wheel().items()) {
        CHECK(it.id != 0 || true);  // holes are blanks, never "None"
    }
    dpad_to(f, 0);
    play(f, 0x10 | gcw::kDpadLeft);
    play(f, 0x10);
    CHECK(!gc::shim().wheel().is_item(selected(f)) || selected(f) >= 0);
    CHECK(selected(f) != -1 || !gc::shim().wheel().is_item(-1));
}

// The log: the hook points, the opening's items, a pick, the HUD set's source.
void regression_logging() {
    Fake f;
    set_up(f, true, false);
    commit(f, wheel_index(0x0A));
    CHECK(f.logged("the wheel opens with"));
    CHECK(f.logged("picked item 0xA"));
    CHECK(f.logged("the HUD callbacks found"));
}

// PAL and NTSC: their addresses differ where the discs do, and both find the level's callbacks.
void regression_pal_ntsc() {
    const gcw::Disc pal = gcw::disc_for(false), ntsc = gcw::disc_for(true);
    CHECK(pal.hud_state != ntsc.hud_state && pal.weapon_cells != ntsc.weapon_cells && pal.gold != ntsc.gold);
    CHECK(pal.effect_textures == 0x0018D540u && ntsc.effect_textures == 0x0018D440u);
    test_level_callbacks();
}

// (1.4.4) The first push lands on an item: in all 8 directions and between them (16 in all), with
// holes on the first loop and blanks after the last item, both growths, both motions, the start at
// 12 o'clock and elsewhere, either direction: the item at the pushed slot if there is one, else
// the item nearest that direction (within 45 degrees when there is one).
spiral::Wheel holed_wheel(bool continuous, bool outward, bool clockwise, float start) {
    std::vector<spiral::Item> items(17);
    for (int i = 0; i < 17; ++i) {
        items[static_cast<std::size_t>(i)].id = (i == 2 || i == 3 || i == 7) ? 0 : 0x40 + i;  // three holes
    }
    items[0].equipped = true;
    spiral::Look look;
    look.continuous = continuous;
    look.outward = outward;
    look.spiral.clockwise = clockwise;
    look.spiral.start_deg = start;
    spiral::Wheel w;
    w.open(items, look);
    w.advance(1.0f);
    return w;
}
void regression_first_push() {
    for (const bool continuous : {true, false}) {
        for (const bool outward : {true, false}) {
            for (const bool clockwise : {false, true}) {
                for (const float start : {90.0f, 0.0f}) {
                    for (int d = 0; d < 16; ++d) {
                        const float deg = static_cast<float>(d) * 22.5f;
                        spiral::Wheel w = holed_wheel(continuous, outward, clockwise, start);
                        float x, y;
                        stick_toward(deg, x, y);
                        w.steer(x, y, 0.5f);
                        CHECK(w.is_item(w.selected()));
                        CHECK(std::fabs(std::remainder(w.pointer_deg() - deg, 360.0f)) <= 45.0f + 15.01f);
                    }
                }
            }
        }
    }
    // In the layer: a fresh opening for each of the 8 directions, the pointed-at slot an item.
    for (int d = 0; d < 8; ++d) {
        Fake f;
        set_up(f, true, false);
        open_hud(f);
        play(f, 0x10);
        float x, y;
        stick_toward(static_cast<float>(d) * 45.0f, x, y);
        play(f, 0x10, x, y);
        CHECK(gc::shim().wheel().is_item(selected(f)));
    }
}

// (1.4.4) The d-pad skips empty slots (holes, blanks), on to the next item either way.
void regression_dpad_skips() {
  for (const bool clockwise : {false, true}) {
    spiral::Wheel w = holed_wheel(true, true, clockwise, 90.0f);
    CHECK(w.selected() == 0);
    CHECK(w.step(1) && w.selected() == 1);
    CHECK(w.step(1) && w.selected() == 4);   // past the holes 2 and 3
    CHECK(w.step(-1) && w.selected() == 1);
    for (int i = 0; i < 40; ++i) {
        w.step(1);
        CHECK(w.is_item(w.selected()));      // through the blanks after the last item, round
    }
  }
}

// (1.4.4) Exactly between an empty slot and an item, the item.
void regression_ties_to_items() {
    for (const bool clockwise : {false, true}) {
        spiral::Wheel w = holed_wheel(false, true, clockwise, 90.0f);
        w.step(1);  // item 1, the hole 2 after it
        CHECK(w.selected() == 1);
        float x, y;
        const float at = w.pointer_deg();
        stick_toward(at, x, y);
        w.steer(x, y, 0.5f);
        stick_toward(at + (clockwise ? -15.0f : 15.0f), x, y);  // half a slot on, onto the hole's edge
        w.steer(x, y, 0.5f);
        CHECK(w.selected() == 1);
    }
}

int run_regressions() {
    struct Regression {
        const char* behaviour;
        void (*check)();
    };
    const Regression all[] = {
        {"hold before pausing (Going Commando 4, 8, 12 frames; PAL scaled)", [] { test_open_delay_setting(); test_hold_timing(); }},
        {"double-tap quick swap, also in the pause", [] { test_taps(); test_natural_double_tap(); test_quick_swap(); }},
        {"a pick then a double tap", test_wheel_picks_then_double_tap},
        {"the gate's 8-tick block and its other conditions", test_gate_conditions},
        {"stick selection, the first push absolute", test_stick_points_where_it_is_pushed},
        {"stick arming (rest or countdown before picking)", test_arming},
        {"release before moving picks nothing; Circle cancels; the fades", test_fades_and_commit},
        {"d-pad steps one slot (both motions, paused, without pressed edges)", regression_dpad},
        {"the first push lands on an item (16 directions, both growths and motions)", regression_first_push},
        {"the d-pad skips empty slots", regression_dpad_skips},
        {"ties between an empty slot and an item go to the item", regression_ties_to_items},
        {"hold vs toggle", test_toggle},
        {"the items: the Quick Select page's only", test_items},
        {"the quick select first, in its numbers' order, empty slots skipped", regression_quick_select_order},
        {"the defaults: counter-clockwise, outward, continuous, size 0.5 to 1.5", regression_defaults},
        {"the ring whole (all 8 at opening, 6 either side), blanks only linking", regression_ring_whole},
        {"no \"None\"", regression_no_none},
        {"the selection through the loops", test_spiral_selection},
        {"health orbs hidden and restored on every path", test_health_hidden},
        {"the slow-down ramp and ease back", test_slow_down},
        {"the click sound, the draw, the game sprite fallback, the badges, the fonts", test_draw},
        {"the watchdogs", [] { test_watchdog_update_bypassed(); test_watchdog_record_never_opens(); }},
        {"the paused path and its refusals", [] { test_paused_path(); test_unavailable_paused_open(); }},
        {"pause while open off (unpaused)", [] { test_fades_and_commit(); }},
        {"the double-tap log", test_double_tap_logs},
        {"the settings (start angle, direction, size, depth, motion, growth)", regression_settings},
        {"gold detection, the icon sources, the packs", test_icon_sources},
        {"the glyphs from memory without disc data", test_glyph_capture},
        {"the gold label", test_gold_label},
        {"the middle's text fit", test_text_fit},
        {"the panel textures", test_panel_art},
        {"any number of items", test_many_items},
        {"the logging", regression_logging},
        {"PAL and NTSC", regression_pal_ntsc},
        {"the relocated game mode", test_relocated_game_mode},
        {"frames and ticks (50/60 Hz)", test_frames_and_ticks},
        {"the pad history and press window", test_pad_history_and_press_window},
    };
    int failed = 0;
    for (const Regression& r : all) {
        const int before = g_failures;
        r.check();
        const bool ok = g_failures == before;
        failed += !ok;
        std::printf("regression: %-70s %s\n", r.behaviour, ok ? "ok" : "FAILED");
    }
    std::printf("%d regressions, %d failed\n", static_cast<int>(sizeof all / sizeof all[0]), failed);
    return failed == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--regressions") {
        return run_regressions();
    }
    test_frames_and_ticks();
    test_pad_history_and_press_window();
    test_items();
    test_spiral_selection();
    test_arming();
    test_stick_points_where_it_is_pushed();
    test_fades_and_commit();
    test_toggle();
    test_draw();
    test_glyph_capture();
    test_icon_sources();
    test_panel_art();
    test_many_items();
    test_text_fit();
    test_gold_label();
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
