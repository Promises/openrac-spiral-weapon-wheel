// SPDX-License-Identifier: GPL-3.0-or-later
//
// The wheel's look (1.2.0): the user's design "Quick select · Menu panels" and its rules ("Helper
// rules · slot states and palette"), every slot a menu panel bent into a sector, after Ratchet &
// Clank's own menu panels. Its sizes are the design's pixels at 1280 x 720 (kDesignHeight a screen
// height), its colours its hex values. Game-agnostic: the painter (paint.hpp) turns them into
// pictures for a game's overlay.
#pragma once

#include <cstdint>

namespace spiral::look {

// The design's screen: 720 pixels tall. The wheel's middle is 8 pixels above the screen's.
constexpr float kDesignHeight = 720.0f;
constexpr float kCentreY = -8.0f;

// The continuous spiral: a loop's first slot at radius 284 (to the middle of its panel), each slot
// after it 11 smaller; a panel 0.33 of its radius thick, 25.2 degrees of the 30 a slot has; the
// selected one pushed 10 out (its thickness its own radius's).
constexpr float kFirstRadius = 284.0f;
constexpr float kRadiusStep = 11.0f;
constexpr float kThickness = 0.33f;
constexpr float kHalfAngle = 12.6f;
constexpr float kPush = 10.0f;

// What is drawn of the loops either side of the pointer's: the 4 slots before its first (their
// opacity 55, 42, 30, 22 %) and the 3 after its last (50, 32, 18 %). An empty slot of the loop
// itself is drawn at 60 %.
constexpr float kBefore[4] = {0.55f, 0.42f, 0.30f, 0.22f};
constexpr float kAfter[3] = {0.50f, 0.32f, 0.18f};
constexpr float kEmpty = 0.60f;

// The glyph: 0.64 of the thickness square (0.7 selected), upright, at the panel's middle; its drop
// shadow 1.5 across and 2.5 down.
constexpr float kGlyph = 0.64f;
constexpr float kGlyphSelected = 0.70f;
constexpr float kShadowX = 1.5f, kShadowY = 2.5f;

// The badge (a quick select number): 24 x 18, corners of 3, centred 2 in from the outer edge and
// 7.6 degrees clockwise of the panel's middle.
constexpr float kBadgeWidth = 24.0f, kBadgeHeight = 18.0f, kBadgeCorner = 3.0f;
constexpr float kBadgeIn = 2.0f, kBadgeTurn = 7.6f;

// The glint: an arc 6 in from the outer edge, 1.5 wide, from 3.6 to 9.6 degrees clockwise of the
// middle.
constexpr float kGlintIn = 6.0f, kGlintWidth = 1.5f, kGlintFrom = 3.6f, kGlintTo = 9.6f;

// The middle: a round panel of radius 110, its frame wider, two glints at 101 (2 wide): from -20
// to -70 degrees and from 115 to 160.
constexpr float kCentreRadius = 110.0f;
constexpr float kCentreGlintRadius = 101.0f, kCentreGlintWidth = 2.0f;

// The pointer: a triangle, its tip 134 out, its base 118 out across 6 degrees either side.
constexpr float kPointerTip = 134.0f, kPointerBase = 118.0f, kPointerHalfAngle = 6.0f;
constexpr float kPointerStroke = 2.0f;

// The text: the name's middle 23 above the wheel's, the ammo's 18.5 below (the design's 21 and
// 28 pixel lines, 10 apart, centred).
constexpr float kNameY = -23.0f, kAmmoY = 18.5f;

// The scanlines: 3 a period, the light one 1.4 of it.
constexpr float kScanPeriod = 3.0f, kScanLight = 1.4f;

struct Rgb {
    std::uint8_t r, g, b;
};
// A frame's four strokes, widest (the outer edge's dark) first, each centred on the panel's edge.
struct Band {
    float width;
    Rgb colour;
};
constexpr Band kFrame[4] = {{7.5f, {0x1B, 0x18, 0x0B}}, {5.9f, {0x5F, 0x5E, 0x48}}, {3.8f, {0x85, 0x84, 0x63}},
                            {1.5f, {0xB0, 0xAE, 0x8A}}};
// The selected slot's ("Load Game"'s yellow frame).
constexpr Band kFrameSelected[4] = {{9.0f, {0x2C, 0x2B, 0x12}}, {7.0f, {0x8F, 0x8C, 0x3C}}, {4.5f, {0xC8, 0xC4, 0x4E}},
                                    {1.8f, {0xEE, 0xEC, 0x84}}};
// The middle's (the khaki, wider).
constexpr Band kFrameCentre[4] = {{9.0f, {0x1B, 0x18, 0x0B}}, {7.0f, {0x5F, 0x5E, 0x48}}, {4.5f, {0x85, 0x84, 0x63}},
                                  {1.8f, {0xB0, 0xAE, 0x8A}}};

constexpr Rgb kScanDark = {0x0D, 0x0C, 0x13};
constexpr Rgb kScanLightColour = {0x1C, 0x1B, 0x22};
// The slots' wash, 30 % (45 % selected): weapons blue, gadgets green.
constexpr Rgb kWashWeapon = {0x6B, 0x74, 0xD0};
constexpr Rgb kWashGadget = {0x4F, 0xB5, 0x5C};
constexpr float kWash = 0.30f, kWashSelected = 0.45f;
// The glyphs: light blue, light green, the highlight yellow selected; their shadow.
constexpr Rgb kGlyphWeapon = {0xAE, 0xB6, 0xF4};
constexpr Rgb kGlyphGadget = {0x9B, 0xE4, 0xA4};
constexpr Rgb kHighlight = {0xE2, 0xDF, 0x4E};
constexpr Rgb kShadow = {0x07, 0x07, 0x0D};
// The glints.
constexpr Rgb kGlint = {0xE8, 0xE8, 0xEC};
constexpr float kGlintAlpha = 0.55f;
constexpr Rgb kCentreGlintLight = {0xED, 0xED, 0xF0};
constexpr float kCentreGlintLightAlpha = 0.85f;
constexpr Rgb kCentreGlintDim = {0xC0, 0xC0, 0xC8};
constexpr float kCentreGlintDimAlpha = 0.40f;
// The badge: grey graded down, its edge.
constexpr Rgb kBadgeTop = {0x8E, 0x8E, 0x8E};
constexpr Rgb kBadgeBottom = {0x55, 0x55, 0x55};
constexpr Rgb kBadgeEdge = {0x20, 0x20, 0x20};
constexpr float kBadgeEdgeWidth = 1.5f;
// The pointer's edge.
constexpr Rgb kPointerEdge = {0x3A, 0x38, 0x00};

}  // namespace spiral::look
