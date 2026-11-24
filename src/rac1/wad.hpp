// SPDX-License-Identifier: GPL-3.0-or-later
//
// WAD decompression (the games' LZ format), for the mod's own read of a level's HUD pictures from
// the player's disc data. Adapted from OpenRAC's port/assets/disc/wad.cpp (GPL-3.0-or-later,
// Copyright (c) 2026 the OpenRAC contributors), itself adapted from ReRAC
// (https://github.com/re-rac/rerac), crates/rc-formats/src/wad.rs and docs/formats/disc_layout.md
// section 3 (ISC License, Copyright (c) 2026 ReRAC contributors: THIRD_PARTY_NOTICES.md).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gc_weapon_wheel::hud {

// The WAD header: "WAD", the compressed size (its own 16 bytes included) at +3, then packets.
constexpr std::size_t kWadHeaderSize = 0x10;

bool is_wad(const std::uint8_t* bytes, std::size_t size);

// The stream decompressed into `out`; false (and `out` cut short) for a damaged one.
bool wad_decompress(const std::uint8_t* bytes, std::size_t size, std::vector<std::uint8_t>& out);

}  // namespace gc_weapon_wheel::hud
