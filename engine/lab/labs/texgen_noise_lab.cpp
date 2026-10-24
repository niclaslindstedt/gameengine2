// LAB `texgen-noise`: every tileable noise kind LOOKED AT, as one octave, as five octaves and
// warped, each drawn two tiles across so a seam would be a line through the middle; with each
// kind's range and mean, and the check that it is periodic (the value at u = 1 is the value at
// u = 0, to rounding).
//
// ge:summary Lab: every texture noise kind at one and five octaves and warped, tiled, its range
// ge:keywords lab texgen noise kinds tileable periodic value perlin cells edges voronoi ridged
// ge:keywords turbulence white warp octaves
// ge:skill textures labs determinism
// ge:test tests/texgen_noise_test.cpp
#include <algorithm>
#include <cmath>
#include <string>

#include "core/num.hpp"
#include "core/num_text.hpp"
#include "lab/lab.hpp"
#include "texgen/noise.hpp"

namespace {

using namespace ge;

void run(lab::Lab& lab) {
  const int panel = static_cast<int>(lab.arg_num("panel"));
  const int period = static_cast<int>(lab.arg_num("period"));
  const auto seed = static_cast<uint32_t>(lab.arg_num("seed"));
  const int gap = 8, label = 14, cols = 3;
  const int n = texgen::kNoiseKindCount;
  lab::Picture sheet = lab::create_picture(cols * panel + (cols + 1) * gap,
                                           n * (panel + label + gap) + gap, {24, 24, 26});
  lab::LabTable table{"NOISE KINDS", {"kind", "min", "max", "mean", "periodic"}, {}};
  for (int k = 0; k < n; ++k) {
    const auto kind = static_cast<texgen::NoiseKind>(k);
    const char* name = texgen::noise_kind_name(kind);
    const texgen::NoiseSpec specs[cols] = {
        {kind, period, 1, 0.5, 1, 0, seed},
        {kind, period, 5, 0.5, 1, 0, seed},
        {kind, period, 5, 0.5, 1, 0.5, seed},
    };
    const char* words[cols] = {"1 OCTAVE", "5 OCTAVES", "5 OCTAVES WARP 0.5"};
    double lo = 1, hi = 0, sum = 0;
    bool periodic = true;
    for (int c = 0; c < cols; ++c) {
      const int x0 = gap + c * (panel + gap), y0 = gap + k * (panel + label + gap);
      lab::draw_text(sheet, x0, y0, std::string(name) + " " + words[c], {220, 220, 220});
      for (int y = 0; y < panel; ++y) {
        for (int x = 0; x < panel; ++x) {
          // Two tiles across and down.
          const double u = 2.0 * (x + 0.5) / panel, v = 2.0 * (y + 0.5) / panel;
          const double f = texgen::sample(specs[c], u - std::floor(u), v - std::floor(v));
          if (c == 1) {
            lo = std::min(lo, f);
            hi = std::max(hi, f);
            sum += f;
          }
          const auto g = static_cast<uint8_t>(core::num::round(f * 255));
          lab::set_pixel(sheet, x0 + x, y0 + label + y, {g, g, g});
        }
      }
      for (int t = 0; t < 64; ++t) {
        const double w = (t + 0.5) / 64;
        // Equal to rounding: a warped field reaches u = 1 by a sum that rounds differently.
        periodic =
            periodic &&
            std::fabs(texgen::sample(specs[c], 0, w) - texgen::sample(specs[c], 1, w)) < 1e-9 &&
            std::fabs(texgen::sample(specs[c], w, 0) - texgen::sample(specs[c], w, 1)) < 1e-9;
      }
    }
    const double mean = sum / (static_cast<double>(panel) * panel);
    lab.metric(std::string(name) + ".min", lo, "");
    lab.metric(std::string(name) + ".max", hi, "");
    lab.metric(std::string(name) + ".mean", mean, "");
    table.rows.push_back({name, core::num::to_fixed(lo, 3), core::num::to_fixed(hi, 3),
                          core::num::to_fixed(mean, 3), periodic ? "yes" : "NO"});
    if (!periodic)
      lab.fail(std::string(name) + " is not periodic: its value at u = 1 is not its value at 0",
               "A lattice index is hashed unwrapped: see wrap_cell in engine/texgen/noise.cpp and "
               "run tests/texgen_noise_test.cpp. Skill: textures.");
  }
  lab.table(table);
  lab.shot({"kinds",
            "TEXTURE NOISE KINDS",
            "Every noise kind (rows) as one octave, five octaves and five octaves warped by 0.5 "
            "(columns), period " +
                std::to_string(period) +
                " cells a tile, drawn two tiles "
                "across and two down: black 0, white 1.",
            "no line through the middle of any panel (each tiles); value soft, perlin "
            "smoother, cells round, edges thin lines, voronoi flat cells, ridged sharp crests",
            {{"seed", lab.arg("seed")}, {"period", lab.arg("period")}}},
           sheet);
}

const lab::LabSpec kSpec{
    "texgen-noise",
    lab::LabKind::picture,
    "engine",
    "engine/texgen/noise.hpp",
    "textures",
    "What does each tileable noise kind look like, and does each repeat without a seam?",
    {
        {"panel", "256", "each panel's side, px"},
        {"period", "4", "the first octave's cells across a tile"},
        {"seed", "7", "the noise seed"},
    },
    "--panel=48",
    run,
};
GE_LAB(kSpec);

}  // namespace
