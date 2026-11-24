// SPDX-License-Identifier: GPL-3.0-or-later
//
// The items' glyphs, made at run time from the player's own game (nothing of the game's is shipped):
// a HUD icon (a flat dark glyph on a coloured, gently graded square) decoded, its ground keyed out
// in its palette (each colour's coverage by how grey it is), cropped to the glyph, and redrawn
// larger from a signed distance field, so its edges are smooth at any size; and the ground's colour
// (one of the checks). Game-agnostic: the game's layer reads the icon's indices and palette (from
// the player's disc data, or from game memory).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spiral {

// An RGBA picture, top row first, straight alpha.
struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// A 256-colour palette in index order: RGBA, alpha the GS's (0 to 0x80, 0x80 opaque).
using Palette = std::array<std::uint8_t, 1024>;

// A palette as the GS stores it (CSM1: entries 8-15 and 16-23 of each 32 swapped) in index order.
Palette palette_from_csm1(const std::uint8_t* stored);

// An 8-bit indexed picture (the PS2's PSMT8) as RGBA (alpha to 255).
Picture decode(const std::uint8_t* indices, int width, int height, const Palette& palette);

// The keying in the palette: an icon's ground is saturated colour (its entries' chroma, max - min
// of R, G and B, 85 and over: a blue, green or red gradient), its glyph near-neutral grey (12 and
// under), the edges blends of the two. So each entry's coverage is 1 - chroma / 100 (the ground's
// chroma), its bottom 15 % taken as ground and its top 8 % as glyph: (1 - chroma / 100 - 0.15) /
// 0.77, held to 0 to 1. One table a palette, then each pixel by its index: the icon's own
// anti-aliased edge and every grey part of the glyph stay (a highlight inside a glyph, if
// coloured, is a hole).
constexpr float kGroundChroma = 100.0f;
constexpr float kCoverFrom = 0.15f, kCoverTo = 0.92f;
constexpr int kGroundEntryChroma = 85;
std::array<float, 256> palette_cover(const Palette& palette);

// What make_glyph made: the glyph (its own near-black colour, alpha its shape) and the ground's
// colour (the mean of its ground entries as used). `ok` false when the picture does not pass as
// an icon: a coloured ground, a near-black glyph well below it, the edge ring mostly ground and
// smooth, the picture mostly smooth (noise is not), the glyph covering 5 % to 70 % of it; `reason`
// the check it failed.
struct Glyph {
    bool ok = false;
    Picture picture;
    std::uint8_t tint[3] = {0, 0, 0};
    // Why it is not one (ok false): the checks a decoded icon must pass before it is used, so that
    // a picture read from memory the game has since used for something else is never shown.
    const char* reason = "";
};

// The glyph of an indexed icon (`width` x `height` indices, at least 8 x 8, and their palette),
// keyed in the palette (palette_cover), cropped, and `scale` times its cropped size.
Glyph make_glyph(const std::uint8_t* indices, int width, int height, const Palette& palette, int scale = 4);

}  // namespace spiral
