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
// The colours (1.4.0) are Ratchet & Clank's own menu panel frame's: its frame is a lit 3D bevel (moby
// class 0x472) whose texture's grey ramp across it runs 110 110 112 117 127 153 213 195 154 133 114
// 98 83 72 61 48 38 31 29 30 34 39 48 52 55 53 49 44 41 40 41, times the menu's light colour (0.9,
// 0.9, 0.6). The bands take the peak (213), the bright side's mean (the ramp's first 11 without the
// peak: 132.5), the falling part's (the next 7: 61.6) and the dark tail's (the last 13: 42.7).
struct Band {
    float width;
    Rgb colour;
};
constexpr Band kFrame[4] = {{7.5f, {38, 38, 26}}, {5.9f, {55, 55, 37}}, {3.8f, {119, 119, 80}}, {1.5f, {192, 192, 128}}};
// The selected slot's ("Load Game"'s yellow frame).
constexpr Band kFrameSelected[4] = {{9.0f, {0x2C, 0x2B, 0x12}}, {7.0f, {0x8F, 0x8C, 0x3C}}, {4.5f, {0xC8, 0xC4, 0x4E}},
                                    {1.8f, {0xEE, 0xEC, 0x84}}};
// The middle's (the same, wider).
constexpr Band kFrameCentre[4] = {{9.0f, {38, 38, 26}}, {7.0f, {55, 55, 37}}, {4.5f, {119, 119, 80}}, {1.8f, {192, 192, 128}}};

// Ratchet & Clank's menu panel, as its pause menu draws one (games/rac1/decomp
// src/game/ui/pause.c: render_level_effects_and_screen_sprites and draw_menu_flashing_panel), bottom
// to top, after the frame: an opaque fill (panel_clear_color 0x80100808); the vignette (HUD icon
// 0xE99E frame 7, black with alpha, stretched over the panel); now and then a flash of static (FX
// texture 0x1A, grey noise, added, at a random place each frame, one texel a game pixel); the
// scanlines (FX texture 0x1C, a 16 x 16 tile of a clear row and a grey one, 1.5 tile rows a game
// line, times 0x50606060: a lit row grey 105 * 0x60 / 128 at alpha 71 / 128 * 0x50 / 128, over the
// fill #1F1F25 and the clear rows the fill: a period of 4/3 game lines, half lit, the first line at
// the screen's top clear); and the glint (FX texture 0x19: two L-shaped lines, top left and bottom
// right, with fading arms, stretched over the panel a game pixel in, its texels 1 to 63 of 64).
constexpr Rgb kPanelFill = {0x08, 0x08, 0x10};
constexpr float kScanRowsPerLine = 1.5f;
constexpr float kScanColour = 0x60 / 128.0f, kScanAlpha = 0x50 / 128.0f;
constexpr int kScanTile = 16, kGlintTexels = 64, kNoiseTile = 32;
// The game's lines on the screen: NTSC's 2D is 416 lines tall, PAL's 448.
constexpr float kLinesNtsc = 416.0f, kLinesPal = 448.0f;
// The flash: a 1 in 2000 chance each frame for each panel; its timer t up 2 a frame to 0x100, its
// strength min(0x80, 2 * (0x80 - |t - 0x80|)) / 0x80 (32 frames up, 64 held, 32 down); the noise
// at a random offset of 0 to 199 texels each frame.
constexpr int kFlashChance = 2000, kFlashStep = 2, kFlashEnd = 0x100, kFlashOffsets = 200;
// The noise picture the flash samples: its tile repeated to this many texels square (overlay
// pictures clamp at their edges: a panel and the offset fit inside).
constexpr int kNoiseTexels = 512;

constexpr Rgb kScanDark = {0x0D, 0x0C, 0x13};
constexpr Rgb kScanLightColour = {0x1C, 0x1B, 0x22};
// The categories, as Ratchet & Clank's menus group its items, each icon's ground a colour of its
// own (sampled from the game's icons: light, mid and dark across the gradient): weapons blue, gold
// weapons orange-tan, hand gadgets green, packs yellow, head gear peach, foot gear red, other items
// purple. A slot is washed with its category's colour, 30 % (45 % selected), and its glyph drawn
// in its light tint. Weapons and gadgets are the design's own values; the other five are derived
// the same way: the wash the category's mid shade (as the design's gadget green is), the glyph its
// light shade mixed 42 % toward white (as the design lightened blue and green).
enum class Category { Weapon, Gold, Gadget, Pack, Head, Feet, Item };
constexpr int kCategories = 7;
struct CategoryColours {
    const char* name;  // as icons.json writes it
    Rgb wash, glyph;
};
constexpr CategoryColours kCategoryColours[kCategories] = {
    {"weapon", {0x6B, 0x74, 0xD0}, {0xAE, 0xB6, 0xF4}},  // the design's (the game's #6475E1 / #4C5BBB / #2F3E97)
    {"gold", {0xC4, 0x92, 0x57}, {0xEF, 0xCF, 0xAA}},    // #E4AD6C / #C49257 / #9B703C
    {"gadget", {0x4F, 0xB5, 0x5C}, {0x9B, 0xE4, 0xA4}},  // the design's (#63DD6F / #4FBD5B / #359C42)
    {"pack", {0xB0, 0xAC, 0x4E}, {0xE7, 0xE3, 0xA7}},    // #D5CF67 / #B0AC4E / #8F8D5A
    {"head", {0xB4, 0x8A, 0x4E}, {0xEB, 0xCB, 0xA6}},    // #DDA566 / #B48A4E / #9B793E
    {"feet", {0xAC, 0x4C, 0x4E}, {0xED, 0xA4, 0xA6}},    // #E06265 / #AC4C4E / #9D373A
    {"item", {0x72, 0x4D, 0xAB}, {0xB7, 0xA2, 0xDF}},    // #835EC8 / #724DAB / #613B8B
};
inline const CategoryColours& colours_of(Category c) { return kCategoryColours[static_cast<int>(c)]; }
constexpr Rgb kWashWeapon = kCategoryColours[0].wash;
constexpr Rgb kWashGadget = kCategoryColours[2].wash;
constexpr Rgb kGlyphWeapon = kCategoryColours[0].glyph;
constexpr Rgb kGlyphGadget = kCategoryColours[2].glyph;
constexpr float kWash = 0.30f, kWashSelected = 0.45f;
// The selected glyph: the highlight yellow; the glyphs' shadow.
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
