// LAB `texgen-cost`: what a bake COSTS. Every library recipe baked at each size, best of a few
// runs, on the worker pool; the time a level's load pays the first time it names a variant (a
// variant already on disk costs a file read). Time it on PRESET=release.
//
// ge:summary Lab: the bake's cost, every library recipe at each size, milliseconds
// ge:keywords lab texgen texture cost bench bake time milliseconds speed
// ge:skill textures perf labs
// ge:test tests/texgen_bake_test.cpp
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

#include "core/num_text.hpp"
#include "lab/lab.hpp"
#include "texgen/bake.hpp"
#include "texgen/layers.hpp"
#include "texgen/library.hpp"

namespace {

using namespace ge;

void run(lab::Lab& lab) {
  const std::vector<double> sizes = lab.arg_nums("sizes");
  const int repeat = std::max(1, static_cast<int>(lab.arg_num("repeat")));
  lab::LabTable table{"BAKE MS (BEST OF " + std::to_string(repeat) + ")", {"recipe"}, {}};
  for (double s : sizes) table.columns.push_back(std::to_string(static_cast<int>(s)) + " px");
  std::vector<double> totals(sizes.size(), 0);
  for (const std::string& path : texgen::list_recipes(lab.arg("library"))) {
    texgen::ParseResult parsed = texgen::load_recipe(path);
    if (!parsed.ok()) continue;
    const std::string name = std::filesystem::path(path).stem().string();
    std::vector<std::string> row{name};
    for (size_t k = 0; k < sizes.size(); ++k) {
      parsed.recipe.size = static_cast<int>(sizes[k]);
      double best = 1e18;
      for (int r = 0; r < repeat; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        const texgen::BakeResult b = texgen::bake(parsed.recipe, {});
        best = std::min(
            best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                      .count());
        if (b.maps.size == 0) lab.fail(name + " baked nothing", "Skill: textures.");
      }
      totals[k] += best;
      lab.metric(name + "." + std::to_string(static_cast<int>(sizes[k])) + "-ms", best, "ms",
                 lab::Det::measured);
      row.push_back(core::num::to_fixed(best, 1));
    }
    table.rows.push_back(row);
  }
  // Where the time goes: each layer kind's mean cost at the largest size, over every library use.
  const int big = static_cast<int>(sizes.empty() ? 512 : sizes.back());
  std::vector<double> kind_ms(texgen::layer_kinds().size(), 0);
  std::vector<int> kind_uses(texgen::layer_kinds().size(), 0);
  double derive_ms = 0;
  int recipes = 0;
  for (const std::string& path : texgen::list_recipes(lab.arg("library"))) {
    texgen::ParseResult parsed = texgen::load_recipe(path);
    if (!parsed.ok()) continue;
    parsed.recipe.size = big;
    std::vector<texgen::Diagnostic> errors;
    const std::vector<double> params = texgen::resolve_params(parsed.recipe, {}, errors);
    texgen::Surface s = texgen::make_surface(big);
    for (size_t k = 0; k < parsed.recipe.layers.size(); ++k) {
      const auto t0 = std::chrono::steady_clock::now();
      texgen::paint_layer(s, parsed.recipe, {}, params, static_cast<int>(k));
      const int kind = parsed.recipe.layers[k].kind;
      kind_ms[kind] +=
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      ++kind_uses[kind];
    }
    const auto t0 = std::chrono::steady_clock::now();
    texgen::derive_maps(s, parsed.recipe.bump);
    derive_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++recipes;
  }
  lab::LabTable kinds{
      "LAYER COST AT " + std::to_string(big) + " PX", {"kind", "uses", "mean ms"}, {}};
  for (size_t k = 0; k < kind_ms.size(); ++k) {
    if (!kind_uses[k]) continue;
    const double mean = kind_ms[k] / kind_uses[k];
    kinds.rows.push_back({texgen::layer_kinds()[k].name, std::to_string(kind_uses[k]),
                          core::num::to_fixed(mean, 1)});
    lab.metric(std::string("kind.") + texgen::layer_kinds()[k].name + "-ms", mean, "ms",
               lab::Det::measured);
  }
  if (recipes) {
    kinds.rows.push_back(
        {"(derive maps)", std::to_string(recipes), core::num::to_fixed(derive_ms / recipes, 1)});
  }
  lab.table(kinds, lab::Det::measured);
  for (size_t k = 0; k < sizes.size(); ++k)
    lab.metric("total." + std::to_string(static_cast<int>(sizes[k])) + "-ms", totals[k], "ms",
               lab::Det::measured);
  lab.table(table, lab::Det::measured);
}

const lab::LabSpec kSpec{
    "texgen-cost",
    lab::LabKind::bench,
    "engine",
    "engine/texgen",
    "textures",
    "How long does each library recipe take to bake at each size?",
    {
        {"library", texgen::kLibraryDir, "the directory of .tex recipes"},
        {"sizes", "256,512,1024", "bake sizes, px"},
        {"repeat", "3", "runs a size; the best is kept"},
    },
    "--sizes=64 --repeat=1",
    run,
};
GE_LAB(kSpec);

}  // namespace
