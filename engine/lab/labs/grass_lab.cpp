// LAB `grass`: THE GRASS WITHOUT A GPU (engine/grass), every number deterministic. Every preset's
// recipe and its density measured off a scattered patch; the blades of every kind seen side on in
// a calm, a breeze and a gale (the CPU's `blade_shape`, the very shape the GPU draws); how far each
// kind bends by the wind's speed; a tussock's clumps from above; and a gust crossing a meadow as a
// front, from above and as the speed at two points 30 m apart down the wind (the second the
// first's, later).
//
// ge:summary Lab: grass on the CPU: presets, density, blades side on by wind, bend curves, gusts
// ge:keywords lab grass preset density blade side-view bend gust front clump picture chart
// ge:skill grass labs
// ge:test tests/grass_sway_test.cpp
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "core/heightfield.hpp"
#include "core/libm/libm.hpp"
#include "core/num_text.hpp"
#include "grass/cover.hpp"
#include "grass/kind.hpp"
#include "grass/sway.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/plot.hpp"
#include "lab/raster.hpp"
#include "wind/field.hpp"

namespace {

namespace gl = ge::lab;
namespace gg = ge::grass;

// A preset the lab names (every name it uses is one of grass::presets()).
gg::Kind named(std::string_view name) {
  for (const auto& k : gg::presets())
    if (k.name == name) return k;
  return {};
}
namespace lm = ge::core::libm;
using ge::core::num::to_fixed;

uint8_t srgb(float linear) {
  const double c = std::clamp(static_cast<double>(linear), 0.0, 1.0);
  const double s = c <= 0.0031308 ? 12.92 * c : 1.055 * lm::pow(c, 1 / 2.4) - 0.055;
  return static_cast<uint8_t>(std::clamp(s * 255 + 0.5, 0.0, 255.0));
}

gl::Rgba colour(const std::array<float, 3>& a, const std::array<float, 3>& b, float t) {
  return {srgb(a[0] + (b[0] - a[0]) * t), srgb(a[1] + (b[1] - a[1]) * t),
          srgb(a[2] + (b[2] - a[2]) * t)};
}

// A patch of `kind` 4 m square on flat ground, its blades.
std::vector<gg::Blade> patch(const gg::Kind& kind, float side, uint32_t seed) {
  gg::Cover c;
  c.kinds.push_back(kind);
  c.seed = seed;
  const float p[] = {0, 0, 0, side, 0, 0, side, 0, side, 0, 0, side};
  const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
  gg::add_mesh(c, p, idx, 0);
  return gg::scatter_all(c);
}

void presets(gl::Lab& lab) {
  gl::LabTable t{"The presets",
                 {"kind", "height m", "width mm", "blades/m2", "measured /m2", "give m/s",
                  "sway Hz", "clump m"},
                 {}};
  for (const gg::Kind& k : gg::presets()) {
    const auto blades = patch(k, 4, 1);
    const double measured = static_cast<double>(blades.size()) / 16;
    t.rows.push_back({k.name, to_fixed(k.height, 2), to_fixed(k.width * 1000, 1),
                      to_fixed(k.density, 0), to_fixed(measured, 1), to_fixed(k.give, 1),
                      to_fixed(k.sway, 1), to_fixed(k.clump, 2)});
    lab.metric("density." + k.name, measured, "1/m2");
    if (std::fabs(measured / k.density - 1) > 0.05) {
      lab.fail(k.name + " grows " + to_fixed(measured, 1) + " blades a square metre, asked " +
                   to_fixed(k.density, 0),
               "Read blade_slots and scatter_one in engine/grass/cover.cpp. Skill: grass.");
    }
  }
  lab.table(t);
  if (const auto recipe = gg::parse_kind("long height=1.1 density=150 dry=0.4"))
    lab.note("A recipe: " + gg::print_kind(*recipe));
}

// Every kind side on, in three winds blowing left to right: the blades of a strip 0.4 m deep.
void side_on(gl::Lab& lab, int cell) {
  const float winds[3] = {0, 5, 15};
  const int cols = static_cast<int>(gg::presets().size());
  gl::Picture pic = gl::create_picture(cols * cell, 3 * cell + 16, {214, 226, 236});
  for (int col = 0; col < cols; ++col) {
    const gg::Kind& k = gg::presets()[static_cast<size_t>(col)];
    gl::draw_text(pic, col * cell + 4, 3 * cell + 4, k.name, {40, 40, 40});
    std::vector<gg::Blade> blades = patch(k, 1.6f, 3);
    // The strip nearest the viewer, drawn back to front.
    std::erase_if(blades, [](const gg::Blade& b) { return b.root[2] > 0.4f; });
    std::sort(blades.begin(), blades.end(),
              [](const gg::Blade& a, const gg::Blade& b) { return a.root[2] > b.root[2]; });
    if (blades.size() > 260) blades.resize(260);
    for (int row = 0; row < 3; ++row) {
      const double ox = col * cell, oy = (row + 1) * cell - 6;
      const double scale = (cell - 12) / 1.8;  // 1.8 m tall frame
      gl::fill_rect(pic, ox, oy, cell, 6, {92, 70, 48});
      const float air[2] = {winds[row], 0};
      for (const gg::Blade& b : blades) {
        const gg::Shape s = gg::blade_shape(k, b, air, 1.5f);
        float prev[3];
        gg::bezier_point(s, 0, prev);
        for (int i = 1; i <= 8; ++i) {
          const float u = static_cast<float>(i) / 8;
          float p[3];
          gg::bezier_point(s, u, p);
          const double w = std::max(1.0, b.width * scale * (1 - u * u) * 1.2);
          gl::draw_line(pic, ox + 6 + prev[0] * scale * 0.5, oy - prev[1] * scale,
                        ox + 6 + p[0] * scale * 0.5, oy - p[1] * scale, colour(k.root, k.tip, u),
                        w);
          std::copy(p, p + 3, prev);
        }
      }
      if (col == 0) {
        gl::draw_text(pic, 4, row * cell + 4, to_fixed(winds[row], 0) + " M/S", {40, 40, 40});
      }
    }
  }
  lab.shot({"side-on",
            "EVERY KIND SIDE ON IN A CALM, A BREEZE AND A GALE",
            "The blades of a strip 0.4 m deep of each preset, seen side on, in a calm (top), 5 m/s "
            "(middle) and 15 m/s (bottom) blowing left to right at the blades' height; the frame "
            "is 1.8 m tall, its width halved. The shapes are blade_shape's, which the GPU draws.",
            "each kind stands at its rest lean in the calm, the soft long grass and the tussock "
            "bowed in the breeze where the lawn barely moves, every kind laid over in the gale; no "
            "blade longer than its kind's height",
            {}},
           pic);
}

void bend_curves(gl::Lab& lab) {
  gl::LineChart c;
  c.frame = {"HOW FAR EACH KIND BENDS",
             "b = w^2 / (w^2 + give^2); the tip's height averaged "
             "over two seconds of sway",
             "wind at the blades (m/s)", "tip height / blade height"};
  c.spikes = false;
  for (const gg::Kind& k : gg::presets()) {
    gl::Trace t{k.name, {}, {}};
    gg::Blade b{};
    b.height = 1;
    b.width = k.width;
    b.seed = 0x51ed27u;
    for (int i = 0; i <= 40; ++i) {
      const float w = static_cast<float>(i) * 0.5f;
      const float air[2] = {w, 0};
      double tip = 0;
      for (int s = 0; s < 40; ++s) {
        tip += gg::blade_shape(k, b, air, static_cast<float>(s) * 0.05f).v2[1];
      }
      t.x.push_back(w);
      t.y.push_back(tip / 40);
    }
    lab.metric("tip-at-10." + k.name, t.y[20], "share");
    c.traces.push_back(std::move(t));
  }
  const gl::Chart chart = gl::line_chart(c);
  lab.shot({"bend",
            chart.title,
            "The tip's height as a share of the blade's length against the wind at the blades, "
            "each preset's give deciding where it falls to half its bend.",
            "every curve falls from about the rest lean's cosine toward a quarter; the lawn's the "
            "slowest, the long grass's the fastest",
            {}},
           gl::to_picture(chart.plot));
}

void clumps(gl::Lab& lab, int size) {
  const gg::Kind k = named("tussock");
  const auto blades = patch(k, 3, 5);
  gl::Picture pic = gl::create_picture(size, size, {88, 72, 52});
  const double s = size / 3.0;
  for (const gg::Blade& b : blades) {
    const double x = b.root[0] * s, y = b.root[2] * s;
    gl::draw_line(pic, x, y, x + b.lean[0] * s * b.height, y + b.lean[1] * s * b.height,
                  colour(k.root, k.tip, 0.8f), 1);
  }
  lab.shot({"clumps",
            "A TUSSOCK FROM ABOVE",
            "Every blade of a 3 m square of tussock from above, each a stroke from its root along "
            "its rest lean for its height: the blades of a clump lean toward its middle.",
            "tufts a few tens of centimetres across, their blades pointing in toward each tuft's "
            "middle; tufts of different heights",
            {}},
           pic);
}

void gusts(gl::Lab& lab, int size) {
  ge::wind::Field f;
  f.mean = 8;
  f.from = 1.5 * 3.14159265358979;  // from the west: blows toward +x
  ge::wind::lay_gusts(f, 38, 0.35);
  gg::GrassAir air;
  air.field = &f;
  gg::WindGrid g;
  gg::fill_wind_grid(g, air, 0, 0, 160, 64, 30);
  std::vector<double> speed(static_cast<size_t>(size) * static_cast<size_t>(size));
  double lo = 1e9, hi = 0;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      float w[2];
      gg::wind_at_blade(g, g.x0 + static_cast<float>(x) * 160 / static_cast<float>(size),
                        g.z0 + static_cast<float>(y) * 160 / static_cast<float>(size), 30, w);
      const double v = std::sqrt(w[0] * w[0] + w[1] * w[1]);
      speed[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] = v;
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  }
  gl::Picture pic = gl::create_picture(size, size, {0, 0, 0});
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double v =
          (speed[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] -
           lo) /
          std::max(hi - lo, 1e-9);
      gl::set_pixel(pic, x, y,
                    {static_cast<uint8_t>(20 + 225 * v), static_cast<uint8_t>(50 + 190 * v),
                     static_cast<uint8_t>(30 + 60 * v)});
    }
  }
  lab.metric("gust.slowest", lo, "m/s");
  lab.metric("gust.fastest", hi, "m/s");
  lab.shot({"gust-front",
            "A GUST CROSSING A MEADOW, FROM ABOVE",
            "The wind at the blades over 160 m square at t = 30 s under an 8 m/s westerly with "
            "gusts of 35 % intensity, from the slowest air (dark) to the fastest (light); the wind "
            "blows left to right.",
            "bands of stronger air standing across the wind (fronts, not stripes along it), "
            "ruffled by cat's-paws a few metres across",
            {}},
           pic);
  gl::LineChart c;
  c.frame = {"THE SAME GUST, 30 M APART DOWNWIND", "8 m/s westerly, 35 % gusts", "t (s)",
             "wind at the blades (m/s)"};
  c.spikes = false;
  gl::Trace up{"upwind", {}, {}}, down{"30 m downwind", {}, {}};
  for (int i = 0; i <= 240; ++i) {
    const double t = i * 0.25;
    gg::fill_wind_grid(g, air, 0, 0, 62, 32, t);
    const size_t a = (size_t{16} * 32 + 1) * 2,
                 b = (size_t{16} * 32 + 16) * 2;  // 15 cells of 2 m apart
    up.x.push_back(t);
    up.y.push_back(g.air[a]);
    down.x.push_back(t);
    down.y.push_back(g.air[b]);
  }
  c.traces = {up, down};
  const gl::Chart chart = gl::line_chart(c);
  lab.shot({"gust-lag",
            chart.title,
            "The wind at the blades at two points on one line down the wind, 30 m apart, over a "
            "minute: the gusts carried downwind at the mean speed.",
            "the downwind line is the upwind line shifted right by about 30 / 6.5 s (the mean "
            "brought down to half a metre is slower than 8 m/s at 10 m)",
            {}},
           gl::to_picture(chart.plot));
}

void run(gl::Lab& lab) {
  const int cell = static_cast<int>(lab.arg_num("cell"));
  presets(lab);
  side_on(lab, cell);
  bend_curves(lab);
  clumps(lab, cell * 2);
  gusts(lab, cell * 2);
}

const gl::LabSpec kSpec{
    "grass",
    gl::LabKind::picture,
    "engine",
    "engine/grass/kind.hpp, engine/grass/cover.hpp, engine/grass/sway.hpp",
    "grass",
    "Does every kind of grass grow its density, stand, bend and sway as its numbers say, clump, "
    "and meet the wind's gusts as fronts crossing the field?",
    {{"cell", "200", "a side-on cell's side, px"}},
    "--cell=96",
    run};
GE_LAB(kSpec);

}  // namespace
