// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral (src/spiral/) against "Prey Menu"'s own formulas: PreyMenu.xml's traits written out
// here as the XML evaluates them, step by step (its two conditional subtractions for the pointer's
// index, its three distances for an item's), over many pointer angles, item counts, both
// directions, start angles and depths; then the wheel's layout (wheel.hpp) and its stick
// (UpdatePointer.gek) and opening (UpdateMenu.gek).
#include <cmath>
#include <cstdio>
#include <vector>

#include "spiral/spiral.hpp"
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
    int sectors = 0, borders = 0, eq = 0, hot = 0, eq_hot = 0, badges = 0, pointers = 0;
    for (const spiral::Sprite& s : f.sprites) {
        sectors += s.art == spiral::Art::Sector;
        borders += s.art == spiral::Art::Border;
        eq += s.art == spiral::Art::BorderEq;
        hot += s.art == spiral::Art::BorderHot;
        eq_hot += s.art == spiral::Art::BorderEqHot;
        pointers += s.art == spiral::Art::Pointer;
        if (s.art == spiral::Art::Badge) {
            ++badges;
            CHECK(s.grey == 0.0f && s.badge >= 1 && s.badge <= 8);  // black numbers
        }
    }
    // 14 items: two loops of 24 slots; the visible ones each have a sector, the items a border.
    CHECK(sectors > 14 && pointers == 1);
    CHECK(eq_hot == 1 && eq == 0 && hot + eq_hot == badges && badges <= 8);
    CHECK(borders + eq + hot + eq_hot <= 15);  // the items and "None"
    // The pointed-at sector is lit, the others black.
    int lit = 0;
    for (const spiral::Sprite& s : f.sprites) {
        lit += s.art == spiral::Art::Sector && s.grey > 0.0f;
    }
    CHECK(lit == 1);
    // The icons: on the radius, the pointed-at one full size.
    bool found = false;
    for (const spiral::Icon& icon : f.icons) {
        if (icon.item == 3) {
            found = true;
            CHECK(near(std::sqrt(icon.x * icon.x + icon.y * icon.y), spiral::kSize * spiral::kRadius));
            CHECK(near(icon.size, spiral::kSize * spiral::kIcon));
        }
    }
    CHECK(found);
    // The d-pad's step: one slot on, onto the next item's middle.
    CHECK(w.step(1) && w.selected() == 4);
    CHECK(w.step(-2) && w.selected() == 2);
    // A smaller wheel: everything scales.
    look.scale = 0.5f;
    spiral::Wheel half;
    half.open(items, look);
    half.advance(1.0f);
    CHECK(near(half.frame().sprites.back().w, spiral::kInfoWidth * 0.5f));
}

}  // namespace

int main() {
    test_against_the_xml();
    test_slots_across_loops();
    test_depth_and_fading();
    test_stick_and_opening();
    test_wheel_frame();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
