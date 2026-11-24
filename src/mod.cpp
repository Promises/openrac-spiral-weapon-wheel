// SPDX-License-Identifier: GPL-3.0-or-later
//
// Spiral Weapon Wheel: a weapon wheel for Ratchet & Clank in the style of Prey (2017)'s favourites
// wheel, after "Prey Menu" (KerosenileArsonist's Fallout: New Vegas mod: its spiral, with its
// author's credit as its Nexus permissions ask), on the pause and timing of Going
// Commando's weapon quick select, whose code it runs (the mod it grew from, "Going Commando weapon
// wheel").
//
// The pause, the hold before it and the double tap are Going Commando's own code (src/gc/*.c,
// copied from rac2-decomp) through the translation layer (src/gc/rac1_shim.*). The wheel is the
// spiral (src/spiral/, game-agnostic): every owned weapon and gadget of the hand, the stick turning
// the pointer around the loops. Its look is the user's design (src/spiral/look.hpp: Ratchet &
// Clank's menu panels bent around the spiral), drawn by the mod's overlay under the game's HUD
// (interface 1.40's pictures and OPENRAC_OVERLAY_UNDER_HUD) from pictures it makes as it loads
// (src/spiral/paint.cpp; no art is shipped); the items' fallback icons and the text are the
// game's own, drawn through the quick select's HUD record. Before the pause the game slows down for a moment (the port's
// time_stretch), as Prey's wheel eases time down.
//
// Seven hook points:
//   rac1.level.update_play          game mode 0's tick: Going Commando's gate before it, and the
//                                   slow-down's tick; when the pause opens, the tick is skipped
//   rac1.freeze.update              the freeze mode's tick: the mod's kind runs UpdateModalState
//                                   (case 7), then the game's (the sound)
//   rac1.freeze.draw                the freeze mode's picture: the world and its HUD, no dialog
//   rac1.hud.quick_select.init      the quick select's HUD record: Going Commando's init, the
//   rac1.hud.quick_select.update    spiral's update and draw
//   rac1.hud.quick_select.draw
//   rac1.menu.item_grid.draw        the pause menu's item grid: the items' glyphs captured from the
//                                   HUD's icons while its pages are certain
// The first three need OpenRAC with those hook points; the pictures need mod interface 1.40.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"

#include "gc/rac1_shim.hpp"
#include "layout.hpp"
#include "spiral/paint.hpp"

namespace {

namespace gcw = gc_weapon_wheel;
namespace gc = gc_weapon_wheel::gc;
namespace rac1 = openrac::rac1;

openrac::Mod* g_mod = nullptr;

gc::Shim& layer() { return gc::shim(); }

constexpr const char* kName = "Spiral Weapon Wheel";
constexpr const char* kId = "spiral-weapon-wheel";  // the manifest's, its folder's name
constexpr float kPi = 3.14159265358979f;

// The folder the mod's library is in (its package's folder).
std::string own_folder() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&own_folder), &module) == 0) {
        return "";
    }
    wchar_t path[2048];
    const DWORD n = GetModuleFileNameW(module, path, 2048);
    if (n == 0 || n >= 2048) {
        return "";
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(n), out.data(), bytes, nullptr, nullptr);
    const std::size_t cut = out.find_last_of("\\/");
    return cut == std::string::npos ? "" : out.substr(0, cut);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&own_folder), &info) == 0 || info.dli_fname == nullptr) {
        return "";
    }
    const std::string path = info.dli_fname;
    const std::size_t cut = path.find_last_of('/');
    return cut == std::string::npos ? "" : path.substr(0, cut);
#endif
}

// The package's folder: the library is in its lib/<platform>/ (the SDK's package layout), else
// beside it; without either, the plugins folder's spiral-weapon-wheel/. Found by the mod's own
// glyphs (assets/glyphs/rac1/icons.json).
std::string package_folder(const openrac::Mod& mod) {
    const std::string library = own_folder();
    for (const std::string& folder : {library + "/../..", library}) {
        if (!library.empty() && std::ifstream(folder + "/assets/glyphs/rac1/icons.json", std::ios::binary)) {
            return folder;
        }
    }
    return mod.plugins_folder() + "/" + kId;
}

// --- The pictures (paint.hpp: made as the mod loads) ---

struct Art {
    int scan = 0, white = 0, badge = 0, pointer = 0, noise = 0;
    int digits[10] = {};
    bool all = false;
};
Art g_art;

void make_art(openrac::Mod& mod) {
    auto make = [&mod](spiral::Art which) {
        const spiral::Picture p = spiral::make_art(which);
        const int id = mod.overlay_image_create(p.width, p.height, p.rgba.data(), which != spiral::Art::Noise);
        if (id == 0) {
            mod.warn(std::string(kName) + ": the port refused a picture of the wheel");
        }
        return id;
    };
    g_art.scan = make(spiral::Art::Scan);
    g_art.white = make(spiral::Art::White);
    g_art.badge = make(spiral::Art::Badge);
    g_art.pointer = make(spiral::Art::Pointer);
    g_art.noise = make(spiral::Art::Noise);
    for (int d = 1; d <= 9; ++d) {
        const spiral::Picture p = spiral::make_digit(d);
        g_art.digits[d] = mod.overlay_image_create(p.width, p.height, p.rgba.data(), true);
    }
    g_art.all = g_art.scan != 0 && g_art.white != 0 && g_art.badge != 0 && g_art.pointer != 0;
}

int picture_for(const spiral::Quad& q) {
    switch (q.art) {
        case spiral::Art::Scan: return g_art.scan;
        case spiral::Art::White: return g_art.white;
        case spiral::Art::Badge: return g_art.badge;
        case spiral::Art::Pointer: return g_art.pointer;
        case spiral::Art::Glyph:
        case spiral::Art::Image: return q.image;
        case spiral::Art::Noise: return g_art.noise;
        case spiral::Art::Digit: return q.image >= 1 && q.image <= 9 ? g_art.digits[q.image] : 0;
    }
    return 0;
}

// The overlay's call under the game's 2D: the wheel's panels (paint.hpp). Its places are the
// screen's heights from the middle; the overlay's are 512 x 448 units over the whole frame (a unit
// across is aspect * 448 / 512 of one down).
void draw_overlay(void*, const openrac_overlay* overlay) {
    if (!g_art.all || openrac::overlay_layer(overlay) != OPENRAC_OVERLAY_LAYER_UNDER_HUD) {
        return;
    }
    const gc::WheelPicture picture = layer().picture();
    if (!picture.shown) {
        return;
    }
    const float aspect = overlay->aspect > 0.0f ? overlay->aspect : 4.0f / 3.0f;
    auto byte = [](float v) { return static_cast<std::uint32_t>(std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255.0f)); };
    // The flash of static is added (as the game's GS blend Cs * FIX + Cd); the rest blended.
    const bool can_add = openrac::overlay_has_set_blend(overlay);
    spiral::Blend blend = spiral::Blend::Alpha;
    for (const spiral::Quad& q : spiral::paint(picture.frame)) {
        if (q.blend != blend) {
            if (q.blend == spiral::Blend::Add && !can_add) {
                continue;  // a port without the blend: no flash rather than a grey one
            }
            overlay->set_blend(overlay->context, q.blend == spiral::Blend::Add ? OPENRAC_OVERLAY_BLEND_ADD : OPENRAC_OVERLAY_BLEND_ALPHA);
            blend = q.blend;
        }
        const int id = picture_for(q);
        if (id == 0 || q.rgba[3] <= 0.0f) {
            continue;
        }
        float xy[8];
        for (int i = 0; i < 4; ++i) {
            xy[2 * i] = 256.0f + q.xy[2 * i] * 512.0f / aspect;
            xy[2 * i + 1] = 224.0f + q.xy[2 * i + 1] * 448.0f;
        }
        const std::uint32_t rgba = (byte(q.rgba[0]) << 24) | (byte(q.rgba[1]) << 16) | (byte(q.rgba[2]) << 8) | byte(q.rgba[3]);
        openrac::overlay_image_quad(overlay, id, xy, q.uv, rgba);
    }
    if (blend != spiral::Blend::Alpha) {
        overlay->set_blend(overlay->context, OPENRAC_OVERLAY_BLEND_ALPHA);
    }
}

// --- The settings ---

int open_delay_frames(const std::string& choice) {
    if (choice == "going_commando") {
        return gc::kGcOpenDelay;
    }
    if (choice == "longer") {
        return gc::kOpenDelayLonger;
    }
    return gc::kOpenDelayLittleLonger;
}

int ramp_frames(const std::string& choice) {
    if (choice == "off") {
        return gc::kRampOff;
    }
    if (choice == "prey") {
        return gc::kRampPrey;
    }
    return gc::kRampShort;
}

void read_settings(openrac::Mod& mod) {
    layer().set_options(mod.setting<bool>("pause"), mod.setting<bool>("click"),
                        open_delay_frames(mod.setting<std::string>("open_delay")));
    gc::WheelOptions w;
    w.look.scale = static_cast<float>(mod.setting<double>("scale"));
    w.look.spiral.start_deg = static_cast<float>(mod.setting<int>("start_angle"));
    w.look.spiral.clockwise = mod.setting<bool>("clockwise");
    w.look.spiral.depth = static_cast<float>(mod.setting<double>("depth"));
    w.look.continuous = mod.setting<std::string>("spiral_motion") != "loop_steps";
    w.look.outward = mod.setting<std::string>("spiral_grows") != "inward";
    // TEMPORARY (remove before release): the debug preview, the wheel's look only.
    const std::string preview = mod.setting<std::string>("debug_preview");
    w.debug_preview = preview == "gold_visibomb_gun"  ? gc::DebugPreview::GoldVisibombGun
                      : preview == "all_weapons_gold" ? gc::DebugPreview::AllWeaponsGold
                      : preview == "everything"       ? gc::DebugPreview::Everything
                                                      : gc::DebugPreview::Off;
    w.toggle = mod.setting<std::string>("mode") == "toggle";
    w.ramp_frames = ramp_frames(mod.setting<std::string>("slowdown"));
    w.ease_back = mod.setting<bool>("ease_back");
    layer().set_wheel_options(w);
}

void connect() { layer().connect(g_mod->loaded_level()); }

// The first time each hook point runs, in the log.
void first(const char* hook) {
    layer().log_once(std::string("hook ") + hook, std::string("first call of ") + hook);
}

template <class Fn>
Fn optional_function(openrac::Mod& mod, const char* name) {
    return mod.function<Fn>(name);
}

void connect_game(openrac::Mod& mod, bool ntsc) {
    gc::Game& g = layer().game;
    openrac::Mod* m = &mod;
    g.memory = gc::Memory{mod.memory().base()};
    g.disc = gcw::disc_for(ntsc);
    const gcw::FunctionNames n = gcw::function_names(ntsc);

    g.relocate = [m](std::uint32_t a) { return m->relocate_level(0, a); };
    g.relocate_exe = [m](std::uint32_t a) { return m->relocate(a); };
    // A level's copy of a function, at its place from the decomps' catalogues (layout.hpp): taken
    // only when the disc's function table has the function of that name there.
    g.code_matches = [m](std::uint32_t address, const char* name) {
        using Any = void (*)();
        const Any at = m->function_at<Any>(address);
        return at != nullptr && at == m->function<Any>(name);
    };
    using V = void (*)();
    using VI = void (*)(int);
    using II = int (*)(int);
    const auto freeze_init = mod.required_function<void (*)(unsigned int, int, int)>(n.mode_freeze_init);
    g.mode_freeze_init = [freeze_init](int kind, int a, int b) { freeze_init(static_cast<unsigned int>(kind), a, b); };
    g.update_hud_elements = mod.required_function<II>(n.update_hud_elements);
    if (const auto help = optional_function<V>(mod, n.help_update)) {
        g.help_update = help;
    }
    g.music_unpause = mod.required_function<V>(n.music_unpause);
    g.music_pause = mod.required_function<VI>(n.music_pause);
    g.snd_pause_group = mod.required_function<VI>(n.snd_pause_group);
    g.snd_continue_group = mod.required_function<VI>(n.snd_continue_group);
    const auto flush = mod.required_function<int (*)()>(n.snd_flush);
    g.snd_flush = [flush] { flush(); };
    g.draw_hud_sprite = mod.required_function<void (*)(int, int, int, int, int, int)>(n.draw_hud_sprite);
    g.get_icon_frame = mod.required_function<int (*)(int, int)>(n.get_icon_frame);
    const auto frame = mod.required_function<void (*)(std::uint32_t, int, int, int, unsigned int, int)>(n.hud_frame);
    g.hud_frame = [frame](std::uint32_t rec, int f, int x, int y, std::uint32_t scale, int alpha) {
        frame(rec, f, x, y, scale, alpha);
    };
    // FontPrintCenter takes the text's game address: the text goes on the game's stack for the call.
    const auto print = mod.required_function<int (*)(int, int, unsigned long long, int, int)>(n.font_print_center);
    g.font_print_center = [m, print](int x, int y, std::uint32_t colour, const char* text, int length) {
        const std::size_t size = std::strlen(text) + 1;
        const openrac::StackFrame frame(*m, static_cast<std::uint32_t>(size));
        std::memcpy(m->memory().pointer(frame.address()), text, size);
        print(x, y, colour, static_cast<int>(frame.address()), length);
    };
    using Print = int (*)(int, int, unsigned long long, int, int);
    for (const auto& [name, slot] : {std::pair<const char*, decltype(&g.font_print_center_small)>{n.font_print_center_small,
                                                                                                    &g.font_print_center_small},
                                     {n.font_print_center_large, &g.font_print_center_large}}) {
        if (const auto other = optional_function<Print>(mod, name)) {
            *slot = [m, other](int x, int y, std::uint32_t colour, const char* text, int length) {
                const std::size_t size = std::strlen(text) + 1;
                const openrac::StackFrame frame(*m, static_cast<std::uint32_t>(size));
                std::memcpy(m->memory().pointer(frame.address()), text, size);
                other(x, y, colour, static_cast<int>(frame.address()), length);
            };
        }
    }
    // The game's text widths (measure_text_width_regular, _small), the text on the game's stack.
    {
        using Width = int (*)(int, int);
        const auto regular = optional_function<Width>(mod, n.text_width_regular);
        const auto small = optional_function<Width>(mod, n.text_width_small);
        if (regular && small) {
            g.text_width = [m, regular, small](gc::TextSize size, const char* text) {
                const std::size_t length = std::strlen(text) + 1;
                const openrac::StackFrame frame(*m, static_cast<std::uint32_t>(length));
                std::memcpy(m->memory().pointer(frame.address()), text, length);
                const int address = static_cast<int>(frame.address());
                return size == gc::TextSize::Small ? small(address, -1) : regular(address, -1);
            };
        }
    }
    const auto msg = mod.required_function<int (*)(int)>(n.msg_string);
    g.msg_string = [msg](int id) { return static_cast<std::uint32_t>(msg(id)); };
    const auto tween = mod.required_function<unsigned long long (*)(unsigned int, unsigned int, float)>(n.fast_tween_color);
    g.fast_tween_color = [tween](std::uint32_t a, std::uint32_t b, float t) { return static_cast<std::uint32_t>(tween(a, b, t)); };
    // hud_screen_position writes through pointers to its x and y: they go on the game's stack.
    const auto position = mod.required_function<int (*)(std::uint32_t, std::uint32_t, std::uint32_t)>(n.hud_screen_position);
    g.hud_screen_position = [m, position](std::uint32_t rec, int& x, int& y) {
        const openrac::StackFrame frame(*m, 8);
        const openrac::Memory memory = m->memory();
        memory.write<std::int32_t>(frame.address(), x);
        memory.write<std::int32_t>(frame.address() + 4, y);
        position(rec, frame.address(), frame.address() + 4);
        x = memory.read<std::int32_t>(frame.address());
        y = memory.read<std::int32_t>(frame.address() + 4);
    };
    g.show_health_hud = mod.required_function<int (*)()>(n.show_health_hud);
    g.hud_keep_visible = mod.required_function<int (*)(int, int)>(n.hud_keep_visible);
    const auto queue = mod.required_function<int (*)(int, int, int, int, int, int, int)>(n.queue_animation_update);
    g.queue_animation_update = [queue](int key, int a, std::uint32_t init, std::uint32_t update, std::uint32_t draw,
                                       std::uint32_t data, int b) {
        return queue(key, a, static_cast<int>(init), static_cast<int>(update), static_cast<int>(draw),
                     static_cast<int>(data), b);
    };
    g.remove_hud_item = mod.required_function<II>(n.remove_hud_item);
    g.append_gif_transfer_packet = mod.required_function<V>(n.append_gif_transfer_packet);
    g.draw_debug_profiler = mod.required_function<V>(n.draw_debug_profiler);
    const auto bank = mod.required_function<int (*)(int, int, int)>(n.play_bank_sound);
    g.play_click = [bank] { bank(0, 1, 0); };
    // The slow-down before the pause: the port's slow motion (interface 1.35).
    g.time_stretch = [m](float stretch) { m->time_stretch(stretch); };
    // The items' glyphs, made from the player's own game as it runs, as overlay pictures.
    g.create_image = [m](int width, int height, const std::uint8_t* rgba) {
        return m->overlay_image_create(width, height, rgba, true);
    };
    g.create_image_nearest = [m](int width, int height, const std::uint8_t* rgba) {
        return m->overlay_image_create(width, height, rgba, false);
    };
    g.destroy_image = [m](int image) { m->overlay_image_destroy(image); };
    // The port's items for registered weapons (rac1_weapon_register: "native extension slot 1 or
    // 36"). Another, when the port has more, is one more here.
    g.extension_items = {1, 36};
    // The items' icons from the player's own disc data (the level's HUD set) and from icon files.
    const std::string game(mod.game());
    g.level_data_path = [m, game](int level) {
        std::string data;
        return m->game_data_dir(game, data) > 0 ? gcw::hud::level_data_path(data, game, level) : std::string();
    };
    g.icon_packs = [m, game] {
        // The mod's own glyphs (a pack in its package), then the other mods' packs by their
        // folders' names, then the player's own: each later one's icons over the earlier's.
        std::vector<std::string> packs{package_folder(*m) + "/assets/glyphs/" + game};
        const std::string plugins = m->plugins_folder();
        if (plugins.empty()) {
            return packs;
        }
        std::error_code e;
        std::vector<std::string> mods;
        for (const auto& entry : std::filesystem::directory_iterator(plugins, e)) {
            const std::string name = entry.path().filename().string();
            if (entry.is_directory(e) && name != kId) {
                mods.push_back(name);
            }
        }
        std::sort(mods.begin(), mods.end());
        for (const std::string& name : mods) {
            packs.push_back(plugins + "/" + name + "/spiral-wheel-icons");
        }
        packs.push_back(plugins + "/" + kId + "/icons");
        return packs;
    };

    // The math: Ratchet & Clank's own where its build has them by these names.
    if (const auto f = optional_function<float (*)(float)>(mod, n.square_root)) g.square_root = f;
    if (const auto f = optional_function<float (*)(float, float)>(mod, n.fast_arc_tan)) g.fast_arc_tan = f;
    if (const auto f = optional_function<float (*)(float, float)>(mod, n.diff_rots)) g.diff_rots = f;
    if (const auto f = optional_function<float (*)(float, float)>(mod, n.sub_rots)) g.sub_rots = f;
    if (const auto f = optional_function<float (*)(float)>(mod, n.normalize_angle)) g.normalize_angle = f;
    if (const auto f = optional_function<float (*)(float, float)>(mod, n.add_rots)) g.add_rots = f;
    if (const auto f = optional_function<float (*)(float)>(mod, n.sin)) g.sin = f;
    if (const auto f = optional_function<float (*)(float)>(mod, n.cos)) g.cos = f;
    if (const auto f = optional_function<float (*)(int)>(mod, n.int_to_float)) g.int_to_float = f;
    if (const auto f = optional_function<int (*)(float)>(mod, n.float_to_int)) g.float_to_int = f;

    g.log = [m](const char* what) { m->warn(std::string(kName) + ": " + what); };
    g.info = [m](const char* what) { m->info(std::string(kName) + ": " + what); };
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }
    const bool ntsc = mod.version() == "ntsc";
    if (!ntsc && mod.version() != "pal") {
        throw openrac::Error("this mod knows the PAL and NTSC-U discs only");
    }
    if (!mod.has_overlay_images()) {
        throw openrac::Error("this mod needs OpenRAC with mod interface 1.40 (pictures in a mod's overlay)");
    }
    g_mod = &mod;
    connect_game(mod, ntsc);
    read_settings(mod);
    make_art(mod);

    mod.hook<rac1::Level_UpdatePlay>([](auto original) {
        connect();
        first("rac1.level.update_play");
        layer().update_play([&] { original(); });
    });
    mod.hook<rac1::Freeze_Update>([](auto original) {
        connect();
        first("rac1.freeze.update");
        layer().freeze_update([&] { original(); });
    });
    mod.hook<rac1::Freeze_Draw>([](auto original) {
        connect();
        first("rac1.freeze.draw");
        layer().freeze_draw([&] { original(); });
    });
    mod.hook<rac1::Hud_QuickSelect_Init>([](auto, std::uint32_t record) {
        connect();
        first("rac1.hud.quick_select.init");
        layer().selector_init(record);
    });
    mod.hook<rac1::Hud_QuickSelect_Update>([](auto, std::uint32_t record) {
        connect();
        first("rac1.hud.quick_select.update");
        layer().selector_update(record);
    });
    mod.hook<rac1::Hud_QuickSelect_Draw>([](auto, std::uint32_t record) -> std::int32_t {
        connect();
        first("rac1.hud.quick_select.draw");
        return layer().selector_draw(record);
    });
    // The glyphs, captured while the pause menu's item grid draws (its HUD pages certain).
    mod.hook<rac1::Menu_ItemGrid_Draw>([](auto original, std::uint32_t grid) -> std::int32_t {
        connect();
        first("rac1.menu.item_grid.draw");
        layer().menu_grid_draw();
        return original(grid);
    });
        // The pictures, under the game's HUD, in play and in the pause (the freeze mode is a menu's phase).
    mod.on_overlay_phases(draw_overlay, nullptr, OPENRAC_OVERLAY_PLAYING | OPENRAC_OVERLAY_MENU | OPENRAC_OVERLAY_UNDER_HUD);

    if (mod.has_settings_set()) {
        mod.on_settings([] { read_settings(*g_mod); });
    }
    mod.info(std::string(kName) + ": the spiral on Triangle (Going Commando's pause, Prey Menu's spiral)" +
             (g_art.all ? "" : "; the port refused its pictures"));
}

}  // namespace

OPENRAC_MOD_CPP("spiral-weapon-wheel", "1.4.5", load)
