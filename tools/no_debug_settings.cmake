# A release must not carry a temporary debug setting (README "Remove before release"): fails when
# the manifest still has a setting whose key starts with "debug_". The build runs it as a test in
# a tag's build (CMakeLists.txt), so a release with one fails its CI.
#   cmake -DMANIFEST=openrac-mod.json -P tools/no_debug_settings.cmake
file(READ "${MANIFEST}" manifest)
string(REGEX MATCHALL "\"key\"[ \t]*:[ \t]*\"debug_[^\"]*\"" found "${manifest}")
if(found)
  message(FATAL_ERROR "Temporary debug settings still in the manifest (remove before release): ${found}")
endif()
message(STATUS "No debug settings in ${MANIFEST}")
