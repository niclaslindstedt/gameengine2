// LAB `texgen-layers`: a recipe built up ONE LAYER AT A TIME, lit after each, so what every
// layer adds is a picture beside the one before it, and a number: how far the layer moved the
// colour and the height (mean change, 0..255). A layer that moves nothing is a layer to cut or
// to turn up.
//
// ge:summary Lab: a recipe built layer by layer, each step lit, with what each layer changed
// ge:keywords lab texgen texture layers before-after steps build-up delta strip
// ge:skill textures labs
// ge:test tests/texgen_bake_test.cpp
#include <cmath>
#include <cstdlib>
#include <string>

#include "core/num_text.hpp"
#include "lab/lab.hpp"
#include "texgen/bake.hpp"
#include "texgen/layers.hpp"
#include "texgen/library.hpp"
#include "texgen/preview.hpp"

namespace {

using namespace ge;

double mean_change(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int channels) {
  double sum = 0;
  size_t n = 0;
  for (size_t i = 0; i < a.size(); i += 4) {
    for (int c = 0; c < channels; ++c, ++n) sum += std::abs(int(a[i + c]) - int(b[i + c]));
  }
  return n ? sum / static_cast<double>(n) : 0;
}

void run(lab::Lab& lab) {
  const int size = static_cast<int>(lab.arg_num("size"));
  const int panel = static_cast<int>(lab.arg_num("panel"));
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
    const int n = static_cast<int>(recipe.layers.size());
    const int gap = 8, label = 24;
    lab::Picture sheet =
        lab::create_picture(n * panel + (n + 1) * gap, panel + label + 2 * gap, {24, 24, 26});
    texgen::Maps before;
    lab::LabTable table{name, {"step", "layer", "colour-change", "height-change"}, {}};
    for (int k = 1; k <= n; ++k) {
      std::vector<texgen::Diagnostic> errors;
      const texgen::Maps maps =
          texgen::derive_maps(texgen::paint(recipe, {}, errors, k), recipe.bump);
      const char* kind = texgen::layer_kinds()[recipe.layers[k - 1].kind].name;
      double dc = 0, dh = 0;
      if (k > 1) {
        dc = mean_change(before.rgba[0], maps.rgba[0], 3);
        dh = mean_change(before.rgba[4], maps.rgba[4], 1);
      }
      const std::string key = name + ".layer-" + std::to_string(k) + "-" + kind;
      lab.metric(key + ".colour", dc, "level");
      lab.metric(key + ".height", dh, "level");
      table.rows.push_back(
          {std::to_string(k), kind, core::num::to_fixed(dc, 2), core::num::to_fixed(dh, 2)});
      const int x = gap + (k - 1) * (panel + gap);
      lab::draw_text(sheet, x, gap, std::to_string(k) + " " + kind, {220, 220, 220});
      if (k > 1)
        lab::draw_text(sheet, x, gap + 10, "D " + core::num::to_fixed(dc, 1), {150, 150, 150});
      lab::blit(sheet, texgen::lit_preview(maps, panel), x, gap + label);
      before = maps;
    }
    lab.table(table);
    lab.shot({name,
              "LAYERS OF " + name,
              "The recipe " + path + " at " + std::to_string(size) +
                  " px, lit after each of its layers in order (left to right): the step's number "
                  "and kind above it, and D, how far it moved the colour (mean, 0..255).",
              "each step adds what its kind says (joints, grime in them, worn edges, dust on "
              "ledges); the life layers follow the structure the pattern layers cut",
              {{"recipe", name}, {"size", std::to_string(size)}}},
             sheet);
  }
}

const lab::LabSpec kSpec{
    "texgen-layers",
    lab::LabKind::picture,
    "engine",
    "engine/texgen",
    "textures",
    "What does each layer of a recipe add, step by step?",
    {
        {"library", texgen::kLibraryDir, "the directory of .tex recipes"},
        {"recipes", "brick_wall,cave_rock,metal_panels,lava_rock", "recipe names, comma-separated"},
        {"size", "256", "the bake's size, px"},
        {"panel", "192", "each step's side on the sheet, px"},
    },
    "--size=64 --panel=64 --recipes=brick_wall",
    run,
};
GE_LAB(kSpec);

}  // namespace
