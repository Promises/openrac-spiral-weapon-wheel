// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral weapon wheel (wheel.hpp).
#include "wheel.hpp"

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
    m_input = open_input(equipped, m_look.spiral);
    m_stick = Stick{};
    m_anim = 0.001f;
    m_open = true;
}

int Wheel::selected() const {
    return pointer(static_cast<int>(m_items.size()), m_input, m_look.spiral).selected;
}

bool Wheel::steer(float x, float y, float deadzone) {
    const int before = selected();
    m_input = turn(m_input, m_stick, x, y, static_cast<int>(m_items.size()), m_look.spiral, deadzone);
    const int after = selected();
    // UpdatePointer.gek: the scroll sound when the index changed to an item or "None".
    return after != before && after >= -1;
}

bool Wheel::step(int slots) {
    const int before = selected();
    const Pointer p = pointer(static_cast<int>(m_items.size()), m_input, m_look.spiral);
    // The middle of the slot `slots` on from the one pointed at, in the input's own terms (the
    // signed index times the sign, back to radians); the stick's next turn starts from there.
    const float target = static_cast<float>((p.round + slots * sign(m_look.spiral)) * sign(m_look.spiral));
    m_input = target * kSlotRadians;
    const Pointer q = pointer(static_cast<int>(m_items.size()), m_input, m_look.spiral);
    const float a = q.angle_deg * kPi / 180.0f;
    m_stick.x = std::cos(a);
    m_stick.y = -std::sin(a);
    m_stick.first = false;
    const int after = selected();
    return after != before && after >= -1;
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
    const Options& o = m_look.spiral;
    const Pointer p = pointer(n, m_input, o);
    // _Prey+Size = 640 * Scale * Anim (HUD units).
    const float size = kSize * m_look.scale * m_anim;
    f.selected = p.selected;
    for (const Tile& t : tiles(n, p, o)) {
        if (!t.visible) {
            continue;
        }
        const float s = size * t.scale;
        // Sector: alpha 165 * AlphaMult, brightness 100 when hovered (else 0: black), turned to
        // _AngleRad.
        Sprite sector;
        sector.art = Art::Sector;
        sector.w = sector.h = s;
        sector.angle_deg = t.angle_deg;
        sector.grey = t.hover ? kSectorHover : 0.0f;
        sector.alpha = kSectorAlpha * t.alpha;
        f.sprites.push_back(sector);
        if (t.blank) {
            continue;
        }
        // Border: not a blank's; the file by _Hotkey and _Equipped (BorderFile_0..3), brightness
        // 175, alpha 255 * AlphaMult. "None" (-1) has no item: a plain border.
        const Item* item = t.index >= 0 ? &m_items[static_cast<std::size_t>(t.index)] : nullptr;
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
        // Icon: the sector's width * 0.1333, at _Prey+Radius * _Scale along _AngleDeg.
        const float a = t.angle_deg * kPi / 180.0f;
        Icon icon;
        icon.item = t.index;
        icon.size = s * kIcon;
        icon.x = std::cos(a) * size * kRadius * t.scale;
        icon.y = -std::sin(a) * size * kRadius * t.scale;
        icon.alpha = t.alpha;
        f.icons.push_back(icon);
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
