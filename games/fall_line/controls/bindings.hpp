// FALL LINE'S CONTROLS: the actions a person plays the game with and the bindings the game ships,
// as a bindings file (engine/input/bindings_text.hpp) an agent can read and a player can edit.
//
// THE ACTIONS. The `ride` context holds the play contract's axes by name (sim/play.hpp: steer,
// tuck, brake, jump, reset), so `ge::play::bind_axes` pairs them with no table between. The `app`
// context holds what the window does with the run (the camera key, the rebinding walk), which no
// input log records.
//
// THE DEFAULTS (`kDefaultBindings`): every action on the keys (A and D or the arrows steer, ramped
// in so a tap is a small turn; W tucks, S brakes, SPACE jumps, R resets, C the camera, F5 rebinds),
// on a gamepad (the left stick steers, the right trigger tucks, the left brakes, south jumps,
// north resets, back the camera) and on a wheel (its steer, throttle and brake roles: the gas
// pedal is the tuck). A keyboard, a pad and a wheel all play at once with no switch.
//
// THE PLAYER'S FILE is the defaults until the player rebinds (rebinding.hpp): `load_player_map`
// reads it when it is there, and `save_player_map` writes it back. A file that does not read
// leaves the defaults in force and says which line is wrong.
//
// ge:summary Fall Line's actions and default bindings; the player's bindings file read and saved
// ge:keywords fall-line controls actions bindings defaults keys gamepad wheel ride app camera
// ge:keywords rebind player-file load_player_map save_player_map default_map default-bindings
// ge:skill input
// ge:test tests/fall_line_controls_test.cpp
#pragma once

#include <string>
#include <string_view>

#include "input/actions.hpp"

namespace fall_line::controls {

// The contexts.
inline constexpr std::string_view kRide = "ride";
inline constexpr std::string_view kApp = "app";

extern const char* const kDefaultBindings;

// The game's actions bound by `kDefaultBindings`.
ge::input::ActionMap default_map();

// `map` with the player's file at `path` read over it. False when the file is there but does not
// read: `map` keeps the defaults and `error` names the line. A file that is not there is no error.
bool load_player_map(const std::string& path, ge::input::ActionMap& map, std::string& error);
// `map` written to `path` (its directory made when missing). False with `error` when it cannot be.
bool save_player_map(const std::string& path, const ge::input::ActionMap& map, std::string& error);

}  // namespace fall_line::controls
