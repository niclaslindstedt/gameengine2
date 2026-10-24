# THE ge COMMAND LINE's build (engine/cli; skill: agent-interface). Included by the root
# CMakeLists.txt in every build, top-level or pulled in by a game with FetchContent, because a
# game repo builds the `ge` of the engine it pins and runs it (`make agents-check` there is
# `ge doctor`). Targets:
#
#   ge_cli   the commands as a library (the tests link it)
#   ge       the executable, written to the top-level build directory (build/<preset>/ge in
#            this tree and in a game's) and installed to bin/ with the package
#
# The consumer skills (consumer-skills/<name>/SKILL.md), the game scaffold (scaffold/**), the
# piece templates `ge add` writes (templates/**), the plugins' CMakeLists.txt (`ge use` reads
# their names and links), the model builders' library (engine/assets/blender/ge_asset.py, `ge asset`
# writes it beside a build) and the VERSION file are compiled into ge (cmake/EmbedFiles.cmake), so a ge carries the skills and the
# scaffold of exactly the engine it was built from and reads nothing from disk to write them.
include_guard(GLOBAL)
include(GNUInstallDirs)

option(GE_CLI "Build the ge command line (engine/cli)" ON)

function(ge_add_cli)
  if(NOT GE_CLI)
    return()
  endif()
  set(root "${PROJECT_SOURCE_DIR}")
  file(GLOB_RECURSE embedded CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${root}/consumer-skills/*/SKILL.md" "${root}/scaffold/*" "${root}/templates/*"
    "${root}/plugins/*/CMakeLists.txt" "${root}/engine/assets/blender/*.py")
  set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedFiles.cmake")
  set(gen "${PROJECT_BINARY_DIR}/gen/cli/embedded_files.cpp")
  add_custom_command(OUTPUT "${gen}"
    COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DOUT=${gen} -P "${script}"
    DEPENDS ${embedded} "${root}/VERSION" "${script}"
    COMMENT "ge: embedding the consumer skills and the scaffold"
    VERBATIM)

  file(GLOB sources CONFIGURE_DEPENDS "${root}/engine/cli/*.cpp")
  list(FILTER sources EXCLUDE REGEX "/main\\.cpp$")
  add_library(ge_cli STATIC ${sources} "${gen}")
  target_include_directories(ge_cli PUBLIC "$<BUILD_INTERFACE:${root}/engine>")
  # ge_lab: `ge graph` draws with the labs' chart kit (engine/lab/chart.hpp). ge_assets: `ge asset`
  # reads the asset manifests (engine/assets/manifest.hpp). ge_net: `ge net` reads captures and
  # runs fake traffic with the engine's own network (engine/net). ge_save: `ge save inspect` reads
  # save files (engine/save/file.hpp).
  target_link_libraries(ge_cli PUBLIC ge_core ge_play ge_lab ge_assets ge_net ge_save)
  # The tree this ge was built from: `ge find` and `ge explain` read the engine's tags there when
  # no other engine tree is found (engine/cli/index.hpp).
  target_compile_definitions(ge_cli PRIVATE GE_ENGINE_SOURCE_DIR="${root}")

  add_executable(ge "${root}/engine/cli/main.cpp")
  target_link_libraries(ge PRIVATE ge_cli)
  set_target_properties(ge PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  install(TARGETS ge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()
