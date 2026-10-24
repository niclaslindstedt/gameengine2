// Fall Line's `play` executable: the game registered on the play contract, and the engine's
// play command line (engine/play/main.hpp) as its main. `ge play`, `ge sim`, `ge replay`,
// `ge state` and `ge diverge` run it; `build/debug/play describe` lists its axes and options.
// Built with the GPU, it registers the game's renderer too (app/play_render.hpp), which `ge shot
// --gpu` and `ge record --gpu` draw with. It registers the game's sound (sound/play_sound.hpp),
// which `ge record --format=mkv` writes beside the pictures.
//
// ge:summary Fall Line's play executable: GE_GAME, GE_SOUND, GE_RENDERER and play_main
// ge:keywords play executable main register ge_game ge_renderer play_main
// ge:skill sim agent-interface
// ge:test tests/fall_line_play_test.cpp
#include "play/main.hpp"
#include "fall_line/sim/play.hpp"
#include "fall_line/sound/play_sound.hpp"
#ifdef FALL_LINE_PLAY_GPU
#include "fall_line/app/play_render.hpp"
#endif

GE_GAME(fall_line::sim::kGame);
GE_SOUND(fall_line::sound::kSound);
#ifdef FALL_LINE_PLAY_GPU
GE_RENDERER(fall_line::app::kRenderer);
#endif

int main(int argc, char** argv) { return ge::play::play_main(argc, argv); }
