// LAB `texgen-specimens`: every recipe in a library baked and LOOKED AT. One sheet a recipe:
// the texture lit, the same lit four times over (a seam is a line through the middle), and the
// five maps it bakes (albedo, normal, ORM, height, glow), with the seam score that says in a
// number whether it tiles: the colour's step across the wrapped edge against its step anywhere
// else (1 is seamless; a seam reads several).
//
// ge:summary Lab: every library recipe baked, lit, tiled and its maps shown, with seam scores
// ge:keywords lab texgen texture specimens sheet tiling seam tileable maps lit preview library
// ge:skill textures labs
// ge:test tests/texgen_bake_test.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/num_text.hpp"
#include "lab/lab.hpp"
#include "texgen/bake.hpp"
#include "texgen/library.hpp"
#include "texgen/preview.hpp"

namespace {

using namespace ge;

// The colour's mean step across the wrapped edge, over the largest mean step across any other
// column or row boundary: a texture that tiles steps across its edge no more than across the
// joints and lines it draws inside, so a score over about 1.2 is a seam.
double seam_score(const texgen::Maps& m) {
  const auto& a = m.rgba[0];
  const int s = m.size;
  auto lum = [&](int x, int y) {
    const size_t o = (static_cast<size_t>(y) * s + x) * 4;
    return 0.3 * a[o] + 0.59 * a[o + 1] + 0.11 * a[o + 2];
  };
  double edge = 0, inner = 1e-9;
  for (int vertical = 0; vertical < 2; ++vertical) {
    for (int b = 0; b < s; ++b) {
      // Boundary b lies between column (or row) b - 1 and b, wrapped; b = 0 is the tile's edge.
      double step = 0;
      for (int k = 0; k < s; ++k) {
        const int p = (b + s - 1) % s;
        step += vertical ? std::fabs(lum(k, p) - lum(k, b)) : std::fabs(lum(p, k) - lum(b, k));
      }
      step /= s;
      if (b == 0)
        edge = std::max(edge, step);
      else
        inner = std::max(inner, step);
    }
  }
  return edge / inner;
}

void run(lab::Lab& lab) {
  const int size = static_cast<int>(lab.arg_num("size"));
  const int panel = static_cast<int>(lab.arg_num("panel"));
  const std::string only = "," + lab.arg("only") + ",";
  lab::LabTable table{"SPECIMENS", {"recipe", "surface", "layers", "glows", "seam", "digest"}, {}};
  int baked = 0;
  for (const std::string& path : texgen::list_recipes(lab.arg("library"))) {
    const std::string name = std::filesystem::path(path).stem().string();
    if (only != ",," && only.find("," + name + ",") == std::string::npos) continue;
    texgen::ParseResult parsed = texgen::load_recipe(path);
    if (!parsed.ok()) {
      lab.fail(texgen::format_diagnostics(parsed.errors, path),
               "Fix the recipe; `ge texture check " + path + "` repeats this. Skill: textures.");
      continue;
    }
    texgen::Recipe& recipe = parsed.recipe;
    recipe.size = size;
    const auto t0 = std::chrono::steady_clock::now();
    const texgen::BakeResult b = texgen::bake(recipe, {});
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double seam = seam_score(b.maps);
    ++baked;
    lab.metric(name + ".seam", seam, "ratio");
    lab.metric(name + ".bake-ms", ms, "ms", lab::Det::measured);
    if (seam > 1.2) {
      lab.fail(name + " shows a seam: its colour steps " + core::num::to_fixed(seam, 2) +
                   "x as far across the wrapped edge as inside",
               "A layer is not periodic: check that every period and stretch is a whole number "
               "and that the layer wraps its coordinates (engine/texgen/noise.hpp). Skill: "
               "textures.");
    }
    char digest[17];
    std::snprintf(digest, sizeof digest, "%016llx",
                  static_cast<unsigned long long>(texgen::maps_digest(b.maps)));
    table.rows.push_back({name, recipe.surface, std::to_string(recipe.layers.size()),
                          b.maps.glows ? "yes" : "no", core::num::to_fixed(seam, 3), digest});

    const int gap = 8, label = 14, cols = 7;
    lab::Picture sheet =
        lab::create_picture(cols * panel + (cols + 1) * gap, panel + label + 2 * gap, {24, 24, 26});
    const char* names[cols] = {"LIT", "LIT 2X2", "ALBEDO", "NORMAL", "ORM", "HEIGHT", "GLOW"};
    for (int c = 0; c < cols; ++c) {
      lab::Picture p;
      if (c == 0)
        p = texgen::lit_preview(b.maps, panel, 1);
      else if (c == 1)
        p = texgen::lit_preview(b.maps, panel, 2);
      else if (c == 2)
        p = texgen::map_picture(b.maps, texgen::MapKind::albedo, panel);
      else if (c == 3)
        p = texgen::map_picture(b.maps, texgen::MapKind::normal, panel);
      else if (c == 4)
        p = texgen::map_picture(b.maps, texgen::MapKind::orm, panel);
      else if (c == 5)
        p = texgen::map_picture(b.maps, texgen::MapKind::height, panel);
      else
        p = texgen::map_picture(b.maps, texgen::MapKind::glow, panel);
      const int x = gap + c * (panel + gap);
      lab::draw_text(sheet, x, gap, names[c], {220, 220, 220});
      lab::blit(sheet, p, x, gap + label);
    }
    std::string tags;
    for (const std::string& t : recipe.tags) {
      if (!tags.empty()) tags += ' ';
      tags += t;
    }
    std::string caption = "The recipe ";
    caption += path;
    caption += " baked at " + std::to_string(size) + " px (";
    caption += std::to_string(recipe.layers.size()) + " layers, surface ";
    caption += recipe.surface;
    caption += ", " + core::num::to_string(recipe.scale_m) + " m a tile, tags ";
    caption += tags;
    caption +=
        "). Lit by one light from the top left; then the maps: ORM is occlusion red, "
        "roughness green, metal blue. Seam score ";
    caption += core::num::to_fixed(seam, 2) + ".";
    lab.shot(
        {name,
         "TEXTURE " + name,
         caption,
         "reads as its material lit; no line through the middle of LIT 2X2; joints and pits "
         "dark in ORM's red; the normal map's relief matches the height",
         {{"recipe", name}, {"size", std::to_string(size)}, {"seed", std::to_string(recipe.seed)}}},
        sheet);
  }
  lab.metric("recipes", baked, "count");
  lab.table(table);
  if (baked == 0)
    lab.fail("no recipe baked from " + lab.arg("library"),
             "Run from the tree's root, or pass --library=<dir of .tex files>. Skill: textures.");
}

const lab::LabSpec kSpec{
    "texgen-specimens",
    lab::LabKind::picture,
    "engine",
    "engine/texgen",
    "textures",
    "What does every library texture look like lit and as maps, and does each tile seamlessly?",
    {
        {"library", texgen::kLibraryDir, "the directory of .tex recipes"},
        {"only", "", "recipe names, comma-separated (empty: all)"},
        {"size", "256", "the bake's size, px (overrides the recipe's)"},
        {"panel", "256", "each picture's side on the sheet, px"},
    },
    "--size=64 --panel=64 --only=brick_wall,lava_rock,tech_panel",
    run,
};
GE_LAB(kSpec);

}  // namespace
