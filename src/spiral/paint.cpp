// SPDX-License-Identifier: GPL-3.0-or-later
//
// The wheel's panels as quads (paint.hpp).
#include "paint.hpp"
#include "look.hpp"

#include <algorithm>
#include <cmath>

namespace spiral {

namespace {

constexpr float kDeg = kPi / 180.0f;

struct Painter {
    const Frame& f;
    std::vector<Quad>& out;

    // A design-pixel place around the wheel's middle (radius, angle in degrees counter-clockwise
    // from 3 o'clock) on the screen.
    void at(float radius, float deg, float& x, float& y) const {
        x = std::cos(deg * kDeg) * radius * f.unit;
        y = f.centre_y - std::sin(deg * kDeg) * radius * f.unit;
    }
    static void colour(Quad& q, const look::Rgb& c, float alpha) {
        q.rgba[0] = static_cast<float>(c.r) / 255.0f;
        q.rgba[1] = static_cast<float>(c.g) / 255.0f;
        q.rgba[2] = static_cast<float>(c.b) / 255.0f;
        q.rgba[3] = alpha;
    }
    // The scanlines' v for a place: down the wheel from its middle (the pattern keeps to the
    // screen's rows, as the design's does, wherever the panel is).
    float scan_v(float y) const { return 0.5f + (y - f.centre_y) / f.unit / kScanSpan; }

    // A ring's piece from `inner` to `outer` (design pixels) between two angles, as quads of a few
    // degrees, in `art` (Scan: its rows by place; White: by `c`).
    void ring(float inner, float outer, float from_deg, float to_deg, Art art, const look::Rgb& c, float alpha) {
        ring_mapped(inner, outer, from_deg, to_deg, art, 0, c, alpha, Blend::Alpha, [this, art](float, float y, float, float) {
            return std::pair<float, float>{0.5f, art == Art::Scan ? scan_v(y) : 0.5f};
        });
    }

    // The same with each corner's place in the picture from `uv(x, y, along, across)`: its place on
    // the screen, and its fractions along the piece (0 at `from_deg`) and across it (0 at `outer`).
    template <typename Uv>
    void ring_mapped(float inner, float outer, float from_deg, float to_deg, Art art, int image, const look::Rgb& c, float alpha,
                     Blend blend, Uv uv) {
        if (alpha <= 0.0f || outer <= inner) {
            return;
        }
        const float length = std::fabs(to_deg - from_deg) * kDeg * outer;
        const int pieces = std::clamp(static_cast<int>(std::ceil(length / 24.0f)), 1, 96);
        for (int i = 0; i < pieces; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(pieces);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(pieces);
            const float a0 = from_deg + (to_deg - from_deg) * t0, a1 = from_deg + (to_deg - from_deg) * t1;
            Quad q;
            q.art = art;
            q.image = image;
            q.blend = blend;
            at(outer, a0, q.xy[0], q.xy[1]);
            at(outer, a1, q.xy[2], q.xy[3]);
            at(inner, a0, q.xy[4], q.xy[5]);
            at(inner, a1, q.xy[6], q.xy[7]);
            const float along[4] = {t0, t1, t0, t1}, across[4] = {0, 0, 1, 1};
            for (int k = 0; k < 4; ++k) {
                const auto [u, v] = uv(q.xy[2 * k], q.xy[2 * k + 1], along[k], across[k]);
                q.uv[2 * k] = u;
                q.uv[2 * k + 1] = v;
            }
            colour(q, art == Art::Scan ? look::Rgb{255, 255, 255} : c, alpha);
            out.push_back(q);
        }
    }

    // A game pixel, in screen heights.
    float game_pixel() const { return 1.0f / f.lines; }

    // The panel's layers over the ring piece (look.hpp: the game's, or the stand-ins without its
    // pictures), below the wash: the fill, the vignette. Over the wash: the flash, the scanlines.
    // `from_deg` is the panel's counter-clockwise side (its pictures' left, as a panel at the top
    // of the wheel shows them), `to_deg` its clockwise side.
    void panel_below(float inner, float outer, float from_deg, float to_deg, float alpha) {
        if (f.art.scan == 0) {
            ring(inner, outer, from_deg, to_deg, Art::Scan, {255, 255, 255}, alpha);  // 1.3.0's stand-in
            return;
        }
        ring(inner, outer, from_deg, to_deg, Art::White, look::kPanelFill, alpha);
        if (f.art.vignette != 0) {
            ring_mapped(inner, outer, from_deg, to_deg, Art::Image, f.art.vignette, {255, 255, 255}, alpha, Blend::Alpha,
                        [](float, float, float along, float across) { return std::pair<float, float>{along, across}; });
        }
    }
    void panel_above(float inner, float outer, float from_deg, float to_deg, float alpha, float flash, int flash_u, int flash_v) {
        if (f.art.scan == 0) {
            return;
        }
        if (flash > 0.0f) {
            // One texel a game pixel from the panel's top left, at the flash's offset.
            float x0 = 1e9f, y0 = 1e9f;
            for (int k = 0; k <= 8; ++k) {
                for (const float r : {inner, outer}) {
                    float x, y;
                    at(r, from_deg + (to_deg - from_deg) * static_cast<float>(k) / 8.0f, x, y);
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                }
            }
            const float texel = game_pixel() * static_cast<float>(look::kNoiseTexels);
            ring_mapped(inner, outer, from_deg, to_deg, f.art.noise != 0 ? Art::Image : Art::Noise, f.art.noise, {255, 255, 255},
                        flash * alpha, Blend::Add, [&](float x, float y, float, float) {
                            return std::pair<float, float>{(x - x0) / texel + static_cast<float>(flash_u) / look::kNoiseTexels,
                                                           (y - y0) / texel + static_cast<float>(flash_v) / look::kNoiseTexels};
                        });
        }
        // The scanlines by the screen's rows: the column covers the screen's height, top down.
        ring_mapped(inner, outer, from_deg, to_deg, Art::Image, f.art.scan,
                    {static_cast<std::uint8_t>(255.0f * look::kScanColour), static_cast<std::uint8_t>(255.0f * look::kScanColour),
                     static_cast<std::uint8_t>(255.0f * look::kScanColour)},
                    look::kScanAlpha * alpha, Blend::Alpha,
                    [](float, float y, float, float) { return std::pair<float, float>{0.5f, y + 0.5f}; });
    }
    // The glint: the game's (FX 0x19 over the panel a game pixel in, its texels 1 to 63), or the
    // design's arc without it.
    void glint(float inner, float outer, float from_deg, float to_deg, float alpha) {
        if (f.art.glint != 0) {
            const float px = game_pixel() / f.unit;  // a game pixel in design pixels
            const float in = 1.0f / static_cast<float>(look::kGlintTexels), out = 1.0f - in;
            const float turn_out = px / outer / kDeg, sign = to_deg > from_deg ? 1.0f : -1.0f;
            ring_mapped(inner + px, outer - px, from_deg + sign * turn_out, to_deg - sign * turn_out, Art::Image, f.art.glint,
                        {255, 255, 255}, alpha, Blend::Alpha, [in, out](float, float, float along, float across) {
                            return std::pair<float, float>{in + (out - in) * along, in + (out - in) * across};
                        });
            return;
        }
        const float middle = (from_deg + to_deg) * 0.5f;
        const float g = outer - look::kGlintIn;
        ring(g - look::kGlintWidth * 0.5f, g + look::kGlintWidth * 0.5f, middle - look::kGlintTo, middle - look::kGlintFrom,
             Art::White, look::kGlint, look::kGlintAlpha * alpha);
    }

    // A straight stroke `width` wide from (r0, deg) to (r1, deg), its ends `width` / 2 past them.
    void spoke(float r0, float r1, float deg, float width, const look::Rgb& c, float alpha) {
        float x0, y0, x1, y1;
        at(r0 - width * 0.5f, deg, x0, y0);
        at(r1 + width * 0.5f, deg, x1, y1);
        // Across: perpendicular, half the width (screen heights).
        const float nx = std::sin(deg * kDeg) * width * 0.5f * f.unit;
        const float ny = std::cos(deg * kDeg) * width * 0.5f * f.unit;
        Quad q;
        q.art = Art::White;
        q.xy[0] = x1 - nx, q.xy[1] = y1 - ny;
        q.xy[2] = x1 + nx, q.xy[3] = y1 + ny;
        q.xy[4] = x0 - nx, q.xy[5] = y0 - ny;
        q.xy[6] = x0 + nx, q.xy[7] = y0 + ny;
        for (float& v : q.uv) {
            v = 0.5f;
        }
        colour(q, c, alpha);
        out.push_back(q);
    }

    // An axis-aligned picture centred at (x, y), `w` by `h` design pixels.
    void picture(Art art, int image, float x, float y, float w, float h, const look::Rgb& c, float alpha) {
        if (alpha <= 0.0f) {
            return;
        }
        const float hw = w * 0.5f * f.unit, hh = h * 0.5f * f.unit;
        Quad q;
        q.art = art;
        q.image = image;
        const float xy[8] = {x - hw, y - hh, x + hw, y - hh, x - hw, y + hh, x + hw, y + hh};
        const float uv[8] = {0, 0, 1, 0, 0, 1, 1, 1};
        std::copy(xy, xy + 8, q.xy);
        std::copy(uv, uv + 8, q.uv);
        colour(q, c, alpha);
        out.push_back(q);
    }

    // The frame's four strokes along a panel's edge, widest first: both arcs and both sides.
    void frame(float inner, float outer, float middle_deg, const look::Band (&bands)[4], float alpha) {
        for (const look::Band& b : bands) {
            const float half = b.width * 0.5f;
            const float out_turn = half / outer / kDeg, in_turn = half / inner / kDeg;
            ring(outer - half, outer + half, middle_deg - look::kHalfAngle - out_turn,
                 middle_deg + look::kHalfAngle + out_turn, Art::White, b.colour, alpha);
            ring(inner - half, inner + half, middle_deg - look::kHalfAngle - in_turn,
                 middle_deg + look::kHalfAngle + in_turn, Art::White, b.colour, alpha);
            spoke(inner, outer, middle_deg - look::kHalfAngle, b.width, b.colour, alpha);
            spoke(inner, outer, middle_deg + look::kHalfAngle, b.width, b.colour, alpha);
        }
    }

    void slot(const Slot& s) {
        const float alpha = s.opacity;
        const float half = look::kThickness * s.radius * 0.5f;
        const float inner = s.radius - half + s.push, outer = s.radius + half + s.push;
        const float a0 = s.angle_deg - look::kHalfAngle, a1 = s.angle_deg + look::kHalfAngle;
        // The panel (look.hpp), the category's wash between its layers (none for an empty slot),
        // its glint, its frame.
        panel_below(inner, outer, a1, a0, alpha);
        if (!s.empty) {
            ring(inner, outer, a0, a1, Art::White, s.wash, (s.selected ? look::kWashSelected : look::kWash) * alpha);
        }
        panel_above(inner, outer, a1, a0, alpha, s.flash, s.flash_u, s.flash_v);
        glint(inner, outer, a1, a0, alpha);
        frame(inner, outer, s.angle_deg, s.selected ? look::kFrameSelected : look::kFrame, alpha);
        // The glyph, upright at the panel's middle, the larger side the square's; its shadow first.
        if (!s.empty && s.glyph != 0 && s.glyph_width > 0 && s.glyph_height > 0) {
            const float box = 2.0f * half * (s.selected ? look::kGlyphSelected : look::kGlyph);
            const float larger = static_cast<float>(std::max(s.glyph_width, s.glyph_height));
            const float w = box * static_cast<float>(s.glyph_width) / larger;
            const float h = box * static_cast<float>(s.glyph_height) / larger;
            float x, y;
            at(s.radius + s.push, s.angle_deg, x, y);
            picture(Art::Glyph, s.glyph, x + look::kShadowX * f.unit, y + look::kShadowY * f.unit, w, h, look::kShadow, alpha);
            picture(Art::Glyph, s.glyph, x, y, w, h, s.selected ? s.selected_colour : s.glyph_colour, alpha);
        }
    }

    void badges() {
        // The badge's picture has a design pixel of room each side for its edge.
        for (const Badge& b : f.badges) {
            picture(Art::Badge, 0, b.x, b.y, look::kBadgeWidth + 2.0f, look::kBadgeHeight + 2.0f, {255, 255, 255}, b.alpha);
            if (b.number >= 1 && b.number <= 9) {
                picture(Art::Digit, b.number, b.x, b.y, look::kDigitWidth + 2.0f, look::kDigitHeight + 2.0f, look::kBadgeDigit, b.alpha);
            }
        }
    }

    // A disc of radius `r` (design pixels) about the wheel's middle as horizontal strips (quads with
    // their corners on the circle: no thin wedges meeting at the middle), each corner's place in
    // the picture from `uv(x, y)`.
    template <typename Uv>
    void disc(float r, Art art, int image, const look::Rgb& c, float alpha, Blend blend, Uv uv) {
        if (alpha <= 0.0f) {
            return;
        }
        const int strips = 48;
        for (int i = 0; i < strips; ++i) {
            // Strip i between the angles where the circle crosses its top and bottom (y up).
            const float t0 = std::cos(kPi * static_cast<float>(i) / strips), t1 = std::cos(kPi * static_cast<float>(i + 1) / strips);
            const float h0 = std::sqrt(std::max(0.0f, 1.0f - t0 * t0)), h1 = std::sqrt(std::max(0.0f, 1.0f - t1 * t1));
            Quad q;
            q.art = art;
            q.image = image;
            q.blend = blend;
            const float u = f.unit;
            const float xy[8] = {-h0 * r * u, f.centre_y - t0 * r * u, h0 * r * u, f.centre_y - t0 * r * u,
                                 -h1 * r * u, f.centre_y - t1 * r * u, h1 * r * u, f.centre_y - t1 * r * u};
            std::copy(xy, xy + 8, q.xy);
            for (int k = 0; k < 4; ++k) {
                const auto [qu, qv] = uv(q.xy[2 * k], q.xy[2 * k + 1]);
                q.uv[2 * k] = qu;
                q.uv[2 * k + 1] = qv;
            }
            colour(q, c, alpha);
            out.push_back(q);
        }
    }

    void middle() {
        const float r = look::kCentreRadius;
        // The same panel as the slots' (the design fills the middle as it fills them): the game's
        // fill, vignette over the disc's square, flash and scanlines, and its glint's L-shaped
        // corners on the square inside the disc; without the game's pictures, 1.3.0's stand-in
        // scanlines and the design's arcs.
        const look::Rgb white = {255, 255, 255};
        const float half = r * f.unit;
        if (f.art.scan == 0) {
            disc(r, Art::Scan, 0, white, 1.0f, Blend::Alpha, [this](float, float y) { return std::pair<float, float>{0.5f, scan_v(y)}; });
        } else {
            disc(r, Art::White, 0, look::kPanelFill, 1.0f, Blend::Alpha, [](float, float) { return std::pair<float, float>{0.5f, 0.5f}; });
            if (f.art.vignette != 0) {
                disc(r, Art::Image, f.art.vignette, white, 1.0f, Blend::Alpha, [this, half](float x, float y) {
                    return std::pair<float, float>{(x + half) / (2.0f * half), (y - f.centre_y + half) / (2.0f * half)};
                });
            }
            if (f.centre_flash > 0.0f) {
                const float texel = game_pixel() * static_cast<float>(look::kNoiseTexels);
                const float fu = static_cast<float>(f.centre_flash_u) / look::kNoiseTexels;
                const float fv = static_cast<float>(f.centre_flash_v) / look::kNoiseTexels;
                disc(r, f.art.noise != 0 ? Art::Image : Art::Noise, f.art.noise, white, f.centre_flash, Blend::Add,
                     [this, half, texel, fu, fv](float x, float y) {
                         return std::pair<float, float>{(x + half) / texel + fu, (y - f.centre_y + half) / texel + fv};
                     });
            }
            disc(r, Art::Image, f.art.scan,
                 {static_cast<std::uint8_t>(255.0f * look::kScanColour), static_cast<std::uint8_t>(255.0f * look::kScanColour),
                  static_cast<std::uint8_t>(255.0f * look::kScanColour)},
                 look::kScanAlpha, Blend::Alpha, [](float, float y) { return std::pair<float, float>{0.5f, y + 0.5f}; });
        }
        if (f.art.glint != 0) {
            // The square inside the disc, a game pixel in, its texels 1 to 63.
            const float s2 = half / std::sqrt(2.0f) - game_pixel();
            const float in = 1.0f / static_cast<float>(look::kGlintTexels), out_uv = 1.0f - in;
            Quad q;
            q.art = Art::Image;
            q.image = f.art.glint;
            const float xy[8] = {-s2, f.centre_y - s2, s2, f.centre_y - s2, -s2, f.centre_y + s2, s2, f.centre_y + s2};
            const float uv[8] = {in, in, out_uv, in, in, out_uv, out_uv, out_uv};
            std::copy(xy, xy + 8, q.xy);
            std::copy(uv, uv + 8, q.uv);
            colour(q, white, 1.0f);
            out.push_back(q);
        }
        for (const look::Band& b : look::kFrameCentre) {
            ring(r - b.width * 0.5f, r + b.width * 0.5f, 0.0f, 360.0f, Art::White, b.colour, 1.0f);
        }
        if (f.art.glint == 0) {
            const float g = look::kCentreGlintRadius, w = look::kCentreGlintWidth * 0.5f;
            ring(g - w, g + w, -70.0f, -20.0f, Art::White, look::kCentreGlintLight, look::kCentreGlintLightAlpha);
            ring(g - w, g + w, 115.0f, 160.0f, Art::White, look::kCentreGlintDim, look::kCentreGlintDimAlpha);
        }
    }

    void pointer() {
        // The triangle's picture: its tip up, a stroke's room around; centred between the tip and
        // the base, turned so that up is out along the pointer.
        const float half = look::kPointerHalfAngle * kDeg;
        const float base_in = look::kPointerBase * std::cos(half);
        const float w = 2.0f * look::kPointerBase * std::sin(half) + 2.0f * look::kPointerStroke;
        const float h = look::kPointerTip - base_in + 2.0f * look::kPointerStroke;
        float cx, cy;
        at((look::kPointerTip + base_in) * 0.5f, f.pointer_deg, cx, cy);
        const float ox = std::cos(f.pointer_deg * kDeg), oy = -std::sin(f.pointer_deg * kDeg);  // out
        const float rx = -oy, ry = ox;                                                       // its right
        const float hw = w * 0.5f * f.unit, hh = h * 0.5f * f.unit;
        Quad q;
        q.art = Art::Pointer;
        const float xy[8] = {cx + ox * hh - rx * hw, cy + oy * hh - ry * hw, cx + ox * hh + rx * hw, cy + oy * hh + ry * hw,
                             cx - ox * hh - rx * hw, cy - oy * hh - ry * hw, cx - ox * hh + rx * hw, cy - oy * hh + ry * hw};
        const float uv[8] = {0, 0, 1, 0, 0, 1, 1, 1};
        std::copy(xy, xy + 8, q.xy);
        std::copy(uv, uv + 8, q.uv);
        out.push_back(q);
    }
};

// Signed distance to a rounded box of half sizes (hx, hy) and corner radius r, at (x, y) from its
// middle (negative inside).
float rounded_box(float x, float y, float hx, float hy, float r) {
    const float qx = std::fabs(x) - hx + r, qy = std::fabs(y) - hy + r;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Signed distance to a triangle (negative inside).
float triangle(float px, float py, const float (&v)[6]) {
    float d = 1e9f;
    float s = 1.0f;
    for (int i = 0; i < 3; ++i) {
        const float ax = v[2 * i], ay = v[2 * i + 1];
        const float bx = v[(2 * i + 2) % 6], by = v[(2 * i + 3) % 6];
        const float ex = bx - ax, ey = by - ay, wx = px - ax, wy = py - ay;
        const float t = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
        const float dx = wx - ex * t, dy = wy - ey * t;
        d = std::min(d, dx * dx + dy * dy);
        // Inside when on the same side of every edge (the vertices go one way round).
        if (ex * wy - ey * wx < 0.0f) {
            s = -1.0f;
        }
    }
    return std::sqrt(d) * (s > 0.0f ? -1.0f : 1.0f);
}

std::uint8_t byte(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

void put(Picture& p, int x, int y, float r, float g, float b, float a) {
    std::uint8_t* o = &p.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width) + static_cast<std::size_t>(x)) * 4];
    o[0] = byte(r), o[1] = byte(g), o[2] = byte(b), o[3] = byte(a);
}

float c(std::uint8_t v) { return static_cast<float>(v) / 255.0f; }

}  // namespace

std::vector<Quad> paint(const Frame& frame) {
    std::vector<Quad> out;
    Painter p{frame, out};
    for (const Slot& s : frame.slots) {
        p.slot(s);
    }
    p.badges();
    p.middle();
    p.pointer();
    return out;
}

Picture make_digit(int digit) {
    // Each digit as strokes in a 6 x 9 box (x right, y down), drawn round-capped.
    struct Line {
        float x0, y0, x1, y1;
    };
    static const std::vector<std::vector<Line>> kStrokes = {
        {},
        {{1.5f, 2, 3.5f, 0}, {3.5f, 0, 3.5f, 9}, {1.5f, 9, 5.5f, 9}},                                     // 1
        {{0.5f, 1.5f, 2, 0}, {2, 0, 4.5f, 0}, {4.5f, 0, 6, 1.5f}, {6, 1.5f, 6, 3}, {6, 3, 0, 9}, {0, 9, 6, 9}},  // 2
        {{0, 0, 6, 0}, {6, 0, 3, 3.5f}, {3, 3.5f, 5, 3.5f}, {5, 3.5f, 6, 5}, {6, 5, 6, 7.5f}, {6, 7.5f, 4.5f, 9},
         {4.5f, 9, 1.5f, 9}, {1.5f, 9, 0, 7.5f}},  // 3
        {{4.5f, 9, 4.5f, 0}, {4.5f, 0, 0, 6}, {0, 6, 6, 6}},                                               // 4
        {{6, 0, 0.5f, 0}, {0.5f, 0, 0, 4}, {0, 4, 4.5f, 4}, {4.5f, 4, 6, 5.5f}, {6, 5.5f, 6, 7.5f}, {6, 7.5f, 4.5f, 9},
         {4.5f, 9, 1, 9}, {1, 9, 0, 8}},  // 5
        {{5, 0, 2, 0}, {2, 0, 0, 2.5f}, {0, 2.5f, 0, 7.5f}, {0, 7.5f, 1.5f, 9}, {1.5f, 9, 4.5f, 9}, {4.5f, 9, 6, 7.5f},
         {6, 7.5f, 6, 5.5f}, {6, 5.5f, 4.5f, 4}, {4.5f, 4, 0, 4}},  // 6
        {{0, 0, 6, 0}, {6, 0, 2, 9}},                                                                       // 7
        {{1.5f, 0, 4.5f, 0}, {4.5f, 0, 5.5f, 1}, {5.5f, 1, 5.5f, 3}, {5.5f, 3, 4.5f, 4}, {4.5f, 4, 1.5f, 4},
         {1.5f, 4, 0.5f, 3}, {0.5f, 3, 0.5f, 1}, {0.5f, 1, 1.5f, 0}, {1.5f, 4, 0, 5.5f}, {0, 5.5f, 0, 7.5f},
         {0, 7.5f, 1.5f, 9}, {1.5f, 9, 4.5f, 9}, {4.5f, 9, 6, 7.5f}, {6, 7.5f, 6, 5.5f}, {6, 5.5f, 4.5f, 4}},  // 8
        {{6, 4.5f, 1.5f, 4.5f}, {1.5f, 4.5f, 0, 3}, {0, 3, 0, 1.5f}, {0, 1.5f, 1.5f, 0}, {1.5f, 0, 4.5f, 0},
         {4.5f, 0, 6, 1.5f}, {6, 1.5f, 6, 6.5f}, {6, 6.5f, 4, 9}, {4, 9, 1, 9}},  // 9
    };
    Picture p;
    const float d = static_cast<float>(kArtDensity);
    p.width = static_cast<int>((look::kDigitWidth + 2.0f) * d);
    p.height = static_cast<int>((look::kDigitHeight + 2.0f) * d);
    p.rgba.assign(static_cast<std::size_t>(p.width * p.height) * 4, 255);
    const std::vector<Line>& lines = kStrokes[static_cast<std::size_t>(std::clamp(digit, 0, 9))];
    for (int y = 0; y < p.height; ++y) {
        for (int x = 0; x < p.width; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) / d - 1.0f, fy = (static_cast<float>(y) + 0.5f) / d - 1.0f;
            float best = 1e9f;
            for (const Line& l : lines) {
                const float ex = l.x1 - l.x0, ey = l.y1 - l.y0;
                const float t = std::clamp(((fx - l.x0) * ex + (fy - l.y0) * ey) / std::max(1e-6f, ex * ex + ey * ey), 0.0f, 1.0f);
                best = std::min(best, std::hypot(fx - l.x0 - ex * t, fy - l.y0 - ey * t));
            }
            const float a = std::clamp(0.5f - (best - look::kDigitStroke * 0.5f) * d, 0.0f, 1.0f);
            p.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width) + static_cast<std::size_t>(x)) * 4 + 3] = byte(a);
        }
    }
    return p;
}

Picture make_art(Art which) {
    Picture p;
    switch (which) {
        case Art::Scan: {
            // A column: the dark rows, the light one 1.4 of each 3 (partly lit at its edge).
            p.width = 2;
            p.height = kScanRows;
            p.rgba.resize(static_cast<std::size_t>(p.width * p.height) * 4);
            const float px = kScanSpan / static_cast<float>(kScanRows);  // design pixels a row
            for (int y = 0; y < p.height; ++y) {
                const float top = std::fmod(static_cast<float>(y) * px, look::kScanPeriod);
                const float lit = std::clamp((look::kScanLight - top) / px, 0.0f, 1.0f);
                for (int x = 0; x < p.width; ++x) {
                    put(p, x, y, c(look::kScanDark.r) + (c(look::kScanLightColour.r) - c(look::kScanDark.r)) * lit,
                        c(look::kScanDark.g) + (c(look::kScanLightColour.g) - c(look::kScanDark.g)) * lit,
                        c(look::kScanDark.b) + (c(look::kScanLightColour.b) - c(look::kScanDark.b)) * lit, 1.0f);
                }
            }
            break;
        }
        case Art::Noise: {
            // Grey noise (the stand-in for the game's FX 0x1A), opaque: the flash adds it by its
            // own strength.
            p.width = p.height = look::kNoiseTexels;
            p.rgba.resize(static_cast<std::size_t>(p.width * p.height) * 4);
            std::uint32_t x = 0x2545F491u;
            for (int i = 0; i < p.width * p.height; ++i) {
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                const std::uint8_t g = static_cast<std::uint8_t>(x >> 24) / 2;
                put(p, i % p.width, i / p.width, g / 255.0f, g / 255.0f, g / 255.0f, 1.0f);
            }
            break;
        }
        case Art::Digit:
            return make_digit(1);
        case Art::White:
        case Art::Glyph:
        case Art::Image:
            p.width = p.height = 4;
            p.rgba.assign(64, 255);
            break;
        case Art::Badge: {
            // 24 x 18 with corners of 3, graded grey top to bottom, its 1.5 edge centred on its
            // outline; a design pixel of room around.
            const float d = static_cast<float>(kArtDensity);
            p.width = static_cast<int>((look::kBadgeWidth + 2.0f) * d);
            p.height = static_cast<int>((look::kBadgeHeight + 2.0f) * d);
            p.rgba.resize(static_cast<std::size_t>(p.width * p.height) * 4);
            const float edge = look::kBadgeEdgeWidth * 0.5f;
            for (int y = 0; y < p.height; ++y) {
                for (int x = 0; x < p.width; ++x) {
                    const float fx = (static_cast<float>(x) + 0.5f) / d - (look::kBadgeWidth + 2.0f) * 0.5f;
                    const float fy = (static_cast<float>(y) + 0.5f) / d - (look::kBadgeHeight + 2.0f) * 0.5f;
                    const float dist = rounded_box(fx, fy, look::kBadgeWidth * 0.5f, look::kBadgeHeight * 0.5f, look::kBadgeCorner);
                    const float t = std::clamp(fy / look::kBadgeHeight + 0.5f, 0.0f, 1.0f);
                    const float fill = std::clamp(0.5f - dist * d, 0.0f, 1.0f);           // inside the outline
                    const float stroke = std::clamp(0.5f - (std::fabs(dist) - edge) * d, 0.0f, 1.0f);
                    const float a = std::max(fill, stroke);
                    float r = c(look::kBadgeTop.r) + (c(look::kBadgeBottom.r) - c(look::kBadgeTop.r)) * t;
                    r = r * (1.0f - stroke) + c(look::kBadgeEdge.r) * stroke;
                    put(p, x, y, r, r, r, a);
                }
            }
            break;
        }
        case Art::Pointer: {
            // The triangle, tip up, filled highlight yellow, its 2 wide dark edge rounded at the
            // corners (a distance's), a stroke's room around.
            const float d = static_cast<float>(kArtDensity);
            const float half = look::kPointerHalfAngle * kDeg;
            const float hb = look::kPointerBase * std::sin(half);
            const float tall = look::kPointerTip - look::kPointerBase * std::cos(half);
            const float room = look::kPointerStroke;
            const float W = 2.0f * hb + 2.0f * room, H = tall + 2.0f * room;
            p.width = static_cast<int>(std::ceil(W * d));
            p.height = static_cast<int>(std::ceil(H * d));
            p.rgba.resize(static_cast<std::size_t>(p.width * p.height) * 4);
            const float v[6] = {W * 0.5f, room, W * 0.5f + hb, room + tall, W * 0.5f - hb, room + tall};
            for (int y = 0; y < p.height; ++y) {
                for (int x = 0; x < p.width; ++x) {
                    const float fx = (static_cast<float>(x) + 0.5f) / d * W / (static_cast<float>(p.width) / d);
                    const float fy = (static_cast<float>(y) + 0.5f) / d * H / (static_cast<float>(p.height) / d);
                    const float dist = triangle(fx, fy, v);
                    const float fill = std::clamp(0.5f - dist * d, 0.0f, 1.0f);
                    const float stroke = std::clamp(0.5f - (std::fabs(dist) - look::kPointerStroke * 0.5f) * d, 0.0f, 1.0f);
                    const float a = std::max(fill, stroke);
                    put(p, x, y, c(look::kHighlight.r) * (1.0f - stroke) + c(look::kPointerEdge.r) * stroke,
                        c(look::kHighlight.g) * (1.0f - stroke) + c(look::kPointerEdge.g) * stroke,
                        c(look::kHighlight.b) * (1.0f - stroke) + c(look::kPointerEdge.b) * stroke, a);
                }
            }
            break;
        }
    }
    return p;
}

}  // namespace spiral
