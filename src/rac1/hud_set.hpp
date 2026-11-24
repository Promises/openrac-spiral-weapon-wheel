// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Ratchet & Clank level's HUD pictures, read by the mod itself from the player's own disc data
// (iso_data/rac1/lumps/levels/NN/data.bin, through the port's read-only game_data_dir): the
// header lump and its banks, decompressed into the mod's own memory, so the items' icons are there
// whenever the mod wants them (the game's copies are only there while its pause menu is open:
// rac1_spiral.cpp). Nothing is written, nothing of the game's state is touched.
//
// The format (OpenRAC's port/assets/world/hud.h, after ReRAC's docs/plan/hud_text.md section 1):
// the level's data container starts with 11 byte ranges {s32 offset, s32 size}: the HUD header is
// the 5th, its banks the 6th to 10th (WAD-compressed, or stored). The header: {u16 icon_count, u16
// frame_count, u32 icon_offset, frame_offset, palette_offset, texture_offset}, then the palettes'
// and the textures' cumulative counts per bank (8 each, at +0x14 and +0x34). Icons {u16 id, u16
// frame_count, u16 first_frame, u8 anim_mode, u8 ticks_per_frame} end with id 0xffff; frames {s16
// palette, s16 texture}; palettes {u32 offset | 0x80000000, u16 cbp, pad}: 256 RGBA32 entries in
// CSM1 order at `offset` in their bank; textures {u32 offset | 0x80000000, u16 tbp, u8 log2 width,
// u8 log2 height}: 8-bit indices. Bank b owns palettes total[b - 1] up to total[b], and the same
// for textures. The item icons' frames are in banks 0 and 1 (their palettes all in bank 1).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gc_weapon_wheel::hud {

// Reads `size` bytes at `offset` of the level's data file into `out`; false when it cannot.
using Reader = std::function<bool(std::uint64_t offset, std::size_t size, std::vector<std::uint8_t>& out)>;

// One frame's picture: its indices, row by row, and its palette in index order (CSM1 undone), RGBA
// with the GS's alpha (0x80 opaque).
struct Indexed {
    int width = 0, height = 0;
    std::vector<std::uint8_t> indices;
    std::array<std::uint8_t, 1024> palette{};
};

class HudSet {
public:
    // The level's HUD set from its data file; false (and `why` says) when it cannot be read.
    bool load(const Reader& read, std::string& why);
    bool loaded() const { return m_loaded; }

    // GetIconFrame(id, k) with every bank there: the icon's first frame + k when it has the icon
    // and k is one of its frames, else frame 0 (as the game's).
    int icon_frame(int id, int k) const;
    // Whether the set has icon `id`.
    bool has_icon(int id) const;
    // Frame `frame`'s picture; false when it has none (no texture or palette, a bank missing).
    bool frame_picture(int frame, Indexed& out) const;

private:
    struct Icon {
        std::uint16_t id, frame_count, first_frame;
    };
    struct Page {
        std::uint32_t offset;  // in its bank (the flag cleared)
        std::uint8_t log2_width = 0, log2_height = 0;
    };
    bool bank(int b) const;  // decompressed on first use
    static int bank_of(const std::array<std::uint32_t, 8>& totals, std::size_t i);

    Reader m_read;
    bool m_loaded = false;
    std::vector<Icon> m_icons;
    std::vector<std::pair<std::int16_t, std::int16_t>> m_frames;  // {palette, texture}
    std::vector<Page> m_palettes, m_textures;
    std::array<std::uint32_t, 8> m_palette_total{}, m_texture_total{};
    std::array<std::pair<std::int32_t, std::int32_t>, 5> m_bank_ranges{};
    mutable std::array<std::vector<std::uint8_t>, 5> m_banks;
    mutable std::array<bool, 5> m_tried{};
};

// The level's data file under a game's data folder (game_data_dir): iso_data/<game>/lumps/levels/
// NN/data.bin, NN the level's number in two digits.
std::string level_data_path(const std::string& data_dir, const std::string& game, int level);

}  // namespace gc_weapon_wheel::hud
