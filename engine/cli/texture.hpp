// `ge texture`: procedural texture recipes from the command line (engine/texgen,
// docs/textures.md).
//
//   ge texture kinds                    every layer kind, its keys, their defaults and ranges
//   ge texture check <file.tex>...      parse each recipe; every mistake a finding at its line
//   ge texture bake <file.tex> [--ref="seed=12 wear=0.8"] [--size=N] [--out=<dir>]
//                                       bake one variant into the cache directory (default
//                                       build/textures): the maps as PNG and the .meta; a
//                                       variant already there is reused, not baked again
//
// ge:summary ge texture: list layer kinds, check recipes, bake a variant to PNG in a cache
// ge:keywords cli ge texture texgen recipe check bake kinds cache png reference
// ge:skill agent-interface textures
// ge:test tests/cli_texture_test.cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cli/outcome.hpp"

namespace ge::cli {

Outcome run_texture_kinds();
Outcome run_texture_check(const std::filesystem::path& repo, const std::vector<std::string>& files);
Outcome run_texture_bake(const std::filesystem::path& repo, const std::string& file,
                         const std::string& ref, const std::string& size, const std::string& out);

}  // namespace ge::cli
