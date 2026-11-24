# Going Commando weapon wheel: an OpenRAC native mod for Ratchet & Clank (from the SDK's template).
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja
#   cmake --build build && ctest --test-dir build
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/gc_weapon_wheel
#
# A tag v<version> (the manifest's) makes the workflow publish the release the launcher installs.
cmake_minimum_required(VERSION 3.20)
project(gc_weapon_wheel LANGUAGES C CXX)

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
find_package(OpenRACMod 1.39 REQUIRED)

# Windows: GitHub's windows-2025 image pairs a Visual Studio whose C++ library asks for a newer Clang
# than the clang-cl it ships (STL1000), depending on the image a run gets. The library's own switch
# lets this clang-cl build against it; MSVC is true for clang-cl too.
if(MSVC)
  add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
endif()

# Going Commando's weapon quick select, copied from rac2-decomp (src/gc/*.c), and the translation
# layer that runs it on Ratchet & Clank (src/gc/rac1_shim.*). The copied files include the layer's
# common.h (src/gc/shim/) in place of rac2-decomp's headers; they are compiled as they are, so the
# warnings their PS2-era casts and declarations raise on a host compiler are off for them, and the
# two a newer compiler makes errors (an address passed as an s32; a function of one pointer type
# passed for another) are warnings, which -w then silences.
set(gc_copied
  src/gc/weapon_wheel_update_select.c
  src/gc/weapon_wheel_draw.c
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
set(gc_layer src/gc/rac1_shim.cpp)
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

openrac_add_mod(gc_weapon_wheel MANIFEST openrac-mod.json SOURCES src/mod.cpp ${gc_layer} ${gc_copied})
target_compile_features(gc_weapon_wheel PRIVATE cxx_std_17)

# The notices of what is copied go in the package (and so the archive, which carries the files
# named LICENSE* beside the manifest).
get_target_property(gc_weapon_wheel_package gc_weapon_wheel OPENRAC_MOD_PACKAGE_DIR)
configure_file(THIRD_PARTY_NOTICES.md "${gc_weapon_wheel_package}/LICENSE.THIRD_PARTY_NOTICES.md" COPYONLY)

# The translation layer with the copied code, on a fake game (ctest; the workflow runs it on every
# platform).
enable_testing()
add_executable(gc_weapon_wheel_test src/gc_weapon_wheel_test.cpp ${gc_layer} ${gc_copied})
target_compile_features(gc_weapon_wheel_test PRIVATE cxx_std_17)
add_test(NAME gc_weapon_wheel COMMAND gc_weapon_wheel_test)
