// SPDX-License-Identifier: GPL-3.0-or-later
//
// The items' glyphs (glyph.hpp).
#include "glyph.hpp"

#include <algorithm>
#include <cmath>

namespace spiral {

namespace {

int csm1(int i) { return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); }

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::uint8_t median(std::vector<std::uint8_t> v) {
    if (v.empty()) {
        return 0;
    }
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

}  // namespace

Picture decode_psmt8(const std::uint8_t* indices, int width, int height, const std::uint8_t* palette) {
    Picture p;
    p.width = width;
    p.height = height;
    p.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int i = 0; i < width * height; ++i) {
        const std::uint8_t* c = palette + 4 * csm1(indices[i]);
        std::uint8_t* o = &p.rgba[static_cast<std::size_t>(i) * 4];
        o[0] = c[0];
        o[1] = c[1];
        o[2] = c[2];
        o[3] = static_cast<std::uint8_t>(std::min(255, (c[3] * 255 + 64) / 128));
    }
    return p;
}

void shade(const std::uint8_t in[3], float value, float saturation, float out[3]) {
    const float r = in[0] / 255.0f, g = in[1] / 255.0f, b = in[2] / 255.0f;
    const float grey = 0.299f * r + 0.587f * g + 0.114f * b;
    const float c[3] = {grey + (r - grey) * saturation, grey + (g - grey) * saturation, grey + (b - grey) * saturation};
    for (int k = 0; k < 3; ++k) {
        out[k] = std::clamp(c[k] * value, 0.0f, 1.0f);
    }
}

Glyph make_glyph(const Picture& icon, int scale) {
    Glyph g;
    const int w = icon.width, h = icon.height;
    if (w < 8 || h < 8 || icon.rgba.size() < static_cast<std::size_t>(w * h * 4) || scale < 1) {
        return g;
    }
    auto px = [&](int x, int y) { return &icon.rgba[(static_cast<std::size_t>(y) * w + x) * 4]; };
    auto value = [&](int x, int y) {
        const std::uint8_t* p = px(x, y);
        return static_cast<int>(std::max({p[0], p[1], p[2]}));
    };
    // The ring one pixel in (past a border): the ground's brightness is the median of its
    // brighter half.
    std::vector<std::pair<int, int>> ring;
    for (int x = 1; x < w - 1; ++x) {
        ring.push_back({x, 1});
        ring.push_back({x, h - 2});
    }
    for (int y = 2; y < h - 2; ++y) {
        ring.push_back({1, y});
        ring.push_back({w - 2, y});
    }
    std::vector<std::uint8_t> ring_v;
    for (const auto& [x, y] : ring) {
        ring_v.push_back(static_cast<std::uint8_t>(value(x, y)));
    }
    const int ring_mid = median(ring_v);
    int ring_max = 0;
    std::vector<std::uint8_t> bright[3];
    std::vector<std::uint8_t> bright_v;
    for (const auto& [x, y] : ring) {
        const int v = value(x, y);
        ring_max = std::max(ring_max, v);
        if (v >= ring_mid) {
            for (int k = 0; k < 3; ++k) {
                bright[k].push_back(px(x, y)[k]);
            }
            bright_v.push_back(static_cast<std::uint8_t>(v));
        }
    }
    // The tint: the top-right 3 x 3 two pixels in, by its median; when that is glyph (dark), the
    // ring's brighter half's median.
    std::vector<std::uint8_t> corner[3];
    int corner_v = 0;
    for (int y = 2; y < 5; ++y) {
        for (int x = w - 5; x < w - 2; ++x) {
            for (int k = 0; k < 3; ++k) {
                corner[k].push_back(px(x, y)[k]);
            }
        }
    }
    for (int k = 0; k < 3; ++k) {
        g.tint[k] = median(corner[k]);
        corner_v = std::max(corner_v, static_cast<int>(g.tint[k]));
    }
    g.tint_from_corner = corner_v > ring_max / 2;
    if (!g.tint_from_corner) {
        for (int k = 0; k < 3; ++k) {
            g.tint[k] = median(bright[k]);
        }
    }
    // The glyph against the ground, by brightness: the ground's value (the ring's brighter half)
    // to the glyph's (the darkest 5 %), soft from 35 % to 75 % of the way.
    const float ground = median(bright_v);
    std::vector<int> all_v;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            all_v.push_back(value(x, y));
        }
    }
    std::nth_element(all_v.begin(), all_v.begin() + static_cast<std::ptrdiff_t>(all_v.size() / 20), all_v.end());
    const float dark = static_cast<float>(all_v[all_v.size() / 20]);
    if (ground - dark < 40.0f) {
        return g;
    }
    std::vector<float> cover(static_cast<std::size_t>(w * h));
    int x0 = w, y0 = h, x1 = -1, y1 = -1, solid = 0;
    std::vector<std::uint8_t> ink[3];
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float t = (ground - static_cast<float>(value(x, y))) / (ground - dark);
            const float c = smooth(0.35f, 0.75f, t) * (px(x, y)[3] / 255.0f);
            cover[static_cast<std::size_t>(y * w + x)] = c;
            if (c > 0.1f) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
            if (c > 0.9f) {
                ++solid;
                for (int k = 0; k < 3; ++k) {
                    ink[k].push_back(px(x, y)[k]);
                }
            }
        }
    }
    if (solid < 4 || x1 < x0) {
        return g;
    }
    // The crop, a pixel of room around it, `scale` times as big: the coverage bilinear, a mask at
    // its half, the distance (in the big grid's pixels) to the nearest pixel of the other side
    // within a radius, signed, then smoothed (two 3 x 3 means take out the staircase), and the
    // edge a smoothstep one big pixel wide either side of 0.
    constexpr int kRoom = 1;
    const int cx0 = x0 - kRoom, cy0 = y0 - kRoom;
    const int cw = (x1 - x0 + 1) + 2 * kRoom, ch = (y1 - y0 + 1) + 2 * kRoom;
    const int bw = cw * scale, bh = ch * scale;
    auto sample = [&](float sx, float sy) {
        // The coverage at a point of the crop (pixel centres at .5), outside the icon 0.
        sx -= 0.5f;
        sy -= 0.5f;
        const int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
        const float fx = sx - static_cast<float>(ix), fy = sy - static_cast<float>(iy);
        auto at = [&](int x, int y) {
            x += cx0;
            y += cy0;
            return x < 0 || y < 0 || x >= w || y >= h ? 0.0f : cover[static_cast<std::size_t>(y * w + x)];
        };
        return (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) +
               (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
    };
    std::vector<std::uint8_t> inside(static_cast<std::size_t>(bw * bh));
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            const float c = sample((static_cast<float>(x) + 0.5f) / static_cast<float>(scale),
                                   (static_cast<float>(y) + 0.5f) / static_cast<float>(scale));
            inside[static_cast<std::size_t>(y * bw + x)] = c >= 0.5f ? 1 : 0;
        }
    }
    const int radius = 2 * scale;
    std::vector<float> field(static_cast<std::size_t>(bw * bh));
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            const std::uint8_t me = inside[static_cast<std::size_t>(y * bw + x)];
            float best = static_cast<float>(radius);
            for (int dy = -radius; dy <= radius; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= bh) {
                    continue;
                }
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= bw || inside[static_cast<std::size_t>(yy * bw + xx)] == me) {
                        continue;
                    }
                    best = std::min(best, std::sqrt(static_cast<float>(dx * dx + dy * dy)));
                }
            }
            // Half a pixel to the boundary between the two pixels.
            field[static_cast<std::size_t>(y * bw + x)] = (me ? 1.0f : -1.0f) * (best - 0.5f);
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> blurred(field.size());
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                float sum = 0.0f;
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = std::clamp(x + dx, 0, bw - 1), yy = std::clamp(y + dy, 0, bh - 1);
                        sum += field[static_cast<std::size_t>(yy * bw + xx)];
                        ++n;
                    }
                }
                blurred[static_cast<std::size_t>(y * bw + x)] = sum / static_cast<float>(n);
            }
        }
        field.swap(blurred);
    }
    g.picture.width = bw;
    g.picture.height = bh;
    g.picture.rgba.resize(static_cast<std::size_t>(bw * bh) * 4);
    std::uint8_t colour[3];
    for (int k = 0; k < 3; ++k) {
        colour[k] = median(ink[k]);
    }
    for (int i = 0; i < bw * bh; ++i) {
        std::uint8_t* o = &g.picture.rgba[static_cast<std::size_t>(i) * 4];
        o[0] = colour[0];
        o[1] = colour[1];
        o[2] = colour[2];
        o[3] = static_cast<std::uint8_t>(std::lround(255.0f * smooth(-1.0f, 1.0f, field[static_cast<std::size_t>(i)])));
    }
    g.ok = true;
    return g;
}

}  // namespace spiral
