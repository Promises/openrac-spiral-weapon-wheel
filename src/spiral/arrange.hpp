// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where each item goes on the spiral (the mod's own rule, game-agnostic): the game's own quick
// select is the home ring. Its items take the first loop's slots nearest the directions they have
// on the game's ring, so a stick pushed the way the player knows points at the same item; the
// other items fill the first loop's free slots in spiral order, then the loops further out. A slot
// of the first loop left over is a hole (an Item with id 0), drawn and picked as a blank.
//
// The first loop is spiral indices 0 to 10, with "None" at -1 (the start angle, spiral.hpp); index
// 11 is the next loop's slot at the start angle again.
#pragma once

#include <string>
#include <vector>

#include "spiral.hpp"
#include "wheel.hpp"

namespace spiral {

// An item of the game's quick select and the direction its slot has on the game's ring (degrees
// counter-clockwise from 3 o'clock, y up).
struct HomeItem {
    Item item;
    float angle_deg = 0;
};

// The first loop's slots (indices 0 to 10).
constexpr int kFirstLoop = kPerLoop - 1;

// The slot `index` is drawn at (_AngleDeg).
float slot_angle(int index, const Options& o);

// The items in spiral order: the home items at their nearest first-loop slots, the rest after.
// Rules, in order:
//  - home items placed in order of how closely a free slot fits them (an exact fit first: the
//    cardinal directions on a start angle that is a multiple of 30);
//  - a tie between two slots (a diagonal, 15 degrees from each) goes to the slot nearer the
//    vertical (up or down), so the two diagonals beside one side never meet;
//  - a home item whose nearest direction is "None"'s (the start angle) takes the slot one loop out
//    at that same angle (index 11), and `notes` says so;
//  - the other items fill the free first-loop slots in index order, then indices 11 on.
std::vector<Item> arrange(const std::vector<HomeItem>& home, const std::vector<Item>& rest, const Options& o,
                          std::vector<std::string>* notes = nullptr);

}  // namespace spiral
