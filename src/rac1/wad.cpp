// SPDX-License-Identifier: GPL-3.0-or-later
//
// WAD decompression (wad.hpp says from where). Packet kinds, by flag byte:
//
//   0x00..0x0f  literal run (4..273 bytes); never followed by another one
//   0x10..0x1f  far match (displacement 0x4000 * (A + 1) + 14 bits), or, with
//               a zero displacement, the pad (skip to the next 0x1000 bytes of
//               the stream) and dummy (only a little literal) packets
//   0x20..0x3f  medium and big match (displacement 1..16384, 3..288 bytes)
//   0x40..0xff  little match (displacement 1..2048, 3..8 bytes)
//
// Every match and the dummy packet end with 0..3 "little literal" bytes, their count in the low
// two bits of the packet's second-to-last byte.
#include "wad.hpp"

#include <cstring>

namespace gc_weapon_wheel::hud {

bool is_wad(const std::uint8_t* bytes, std::size_t size) {
    return size >= kWadHeaderSize && std::memcmp(bytes, "WAD", 3) == 0;
}

bool wad_decompress(const std::uint8_t* bytes, std::size_t size, std::vector<std::uint8_t>& out) {
    out.clear();
    if (!is_wad(bytes, size)) {
        return false;
    }
    std::uint32_t total = 0;
    std::memcpy(&total, bytes + 3, 4);
    if (total < kWadHeaderSize || total > size) {
        return false;
    }
    const std::uint8_t* stream = bytes + kWadHeaderSize;
    const std::size_t end = total - kWadHeaderSize;
    std::size_t ptr = 0;
    out.reserve(static_cast<std::size_t>(total) * 2);
    bool ok = true;
    auto next = [&]() -> std::uint8_t {
        if (ptr >= end) {
            ok = false;
            return 0;
        }
        return stream[ptr++];
    };
    auto literals = [&](std::size_t n) {
        if (end - ptr < n) {
            ok = false;
            return;
        }
        out.insert(out.end(), stream + ptr, stream + ptr + n);
        ptr += n;
    };
    auto copy_match = [&](std::size_t displacement, std::size_t length) {
        if (displacement == 0 || displacement > out.size()) {
            ok = false;
            return;
        }
        // Byte by byte: a match overlapping its own output repeats it.
        const std::size_t source = out.size() - displacement;
        for (std::size_t i = 0; i < length; ++i) {
            out.push_back(out[source + i]);
        }
    };
    while (ok && ptr < end) {
        const std::uint8_t flag = next();
        if (flag < 0x10) {
            const std::size_t n = flag == 0 ? std::size_t{next()} + 18 : std::size_t{flag} + 3;
            literals(n);
            // The format never puts two literal runs in a row: a second one is a damaged stream.
            if (ptr < end && stream[ptr] < 0x10) {
                ok = false;
            }
            continue;
        }
        std::size_t length = 0, displacement = 0;
        std::uint8_t little = 0;
        if (flag < 0x20) {
            std::size_t ml = flag & 7;
            if (ml == 0) {
                ml = std::size_t{next()} + 7;
            }
            const std::uint8_t b0 = next();
            const std::uint8_t b1 = next();
            const std::size_t a = (flag >> 3) & 1;
            const std::size_t far = std::size_t{b1} * 0x40 + (b0 >> 2);
            // No displacement: the padding (on at the next 0x1000 of the stream), or a dummy.
            if (a == 0 && far == 0) {
                if (ml != 1) {
                    ptr = (ptr + 0xfff) & ~std::size_t{0xfff};
                    continue;
                }
                literals(b0 & 3);
                continue;
            }
            displacement = 0x4000 * (a + 1) + far;
            length = ml + 2;
            little = b0;
        } else if (flag < 0x40) {
            std::size_t ml = flag & 0x1f;
            if (ml == 0) {
                ml = std::size_t{next()} + 0x1f;
            }
            length = ml + 2;
            const std::uint8_t b1 = next();
            const std::uint8_t b2 = next();
            displacement = std::size_t{b2} * 0x40 + (b1 >> 2) + 1;
            little = b1;
        } else {
            const std::uint8_t b1 = next();
            length = (flag >> 5) + 1;
            displacement = std::size_t{b1} * 8 + ((flag >> 2) & 7) + 1;
            little = flag;
        }
        if (!ok) {
            break;
        }
        copy_match(displacement, length);
        literals(little & 3);
    }
    return ok;
}

}  // namespace gc_weapon_wheel::hud
