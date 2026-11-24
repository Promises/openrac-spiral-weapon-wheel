// SPDX-License-Identifier: GPL-3.0-or-later
//
// The wheel's panels as pictures (look.hpp, the user's design): each slot a menu panel bent into
// its sector (scanlines, the category's wash, the khaki frame graded dark to light, a glint), its
// glyph with a drop shadow, its badge; the round middle; the pointer. Everything is quads of a few
// pictures the mod makes as it loads (nothing shipped): the scanlines, white, the badge and the
// pointer, and the items' glyphs. Game-agnostic: a game's overlay draws the quads in order.
#pragma once

#include <vector>

#include "glyph.hpp"
#include "wheel.hpp"

namespace spiral {

enum class Art {
    Scan,     // the scanlines: a column, the design's 3 pixel period, `v` down the wheel
    White,    // white: the washes, the frames and the glints, by their colour
    Badge,    // the quick select number's badge (its number is the game's text)
    Pointer,  // the pointer's triangle, its tip up
    Glyph,    // an item's glyph (`image`), white: its colour is the quad's
    Image,    // a picture of the layer's (`image`): the game's own panel textures (PanelArt)
    Noise,    // the flash's stand-in: grey noise, kNoiseTexels square, when the game's is not there
    Digit,    // a badge's number (`image`: 1 to 9), white: its colour is the quad's
};

// A badge's number as a picture (make_digit): strokes in a kDigitWidth x kDigitHeight box, a design
// pixel of room around, kArtDensity pixels a design pixel, white.
Picture make_digit(int digit);

// How a quad goes over the frame: blended by its alpha, or added (the flash of static).
enum class Blend { Alpha, Add };

// A picture stretched over four corners (top left, top right, bottom left, bottom right) in screen
// heights from the screen's middle (y down), each at its place `uv` in the picture (0 to 1 from its
// top left), times `rgba` (0 to 1 each).
struct Quad {
    Art art = Art::White;
    int image = 0;  // Art::Glyph, Art::Image: the overlay picture
    Blend blend = Blend::Alpha;
    float xy[8] = {};
    float uv[8] = {};
    float rgba[4] = {1, 1, 1, 1};
};

// The quads of a frame, in the order drawn.
std::vector<Quad> paint(const Frame& frame);

// The pictures the quads use but the glyphs (RGBA, straight alpha), made once as the mod loads.
Picture make_art(Art which);

// The scanline picture's reach: it covers this many of the design's pixels down, centred on the
// wheel's middle (the wheel at its largest size, 1.5, fits), in this many rows.
constexpr float kScanSpan = 1200.0f;
constexpr int kScanRows = 4000;
// The badge's and the pointer's pictures: this many pixels a design pixel.
constexpr int kArtDensity = 4;

}  // namespace spiral
