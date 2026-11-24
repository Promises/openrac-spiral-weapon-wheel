// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where Ratchet & Clank keeps what this mod reads, writes and calls, on both discs. PAL from
// games/rac1/decomp (include/names/*.h, include/structs/hero.h, pad.h); NTSC-U from the port's
// translation of the NTSC decomp (games/rac1/ntsc-decomp) and its function table, checked in the
// generated C of OpenRAC at the commit sdk.lock pins. "Level" addresses are level 0's (the "L00"
// labels): every level has its own copy, which Mod::relocate_level(0, ...) finds.
#pragma once

#include <cstdint>

namespace gc_weapon_wheel {

// --- The hero (offsets from g_hero; the same on both discs) ---

// items[0].switch_lock (0x1090 + 0x1C): 2 blocks a change of item.
constexpr std::uint32_t kHeroSwitchLock = 0x10ACu;
// items[0].item_id: the item in hand (Going Commando's hero+0x1248).
constexpr std::uint32_t kHeroItem = 0x10B8u;
// unk1B2 (NTSC: item_select_override_timer): nonzero lets the quick select open over the item locks.
constexpr std::uint32_t kHeroSelectOverride = 0x1B2u;
// unk1FF5: what the quick select sets for item 0x18 in place of a pending item.
constexpr std::uint32_t kHeroItem18 = 0x1FF5u;
// vehicle_mode: 0 on foot, 1 Clank (the four-slot radial), 2 a vehicle, 3 (the quick select's other
// case). Going Commando's mode byte (hero+0x22B4).
constexpr std::uint32_t kHeroVehicleMode = 0x20A4u;
// item_select_blocked and item_use_locked (Going Commando's +0x22B7 and +0x22BC).
constexpr std::uint32_t kHeroSelectBlocked = 0x20A7u;
constexpr std::uint32_t kHeroUseLocked = 0x20ACu;
// pending_item[0]: the item hero_switch_item changes to (Going Commando's hero+0x22C8).
constexpr std::uint32_t kHeroPendingItem = 0x20B8u;
// item_selector_timer (hud_item_selector_close_timer): Going Commando's hero+0x24F6.
constexpr std::uint32_t kHeroCloseTimer = 0x22D2u;
// quick_swap_tap_timer and quick_swap_tap_window: Going Commando's hero+0x24F8 and +0x24FA.
constexpr std::uint32_t kHeroTapTimer = 0x22D4u;
constexpr std::uint32_t kHeroTapWindow = 0x22D6u;

// --- The pad (PadState, include/structs/pad.h) ---
constexpr std::uint32_t kPadHistIdx = 0x18Eu;
constexpr std::uint32_t kPadHistCount = 0x190u;
constexpr std::uint32_t kPadHeld = 0x1A0u;
constexpr std::uint32_t kPadPressed = 0x1A4u;
constexpr std::uint32_t kPadRawReleased = 0x1B8u;
constexpr std::uint32_t kPadHeldUnmasked = 0x1C0u;
constexpr std::uint32_t kPadPressedUnmasked = 0x1C4u;
constexpr std::uint32_t kPadMode = 0x1CCu;
constexpr std::uint32_t kPadStickMoved = 0x1D8u;
constexpr std::uint32_t kPadHistory = 0x1E0u;
constexpr std::uint32_t kPadStickX = 0x148u;
constexpr std::uint32_t kPadStickY = 0x14Cu;
constexpr std::uint32_t kTriangle = 0x10u;

// --- Tables ---
// item_defs: 0x4C a record; +0x38 icon (u16), +0x46 name (s16), +0x48 the gold name (s16).
constexpr std::uint32_t kItemDefStride = 0x4Cu;
constexpr std::uint32_t kItemDefIcon = 0x38u;
constexpr std::uint32_t kItemDefName = 0x46u;
constexpr std::uint32_t kItemDefGoldName = 0x48u;
// item_price_table: 24 bytes a record; +8 uses ammo (u16), +0xE its most ammo (u16).
constexpr std::uint32_t kPriceStride = 24u;
constexpr std::uint32_t kPriceUsesAmmo = 8u;
constexpr std::uint32_t kPriceMaxAmmo = 0xEu;
// usage_stats: 8 bytes an entry (count u16, best u16, level mask u32); 20 the quick select's, 31
// the quick swap's.
constexpr std::uint32_t kUsageStride = 8u;
constexpr int kUsageQuickSelect = 20;
constexpr int kUsageQuickSwap = 31;
// mode_freeze_info: +0 the kind, +0x14 the mode it was entered from.
constexpr std::uint32_t kFreezeInfoKind = 0x0u;
constexpr std::uint32_t kFreezeSavedMode = 0x14u;
// The HUD records (hud_chan_slots, 0x90 bytes each): +0x48 and +0x4A (cleared at init), +0x50 and
// +0x54 the position, +0x58 and +0x5C the size, +0x60 the anchor flags, +0x64 the handle, +0x6C the
// slide, +0x70 the fades, +0x74 the selection, +0x78 the state, +0x7C the timeout.
constexpr std::uint32_t kRecordWidth = 0x58u;
constexpr std::uint32_t kRecordHeight = 0x5Cu;
constexpr std::uint32_t kRecordHandle = 0x64u;
constexpr std::uint32_t kRecordSlide = 0x6Cu;
constexpr std::uint32_t kRecordFades = 0x70u;
constexpr std::uint32_t kRecordSelected = 0x74u;
// HUD sprite ids: the quick select's panel quarter and its selection marker.
constexpr int kPanelSprite = 0xE934;
constexpr int kMarkerSprite = 0xE99C;

struct Disc {
    bool ntsc;
    // --- The executable's globals ---
    std::uint32_t hero;               // g_hero
    std::uint32_t pad;                // g_controller_state
    std::uint32_t slot_items;         // the slot assignment, int[8]
    std::uint32_t ammo;               // weapon_ammo_counts
    std::uint32_t gold;               // the gold weapons' flags (item_text_variant), u8 per item
    std::uint32_t usage_stats;        // usage_stats
    std::uint32_t mode_freeze_info;   // mode_freeze_info
    std::uint32_t letterbox_height;   // letterbox_bar_height
    std::uint32_t letterbox_enabled;  // letterbox_enabled
    std::uint32_t bolts;              // gBolts
    std::uint32_t quick_swap_item;    // quick_swap_item
    std::uint32_t last_weapon;        // last_weapon_id
    std::uint32_t level_index;        // g_current_level_index
    std::uint32_t event_frames;       // gameplay_event_frame_count
    std::uint32_t game_mode;          // g_game_mode (mode_freeze_init and update_mode_freeze's)
    std::uint32_t frame_scale;        // frame_scale, scale_ticks' factor (float)
    // --- Level 0's data ---
    std::uint32_t item_defs;
    std::uint32_t item_price;
    std::uint32_t slot_tables;        // hud_selector_slot_tables: the page table's address
    std::uint32_t page;               // quick_select_page
    std::uint32_t slot_count;         // quick_select_slot_count
    std::uint32_t page0_entries;      // the eight-slot selector's entries
    std::uint32_t default_left;       // quick_select_default_left_slot (right, up, down follow)
    std::uint32_t cinematic_mode;     // the level's mode
    std::uint32_t cinematic_frames;   // ticks since it changed
    std::uint32_t level_frame_counter;
    std::uint32_t skip_frame;
    std::uint32_t draw_mask;          // debug_profiler_color
    std::uint32_t rendered_frame_count;
    std::uint32_t timed_hud_visible;
    std::uint32_t angle_offset;       // quick_select_slot_angle_offset
    std::uint32_t saved_selected;     // the selection a close leaves
    std::uint32_t saved_state;        // quick_select_last_state
    std::uint32_t close_frame;        // quick_select_close_frame
    std::uint32_t ammo_format;        // hud_ammo_format
    std::uint32_t width;              // quick_select_width, quick_select_height
    std::uint32_t height;
    std::uint32_t center_x;           // quick_select_center_x, quick_select_center_y
    std::uint32_t center_y;
    std::uint32_t ring_radius;        // float
    std::uint32_t ring_aspect;        // float
    std::uint32_t unselected_alpha;   // float
    // --- Level 0's code: the HUD callbacks queue_animation_update takes ---
    std::uint32_t selector_init, selector_update, selector_draw;
    std::uint32_t bolt_init, bolt_update, bolt_draw;
};

inline Disc disc_for(bool ntsc) {
    Disc d{};
    d.ntsc = ntsc;
    if (ntsc) {
        d.hero = 0x0013F350u;
        d.pad = 0x0013C940u;
        d.slot_items = 0x00141EA0u;
        d.ammo = 0x0013D428u;
        d.gold = 0x0013E520u;
        d.usage_stats = 0x00141848u;
        d.mode_freeze_info = 0x00193300u;
        d.letterbox_height = 0x0015F448u;
        d.letterbox_enabled = 0x0015F444u;
        d.bolts = 0x0015ED98u;
        d.quick_swap_item = 0x0015ED8Cu;
        d.last_weapon = 0x00141660u;
        d.level_index = 0x0015ED84u;
        d.event_frames = 0x0015EEA4u;
        d.game_mode = 0x0015F604u;
        d.frame_scale = 0x0015ED60u;
        d.item_defs = 0x00179AC0u;
        d.item_price = 0x001C40B0u;
        d.slot_tables = 0x0015FAC0u;
        d.page = 0x0015F72Cu;
        d.slot_count = 0x0015FA90u;
        d.page0_entries = 0x0017DAC0u;
        d.default_left = 0x0017DC18u;
        d.cinematic_mode = 0x0015F5C4u;
        d.cinematic_frames = 0x0015F5C8u;
        d.level_frame_counter = 0x0015F5CCu;
        d.skip_frame = 0x0015F5D8u;
        d.draw_mask = 0x0015F3F4u;
        d.rendered_frame_count = 0x0015F3F8u;
        d.timed_hud_visible = 0x0015F594u;
        d.angle_offset = 0x0015F784u;
        d.saved_selected = 0x0015FA9Cu;
        d.saved_state = 0x0015FA98u;
        d.close_frame = 0x0015FA94u;
        d.ammo_format = 0x0015F7C8u;
        d.width = 0x0015F718u;
        d.height = 0x0015F71Cu;
        d.center_x = 0x0015F720u;
        d.center_y = 0x0015F724u;
        d.ring_radius = 0x0015F78Cu;
        d.ring_aspect = 0x0015F7C4u;
        d.unselected_alpha = 0x0015F7C0u;
        d.selector_init = 0x00237AE8u;
        d.selector_update = 0x00237BA0u;
        d.selector_draw = 0x002382A0u;
        d.bolt_init = 0x00239CC8u;
        d.bolt_update = 0x00239D00u;
        d.bolt_draw = 0x00239DF8u;
    } else {
        d.hero = 0x0013F450u;
        d.pad = 0x0013CA40u;
        d.slot_items = 0x00141FA0u;
        d.ammo = 0x0013D530u;
        d.gold = 0x0013E620u;
        d.usage_stats = 0x00141948u;
        d.mode_freeze_info = 0x00193400u;
        d.letterbox_height = 0x0015F548u;
        d.letterbox_enabled = 0x0015F544u;
        d.bolts = 0x0015EE98u;
        d.quick_swap_item = 0x0015EE8Cu;
        d.last_weapon = 0x00141760u;
        d.level_index = 0x0015EE84u;
        d.event_frames = 0x0015EFA4u;
        d.game_mode = 0x0015F6E8u;
        d.frame_scale = 0x0015EE60u;
        d.item_defs = 0x00179BC0u;
        d.item_price = 0x001C43B0u;
        d.slot_tables = 0x0015FB78u;
        d.page = 0x0015F80Cu;
        d.slot_count = 0x0015FB48u;
        d.page0_entries = 0x0017DBC0u;
        d.default_left = 0x0017DD18u;
        d.cinematic_mode = 0x0015F6A8u;
        d.cinematic_frames = 0x0015F6ACu;
        d.level_frame_counter = 0x0015F6B0u;
        d.skip_frame = 0x0015F6BCu;
        d.draw_mask = 0x0015F4F4u;
        d.rendered_frame_count = 0x0015F4F8u;
        d.timed_hud_visible = 0x0015F674u;
        d.angle_offset = 0x0015F864u;
        d.saved_selected = 0x0015FB54u;
        d.saved_state = 0x0015FB50u;
        d.close_frame = 0x0015FB4Cu;
        d.ammo_format = 0x0015F8A8u;
        d.width = 0x0015F7F8u;
        d.height = 0x0015F7FCu;
        d.center_x = 0x0015F800u;
        d.center_y = 0x0015F804u;
        d.ring_radius = 0x0015F86Cu;
        d.ring_aspect = 0x0015F8A4u;
        d.unselected_alpha = 0x0015F8A0u;
        d.selector_init = 0x00238478u;
        d.selector_update = 0x00238530u;
        d.selector_draw = 0x00238C30u;
        d.bolt_init = 0x0023A658u;
        d.bolt_update = 0x0023A690u;
        d.bolt_draw = 0x0023A788u;
    }
    return d;
}

// Ratchet & Clank's functions by their names in each disc's build: PAL's "func_<address>"
// (games/rac1/decomp's names.h), NTSC's as the port's function table has them.
struct FunctionNames {
    const char* mode_freeze_init;
    const char* update_hud_elements;
    const char* help_update;
    const char* music_unpause;
    const char* music_pause;
    const char* snd_pause_group;
    const char* snd_continue_group;
    const char* snd_flush;
    const char* draw_hud_sprite;
    const char* get_icon_frame;
    const char* hud_frame;
    const char* font_print_center;
    const char* msg_string;
    const char* fast_tween_color;
    const char* hud_screen_position;
    const char* show_health_hud;
    const char* hud_keep_visible;
    const char* queue_animation_update;
    const char* remove_hud_item;
    const char* append_gif_transfer_packet;
    const char* draw_debug_profiler;
    const char* play_bank_sound;
    // The math the copied code does, Ratchet & Clank's own (optional: a faithful C stand-in
    // otherwise).
    const char* square_root;
    const char* fast_arc_tan;
    const char* diff_rots;
    const char* sub_rots;
    const char* normalize_angle;
    const char* add_rots;
    const char* sin;
    const char* cos;
    const char* int_to_float;
    const char* float_to_int;
};

inline FunctionNames function_names(bool ntsc) {
    if (ntsc) {
        return {"FUN_001fbab8", "FUN_L00_0023ac78", "FUN_001fde90", "music_unpause", "music_pause",
                "FUN_0012e3e8", "FUN_0012e418", "FUN_0012dc80", "FUN_001ffc30", "FUN_001ff960",
                "FUN_L00_0023b120", "FUN_001f6af0", "FUN_001fdd10", "FUN_001fa6e0", "FUN_L00_00235a70",
                "FUN_L00_00234e00", "FUN_L00_00235e18", "FUN_001ff308", "FUN_001ff480", "FUN_001fb368",
                "FUN_001f39d0", "FUN_0022db10",
                "FUN_001f9988", "FUN_001f9e90", "FUN_001fa688", "FUN_001fa5c8", "FUN_001fa610",
                "FUN_001fa580", "FUN_001f9de0", "FUN_001f9dc8", "func_001FA6C0", "FUN_001fa6d0"};
    }
    return {"func_001FBC80", "func_L00_0023B610", "func_001FE6C0", "func_00216F28", "func_00216EF0",
            "func_0012E528", "func_0012E558", "func_0012DDC0", "func_00200468", "func_00200198",
            "func_L00_0023BAB8", "func_001F6EA8", "func_001FE540", "func_001FA8A8", "func_L00_00236400",
            "func_L00_00235790", "func_L00_002367A8", "func_001FFB38", "func_001FFCB0", "func_001FB530",
            "func_001F3D78", "func_0022EE28",
            "func_001F9B50", "func_001FA058", "func_001FA850", "func_001FA790", "func_001FA7D8",
            "func_001FA748", "func_001F9FA8", "func_001F9F90", "func_001FA888", "func_001FA898"};
}

}  // namespace gc_weapon_wheel
