// `ge save`: save files read without the game (docs/saves.md).
//
//   ge save inspect <file>   the header, the meta a menu lists, and every chunk: its tag, its
//                            instance, its version, its offset, its bytes and its checksum;
//                            a damaged or newer file comes back as one finding naming the part
//
// No game and no build are needed: the file format is the engine's (engine/save/file.hpp).
//
// ge:summary ge save inspect: a save file's header, meta and chunks, or why it is refused
// ge:keywords cli ge save inspect file header meta chunk directory checksum damaged
// ge:skill agent-interface save
// ge:test tests/cli_save_test.cpp
#pragma once

#include <string>

#include "cli/commands.hpp"
#include "cli/outcome.hpp"

namespace ge::cli {

Outcome run_save_inspect(const std::string& file);
// A parsed `ge save <inspect>` line, run.
Outcome run_save(const Args& a);

}  // namespace ge::cli
