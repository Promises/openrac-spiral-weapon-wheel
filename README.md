# Going Commando weapon wheel

A native mod for [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s port of Ratchet & Clank: Going
Commando's **weapon quick select**, running **Going Commando's own code**. Hold Triangle and, after a
few frames (Going Commando's HUD-open request), the world holds, as in Going Commando: no freeze
dialog, no dim; the stick (or the d-pad) picks a weapon on the ring with Going Commando's
sectors, hysteresis and snapping, and Going Commando's ammo and name text
under the ring; let go of Triangle and the weapon is equipped. A tap does not pause: a double tap
is the quick swap, as in Going Commando. Going Commando's black bars at the top and bottom are left
out on purpose (Ratchet & Clank's letterbox changes its viewport: the world would zoom). The ring, its icons, the slots and
their neighbours, the names and the ammo format are Ratchet & Clank's own, read from the game.

Settings: **Pause while open** (on: Going Commando's option byte; off: the quick select opens over
the running game from Ratchet & Clank's own Triangle, Going Commando's code running it, as Going
Commando does with the option off); **Click sound** (off: Ratchet & Clank's quick select is silent;
on, a stand-in sound where Going Commando clicks, see below).

**Needs OpenRAC with mod interface 1.39 and three hook points that are not in a release or in
OpenRAC's develop yet**: `rac1.level.update_play`, `rac1.freeze.update` and `rac1.freeze.draw`, on
the branch `rac1-freeze-mode-hooks` of [Promises/OpenRAC](https://github.com/Promises/OpenRAC)
(commit `dae54f21`, which `sdk.lock` pins). A game without them refuses the mod and says why.

## How it works

The quick select is Going Commando's, copied from its decompilation
([rac2-decomp](https://github.com/OpenRAC/rac2-decomp), GPL-3.0, at `4ea93cea`) into `src/gc/`,
unchanged but for the lines marked `(gc-weapon-wheel)`
([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists the files and lines):

- `weapon_wheel_update_select`: the per-frame logic: the arming, the stick's sector (over 0.9
  tilt, 0.589 rad of hysteresis, the even slots' 0.2/0.8 snap, the nudge to a filled neighbour), the
  d-pad's neighbours, the click, the commit as Triangle is let go (the open fade down to 0), the
  usage count, the ammo clamp, the close;
- `weapon_wheel_draw`: the selected weapon's ammo (`x + 105, y + 112`, red at 0) and its name
  (split at `-` or a space), Going Commando's fades; its ring is Ratchet & Clank's (below);
- `menu_init_panel_layout` and `weapon_menu_init_slots`: the record's 210 x 200, the eight slots;
- the pause: `level_update_game_frame_input` (the gate: Triangle pressed within the press window
  asks for the HUD-open request, `hud_request_open`; once `level_update_countdown_state_machine` has
  counted its 4 frames with Triangle still held, `hud_is_open_done`, then
  `request_begin_slot(4, 1, 7, 0, 0)`; Triangle let go first, or a double tap's swap, cancels it), `UpdateModalState` (its prologue and mode 7's four phases:
  the camera flag, the wheel's HUD record kept, the HUD records updated, the health meter and bolt
  counter shown, the counters, the end), `menu_set_mode`, `menu_end`,
  `menu_advance_state_in_mode_seven`, `menu_is_state_seven`, `gfx_render_frame_extra_layers` (the
  picture: the world and its HUD drawn again, no dialog), `func_0029CE38`,
  `hud_clear_flag_a`/`_b` (the request's words), `PadPressedWithin`, the press window
  (`hero_update_hit_streak_effect`'s first block) and Triangle's double tap
  (`hero_update_button_hold_timers`).

A translation layer (`src/gc/rac1_shim.hpp`, `src/gc/rac1_shim.cpp`, `src/gc/shim/common.h`) runs it
on Ratchet & Clank, whose quick select is Going Commando's ancestor, record for record:

- **The HUD record** is the same 0x90 bytes (the update and draw callbacks take it; +0x64 handle,
  +0x6C slide, +0x70 fades, +0x74 selection, +0x78 state, +0x7C timeout): the copied functions get
  it straight from game memory. So do the pad (Ratchet & Clank's `PadState` has every field the code
  reads at Going Commando's offsets, the 30-poll history included: `hist_idx` 0x18E, `hist_count`
  0x190, `hist_btn` 0x1E0), the page of seven-word slot entries, the page index and slot count, the
  defaults, the ammo, the close bookkeeping.
- **Copies** where Ratchet & Clank keeps a thing at other offsets: the hero (Going Commando's
  +0x1248 item is `items[0].item_id`, +0x22C8 the pending item `pending_item[0]`, +0x22B4 the mode
  `vehicle_mode`, +0x24F6/+0x24F8/+0x24FA the close and double-tap timers Ratchet & Clank's
  +0x22D2/+0x22D4/+0x22D6), the usage entry, and Going Commando's weapon table, one record per item
  filled from `item_defs` and `item_price_table` (the gold name for a gold weapon).
- **The pause**: Going Commando's game mode 4 with modal mode 7 runs in Ratchet & Clank's game mode
  4, the freeze mode, its ancestor, with a freeze kind of the mod's own (9: `update_mode_freeze`
  has cases 0 to 8 only). `request_begin_slot` is `mode_freeze_init(9)`, then the copied
  `menu_set_mode(7)`, then `music_Unpause` (Going Commando does not pause the music for mode 7;
  `mode_freeze_init` does). The gate runs before the level's tick (`rac1.level.update_play`) and,
  when it opens the quick select, the tick is skipped, as Going Commando returns before its world
  update. The freeze's tick (`rac1.freeze.update`) runs `UpdateModalState` and then the game's own
  (the sound once, and the sounds and music back on once the mode is left); its picture
  (`rac1.freeze.draw`) is `gfx_render_frame_extra_layers`. `request_end_slot` puts the mode back
  (`mode_freeze_info + 0x14`) and runs the copied `menu_end` (the sound group and the music back on,
  `snd_FlushSoundCommands`, the pad's history head and count cleared).
- **Going Commando's functions** are polyfills: Ratchet & Clank's own by name (`update_hud_elements`
  for `hud_update_record_timers(1)`, `Help_Update` for `level_run_callbacks`, `remove_hud_item`,
  `queue_animation_update` with the selector's own init/update/draw for `hud_set_slot_contents(0x23)`,
  `show_health_hud`/`hud_keep_visible` and the bolt counter for `level_forward_value_b(360)`/
  `level_forward_value(180)`, `FontPrintCenter`, `msg_string`, `FastTweenColor`,
  `hud_screen_position`, `append_gif_transfer_packet` and `DrawDebugProfiler` for the render, the
  sound group calls and the math), or a stand-in; each says what differs.
- **Time**: Going Commando counts 60 frames a second, Ratchet & Clank's PAL tick is a 50th: the
  copied frame counts that are times (the record's timeout and arming, the double-tap's 20, the
  press window) are marked and scaled as Going Commando's PAL build scales its press window,
  `(n * 5 + 2) / 6`.
- **The ring**: Going Commando's ring is its resident HUD's labels, which Ratchet & Clank does not
  have. In its place (marked) the layer draws Ratchet & Clank's own, as `draw_hud_item_selector_panel`
  does (the four mirrored panel quarters, the icons on the ring, the unselected ones fainter, the
  marker, pulsing as Ratchet & Clank's), at Going Commando's alpha, on the centre Going Commando's
  text is drawn at.
- **Where**: Going Commando's quick select's place is its HUD's data (the element table
  `hud_set_slot_contents` loads from the disc), which its code does not give, so Ratchet & Clank's is
  kept: the quick select's record at Ratchet & Clank's place (+0x50, +0x54), anchored by Ratchet &
  Clank's `hud_screen_position` with Going Commando's 210 x 200 panel.
- **No letterbox**: Going Commando's bars (`hud_update_letterbox_bars`) are cut, on purpose: Ratchet
  & Clank's letterbox changes the viewport, which zoomed the frozen world. Nothing of the mod writes
  Ratchet & Clank's letterbox, camera or projection.
- **The HUD-open request** is Going Commando's two words (its state; the double tap's swap flag),
  the same in every function that uses them (USA v2.00's level programs: gp-0x5C80 and gp-0x5C84):
  `hud_request_open`, `hud_is_open_done`, the state machine, `hud_clear_flag_a`/`_b`,
  `level_set_flag_d_true`. Ratchet & Clank's own double tap (in the hero's update, the same timers)
  is seen by its effect (the window open, Triangle let go, both timers cleared) and sets the swap flag,
  as Going Commando's decider does. The countdown's start, 4 frames, is Going Commando's data (every
  USA v2.00 level program), 3 ticks on PAL.

## With other mods

- **Pause Weapon Wheel** (`pause-weapon-wheel`): takes the same quick select hook points and pauses
  its own way. The manifest declares the conflict.
- **Enhanced Movement** (`rac1-enhanced-movement`): in the freeze mode neither `handle_pause_input`
  nor `Camera_UpdateFrame` runs, so its right-stick camera does not move while the paused quick select
  is open; with the pause off it does, as in Going Commando.
- **Gadget Quick Use**: it reads Triangle in `on_pad` at pads, faucets and the Swingshot. The paused
  open reads `PadPressedWithin` over the pad's history, which the game builds from the report after
  `on_pad`: a press it takes out of the report never reaches the history and does not open the
  quick select; a press it leaves in does both (check that mod's choice where they meet).
- **Going Commando Magneboots** (`rac1.player.update_equipped_items`): the commit only writes
  `pending_item[0]`; `hero_switch_item` applies it in the hero's own update, after the pause. No
  clash expected.
- **Rumble**: the freeze mode is not play (`OPENRAC_PLAY_PLAYING`), so it goes quiet while the
  paused quick select is open; Going Commando's actuator also stops updating outside mode 0.

## Assumptions and what is not verified

- **Going Commando's `g_modal_timer` has no writer in its C** (rac2-decomp at `4ea93cea`). Read
  literally, phase 1 would end on its first frame and the pause after about 7 frames; Going
  Commando holds it while Triangle is. The layer keeps the timer armed (2) while Triangle is held in
  phases 0 and 1 (from the open, before the record is registered, so while the record is open and
  Triangle held), so phase 1 lasts until Triangle is let go; then phase 2's counter ends it and the
  ring fades and commits in play. To check against Going Commando on a real console or the port.
- Mode 7's counter starts at 6 frames (`ref_00286738_00`: a value of Going Commando's data, from
  rac2-decomp's note; no writer found either).
- **Where Going Commando's code does not say, Ratchet & Clank's behaviour is kept** (none of these is
  a guess at Going Commando's):
  - the quick select's place on the screen (above);
  - the ring, icons and marker: Ratchet & Clank's sprites and layout, its unselected icons' alpha
    scale, its marker's pulse (Going Commando's ring and highlight are its resident HUD's, not code);
  - the names (the gold name for a gold weapon) and the ammo format, Ratchet & Clank's;
  - no class preload before the commit, no notice or scripted-sequence hold, no hit-streak input
    block, no check of Going Commando's hero state 0x37 in the double tap: Ratchet & Clank has none;
  - the commit of item 0x18 (hero+0x1FF5), Ratchet & Clank's rule;
  - the quick swap itself (Ratchet & Clank's `hero_update_transitions` rule) and its usage count;
  - the health meter and bolt counter as Ratchet & Clank's Select shows them, for Going Commando's
    `level_forward_value_b`/`level_forward_value`; `Help_Update` for `level_run_callbacks` (where both
    games' play ticks call them, after the HUD records' update);
  - the click: Going Commando's code plays its global bank's sound 3; Ratchet & Clank's quick select
    is silent and has no UI click (the SDK's `ui_sound` plays only on the mod's own pages), so "Click
    sound" is off by default. On, it plays the level bank's entry 0 (`allocate_voice_for_bank_entry(0,
    1, 0)`, the sound a help message opens with): a stand-in, not Going Commando's sound.
- The gate's hero part is Ratchet & Clank's Triangle conditions (the item slot not locked, no timed
  HUD, the item locks, on foot or mode 3). The calls `hero_update_transitions` makes before it
  reaches them (damage, item input, water, firing) are not evaluated: they have side effects.
- The selector's and the bolt counter's callbacks are queued by their addresses in the loaded
  level: each level's own copy, from the decomps' catalogues (`src/layout.hpp`, levels 0 to 18), taken
  only when the disc's function table has the function of that name there (the log says, once a
  level). Without them there is no paused open in that level.
- The executable's globals are read where the loaded program keeps them (`Mod::relocate`): the game
  mode word `mode_freeze_init` writes (NTSC 0x15F604) is one a level keeps elsewhere, the same word
  the level's main loop reads (level 0's 0x15F5C4; the log says whether they agree, once a level).
- The gate's `blocked` includes whether the paused open can be entered now (callbacks found, the game
  mode play, no freeze on), so the double tap's timers run in the gate only when the freeze is
  entered: once a press, never beside the game's own (which made a single tap switch weapons in 1.0.0).
- With "Pause while open" on, a quick select Ratchet & Clank's own Triangle opens is always
  refused (Going Commando's decider opens the HUD quick select only with the option off); the health
  meter and bolt counter that opener shows still show. So in a level where the paused open cannot
  happen (the log says why), Triangle opens nothing with the option on.
- Every way out of the freeze ends it (back to the saved mode, Going Commando's `menu_end`, the
  reason in the log): mode 7's phases; a freeze of the mod's kind without the paused quick select or
  without its callbacks; the wheel's record not open after half a second; and two freeze pictures
  with no freeze tick between them (a port whose `rac1.freeze.update` does not run), which also
  turns the paused open off for the session. The mod's freeze kind always draws Going Commando's
  picture, never the freeze dialog.
- The gate asks again each tick a press is within its window (10 frames), as Going Commando's does:
  a tap's request ends for good once the press is out of the window; a hold opens 4 frames after.
- The NTSC address of `frame_scale` is the port's guess (not checked); a value out of range takes
  the tick rate's factor.
- The log (`openrac-rac1-ntsc.log`, lines "[mod gc-weapon-wheel]"): the first call of each hook
  point; once a level, the callbacks found (or which check failed) and the game mode words; on each
  Triangle press, the gate's result with its first failing condition and the values it read; each
  freeze entered (kind, saved mode) and left (why); each HUD-open request, one line: the play tick
  it was asked at, how it ended (done: the paused open; cancelled: Triangle let go or a double tap's
  swap) after how many ticks, and each tick's state before and after with the masked held word's
  Triangle bit (`01H 12H 22H 22H 22H 23H`: asked, counting, done; `21-` a cancel the gate asked again
  while the press was in its window); each double-tap swap (from, to, the timers) and each second
  release in the window that does not swap, and why; the game's own quick select refused.

## Placeholder art

[docs/placeholder-art.md](docs/placeholder-art.md): a private texture pack of Going Commando's quick
select pictures under Ratchet & Clank's hashes, made on your own machine from your own two discs
with `tools/placeholder_art.py`. None of it is in this repository.

## Build

```sh
python3 tools/fetch_sdk.py          # the SDK sdk.lock pins, into .sdk/
cmake -S . -B build -G Ninja
cmake --build build && ctest --test-dir build
python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/gc_weapon_wheel
```

`sdk.lock` pins the SDK's source at a commit of the Promises/OpenRAC fork (`git` and `commit`;
`tools/fetch_sdk.py` fetches that commit's `sdk/` alone); it goes to OpenRAC's develop, then to a
release's `url` and `sha256`, once the hook points are merged.

The test (`src/gc_weapon_wheel_test.cpp`) runs the copied code and the layer on a fake game: the
selection math (sectors, hysteresis, snap, nudge), the d-pad's neighbours from a Ratchet & Clank
page, the fades and the commit (the pending item, item 0x18's flag, the ammo clamp, the usage
count, the close), the draw (the ring, the marker, the ammo and name text and their
colours, the gold name), the paused path (the gate, `mode_freeze_init(9)`, mode 7's phases, the
release, the counter, the mode back, the music and sound group), the gate's conditions, the press
window and `PadPressedWithin`, the 50/60 Hz scaling, the quick swap, the relocated game mode word,
each level's callbacks, the taps (a tap does nothing and never freezes, a release before the request
is done cancels it, a double tap swaps once, a hold freezes), Ratchet & Clank's letterbox never
written, the place, a held press opening at the same tick each time, the cancel only on a
release, the double tap's logs, and the freeze's watchdogs.

## License

GNU General Public License version 3 ([LICENSE](LICENSE)). The files copied from rac2-decomp are
GPL-3.0-only, as that project gives no later version; the mod's own files are GPL-3.0-or-later.
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) names the source, its commit, the files and lines
copied, and its earlier MIT notice; the package carries it as `LICENSE.THIRD_PARTY_NOTICES.md`.
