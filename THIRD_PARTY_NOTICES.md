# Third-party notices

Spiral Weapon Wheel's **code** is licensed under the GNU General Public License version 3
([LICENSE](LICENSE)). The mod's own files are GPL-3.0-or-later; the files copied from Going
Commando's decompilation, and `src/gc/rac1_spiral.cpp`, which follows two of its functions, are
under that project's license, the GPL version 3 (it names no later version, so they are
GPL-3.0-only). Each file says which in its `SPDX-License-Identifier` line.

**The art is not under the GPL.** The pictures in `assets/spiral-wheel/` are Prey Menu's, by
KerosenileArsonist, used under its author's Nexus permissions (below), which forbid use in anything
sold. The GPL covers the code only, not these files.

## Prey Menu (Fallout: New Vegas), by KerosenileArsonist

- Source: <https://www.nexusmods.com/newvegas/mods/94499>, "Prey Menu" by KerosenileArsonist.
- Its Nexus "Permissions and credits": the assets are the author's own or from free-to-use modders'
  resources; upload, modification and conversion to other games are allowed, crediting the author
  as the creator; asset use is allowed without asking, with credit; use in mods or files that are
  sold (for money, on the Steam Workshop or other platforms) is not allowed; earning donation
  points is allowed.

**The art** (© KerosenileArsonist, from Prey Menu, <https://www.nexusmods.com/newvegas/mods/94499>,
used under the author's Nexus permissions: free to use, modify and convert with credit; not for use
in anything sold). Converted from the mod's DDS textures (`textures/interface/PreyMenu/`) to PNG at
their own sizes, unchanged otherwise:

| File here | Prey Menu's |
|---|---|
| `assets/spiral-wheel/sector.png` | `sector.dds` (1024 x 1024) |
| `assets/spiral-wheel/border.png` | `border.dds` (1024 x 1024) |
| `assets/spiral-wheel/borderEq.png` | `borderEq.dds` (1024 x 1024) |
| `assets/spiral-wheel/borderHot.png` | `borderHot.dds` (1024 x 1024) |
| `assets/spiral-wheel/borderEqHot.png` | `borderEqHot.dds` (1024 x 1024) |
| `assets/spiral-wheel/pointer.png` | `pointer.dds` (1024 x 1024) |
| `assets/spiral-wheel/info.png` | `info.dds` (512 x 8) |
| `assets/spiral-wheel/hotkey1.png` ... `hotkey8.png` | `hotkeys/1.dds` ... `hotkeys/8.dds` (32 x 32) |
| `docs/mockup.png` | a picture of the wheel made from the files above |

**The spiral's design** (the same credit and permissions: modification and conversion with credit).
`src/spiral/spiral.cpp` and `src/spiral/wheel.cpp` port the spiral's logic from the mod's menu and
scripts, each function naming the trait or script it follows:
`menus/prefabs/PreyMenu/PreyMenu.xml` (the pointer's index, its wrap and angle; each slot's angle,
distance from the pointer, size, fade and highlight; the art's sizes, radii and colours),
`NVSE/plugins/scripts/ln_PreyMenu.txt` (the direction's sign and the depth's shallowness),
`NVSE/user_defined_functions/PreyMenu/UpdatePointer.gek` (the stick turning the pointer),
`UpdateMenu.gek` (the pointer on the equipped item, the opening's zoom) and `config/PreyMenu.ini`
(the settings and their defaults). No script is copied: the code is this mod's own, written from
them. The mod's tests (`src/spiral_test.cpp`) write the XML's formulas out as it evaluates them and
check the port against them.

## stb_image

`src/third_party/stb_image.h`: stb_image v2.34 by Sean Barrett and contributors
(<https://github.com/nothings/stb>, commit `6b5799bae8a1fe3792691ef4b72ae26434c3e799`), unchanged.
It reads the art's PNGs. Public domain, or the MIT License (its own notice, at the file's end, gives
both).

## Prey (2017)

Nothing of Prey's is copied: no code, art or data. Its favourites wheel's behaviour (a time scale
that eases the game's time to a target over a duration, separate time scales for the world and the
player, a weapons menu and a powers menu) was read from the class layouts of Chairloader's headers
(<https://github.com/thelivingdiamond/Chairloader>, `Common/Prey/GameDll/ark/ArkTimeScaler.h`,
`ark/player/ArkFocusModeComponent.h`, `ark/ui/ArkFocusModeUIComponent.h`,
`ark/player/ArkQuickSelectComponent.h`), and from footage of the game. Prey is a trademark of
ZeniMax Media; this mod is not affiliated with or endorsed by it.

## Going Commando's decompilation: OpenRAC/rac2-decomp

- Source: <https://github.com/OpenRAC/rac2-decomp>, commit
  `4ea93ceaf43aec055e19211d36512e847860bac2` (its `master`).
- License: GNU General Public License version 3 (its `LICENSE`: Copyright (c) 2026 Kryštof
  "Lynder063" Malinda). The project was MIT, Copyright (c) 2026 llesieur99, up to its commit
  `d46a3539` (2026-10-11); code from then keeps that notice, reproduced below. The history of
  rac2-decomp's `master` begins at `7fccf735` (2026-10-11, the same day) and has no `d46a3539`:
  six of the files copied (`00285840.c`, `00287E18.c`, `0028BDB0.c`, `00290670.c`, `0027B948.c`,
  `0029BCC8.c`) are from that day's first two commits, the others were added from 2026-10-19 on.
- Target: Ratchet & Clank: Going Commando, USA v1.01.

Copied, unchanged but for the lines marked `(gc-weapon-wheel)` (each file's header lists the exact
lines and says what the marked ones do). They are the pause, the hold before it and the double tap
of the mod this one grew from, Going Commando weapon wheel; its two files of the quick select's own
update and draw (`weapon_wheel_update_select`, `src/game/0028BE00-g8.c`, and `weapon_wheel_draw`,
`src/game/0028BDB0-wheel-g8.c`) are not copied here. `src/gc/rac1_spiral.cpp` takes their place
under their names: it keeps their frame (the HUD-open request and mode 7's timer kept while the
wheel is up, the record's timeout and slide, the open and selection fades, the pick as Triangle is
let go once the open fade has run down, item 0x18's rule, the usage statistics, the ammo held to its
most, mode 7 moved on or the request cleared, the record closed, the name and ammo text in Going
Commando's colours) around the spiral, and is GPL-3.0-only for that reason.

| File here | Function(s) | rac2-decomp file and lines | USA v1.01 |
|---|---|---|---|
| `src/gc/menu_init_panel_layout.c` | `menu_init_panel_layout` | `src/game/0028BDB0.c`: 1-19 (1 line marked: the countdown in ticks) | `func_0028BDB0` |
| `src/gc/weapon_menu_init_slots.c` | `weapon_menu_init_slots` | `src/game/0028BCE8-g8.c`: 1-40 | `func_0028BCE8` |
| `src/gc/UpdateModalState.c` | `UpdateModalState`, its prologue and mode 7 | `src/game/00286738-g8.c`: 1-177, 593-661, 765-767 | `func_00286738` |
| `src/gc/menu_set_mode.c` | `menu_set_mode` | `src/game/00285840-g8.c`: 1-67 | `func_002858C0` |
| `src/gc/menu_end.c` | `menu_get_saved_state`, `menu_get_second_value`, `menu_end` | `src/game/00285840.c`: 1-44 | `func_00285840`, `func_00285850`, `func_00285860` |
| `src/gc/menu_set_state_word_three.c` | `menu_set_state_word_three` | `src/game/00287E60.c`: 1-10 | `func_00287E60` |
| `src/gc/menu_state_seven.c` | `menu_advance_state_in_mode_seven`, `menu_is_state_seven` | `src/game/00287E18.c`: 1-35 | `func_00287E18`, `func_00287E48` |
| `src/gc/hud_letterbox.c` | `hud_request_letterbox_bars`, `hud_clear_flag_a`, `hud_clear_flag_b` | `src/game/00290670.c`: 1-13, 41-54 (`hud_update_letterbox_bars`, 14-40, cut on purpose: Going Commando's bars are left out) | `func_00290670`, `func_00290720`, `func_00290728` |
| `src/gc/hud_open_request.c` | `hud_request_open`, `hud_is_open_done` | `src/game/00290730.c`: 1-19 | `func_00290730`, `func_00290748` |
| `src/gc/level_update_countdown_state_machine.c` | `level_update_countdown_state_machine` | `src/levels/shared/L00_002FC058-g8.c`: 1-41 | `func_L00_002FC058` |
| `src/gc/level_update_game_frame_input.c` | `level_update_game_frame_input`, its head and the paused open | `src/levels/shared/L00_0036F488-g8.c`: 1-81, 83-86, 93, 106-107, 112-116, 118-120 | `func_L00_0036F488` |
| `src/gc/hero_update_button_hold_timers.c` | `hero_update_button_hold_timers` | `src/levels/shared/L00_002CB218.c`: 1-40 (2 lines marked: 20 frames in ticks) | `func_L00_002CB218` |
| `src/gc/PadPressedWithin.c` | `PadPressedWithin` | `src/levels/shared/L00_0032EF10.c`: 1-32 | `func_L00_0032EF10` |
| `src/gc/hero_update_hit_streak_effect.c` | `hero_update_hit_streak_effect`, its press window | `src/levels/shared/L00_003266D8-g8.c`: 1-27, 42 | `func_L00_003266D8` |
| `src/gc/gfx_render_frame_extra_layers.c` | `gfx_render_frame_extra_layers` | `src/game/0027B948.c`: 1-3, 124-142 (1 line marked: the loading guard) | `func_0027C4A8` |
| `src/gc/func_0029CE38.c` | `func_0029CE38` | `src/game/0029BCC8.c`: 1-2, 14, 520-532 | `func_0029CE38` |

No data of Going Commando's is copied: its pattern, neighbour and default tables, strings and
textures are its disc's. The quick select's slots, neighbours, defaults, icons, names, ammo format
and sprites are Ratchet & Clank's own, read from the game's memory as it runs. The constants written
in the copied C (frame counts, angles, colours) are code. Two values of Going Commando's data are
used: the start of mode 7's counter (`ref_00286738_00`, 6 frames, from rac2-decomp's note on
`func_00286738`), as the translation layer's `gc_shim_modal_counter_start`, and the HUD-open
request's countdown (`ref_L00_002FC058_01`, 4 frames, the word at 0x1A9374 of USA v2.00's level 0
program, the same in every level), as `gc_shim_open_delay_start`. Which words the request's
functions share (gp-0x5C80, gp-0x5C84 in USA v2.00's level programs), that
`menu_set_state_word_three`'s word is mode 7's phase-1 timer (gp-0x6240), and that a level's copy
of `UpdateModalState` calls `hero_update_button_hold_timers` where the C calls `stub_empty_20`, were
read from their instructions, not copied.

The translation layer (`src/gc/rac1_shim.cpp`, `src/gc/rac1_spiral.cpp`) reimplements, for its polyfills, what these
rac2-decomp functions do: `request_begin_slot` and `request_end_slot` (mode 4 entered and left),
`pad_clear_port_state`, `FastDecTimer`; each polyfill's comment cites its source.

## Ratchet & Clank's decompilation: OpenRAC games/rac1/decomp

- Source: <https://github.com/OpenRAC/OpenRAC>, `games/rac1/decomp` (GNU GPL version 3), and the
  NTSC decomp (`games/rac1/ntsc-decomp`, GPL-3.0) for the NTSC-U addresses.
- The translation layer reimplements, after it: the quick swap of `hero_update_transitions`
  (`src/overlays/shared/hero_item_input_and_hud.c`:152-168) and its Triangle conditions (110-116),
  and `scale_ticks`. It follows the field layouts of `include/structs/hero.h` and `pad.h` in
  `src/layout.hpp` and `src/gc/rac1_shim.hpp`, and reads the items owned (`item_available`) and the
  pause menu's Quick Select page's grids (`pause_item_candidate_entries`, `pause_hand_gadget_cells`:
  their cells, `include/structs/menu.h` MenuGridCell; shown and assigned when owned, as
  `draw_menu_item_grid` and `update_menu_item_slots` do, `src/game/ui/pause.c`:2964 and 2304-2341;
  the NTSC-U addresses from `config/names_ntsc.tsv`). No code is
  copied from it.

## The OpenRAC mod SDK

The headers under `.sdk/` (fetched, not in this repository) are MIT, Copyright (c) 2026 the
OpenRAC contributors; the mod includes them. `tools/fetch_sdk.py` is the SDK's own (MIT).

## rac2-decomp's earlier MIT notice

```
MIT License

Copyright (c) 2026 llesieur99

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Ratchet & Clank and Ratchet & Clank: Going Commando are trademarks of Sony Interactive
Entertainment, developed by Insomniac Games. This mod is not affiliated with or endorsed by
either, and contains none of their code or assets: the decompiled code it copies is the
decompilation projects' reconstruction, and the game runs from the player's own disc. The items'
icons, names and font the wheel shows are read from the player's game as it runs.
