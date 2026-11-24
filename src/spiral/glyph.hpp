// SPDX-License-Identifier: GPL-3.0-or-later
//
// The items' glyphs, made at run time from the player's own game (nothing of the game's is shipped):
// a HUD icon (a flat dark glyph on a coloured, gently graded square) decoded, its ground keyed out
// by brightness with a soft edge, cropped to the glyph, and redrawn larger from a signed distance
// field, so its edges are smooth at any size; and the ground's colour (one of the checks).
// Game-agnostic: the game's layer reads the icon's indices and palette from game memory.
#pragma once

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

// An 8-bit indexed picture (the PS2's PSMT8) with its 256-colour RGBA32 palette as the GS stores it
// (CSM1: entries 8-15 and 16-23 of each 32 swapped), alpha 0 to 0x80 (0x80 opaque), as RGBA.
Picture decode_psmt8(const std::uint8_t* indices, int width, int height, const std::uint8_t* palette);

// What make_glyph made: the glyph (its own near-black colour, alpha its shape) and the ground's
// colour. `ok` false when the picture does not pass as an icon: a near-black glyph well below a
// coloured ground, the edge ring mostly ground and smooth, the picture mostly smooth (noise is
// not), the glyph covering 5 % to 70 % of it; `reason` the check it failed.
struct Glyph {
    bool ok = false;
    Picture picture;
    std::uint8_t tint[3] = {0, 0, 0};
    bool tint_from_corner = false;  // the top-right corner was ground (else the ring's brightest)
    // Why it is not one (ok false): the checks a decoded icon must pass before it is used, so that
    // a picture read from memory the game has since used for something else is never shown.
    const char* reason = "";
};

// The glyph of `icon` (an RGBA picture at least 8 x 8), `scale` times its cropped size.
Glyph make_glyph(const Picture& icon, int scale = 4);

}  // namespace spiral
