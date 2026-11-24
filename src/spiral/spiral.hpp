// SPDX-License-Identifier: GPL-3.0-or-later
//
// The spiral of "Prey Menu" (KerosenileArsonist's Fallout: New Vegas mod,
// https://www.nexusmods.com/newvegas/mods/94499, used with its author's credit as the Nexus
// permissions ask: THIRD_PARTY_NOTICES.md), ported from its menu and scripts. Game-agnostic: no
// game, no SDK. Each function names the trait or script it follows; the files are:
//
//   menus/prefabs/PreyMenu/PreyMenu.xml                       the traits _Prey+TotalCount,
//       _Prey+BlankCount, _Prey+Spiral, _Prey+PointerIndex*, _Prey+PointerAngle, and the item
//       template's _AngleDeg, _IndexDiff, _AlphaMult, _Scale and _Hover; the sizes and radii
//   NVSE/plugins/scripts/ln_PreyMenu.txt                     ClockwiseSign, Shallowness
//   NVSE/user_defined_functions/PreyMenu/UpdatePointer.gek   the stick turns the pointer
//   NVSE/user_defined_functions/PreyMenu/UpdateMenu.gek      the pointer starts on the equipped item
//
// Angles are degrees counter-clockwise from 3 o'clock as shown (y up), as the mod's are; screen
// vectors have y down, as the mod's UpdatePointer has them.
#pragma once

#include <vector>

namespace spiral {

// Twelve slots a loop: each is 30 degrees (the sector picture spans -15 to +15 degrees).
constexpr int kPerLoop = 12;
constexpr float kPi = 3.14159265358979f;
// pi / 6 and 6 / pi, as the mod writes them (0.52359877559, 1.9098593171).
constexpr float kSlotRadians = kPi / 6.0f;
constexpr float kSlotsPerRadian = 6.0f / kPi;

// The settings the mod's ini and menu have (General: fStartAngle, bClockwise, fDepth).
struct Options {
    float start_deg = 90.0f;  // the first item's angle: 90 is 12 o'clock
    bool clockwise = false;
    float depth = 0.75f;      // 0 to 1
};

// ln_PreyMenu.txt: ClockwiseSign = 1 - 2 * bClockwise (1 counter-clockwise, -1 clockwise).
int sign(const Options& o);
// ln_PreyMenu.txt: Shallowness = 0.9 + 0.0666 * fDepth (0.95 at the default 0.75). Each slot
// behind the pointer is drawn this much smaller, each slot ahead this much larger: a smaller
// depth puts the loops further apart.
float shallowness(const Options& o);

// Where the pointer is, from the angle the stick has turned it (_Prey+PointerAngleInput, radians).
struct Pointer {
    float index = 0;      // _Prey+PointerIndex: in slots, wrapped into the spiral (signed)
    int round = 0;        // _Prey+PointerIndexRound: the slot it points at (signed)
    int selected = 0;     // _Prey+PointerIndexRoundSigned: the item (0 up), -1 "None", below a blank
    float angle_deg = 0;  // the pointer picture's turn (its rotateangle, in degrees)
    int total = 0;        // _Prey+TotalCount: whole loops of 12 for the items and "None"
    int blanks = 0;       // _Prey+BlankCount: the slots that are not items ("None" and the blanks)
    bool spiral = false;  // _Prey+Spiral: 12 items or more make a spiral; fewer, one flat ring
};

Pointer pointer(int items, float input_rad, const Options& o);

// One slot of the spiral: an item (0 up), "None" (-1) or a blank (below -1).
struct Tile {
    int index = 0;
    float angle_deg = 0;  // _AngleDeg: where it is
    float diff = 0;       // _IndexDiff: slots from the pointer, the nearest way round; > 0 behind
    float alpha = 1;      // _AlphaMult: 1 within 6 slots, gone at 12 (a spiral only)
    float scale = 1;      // _Scale: shallowness ^ diff (a spiral only)
    bool blank = false;   // _Blank
    bool hover = false;   // _Hover: the pointer is on it
    bool visible = true;  // its alpha above 0
};

Tile tile(const Pointer& p, int index, const Options& o);

// Every slot from the first blank to the last item, in the order the mod draws them (<depth> is
// 10 * index: a later item over an earlier one).
std::vector<Tile> tiles(int items, const Pointer& p, const Options& o);

// UpdateMenu.gek: the pointer as the menu opens, on the equipped item (-1: on "None").
float open_input(int equipped, const Options& o);

// UpdatePointer.gek: the stick's direction last time, and whether the next is the first since the
// menu opened (then the last direction is taken as the pointer's own).
struct Stick {
    float x = 0;
    float y = 0;
    bool first = true;
};

// UpdatePointer.gek: the pointer's angle turned by the stick, from the last direction to (x, y)
// (screen space: y down), the signed angle between them. A stick inside `deadzone` (0: only the
// centre, as the mod's own) leaves both alone.
float turn(float input_rad, Stick& stick, float x, float y, int items, const Options& o,
           float deadzone = 0.0f);

}  // namespace spiral
