// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral (src/spiral/) against "Prey Menu"'s own formulas: PreyMenu.xml's traits written out
// here as the XML evaluates them, step by step (its two conditional subtractions for the pointer's
// index, its three distances for an item's), over many pointer angles, item counts, both
// directions, start angles and depths; then the wheel's layout (wheel.hpp) and its stick
// (UpdatePointer.gek) and opening (UpdateMenu.gek).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <cstdio>
#include <vector>

#include "spiral/arrange.hpp"
#include "spiral/glyph.hpp"
#include "spiral/spiral.hpp"
#include "spiral/look.hpp"
#include "spiral/paint.hpp"
#include "spiral/wheel.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                             \
    } while (0)

bool near(float a, float b, float within = 1e-3f) { return std::fabs(a - b) <= within; }

// --- PreyMenu.xml, as written ---

struct Xml {
    float pointer_index, pointer_angle_deg;
    int round, round_signed, total, blanks;
    bool spiral;
};

// The traits from _Prey+TotalCount to the pointer's rotateangle, operator by operator.
Xml xml_pointer(int item_count, float pointer_angle_input, int clockwise_sign, float start_deg) {
    Xml x{};
    // <_Prey+TotalCount><copy ItemCount/><add>1</add><div>12</div><ceil/><mul>12</mul>
    x.total = static_cast<int>(std::ceil((item_count + 1) / 12.0f) * 12.0f);
    x.blanks = x.total - item_count;
    x.spiral = item_count >= 12;
    const float cs = static_cast<float>(clockwise_sign);
    const float input = pointer_angle_input * 1.9098593171f;
    // ShiftBack: <copy Input/><mul Sign/><sub ItemCount/><add>0.5</add><mul Sign/>
    const float back = ((input * cs) - item_count + 0.5f) * cs;
    // ShiftForward: <copy Input/><mul Sign/><add BlankCount/><add>0.5</add><mul Sign/>
    const float forward = ((input * cs) + x.blanks + 0.5f) * cs;
    // PointerIndex: <copy Input/><mul Sign/>
    float v = input * cs;
    //   <sub>(ShiftBack * Sign / Total, floor, + 1) * Total <onlyif ShiftForward < 0/></sub>
    if (forward < 0.0f) {
        v -= (std::floor(back * cs / x.total) + 1.0f) * x.total;
    }
    //   <sub>(ShiftForward * Sign / Total, floor) * Total <onlyif ShiftBack > 0/></sub>
    if (back > 0.0f) {
        v -= std::floor(forward * cs / x.total) * x.total;
    }
    //   <mul Sign/>
    x.pointer_index = v * cs;
    x.round = static_cast<int>(std::floor(x.pointer_index + 0.5f));
    x.round_signed = x.round * clockwise_sign;
    // Pointer rotateangle: 0.52359877559 * Sign + PointerAngle + StartAngleRad (degrees here).
    x.pointer_angle_deg = (0.52359877559f * cs + x.pointer_index / 1.9098593171f) * 57.2957795131f + start_deg;
    return x;
}

struct XmlTile {
    float angle_deg, diff, alpha, scale;
    bool hover;
};

// The item template's traits for _Index `index`.
XmlTile xml_tile(const Xml& p, int index, int clockwise_sign, float start_deg, float shallowness) {
    XmlTile t{};
    const float cs = static_cast<float>(clockwise_sign);
    const float signed_index = index * cs;
    t.angle_deg = (signed_index + cs) * 30.0f + start_deg;
    const float behind = (-p.total * cs + p.pointer_index) - signed_index;
    const float normal = p.pointer_index - signed_index;
    const float ahead = (p.total * cs + p.pointer_index) - signed_index;
    const float ab = std::fabs(behind), an = std::fabs(normal), aa = std::fabs(ahead);
    float diff = 0.0f;
    if (ab < an && ab < aa) diff += behind;
    if (an <= ab && an <= aa) diff += normal;
    if (aa < ab && aa < an) diff += ahead;
    t.diff = diff * cs;
    float alpha = 1.0f;
    if (p.spiral) alpha -= std::fabs(t.diff) / 6.0f - 1.0f;
    t.alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
    t.scale = p.spiral ? std::pow(shallowness, t.diff) : 1.0f;
    t.hover = p.round == static_cast<int>(signed_index) && index >= -1;
    return t;
}

// --- Tests ---

void test_against_the_xml() {
    int compared = 0;
    for (int items : {0, 1, 5, 11, 12, 13, 20, 23, 24, 25, 36}) {
        for (bool clockwise : {false, true}) {
            for (float start : {90.0f, 0.0f, -90.0f, 45.0f, 180.0f}) {
                for (float depth : {0.0f, 0.5f, 0.75f, 1.0f}) {
                    spiral::Options o;
                    o.clockwise = clockwise;
                    o.start_deg = start;
                    o.depth = depth;
                    const int cs = spiral::sign(o);
                    CHECK(cs == (clockwise ? -1 : 1));
                    CHECK(near(spiral::shallowness(o), 0.9f + 0.0666f * depth));
                    for (int k = -400; k <= 400; k += 7) {
                        const float input = static_cast<float>(k) * 0.0731f;
                        const spiral::Pointer p = spiral::pointer(items, input, o);
                        const Xml x = xml_pointer(items, input, cs, start);
                        CHECK(p.total == x.total && p.blanks == x.blanks && p.spiral == x.spiral);
                        CHECK(near(p.index, x.pointer_index));
                        CHECK(p.round == x.round && p.selected == x.round_signed);
                        CHECK(near(std::remainder(p.angle_deg - x.pointer_angle_deg, 360.0f), 0.0f, 0.01f));
                        for (int index = -p.blanks; index < items; ++index) {
                            const spiral::Tile t = spiral::tile(p, index, o);
                            const XmlTile u = xml_tile(x, index, cs, start, spiral::shallowness(o));
                            CHECK(near(t.angle_deg, u.angle_deg) && near(t.diff, u.diff));
                            CHECK(near(t.alpha, u.alpha) && near(t.scale, u.scale, 1e-3f * u.scale));
                            CHECK(t.hover == u.hover);
                            ++compared;
                        }
                    }
                }
            }
        }
    }
    CHECK(compared > 100000);
}

void test_slots_across_loops() {
    spiral::Options o;  // counter-clockwise from 12 o'clock
    const int items = 20;  // 24 slots: items 0..19, three blanks, "None"
    auto at = [&](float slots) { return spiral::pointer(items, slots * spiral::kSlotRadians, o); };
    // "None" sits at the start angle, the first item one slot on (30 degrees counter-clockwise).
    CHECK(at(-1).selected == -1 && near(at(-1).angle_deg, 90.0f));
    CHECK(at(0).selected == 0 && near(at(0).angle_deg, 120.0f));
    CHECK(near(spiral::tile(at(0), 0, o).angle_deg, 120.0f));
    // The second loop: item 13 is a whole turn on from item 1, at the same angle.
    CHECK(at(13).selected == 13);
    CHECK(near(std::remainder(spiral::tile(at(13), 13, o).angle_deg - spiral::tile(at(13), 1, o).angle_deg, 360.0f), 0.0f));
    // Past the last item: the three blanks, then "None", then the first item again.
    CHECK(at(20).selected == -4 && at(21).selected == -3 && at(22).selected == -2);
    CHECK(at(23).selected == -1 && at(24).selected == 0 && at(25).selected == 1);
    // And back the other way.
    CHECK(at(-2).selected == -2 && at(-5).selected == 19);
    // Half a slot is the boundary: the nearest slot.
    CHECK(at(2.49f).selected == 2 && at(2.51f).selected == 3);
    // Clockwise: the first item 30 degrees clockwise of the start, the input the other way.
    spiral::Options cw = o;
    cw.clockwise = true;
    const spiral::Pointer p = spiral::pointer(items, -3.0f * spiral::kSlotRadians, cw);
    CHECK(p.selected == 3);
    CHECK(near(spiral::tile(p, 0, cw).angle_deg, 60.0f));
    // Fewer than 12: one flat ring of 12, every slot full size and fully seen.
    const spiral::Pointer small = spiral::pointer(5, 2.0f * spiral::kSlotRadians, o);
    CHECK(!small.spiral && small.total == 12 && small.blanks == 7);
    for (const spiral::Tile& t : spiral::tiles(5, small, o)) {
        CHECK(t.scale == 1.0f && t.alpha == 1.0f && t.visible);
    }
}

void test_depth_and_fading() {
    spiral::Options o;
    const int items = 30;
    const spiral::Pointer p = spiral::pointer(items, 15.0f * spiral::kSlotRadians, o);
    // Behind the pointer smaller, ahead larger, by shallowness per slot.
    const spiral::Tile behind = spiral::tile(p, 12, o), here = spiral::tile(p, 15, o), ahead = spiral::tile(p, 18, o);
    CHECK(near(here.scale, 1.0f) && near(behind.scale, std::pow(0.95f, 3.0f)) && near(ahead.scale, std::pow(0.95f, -3.0f)));
    CHECK(here.hover && !behind.hover);
    // Full within 6 slots, half at 9, gone at 12 or more.
    CHECK(near(spiral::tile(p, 9, o).alpha, 1.0f) && near(spiral::tile(p, 6, o).alpha, 0.5f));
    CHECK(spiral::tile(p, 3, o).alpha == 0.0f && !spiral::tile(p, 3, o).visible);
    // A lower depth spreads the loops: the loop behind smaller.
    spiral::Options deep = o;
    deep.depth = 0.0f;
    CHECK(spiral::tile(spiral::pointer(items, 15.0f * spiral::kSlotRadians, deep), 3, deep).scale <
          spiral::tile(p, 3, o).scale);
}

void test_stick_and_opening() {
    spiral::Options o;
    // UpdateMenu.gek: opened on the equipped item.
    CHECK(near(spiral::open_input(4, o), 4.0f * spiral::kSlotRadians));
    o.clockwise = true;
    CHECK(near(spiral::open_input(4, o), -4.0f * spiral::kSlotRadians));
    o.clockwise = false;
    // UpdatePointer.gek: the first input is measured from the pointer's own direction (item 0 at
    // 120 degrees): the stick at 12 o'clock (screen (0, -1), 90 degrees) turns it back 30 degrees
    // onto "None".
    spiral::Stick stick;
    float input = spiral::open_input(0, o);
    input = spiral::turn(input, stick, 0.0f, -1.0f, 20, o);
    CHECK(near(input, -spiral::kSlotRadians) && spiral::pointer(20, input, o).selected == -1);
    // Then a quarter turn counter-clockwise (12 to 9 o'clock) adds 90 degrees: three slots on.
    input = spiral::turn(input, stick, -1.0f, 0.0f, 20, o);
    CHECK(near(input, 2.0f * spiral::kSlotRadians) && spiral::pointer(20, input, o).selected == 2);
    // Round and round: a full turn more is twelve slots on, the next loop.
    for (float a : {180.0f, 270.0f, 0.0f, 90.0f, 180.0f}) {
        input = spiral::turn(input, stick, std::cos(a * spiral::kPi / 180.0f), -std::sin(a * spiral::kPi / 180.0f), 20, o);
    }
    CHECK(near(input, 14.0f * spiral::kSlotRadians, 1e-3f) && spiral::pointer(20, input, o).selected == 14);
    // A stick at rest (inside the dead zone) leaves it.
    CHECK(near(spiral::turn(input, stick, 0.1f, 0.1f, 20, o, 0.5f), input));
    // Clockwise: the same quarter turn counter-clockwise goes back three slots.
    spiral::Options cw;
    cw.clockwise = true;
    spiral::Stick s2;
    float in2 = spiral::open_input(5, cw);
    const float at = spiral::pointer(20, in2, cw).angle_deg * spiral::kPi / 180.0f;
    in2 = spiral::turn(in2, s2, std::cos(at), -std::sin(at), 20, cw);
    CHECK(spiral::pointer(20, in2, cw).selected == 5);
    const float later = at + spiral::kPi / 2.0f;
    in2 = spiral::turn(in2, s2, std::cos(later), -std::sin(later), 20, cw);
    CHECK(spiral::pointer(20, in2, cw).selected == 2);
}

void test_wheel_frame() {
    std::vector<spiral::Item> items;
    for (int i = 0; i < 14; ++i) {
        spiral::Item it;
        it.id = 0x09 + i;
        it.icon = 100 + i;
        it.hot = i < 8 ? i + 1 : 0;
        it.equipped = i == 3;
        it.gadget = i == 5;
        items.push_back(it);
    }
    spiral::Look look;
    spiral::Wheel w;
    w.open(items, look);
    // Opened on the equipped item, zooming from nothing.
    CHECK(w.selected() == 3 && w.anim() < 0.01f);
    w.advance(0.05f);
    CHECK(near(w.anim(), 0.51f, 0.02f));
    w.advance(1.0f);
    CHECK(w.anim() == 1.0f);
    const spiral::Frame f = w.frame();
    CHECK(f.selected == 3);
    // The design's pixels at a 720 pixel screen; its middle 8 above the screen's.
    CHECK(near(f.unit, 1.0f / 720.0f) && near(f.centre_y, -8.0f / 720.0f));
    CHECK(near(f.name_y, (-8.0f - 23.0f) / 720.0f) && near(f.detail_y, (-8.0f + 18.5f) / 720.0f));
    // 14 items: 24 slots in two loops, the pointer's the first: its 12 at radius 284 down by 11 a
    // slot, the 4 before it (the spiral's last, blanks) and the 3 after it (items 12 and 13, a blank)
    // fading; the selected one drawn last, pushed 10 out.
    CHECK(near(w.anchor(), 0.0f));
    CHECK(f.slots.size() == 19u);
    CHECK(f.slots.back().selected && f.slots.back().index == 3 && near(f.slots.back().push, 10.0f));
    int selected = 0;
    for (const spiral::Slot& s : f.slots) {
        const int k = s.index >= 0 ? s.index : s.index + 24;
        const int from_first = k >= 20 ? k - 24 : k;  // -4 .. 14
        CHECK(near(s.radius, 284.0f - 11.0f * static_cast<float>(from_first)));
        CHECK(near(s.opacity, from_first >= 0 && from_first < 12 ? 1.0f
                              : from_first < 0 ? std::min(spiral::look::kBefore[-from_first - 1], 0.6f)
                                               : spiral::look::kAfter[from_first - 12]));
        CHECK(s.empty == (s.index < 0 || s.index >= 14));
        CHECK(s.gadget == (s.index == 5));
        CHECK(s.badge == (s.index >= 0 && s.index < 8 ? s.index + 1 : 0));
        selected += s.selected;
        CHECK(s.push == (s.selected ? 10.0f : 0.0f));
    }
    CHECK(selected == 1);
    // The outer first: the radii go down but for the selected one.
    for (std::size_t i = 1; i + 1 < f.slots.size(); ++i) {
        CHECK(f.slots[i].radius < f.slots[i - 1].radius);
    }
    // No glyphs: each item's own icon at its panel's middle, 0.64 of the thickness (0.7 selected).
    CHECK(f.icons.size() == 14u);
    for (const spiral::Icon& icon : f.icons) {
        const spiral::Slot* slot = nullptr;
        for (const spiral::Slot& s : f.slots) {
            slot = s.index == icon.item ? &s : slot;
        }
        CHECK(slot != nullptr);
        if (slot == nullptr) {
            continue;
        }
        const float r = slot->radius + slot->push;
        CHECK(near(std::hypot(icon.x, icon.y - f.centre_y) * 720.0f, r, 1e-2f));
        CHECK(near(icon.size * 720.0f, 0.33f * slot->radius * (slot->selected ? 0.7f : 0.64f), 1e-3f));
        CHECK(near(icon.alpha, slot->opacity));
    }
    // The badges: the quick select's eight, 2 in from the outer edge, 7.6 degrees clockwise.
    CHECK(f.badges.size() == 8u);
    for (const spiral::Badge& b : f.badges) {
        const spiral::Slot* slot = nullptr;
        for (const spiral::Slot& s : f.slots) {
            slot = s.badge == b.number ? &s : slot;
        }
        CHECK(slot != nullptr);
        if (slot == nullptr) {
            continue;
        }
        const float outer = slot->radius * 1.165f + slot->push - 2.0f;
        const float x = b.x * 720.0f, y = (b.y - f.centre_y) * 720.0f;
        CHECK(near(std::hypot(x, y), outer, 1e-2f));
        const float deg = std::atan2(-y, x) * 180.0f / spiral::kPi;
        CHECK(near(std::remainder(deg - (slot->angle_deg - 7.6f), 360.0f), 0.0f, 1e-3f));
    }
    // The d-pad's step: one slot on, onto the next item's middle.
    CHECK(w.step(1) && w.selected() == 4);
    CHECK(w.step(-2) && w.selected() == 2);
    // Into the second loop: the spiral eases there (its time constant 0.06 s), the loop's first slot
    // then at 284; the first loop is the one before it, fading.
    for (int i = 0; i < 10; ++i) {
        w.step(1);
    }
    CHECK(w.selected() == 12 && near(w.anchor_target(), 12.0f));
    w.advance(0.03f);
    CHECK(w.anchor() > 3.0f && w.anchor() < 9.0f);
    w.advance(1.0f);
    CHECK(near(w.anchor(), 12.0f));
    for (const spiral::Slot& s : w.frame().slots) {
        if (s.index == 12) {
            CHECK(near(s.radius, 284.0f));
        }
        if (s.index == 11) {
            CHECK(near(s.radius, 295.0f) && near(s.opacity, 0.55f));
        }
    }
    // Back into the first loop (with two loops either way round is as far): the spiral turns back,
    // the way the pointer went, item 11 gliding in from the loop before to the first loop's last.
    CHECK(w.step(-1) && w.selected() == 11 && near(w.anchor_target(), 0.0f));
    w.advance(0.03f);
    CHECK(w.anchor() > 3.0f && w.anchor() < 9.0f);
    for (const spiral::Slot& s : w.frame().slots) {
        if (s.index == 11) {
            CHECK(s.radius < 295.0f && s.radius > 163.0f && s.selected);
        }
    }
    w.advance(1.0f);
    CHECK(near(w.anchor(), 0.0f));
    // A smaller wheel: everything scales.
    look.scale = 0.5f;
    spiral::Wheel half;
    half.open(items, look);
    half.advance(1.0f);
    CHECK(near(half.frame().unit, 0.5f / 720.0f));
    // The depth setting: the radius step, 11 at its default and below (the screen is full), smaller
    // above it.
    spiral::Options o;
    CHECK(near(spiral::radius_step(o), 11.0f));
    o.depth = 0.0f;
    CHECK(near(spiral::radius_step(o), 11.0f));
    o.depth = 1.0f;
    CHECK(spiral::radius_step(o) < 8.0f && spiral::radius_step(o) >= 5.0f);
    // The opacity between whole slots: linear.
    CHECK(near(spiral::slot_opacity(-0.5f, true), (1.0f + 0.55f) / 2.0f));
    CHECK(near(spiral::slot_opacity(14.5f, true), 0.09f) && spiral::slot_opacity(15.0f, true) == 0.0f);
    CHECK(spiral::slot_opacity(-5.0f, true) == 0.0f && spiral::slot_opacity(11.0f, false) == 1.0f);
    // Fewer than 12 items: one loop, each slot once, no fading.
    std::vector<spiral::Item> few(items.begin(), items.begin() + 5);
    spiral::Wheel ring;
    ring.open(few, spiral::Look{});
    ring.advance(1.0f);
    const spiral::Frame rf = ring.frame();
    CHECK(rf.slots.size() == 12u);
    for (const spiral::Slot& s : rf.slots) {
        CHECK(s.opacity == (s.empty ? 0.6f : 1.0f));
    }
}

// The panels as quads (paint.hpp): what each slot, the middle and the pointer are drawn with.
void test_paint() {
    std::vector<spiral::Item> items(14);
    for (int i = 0; i < 14; ++i) {
        items[static_cast<std::size_t>(i)].id = 0x09 + i;
        items[static_cast<std::size_t>(i)].hot = i < 8 ? i + 1 : 0;
    }
    items[2].equipped = true;
    items[2].glyph = 71;  // the selected one: a weapon
    items[2].glyph_width = items[2].glyph_height = 100;
    items[4].glyph = 72;  // a gadget, 100 x 50
    items[4].gadget = true;
    items[4].glyph_width = 100;
    items[4].glyph_height = 50;
    spiral::Wheel w;
    w.open(items, spiral::Look{});
    w.advance(1.0f);
    const spiral::Frame f = w.frame();
    const std::vector<spiral::Quad> quads = spiral::paint(f);
    int pointers = 0, badges = 0;
    std::vector<const spiral::Quad*> glyphs;
    float min_v = 1.0f, max_v = 0.0f;
    bool washed_weapon = false, washed_gadget = false, washed_selected = false, yellow_frame = false;
    for (const spiral::Quad& q : quads) {
        pointers += q.art == spiral::Art::Pointer;
        badges += q.art == spiral::Art::Badge;
        if (q.art == spiral::Art::Glyph) {
            glyphs.push_back(&q);
        }
        if (q.art == spiral::Art::Scan) {
            for (int k = 0; k < 4; ++k) {
                min_v = std::min(min_v, q.uv[2 * k + 1]);
                max_v = std::max(max_v, q.uv[2 * k + 1]);
            }
        }
        auto is = [&q](const spiral::look::Rgb& c, float alpha) {
            return near(q.rgba[0], c.r / 255.0f) && near(q.rgba[1], c.g / 255.0f) && near(q.rgba[2], c.b / 255.0f) &&
                   near(q.rgba[3], alpha);
        };
        washed_weapon |= q.art == spiral::Art::White && is(spiral::look::kWashWeapon, 0.30f);
        washed_gadget |= q.art == spiral::Art::White && is(spiral::look::kWashGadget, 0.30f);
        washed_selected |= q.art == spiral::Art::White && is(spiral::look::kWashWeapon, 0.45f);
        yellow_frame |= q.art == spiral::Art::White && is(spiral::look::kFrameSelected[3].colour, 1.0f);
    }
    CHECK(pointers == 1 && badges == 8);
    CHECK(washed_weapon && washed_gadget && washed_selected && yellow_frame);
    // The scanlines' places stay inside their picture.
    CHECK(min_v > 0.0f && max_v < 1.0f);
    // Each glyph twice, its shadow first (dark, 1.5 across and 2.5 down), then in its colour: the
    // gadget's light green at 0.64 of the thickness, the selected weapon's highlight at 0.7.
    CHECK(glyphs.size() == 4u);
    if (glyphs.size() == 4u) {
        for (int i = 0; i < 4; i += 2) {
            const spiral::Quad& shadow = *glyphs[static_cast<std::size_t>(i)];
            const spiral::Quad& glyph = *glyphs[static_cast<std::size_t>(i + 1)];
            CHECK(shadow.image == glyph.image);
            CHECK(near(shadow.rgba[0], 7.0f / 255.0f) && near(shadow.rgba[2], 13.0f / 255.0f));
            CHECK(near((shadow.xy[0] - glyph.xy[0]) * 720.0f, 1.5f, 1e-3f) && near((shadow.xy[1] - glyph.xy[1]) * 720.0f, 2.5f, 1e-3f));
        }
        const spiral::Quad& gadget = *glyphs[1];
        const spiral::Quad& weapon = *glyphs[3];  // the selected one is drawn last
        CHECK(gadget.image == 72 && weapon.image == 71);
        CHECK(near(gadget.rgba[1], 0xE4 / 255.0f) && near(weapon.rgba[0], 0xE2 / 255.0f) && near(weapon.rgba[2], 0x4E / 255.0f));
        const float r4 = 284.0f - 11.0f * 4.0f, r2 = 284.0f - 11.0f * 2.0f;
        CHECK(near((gadget.xy[2] - gadget.xy[0]) * 720.0f, 0.64f * 0.33f * r4, 1e-2f));
        CHECK(near((gadget.xy[5] - gadget.xy[1]) * 720.0f, 0.64f * 0.33f * r4 / 2.0f, 1e-2f));
        CHECK(near((weapon.xy[2] - weapon.xy[0]) * 720.0f, 0.70f * 0.33f * r2, 1e-2f));
    }
    // Every corner on the screen near the wheel (the largest radius with its frame, 5 more).
    for (const spiral::Quad& q : quads) {
        for (int k = 0; k < 4; ++k) {
            CHECK(std::hypot(q.xy[2 * k], q.xy[2 * k + 1] - f.centre_y) * 720.0f < 284.0f + 44.0f + 0.165f * 328.0f + 10.0f);
        }
    }
    // The pictures: the scanlines a 3 pixel period (10 rows of 4000 over 1200 pixels), the light
    // part 1.4 of it; the badge grey graded down with a dark edge; the pointer's tip at its top.
    const spiral::Picture scan = spiral::make_art(spiral::Art::Scan);
    CHECK(scan.height == spiral::kScanRows && scan.width == 2);
    auto row = [&scan](int y) { return scan.rgba[static_cast<std::size_t>(y) * 2 * 4]; };
    CHECK(row(0) == 0x1C && row(3) == 0x1C && row(5) == 0x0D && row(9) == 0x0D && row(10) == 0x1C);
    CHECK(row(4) > 0x0D && row(4) < 0x1C);  // 0.67 of row 4 is lit
    const spiral::Picture badge = spiral::make_art(spiral::Art::Badge);
    CHECK(badge.width == 26 * 4 && badge.height == 20 * 4);
    auto at = [](const spiral::Picture& p, int x, int y, int c) {
        return p.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width) + static_cast<std::size_t>(x)) * 4 +
                      static_cast<std::size_t>(c)];
    };
    CHECK(at(badge, 52, 12, 0) > at(badge, 52, 68, 0));  // lighter at the top
    CHECK(at(badge, 52, 4, 3) == 255 && at(badge, 52, 4, 0) < 0x30);  // the edge
    CHECK(at(badge, 0, 0, 3) == 0);
    const spiral::Picture pointer = spiral::make_art(spiral::Art::Pointer);
    CHECK(at(pointer, pointer.width / 2, pointer.height / 2, 3) == 255);
    CHECK(at(pointer, pointer.width / 2, pointer.height / 2, 0) == 0xE2);  // filled yellow
    CHECK(at(pointer, 0, 0, 3) == 0 && at(pointer, pointer.width - 1, 0, 3) == 0);  // the tip is narrow
}

// The game's quick select as the home ring: its eight items at the directions of its own ring
// (slot i at 90 - 45 * i degrees: games/rac1/decomp draw_hud_item_selector_panel).
std::vector<spiral::HomeItem> home_ring(const std::vector<std::int32_t>& ids) {
    std::vector<spiral::HomeItem> out;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] != 0) {
            spiral::HomeItem h;
            h.item.id = ids[i];
            h.item.hot = static_cast<int>(i) + 1;
            h.angle_deg = 90.0f - 45.0f * static_cast<float>(i);
            out.push_back(h);
        }
    }
    return out;
}

std::vector<spiral::Item> others(std::initializer_list<std::int32_t> ids) {
    std::vector<spiral::Item> out;
    for (std::int32_t id : ids) {
        spiral::Item it;
        it.id = id;
        out.push_back(it);
    }
    return out;
}

int index_of(const std::vector<spiral::Item>& slots, std::int32_t id) {
    int found = -100, count = 0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].id == id) {
            found = static_cast<int>(i);
            ++count;
        }
    }
    CHECK(count == 1);  // no duplicates, none lost
    return found;
}

void test_home_ring() {
    const std::vector<std::int32_t> ring = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    // Counter-clockwise from 12 o'clock (the defaults): item slot 0 is on the start angle (no
    // "None"); the cardinals exactly; the diagonals to the slot nearer the vertical (45 -> 60,
    // -45 -> -60, -135 -> -120, 135 -> 120).
    {
        spiral::Options o;
        const auto slots = spiral::arrange(home_ring(ring), others({0x01, 0x02, 0x03, 0x04, 0x05, 0x06}), o);
        const int expect[8] = {0, 11, 9, 7, 6, 5, 3, 1};
        for (int i = 0; i < 8; ++i) {
            CHECK(index_of(slots, ring[static_cast<std::size_t>(i)]) == expect[i]);
        }
        // The others fill the first loop's free slots in order (2, 4, 8, 10), then outward (12, 13).
        CHECK(index_of(slots, 0x01) == 2 && index_of(slots, 0x02) == 4 && index_of(slots, 0x03) == 8);
        CHECK(index_of(slots, 0x04) == 10 && index_of(slots, 0x05) == 12 && index_of(slots, 0x06) == 13);
        CHECK(slots.size() == 14u);
    }
    // Every direction within 15 degrees of its slot (exactly for the cardinals when the start angle
    // is a multiple of 30), both directions, other start angles; all eight on the first loop; no two
    // items share a slot.
    for (bool clockwise : {false, true}) {
        for (float start : {90.0f, 0.0f, 150.0f, -60.0f, -45.0f, 200.0f}) {
            spiral::Options o;
            o.clockwise = clockwise;
            o.start_deg = start;
            const auto slots = spiral::arrange(home_ring(ring), others({0x01, 0x02, 0x03, 0x04, 0x05}), o);
            for (int i = 0; i < 8; ++i) {
                const int index = index_of(slots, ring[static_cast<std::size_t>(i)]);
                const float want = 90.0f - 45.0f * static_cast<float>(i);
                const float off = std::fabs(std::remainder(spiral::slot_angle(index, o) - want, 360.0f));
                CHECK(off <= 15.01f);
                if (std::fabs(std::remainder(start, 30.0f)) < 0.01f && i % 2 == 0) {
                    CHECK(off < 0.01f);
                }
                CHECK(index >= 0 && index < spiral::kFirstLoop);
            }
            for (std::int32_t id : {0x01, 0x02, 0x03, 0x04, 0x05}) {
                index_of(slots, id);
            }
            // The wheel draws slot i where slot_angle says.
            spiral::Look look;
            look.spiral = o;
            spiral::Wheel w;
            auto items = slots;
            items[0].equipped = items[0].id != 0;
            w.open(items, look);
            w.advance(1.0f);
            const float drawn = spiral::tile(spiral::pointer(static_cast<int>(items.size()), w.input(), w.spiral_options()), 5,
                                             w.spiral_options())
                                    .angle_deg;
            CHECK(std::fabs(std::remainder(drawn - spiral::slot_angle(5, o), 360.0f)) < 0.01f);
        }
    }
    // Empty quick select slots leave their places to the others; with few others, holes (id 0)
    // stay in the first loop.
    {
        spiral::Options o;
        const auto slots = spiral::arrange(home_ring({0x10, 0, 0x12, 0, 0, 0, 0x15, 0}), others({0x01}), o);
        CHECK(index_of(slots, 0x10) == 0 && index_of(slots, 0x12) == 9 && index_of(slots, 0x15) == 3);
        CHECK(index_of(slots, 0x01) == 1);  // the first free slot
        int holes = 0;
        for (const spiral::Item& it : slots) {
            holes += it.id == 0;
        }
        CHECK(slots.size() == 10u && holes == 6);
        // A hole, and the spiral's slot before the first item (Prey Menu's "None"), are blanks: no
        // border, no icon, not lit, not picked.
        spiral::Look look;
        spiral::Wheel w;
        auto items = slots;
        items[static_cast<std::size_t>(index_of(slots, 0x12))].equipped = true;
        w.open(items, look);
        w.advance(1.0f);
        CHECK(w.selected() == 9);
        CHECK(!w.step(-1) && w.selected() == 8 && !w.is_item(8));
        int filled = 0, lit = 0;
        for (const spiral::Slot& sl : w.frame().slots) {
            filled += !sl.empty;
            lit += sl.selected;
        }
        CHECK(filled == 4 && lit == 0);  // the 4 items, no "None"
        CHECK(!w.is_item(-1));
    }
}

// A synthetic HUD icon, as the game's look: a diagonal two-tone ground (lighter top left), a
// near-black glyph (a ring of radii `inner` to `outer` about the middle, and a bar), the edge
// anti-aliased by 4 x 4 supersampling; `corner_glyph` puts glyph in the top-right corner.
spiral::Picture synthetic_icon(const std::uint8_t ground[3], float inner, float outer, bool corner_glyph) {
    spiral::Picture p;
    p.width = p.height = 32;
    p.rgba.resize(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            float ink = 0.0f;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const float fx = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / 4.0f - 16.0f;
                    const float fy = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / 4.0f - 16.0f;
                    const float r = std::sqrt(fx * fx + fy * fy);
                    const bool in = (r >= inner && r <= outer) || (std::fabs(fx) < 1.5f && fy > 6.0f && fy < 14.0f) ||
                                    (corner_glyph && fx > 9.0f && fy < -9.0f);
                    ink += in ? 1.0f / 16.0f : 0.0f;
                }
            }
            const float shine = 1.0f - 0.25f * static_cast<float>(x + y) / 62.0f;  // the gradient
            std::uint8_t* o = &p.rgba[static_cast<std::size_t>(y * 32 + x) * 4];
            for (int k = 0; k < 3; ++k) {
                const float g = static_cast<float>(ground[k]) * shine;
                o[k] = static_cast<std::uint8_t>(std::lround(g * (1.0f - ink) + 15.0f * ink));
            }
            o[3] = 255;
        }
    }
    return p;
}

void test_glyphs() {
    // PSMT8 with a CSM1 palette: index 8 reads entry 16, index 16 entry 8; alpha 0x80 is opaque.
    {
        std::vector<std::uint8_t> palette(1024, 0);
        palette[16 * 4 + 0] = 200;
        palette[16 * 4 + 3] = 0x80;
        palette[8 * 4 + 1] = 100;
        palette[8 * 4 + 3] = 0x40;
        const std::uint8_t indices[4] = {8, 16, 0, 8};
        const spiral::Picture p = spiral::decode_psmt8(indices, 2, 2, palette.data());
        CHECK(p.rgba[0] == 200 && p.rgba[3] == 255);
        CHECK(p.rgba[5] == 100 && p.rgba[7] == 128);
    }
    const std::uint8_t blue[3] = {110, 115, 221};
    const std::uint8_t green[3] = {105, 215, 108};
    // A ring glyph on blue: the tint is the top-right corner's ground, the glyph cut out, cropped to
    // the ring (radius 11: 22 pixels and a pixel of room each side, four times as big), near black,
    // its edge smooth: along the ring the drawn radius varies by under a big pixel.
    {
        const spiral::Glyph g = spiral::make_glyph(synthetic_icon(blue, 8.0f, 11.0f, false));
        CHECK(g.ok && g.tint_from_corner);
        // The corner two pixels in is the ground there, a little darker than at the top left.
        CHECK(std::abs(g.tint[0] - 110 * 0.82) < 10 && std::abs(g.tint[2] - 221 * 0.82) < 12);
        CHECK(g.picture.width >= 22 * 4 && g.picture.width <= 26 * 4);
        CHECK(g.picture.rgba[0] < 40);  // the glyph's own dark colour
        // The ring's outer edge: where alpha crosses half, in 64 directions about the centre.
        // The ring's centre (16, 16) in the crop: it starts a pixel before the ring's left and top
        // (16 - 11 - 1 = 4), four big pixels to a pixel.
        const float cx = (16.0f - 4.0f) * 4.0f, cy = cx;
        float lo = 1e9f, hi = 0.0f;
        for (int k = 0; k < 64; ++k) {
            const float a = static_cast<float>(k) * 2.0f * spiral::kPi / 64.0f;
            if (std::sin(a) < -0.3f) {
                continue;  // not across the bar (below the centre: y grows downward)
            }
            float edge = 0.0f;
            for (float r = 0.0f; r < cx; r += 0.25f) {
                const int x = static_cast<int>(cx + r * std::cos(a)), y = static_cast<int>(cy - r * std::sin(a));
                if (x < 0 || y < 0 || x >= g.picture.width || y >= g.picture.height) {
                    break;
                }
                if (g.picture.rgba[static_cast<std::size_t>(y * g.picture.width + x) * 4 + 3] >= 128) {
                    edge = r;
                }
            }
            lo = std::min(lo, edge);
            hi = std::max(hi, edge);
        }
        CHECK(hi > 0.0f && hi - lo < 4.0f);  // within a source pixel all round (4 big pixels)
        CHECK(std::fabs((lo + hi) / 2.0f - 11.0f * 4.0f) < 4.0f);
    }
    // The glyph in the top-right corner: the tint from the ring's brighter half instead.
    {
        const spiral::Glyph g = spiral::make_glyph(synthetic_icon(green, 8.0f, 11.0f, true));
        CHECK(g.ok && !g.tint_from_corner);
        CHECK(g.tint[1] > 150 && g.tint[0] < 120);
    }
    // No glyph (a flat square): not one.
    {
        spiral::Picture flat = synthetic_icon(blue, 100.0f, 101.0f, false);
        for (std::size_t i = 0; i < flat.rgba.size(); i += 4) {
            flat.rgba[i] = 110;
            flat.rgba[i + 1] = 115;
            flat.rgba[i + 2] = 221;
        }
        CHECK(!spiral::make_glyph(flat).ok);
    }
    // In the wheel: a glyph is the slot's, the game's icon not drawn for it.
    {
        std::vector<spiral::Item> items(3);
        for (int i = 0; i < 3; ++i) {
            items[static_cast<std::size_t>(i)].id = 0x20 + i;
            items[static_cast<std::size_t>(i)].icon = 0x100 + i;
        }
        items[0].equipped = true;
        items[1].glyph = 77;
        items[1].glyph_width = 100;
        items[1].glyph_height = 50;
        spiral::Wheel w;
        w.open(items, spiral::Look{});
        w.advance(1.0f);
        const spiral::Frame f = w.frame();
        CHECK(f.icons.size() == 2u);  // items 0 and 2: the game's own icons
        bool glyph = false;
        for (const spiral::Slot& sl : f.slots) {
            if (sl.index == 1) {
                glyph = sl.glyph == 77 && sl.glyph_width == 100 && sl.glyph_height == 50;
            }
        }
        CHECK(glyph);
    }
}

}  // namespace

int main() {
    test_against_the_xml();
    test_slots_across_loops();
    test_depth_and_fading();
    test_stick_and_opening();
    test_wheel_frame();
    test_paint();
    test_home_ring();
    test_glyphs();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
