// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral weapon wheel (wheel.hpp).
#include "wheel.hpp"
#include "glyph.hpp"

#include <algorithm>
#include <cmath>

namespace spiral {

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
}

Frame Wheel::frame() const {
    Frame f;
    const int n = static_cast<int>(m_items.size());
    const Options o = spiral_options();
    const Pointer p = pointer(n, m_input, o);
    // _Prey+Size = 640 * Scale * Anim (HUD units).
    const float size = kSize * m_look.scale * m_anim;
    f.selected = p.selected;
    for (const Tile& t : tiles(n, p, o)) {
        if (!t.visible) {
            continue;
        }
        const float s = size * t.scale;
        // A hole (arrange.hpp: a first-loop slot no item took) is drawn as a blank.
        const Item* item = t.index >= 0 ? &m_items[static_cast<std::size_t>(t.index)] : nullptr;
        if (item != nullptr && item->id == 0) {
            item = nullptr;
        }
        // No "None" here: its slot (-1) is a blank like the others.
        const bool blank = item == nullptr;
        // Sector: alpha 165 * AlphaMult, brightness 100 when hovered (else 0: black), turned to
        // _AngleRad; (the mod's) an item's own ground colour, darkened, when it has one.
        Sprite sector;
        sector.art = Art::Sector;
        sector.w = sector.h = s;
        sector.angle_deg = t.angle_deg;
        sector.grey = t.hover && !blank ? kSectorHover : 0.0f;
        if (item != nullptr && item->tinted) {
            sector.coloured = true;
            shade(item->tint, t.hover ? kCellHoverValue : kCellValue, t.hover ? kCellHoverSaturation : kCellSaturation,
                  sector.rgb);
        }
        sector.alpha = kSectorAlpha * t.alpha;
        f.sprites.push_back(sector);
        if (blank) {
            continue;
        }
        // Border: not a blank's; the file by _Hotkey and _Equipped (BorderFile_0..3), brightness
        // 175, alpha 255 * AlphaMult.
        const bool hot = item != nullptr && item->hot > 0;
        const bool eq = item != nullptr && item->equipped;
        Sprite border;
        border.art = hot ? (eq ? Art::BorderEqHot : Art::BorderHot) : (eq ? Art::BorderEq : Art::Border);
        border.w = border.h = s;
        border.angle_deg = t.angle_deg;
        border.grey = kBorderGrey;
        border.alpha = t.alpha;
        f.sprites.push_back(border);
        if (item == nullptr) {
            continue;
        }
        // Icon: the sector's width * 0.1333, at _Prey+Radius * _Scale along _AngleDeg; (the mod's)
        // an item's glyph instead, fitted inside its sector.
        const float a = t.angle_deg * kPi / 180.0f;
        const float ix = std::cos(a) * size * kRadius * t.scale;
        const float iy = -std::sin(a) * size * kRadius * t.scale;
        if (item->glyph != 0 && item->glyph_width > 0 && item->glyph_height > 0) {
            const float box = s * kGlyphSquare * (1.0f - 2.0f * kGlyphPadding);
            const float larger = static_cast<float>(std::max(item->glyph_width, item->glyph_height));
            Sprite glyph;
            glyph.art = Art::Glyph;
            glyph.image = item->glyph;
            glyph.x = ix;
            glyph.y = iy;
            glyph.w = box * static_cast<float>(item->glyph_width) / larger;
            glyph.h = box * static_cast<float>(item->glyph_height) / larger;
            glyph.alpha = t.alpha;
            f.sprites.push_back(glyph);
        } else {
            Icon icon;
            icon.item = t.index;
            icon.size = s * kIcon;
            icon.x = ix;
            icon.y = iy;
            icon.alpha = t.alpha;
            f.icons.push_back(icon);
        }
        if (hot) {
            // Hotkey: _AngleDeg - 11.5 at _Prey+RadiusHotkey * _Scale, SizeHotkey * _Scale, black
            // (red, green and blue 0), the border's alpha.
            const float b = (t.angle_deg - kBadgeTurn) * kPi / 180.0f;
            Sprite badge;
            badge.art = Art::Badge;
            badge.badge = item->hot;
            badge.w = badge.h = kBadge * m_look.scale * m_anim * t.scale;
            badge.x = std::cos(b) * size * kRadiusHotkey * t.scale;
            badge.y = -std::sin(b) * size * kRadiusHotkey * t.scale;
            badge.grey = 0.0f;
            badge.alpha = t.alpha;
            f.sprites.push_back(badge);
        }
    }
    // Pointer: brightness 200, the wheel's size, turned to its rotateangle.
    Sprite pointer_sprite;
    pointer_sprite.art = Art::Pointer;
    pointer_sprite.w = pointer_sprite.h = size;
    pointer_sprite.angle_deg = p.angle_deg;
    pointer_sprite.grey = kPointerGrey;
    f.sprites.push_back(pointer_sprite);
    // InfoBar: 320 x 5 at the middle, brightness 200; the name above it, the second line below
    // (the mod's category line).
    Sprite info;
    info.art = Art::Info;
    info.w = kInfoWidth * m_look.scale * m_anim;
    info.h = kInfoHeight * m_look.scale * m_anim;
    info.grey = kInfoGrey;
    f.sprites.push_back(info);
    f.text_height = kTextHeight * m_look.scale * m_anim;
    f.name_y = -info.h * 0.5f - f.text_height * 1.2f;
    f.detail_y = info.h * 0.5f + f.text_height * 0.2f;
    return f;
}

}  // namespace spiral
