// RECIPES ON DISK: reading a `.tex` file, and listing a directory of them. The engine's own
// specimen set is engine/texgen/library/ (a brick wall, cave rock, concrete, planks, panels,
// rust, a lit tech panel, lava, snowy stone, flagstones, tiles, hazard stripes): the labs and
// tests bake every one, and a game keeps its own beside its levels.
//
// ge:summary Reads recipe files and lists a directory of them; the engine's specimen library
// ge:keywords library recipe file load list directory tex specimens load_recipe list_recipes
// ge:skill textures
// ge:test tests/texgen_recipe_test.cpp
#pragma once

#include <string>
#include <vector>

#include "texgen/recipe.hpp"

namespace ge::texgen {

// The repository path of the engine's specimen recipes, relative to the tree's root.
inline constexpr const char* kLibraryDir = "engine/texgen/library";

// `path` read and parsed; a file that cannot be read is one diagnostic saying so.
ParseResult load_recipe(const std::string& path);

// Every `*.tex` in `dir`, sorted by name; empty when the directory is missing.
std::vector<std::string> list_recipes(const std::string& dir);

}  // namespace ge::texgen
