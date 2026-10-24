// THE KNOBS AS CVARS: every row of `knobs()` bound to a game's console under an owner prefix, so
// a cfg file, the console and a server retune a moving character without a rebuild. A bound cvar
// writes straight into the `MoveSpec` the step reads, so nothing is looked up during a step.
//
// THE TIER is the caller's flags. A server's rules (the default, `kServerKnobs`) carry `kServer`
// and `kServerInfo`: only the server's own console or cfg sets them, every client gets their
// values in the server info and follows them (`Cvars::apply_info`), and a client's own `set` of
// one is refused with the reason, so every machine steps the same row. A single-player game
// that lets the player tune the feel binds them `kArchive` instead.
//
// THE DEFAULTS are the spec's values when it is bound (a preset's row, or the game's own), so
// `reset` returns a knob to the row the game started from, and binding changes no bit of it.
// The help line ends with the knob's unit, which `ge cvar ls` prints beside the value.
//
// ge:summary Character movement's knobs bound as console cvars under an owner prefix (sv_gravity)
// ge:keywords move movement cvars cvar console bind bind_knobs server-info sv_ sv_gravity
// ge:keywords sv_airaccelerate sv_maxspeed cfg tier kserver apply_cfg server-cfg
// ge:skill move console
// ge:test tests/move_cvars_test.cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "console/cvar.hpp"
#include "move/spec.hpp"

namespace ge::move {

// The server's rules: set only on the server, sent to every client.
inline constexpr uint32_t kServerKnobs = console::kServer | console::kServerInfo;
// The owner prefix a server's rules take (docs/console.md § Names).
inline constexpr std::string_view kServerPrefix = "sv_";

// Every knob of `spec` bound in `cvars` as `prefix` + its name (`sv_` + `gravity`), with
// `flags`, its kind, range, help and unit, its default the value `spec` holds now. `spec` must
// outlive the cvars. Returns how many were bound.
int bind_knobs(console::Cvars& cvars, MoveSpec& spec, std::string_view prefix = kServerPrefix,
               uint32_t flags = kServerKnobs);

// A cfg file run on `spec` as a server's console would run it: every knob bound under
// `prefix`, the file exec'd, its commands run. False, with each problem (file:line, what and the
// next action) in `error`, when the file is missing or a line was refused; the lines before a
// refused one stay applied. What a lab's `--cfg` and a tool read a server's row with.
bool apply_cfg(MoveSpec& spec, std::string_view path, std::string& error,
               std::string_view prefix = kServerPrefix);

}  // namespace ge::move
