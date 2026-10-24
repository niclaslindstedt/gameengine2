// Recipe files: read whole, parsed, and a directory listed in name order.
//
// ge:summary Reads recipe files and lists `.tex` files in a directory, sorted
// ge:keywords library load_recipe list_recipes file directory
// ge:skill textures
// ge:test tests/texgen_recipe_test.cpp
#include "texgen/library.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ge::texgen {

ParseResult load_recipe(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ParseResult r;
    r.errors.push_back({0, "could not read `" + path + "`; check the path (recipes end in .tex)."});
    return r;
  }
  std::stringstream text;
  text << in.rdbuf();
  return parse_recipe(text.str());
}

std::vector<std::string> list_recipes(const std::string& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (e.is_regular_file() && e.path().extension() == ".tex") out.push_back(e.path().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace ge::texgen
