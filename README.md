# Spiral Weapon Wheel

A native mod for [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s port of Ratchet & Clank: a weapon
wheel in the style of Prey (2017)'s favourites wheel. Its spiral comes from
[Prey Menu](https://www.nexusmods.com/newvegas/mods/94499) by KerosenileArsonist, a Fallout: New
Vegas mod. Its look is Ratchet & Clank's own menu panels, bent around the spiral. The pause and the
timing come from Going Commando's own quick select, whose decompiled code it runs.

The mod ships no art, and nothing of the game's: its panels, badges and pointer are drawn as it
loads, and each item's glyph is made from your own game while it runs.

![The wheel, a mock-up](docs/mockup.png)

*A mock-up drawn by the mod's own painter (`src/spiral/paint.cpp`) with placeholder glyphs and a
stand-in font. In the game each item's glyph is made from your own game's icon (below), and the
names, numbers and font are Ratchet & Clank's own.*

- **Hold Triangle.** The game slows down for a moment, then holds (Going Commando's pause, with no
  freeze dialog and no dimming).
- **The spiral.** Everything you can put on your quick select (the weapons and gadgets of the pause
  menu's Quick Select page) lies on a spiral, twelve to a loop. Your own quick select is the home
  ring: its eight items sit on the first loop in the directions they have on the game's ring, so
  the stick points where you know them (the first push of each opening lands on that loop); the
  others fill the free slots, then the next loops. The spiral winds inward, each slot a little
  further in than the one before. Turn the stick around and the pointer goes round and round through
  the loops: the pointer's loop is shown whole, the end of the loop before it and the start of the
  one after it fade out either side, and the spiral turns to the next loop as the pointer reaches
  it. The d-pad steps one item at a time.
- **What it shows.** Every slot is a menu panel bent into its sector: scanlines, a khaki frame and
  a white glint, washed blue for a weapon and green for a gadget. Each item's glyph is cut out of
  your game's own icon and redrawn sharp, light blue or light green with a dark drop shadow. The
  pointed-at slot has the yellow "Load Game" frame and a yellow glyph, and is pushed out. Items on
  your quick select carry their number on a small grey badge. The pointed-at item's name and ammo
  are in the round middle panel, in the pause menu's blue. The health meter is hidden while the
  wheel is open (the bolt and ammo counters stay), and comes back as it was.
- **Picking.** Let go of Triangle to equip the pointed-at item. Let go before you have moved the
  pointer and nothing changes (so a double tap's swap stands). Circle closes the wheel without
  picking anything. There is no "None" slot.
- **Double tap Triangle** for the quick swap to your previous weapon, as before.

## Settings

| Setting | Default | What it does |
|---|---|---|
| Pause while open | on | On, the world holds while the wheel is open. Off, the wheel opens over the running game. |
| Slow-down before pausing | Short | As the wheel opens, the game's time eases down to an eighth of its speed, then the pause holds it, as Prey's wheel does. *Short* is about 0.15 s of game time, *Prey-like* about 0.3 s, *Off* pauses at once. |
| Ease back on close | off | On, the game's time eases back up as the wheel closes, the way it slowed down. |
| Open and close | Hold | *Hold*: let go of Triangle to pick. *Toggle*: the wheel stays up when you let go; press Triangle again to pick. |
| Hold before pausing | A little longer | How long Triangle is held before the wheel opens. *Going Commando* is its own 4 frames, *A little longer* 8 frames (so taps and double taps don't open it), *Longer* 12. On PAL: 3, 7, 10. |
| Size | 1 | 1 is the design's size: the first loop is about nine tenths of the screen's height across. |
| Start angle | 90 | Where the spiral starts, in degrees counter-clockwise from 3 o'clock (90 is 12 o'clock). |
| Clockwise | on | Which way the spiral turns (on is the design's; Prey Menu's default was off). |
| Spiral depth | 0.75 | How far in each slot is from the one before: the design's 11 pixels (of 720) at 0.75 and below, less above it, down to 5 at 1. |
| Click sound | off | A sound as the pointer moves to another item (Ratchet & Clank's quick select is silent; this is a stand-in). |

## Requirements

**OpenRAC with mod interface 1.40 and three hook points**, none of them in a release yet:

- the overlay pictures and the layer under the HUD:
  [OpenRAC#865](https://github.com/OpenRAC/OpenRAC/pull/865);
- the hook points `rac1.level.update_play`, `rac1.freeze.update` and `rac1.freeze.draw`:
  [OpenRAC#863](https://github.com/OpenRAC/OpenRAC/pull/863);
- and, so the Field of View stays as it is while the world holds,
  [OpenRAC#862](https://github.com/OpenRAC/OpenRAC/pull/862).

A game without them refuses the mod and says why. Until they are merged, `sdk.lock` pins the SDK of
the branch `spiral-wheel-test` of [Promises/OpenRAC](https://github.com/Promises/OpenRAC), which
merges the three.

Ratchet & Clank PAL and NTSC-U.

## Compatibility

- **Going Commando weapon wheel**, **Weapon Wheel Pause** (`weapon-wheel-pause`, `pause-weapon-wheel`):
  these conflict, because all of them replace the quick select. The manifest says so.
- **Gadget Quick Use** has not been tested together with this mod. It reads Triangle at pads,
  faucets and the Swingshot.
- **Enhanced Movement**: while the world holds, the camera does not move.

## How it works

- **The pause** is Going Commando weapon wheel's, unchanged:
  - Going Commando's quick select gate and mode 7, copied from
    [rac2-decomp](https://github.com/OpenRAC/rac2-decomp) into `src/gc/`;
  - run on Ratchet & Clank by a translation layer (`src/gc/rac1_shim.*`) in its freeze mode, through
    the three hook points;
  - with its hold before the pause, its double tap (the double tap's timers also run in the pause),
    its 50/60 Hz scaling and its watchdogs.
  - [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every file and line copied.
- **The wheel** replaces the quick select's update and draw (`src/gc/rac1_spiral.cpp`):
  - It keeps their frame: the fades, the pick as Triangle is let go, the usage statistics, item
    0x18's rule.
  - In place of the eight-slot ring, it uses the spiral (`src/spiral/`, game-agnostic). Its pointer
    is ported from Prey Menu's menu and scripts:
    - the pointer's angle grows by the stick's turn;
    - the spiral wraps through blank slots (Prey Menu's "None" slot, before the first item, is a
      blank here);
    - each slot's angle.
  - Its look is the design's (`src/spiral/look.hpp`): a continuous spiral, the pointer's loop's
    first slot 284 pixels out (of 720), each slot after it 11 further in; a panel 0.33 of its
    radius thick across 25.2 of its 30 degrees, its glyph 0.64 of the thickness (0.7 selected); the
    4 slots before the loop and the 3 after it at 55, 42, 30, 22 % and 50, 32, 18 %; an empty slot
    of the loop at 60 %. When the pointer moves into another loop, the spiral eases there (0.06 s),
    turning the way the pointer went.
  - The painter (`src/spiral/paint.cpp`) draws it all as quads of four pictures it makes as the
    mod loads (the scanlines, white, the badge, the pointer) and the glyphs: the panels in strips
    of a few degrees, the frame's four strokes centred on each edge, widest first.
- **The order**: the game's quick select first, each item at the first loop's slot nearest its
  direction on the game's ring (`draw_hud_item_selector_panel`: slot 0 at the top, the others
  clockwise, 45 degrees apart). The first loop is the 12 slots from the start angle on (item 0 on
  it). The cardinal directions fit the 30-degree slots exactly; a diagonal goes to the slot nearer
  the vertical (45 to 60, 135 to 120, -45 to -60, -135 to -120), so the two beside one side never
  meet. The others follow: the first loop's free slots in order, then outward. The first push of
  an opening points at the first loop's slot in the stick's direction; after it the stick turns
  the pointer as Prey Menu's does.
- **The glyphs** are made as the game runs, from the player's own game: each item's HUD icon read
  from game memory (`hud_state`: the frame's 8-bit indices and its CSM1 palette, the frame the
  Quick Select page draws), its ground keyed out by brightness with a soft edge, cropped, and
  redrawn four times as large from a signed distance field (smooth edges), kept as a white shape
  that the look colours. Nothing of the game's is in the mod or this repository. An icon that
  cannot be read is drawn as the game's own sprite, at the glyph's place and size.
- **When the glyphs are captured.** The game keeps the HUD's icons (its bank 0) in a buffer only
  for a while: the level load decompresses them into one, the pause menu into another, and in
  play the HUD draws them from the GS while the level reuses both buffers. The icon pages then
  still point at memory that is no longer theirs (which is where 1.1.1's noise came from, as it
  remade glyphs when the pixels changed). So a glyph is captured while the pause menu's item grid
  draws (the hook point `rac1.menu.item_grid.draw`, the pages certain), or, for an item that has
  none yet, at the wheel's opening from the pages as they are. Every decode is checked first: a
  near-black glyph well below a coloured ground, the edge ring mostly ground and smooth, the
  picture mostly smooth (noise is not), the glyph covering 5 % to 70 % of it. A glyph that passes
  is kept for the rest of the level (one from an opening is replaced once by the next pause menu's);
  one that fails is not used, the item keeps the glyph it had or the game's own sprite, and it is
  tried again at the next opening or pause menu. Only a level change drops the glyphs. The log
  has, once a level, each icon captured and each rejected with the reason.
- **The items** are those of the pause menu's Quick Select page: the owned items of its weapons
  grid (15 cells), then of its hand gadgets grid (6 cells), read from the game's own tables
  (`pause_item_candidate_entries`, `pause_hand_gadget_cells`), shown and assigned when owned as the
  page does (`draw_menu_item_grid`, `update_menu_item_slots`). Nothing of the Items page.
- **The pictures** (the panels and the glyphs) are drawn under the game's HUD by the mod's overlay
  (interface 1.40). The text, and any icon drawn as the game's sprite, are drawn on top, through
  the game's own HUD calls: the quick select numbers in its small font, the name in its normal
  one and the ammo in its large one, the name and ammo over a dark copy a unit down and right (as
  the game shadows its own text).
- **The health meter** (its HUD record: slot 1, id 0x7535) is closed on every tick the wheel shows,
  from its first: Ratchet & Clank's own Triangle shows it on each press (`hero_update_transitions`).
  When the wheel closes, whichever way, it is shown again as that press shows it, for
  `scale_ticks(0xB4)` (3 seconds). Going Commando's pause also shows it; the mod leaves that out.
- **The slow-down** uses the port's slow motion (`time_stretch`). The game's time scale goes down
  evenly from 1 to an eighth over the setting's length, then Going Commando's pause begins.

### Where each behaviour comes from

| Behaviour | From |
|---|---|
| The spiral: 12 a loop, the pointer turning with the stick, the wrap, the settings | Prey Menu's scripts and menu |
| The look: menu panels on a continuous spiral, the washes, frames, glints, badges, the pointer, the middle panel, the fades either side | the user's design, after Ratchet & Clank's menus |
| What the Quick Select page can assign, like Prey's favourites | the mod's own choice; the rule is the game's |
| The quick select as the home ring, the diagonals' rounding, the first push on that loop | the mod's own choice |
| Glyphs from the game's icons | the user's choice; the method is the mod's own |
| Glyphs captured while the pause menu draws, checked before use, kept for the level | the mod's own, from the HUD banks' lifetimes in the decompilation |
| The health meter hidden while open | Prey hides its HUD; the mod's own way |
| Nothing picked until the pointer moved | Going Commando (its quick select opens on nothing) |
| The quick select's items marked with their number, as Prey Menu marks hotkeys | the mod's own choice |
| The slow-down, then the pause | Prey's wheel as seen in its footage; the ramp is the mod's own (Prey's time scaler eases to a target over a duration) |
| The pause, the hold before it, the double tap, the fades, the pick on release | Going Commando, from its decompilation |
| The stick's rest (0.5) and the wait for it before the stick picks | Going Commando |
| The d-pad stepping one slot, Circle cancelling, the toggle mode's second press | the mod's own choice |
| No background blur, no categories | not done (no blur in the port's API; Ratchet & Clank has one kind of item to pick) |

## The log

The game's log (`openrac-rac1-*.log`, lines `[mod spiral-weapon-wheel]`) shows:

- at load and in each level, whether the hook points, the quick select's functions and the
  pictures were found;
- one line per Triangle press, giving why the wheel did not open or the trace of the hold;
- at each opening, the items on the spiral in order; the stick's first turns (as read, its
  direction, the slot); and the pick ("picked item 0x.. (name), equipped 0x..") or "cancelled";
- one line per double-tap swap;
- a warning if the pause ever had to be ended by its watchdog.

## Build

```sh
python3 tools/fetch_sdk.py          # the SDK sdk.lock pins, into .sdk/
cmake -S . -B build -G Ninja
cmake --build build && ctest --test-dir build
python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/spiral_weapon_wheel
```

The tests:

- `src/spiral_test.cpp` checks the spiral against Prey Menu's own formulas, written out as its XML
  evaluates them, over millions of pointer angles. It covers both directions, start angles, depths
  and item counts, plus the stick and the opening. It checks the look against the design's values:
  each slot's radius, opacity, push, glyph and badge places, the fades either side, the spiral's
  turn to another loop (both ways), the depth's step; and the painter's quads (the washes, the
  yellow frame, the glyphs' colours, sizes and shadows, the badges, the pointer) and its pictures.
- `src/spiral_weapon_wheel_test.cpp` runs the layer, the copied code and the wheel on a fake game:
  - the items offered (none of the Items page), the selection through the loops, the d-pad and the picks (an item, item 0x18,
    a blank, the item in hand, Circle, a release before moving);
  - the glyphs (synthetic icons only: no game pixels in the tests) and their fallback; their
    capture with the pages made stale after it (kept) and before it (rejected with the reason, the
    game's sprite), the pause menu's capture and a level change; the health meter hidden and
    restored on every way out;
  - the toggle mode and the draw (the slots' glyphs, the badges' numbers in the small font, the
    name and ammo in the normal and large fonts over their shadow, in the menu's blue);
  - the slow-down into the pause and the ease back;
  - the pause from the hold to the release, the gate's conditions, taps, double taps and holds at
    each "Hold before pausing", 50/60 Hz, and the watchdogs.

## Credits

- **Prey Menu** by **KerosenileArsonist** (<https://www.nexusmods.com/newvegas/mods/94499>): the
  spiral's design, used with credit as its Nexus permissions ask. (Up to 1.1.2 the wheel also used
  its art; since 1.2.0 the mod ships none.)
- **Going Commando's code**: [rac2-decomp](https://github.com/OpenRAC/rac2-decomp) (Kryštof
  "Lynder063" Malinda and its contributors; earlier llesieur99).
- **Ratchet & Clank's**: [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s decompilations and the
  OpenRAC mod SDK.
- **stb_image** by Sean Barrett (public domain or MIT), kept in the tree (`src/third_party/`); 1.2.0
  does not use it.
- Prey's wheel was studied through [Chairloader](https://github.com/thelivingdiamond/Chairloader)'s
  headers. Nothing of Prey's is copied.

## License

- **The code** is under the GNU General Public License version 3 ([LICENSE](LICENSE)). The files
  copied from rac2-decomp, and `src/gc/rac1_spiral.cpp`, are GPL-3.0-only. The mod's own files are
  GPL-3.0-or-later.
- There is no art in the mod. `docs/mockup.png` is drawn by the mod's own code with placeholder
  glyphs.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) has the details. The package carries it as
  `LICENSE.THIRD_PARTY_NOTICES.md`.

Ratchet & Clank and Ratchet & Clank: Going Commando are trademarks of Sony Interactive
Entertainment, developed by Insomniac Games. Prey is a trademark of ZeniMax Media. This mod is not
affiliated with or endorsed by any of them, and contains none of their code or assets.
