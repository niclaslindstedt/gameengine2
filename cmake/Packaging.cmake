# Installs the engine as the CMake package `gameengine2`, the thing every
# release publishes (docs/publishing.md). Included once, at the end of the
# root CMakeLists.txt, after every exported target exists.
#
#   find_package(gameengine2 0.1 REQUIRED)
#   target_link_libraries(game PRIVATE ge::core ge::audio ge::racing)   # ge::fp comes with them
#   target_link_libraries(labs PRIVATE ge::lab)                         # a game's labs (docs/labs.md)
#   target_link_libraries(play PRIVATE ge::play)                       # its play target (ge play)
#
# The ge executable (cmake/Cli.cmake) installs to bin/ beside the libraries; it is a tool, not
# a target a game links.
#
# The package version is the VERSION file — the one place it is stated; the
# release workflow writes it. Every target a consumer links is listed in
# GE_EXPORTED_TARGETS below with its export name; a new public target is a
# row there. ge::fp carries the floating-point determinism flags to the
# consumer, so a game built against the package computes what the engine's
# own tests computed. ge_flags (warnings, -Werror) is linked
# only as $<BUILD_INTERFACE:...> and never reaches a consumer.
#
# Headers install under include/gameengine2/ with the same layout as
# engine/ and plugins/, so an #include is spelled the same in-tree and installed.

include_guard(GLOBAL)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" GE_PACKAGE_VERSION LIMIT_COUNT 1)
set(GE_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/gameengine2")
set(GE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/gameengine2")

# target -> export name (consumers see ge::<export name>)
set(GE_EXPORTED_TARGETS ge_fp fp ge_isa isa ge_core core ge_audio audio ge_assets assets ge_ui ui ge_ai ai
  ge_wind wind ge_water water ge_net net ge_console console ge_input input ge_move move ge_camera camera ge_texgen texgen ge_lab lab ge_prof prof ge_prof_alloc prof_alloc ge_play play
  ge_replay replay)
# Every plugin is published as ge::<plugin> (cmake/Plugins.cmake collects them), and its
# presentation half as ge::<plugin>_app when every engine target it links is published here
# (APP audio is; render, gpu and platform are not yet, below).
set(GE_PUBLISHED_MODULES core audio assets ui ai wind water console input move camera texgen lab prof play replay)
get_property(_ge_plugins GLOBAL PROPERTY GE_PLUGIN_TARGETS)
foreach(_target IN LISTS _ge_plugins)
  get_property(_app_modules TARGET ${_target} PROPERTY GE_APP_MODULES)
  set(_unpublished ${_app_modules})
  list(REMOVE_ITEM _unpublished ${GE_PUBLISHED_MODULES})
  if(_unpublished)
    continue()
  endif()
  string(REGEX REPLACE "^ge_" "" _name ${_target})
  list(APPEND GE_EXPORTED_TARGETS ${_target} ${_name})
  list(APPEND _ge_published_apps ${_name})
endforeach()

# A presentation half that is not published (or not built) installs no headers: they include
# engine modules the package does not carry (gpu/, render/).
set(_ge_app_excludes "")
file(GLOB _ge_plugin_dirs LIST_DIRECTORIES true "${PROJECT_SOURCE_DIR}/plugins/*")
foreach(_dir IN LISTS _ge_plugin_dirs)
  get_filename_component(_name ${_dir} NAME)
  if(IS_DIRECTORY "${_dir}/app" AND NOT "${_name}_app" IN_LIST _ge_published_apps)
    list(APPEND _ge_app_excludes REGEX "/plugins/${_name}/app$" EXCLUDE)
  endif()
endforeach()

set(_ge_targets "")
while(GE_EXPORTED_TARGETS)
  list(POP_FRONT GE_EXPORTED_TARGETS _target _export_name)
  set_target_properties(${_target} PROPERTIES EXPORT_NAME ${_export_name})
  list(APPEND _ge_targets ${_target})
  add_library(ge::${_export_name} ALIAS ${_target})  # same spelling under FetchContent
endwhile()

target_include_directories(ge_core INTERFACE $<INSTALL_INTERFACE:${GE_INSTALL_INCLUDEDIR}>)

install(TARGETS ${_ge_targets}
  EXPORT gameengine2Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# engine/platform, engine/gpu and engine/render (built on gpu) are not published yet: a
# consumer would need SDL3 and Dawn from the package too, which the store builds (M11) and the ge CLI (M12) decide. The release
# and packaging workflows build with -DGE_PLATFORM=OFF -DGE_GPU=OFF.
install(DIRECTORY "${PROJECT_SOURCE_DIR}/engine/"
  DESTINATION ${GE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl"
  PATTERN "gpu" EXCLUDE PATTERN "platform" EXCLUDE PATTERN "render" EXCLUDE
  PATTERN "cli" EXCLUDE PATTERN "labs" EXCLUDE PATTERN "os_socket.hpp" EXCLUDE
  PATTERN "session_wire.hpp" EXCLUDE PATTERN "reach_wire.hpp" EXCLUDE PATTERN "portmap_wire.hpp" EXCLUDE
  PATTERN "replicate_wire.hpp" EXCLUDE PATTERN "to_view.hpp" EXCLUDE)

# A plugin's headers install beside the engine's (a plugin's name never equals an engine
# directory), so "racing/tape.hpp" is spelled the same in-tree and installed. Labs are not
# published; a presentation half's headers install only when the half is published (above).
install(DIRECTORY "${PROJECT_SOURCE_DIR}/plugins/"
  DESTINATION ${GE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl"
  PATTERN "labs" EXCLUDE ${_ge_app_excludes})

install(EXPORT gameengine2Targets
  NAMESPACE ge::
  DESTINATION ${GE_INSTALL_CMAKEDIR})

configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/gameengine2Config.cmake.in"
  "${PROJECT_BINARY_DIR}/gameengine2Config.cmake"
  INSTALL_DESTINATION ${GE_INSTALL_CMAKEDIR})

# 0.x: a minor bump may break (tools/release.py), so 0.x matches only its own
# minor; from 1.0 on, any later version of the same major satisfies a request.
if(GE_PACKAGE_VERSION VERSION_LESS 1.0.0)
  set(_ge_compat SameMinorVersion)
else()
  set(_ge_compat SameMajorVersion)
endif()
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/gameengine2ConfigVersion.cmake"
  VERSION ${GE_PACKAGE_VERSION}
  COMPATIBILITY ${_ge_compat})

install(FILES
  "${PROJECT_BINARY_DIR}/gameengine2Config.cmake"
  "${PROJECT_BINARY_DIR}/gameengine2ConfigVersion.cmake"
  DESTINATION ${GE_INSTALL_CMAKEDIR})

# The built-in UI font is compiled into ge::ui; its licence travels with the package.
install(FILES "${PROJECT_SOURCE_DIR}/engine/ui/fonts/OFL.txt" DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/gameengine2 RENAME FONT-OFL.txt)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/gameengine2 OPTIONAL)
