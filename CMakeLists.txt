# Spiral Weapon Wheel: an OpenRAC native mod for Ratchet & Clank (from the SDK's template).
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja
#   cmake --build build && ctest --test-dir build
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/spiral_weapon_wheel
#
# A tag v<version> (the manifest's) makes the workflow publish the release the launcher installs.
cmake_minimum_required(VERSION 3.20)
project(spiral_weapon_wheel LANGUAGES C CXX)

# The SDK: the one sdk.lock pins (tools/fetch_sdk.py puts it in .sdk/); OPENRAC_SDK=<dir> (an
# unpacked SDK, or sdk/ in a checkout of OpenRAC) or OpenRACMod_DIR=<dir>/cmake for another.
if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
find_package(OpenRACMod 1.41 REQUIRED)

# Windows: GitHub's windows-2025 image pairs a Visual Studio whose C++ library asks for a newer Clang
# than the clang-cl it ships (STL1000), depending on the image a run gets. The library's own switch
# lets this clang-cl build against it; MSVC is true for clang-cl too.
if(MSVC)
  add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
endif()

# Going Commando's weapon quick select's pause, copied from rac2-decomp (src/gc/*.c), and the
# translation layer that runs it on Ratchet & Clank with the spiral wheel in place of its ring
# (src/gc/rac1_shim.*, src/gc/rac1_spiral.cpp; the spiral itself in src/spiral/). The copied files include the layer's
# common.h (src/gc/shim/) in place of rac2-decomp's headers; they are compiled as they are, so the
# warnings their PS2-era casts and declarations raise on a host compiler are off for them, and the
# two a newer compiler makes errors (an address passed as an s32; a function of one pointer type
# passed for another) are warnings, which -w then silences.
set(gc_copied
  src/gc/menu_init_panel_layout.c
  src/gc/weapon_menu_init_slots.c
  src/gc/UpdateModalState.c
  src/gc/menu_set_mode.c
  src/gc/menu_end.c
  src/gc/menu_state_seven.c
  src/gc/menu_set_state_word_three.c
  src/gc/hud_letterbox.c
  src/gc/hud_open_request.c
  src/gc/level_update_countdown_state_machine.c
  src/gc/level_update_game_frame_input.c
  src/gc/hero_update_button_hold_timers.c
  src/gc/PadPressedWithin.c
  src/gc/hero_update_hit_streak_effect.c
  src/gc/gfx_render_frame_extra_layers.c
  src/gc/func_0029CE38.c)
set(gc_layer src/gc/rac1_shim.cpp src/gc/rac1_spiral.cpp src/spiral/spiral.cpp src/spiral/wheel.cpp src/spiral/glyph.cpp
  src/spiral/paint.cpp src/spiral/icon_packs.cpp src/spiral/json.cpp src/rac1/hud_set.cpp src/rac1/wad.cpp)
set(gc_copied_options -w)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  list(APPEND gc_copied_options -Wno-int-conversion -Wno-incompatible-pointer-types)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND gc_copied_options -Wno-incompatible-function-pointer-types)
  endif()
endif()
set_source_files_properties(${gc_copied} PROPERTIES
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/gc/shim"
  COMPILE_OPTIONS "${gc_copied_options}")
# weapon_menu_init_slots writes Ratchet & Clank's page table word itself (gc/shim/common.h).
set_source_files_properties(src/gc/weapon_menu_init_slots.c PROPERTIES
  COMPILE_DEFINITIONS GC_SHIM_UNIT_INIT_SLOTS)
# The gate has its own view of the hero (gc/shim/common.h).
set_source_files_properties(src/gc/level_update_game_frame_input.c PROPERTIES
  COMPILE_DEFINITIONS GC_SHIM_UNIT_GATE)
# hero_update_button_hold_timers passes an address as an s32 (gc/shim/common.h).
set_source_files_properties(src/gc/hero_update_button_hold_timers.c PROPERTIES
  COMPILE_DEFINITIONS GC_SHIM_HANDLES)

openrac_add_mod(spiral_weapon_wheel MANIFEST openrac-mod.json SOURCES src/mod.cpp ${gc_layer} ${gc_copied})
target_compile_features(spiral_weapon_wheel PRIVATE cxx_std_17)
target_include_directories(spiral_weapon_wheel PRIVATE src)
# The notices of what is copied go in the package (and so the archive, which carries the files
# named LICENSE* beside the manifest). The wheel's panels are made as the mod loads (paint.hpp);
# its own glyphs, the user's original drawings, are an icon pack in assets/glyphs/ (the manifest's
# "assets"), under their own terms: GPL-2.0-or-later (assets/glyphs/LICENSE-glyphs).
get_target_property(spiral_weapon_wheel_package spiral_weapon_wheel OPENRAC_MOD_PACKAGE_DIR)
configure_file(THIRD_PARTY_NOTICES.md "${spiral_weapon_wheel_package}/LICENSE.THIRD_PARTY_NOTICES.md" COPYONLY)
file(GLOB spiral_glyphs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/glyphs/rac1/*")
foreach(glyph ${spiral_glyphs})
  get_filename_component(glyph_name "${glyph}" NAME)
  configure_file("${glyph}" "${spiral_weapon_wheel_package}/assets/glyphs/rac1/${glyph_name}" COPYONLY)
endforeach()
configure_file(assets/glyphs/LICENSE-glyphs "${spiral_weapon_wheel_package}/assets/glyphs/LICENSE-glyphs" COPYONLY)

# The spiral's math against Prey Menu's formulas, and the translation layer with the copied code on
# a fake game (ctest; the workflow runs them on every platform).
enable_testing()
add_executable(spiral_test src/spiral_test.cpp src/spiral/spiral.cpp src/spiral/wheel.cpp src/spiral/glyph.cpp
  src/spiral/paint.cpp)
target_compile_features(spiral_test PRIVATE cxx_std_17)
add_test(NAME spiral COMMAND spiral_test)
add_executable(icons_test src/icons_test.cpp src/spiral/icon_packs.cpp src/spiral/json.cpp src/spiral/glyph.cpp
  src/rac1/hud_set.cpp src/rac1/wad.cpp)
target_compile_features(icons_test PRIVATE cxx_std_17)
target_include_directories(icons_test PRIVATE src)
# The tests read the mod's own glyphs from the source tree, through the loader any pack goes through.
target_compile_definitions(icons_test PRIVATE SPIRAL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME icons COMMAND icons_test)
add_executable(spiral_weapon_wheel_test src/spiral_weapon_wheel_test.cpp ${gc_layer} ${gc_copied})
target_compile_features(spiral_weapon_wheel_test PRIVATE cxx_std_17)
target_include_directories(spiral_weapon_wheel_test PRIVATE src)
target_compile_definitions(spiral_weapon_wheel_test PRIVATE SPIRAL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME spiral_weapon_wheel COMMAND spiral_weapon_wheel_test)
# Every behaviour the wheel has had, a named check each (README "Behaviours and their tests").
add_test(NAME regressions COMMAND spiral_weapon_wheel_test --regressions)
# The release guard itself trips on a manifest with a debug setting (every build).
add_test(NAME release_guard_trips
         COMMAND ${CMAKE_COMMAND} -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/tools/testdata/manifest_with_debug_setting.json
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/no_debug_settings.cmake)
set_tests_properties(release_guard_trips PROPERTIES WILL_FAIL TRUE)
# A release (a tag's build, or -DSPIRAL_RELEASE=ON) fails while a temporary debug setting is still
# in the manifest (README "Remove before release").
if("$ENV{GITHUB_REF}" MATCHES "^refs/tags/" OR SPIRAL_RELEASE)
  add_test(NAME no_debug_settings
           COMMAND ${CMAKE_COMMAND} -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/openrac-mod.json
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/no_debug_settings.cmake)
endif()
