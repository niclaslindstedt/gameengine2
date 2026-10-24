// FALL LINE ON THE PLAY CONTRACT (engine/play/game.hpp): the game as an agent plays it through
// `ge play`, `ge sim`, `ge replay`, `ge state` and `ge diverge`. A run is a `Driver`
// (simulate.hpp) on the race map built for the seed, so an agent's run is hashed and reported
// exactly as the bot's is; the engine's session snaps, records and replays it.
//
// - THE AXES: `steer` (-1 left .. 1 right), `tuck` and `brake` (0..1), `jump` (held: the longer
//   the higher), `reset` (pressed: stand back on the piste past the last gate taken).
// - THE OPTIONS: `rivals` (bots on the grid beside the player) and `version` (the generator's).
// - `observe`: the clock, where the skier is and how fast, the gate owed (its bearing off his
//   heading, + right, and its distance), where he stands on the piste and how it bends ahead, his
//   place, and the events since the last observation (his own, and any shoulder he was in).
// - `state`: every racer's skier and progress, to the bit, for `ge diverge`.
// - `pilot`: the bot's controls for the player.
// - `open_run_on`: a run on a map the caller built (the app's, under the sky its cvars picked),
//   which `ge replay` cannot rebuild from the log's seed alone.
// - `state_of`: the GameState behind a Run this spec opened, for the game's GPU renderer
//   (app/play_render.hpp), which draws a run the play contract steps.
// - `trace`: the bot's decisions (bot.hpp), the player's pilot as agent 0 and each rival as his
//   racer index, for `ge why`.
//
// ge:summary Fall Line on the play contract: its axes, options, observation, state, report, pilot
// ge:keywords play agent observe observation state report pilot axes options game-spec run
// ge:keywords fall_line_game open_run state_of trace why
// ge:skill sim agent-interface
// ge:test tests/fall_line_play_test.cpp
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "mountain/mapgen/types.hpp"
#include "play/game.hpp"

namespace fall_line::sim {

struct GameState;

// The most events one observation carries; older ones are dropped and counted.
inline constexpr size_t kMostEvents = 256;
// How far down the piste the observation's `piste_bend` looks, m.
inline constexpr double kBendAhead = 30;

// The game's spec: `GE_GAME(fall_line::sim::kGame)` in the play executable registers it.
extern const ge::play::GameSpec kGame;

// A run of `setup`'s field on `level` in place of the seed's race map. Null with `error` when the
// setup's rivals or hands are out of range.
std::unique_ptr<ge::play::Run> open_run_on(std::shared_ptr<const ge::mountain::mapgen::Level> level,
                                           const ge::play::Setup& setup, std::string& error);

// The state of a run `kGame.open` (or `open_run_on`) made, or null for any other Run.
const GameState* state_of(const ge::play::Run& run);

}  // namespace fall_line::sim
