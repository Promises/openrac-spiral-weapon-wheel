// SPDX-License-Identifier: GPL-3.0-or-later
//
// A level's HUD pictures from the disc data (hud_set.hpp).
#include "hud_set.hpp"
#include "wad.hpp"

#include <cstdio>
#include <cstring>

namespace gc_weapon_wheel::hud {

namespace {

constexpr std::size_t kRanges = 11;
constexpr int kHeaderRange = 4, kFirstBankRange = 5, kBanks = 5;
constexpr std::size_t kHeaderFixed = 0x54;  // through the textures' totals

template <typename T>
T at(const std::vector<std::uint8_t>& b, std::size_t offset) {
    T v{};
    if (offset + sizeof v <= b.size()) {
        std::memcpy(&v, b.data() + offset, sizeof v);
    }
    return v;
}

// A range of the data file, decompressed when it is WAD.
bool read_range(const Reader& read, std::int32_t offset, std::int32_t size, std::vector<std::uint8_t>& out) {
    if (offset < 0 || size <= 0) {
        return false;
    }
    std::vector<std::uint8_t> raw;
    if (!read(static_cast<std::uint64_t>(offset), static_cast<std::size_t>(size), raw) || raw.size() != static_cast<std::size_t>(size)) {
        return false;
    }
    if (is_wad(raw.data(), raw.size())) {
        return wad_decompress(raw.data(), raw.size(), out);
    }
    out = std::move(raw);
    return true;
}

}  // namespace

std::string level_data_path(const std::string& data_dir, const std::string& game, int level) {
    char nn[8];
    std::snprintf(nn, sizeof nn, "%02d", level);
    return data_dir + "/iso_data/" + game + "/lumps/levels/" + nn + "/data.bin";
}

int HudSet::bank_of(const std::array<std::uint32_t, 8>& totals, std::size_t i) {
    for (int b = 0; b < kBanks; ++b) {
        if (i < totals[static_cast<std::size_t>(b)]) {
            return b;
        }
    }
    return -1;
}

bool HudSet::load(const Reader& read, std::string& why) {
    *this = HudSet{};
    m_read = read;
    std::vector<std::uint8_t> table;
    if (!read(0, kRanges * 8, table) || table.size() != kRanges * 8) {
        why = "the level's data file cannot be read";
        return false;
    }
    std::vector<std::uint8_t> header;
    if (!read_range(read, at<std::int32_t>(table, kHeaderRange * 8), at<std::int32_t>(table, kHeaderRange * 8 + 4), header) ||
        header.size() < kHeaderFixed) {
        why = "no HUD header in the level's data";
        return false;
    }
    for (int b = 0; b < kBanks; ++b) {
        const std::size_t r = static_cast<std::size_t>(kFirstBankRange + b) * 8;
        m_bank_ranges[static_cast<std::size_t>(b)] = {at<std::int32_t>(table, r), at<std::int32_t>(table, r + 4)};
    }
    const std::uint16_t icon_count = at<std::uint16_t>(header, 0);
    const std::uint16_t frame_count = at<std::uint16_t>(header, 2);
    const std::uint32_t icon_offset = at<std::uint32_t>(header, 4);
    const std::uint32_t frame_offset = at<std::uint32_t>(header, 8);
    const std::uint32_t palette_offset = at<std::uint32_t>(header, 12);
    const std::uint32_t texture_offset = at<std::uint32_t>(header, 16);
    for (std::size_t i = 0; i < 8; ++i) {
        m_palette_total[i] = at<std::uint32_t>(header, 0x14 + 4 * i);
        m_texture_total[i] = at<std::uint32_t>(header, 0x34 + 4 * i);
    }
    auto fits = [&header](std::uint64_t offset, std::uint64_t count, std::uint64_t stride) {
        return offset + count * stride <= header.size();
    };
    if (!fits(icon_offset, icon_count, 8) || !fits(frame_offset, frame_count, 4) ||
        !fits(palette_offset, m_palette_total[4], 8) || !fits(texture_offset, m_texture_total[4], 8)) {
        why = "the HUD header's tables do not fit it";
        return false;
    }
    for (std::size_t i = 0; i < icon_count; ++i) {
        const std::size_t o = icon_offset + 8 * i;
        const std::uint16_t id = at<std::uint16_t>(header, o);
        if (id == 0xFFFF) {
            break;
        }
        m_icons.push_back({id, at<std::uint16_t>(header, o + 2), at<std::uint16_t>(header, o + 4)});
    }
    for (std::size_t i = 0; i < frame_count; ++i) {
        m_frames.push_back({at<std::int16_t>(header, frame_offset + 4 * i), at<std::int16_t>(header, frame_offset + 4 * i + 2)});
    }
    for (std::size_t i = 0; i < m_palette_total[4]; ++i) {
        m_palettes.push_back({at<std::uint32_t>(header, palette_offset + 8 * i) & 0x7FFFFFFFu});
    }
    for (std::size_t i = 0; i < m_texture_total[4]; ++i) {
        const std::size_t o = texture_offset + 8 * i;
        m_textures.push_back({at<std::uint32_t>(header, o) & 0x7FFFFFFFu, at<std::uint8_t>(header, o + 6), at<std::uint8_t>(header, o + 7)});
    }
    m_loaded = true;
    return true;
}

bool HudSet::bank(int b) const {
    if (b < 0 || b >= kBanks) {
        return false;
    }
    const std::size_t i = static_cast<std::size_t>(b);
    if (!m_tried[i]) {
        m_tried[i] = true;
        if (!read_range(m_read, m_bank_ranges[i].first, m_bank_ranges[i].second, m_banks[i])) {
            m_banks[i].clear();
        }
    }
    return !m_banks[i].empty();
}

bool HudSet::has_icon(int id) const {
    for (const Icon& icon : m_icons) {
        if (icon.id == id) {
            return true;
        }
    }
    return false;
}

int HudSet::icon_frame(int id, int k) const {
    for (const Icon& icon : m_icons) {
        if (icon.id == id) {
            return k >= 0 && k < icon.frame_count ? icon.first_frame + k : 0;
        }
    }
    return 0;
}

bool HudSet::frame_picture(int frame, Indexed& out) const {
    if (!m_loaded || frame < 0 || static_cast<std::size_t>(frame) >= m_frames.size()) {
        return false;
    }
    const auto [palette, texture] = m_frames[static_cast<std::size_t>(frame)];
    if (palette < 0 || texture < 0 || static_cast<std::size_t>(palette) >= m_palettes.size() ||
        static_cast<std::size_t>(texture) >= m_textures.size()) {
        return false;
    }
    const int pb = bank_of(m_palette_total, static_cast<std::size_t>(palette));
    const int tb = bank_of(m_texture_total, static_cast<std::size_t>(texture));
    if (!bank(pb) || !bank(tb)) {
        return false;
    }
    const Page& t = m_textures[static_cast<std::size_t>(texture)];
    const int w = 1 << (t.log2_width & 15), h = 1 << (t.log2_height & 15);
    const std::vector<std::uint8_t>& tbytes = m_banks[static_cast<std::size_t>(tb)];
    const std::vector<std::uint8_t>& pbytes = m_banks[static_cast<std::size_t>(pb)];
    const std::uint32_t poff = m_palettes[static_cast<std::size_t>(palette)].offset;
    if (w > 256 || h > 256 || static_cast<std::uint64_t>(t.offset) + static_cast<std::uint64_t>(w * h) > tbytes.size() ||
        static_cast<std::uint64_t>(poff) + 1024 > pbytes.size()) {
        return false;
    }
    out.width = w;
    out.height = h;
    out.indices.assign(tbytes.begin() + t.offset, tbytes.begin() + t.offset + w * h);
    // CSM1: within each 32 entries, 8-15 and 16-23 are swapped.
    for (int i = 0; i < 256; ++i) {
        const int stored = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        std::memcpy(out.palette.data() + 4 * i, pbytes.data() + poff + 4 * static_cast<std::uint32_t>(stored), 4);
    }
    return true;
}

}  // namespace gc_weapon_wheel::hud
