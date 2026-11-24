// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral weapon wheel (wheel.hpp).
#include "wheel.hpp"
#include "look.hpp"

#include <algorithm>
#include <cmath>

namespace spiral {

namespace {
// The middle disc's flash, among the slots' (no slot has this index).
constexpr int kCentrePanel = -1000000;
}  // namespace

void Wheel::open(std::vector<Item> items, const Look& look) {
    m_items = std::move(items);
    m_look = look;
    int equipped = -1;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].equipped) {
            equipped = static_cast<int>(i);
            break;
        }
    }
    // UpdateMenu.gek: PointerAngleInput = iEquipped * pi/6 * ClockwiseSign; FirstInput 1; Anim
    // from 0.001.
    m_input = open_input(equipped, spiral_options());
    m_stick = Stick{};
    m_anim = 0.001f;
    m_open = true;
    m_loop = 0.0f;
    m_loop = target_loop();
    m_anchor = m_loop;
    m_flashes.clear();
}

std::uint32_t Wheel::random() {
    // xorshift32
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return m_random;
}

void Wheel::flash_frame() {
    const Pointer p = pointer(static_cast<int>(m_items.size()), m_input, spiral_options());
    // The running ones: on 2 a frame, a new place for the noise; gone at the end.
    for (auto it = m_flashes.begin(); it != m_flashes.end();) {
        it->second.t += look::kFlashStep;
        it->second.u = static_cast<int>(random() % look::kFlashOffsets);
        it->second.v = static_cast<int>(random() % look::kFlashOffsets);
        it = it->second.t >= look::kFlashEnd ? m_flashes.erase(it) : it + 1;
    }
    // Each panel not flashing (the middle disc's as kCentrePanel): its chance.
    for (int index = -p.blanks - 1; index < static_cast<int>(m_items.size()); ++index) {
        if (index == -p.blanks - 1) {
            index = kCentrePanel;
        }
        const bool running = std::any_of(m_flashes.begin(), m_flashes.end(), [index](const auto& f) { return f.first == index; });
        if (!running && random() % look::kFlashChance == 0) {
            Flash f;
            f.u = static_cast<int>(random() % look::kFlashOffsets);
            f.v = static_cast<int>(random() % look::kFlashOffsets);
            m_flashes.push_back({index, f});
        }
        if (index == kCentrePanel) {
            index = -p.blanks - 1;
        }
    }
}

float radius_step(const Options& o) {
    const float shrink = 1.0f - shallowness(o);
    const float ours = 1.0f - shallowness(Options{});
    return std::clamp(look::kRadiusStep * shrink / ours, 5.0f, look::kRadiusStep);
}

float slot_opacity(float k, bool spiral) {
    if (!spiral) {
        return k > -1.0f && k < static_cast<float>(kPerLoop) ? 1.0f : 0.0f;
    }
    // At whole slots: 1 in the loop (0 to 11), the 4 before it and the 3 after it fading, 0 past
    // them; linear between.
    auto at = [](int i) {
        if (i >= 0 && i < kPerLoop) {
            return 1.0f;
        }
        if (i < 0 && i >= -4) {
            return look::kBefore[-i - 1];
        }
        if (i >= kPerLoop && i < kPerLoop + 3) {
            return look::kAfter[i - kPerLoop];
        }
        return 0.0f;
    };
    const float f = std::floor(k);
    const float t = k - f;
    const int i = static_cast<int>(f);
    return at(i) * (1.0f - t) + at(i + 1) * t;
}

// The slots the spiral has (pointer.total), and an index taken into 0 up to them.
namespace {
int wrap_index(int index, int total) { return total > 0 ? ((index % total) + total) % total : 0; }
float wrap_into(float v, float from, float total) { return v - std::floor((v - from) / total) * total; }
}  // namespace

// The loop's first slot the pointer is in, counted on from the last one (unwrapped): the pointed-at
// slot a whole loop or more past the last loop's first, or before it, moves it on or back by whole
// loops, so the spiral turns the way the pointer went (with two loops, either way is as far).
float Wheel::target_loop() const {
    const Pointer p = pointer(static_cast<int>(m_items.size()), m_input, spiral_options());
    if (!p.spiral) {
        return 0.0f;
    }
    const float total = static_cast<float>(p.total);
    const float k = wrap_into(static_cast<float>(wrap_index(p.selected, p.total)) - m_loop, -5.0f, total);
    const float loop = static_cast<float>(kPerLoop);
    return m_loop + std::floor(k / loop) * loop;
}

float Wheel::anchor_target() const {
    const float total = static_cast<float>(pointer(static_cast<int>(m_items.size()), m_input, spiral_options()).total);
    return total > 0.0f ? wrap_into(target_loop(), 0.0f, total) : 0.0f;
}

float Wheel::anchor() const {
    const Pointer p = pointer(static_cast<int>(m_items.size()), m_input, spiral_options());
    const float total = static_cast<float>(p.total);
    if (total <= 0.0f) {
        return 0.0f;
    }
    if (m_look.continuous && p.spiral) {
        // The pointer's place in items (its index is signed by the direction).
        return wrap_into(p.index * static_cast<float>(sign(spiral_options())) - kContinuousIn, 0.0f, total);
    }
    return wrap_into(m_anchor, 0.0f, total);
}

void Wheel::set_continuous(bool continuous) {
    if (continuous == m_look.continuous) {
        return;
    }
    m_look.continuous = continuous;
    // Into loop steps: at the pointer's loop at once.
    m_loop = target_loop();
    m_anchor = m_loop;
}

float Wheel::pointer_deg() const {
    return pointer(static_cast<int>(m_items.size()), m_input, spiral_options()).angle_deg;
}

bool Wheel::is_item(int index) const {
    return index >= 0 && index < static_cast<int>(m_items.size()) && m_items[static_cast<std::size_t>(index)].id != 0;
}

Options Wheel::spiral_options() const {
    Options o = m_look.spiral;
    o.start_deg -= 30.0f * static_cast<float>(sign(o));
    return o;
}

int Wheel::selected() const {
    return pointer(static_cast<int>(m_items.size()), m_input, spiral_options()).selected;
}

bool Wheel::steer(float x, float y, float deadzone) {
    const int before = selected();
    const float length = std::sqrt(x * x + y * y);
    if (m_stick.first && length > 0.0f && length >= deadzone) {
        // (the mod's) The first push of an opening points on the first loop, where the game's own
        // quick select has its items (arrange.hpp): the slot at the stick's direction there, not
        // the nearest turn from the item in hand (UpdatePointer.gek's first input), which from item
        // 0 would go back into the blanks before it. Then the stick turns the pointer as Prey
        // Menu's.
        const Options o = m_look.spiral;
        const float cs = static_cast<float>(sign(o));
        const float stick_deg = std::atan2(-y, x) * 180.0f / kPi;
        float k = std::remainder((stick_deg - o.start_deg) * cs, 360.0f) / 30.0f;  // -6 .. 6
        if (k < -0.5f) {
            k += static_cast<float>(kPerLoop);  // into -0.5 .. 11.5
        }
        m_input = k * kSlotRadians * cs;
        m_stick.x = x / length;
        m_stick.y = y / length;
        m_stick.first = false;
    } else {
        m_input = turn(m_input, m_stick, x, y, static_cast<int>(m_items.size()), spiral_options(), deadzone);
    }
    const int after = selected();
    // UpdatePointer.gek: the scroll sound when the index changed to an item or "None".
    return after != before && is_item(after);
}

bool Wheel::step(int slots) {
    const int before = selected();
    const Pointer p = pointer(static_cast<int>(m_items.size()), m_input, spiral_options());
    // The middle of the slot `slots` on from the one pointed at, in the input's own terms (the
    // signed index times the sign, back to radians); the stick's next turn starts from there.
    const float target = static_cast<float>((p.round + slots * sign(spiral_options())) * sign(spiral_options()));
    m_input = target * kSlotRadians;
    const Pointer q = pointer(static_cast<int>(m_items.size()), m_input, spiral_options());
    const float a = q.angle_deg * kPi / 180.0f;
    m_stick.x = std::cos(a);
    m_stick.y = -std::sin(a);
    m_stick.first = false;
    const int after = selected();
    return after != before && is_item(after);
}

void Wheel::advance(float seconds) {
    if (m_anim < 1.0f) {
        m_anim += seconds / kOpenSeconds;
        if (m_anim > 1.0f) {
            m_anim = 1.0f;
        }
    }
    // The spiral eases to the pointer's loop, the way the pointer went.
    m_loop = target_loop();
    const float d = m_loop - m_anchor;
    m_anchor = std::fabs(d) < 0.01f ? m_loop : m_anchor + d * (1.0f - std::exp(-seconds / kLoopSeconds));
}

Frame Wheel::frame() const {
    Frame f;
    const int n = static_cast<int>(m_items.size());
    const Options o = spiral_options();
    const Pointer p = pointer(n, m_input, o);
    f.unit = m_look.scale * m_anim * look::kFit / look::kDesignHeight;
    f.centre_y = look::kCentreY * f.unit;
    f.pointer_deg = p.angle_deg;
    f.selected = p.selected;
    f.name_y = f.centre_y + look::kNameY * f.unit;
    f.detail_y = f.centre_y + look::kAmmoY * f.unit;
    for (const auto& [index, flash] : m_flashes) {
        if (index == kCentrePanel) {
            f.centre_flash = static_cast<float>(std::min(0x80, 2 * (0x80 - std::abs(flash.t - 0x80)))) / 128.0f;
            f.centre_flash_u = flash.u;
            f.centre_flash_v = flash.v;
        }
    }
    const float step = radius_step(m_look.spiral);
    const float total = static_cast<float>(p.total);
    // Each slot k on from the loop shown's first (the anchor): the 4 before it as negative k, the
    // loops wrapping (a spiral's slots are a loop of `total`).
    std::vector<std::pair<float, Slot>> drawn;
    for (const Tile& t : tiles(n, p, o)) {
        // The blanks (the slots after the last item, to the spiral's whole loops) are not drawn: only
        // the items and the holes between them (arrange.hpp).
        if (t.index < 0) {
            continue;
        }
        const float at = static_cast<float>(wrap_index(t.index, p.total)) - anchor();
        const float k = p.spiral ? wrap_into(at, -5.0f, total) : wrap_into(at, -0.5f, total);
        const float fade = slot_opacity(k, p.spiral);
        if (fade <= 0.0f) {
            continue;
        }
        const Item* item = t.index >= 0 && t.index < n ? &m_items[static_cast<std::size_t>(t.index)] : nullptr;
        if (item != nullptr && item->id == 0) {
            item = nullptr;  // a hole (arrange.hpp): a first-loop slot no item took
        }
        Slot s;
        s.index = t.index;
        s.empty = item == nullptr;
        if (item != nullptr) {
            const look::CategoryColours& c = look::colours_of(item->category);
            s.category = item->category;
            s.wash = item->colours.has_wash ? item->colours.wash : c.wash;
            s.glyph_colour = item->colours.has_glyph ? item->colours.glyph : c.glyph;
            s.selected_colour = item->colours.has_selected ? item->colours.selected : look::kHighlight;
        }
        s.selected = item != nullptr && t.hover;
        s.angle_deg = t.angle_deg;
        s.radius = std::min(look::kMaxRadius, look::kFirstRadius - step * k);
        s.push = s.selected ? look::kPush : 0.0f;
        for (const auto& [index, flash] : m_flashes) {
            if (index == t.index) {
                s.flash = static_cast<float>(std::min(0x80, 2 * (0x80 - std::abs(flash.t - 0x80)))) / 128.0f;
                s.flash_u = flash.u;
                s.flash_v = flash.v;
            }
        }
        s.opacity = s.empty ? std::min(fade, look::kEmpty) : fade;
        if (item != nullptr) {
            s.glyph = item->glyph;
            s.glyph_width = item->glyph_width;
            s.glyph_height = item->glyph_height;
            s.badge = item->hot;
        }
        drawn.push_back({s.selected ? 1e9f : k, s});
    }
    // The outer first (the design's order), the selected last.
    std::stable_sort(drawn.begin(), drawn.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [k, s] : drawn) {
        f.slots.push_back(s);
        if (s.empty) {
            continue;
        }
        const float a = s.angle_deg * kPi / 180.0f;
        const float middle = (s.radius + s.push) * f.unit;
        // The game's own icon where there is no glyph: the glyph's square.
        if (s.glyph == 0 || s.glyph_width <= 0 || s.glyph_height <= 0) {
            Icon icon;
            icon.item = s.index;
            icon.size = look::kThickness * s.radius * (s.selected ? look::kGlyphSelected : look::kGlyph) * f.unit;
            icon.x = std::cos(a) * middle;
            icon.y = f.centre_y - std::sin(a) * middle;
            icon.alpha = s.opacity;
            f.icons.push_back(icon);
        }
        if (s.badge > 0) {
            const float outer = s.radius * (1.0f + look::kThickness * 0.5f) + s.push - look::kBadgeIn;
            const float b = (s.angle_deg - look::kBadgeTurn) * kPi / 180.0f;
            Badge badge;
            badge.number = s.badge;
            badge.x = std::cos(b) * outer * f.unit;
            badge.y = f.centre_y - std::sin(b) * outer * f.unit;
            badge.alpha = s.opacity;
            f.badges.push_back(badge);
        }
    }
    return f;
}

}  // namespace spiral
