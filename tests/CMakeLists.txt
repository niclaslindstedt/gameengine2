# Every tests/<topic>_test.cpp becomes its own executable and CTest case.
# New test file = new topic; nothing to register by hand.
file(GLOB GE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
# Every test links the engine, every plugin and every game's simulation, so a plugin's tests
# (tests/<plugin>_*_test.cpp) need no wiring either.
get_property(GE_PLUGIN_TARGETS GLOBAL PROPERTY GE_PLUGIN_TARGETS)
get_property(GE_GAME_TARGETS GLOBAL PROPERTY GE_GAME_TARGETS)
# tests/cli_*_test.cpp need GE_CLI and link ge_cli (the ge command line).
# tests/gpu_*_test.cpp need GE_GPU and link ge_gpu (tests/render_*_test.cpp link ge_render);
# tests/platform_*_test.cpp need GE_PLATFORM and GE_GPU (a window is only worth opening to present to). Either kind exits 77 (CTest's
# SKIP) when the machine cannot run it (no adapter, no display), except where the environment
# sets GE_GPU_REQUIRE=1 (CI's Linux x64 rows, with lavapipe and a virtual display): there a
# skip is a failure.
# A test that includes a plugin's presentation half ("<plugin>/app/...") is built only when that
# half is (cmake/Plugins.cmake: mountain's needs the GPU); one that includes a game's
# ("<game>/app/...") links the game's `<game>_app` and is built only when that is.
# The runner every test executable shares, compiled once (and linted once) rather than per test.
add_library(ge_test_main OBJECT main.cpp)
target_link_libraries(ge_test_main PRIVATE ge_core)
target_compile_definitions(ge_test_main PRIVATE GE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
foreach(src ${GE_TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  set(extra)
  set(cli)
  if(name MATCHES "^gpu_")
    if(NOT GE_GPU)
      continue()
    endif()
    set(extra ge_gpu)
  elseif(name MATCHES "^render_")
    if(NOT GE_GPU)
      continue()
    endif()
    set(extra ge_render)
  elseif(name MATCHES "^cli_")
    if(NOT GE_CLI)
      continue()
    endif()
    set(cli ge_cli)
  elseif(name MATCHES "^platform_")
    if(NOT GE_PLATFORM OR NOT GE_GPU)
      continue()
    endif()
    set(extra ge_platform ge_gpu)
  endif()
  file(STRINGS ${src} app_includes REGEX "^#include \"[a-z0-9]+/app/")
  set(missing_app FALSE)
  foreach(line IN LISTS app_includes)
    string(REGEX REPLACE "^#include \"([a-z0-9]+)/app/.*" "\\1" plugin "${line}")
    if(NOT TARGET ge_${plugin}_app)
      set(missing_app TRUE)
    endif()
  endforeach()
  if(missing_app)
    continue()
  endif()
  # A test that includes a game's presentation half ("<game>/app/...") links it, and is built
  # only when it is (with the GPU).
  file(STRINGS ${src} game_app_includes REGEX "^#include \"[a-z0-9_]+/app/")
  foreach(line IN LISTS game_app_includes)
    string(REGEX REPLACE "^#include \"([a-z0-9_]+)/app/.*" "\\1" game "${line}")
    if(TARGET ${game}_app)
      list(APPEND extra ${game}_app)
    elseif(NOT TARGET ge_${game}_app)
      set(missing_app TRUE)
    endif()
  endforeach()
  if(missing_app)
    continue()
  endif()
  add_executable(${name} ${src} $<TARGET_OBJECTS:ge_test_main>)
  target_link_libraries(${name} PRIVATE ge_core ge_audio ge_assets ge_net ge_input ge_wind ge_water ge_move ge_camera ge_ui ge_save ge_lab ge_play ge_replay ${GE_PLUGIN_TARGETS} ${GE_GAME_TARGETS}
    ${extra} ${cli})
  target_compile_definitions(${name} PRIVATE GE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME ${name} COMMAND ${name})
  if(name MATCHES "^mountain_")
    # The generator's tests build whole maps and are the slowest cases: start them early.
    set_tests_properties(${name} PROPERTIES COST 50)
  endif()
  if(cli)
    # A machine that cannot make symlinks (Windows without Developer Mode) cannot hold a game
    # repo's guidance; the scaffold's tests skip there.
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(extra OR app_includes)
    # A plugin's app half draws with the GPU, so its tests skip without an adapter too. The
    # software driver's worker threads keep allocations past exit, which the leak check
    # would report as ours; every other test keeps it.
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
  endif()
endforeach()

# Every committed model against its stamp and its size budget, and loaded (engine/assets/
# manifest.hpp): a model older than its sources or over its budget fails here.
add_test(NAME assets_check COMMAND assets check --root=${PROJECT_SOURCE_DIR})

# Every lab at its smoke flags, twice in one process (a digest that moves between the two runs
# is a determinism break), and each digest against the committed baseline, which the same code
# must reproduce on every compiler row (docs/labs.md § Digests; `make labs-digests` rewrites it).
# The labs are cut into GE_LABS_SHARDS slices (labs_smoke_1 ... labs_smoke_N, every Nth lab
# each), so a parallel ctest runs them side by side; together they run every lab once.
set(GE_LABS_SHARDS 8 CACHE STRING "How many CTest cases the labs' smoke pass is split into")
foreach(i RANGE 1 ${GE_LABS_SHARDS})
  set(name labs_smoke_${i})
  add_test(NAME ${name} COMMAND labs --all --smoke --repeat=2 --quiet --shard=${i}/${GE_LABS_SHARDS}
    --out=${PROJECT_BINARY_DIR}/previews
    --check-digests=${PROJECT_SOURCE_DIR}/tests/labs_digests.txt)
  # From the repository's root, where a lab finds its golden images (gpu-capture). With the GPU
  # in, the software driver's worker threads keep allocations past exit (as in the gpu_ tests).
  # COST: start the slices first, so a parallel ctest is not left waiting on one at the end.
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COST 100)
  if(GE_GPU)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
  endif()
endforeach()
