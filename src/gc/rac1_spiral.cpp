// SPDX-License-Identifier: GPL-3.0-only
//
// The spiral wheel in the translation layer: the quick select record's update and draw, which
// Going Commando's weapon_wheel_update_select and weapon_wheel_draw were in the mod this one grew
// from (Going Commando weapon wheel). The record's frame is theirs (rac2-decomp at 4ea93cea,
// src/game/0028BE00-g8.c and 0028BDB0-wheel-g8.c, GPL-3.0-only, hence this file's license): the
// HUD-open request and mode 7's timer kept while the wheel is up, the record's timeout and slide,
// the open fade (fade[0], 0 to 8) and the selection fade (fade[1]), the commit as Triangle is let go
// once fade[0] has run down (the pending item, item 0x18's rule, the usage statistics, the ammo
// held to the most), mode 7 moved on or the request cleared, the record closed. What it selects
// and draws is the spiral of "Prey Menu" (src/spiral/: the stick turns the pointer around the
// loops; the art the mod's overlay draws under the HUD, the icons and text the game's own HUD
// calls draw here).
//
// The mod's own, marked in the comments: the items (every owned hand item, not the quick select's
// eight), "None" as the wrench, the stick's rest, the d-pad stepping one slot, Circle cancelling,
// the toggle mode, and the slow-down before the pause.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "rac1_shim.hpp"
#include "../spiral/arrange.hpp"

namespace gc_weapon_wheel::gc {

namespace {

// The record (Going Commando's Selector, Ratchet & Clank's 0x90-byte HUD record).
constexpr std::uint32_t kHandle = 0x64;
constexpr std::uint32_t kTimer = 0x6C;
constexpr std::uint32_t kFade = 0x70;
constexpr std::uint32_t kSelected = 0x74;
constexpr std::uint32_t kState = 0x78;
constexpr std::uint32_t kTimeout = 0x7C;
// Going Commando's pad words the update reads (its Controller: held 0x1C0, pressed 0x1C4, inhibit
// 0x1CC, repeat 0x1D8, the stick 0x148/0x14C).
constexpr std::uint32_t kGcHeld = 0x1C0;
constexpr std::uint32_t kGcPressed = 0x1C4;
constexpr std::uint32_t kGcInhibit = 0x1CC;
// Going Commando's statistics entry (gc_shim_stats + 0xF0: uses, its best, the levels).
constexpr std::uint32_t kGcUses = kGcStatsQuickSelect;
// The game's 2D alpha at full (the PS2's 0x80).
constexpr float kFullAlpha = 128.0f;

std::int32_t read_i32(const std::uint8_t* p, std::uint32_t at) {
    std::int32_t v;
    std::memcpy(&v, p + at, sizeof v);
    return v;
}
void write_i32(std::uint8_t* p, std::uint32_t at, std::int32_t v) { std::memcpy(p + at, &v, sizeof v); }

// A screen height in the game's 2D units: 512 x 416 over the 4:3 picture, so a screen height is
// 384 units across (512 / (4/3)) and 416 down.
constexpr float kAcrossPerHeight = kScreenWidth * 0.75f;

int game_x(float x) { return static_cast<int>(std::lround(kScreenWidth * 0.5f + x * kAcrossPerHeight)); }
int game_y(float y) { return static_cast<int>(std::lround(kScreenHeight * 0.5f + y * kScreenHeight)); }

}  // namespace

// The items the player can put on the quick select, in the Quick Select page's order: its weapons
// grid, then its hand gadgets grid (layout.hpp: the cells' tables, draw_menu_item_grid's and
// update_menu_item_slots' rule), each cell's item when it is an item (kind 0) and owned. Nothing of
// the Items page (packs, helmets, boots, the Sonic Summoner, the Persuader...) is in them.
std::vector<spiral::Item> Shim::collect_items() {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    std::vector<spiral::Item> out;
    if (m.base == nullptr) {
        return out;
    }
    const std::int32_t in_hand = m.read<std::int32_t>(a.hero + kHeroItem);
    auto take = [&](std::uint32_t table, int count) {
        for (int i = 0; i < count; ++i) {
            const std::uint32_t cell = table + static_cast<std::uint32_t>(i) * kCellStride;
            const std::int32_t id = m.read<std::int16_t>(cell + kCellItem);
            if (m.read<std::int16_t>(cell + kCellKind) != 0 || id <= 0 || id >= static_cast<std::int32_t>(kWeapons) ||
                m.read<std::uint8_t>(a.item_available + static_cast<std::uint32_t>(id)) == 0) {
                continue;
            }
            spiral::Item item;
            item.id = id;
            item.icon = m.read<std::uint16_t>(cell + kCellIcon);
            item.equipped = id == in_hand;
            for (std::uint32_t slot = 0; slot < 8; ++slot) {
                if (m.read<std::int32_t>(a.slot_items + 4 * slot) == id) {
                    item.hot = static_cast<int>(slot) + 1;
                    break;
                }
            }
            out.push_back(item);
        }
    };
    take(a.weapon_cells, kWeaponCells);
    take(a.gadget_cells, kGadgetCells);
    // The game's quick select is the home ring (arrange.hpp): slot i of its eight at the direction
    // draw_hud_item_selector_panel draws it (games/rac1/decomp
    // src/overlays/shared/weapon_hud_and_counters.c:1348-1353: FastAddRots(2 * i * pi / count - pi,
    // pi / 2) with y down, so slot 0 at the top and the others clockwise: 90 - 45 * i degrees
    // counter-clockwise from 3 o'clock); an empty slot leaves its place to the others.
    std::vector<spiral::HomeItem> home;
    std::vector<spiral::Item> rest;
    for (const spiral::Item& item : out) {
        if (item.hot > 0) {
            home.push_back({item, 90.0f - 45.0f * static_cast<float>(item.hot - 1)});
        } else {
            rest.push_back(item);
        }
    }
    std::vector<std::string> notes;
    std::vector<spiral::Item> arranged = spiral::arrange(home, rest, m_wheel_options.look.spiral, &notes);
    for (const std::string& note : notes) {
        log_once("arrange " + note, note);
    }
    return arranged;
}

std::string Shim::item_name(std::int32_t id) const {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    if (id <= 0 || static_cast<std::size_t>(id) >= kWeapons || !game.msg_string) {
        return "";
    }
    const std::uint32_t u = static_cast<std::uint32_t>(id);
    const bool gold = m.read<std::uint8_t>(a.gold + u) != 0;
    const std::int16_t text = m.read<std::int16_t>(a.item_defs + u * kItemDefStride + (gold ? kItemDefGoldName : kItemDefName));
    const std::uint32_t at = game.msg_string(text);
    if (at == 0) {
        return "";
    }
    const char* chars = reinterpret_cast<const char*>(m.at(at));
    return std::string(chars, strnlen(chars, 80));
}

std::string Shim::item_ammo(std::int32_t id, bool& empty) const {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    empty = false;
    if (id <= 0 || static_cast<std::size_t>(id) >= kWeapons) {
        return "";
    }
    const std::uint32_t u = static_cast<std::uint32_t>(id);
    const std::uint32_t price = a.item_price + u * kPriceStride;
    if (m.read<std::uint16_t>(price + kPriceUsesAmmo) == 0) {
        return "";
    }
    const std::int32_t ammo = m.read<std::int32_t>(a.ammo + 4 * u);
    char format[16] = {};
    std::memcpy(format, m.at(a.ammo_format), sizeof format - 1);
    if (std::strcmp(format, "%d/%d") != 0) {
        std::snprintf(format, sizeof format, "%%d/%%d");
    }
    char text[32];
    std::snprintf(text, sizeof text, format, ammo, m.read<std::uint16_t>(price + kPriceMaxAmmo));
    empty = ammo == 0;
    return text;
}

// --- The slow-down before the pause (the mod's) ---
//
// Prey's favourites wheel eases the game's time down as it opens, then holds it (its focus mode
// keeps a time scaler that goes from one scale to a target over a duration: Chairloader's
// ArkTimeScaler). Here: the port's slow motion, each tick `stretch` times as long, the time's scale
// (1 / stretch) going down evenly from 1 to an eighth (the port's slowest) over the setting's
// length of the game's own time, then Going Commando's pause. A tick at stretch s lasts s 60ths.

void Shim::set_stretch(float stretch) {
    m_stretch = stretch;
    if (game.time_stretch) {
        game.time_stretch(stretch);
    }
}

namespace {
// The stretch at `t` (0 to 1) of the way: the scale 1 - t * 7/8, as a stretch.
float stretch_at(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f / (1.0f - t * (1.0f - 1.0f / kStretchMax));
}
}  // namespace

bool Shim::begin_ramp() {
    if (m_wheel_options.ramp_frames <= 0 || !paused_path_available()) {
        return false;
    }
    m_ramp = Ramp::Down;
    m_ramp_elapsed = 0.0f;
    m_ramp_record = false;
    set_stretch(1.0f);
    // The wheel's record, opened in play as the game's own Triangle opens it; the same callbacks
    // keep it in the pause.
    queue_selector();
    log_once("ramp", "the game slows down before the pause (\"Slow-down before pausing\")");
    return true;
}

void Shim::ramp_tick() {
    const float frames = static_cast<float>(std::max(1, m_wheel_options.ramp_frames));
    // The tick that ran lasted the stretch it had.
    m_ramp_elapsed += m_stretch;
    if (m_ramp == Ramp::Down) {
        set_stretch(stretch_at(m_ramp_elapsed / frames));
    } else if (m_ramp == Ramp::Up) {
        if (m_ramp_elapsed >= frames) {
            m_ramp = Ramp::None;
            set_stretch(1.0f);
        } else {
            set_stretch(stretch_at(1.0f - m_ramp_elapsed / frames));
        }
    }
}

void Shim::end_ramp(bool ease_back) {
    if (m_ramp != Ramp::Down) {
        return;
    }
    if (ease_back && m_wheel_options.ease_back) {
        // Back up from where it got to.
        const float frames = static_cast<float>(std::max(1, m_wheel_options.ramp_frames));
        m_ramp = Ramp::Up;
        m_ramp_elapsed = std::max(0.0f, frames - m_ramp_elapsed);
        return;
    }
    m_ramp = Ramp::None;
    set_stretch(1.0f);
}

// --- The wheel ---

void Shim::publish() {
    WheelPicture p;
    p.shown = m_wheel.is_open();
    if (p.shown) {
        p.frame = m_wheel.frame();
    }
    const std::lock_guard<std::mutex> lock(m_picture_lock.lock);
    m_picture = std::move(p);
}

WheelPicture Shim::picture() const {
    const std::lock_guard<std::mutex> lock(m_picture_lock.lock);
    return m_picture;
}

void Shim::close_wheel() {
    m_wheel.close();
    m_armed = false;
    m_committing = false;
    m_cancelled = false;
    publish();
}

// The record's update, each tick it is open (in the pause, in the slow-down, or over the running
// game with "Pause while open" off): weapon_wheel_update_select's frame with the spiral's selection.
void Shim::spiral_update(std::uint8_t* record) {
    const Memory& m = game.memory;
    std::uint8_t* pad = m.at(m_level.at.pad);
    std::uint8_t* fade = record + kFade;
    const bool paused = pause();
    // Going Commando's: active while mode 7 is up, or (option off) once the HUD-open request is done.
    bool active = !paused || menu_is_state_seven() != 0 || m_ramp == Ramp::Down;
    if (!paused) {
        hud_request_open();
        if (hud_is_open_done() == 0) {
            active = false;
        }
    }
    if (active && paused && m_ramp != Ramp::Down) {
        menu_set_state_word_three();
    }
    write_i32(record, kTimeout, frames(180));
    write_i32(record, kTimer, 24);
    if (gc_shim_FastDecTimer(m.at(m_level.at.hero + kHeroCloseTimer)) != 0 && !paused) {
        write_i32(pad, kGcInhibit, 2);
    }
    const std::uint32_t held = static_cast<std::uint32_t>(read_i32(pad, kGcHeld));
    const std::uint32_t pressed = static_cast<std::uint32_t>(read_i32(pad, kGcPressed));
    float stick_x = m.read<float>(m_level.at.pad + kPadStickX);
    float stick_y = m.read<float>(m_level.at.pad + kPadStickY);
    const float magnitude = std::sqrt(stick_x * stick_x + stick_y * stick_y);

    // The wheel opens with the record (UpdateMenu.gek): the items now, the pointer on the one in hand.
    if (!m_wheel.is_open()) {
        m_wheel.open(collect_items(), m_wheel_options.look);
        m_names.clear();
        for (const spiral::Item& item : m_wheel.items()) {
            m_names.push_back(item_name(item.id));
        }
        m_armed = false;
        m_toggle_released = false;
        m_committing = false;
        m_cancelled = false;
        m_stick_logged = 0;
        write_i32(record, kSelected, m_wheel.selected());
        if (game.info) {
            std::string line = "the wheel opens with " + std::to_string(m_wheel.items().size()) + " items:";
            for (const spiral::Item& item : m_wheel.items()) {
                char id[16];
                std::snprintf(id, sizeof id, item.id != 0 ? " 0x%X" : " -", static_cast<unsigned>(item.id));
                line += id;
            }
            game.info(line.c_str());
        }
    }
    m_wheel.advance(1.0f / 60.0f);

    // Going Commando's: the selection waits for the stick to rest (under 0.5), a d-pad press or its
    // countdown (the record's state, 30 frames from menu_init_panel_layout), so a stick held as the
    // wheel opens does not pick at once.
    std::int32_t state = read_i32(record, kState);
    if ((state >> 24) == 0) {
        if ((pressed & 0xF000u) != 0 || magnitude < kStickDeadzone || --state == -1) {
            state = 0x10000FF;
            m_armed = true;
        }
        write_i32(record, kState, state);
    }
    const int before = m_wheel.selected();
    bool moved = false;
    const bool holding = (held & kTriangle) != 0;
    // (the mod's) Toggle: once let go, the wheel stays up; Triangle again picks.
    if (m_wheel_options.toggle && !holding) {
        m_toggle_released = true;
    }
    const bool steering = active && m_armed && !m_committing && (holding || m_wheel_options.toggle);
    if (steering) {
        // UpdatePointer.gek: the stick turns the pointer, as a screen vector (y down). Ratchet &
        // Clank's stick is one already: its y grows downward (pushed up it reads below 0; Going
        // Commando's quick select, whose slot 0 is at the top, takes it when atan2(y, x) is -pi/2).
        if (magnitude >= kStickDeadzone) {
            moved = m_wheel.steer(stick_x, stick_y, kStickDeadzone);
            // The first few turns of each opening in the log: the stick as read, its direction on
            // the screen (degrees counter-clockwise from 3 o'clock) and the slot it points at.
            if (m_stick_logged < kStickLogTurns && game.info) {
                ++m_stick_logged;
                const int slot = m_wheel.selected();
                const float screen = std::atan2(-stick_y, stick_x) * 180.0f / spiral::kPi;
                char line[200];
                std::snprintf(line, sizeof line,
                              "stick %d of the opening: raw (%.2f, %.2f), %.0f deg on the screen; the pointer at "
                              "%.0f deg, slot %d%s",
                              m_stick_logged, stick_x, stick_y, screen, m_wheel.pointer_deg(), slot,
                              slot >= 0 ? "" : slot == -1 ? " (None)" : " (a blank)");
                game.info(line);
            }
        } else if ((pressed & (kDpadRight | kDpadDown)) != 0) {
            // (the mod's) The d-pad: one slot on or back.
            moved = m_wheel.step(1);
        } else if ((pressed & (kDpadLeft | kDpadUp)) != 0) {
            moved = m_wheel.step(-1);
        }
    }
    const int selected = m_wheel.selected();
    write_i32(record, kSelected, selected);
    // Going Commando's selection fade, and its click (the mod's "Click sound"; Prey Menu's
    // UIPipBoyScroll on a change to an item or "None").
    if (selected != before) {
        fade[1] = 0;
        if (moved) {
            gc_shim_snd_play_from_global_bank(3, 0, nullptr);
        }
    } else if (fade[1] < 8) {
        ++fade[1];
    }

    // (the mod's) Circle cancels: closed with nothing picked.
    if (active && (pressed & kCircle) != 0 && !m_committing) {
        m_committing = true;
        m_cancelled = true;
    }
    // The pick: Triangle let go (or, toggled, pressed again).
    if (active && !m_committing) {
        if (!m_wheel_options.toggle ? !holding : (m_toggle_released && (pressed & kTriangle) != 0)) {
            m_committing = true;
        }
    }
    // Going Commando's: once let go (here: once picking), fade[0] runs down; at 0 the pick is
    // made; each such tick mode 7 is moved on (or, the option off or in the slow-down, the request
    // cleared); the record closes once fade[0] is 0.
    if (m_committing) {
        if (fade[0] != 0) {
            --fade[0];
        }
        if (fade[0] == 0 && game.info) {
            char line[200];
            const std::int32_t in_hand = Memory{gc_shim_hero}.read<std::int32_t>(kGcHeroItem);
            if (m_cancelled || !m_wheel.is_item_or_none(selected)) {
                std::snprintf(line, sizeof line, "cancelled (%s; 0x%X in hand)", m_cancelled ? "Circle" : "let go on a blank",
                              static_cast<unsigned>(in_hand));
            } else {
                const std::int32_t pick = selected >= 0 ? m_wheel.items()[static_cast<std::size_t>(selected)].id : kWrench;
                const std::string name = selected >= 0 ? m_names[static_cast<std::size_t>(selected)] : item_name(kWrench);
                std::snprintf(line, sizeof line, "picked item 0x%X (%s), equipped 0x%X%s", static_cast<unsigned>(pick),
                              name.c_str(), static_cast<unsigned>(pick),
                              pick == in_hand ? " (already in hand)" : "");
            }
            game.info(line);
        }
        if (fade[0] == 0 && !m_cancelled && m_wheel.is_item_or_none(selected)) {
            Memory hero{gc_shim_hero};
            Memory stats{gc_shim_stats};
            // (the mod's) "None" is the wrench.
            const std::int32_t id = selected >= 0 ? m_wheel.items()[static_cast<std::size_t>(selected)].id : kWrench;
            if (hero.read<std::int32_t>(kGcHeroItem) != id && stats.read<std::uint16_t>(kGcUses) < 0xFFFF) {
                stats.write<std::uint16_t>(kGcUses, static_cast<std::uint16_t>(stats.read<std::uint16_t>(kGcUses) + 1));
            }
            if (stats.read<std::uint32_t>(kGcUses + 4) < gc_shim_frame_counter) {
                stats.write<std::uint32_t>(kGcUses + 4, gc_shim_frame_counter);
            }
            const std::int32_t level = m.read<std::int32_t>(m_level.at.level_index);
            stats.write<std::uint32_t>(kGcUses + 8, stats.read<std::uint32_t>(kGcUses + 8) | (1u << (level & 31)) | 0x80000000u);
            // (the mod's) The wheel opens on the item in hand: let go there, nothing changes (Going
            // Commando's opens on nothing, which picks nothing), so a double tap's swap in the pause
            // stands.
            if (id == hero.read<std::int32_t>(kGcHeroItem)) {
                // nothing to equip
            } else if (id == 0x18) {
                gc_shim_item_0x18();
            } else {
                hero.write<std::int32_t>(kGcHeroPending, id);
            }
            // The ammo held to its most (the pick's last line in weapon_wheel_update_select).
            const std::uint32_t u = static_cast<std::uint32_t>(id);
            const std::uint32_t price = m_level.at.item_price + u * kPriceStride;
            const std::int32_t most = m.read<std::uint16_t>(price + kPriceMaxAmmo);
            if (m.read<std::uint16_t>(price + kPriceUsesAmmo) != 0 && most < m.read<std::int32_t>(m_level.at.ammo + 4 * u)) {
                m.write<std::int32_t>(m_level.at.ammo + 4 * u, most);
            }
        }
        if (paused && m_ramp != Ramp::Down && menu_is_state_seven() != 0) {
            menu_advance_state_in_mode_seven();
        } else {
            hud_clear_flag_b();
            hud_clear_flag_a();
            end_ramp(true);
        }
    } else if (fade[0] < 8) {
        ++fade[0];
    }
    if (fade[0] == 0 && m_committing) {
        gc_shim_hud_close_element_by_key(read_i32(record, kHandle));
        write_i32(record, kTimer, -6);
        close_wheel();
        return;
    }
    publish();
}

// The record's draw (in the world's picture, the game's 2D): the items' icons on the spiral and the
// pointed-at item's name and ammo, with the game's own sprites and font. The art is the overlay's.
int Shim::spiral_draw(std::uint8_t* record) {
    const Memory& m = game.memory;
    const Memory rec{record};
    if (!m_wheel.is_open()) {
        return rec.read<std::int32_t>(kRecordWidth);
    }
    const spiral::Frame f = m_wheel.frame();
    const float open = std::clamp(static_cast<float>(record[kFade]) / 8.0f, 0.0f, 1.0f);
    const float pick = std::clamp(static_cast<float>(record[kFade + 1]) / 8.0f, 0.0f, 1.0f);
    for (const spiral::Icon& icon : f.icons) {
        const spiral::Item& item = m_wheel.items()[static_cast<std::size_t>(icon.item)];
        const bool gold = m.read<std::uint8_t>(m_level.at.gold + static_cast<std::uint32_t>(item.id)) != 0;
        const int w = std::max(1, static_cast<int>(std::lround(icon.size * kAcrossPerHeight)));
        const int h = std::max(1, static_cast<int>(std::lround(icon.size * kScreenHeight)));
        const int alpha = static_cast<int>(kFullAlpha * icon.alpha * open);
        game.draw_hud_sprite(game.get_icon_frame(item.icon, gold ? 4 : 0), game_x(icon.x) - w / 2, game_y(icon.y) - h / 2, w, h,
                             alpha);
    }
    // The name above the info bar and the ammo below, in Going Commando's colours (white, the ammo
    // red when it is out), faded in with the selection.
    if (m_wheel.is_item_or_none(f.selected)) {
        const float opacity = open * pick;
        const std::uint32_t white = tween_colour(0x00F0F0F0u, 0x80F0F0F0u, opacity);
        std::string name = f.selected >= 0 ? m_names[static_cast<std::size_t>(f.selected)] : item_name(kWrench);
        if (!name.empty()) {
            game.font_print_center(game_x(0.0f), game_y(f.name_y), white, name.c_str(), -1);
        }
        if (f.selected >= 0) {
            bool empty = false;
            const std::string ammo = item_ammo(m_wheel.items()[static_cast<std::size_t>(f.selected)].id, empty);
            if (!ammo.empty()) {
                const std::uint32_t colour = empty ? tween_colour(0x004040FFu, 0x804040FFu, opacity) : white;
                game.font_print_center(game_x(0.0f), game_y(f.detail_y), colour, ammo.c_str(), -1);
            }
        }
    }
    return rec.read<std::int32_t>(kRecordWidth);
}

}  // namespace gc_weapon_wheel::gc

// Going Commando's names for the record's callbacks (UpdateModalState's mode 7 queues them with
// hud_set_slot_contents): the spiral's update and draw.
extern "C" {
void weapon_wheel_update_select(void* selector) {
    gc_weapon_wheel::gc::shim().spiral_update(static_cast<std::uint8_t*>(selector));
}
int weapon_wheel_draw(char* menu) {
    return gc_weapon_wheel::gc::shim().spiral_draw(reinterpret_cast<std::uint8_t*>(menu));
}
}
