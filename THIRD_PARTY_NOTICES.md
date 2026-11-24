# Third-party notices

Going Commando weapon wheel is licensed under the GNU General Public License version 3
([LICENSE](LICENSE)). The mod's own files are GPL-3.0-or-later; the files copied from Going
Commando's decompilation are under that project's license, the GPL version 3 (it names no later
version, so they are GPL-3.0-only). Each file says which in its `SPDX-License-Identifier` line.

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
lines and says what the marked ones do):

| File here | Function(s) | rac2-decomp file and lines | USA v1.01 |
|---|---|---|---|
| `src/gc/weapon_wheel_update_select.c` | `weapon_wheel_update_select` | `src/game/0028BE00-g8.c`: 1-186 (2 lines marked: the timeout in ticks, item 0x18's rule) | `func_0028BE00` |
| `src/gc/weapon_wheel_draw.c` | `weapon_wheel_draw` | `src/game/0028BDB0-wheel-g8.c`: 1-43, 56-102 (the ring, 44-55, replaced by Ratchet & Clank's) | `func_0028C6D0` |
| `src/gc/menu_init_panel_layout.c` | `menu_init_panel_layout` | `src/game/0028BDB0.c`: 1-19 (1 line marked: the countdown in ticks) | `func_0028BDB0` |
| `src/gc/weapon_menu_init_slots.c` | `weapon_menu_init_slots` | `src/game/0028BCE8-g8.c`: 1-40 | `func_0028BCE8` |
| `src/gc/UpdateModalState.c` | `UpdateModalState`, its prologue and mode 7 | `src/game/00286738-g8.c`: 1-177, 593-661, 765-767 | `func_00286738` |
| `src/gc/menu_set_mode.c` | `menu_set_mode` | `src/game/00285840-g8.c`: 1-67 | `func_002858C0` |
| `src/gc/menu_end.c` | `menu_get_saved_state`, `menu_get_second_value`, `menu_end` | `src/game/00285840.c`: 1-44 | `func_00285840`, `func_00285850`, `func_00285860` |
| `src/gc/menu_state_seven.c` | `menu_advance_state_in_mode_seven`, `menu_is_state_seven` | `src/game/00287E18.c`: 1-35 | `func_00287E18`, `func_00287E48` |
| `src/gc/hud_letterbox.c` | `hud_request_letterbox_bars`, `hud_update_letterbox_bars`, `hud_clear_flag_a`, `hud_clear_flag_b` | `src/game/00290670.c`: 1-54 | `func_00290670`, `func_00290680`, `func_00290720`, `func_00290728` |
| `src/gc/level_update_game_frame_input.c` | `level_update_game_frame_input`, its head and the paused open | `src/levels/shared/L00_0036F488-g8.c`: 1-81, 83-86, 93, 107, 112-116, 118-120 | `func_L00_0036F488` |
| `src/gc/hero_update_button_hold_timers.c` | `hero_update_button_hold_timers` | `src/levels/shared/L00_002CB218.c`: 1-40 (2 lines marked: 20 frames in ticks) | `func_L00_002CB218` |
| `src/gc/PadPressedWithin.c` | `PadPressedWithin` | `src/levels/shared/L00_0032EF10.c`: 1-32 | `func_L00_0032EF10` |
| `src/gc/hero_update_hit_streak_effect.c` | `hero_update_hit_streak_effect`, its press window | `src/levels/shared/L00_003266D8-g8.c`: 1-27, 42 | `func_L00_003266D8` |
| `src/gc/gfx_render_frame_extra_layers.c` | `gfx_render_frame_extra_layers` | `src/game/0027B948.c`: 1-3, 124-142 (1 line marked: the loading guard) | `func_0027C4A8` |
| `src/gc/func_0029CE38.c` | `func_0029CE38` | `src/game/0029BCC8.c`: 1-2, 14, 520-532 | `func_0029CE38` |

No data of Going Commando's is copied: its pattern, neighbour and default tables, strings and
textures are its disc's. The quick select's slots, neighbours, defaults, icons, names, ammo format
and sprites are Ratchet & Clank's own, read from the game's memory as it runs. The constants written
in the copied C (frame counts, angles, colours) are code. One value of Going Commando's data is
used: the start of mode 7's counter (`ref_00286738_00`, 6 frames, from rac2-decomp's note on
`func_00286738`), as the translation layer's `gc_shim_modal_counter_start`.

The translation layer (`src/gc/rac1_shim.cpp`) reimplements, for its polyfills, what these
rac2-decomp functions do: `request_begin_slot` and `request_end_slot` (mode 4 entered and left),
`pad_clear_port_state`, `FastDecTimer`; each polyfill's comment cites its source.

## Ratchet & Clank's decompilation: OpenRAC games/rac1/decomp

- Source: <https://github.com/OpenRAC/OpenRAC>, `games/rac1/decomp` (GNU GPL version 3), and the
  NTSC decomp (`games/rac1/ntsc-decomp`, GPL-3.0) for the NTSC-U addresses.
- The translation layer reimplements, after it: `draw_hud_item_selector_panel`'s ring
  (`src/overlays/shared/weapon_hud_and_counters.c`:1303-1360: the panel quarters, the icons on the
  ring, the marker), the quick swap of `hero_update_transitions`
  (`src/overlays/shared/hero_item_input_and_hud.c`:152-168) and its Triangle conditions (110-116),
  and `scale_ticks`. It follows the field layouts of `include/structs/hero.h` and `pad.h` in
  `src/layout.hpp` and `src/gc/rac1_shim.hpp`. No code is copied from it.

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
decompilation projects' reconstruction, and the game runs from the player's own disc.
