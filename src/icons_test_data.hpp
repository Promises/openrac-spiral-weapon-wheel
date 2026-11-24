// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test data the icons' tests make (icons_test.cpp, spiral_weapon_wheel_test.cpp): WAD streams, a
// level's data file with a HUD set of synthetic pictures, and PNG files. Nothing of the game's.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace test_data {

// A WAD stream of `packets` (its 16-byte header: "WAD", the size, a name).
inline std::vector<std::uint8_t> wad(const std::vector<std::uint8_t>& packets) {
    std::vector<std::uint8_t> v = {'W', 'A', 'D', 0, 0, 0, 0, 'T', 'E', 'S', 'T', 0, 0, 0, 0, 0};
    v.insert(v.end(), packets.begin(), packets.end());
    const std::uint32_t size = static_cast<std::uint32_t>(v.size());
    std::memcpy(v.data() + 3, &size, 4);
    return v;
}

// Any bytes as a WAD stream: literal runs (up to 273 bytes, flag 0 and the length - 18; or flag
// length - 3 for 4 to 18), a dummy packet (0x11 0 0) between two, the last few as a dummy's little
// literals.
inline std::vector<std::uint8_t> compress(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint8_t> packets;
    std::size_t i = 0;
    while (bytes.size() - i >= 4) {
        const std::size_t n = std::min<std::size_t>(273, bytes.size() - i);
        if (n >= 18) {
            packets.push_back(0);
            packets.push_back(static_cast<std::uint8_t>(n - 18));
        } else {
            packets.push_back(static_cast<std::uint8_t>(n - 3));
        }
        packets.insert(packets.end(), bytes.begin() + static_cast<std::ptrdiff_t>(i), bytes.begin() + static_cast<std::ptrdiff_t>(i + n));
        i += n;
        packets.insert(packets.end(), {0x11, 0, 0});
    }
    while (i < bytes.size()) {
        const std::size_t n = std::min<std::size_t>(3, bytes.size() - i);
        packets.insert(packets.end(), {0x11, static_cast<std::uint8_t>(n), 0});
        packets.insert(packets.end(), bytes.begin() + static_cast<std::ptrdiff_t>(i), bytes.begin() + static_cast<std::ptrdiff_t>(i + n));
        i += n;
    }
    return wad(packets);
}

// A synthetic icon's palette (index order, GS alpha): 16 shades of a blue ground by 16 steps of a
// near-black ink, as the game's icons blend their glyphs' edges.
inline std::array<std::uint8_t, 1024> ring_palette() {
    std::array<std::uint8_t, 1024> p{};
    const float ground[3] = {110, 115, 221};
    for (int shade = 0; shade < 16; ++shade) {
        for (int ink = 0; ink < 16; ++ink) {
            const float shine = 1.0f - 0.25f * static_cast<float>(shade) / 15.0f;
            const float t = static_cast<float>(ink) / 15.0f;
            for (int k = 0; k < 3; ++k) {
                p[static_cast<std::size_t>(shade * 16 + ink) * 4 + static_cast<std::size_t>(k)] =
                    static_cast<std::uint8_t>(std::lround(ground[k] * shine * (1.0f - t) + 15.0f * t));
            }
            p[static_cast<std::size_t>(shade * 16 + ink) * 4 + 3] = 0x80;
        }
    }
    return p;
}

// A 32 x 32 ring of radii `inner` to `outer` about the middle, with a bar below it, as indices into
// ring_palette (4 x 4 supersampled).
inline std::vector<std::uint8_t> ring_indices(float inner, float outer) {
    std::vector<std::uint8_t> out(32 * 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            float ink = 0.0f;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const float fx = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / 4.0f - 16.0f;
                    const float fy = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / 4.0f - 16.0f;
                    const float r = std::sqrt(fx * fx + fy * fy);
                    const bool in = (r >= inner && r <= outer) || (std::fabs(fx) < 1.5f && fy > 6.0f && fy < 14.0f);
                    ink += in ? 1.0f / 16.0f : 0.0f;
                }
            }
            const int shade = (x + y) * 15 / 62;
            out[static_cast<std::size_t>(y * 32 + x)] = static_cast<std::uint8_t>(shade * 16 + std::lround(ink * 15.0f));
        }
    }
    return out;
}

constexpr int kIconRing = 0x1000, kIconThick = 0x2000;

struct LevelData {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> ring_indices, thick_indices;
    std::array<std::uint8_t, 1024> palette;  // index order
};

// A level's data file: the 11 byte ranges, a HUD header and two banks. Frame 0 has no picture;
// each icon of `ring` then `thick` has 5 frames (k 0 to 4, a gold weapon's 4), the ring's texture
// or the thicker ring's (texture 0 or 1, both in bank 0, WAD-compressed), all with palette 0 (bank
// 1, stored, in the GS's CSM1 order). `no_banks`: the banks' ranges empty.
inline LevelData level_data(const std::vector<int>& ring, const std::vector<int>& thick, bool no_banks = false) {
    LevelData d;
    d.ring_indices = ring_indices(8.0f, 11.0f);
    d.thick_indices = ring_indices(6.0f, 12.0f);
    d.palette = ring_palette();
    auto put32 = [](std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
        if (v.size() < at + 4) {
            v.resize(at + 4);
        }
        std::memcpy(v.data() + at, &x, 4);
    };
    auto put16 = [](std::vector<std::uint8_t>& v, std::size_t at, std::uint16_t x) {
        if (v.size() < at + 2) {
            v.resize(at + 2);
        }
        std::memcpy(v.data() + at, &x, 2);
    };
    // The header: the fixed part (0xb4), the icons, the frames, the palettes, the textures.
    std::vector<std::uint8_t> h(0xB4, 0);
    const std::size_t icons = static_cast<std::size_t>(ring.size() + thick.size());
    const std::size_t icon_off = 0xB4, frame_off = icon_off + 8 * (icons + 1);
    const std::size_t frames = 1 + 5 * icons;
    const std::size_t pal_off = frame_off + 4 * frames, tex_off = pal_off + 8;
    put16(h, 0, static_cast<std::uint16_t>(icons + 1));
    put16(h, 2, static_cast<std::uint16_t>(frames));
    put32(h, 4, static_cast<std::uint32_t>(icon_off));
    put32(h, 8, static_cast<std::uint32_t>(frame_off));
    put32(h, 12, static_cast<std::uint32_t>(pal_off));
    put32(h, 16, static_cast<std::uint32_t>(tex_off));
    for (int b = 0; b < 8; ++b) {
        put32(h, 0x14 + 4 * static_cast<std::size_t>(b), b == 0 ? 0u : 1u);  // the palette in bank 1
        put32(h, 0x34 + 4 * static_cast<std::size_t>(b), 2u);                // the textures in bank 0
    }
    std::size_t i = 0;
    for (const std::vector<int>* list : {&ring, &thick}) {
        for (const int id : *list) {
            put16(h, icon_off + 8 * i, static_cast<std::uint16_t>(id));
            put16(h, icon_off + 8 * i + 2, 5);
            put16(h, icon_off + 8 * i + 4, static_cast<std::uint16_t>(1 + 5 * i));
            for (int k = 0; k < 5; ++k) {
                put16(h, frame_off + 4 * (1 + 5 * i + static_cast<std::size_t>(k)), 0);
                put16(h, frame_off + 4 * (1 + 5 * i + static_cast<std::size_t>(k)) + 2, list == &ring ? 0 : 1);
            }
            ++i;
        }
    }
    put16(h, icon_off + 8 * i, 0xFFFF);
    put16(h, frame_off, 0xFFFF);  // frame 0: palette and texture -1
    put16(h, frame_off + 2, 0xFFFF);
    h.resize(tex_off + 16, 0);
    put32(h, pal_off, 0x80000000u);
    put32(h, tex_off, 0x80000000u);
    h[tex_off + 6] = 5;
    h[tex_off + 7] = 5;
    put32(h, tex_off + 8, 0x80000000u | 1024u);
    h[tex_off + 14] = 5;
    h[tex_off + 15] = 5;
    std::vector<std::uint8_t> bank0 = d.ring_indices;
    bank0.insert(bank0.end(), d.thick_indices.begin(), d.thick_indices.end());
    bank0 = compress(bank0);
    std::vector<std::uint8_t> bank1(1024);
    for (int e = 0; e < 256; ++e) {
        const int stored = (e & 0xE7) | ((e & 0x08) << 1) | ((e & 0x10) >> 1);
        std::memcpy(bank1.data() + 4 * stored, d.palette.data() + 4 * e, 4);
    }
    // The file: the ranges, then the header and the banks.
    std::vector<std::uint8_t>& f = d.bytes;
    f.assign(0x58, 0);
    for (int r = 0; r < 11; ++r) {
        put32(f, 8 * static_cast<std::size_t>(r), 0xFFFFFFFFu);  // absent: offset -1
    }
    auto add = [&](int r, const std::vector<std::uint8_t>& part) {
        const std::size_t at = f.size();
        f.insert(f.end(), part.begin(), part.end());
        put32(f, 8 * static_cast<std::size_t>(r), static_cast<std::uint32_t>(at));
        put32(f, 8 * static_cast<std::size_t>(r) + 4, static_cast<std::uint32_t>(part.size()));
    };
    add(4, h);
    if (!no_banks) {
        add(5, bank0);
        add(6, bank1);
    }
    return d;
}

inline LevelData level_data(bool no_banks = false) { return level_data({kIconRing}, {kIconThick}, no_banks); }

// A PNG of `w` x `h` pixels all one RGBA colour: zlib's stored blocks, no compression.
inline std::vector<std::uint8_t> png(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    auto crc = [](const std::uint8_t* p, std::size_t n) {
        std::uint32_t c = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int k = 0; k < 8; ++k) {
                c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            }
        }
        return c ^ 0xFFFFFFFFu;
    };
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto be32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) {
        for (int s = 24; s >= 0; s -= 8) {
            v.push_back(static_cast<std::uint8_t>(x >> s));
        }
    };
    auto chunk = [&](const char* type, const std::vector<std::uint8_t>& data) {
        be32(out, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        be32(out, crc(body.data(), body.size()));
    };
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, static_cast<std::uint32_t>(w));
    be32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", ihdr);
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            raw.insert(raw.end(), {r, g, b, a});
        }
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    for (std::size_t i = 0; i < raw.size() || i == 0; i += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - i);
        z.push_back(i + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(i), raw.begin() + static_cast<std::ptrdiff_t>(i + n));
        if (i + n >= raw.size()) {
            break;
        }
    }
    std::uint32_t s1 = 1, s2 = 0;
    for (const std::uint8_t c : raw) {
        s1 = (s1 + c) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    be32(z, (s2 << 16) | s1);
    chunk("IDAT", z);
    chunk("IEND", {});
    return out;
}

}  // namespace test_data
