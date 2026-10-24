# Fall Line, the engine's demo game. Its simulation (sim/) is one static library the labs and
# the tests link: create_game, the step's order, the course, the bot and simulate (skill: sim).
# A game is the demo, never part of the published engine.
file(GLOB_RECURSE FALL_LINE_SIM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sim/*.cpp)
add_library(fall_line_sim STATIC ${FALL_LINE_SIM_SOURCES})
target_include_directories(fall_line_sim PUBLIC ${PROJECT_SOURCE_DIR}/games)
target_link_libraries(fall_line_sim PUBLIC fall_line_cfg ge_core ge_prof ge_play ge_replay ge_racing ge_mountain ge_skier ge_snowmobile ge_helicopter ge_paramotor)
set_property(GLOBAL APPEND PROPERTY GE_GAME_TARGETS fall_line_sim)

# Its cvars (cfg/): every setting a cfg may change, bound to one Settings struct the app reads and
# the play executable registers (skill: console). No GPU.
file(GLOB FALL_LINE_CFG_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cfg/*.cpp)
add_library(fall_line_cfg STATIC ${FALL_LINE_CFG_SOURCES})
target_include_directories(fall_line_cfg PUBLIC ${PROJECT_SOURCE_DIR}/games)
target_link_libraries(fall_line_cfg PUBLIC ge_console ge_camera)
set_property(GLOBAL APPEND PROPERTY GE_GAME_TARGETS fall_line_cfg)

# Its `play` executable (play/): the game on the play contract, which `ge play`, `ge sim`,
# `ge replay`, `ge state` and `ge diverge` build and run (skill: agent-interface).
# It lands at the build's root (build/<preset>/play), where ge looks for a game's play target.
add_executable(play play/main.cpp)
target_link_libraries(play PRIVATE fall_line_sim fall_line_cfg)
set_target_properties(play PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# Its sound (sound/): the bank, the event route, the listener and the faders, over the skier's
# beds. No GPU and no device, so the tests and the `audition` lab link it headless (skill: audio).
file(GLOB FALL_LINE_SOUND_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sound/*.cpp)
add_library(fall_line_sound STATIC ${FALL_LINE_SOUND_SOURCES})
target_link_libraries(fall_line_sound PUBLIC fall_line_sim ge_skier_app ge_audio)
set_property(GLOBAL APPEND PROPERTY GE_GAME_TARGETS fall_line_sound)

# Its camera (camera/): the ladder of engine rigs a run is seen from, the skier as the camera's
# subject, the mountain's answers to it. No GPU, so the tests and the labs link it headless
# (skill: camera).
file(GLOB FALL_LINE_CAMERA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/camera/*.cpp)
add_library(fall_line_camera STATIC ${FALL_LINE_CAMERA_SOURCES})
target_link_libraries(fall_line_camera PUBLIC fall_line_sim ge_skier_app ge_camera)
set_property(GLOBAL APPEND PROPERTY GE_GAME_TARGETS fall_line_camera)

# Its app half: `fall_line_app`, the game's frame (its pass table over engine/render, the look it hands the
# passes, its named views, its forest and its skinned skiers), which the `fall_line` window and the game's GPU labs both link; it
# needs the GPU. Headers are spelled "fall_line/app/<file>.hpp".
if(GE_GPU)
  file(GLOB FALL_LINE_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp)
  list(FILTER FALL_LINE_APP_SOURCES EXCLUDE REGEX "/app/main\\.cpp$")
  add_library(fall_line_app STATIC ${FALL_LINE_APP_SOURCES})
  target_include_directories(fall_line_app PUBLIC ${PROJECT_SOURCE_DIR}/games)
  target_link_libraries(fall_line_app PUBLIC ge_render ge_prof ge_mountain_app ge_skier_app fall_line_sim fall_line_camera fall_line_cfg)
  # The skier model the racers are drawn as (scene.hpp), compiled in: the game reads no file.
  ge_embed_binary(fall_line_app fall_line_skier_glb
    ${PROJECT_SOURCE_DIR}/plugins/skier/assets/skier/skier.glb)
  set_property(GLOBAL APPEND PROPERTY GE_GAME_APPS fall_line_app)
  # The play executable draws the GPU's pictures too (`ge shot --gpu`, `ge record --gpu`).
  target_link_libraries(play PRIVATE fall_line_app)
  target_compile_definitions(play PRIVATE FALL_LINE_PLAY_GPU=1)
endif()
if(GE_PLATFORM AND GE_GPU)
  add_executable(fall_line app/main.cpp)
  target_link_libraries(fall_line PRIVATE ge_platform fall_line_app fall_line_sound fall_line_cfg)
endif()
