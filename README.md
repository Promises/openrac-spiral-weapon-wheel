# Spiral Weapon Wheel

A native mod for [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s port of Ratchet & Clank: a weapon
wheel in the style of Prey (2017)'s favourites wheel. Its spiral and art come from
[Prey Menu](https://www.nexusmods.com/newvegas/mods/94499) by KerosenileArsonist, a Fallout: New
Vegas mod. The pause and the timing come from Going Commando's own quick select, whose decompiled code
it runs.

![The wheel, a mock-up](docs/mockup.png)

*A mock-up made from the mod's art, with placeholder icons. In the game the icons, names and font
are Ratchet & Clank's own.*

- **Hold Triangle.** The game slows down for a moment, then holds (Going Commando's pause, with no
  freeze dialog and no dimming).
- **The spiral.** Everything you can put on your quick select (the weapons and gadgets of the pause
  menu's Quick Select page, in its order) lies on a spiral, twelve to a loop. Turn the stick
  around and the pointer goes round and round through the loops: items you have passed shrink
  inward and fade, and the ones ahead come out. The d-pad steps one item at a time.
- **What it shows.** The item in hand has the thick corners. Items on your quick select have a
  filled corner with their quick select number. The pointed-at item's name and ammo are in the
  middle.
- **Picking.** Let go of Triangle to equip the pointed-at item. **None**, where the spiral starts,
  is the wrench. Circle closes the wheel without picking anything.
- **Double tap Triangle** for the quick swap to your previous weapon, as before.

## Settings

| Setting | Default | What it does |
|---|---|---|
| Pause while open | on | On, the world holds while the wheel is open. Off, the wheel opens over the running game. |
| Slow-down before pausing | Short | As the wheel opens, the game's time eases down to an eighth of its speed, then the pause holds it, as Prey's wheel does. *Short* is about 0.15 s of game time, *Prey-like* about 0.3 s, *Off* pauses at once. |
| Ease back on close | off | On, the game's time eases back up as the wheel closes, the way it slowed down. |
| Open and close | Hold | *Hold*: let go of Triangle to pick. *Toggle*: the wheel stays up when you let go; press Triangle again to pick. |
| Hold before pausing | A little longer | How long Triangle is held before the wheel opens. *Going Commando* is its own 4 frames, *A little longer* 8 frames (so taps and double taps don't open it), *Longer* 12. On PAL: 3, 7, 10. |
| Size | 1 | 1 is two thirds of the screen's height. |
| Start angle | 90 | Where the spiral starts, in degrees counter-clockwise from 3 o'clock (90 is 12 o'clock). |
| Clockwise | off | Which way the spiral turns. |
| Spiral depth | 0.75 | Prey Menu's: how much smaller each slot behind the pointer is. Lower spreads the loops further apart. |
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
  - In place of the eight-slot ring, it uses the spiral (`src/spiral/`, game-agnostic), ported from
    Prey Menu's menu and scripts:
    - the pointer's angle grows by the stick's turn;
    - the spiral wraps through blank slots and "None";
    - each slot's angle, size (`0.9 + 0.0666 * depth` to the power of its distance from the
      pointer) and fade (full within six slots, gone at twelve);
    - the art's sizes and colours.
- **The items** are those of the pause menu's Quick Select page: the owned items of its weapons
  grid (15 cells), then of its hand gadgets grid (6 cells), read from the game's own tables
  (`pause_item_candidate_entries`, `pause_hand_gadget_cells`), shown and assigned when owned as the
  page does (`draw_menu_item_grid`, `update_menu_item_slots`). Nothing of the Items page.
- **The pictures** are drawn under the game's HUD by the mod's overlay (interface 1.40). The icons
  and text are drawn on top, through the game's own HUD calls.
- **The slow-down** uses the port's slow motion (`time_stretch`). The game's time scale goes down
  evenly from 1 to an eighth over the setting's length, then Going Commando's pause begins.

### Where each behaviour comes from

| Behaviour | From |
|---|---|
| The spiral: 12 a loop, the pointer turning with the stick, the wrap, sizes, fades, the art's states, the settings and their defaults | Prey Menu's scripts and menu |
| What the Quick Select page can assign, in its order, like Prey's favourites | the mod's own choice; the rule is the game's |
| The quick select's items marked with their number, as Prey Menu marks hotkeys | the mod's own choice |
| "None" is the wrench | the mod's own choice (Prey Menu's "None" unequips) |
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
  and item counts, plus the stick, the opening and the layout.
- `src/spiral_weapon_wheel_test.cpp` runs the layer, the copied code and the wheel on a fake game:
  - the items offered (none of the Items page), the selection through the loops, the d-pad and the picks (an item, item 0x18,
    "None", a blank, the item in hand, Circle);
  - the toggle mode and the draw;
  - the slow-down into the pause and the ease back;
  - the pause from the hold to the release, the gate's conditions, taps, double taps and holds at
    each "Hold before pausing", 50/60 Hz, and the watchdogs.

## Credits

- **Prey Menu** by **KerosenileArsonist** (<https://www.nexusmods.com/newvegas/mods/94499>): the
  wheel's art and its spiral's design, used with credit as its Nexus permissions ask.
- **Going Commando's code**: [rac2-decomp](https://github.com/OpenRAC/rac2-decomp) (Kryštof
  "Lynder063" Malinda and its contributors; earlier llesieur99).
- **Ratchet & Clank's**: [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s decompilations and the
  OpenRAC mod SDK.
- **stb_image** by Sean Barrett (public domain or MIT).
- Prey's wheel was studied through [Chairloader](https://github.com/thelivingdiamond/Chairloader)'s
  headers. Nothing of Prey's is copied.

## License

- **The code** is under the GNU General Public License version 3 ([LICENSE](LICENSE)). The files
  copied from rac2-decomp, and `src/gc/rac1_spiral.cpp`, are GPL-3.0-only. The mod's own files are
  GPL-3.0-or-later.
- **The art** (`assets/spiral-wheel/`, and `docs/mockup.png`, which is made from it) is **not** under
  the GPL. It is © KerosenileArsonist, from Prey Menu, used under the author's Nexus permissions: you
  may use, modify and convert it with credit, but **not in anything sold**.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) has the details. The package carries it as
  `LICENSE.THIRD_PARTY_NOTICES.md`.

Ratchet & Clank and Ratchet & Clank: Going Commando are trademarks of Sony Interactive
Entertainment, developed by Insomniac Games. Prey is a trademark of ZeniMax Media. This mod is not
affiliated with or endorsed by any of them, and contains none of their code or assets.
