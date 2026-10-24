// `ge snow`: a saved snow layer (engine/snow/snapshot.hpp, `snow::save`) read without its game.
//
//   ge snow show <file.snow> [--out=<dir>]   the layer's region, cell, tiles and memory, the
//       samples pressed and thrown onto, the deepest drop, the densest snow and the mass held;
//       and three PNGs over the tiles' extent: the depth, the density, and the samples' state
//       (pressed blue, berm amber, untouched grey)
//   ge snow column <file.snow> --at=<x,z>    the column there: depth, density, packed share, kind,
//       and whether a press changed it
//
// A snapshot carries only the tiles, so untouched snow reads as none here: what `show` pictures
// is what was pressed. The answer's `result` is JSON for an agent.
//
// ge:summary ge snow: a saved snow layer's stats, maps and a column, read without the game
// ge:keywords cli ge snow show column snapshot gesnow depth density tiles memory pressed berm
// ge:keywords run_snow_show run_snow_column
// ge:skill agent-interface snow
// ge:test tests/cli_snow_test.cpp
#pragma once

#include <string>

#include "cli/outcome.hpp"

namespace ge::cli {

Outcome run_snow_show(const std::string& file, const std::string& out_dir);
Outcome run_snow_column(const std::string& file, const std::string& at);

}  // namespace ge::cli
