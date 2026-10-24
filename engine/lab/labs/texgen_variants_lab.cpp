// LAB `texgen-variants`: one recipe as the rooms of a level would name it. The top row is the
// same recipe under four seeds (four rooms, four walls: each its own, none the same); the rows
// under it turn each of its params from its low end to its high end (a room's age: clean to
// worn, dry to wet). This is how a texture "comes alive" with the map: one recipe, many stable
// variants, each a texture reference a level stores.
//
// ge:summary Lab: a recipe under several seeds and each param swept, as a level's rooms name it
// ge:keywords lab texgen texture variants seeds params sweep reference room level age wear
// ge:skill textures labs
// ge:test tests/texgen_bake_test.cpp
#include <string>

#include "core/num.hpp"
#include "core/num_text.hpp"
#include "lab/lab.hpp"
#include "texgen/bake.hpp"
#include "texgen/library.hpp"
#include "texgen/preview.hpp"

namespace {

using namespace ge;

void run(lab::Lab& lab) {
  const int size = static_cast<int>(lab.arg_num("size"));
  const int panel = static_cast<int>(lab.arg_num("panel"));
  const std::vector<double> seeds = lab.arg_nums("seeds");
  const int steps = 4;
  const std::string& list = lab.arg("recipes");
  size_t at = 0;
  while (at <= list.size()) {
    size_t end = list.find(',', at);
    if (end == std::string::npos) end = list.size();
    const std::string name = list.substr(at, end - at);
    at = end + 1;
    if (name.empty()) continue;
    const std::string path = lab.arg("library") + "/" + name + ".tex";
    texgen::ParseResult parsed = texgen::load_recipe(path);
    if (!parsed.ok()) {
      lab.fail(texgen::format_diagnostics(parsed.errors, path),
               "Fix the recipe or the --recipes list. Skill: textures.");
      continue;
    }
    texgen::Recipe& recipe = parsed.recipe;
    recipe.size = size;
    const int rows = 1 + static_cast<int>(recipe.params.size());
    const int cols = std::max(static_cast<int>(seeds.size()), steps);
    const int gap = 8, label = 14;
    lab::Picture sheet = lab::create_picture(cols * panel + (cols + 1) * gap,
                                             rows * (panel + label + gap) + gap, {24, 24, 26});
    auto put = [&](int row, int col, const texgen::TextureRef& ref) {
      const texgen::BakeResult b = texgen::bake(recipe, ref);
      const int x = gap + col * (panel + gap), y = gap + row * (panel + label + gap);
      lab::draw_text(sheet, x, y, texgen::ref_text(ref), {220, 220, 220});
      lab::blit(sheet, texgen::lit_preview(b.maps, panel), x, y + label);
      lab.metric(name + "." + texgen::ref_text(ref).substr(name.size() + 1),
                 static_cast<double>(texgen::maps_digest(b.maps) % 1000000007ull), "digest");
    };
    for (size_t c = 0; c < seeds.size(); ++c) {
      texgen::TextureRef ref{recipe.name, static_cast<uint32_t>(seeds[c]), {}};
      put(0, static_cast<int>(c), ref);
    }
    for (size_t p = 0; p < recipe.params.size(); ++p) {
      const texgen::Param& param = recipe.params[p];
      for (int s = 0; s < steps; ++s) {
        const double v =
            core::num::round((param.lo + (param.hi - param.lo) * s / (steps - 1)) * 100) / 100;
        texgen::TextureRef ref{recipe.name, std::nullopt, {{param.name, v}}};
        put(1 + static_cast<int>(p), s, ref);
      }
    }
    lab.shot({name,
              "VARIANTS OF " + name,
              "The recipe " + path + " at " + std::to_string(size) +
                  " px, lit. Top row: the texture reference with four seeds (a room's own "
                  "seed). Each row under it: one param from its low end to its high end, the "
                  "seed left at the recipe's. Each panel is labelled with its reference.",
              "the seeds differ in detail but are the same material; each param's row changes "
              "smoothly and in the way its name says",
              {{"recipe", name}, {"size", std::to_string(size)}, {"seeds", lab.arg("seeds")}}},
             sheet);
  }
}

const lab::LabSpec kSpec{
    "texgen-variants",
    lab::LabKind::picture,
    "engine",
    "engine/texgen",
    "textures",
    "How does one recipe vary with a room's seed and its params?",
    {
        {"library", texgen::kLibraryDir, "the directory of .tex recipes"},
        {"recipes", "brick_wall,cave_rock,snowy_stone,metal_panels",
         "recipe names, comma-separated"},
        {"seeds", "1,2,3,4", "the references' seeds"},
        {"size", "256", "the bake's size, px"},
        {"panel", "192", "each panel's side, px"},
    },
    "--size=64 --panel=64 --recipes=brick_wall --seeds=1,2",
    run,
};
GE_LAB(kSpec);

}  // namespace
