// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where each item goes on the spiral (arrange.hpp).
#include "arrange.hpp"

#include <cmath>

namespace spiral {

namespace {

// How far apart two angles are, 0 to 180.
float apart(float a, float b) { return std::fabs(std::remainder(a - b, 360.0f)); }

}  // namespace

float slot_angle(int index, const Options& o) {
    return static_cast<float>(index * sign(o) * 30) + o.start_deg;
}

std::vector<Item> arrange(const std::vector<HomeItem>& home, const std::vector<Item>& rest, const Options& o) {
    std::vector<Item> slots;  // index -> item (id 0: a hole)
    auto put = [&](int index, const Item& item) {
        if (static_cast<int>(slots.size()) <= index) {
            slots.resize(static_cast<std::size_t>(index) + 1);
        }
        slots[static_cast<std::size_t>(index)] = item;
    };
    auto taken = [&](int index) { return index < static_cast<int>(slots.size()) && slots[static_cast<std::size_t>(index)].id != 0; };

    // The home items, the best fitting first.
    std::vector<bool> placed(home.size(), false);
    for (std::size_t round = 0; round < home.size(); ++round) {
        int best_item = -1, best_slot = -2;
        float best_fit = 1e9f, best_vertical = -1.0f;
        for (std::size_t h = 0; h < home.size(); ++h) {
            if (placed[h] || home[h].item.id == 0) {
                continue;
            }
            for (int index = 0; index < kFirstLoop; ++index) {
                if (taken(index)) {
                    continue;
                }
                const float a = slot_angle(index, o);
                const float fit = apart(a, home[h].angle_deg);
                const float vertical = std::fabs(std::sin(a * kPi / 180.0f));
                const bool better = fit < best_fit - 0.01f ||
                                    (std::fabs(fit - best_fit) <= 0.01f && vertical > best_vertical + 0.001f);
                if (better) {
                    best_item = static_cast<int>(h);
                    best_slot = index;
                    best_fit = fit;
                    best_vertical = vertical;
                }
            }
        }
        if (best_item < 0) {
            break;
        }
        placed[static_cast<std::size_t>(best_item)] = true;
        const int index = best_slot;
        put(index, home[static_cast<std::size_t>(best_item)].item);
    }
    // The rest: the first loop's free slots in order, then outward.
    int next = 0;
    for (const Item& item : rest) {
        while (taken(next)) {
            ++next;
        }
        put(next, item);
        ++next;
    }
    return slots;
}

}  // namespace spiral
