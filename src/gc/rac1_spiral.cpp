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
// loops; the panels the mod's overlay draws under the HUD, the fallback icons and the text the
// game's own HUD calls draw here).
//
// The mod's own, marked in the comments: the items (every owned hand item, not the quick select's
// eight), "None" as the wrench, the stick's rest, the d-pad stepping one slot, Circle cancelling,
// the toggle mode, and the slow-down before the pause.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "rac1_shim.hpp"
#include "../rac1/hud_set.hpp"
#include "../spiral/arrange.hpp"
#include "../spiral/glyph.hpp"
#include "../spiral/icon_packs.hpp"

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

// The game's own provider: the pause menu's Quick Select page, its weapons grid then its hand
// gadgets grid (layout.hpp: the cells' tables, draw_menu_item_grid's and update_menu_item_slots'
// rule), each cell's item when it is an item (kind 0) and owned (`everything`, the debug preview:
// owned or not, the unowned a preview entry). Nothing of the Items page (packs, helmets, boots, the
// Sonic Summoner, the Persuader...) is in them. Each item's icon is its definition's (item_defs
// +0x38), else its cell's.
void Shim::quick_select_items(std::vector<spiral::Item>& out, bool everything) const {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    auto take = [&](std::uint32_t table, int count, bool gadgets) {
        for (int i = 0; i < count; ++i) {
            const std::uint32_t cell = table + static_cast<std::uint32_t>(i) * kCellStride;
            const std::int32_t id = m.read<std::int16_t>(cell + kCellItem);
            if (m.read<std::int16_t>(cell + kCellKind) != 0 || id <= 0 || id >= kItemTable || (!owned(id) && !everything)) {
                continue;
            }
            spiral::Item item;
            item.id = id;
            item.preview = !owned(id);
            item.category = gadgets ? spiral::look::Category::Gadget : spiral::look::Category::Weapon;
            item.icon = item_icon(id, m.read<std::uint16_t>(cell + kCellIcon));
            out.push_back(item);
        }
    };
    take(a.weapon_cells, kWeaponCells, false);
    take(a.gadget_cells, kGadgetCells, true);
}

// (the mod's) The port's provider: weapons equipment mods registered (rac1_weapon_register), in
// the items the port gives them (Game::extension_items: today its two native extension slots, 1
// and 36, which the grids do not have), when owned and with an icon, as the port's own quick
// select lists them (port/game/common/mod_weapon_wheel.inc). More slots, or an SDK list of them,
// are more items here, or another provider (add_item_provider).
void Shim::registered_items(std::vector<spiral::Item>& out, bool everything) const {
    for (const std::int32_t id : game.extension_items) {
        const std::uint16_t icon = item_icon(id, 0);
        if (icon == 0 || (!owned(id) && !everything)) {
            continue;
        }
        spiral::Item item;
        item.id = id;
        item.icon = icon;
        item.preview = !owned(id);
        out.push_back(item);
    }
}

bool Shim::owns_gold(std::int32_t id) const {
    return id > 0 && id < kItemTable && game.memory.base != nullptr &&
           game.memory.read<std::uint8_t>(m_level.at.gold + static_cast<std::uint32_t>(id)) != 0;
}

// (the mod's) Whether the wheel shows item `id` (a weapon) as gold: the player owns its gold
// version (gold_weapon_purchased), or the temporary debug setting previews it (look only: nothing
// of the game's is written for it).
bool Shim::shows_gold(std::int32_t id) const {
    const DebugPreview preview = m_wheel_options.debug_preview;
    if (preview == DebugPreview::AllWeaponsGold || (preview == DebugPreview::GoldVisibombGun && id == kVisibombGun)) {
        return true;
    }
    return owns_gold(id);
}

// (the mod's) The label over a gold weapon's name: the game's own word, from its gold name and its
// name (item_defs +0x48 and +0x46: "Gold Visibomb Gun" less "Visibomb Gun"), else "Gold".
std::string Shim::gold_label(std::int32_t id) const {
    const std::string name = item_name(id, false), gold = item_name(id, true);
    std::string label;
    if (!name.empty() && gold.size() > name.size()) {
        if (gold.compare(gold.size() - name.size(), name.size(), name) == 0) {
            label = gold.substr(0, gold.size() - name.size());
        } else if (gold.compare(0, name.size(), name) == 0) {
            label = gold.substr(name.size());
        }
    }
    const auto first = label.find_first_not_of(" -");
    const auto last = label.find_last_not_of(" -");
    label = first == std::string::npos ? "" : label.substr(first, last - first + 1);
    return label.empty() ? "Gold" : label;
}

bool Shim::owned(std::int32_t id) const {
    return id > 0 && id < kItemTable && game.memory.base != nullptr &&
           game.memory.read<std::uint8_t>(m_level.at.item_available + static_cast<std::uint32_t>(id)) != 0;
}

// The items the player can put on the quick select: what each item provider lists, in the
// providers' order, an item listed once (the first provider's). Nothing here is sized or capped:
// any number of items, any item ids. Each item's place on the game's quick select and whether it
// is in hand, for whichever item it is.
std::vector<spiral::Item> Shim::collect_items() {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    std::vector<spiral::Item> out;
    if (m.base == nullptr) {
        return out;
    }
    // TEMPORARY (the debug preview, README "Remove before release"): Everything.
    const bool everything = m_wheel_options.debug_preview == DebugPreview::Everything;
    std::vector<spiral::Item> listed;
    quick_select_items(listed, everything);
    registered_items(listed, everything);
    for (const ItemProvider& provider : m_item_providers) {
        provider(*this, listed);
    }
    const std::int32_t in_hand = m.read<std::int32_t>(a.hero + kHeroItem);
    for (spiral::Item& item : listed) {
        if (item.id == 0 || std::any_of(out.begin(), out.end(), [&item](const spiral::Item& it) { return it.id == item.id; })) {
            continue;
        }
        item.equipped = item.id == in_hand;
        item.gold = shows_gold(item.id);
        item.hot = 0;
        for (std::uint32_t slot = 0; slot < 8 && !item.preview; ++slot) {
            if (m.read<std::int32_t>(a.slot_items + 4 * slot) == item.id) {
                item.hot = static_cast<int>(slot) + 1;
                break;
            }
        }
        out.push_back(item);
    }
    // The game's quick select is the home ring (arrange.hpp): slot i of its eight at the direction
    // draw_hud_item_selector_panel draws it (games/rac1/decomp
    // src/overlays/shared/weapon_hud_and_counters.c:1348-1353: FastAddRots(2 * i * pi / count - pi,
    // pi / 2) with y down, so slot 0 at the top and the others clockwise: 90 - 45 * i degrees
    // counter-clockwise from 3 o'clock); an empty slot leaves its place to the others.
    // The debug preview's Everything: after each weapon with a gold version (kGoldWeapons), that
    // version as an entry of its own (in the rest, where the weapon is or would be).
    std::vector<spiral::HomeItem> home;
    std::vector<spiral::Item> rest;
    for (const spiral::Item& item : out) {
        if (item.hot > 0) {
            home.push_back({item, 90.0f - 45.0f * static_cast<float>(item.hot - 1)});
        } else {
            rest.push_back(item);
        }
        if (everything && std::find(std::begin(kGoldWeapons), std::end(kGoldWeapons), item.id) != std::end(kGoldWeapons)) {
            spiral::Item gold = item;
            gold.hot = 0;
            gold.equipped = false;
            gold.gold = true;
            gold.preview = true;
            rest.push_back(gold);
        }
    }
    std::vector<spiral::Item> arranged = spiral::arrange(home, rest, m_wheel_options.look.spiral);
    for (spiral::Item& item : arranged) {
        attach_glyph(item);
    }
    return arranged;
}

// (the mod's) The items' glyphs, from their HUD icons in game memory (layout.hpp: hud_state's
// frames, image and palette pages): the frame the Quick Select page draws for an item (frame 4 for
// a gold weapon, as draw_menu_item_grid), decoded, checked and redrawn (glyph.hpp), kept as an
// overlay picture for the rest of the level.
//
// When the pages hold the icons (games/rac1/decomp): LoadHudBanks (src/game/system/loaders.c:386)
// decompresses bank 0, the HUD's icons, into a level resource buffer, uploads it to the GS and
// leaves it linked there (Hud_SendResidentBank); banks 2 to 4 stay stashed. The pause menu
// (src/overlays/shared/pause_menu.c:289-295) unlinks bank 0 (apply_group_entry_adjustment(0)) and
// decompresses banks 0 and 2 (3 or 4) into its own buffer (decompress_and_relocate_selected_groups,
// src/overlays/shared/weapon_hud_and_counters.c:58), the grid draws from them, and as it closes it
// uploads bank 0 to the GS from that buffer again (pause_menu.c:399-404) and unlinks 2 to 4 (their
// pages' bit 31). In play the HUD draws bank 0 from the GS, and both buffers are the level's to
// reuse: bank 0's pages stay linked to memory that is no longer theirs, which is the noise 1.1.1
// showed later in a session as it rebuilt glyphs whenever the pixels changed.
//
// So: while the pause menu's item grid draws (rac1.menu.item_grid.draw) the pages are certain, and
// a glyph captured there is kept for the level. At the wheel's opening, an item without a glyph yet
// is tried from the pages as they are, and kept only when it passes the checks; the next pause menu
// captures it again from certain pages, once. A glyph is never made again from changed pixels; a
// level change (its own HUD set) drops them all. Anything amiss leaves the item without one, and
// the game's own icon is drawn.
int Shim::icon_frame(std::uint16_t icon, bool gold) const {
    return game.get_icon_frame ? game.get_icon_frame(icon, gold ? 4 : 0) : 0;
}

Shim::CachedGlyph& Shim::glyph_entry(int frame) {
    auto it = std::find_if(m_glyphs.begin(), m_glyphs.end(), [&](const CachedGlyph& g) { return g.frame == frame; });
    if (it == m_glyphs.end()) {
        m_glyphs.push_back({});
        m_glyphs.back().frame = frame;
        return m_glyphs.back();
    }
    return *it;
}

void Shim::capture_glyph(CachedGlyph& entry, std::uint16_t icon, Capture when) {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    const int frame = entry.frame;
    const char* where = when == Capture::Menu ? "in the pause menu" : "at the wheel's opening";
    auto report = [&](const char* outcome, const char* reason) {
        // Once a level for each icon, place and outcome.
        char key[96];
        std::snprintf(key, sizeof key, "glyph %d %d %d %s %s", m_level_number, frame, static_cast<int>(when), outcome, reason);
        char line[200];
        std::snprintf(line, sizeof line, "the glyph of icon 0x%X (frame %d), %s: %s%s%s", static_cast<unsigned>(icon), frame,
                      where, outcome, *reason != 0 ? ": " : "", reason);
        log_once(key, line);
    };
    const std::uint32_t frames = m.read<std::uint32_t>(a.hud_state + kHudFrames);
    const std::uint32_t images = m.read<std::uint32_t>(a.hud_state + kHudImages);
    const std::uint32_t palettes = m.read<std::uint32_t>(a.hud_state + kHudPalettes);
    // A page whose bank is not linked has bit 31 set (apply_group_entry_adjustment): out of memory.
    auto in_memory = [](std::uint32_t address, std::uint32_t bytes) {
        return address != 0 && address < 0x02000000u && address + bytes <= 0x02000000u;
    };
    if (frame <= 0 || !in_memory(frames, 4u * static_cast<std::uint32_t>(frame) + 4) || images == 0 || palettes == 0) {
        report("rejected", "no HUD frame table");
        return;
    }
    const std::int16_t palette = m.read<std::int16_t>(frames + 4u * static_cast<std::uint32_t>(frame));
    const std::int16_t image = m.read<std::int16_t>(frames + 4u * static_cast<std::uint32_t>(frame) + 2);
    if (palette < 0 || image < 0 || !in_memory(images, 8u * static_cast<std::uint32_t>(image) + 8) ||
        !in_memory(palettes, 8u * static_cast<std::uint32_t>(palette) + 8)) {
        report("rejected", "no pages for the frame");
        return;
    }
    const std::uint32_t pixels = m.read<std::uint32_t>(images + 8u * static_cast<std::uint32_t>(image));
    const int width = 1 << (m.read<std::uint8_t>(images + 8u * static_cast<std::uint32_t>(image) + 6) & 15);
    const int height = 1 << (m.read<std::uint8_t>(images + 8u * static_cast<std::uint32_t>(image) + 7) & 15);
    const std::uint32_t colours = m.read<std::uint32_t>(palettes + 8u * static_cast<std::uint32_t>(palette));
    const std::uint32_t bytes = static_cast<std::uint32_t>(width * height);
    if (width < 8 || height < 8 || width > 256 || height > 256) {
        report("rejected", "not an icon's size");
        return;
    }
    if (!in_memory(pixels, bytes) || !in_memory(colours, 1024)) {
        report("rejected", "its pages are not loaded");
        return;
    }
    const spiral::Glyph g = spiral::make_glyph(m.at(pixels), width, height, spiral::palette_from_csm1(m.at(colours)));
    if (!g.ok) {
        report("rejected", g.reason);
        return;
    }
    // White, its shape in the alpha: the look colours it (look.hpp: blue, green, the highlight).
    std::vector<std::uint8_t> white = g.picture.rgba;
    for (std::size_t i = 0; i + 3 < white.size(); i += 4) {
        white[i] = white[i + 1] = white[i + 2] = 255;
    }
    const int made = game.create_image(g.picture.width, g.picture.height, white.data());
    if (made == 0) {
        report("rejected", "the port refused the picture");
        return;
    }
    if (entry.image != 0 && game.destroy_image) {
        game.destroy_image(entry.image);
    }
    entry.image = made;
    entry.width = g.picture.width;
    entry.height = g.picture.height;
    entry.from_menu = when == Capture::Menu;
    report("captured and valid", "");
}

// rac1.menu.item_grid.draw: every owned cell's icon of the Quick Select page's grids that has no
// glyph from a pause menu yet, tried once a pause menu (a rejected one is not decoded again each
// frame).
void Shim::menu_grid_draw() {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    m_in_menu = true;
    if (m_hud.loaded() || m.base == nullptr || !game.create_image || !game.get_icon_frame || a.hud_state == 0) {
        return;  // with the disc's HUD set, its pictures are the mod's own (attach_glyph)
    }
    auto take = [&](std::uint32_t table, int count) {
        for (int i = 0; i < count; ++i) {
            const std::uint32_t cell = table + static_cast<std::uint32_t>(i) * kCellStride;
            const std::int32_t id = m.read<std::int16_t>(cell + kCellItem);
            if (m.read<std::int16_t>(cell + kCellKind) != 0 || !owned(id)) {
                continue;
            }
            const std::uint16_t icon = m.read<std::uint16_t>(cell + kCellIcon);
            const int frame = icon_frame(icon, owns_gold(id));
            if (frame <= 0) {
                continue;
            }
            CachedGlyph& entry = glyph_entry(frame);
            if (entry.from_menu || entry.menu_tried == m_menus) {
                continue;
            }
            entry.menu_tried = m_menus;
            capture_glyph(entry, icon, Capture::Menu);
        }
    };
    take(a.weapon_cells, kWeaponCells);
    take(a.gadget_cells, kGadgetCells);
}

std::uint16_t Shim::item_icon(std::int32_t id, std::uint16_t fallback) const {
    const Memory& m = game.memory;
    if (m.base == nullptr || m_level.at.item_defs == 0 || id <= 0 || id >= kItemTable) {
        return fallback;
    }
    const std::uint16_t icon =
        m.read<std::uint16_t>(m_level.at.item_defs + static_cast<std::uint32_t>(id) * kItemDefStride + kItemDefIcon);
    return icon != 0 ? icon : fallback;
}

// (the mod's) An item's picture on the wheel, the first there is of (ICONS.md):
//   1. an icon pack's glyph for the item (icon_packs.hpp: the mod's own glyphs are a pack, read as
//      any other; a later pack's over an earlier's), with the pack's category and colours if it
//      gives them;
//   2. the glyph the mod makes from the item's HUD icon in the level's own HUD set, which it reads
//      from the player's disc data (hud_set.hpp: the game's copy is only there in the pause menu);
//      without the disc data, from game memory as 1.1.2 did (capture_glyph);
//   3. none: the game's own icon is drawn (spiral_draw).
// Its colours are its category's (look.hpp): the game's grouping of the item (collect_items), or
// its pack's; a weapon whose gold version the player owns (gold_weapon_purchased, the flag the pause
// menu draws icon frame 4 by: games/rac1/decomp src/game/ui/pause.c:2977) is gold. Made once a
// level for each item and frame (a gold weapon's frame is another), logged once.
void Shim::attach_glyph(spiral::Item& item) {
    if (item.id == 0 || !game.create_image) {
        return;
    }
    const bool gold = item.gold;
    const int variant = gold ? 4 : 0;
    const bool disc = m_hud.loaded() && m_hud.has_icon(item.icon);
    const int frame = disc ? m_hud.icon_frame(item.icon, variant) : icon_frame(static_cast<std::uint16_t>(item.icon), gold);
    const std::pair<int, int> key{item.id, frame};
    auto found = m_item_icons.find(key);
    if (found == m_item_icons.end()) {
        ItemPicture made;
        std::string source = "none: the game's own icon";
        auto create = [&](const spiral::Picture& p) {
            made.image = game.create_image(p.width, p.height, p.rgba.data());
            made.width = p.width;
            made.height = p.height;
            return made.image != 0;
        };
        hud::Indexed picture;
        std::string why;
        const spiral::PackIcon* pack = m_icon_packs.icon(item.id);
        const spiral::Picture* drawn = pack != nullptr ? m_icon_packs.picture(*pack, why) : nullptr;
        if (drawn != nullptr && create(*drawn)) {
            made.pack = pack;
            source = "the icon pack " + pack->pack + "'s " + pack->file;
        } else if (disc && m_hud.frame_picture(frame, picture)) {
            if (pack != nullptr) {
                source = "the icon pack " + pack->pack + "'s " + pack->file + " cannot be used (" + why + "); ";
            } else {
                source.clear();
            }
            spiral::Palette palette{};
            std::copy(picture.palette.begin(), picture.palette.end(), palette.begin());
            const spiral::Glyph g = spiral::make_glyph(picture.indices.data(), picture.width, picture.height, palette);
            std::vector<std::uint8_t> white = g.picture.rgba;
            for (std::size_t i = 0; i + 3 < white.size(); i += 4) {
                white[i] = white[i + 1] = white[i + 2] = 255;
            }
            if (!g.ok) {
                source += std::string("none: the disc's icon is not a glyph (") + g.reason + "): the game's own icon";
            } else if ((made.image = game.create_image(g.picture.width, g.picture.height, white.data())) != 0) {
                made.width = g.picture.width;
                made.height = g.picture.height;
                source += "the glyph made from the disc's HUD set";
            } else {
                source += "none: the port refused the picture: the game's own icon";
            }
        } else if (!m_hud.loaded()) {
            // No disc data: game memory (1.1.2's capture, its checks); not kept here, as its glyph
            // may yet come from the pause menu.
            if (frame > 0 && game.get_icon_frame && m_level.at.hud_state != 0) {
                CachedGlyph& entry = glyph_entry(frame);
                if (entry.image == 0) {
                    capture_glyph(entry, static_cast<std::uint16_t>(item.icon), Capture::Opening);
                }
                item.glyph = entry.image;
                item.glyph_width = entry.width;
                item.glyph_height = entry.height;
            }
            if (gold && item.category == spiral::look::Category::Weapon) {
                item.category = spiral::look::Category::Gold;
            }
            return;
        } else {
            source = "none: the disc's HUD set has no such icon: the game's own icon";
        }
        char line[512];
        std::snprintf(line, sizeof line, "the icon of item 0x%X (HUD icon 0x%X, frame %d): %s", static_cast<unsigned>(item.id),
                      static_cast<unsigned>(item.icon), frame, source.c_str());
        char logkey[64];
        std::snprintf(logkey, sizeof logkey, "icon %d %d %d", m_level_number, item.id, frame);
        log_once(logkey, line);
        found = m_item_icons.emplace(key, made).first;
    }
    item.glyph = found->second.image;
    item.glyph_width = found->second.width;
    item.glyph_height = found->second.height;
    if (const spiral::PackIcon* pack = found->second.pack) {
        if (pack->has_category) {
            item.category = pack->category;
        }
        item.colours = pack->colours;
        item.wrap = pack->wrap;
    }
    if (gold && item.category == spiral::look::Category::Weapon) {
        item.category = spiral::look::Category::Gold;
    }
}

// (the mod's) The game's own menu panel (look.hpp), its pictures read as the game reads them: the
// effect textures 0x1C (the scanlines' tile), 0x19 (the glint) and 0x1A (the static's noise) from
// game memory (GetEffectTex: effect_texture_definitions, level_texture_payload_address), where
// the level keeps them; the vignette (HUD icon 0xE99E frame 7) from the disc's HUD set. Each is
// checked for its expected size and look; one that is not right is left to the painter's stand-in
// (1.3.0's look). Nothing of them is kept on disk.
void Shim::load_panel_art() {
    if (m_panel_tried || !game.create_image) {
        return;
    }
    m_panel_tried = true;
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    auto in_memory = [](std::uint32_t address, std::uint32_t bytes) {
        return address != 0 && address < 0x02000000u && address + bytes <= 0x02000000u;
    };
    // An effect texture as RGBA (alpha to 255), or empty.
    auto effect = [&](int n, int log2, std::string& why) {
        spiral::Picture p;
        const std::uint32_t def = a.effect_textures + 16u * static_cast<std::uint32_t>(n);
        const std::uint32_t payload = m.base != nullptr && a.texture_payload != 0 ? m.read<std::uint32_t>(a.texture_payload) : 0;
        if (m.base == nullptr || a.effect_textures == 0 || !in_memory(def, 16) || !in_memory(payload, 16)) {
            why = "no effect texture table";
            return p;
        }
        if (m.read<std::int16_t>(def + 0xC) != log2 || m.read<std::int16_t>(def + 0xE) != log2) {
            why = "not its size";
            return p;
        }
        const int side = 1 << log2;
        const std::uint32_t texels = payload + 16u * m.read<std::uint16_t>(def + 8);
        const std::uint32_t colours = payload + 16u * m.read<std::uint16_t>(def + 0xA);
        if (!in_memory(texels, static_cast<std::uint32_t>(side * side)) || !in_memory(colours, 1024)) {
            why = "its texels are not in memory";
            return p;
        }
        return spiral::decode(m.at(texels), side, side, spiral::palette_from_csm1(m.at(colours)));
    };
    auto px = [](const spiral::Picture& p, int x, int y) { return &p.rgba[(static_cast<std::size_t>(y) * p.width + x) * 4]; };
    std::string report;
    auto note = [&report](const char* what, const std::string& outcome) {
        report += (report.empty() ? "" : ", ") + std::string(what) + " " + outcome;
    };
    auto create_nearest = [this](const spiral::Picture& p) {
        return (game.create_image_nearest ? game.create_image_nearest : game.create_image)(p.width, p.height, p.rgba.data());
    };
    // The scanlines' tile: its even rows clear, its odd rows grey and part opaque, each row one
    // tone. A column of its rows, a row a tile row, over the screen's height (PAL's 448 lines, at
    // 1.5 rows a line: NTSC's fewer lines use its top).
    {
        std::string why;
        const spiral::Picture tile = effect(0x1C, 4, why);
        bool ok = !tile.rgba.empty();
        for (int y = 0; ok && y < tile.height; ++y) {
            for (int x = 0; ok && x < tile.width; ++x) {
                const std::uint8_t* c = px(tile, x, y);
                const std::uint8_t* first = px(tile, 0, y);
                ok = (y % 2 == 0 ? c[3] == 0 : c[3] > 0 && c[3] < 255) && std::abs(c[0] - first[0]) < 8 &&
                     std::abs(c[0] - c[1]) < 8 && std::abs(c[3] - first[3]) < 8;
            }
        }
        if (ok) {
            spiral::Picture column;
            column.width = 2;
            column.height = static_cast<int>(spiral::look::kLinesPal * spiral::look::kScanRowsPerLine);
            column.rgba.resize(static_cast<std::size_t>(column.width * column.height) * 4);
            for (int y = 0; y < column.height; ++y) {
                for (int x = 0; x < column.width; ++x) {
                    std::copy(px(tile, 0, y % tile.height), px(tile, 0, y % tile.height) + 4,
                              column.rgba.begin() + (static_cast<std::ptrdiff_t>(y) * column.width + x) * 4);
                }
            }
            m_panel_art.scan = create_nearest(column);
        }
        note("scanlines (FX 0x1C)", m_panel_art.scan != 0 ? "read" : "not read (" + (why.empty() ? std::string("not its pattern") : why) + ")");
    }
    // The glint: mostly clear, its two corners lit.
    {
        std::string why;
        const spiral::Picture g = effect(0x19, 6, why);
        int lit = 0;
        for (std::size_t i = 3; i < g.rgba.size(); i += 4) {
            lit += g.rgba[i] > 0;
        }
        const bool ok = !g.rgba.empty() && lit > 8 && lit * 4 < g.width * g.height && px(g, 1, 1)[3] > 0 && px(g, 62, 62)[3] > 0;
        if (ok) {
            m_panel_art.glint = game.create_image(g.width, g.height, g.rgba.data());
        }
        note("glint (FX 0x19)", m_panel_art.glint != 0 ? "read" : "not read (" + (why.empty() ? std::string("not its pattern") : why) + ")");
    }
    // The noise: grey, varied; repeated to kNoiseTexels square, opaque (the flash adds it by its own
    // strength).
    {
        std::string why;
        const spiral::Picture n = effect(0x1A, 5, why);
        int low = 255, high = 0;
        bool grey = !n.rgba.empty();
        for (std::size_t i = 0; grey && i < n.rgba.size(); i += 4) {
            grey = std::abs(n.rgba[i] - n.rgba[i + 1]) < 16 && std::abs(n.rgba[i] - n.rgba[i + 2]) < 16;
            low = std::min<int>(low, n.rgba[i]);
            high = std::max<int>(high, n.rgba[i]);
        }
        if (grey && high - low > 32) {
            spiral::Picture tiled;
            tiled.width = tiled.height = spiral::look::kNoiseTexels;
            tiled.rgba.resize(static_cast<std::size_t>(tiled.width * tiled.height) * 4);
            for (int y = 0; y < tiled.height; ++y) {
                for (int x = 0; x < tiled.width; ++x) {
                    std::uint8_t* o = &tiled.rgba[(static_cast<std::size_t>(y) * tiled.width + x) * 4];
                    std::copy(px(n, x % n.width, y % n.height), px(n, x % n.width, y % n.height) + 3, o);
                    o[3] = 255;
                }
            }
            m_panel_art.noise = create_nearest(tiled);
        }
        note("static (FX 0x1A)", m_panel_art.noise != 0 ? "read" : "not read (" + (why.empty() ? std::string("not its pattern") : why) + ")");
    }
    // The vignette: HUD icon 0xE99E frame 7 from the disc's HUD set, 128 x 128, black with alpha.
    {
        hud::Indexed v;
        bool ok = m_hud.loaded() && m_hud.has_icon(0xE99E) && m_hud.frame_picture(m_hud.icon_frame(0xE99E, 7), v) &&
                  v.width == 128 && v.height == 128;
        spiral::Picture p;
        if (ok) {
            spiral::Palette palette{};
            std::copy(v.palette.begin(), v.palette.end(), palette.begin());
            p = spiral::decode(v.indices.data(), v.width, v.height, palette);
            for (std::size_t i = 0; ok && i < p.rgba.size(); i += 4) {
                ok = p.rgba[i] < 48 && p.rgba[i + 1] < 48 && p.rgba[i + 2] < 48;
            }
        }
        if (ok) {
            m_panel_art.vignette = game.create_image(p.width, p.height, p.rgba.data());
        }
        note("vignette (HUD 0xE99E/7)", m_panel_art.vignette != 0 ? "read" : "not read");
    }
    log_once("panel " + std::to_string(m_level_number), "level " + std::to_string(m_level_number) + ": the menu panel's " + report);
}

void Shim::forget_panel_art() {
    for (const int image : {m_panel_art.scan, m_panel_art.glint, m_panel_art.vignette, m_panel_art.noise}) {
        if (image != 0 && game.destroy_image) {
            game.destroy_image(image);
        }
    }
    m_panel_art = spiral::PanelArt{};
    m_panel_tried = false;
}

void Shim::forget_glyphs() {
    for (const CachedGlyph& g : m_glyphs) {
        if (g.image != 0 && game.destroy_image) {
            game.destroy_image(g.image);
        }
    }
    m_glyphs.clear();
    for (const auto& [key, picture] : m_item_icons) {
        if (picture.image != 0 && game.destroy_image) {
            game.destroy_image(picture.image);
        }
    }
    m_item_icons.clear();
    forget_panel_art();
    m_menus = 0;
    m_in_menu = false;
}

int Shim::glyph_image(int frame) const {
    for (const CachedGlyph& g : m_glyphs) {
        if (g.frame == frame) {
            return g.image;
        }
    }
    return 0;
}

bool Shim::glyph_from_menu(int frame) const {
    for (const CachedGlyph& g : m_glyphs) {
        if (g.frame == frame) {
            return g.from_menu;
        }
    }
    return false;
}

// (the mod's) The health meter hidden while the wheel shows, as Prey hides its HUD. Its record is
// hud_chan_slots' slot 1, two before the quick select's (queue_animation_update: slot = key & 0xF;
// show_health_hud queues key 1 with id 0x7535, kept at +0x20; +0x64 is the record's handle, a
// running number the game hands out, not the id). Ratchet & Clank's own Triangle shows it on every
// press (hero_update_transitions, hero_item_input_and_hud.c:127-128, for scale_ticks(0xB4)), so it
// is closed (remove_hud_item with its handle) on every tick of the wheel's, before the HUD is drawn.
// When the wheel closes it is shown again as that press would show it: scale_ticks(0xB4), 180
// frames. The bolt counter and the ammo stay.
void Shim::hide_health(std::uint32_t record) {
    const Memory& m = game.memory;
    if (record < 2 * kRecordBytes || !game.remove_hud_item) {
        return;
    }
    const std::uint32_t health = record - 2 * kRecordBytes;
    const std::int32_t id = m.read<std::int32_t>(health + kRecordId);
    const std::int32_t handle = m.read<std::int32_t>(health + kRecordHandle);
    const bool up = id == kHealthHandle && m.read<std::uint32_t>(health + kRecordUpdate) != 0;
    int closed = -1;
    if (up) {
        m_health_hidden = true;
        closed = game.remove_hud_item(handle);
    }
    if (!m_health_logged && game.info) {
        m_health_logged = true;
        char line[200];
        std::snprintf(line, sizeof line,
                      "the health meter (slot 1 at 0x%08X: id 0x%X, handle %d, update 0x%08X, timeout %d): %s%s", health,
                      static_cast<unsigned>(id), handle, m.read<std::uint32_t>(health + kRecordUpdate),
                      m.read<std::int32_t>(health + kRecordTimeout), up ? "up, closed" : "not up",
                      up ? (closed == 1 ? " (remove_hud_item 1)" : " (remove_hud_item 0: NOT closed)") : "");
        game.info(line);
    }
}

void Shim::restore_health() {
    if (!m_health_hidden) {
        return;
    }
    m_health_hidden = false;
    if (game.show_health_hud && game.hud_keep_visible) {
        game.hud_keep_visible(game.show_health_hud(), ticks(0xB4));
    }
}

std::string Shim::item_name(std::int32_t id, bool gold) const {
    const Memory& m = game.memory;
    const Disc& a = m_level.at;
    if (id <= 0 || id >= kItemTable || !game.msg_string) {
        return "";  // past the game's tables: no name of the game's
    }
    const std::uint32_t u = static_cast<std::uint32_t>(id);
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
    if (id <= 0 || id >= kItemTable) {
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
        p.frame.art = m_panel_art;
        p.frame.lines = pal() ? spiral::look::kLinesPal : spiral::look::kLinesNtsc;
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
    restore_health();
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
    // The d-pad's presses from the held word's own edges too (1.4.3: in game the d-pad stopped
    // stepping; the pressed word may not carry its edges in the pause), so a press counts either way.
    const std::uint32_t dpad_edges = held & ~m_held_before & 0xF000u;
    m_held_before = held;
    const std::uint32_t pressed = static_cast<std::uint32_t>(read_i32(pad, kGcPressed)) | dpad_edges;
    float stick_x = m.read<float>(m_level.at.pad + kPadStickX);
    float stick_y = m.read<float>(m_level.at.pad + kPadStickY);
    const float magnitude = std::sqrt(stick_x * stick_x + stick_y * stick_y);

    // The wheel opens with the record (UpdateMenu.gek): the items now, the pointer on the one in hand.
    if (!m_wheel.is_open()) {
        load_panel_art();
        ++m_openings;
        m_held_before = held;
        m_wheel.open(collect_items(), m_wheel_options.look);
        m_wheel.seed(static_cast<std::uint32_t>(m_play_ticks) * 2654435761u + 1u);
        m_names.clear();
        for (const spiral::Item& item : m_wheel.items()) {
            m_names.push_back(item_name(item.id, false));
        }
        m_armed = false;
        m_toggle_released = false;
        m_committing = false;
        m_cancelled = false;
        m_stick_logged = 0;
        m_touched = false;
        m_health_logged = false;
        write_i32(record, kSelected, m_wheel.selected());
        if (game.info) {
            std::string line = "the wheel opens with " + std::to_string(m_wheel.items().size()) + " items:";
            for (const spiral::Item& item : m_wheel.items()) {
                char id[16];
                std::snprintf(id, sizeof id, item.id != 0 ? " 0x%X" : " -", static_cast<unsigned>(item.id));
                line += id;
                line += item.preview ? (item.gold ? "(gold preview)" : "(preview)") : "";
            }
            game.info(line.c_str());
        }
    }
    m_wheel.advance(1.0f / 60.0f);
    m_wheel.flash_frame();
    hide_health(m.address_of(record));

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
            m_touched = true;
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
            m_touched = true;
            moved = m_wheel.step(1);
        } else if ((pressed & (kDpadLeft | kDpadUp)) != 0) {
            m_touched = true;
            moved = m_wheel.step(-1);
        }
    }
    // The d-pad in the log, each press while the wheel shows: what was read and what it did.
    if ((pressed & 0xF000u) != 0 && game.info) {
        char line[200];
        std::snprintf(line, sizeof line, "d-pad 0x%X (held 0x%X, stick %.2f): %s, slot %d", static_cast<unsigned>(pressed & 0xF000u),
                      static_cast<unsigned>(held), magnitude,
                      steering ? (magnitude >= kStickDeadzone ? "the stick has it" : "stepped") : "not steering yet",
                      m_wheel.selected());
        game.info(line);
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
    if ((pressed & kCircle) != 0 && !m_committing) {
        m_committing = true;
        m_cancelled = true;
    }
    // The pick: Triangle let go (or, toggled, pressed again), as Going Commando's, active or not.
    if (!m_committing) {
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
            if (m_cancelled || !m_touched || !m_wheel.is_item(selected) || preview_entry(selected)) {
                std::snprintf(line, sizeof line, "cancelled (%s; 0x%X in hand)",
                              m_cancelled                ? "Circle"
                              : !m_touched               ? "the pointer never moved"
                              : preview_entry(selected) ? "let go on a debug preview entry: nothing equipped"
                                                         : "let go on a blank",
                              static_cast<unsigned>(in_hand));
            } else {
                const std::int32_t pick = m_wheel.items()[static_cast<std::size_t>(selected)].id;
                const std::string name = m_names[static_cast<std::size_t>(selected)];
                std::snprintf(line, sizeof line, "picked item 0x%X (%s), equipped 0x%X%s", static_cast<unsigned>(pick),
                              name.c_str(), static_cast<unsigned>(pick),
                              pick == in_hand ? " (already in hand)" : "");
            }
            game.info(line);
        }
        // A wheel let go before the stick or the d-pad moved its pointer picks nothing, as Going
        // Commando's, which opens on nothing (here on the item in hand): a double tap's swap in the
        // pause stands.
        if (fade[0] == 0 && !m_cancelled && m_touched && m_wheel.is_item(selected) && !preview_entry(selected)) {
            Memory hero{gc_shim_hero};
            Memory stats{gc_shim_stats};
            const std::int32_t id = m_wheel.items()[static_cast<std::size_t>(selected)].id;
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

// The middle's text: a margin inside the disc's frame (the design's pixels); a game line's height
// (the game draws its three fonts at 16 units a glyph: port/game/rac1/host/save_marks.c, FontPrint)
// and its ink either side of its middle (capitals); the gap between lines (the design's 10 pixels
// between its name and ammo, at 720 lines: 6 of the game's 416).
constexpr float kNameMargin = 4.0f;
constexpr float kLineUnits = 16.0f;
constexpr float kTextInk = 6.0f;
constexpr float kLineGap = 6.0f;
// The gold label's colour: the gold category's glyph tint (#EFCFAA), as the game's text colours it.
constexpr std::uint32_t kGoldLabel = 0x80AACFEFu;


// (the mod's) The room for a line of text in the middle disc at `y` (its middle, screen heights),
// in the game's 2D units across (384 a screen height: 512 over the 4:3 picture): the disc's chord
// at the line's ink (kTextInk either side of its middle: the narrower of its top and bottom), less
// the margin either side.
int Shim::centre_room(const spiral::Frame& f, float y) const {
    const float inner = (spiral::look::kCentreRadius - spiral::look::kFrameCentre[0].width * 0.5f) * f.unit;
    const float dy = std::fabs(y - f.centre_y) + kTextInk / kScreenHeight;
    const float chord = dy < inner ? std::sqrt(inner * inner - dy * dy) : 0.0f;
    return static_cast<int>(2.0f * std::max(0.0f, chord - kNameMargin * f.unit) * kAcrossPerHeight);
}

// (the mod's) The middle's lines: the gold label (if any), the name, the ammo (if any), each a
// game line (16 units: the game draws all three fonts at 16), kLineGap apart, centred in the disc
// as one block. The name: the normal font, else the small, else wrapped at a word onto two lines
// (normal, else small), else at its icon pack's hint (normal, else small), else the small cut
// with "..."; the ammo the large font, else the normal,
// else the small, else cut. Each line fits the disc's chord at it (centre_room), by the game's own
// measure. Without the measure, the first choice.
std::vector<std::string> wrap_hint(const std::string& hint, const std::string& name) {
    // The hint's lines (split at each "|"), when the name is the hint without its "|"s, or without
    // them and a hyphen written before one ("Hydro-|displacer" is "Hydrodisplacer"); else none.
    std::vector<std::string> parts;
    std::string plain, unhyphened, part;
    for (std::size_t i = 0; i < hint.size(); ++i) {
        if (hint[i] == '|') {
            parts.push_back(part);
            part.clear();
            if (!unhyphened.empty() && unhyphened.back() == '-') {
                unhyphened.pop_back();
            }
            continue;
        }
        part += hint[i];
        plain += hint[i];
        unhyphened += hint[i];
    }
    parts.push_back(part);
    if (parts.size() < 2 || (name != plain && name != unhyphened)) {
        return {};
    }
    return parts;
}

std::vector<Shim::CentreLine> Shim::centre_lines(const spiral::Frame& f, const std::string& label, const std::string& name,
                                                 const std::string& ammo, std::uint32_t ammo_colour, const std::string& wrap) const {
    auto fits = [&](TextSize size, const std::string& text, float y) {
        return !game.text_width || game.text_width(size, text.c_str()) <= centre_room(f, y);
    };
    auto cut = [&](TextSize size, std::string text, float y) {
        if (fits(size, text, y)) {
            return text;
        }
        while (!text.empty()) {
            text.pop_back();
            if (fits(size, text + "...", y)) {
                return text + "...";
            }
        }
        return std::string("...");
    };
    struct Choice {
        TextSize size;
        std::vector<std::string> lines;
        bool cut;
    };
    std::vector<Choice> choices = {{TextSize::Normal, {name}, false}, {TextSize::Small, {name}, false}};
    // The word boundary nearest the middle.
    std::size_t split = std::string::npos;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == ' ' && (split == std::string::npos ||
                               std::abs(static_cast<int>(i) * 2 - static_cast<int>(name.size())) <
                                   std::abs(static_cast<int>(split) * 2 - static_cast<int>(name.size())))) {
            split = i;
        }
    }
    if (split != std::string::npos) {
        const std::vector<std::string> two = {name.substr(0, split), name.substr(split + 1)};
        choices.push_back({TextSize::Normal, two, false});
        choices.push_back({TextSize::Small, two, false});
    }
    // An icon pack's hint where the name breaks (a long single word: "Hydro-|displacer").
    const std::vector<std::string> hinted = wrap_hint(wrap, name);
    if (!hinted.empty()) {
        choices.push_back({TextSize::Normal, hinted, false});
        choices.push_back({TextSize::Small, hinted, false});
    }
    choices.push_back({TextSize::Small, {name}, true});
    for (std::size_t c = 0; c < choices.size(); ++c) {
        const Choice& choice = choices[c];
        enum class Role { Label, Name, Ammo };
        std::vector<CentreLine> lines;
        std::vector<Role> roles;
        if (!label.empty()) {
            lines.push_back({TextSize::Small, label, kGoldLabel, 0.0f});
            roles.push_back(Role::Label);
        }
        for (const std::string& part : choice.lines) {
            lines.push_back({choice.size, part, kMenuBlue, 0.0f});
            roles.push_back(Role::Name);
        }
        if (!ammo.empty()) {
            lines.push_back({TextSize::Large, ammo, ammo_colour, 0.0f});
            roles.push_back(Role::Ammo);
        }
        std::vector<float> heights(lines.size(), kLineUnits / kScreenHeight), at(lines.size());
        spiral::stack_lines(f.centre_y, heights.data(), static_cast<int>(lines.size()), kLineGap / kScreenHeight, at.data());
        bool ok = true;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            CentreLine& line = lines[i];
            line.y = at[i];
            if (roles[i] == Role::Name && !choice.cut) {
                ok = ok && fits(line.size, line.text, line.y);
                continue;
            }
            if (roles[i] == Role::Ammo) {
                line.size = fits(TextSize::Large, line.text, line.y)    ? TextSize::Large
                            : fits(TextSize::Normal, line.text, line.y) ? TextSize::Normal
                                                                         : TextSize::Small;
            }
            line.text = cut(line.size, line.text, line.y);
        }
        if (ok || c + 1 == choices.size()) {
            return lines;
        }
    }
    return {};
}

// The record's draw (in the world's picture, the game's 2D): the items' icons on the spiral and the
// pointed-at item's name and ammo, with the game's own sprites and font. The panels are the
// overlay's.
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
        const bool gold = item.gold;
        const int w = std::max(1, static_cast<int>(std::lround(icon.size * kAcrossPerHeight)));
        const int h = std::max(1, static_cast<int>(std::lround(icon.size * kScreenHeight)));
        const int alpha = static_cast<int>(kFullAlpha * icon.alpha * open);
        game.draw_hud_sprite(game.get_icon_frame(item.icon, gold ? 4 : 0), game_x(icon.x) - w / 2, game_y(icon.y) - h / 2, w, h,
                             alpha);
    }
    // The name and the ammo in the middle, in the pause menu's blue (the ammo red when it is out),
    // each over its dark copy a unit down and right (the game's own shadow, hud.c's), faded in with
    // the selection.
    if (m_wheel.is_item(f.selected)) {
        const float opacity = open * pick;
        auto line = [&](TextSize size, int height, float y, std::uint32_t colour, const std::string& text) {
            const std::uint32_t c = tween_colour(colour & 0x00FFFFFFu, colour, opacity);
            const std::uint32_t shadow = tween_colour(0u, colour & 0xFF000000u, opacity);
            const int top = game_y(y) - height / 2;
            game.font_print(size, game_x(0.0f) + 1, top + 1, shadow, text.c_str());
            game.font_print(size, game_x(0.0f), top, c, text.c_str());
        };
        // The label (a gold weapon's: "Gold", the game's word, in the gold tint), the name and the
        // ammo, laid out in the disc (centre_lines).
        const spiral::Item& selected = m_wheel.items()[static_cast<std::size_t>(f.selected)];
        const std::string& name = m_names[static_cast<std::size_t>(f.selected)];
        bool empty = false;
        const std::string ammo = item_ammo(selected.id, empty);
        const bool gold = selected.category == spiral::look::Category::Gold;
        const std::vector<CentreLine> lines =
            centre_lines(f, gold ? gold_label(selected.id) : std::string(), name, ammo, empty ? kAmmoOut : kMenuBlue, selected.wrap);
        for (const CentreLine& l : lines) {
            line(l.size, static_cast<int>(kLineUnits), l.y, l.colour, l.text);
        }
        // Once an opening, for each item pointed at: how its name was measured and fitted.
        if (game.text_width && !name.empty()) {
            char key[96], text[320];
            std::snprintf(key, sizeof key, "fit %d %d", m_openings, selected.id);
            const int normal = game.text_width(TextSize::Normal, name.c_str());
            const int small = game.text_width(TextSize::Small, name.c_str());
            int lines_of_name = 0;
            const char* font = "normal";
            for (std::size_t i = (gold ? 1 : 0); i < lines.size() - (ammo.empty() ? 0 : 1); ++i) {
                ++lines_of_name;
                font = lines[i].size == TextSize::Small ? "small" : "normal";
            }
            std::snprintf(text, sizeof text,
                          "the name \"%s\": %d units normal, %d small (%.4f / %.4f screen heights), the disc's room at "
                          "its middle %d units; drawn in %d line(s), %s",
                          name.c_str(), normal, small, normal / kAcrossPerHeight, small / kAcrossPerHeight,
                          centre_room(f, f.centre_y), lines_of_name, font);
            log_once(key, text);
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
