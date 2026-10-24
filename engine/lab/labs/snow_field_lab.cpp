// LAB `snow-field`: a patch of fresh snow crossed by everything at once (engine/snow/layer.hpp,
// press.hpp), seen from above. A 64 m layer of --depth m of powder over a gently tilted ground
// is crossed by a skier's S-turns ending in a skidded stop (each ski pressed to the sink a
// skier's planing at speed would give, --ski-sink), a walker, a car's two tyres, a snowmobile's
// tread and skis on a curve, and a skier who fell and slid. Pictured:
//
// - THE SURFACE, hill-shaded under a low sun from the north-west (the vertical drawn twice), so
//   every groove, rut, trench, posthole and berm reads as relief;
// - THE DENSITY, white loose to deep blue packed, with the tiles the layer allocated outlined: what
//   the imprints cost, and that untouched snow costs nothing.
//
// Numbers: the tiles and their memory, the records, the samples pressed, the mass held to the unit,
// and (measured, outside the digest) what a press costs.
//
// ge:summary Lab: a patch of powder crossed by a skier, walker, car, snowmobile and a fall,
// ge:summary seen from above
// ge:keywords lab snow field tracks hillshade relief density tiles memory skier walker car
// ge:keywords snowmobile body press-cost
// ge:skill snow labs
// ge:test tests/snow_press_test.cpp
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "snow/layer.hpp"
#include "snow/press.hpp"
#include "snow/reference.hpp"

namespace {

namespace sn = ge::snow;
namespace gl = ge::lab;
namespace lm = ge::core::libm;

constexpr double kHalf = 15;   // m: the tracks lie in this half-width square round the origin
constexpr double kLayer = 32;  // m: the layer's half width (its 6.4 m tiles, ten a side)
constexpr double kHz = 120;    // presses a second of a moving thing

std::string fmt(double v, int places) {
  char b[48];
  std::snprintf(b, sizeof b, "%.*f", places, v);
  return b;
}

struct Presser {
  sn::Layer& layer;
  int calls = 0;
  double t = 0;
  void operator()(const sn::Imprint& imp) {
    sn::press(layer, imp, t);
    calls++;
    t += 1 / kHz;
  }
};

// A pair of skis down a line through `pts` (x, z every step), each ski a capsule from where it was
// a step ago, pressed `sink` m under the untouched surface; `skid` turns the pair across.
void ski_line(Presser& p, const std::vector<double>& xs, const std::vector<double>& zs, double sink,
              size_t skid_from) {
  double last[2][2] = {{0, 0}, {0, 0}};
  for (size_t i = 1; i < xs.size(); i++) {
    const double dx = xs[i] - xs[i - 1], dz = zs[i] - zs[i - 1];
    const double heading = lm::atan2(dx, dz);
    const bool skid = i >= skid_from;
    const double rx = lm::cos(heading), rz = -lm::sin(heading);  // right of the line
    for (int k = 0; k < 2; k++) {
      const double side = k ? 0.11 : -0.11;
      const double x = xs[i] + rx * side, z = zs[i] + rz * side;
      sn::Imprint imp;
      imp.sink = skid ? sink * 1.6 : sink;
      imp.motion = sn::Motion::slide;
      imp.wall = 6;
      if (skid) {
        // Across the travel, the pair's length long.
        imp.shape = sn::capsule(xs[i] - rx * 0.8, zs[i] - rz * 0.8, xs[i] + rx * 0.8,
                                zs[i] + rz * 0.8, 0.12);
        imp.side_push = 0.5;
        if (k == 1) break;
      } else {
        imp.shape = sn::capsule(i == 1 ? x : last[k][0], i == 1 ? z : last[k][1], x, z, 0.035);
        imp.side_push = 0.05;
      }
      p(imp);
      last[k][0] = x;
      last[k][1] = z;
    }
  }
}

void run(gl::Lab& lab) {
  const double depth = lab.arg_num("depth");
  const double ski_sink = lab.arg_num("ski-sink");
  const int px_per_m = static_cast<int>(lab.arg_num("px-per-m"));
  sn::LayerSpec spec;
  spec.region = {-kLayer, -kLayer, kLayer, kLayer};
  ge::core::Heightfield ground =
      sn::bake_ground(spec.region, 0.5, [](double x, double z) { return -0.12 * z + 0.02 * x; });
  spec.ground.field = &ground;
  spec.cover.depth = depth;
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, spec);
  Presser press{layer};
  const auto t0 = std::chrono::steady_clock::now();

  // THE SKIER: S-turns down the patch along +z, 12 m/s, then a skidded stop.
  {
    std::vector<double> xs, zs;
    const double v = 12 / kHz;
    for (int n = 0; -13 + n * v < 9; n++) {
      const double z = -13 + n * v;
      xs.push_back(-6 + 3.2 * lm::sin(z * 0.42));
      zs.push_back(z);
    }
    const size_t skid_from = xs.size();
    for (int i = 0; i < 40; i++) {
      xs.push_back(xs.back());
      zs.push_back(zs.back() + v * (1 - i / 40.0));
    }
    ski_line(press, xs, zs, ski_sink, skid_from);
  }
  // THE WALKER: a line of steps across the patch, left and right of his line.
  {
    const sn::Imprint boot = sn::reference_contact(2).imprint;
    const double heading = lm::atan2(1.0, 0.35);
    for (int i = 0; i < 34; i++) {
      const double along = -13 + 0.75 * i;
      const double side = i % 2 ? 0.12 : -0.12;
      const double x = along * lm::sin(heading) + side * lm::cos(heading);
      const double z = -2 + along * lm::cos(heading) - side * lm::sin(heading);
      press(sn::place(boot, x, z, heading));
    }
  }
  // THE CAR: two tyres 1.5 m apart, straight down the right of the patch.
  {
    const sn::Imprint tyre = sn::reference_contact(3).imprint;
    for (int n = 0; n < 140; n++) {
      const double z = -14 + n * 0.2;
      press(sn::place(tyre, 8.25, z, 0));
      press(sn::place(tyre, 9.75, z, 0));
    }
  }
  // THE SNOWMOBILE: its tread on a curve, its two skis a metre apart ahead of it.
  {
    const sn::Imprint tread = sn::reference_contact(4).imprint;
    for (int n = 0; n < 110; n++) {
      const double a = n * 0.02;
      const double x = 2 + 9 * lm::cos(a + 1.6), z = 3 + 9 * lm::sin(a + 1.6);
      const double heading = lm::atan2(-lm::sin(a + 1.6), lm::cos(a + 1.6));
      press(sn::place(tread, x, z, heading));
      for (const double side : {-0.5, 0.5}) {
        sn::Imprint ski;
        const double fx = x + 1.6 * lm::sin(heading) + side * lm::cos(heading);
        const double fz = z + 1.6 * lm::cos(heading) - side * lm::sin(heading);
        ski.shape = sn::ellipse(fx, fz, heading, 0.5, 0.075);
        ski.load = 600;
        ski.motion = sn::Motion::slide;
        ski.side_push = 0.2;
        press(ski);
      }
    }
  }
  // THE FALL: a skier down and sliding on his side.
  {
    const sn::Imprint body = sn::reference_contact(5).imprint;
    for (int i = 0; i < 12; i++) press(sn::place(body, 3 + 0.25 * i, -10 + 0.1 * i, 1.3));
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  const bool held = sn::mass_units(layer) + layer.spilled == layer.mass_in;
  if (!held) {
    lab.fail("the field made or lost snow",
             "press must keep the mass to the unit: run tests/snow_press_test.cpp. Skill: snow.");
  }
  lab.metric("presses", press.calls, "count");
  lab.metric("tiles", static_cast<double>(layer.tiles.size()), "count");
  lab.metric("tile-share",
             100.0 * static_cast<double>(layer.tiles.size()) /
                 static_cast<double>(layer.tile_cols * layer.tile_rows),
             "%");
  lab.metric("memory", static_cast<double>(layer.tiles.size() * sizeof(sn::Tile)) / 1024, "KB");
  lab.metric("records", static_cast<double>(layer.records.size() + layer.records_dropped), "count");
  lab.metric("mass-held", held ? 1 : 0, "");
  lab.metric("press-cost", secs * 1e6 / press.calls, "us", gl::Det::measured);

  // THE PICTURES.
  const int size = static_cast<int>(2 * kHalf * px_per_m);
  gl::Picture relief = gl::create_picture(size, size, {255, 255, 255, 255});
  const int dense_ppm = px_per_m / 2 > 1 ? px_per_m / 2 : 1;
  const int dense_size = static_cast<int>(2 * kLayer * dense_ppm);
  gl::Picture dense = gl::create_picture(dense_size, dense_size, {255, 255, 255, 255});
  const double e = 0.05;
  double lx = -0.5, ly = 0.6, lz = 0.6;  // towards the sun: north-west (+z north)
  const double ln = std::sqrt(lx * lx + ly * ly + lz * lz);
  lx /= ln;
  ly /= ln;
  lz /= ln;
  for (int j = 0; j < size; j++) {
    for (int i = 0; i < size; i++) {
      const double x = -kHalf + (i + 0.5) / px_per_m, z = kHalf - (j + 0.5) / px_per_m;
      // The snow's own relief, drawn twice up; the ground's tilt left out.
      auto top = [&](double qx, double qz) {
        const sn::Column q = sn::sample(layer, qx, qz);
        return q.depth * 2;
      };
      const double gx = (top(x + e, z) - top(x - e, z)) / (2 * e);
      const double gz = (top(x, z + e) - top(x, z - e)) / (2 * e);
      double nx = -gx, ny = 1, nz = -gz;
      const double nn = std::sqrt(nx * nx + ny * ny + nz * nz);
      nx /= nn;
      ny /= nn;
      nz /= nn;
      const double lit = ge::core::clamp(nx * lx + ny * ly + nz * lz, 0, 1);
      const double sky = 0.5 + 0.3 * ny;
      auto ch = [&](double sun, double amb) {
        return static_cast<uint8_t>(ge::core::clamp(sun * lit + amb * sky, 0, 255));
      };
      gl::set_pixel(relief, i, j, {ch(235, 70), ch(238, 92), ch(240, 128), 255});
    }
  }
  for (int j = 0; j < dense_size; j++) {
    for (int i = 0; i < dense_size; i++) {
      const double x = -kLayer + (i + 0.5) / dense_ppm, z = kLayer - (j + 0.5) / dense_ppm;
      const double t = ge::core::clamp((sn::sample(layer, x, z).density - 50) / 500, 0, 1);
      auto mix = [&](double a, double b) { return static_cast<uint8_t>(a + (b - a) * t); };
      gl::set_pixel(dense, i, j, {mix(248, 30), mix(250, 80), mix(255, 165), 255});
    }
  }
  const double tile_m = sn::kTileCells * spec.cell;
  for (const sn::Tile& t : layer.tiles) {
    const double x0 = t.tx * tile_m * dense_ppm, y0 = dense_size - (t.tz + 1) * tile_m * dense_ppm;
    const double w = tile_m * dense_ppm;
    gl::draw_line(dense, x0, y0, x0 + w, y0, {200, 70, 40, 200});
    gl::draw_line(dense, x0, y0 + w, x0 + w, y0 + w, {200, 70, 40, 200});
    gl::draw_line(dense, x0, y0, x0, y0 + w, {200, 70, 40, 200});
    gl::draw_line(dense, x0 + w, y0, x0 + w, y0 + w, {200, 70, 40, 200});
  }
  const std::vector<std::pair<std::string, std::string>> inputs = {
      {"depth", lab.arg("depth")}, {"ski-sink", lab.arg("ski-sink")}, {"cell", "0.1"}};
  lab.shot({"relief", "A PATCH OF POWDER CROSSED BY EVERYTHING",
            "30 m square of " + fmt(depth * 100, 0) +
                " cm powder, hill-shaded from the north-west, the relief drawn twice up: a "
                "skier's S-turns and skidded stop (left), a walker's steps, a car's two tyres "
                "(right), a snowmobile's tread and skis on a curve, a skier's fall (bottom).",
            "two thin parallel ski grooves widening into a skid band; staggered footprints; deep "
            "ruts with berms; the tread's broad trench with ski lines ahead of it; a crater; "
            "smooth untouched snow everywhere else",
            inputs},
           relief);
  lab.shot({"density", "WHAT THE SNOW WAS PACKED TO, AND WHAT IT COST",
            "The whole 64 m layer's density, white 50 kg/m3 to deep blue 550; the " +
                std::to_string(layer.tiles.size()) + " fine tiles the layer allocated (6.4 m, " +
                "32 KB each) of its " + std::to_string(layer.tile_cols * layer.tile_rows) +
                " outlined in red. Untouched snow keeps no tile.",
            "packed floors where the pressure was high (footprints and ruts deepest blue); "
            "tiles only where something pressed",
            inputs},
           dense);
}

const gl::LabSpec kSpec{
    "snow-field",
    gl::LabKind::picture,
    "engine",
    "engine/snow/layer.hpp",
    "snow",
    "What does a patch of powder look like after a skier, a walker, a car, a snowmobile and a "
    "fall have crossed it, and what does keeping it cost?",
    {
        {"depth", "0.5", "the powder's depth, m"},
        {"ski-sink", "0.08", "how far each ski is pressed under the untouched surface, m"},
        {"px-per-m", "30", "the pictures' pixels a metre"},
    },
    "--px-per-m=8",
    run,
};
GE_LAB(kSpec);

}  // namespace
