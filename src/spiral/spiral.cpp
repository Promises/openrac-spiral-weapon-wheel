// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral, ported from "Prey Menu" (spiral.hpp says from where).
#include "spiral.hpp"

#include <cmath>

namespace spiral {

int sign(const Options& o) { return 1 - 2 * (o.clockwise ? 1 : 0); }

float shallowness(const Options& o) {
    const float depth = o.depth < 0.0f ? 0.0f : o.depth > 1.0f ? 1.0f : o.depth;
    return 0.9f + 0.0666f * depth;
}

Pointer pointer(int items, float input_rad, const Options& o) {
    Pointer p;
    const int cs = sign(o);
    const int n = items < 0 ? 0 : items;
    // _Prey+TotalCount = ceil((ItemCount + 1) / 12) * 12; _Prey+BlankCount = TotalCount - ItemCount;
    // _Prey+Spiral = ItemCount >= 12.
    p.total = (n + 1 + kPerLoop - 1) / kPerLoop * kPerLoop;
    p.blanks = p.total - n;
    p.spiral = n >= kPerLoop;
    // _Prey+PointerIndexInput = PointerAngleInput * 1.9098593171.
    const float input = input_rad * kSlotsPerRadian;
    // _Prey+PointerIndex: the input (signed by the direction) less whole spirals, by two
    // conditional subtractions on ShiftBack (input - ItemCount + 0.5) and ShiftForward (input +
    // BlankCount + 0.5). For either sign they come to one wrap into [-BlankCount - 0.5,
    // ItemCount - 0.5): the spiral is a loop of TotalCount slots ("None" and the blanks between
    // the last item and the first). The tests check this against the two subtractions as written.
    const float signed_input = input * static_cast<float>(cs);
    const float total = static_cast<float>(p.total);
    const float wrapped =
        signed_input - std::floor((signed_input + static_cast<float>(p.blanks) + 0.5f) / total) * total;
    p.index = static_cast<float>(cs) * wrapped;
    // _Prey+PointerIndexRound: <floor>0.5</floor>, read as the nearest slot (the wrap's bounds are
    // half a slot either side of a slot's middle).
    p.round = static_cast<int>(std::floor(p.index + 0.5f));
    p.selected = p.round * cs;
    // The pointer picture's rotateangle: 0.52359877559 * ClockwiseSign + PointerAngle + StartAngleRad,
    // PointerAngle = PointerIndex / 1.9098593171.
    p.angle_deg = 30.0f * static_cast<float>(cs) + p.index * 30.0f + o.start_deg;
    return p;
}

Tile tile(const Pointer& p, int index, const Options& o) {
    Tile t;
    const int cs = sign(o);
    t.index = index;
    // _IndexSigned = _Index * ClockwiseSign; _AngleDeg = (_IndexSigned + ClockwiseSign) * 30 +
    // StartAngleDeg.
    const float signed_index = static_cast<float>(index * cs);
    t.angle_deg = (signed_index + static_cast<float>(cs)) * 30.0f + o.start_deg;
    // _IndexDiff: the pointer's distance in slots from this one, through the spiral behind
    // (PointerIndexBehind = PointerIndex - TotalCount * sign), as it is, or ahead, the nearest of
    // the three (ties as the XML's conditions break them), times the sign.
    const float total = static_cast<float>(p.total * cs);
    const float behind = (p.index - total) - signed_index;
    const float normal = p.index - signed_index;
    const float ahead = (p.index + total) - signed_index;
    const float ab = std::fabs(behind), an = std::fabs(normal), aa = std::fabs(ahead);
    float diff = 0.0f;
    if (ab < an && ab < aa) {
        diff += behind;
    }
    if (an <= ab && an <= aa) {
        diff += normal;
    }
    if (aa < ab && aa < an) {
        diff += ahead;
    }
    t.diff = diff * static_cast<float>(cs);
    // _AlphaMult = clamp(1 - (|IndexDiff| / 6 - 1), 0, 1), the subtraction only in a spiral;
    // _Scale = Shallowness ^ IndexDiff in a spiral, else 1.
    float alpha = 1.0f - (p.spiral ? std::fabs(t.diff) / 6.0f - 1.0f : 0.0f);
    t.alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
    t.scale = p.spiral ? std::pow(shallowness(o), t.diff) : 1.0f;
    // _Blank = _Index < -1; _Hover = PointerIndexRound == _IndexSigned, not a blank.
    t.blank = index < -1;
    t.hover = p.round == index * cs && !t.blank;
    // <visible>: the item's own and the blanks of the last loop, only while _AlphaMult is above 0.
    t.visible = t.alpha > 0.0f;
    return t;
}

std::vector<Tile> tiles(int items, const Pointer& p, const Options& o) {
    std::vector<Tile> out;
    const int n = items < 0 ? 0 : items;
    out.reserve(static_cast<std::size_t>(p.total));
    for (int index = -p.blanks; index < n; ++index) {
        out.push_back(tile(p, index, o));
    }
    return out;
}

float open_input(int equipped, const Options& o) {
    return static_cast<float>(equipped) * kSlotRadians * static_cast<float>(sign(o));
}

float turn(float input_rad, Stick& stick, float x, float y, int items, const Options& o, float deadzone) {
    // Normalised, or nothing at the centre (the mod's own: a length of 0); the mod's setting of a
    // dead zone is this layer's, for a real stick's rest.
    const float length = std::sqrt(x * x + y * y);
    if (length == 0.0f || length < deadzone) {
        return input_rad;
    }
    const float x1 = x / length;
    const float y1 = y / length;
    float x2 = stick.x;
    float y2 = stick.y;
    stick.x = x1;
    stick.y = y1;
    if (stick.first) {
        // The first input since the menu opened: the last direction is the pointer's own,
        // PointerAngle (degrees) + StartAngleDeg + 30 * ClockwiseSign, as a screen vector.
        stick.first = false;
        const Pointer p = pointer(items, input_rad, o);
        const float a = p.angle_deg * kPi / 180.0f;
        x2 = std::cos(a);
        y2 = -std::sin(a);
    }
    // ModUIFloat PointerAngleInput += atan2(X1*Y2 - Y1*X2, X1*X2 + Y1*Y2).
    return input_rad + std::atan2(x1 * y2 - y1 * x2, x1 * x2 + y1 * y2);
}

}  // namespace spiral
