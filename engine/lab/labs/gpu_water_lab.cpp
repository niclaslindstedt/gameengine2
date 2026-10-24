// LAB `gpu-water`: THE WATER'S SURFACE DRAWN (render/water.hpp), on the software adapter, over
// two scenes: a lake in a bowl of hills (its shore shelving from the sand to 25 m) and a sea off
// a beach. Named views: the calm lake and the same lake in a fresh wind; the sea under a light
// air, a fresh breeze and a gale; the sea at sunset into the sun's path; the shallows from above
// in six looks (the colour from what is in the water, water/optics.hpp); and a slow fly-out
// over the sea, every distance's band in one strip. It holds the GPU's crest to the
// simulation's (the depth read back looking straight down against `surface_at` with the same
// waves kept) and the detail band's slope variance to the spectrum's, and fails on a GPU error,
// a pipeline made after the load, or a view with no water in it.
//
// ge:summary Lab: the water's surface drawn: lake, sea by wind, sunset, looks, the fly-out
// ge:keywords lab gpu water surface lake sea sunset glitter fresnel refraction looks fly-out
// ge:keywords crest detail-waves picture software-adapter
// ge:skill render water labs
// ge:test tests/render_water_test.cpp
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/heightfield.hpp"
#include "core/libm/libm.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/target.hpp"
#include "lab/lab.hpp"
#include "lab/labs/water_scene.hpp"
#include "lab/raster.hpp"
#include "render/frame.hpp"
#include "render/post.hpp"
#include "render/sky.hpp"
#include "render/terrain.hpp"
#include "render/water.hpp"
#include "water/world.hpp"
#include "wind/field.hpp"

namespace {

namespace gl = ge::lab;
namespace gr = ge::render;
namespace gw = ge::water;
namespace lm = ge::core::libm;
using ge::lab::water_scene::draw;
using ge::lab::water_scene::fixed;
using ge::lab::water_scene::gpu_required;
using ge::lab::water_scene::make_scene;
using ge::lab::water_scene::paste;
using ge::lab::water_scene::placeholders;
using ge::lab::water_scene::Scene;
using ge::lab::water_scene::view_at;
using gl::Det;
constexpr double kPi = 3.14159265358979323846;

// THE LAKE: a bowl 1 km across, its surface at 100 m, 25 m deep in the middle and the shore at
// 320 m from it; the hills rising beyond.
constexpr double kLakeLevel = 100, kLakeX = 512, kLakeZ = 512;
float lake_ground(double x, double z) {
  const double dx = x - kLakeX, dz = z - kLakeZ, r = std::sqrt(dx * dx + dz * dz);
  const double bowl = 75 + 25 * (r / 320) * (r / 320);
  return static_cast<float>(bowl + 2.5 * lm::sin(x / 37) * lm::cos(z / 53));
}
// THE SEA: the sea floor 40 m down, rising to a beach from z = 7600 m (the field is 8 km
// across, so the copies the terrain mirrors past its edge lie beyond the clipmap's reach).
constexpr double kBeach = 7600;
float sea_ground(double x, double z) {
  const double rise = z < kBeach ? -40 : -40 + (z - kBeach) * 0.12;
  return static_cast<float>(rise + 1.5 * lm::sin(x / 29) * lm::cos(z / 41));
}

// A lake world under `wind` m/s from the west, in `look`.
gw::World lake_world(double wind, const std::string& look) {
  ge::wind::Field field;
  field.mean = wind;
  field.from = 1.5 * kPi;
  gw::World world(field);
  std::vector<gw::Point> shore;
  for (int i = 0; i < 48; ++i) {
    const double a = 2 * kPi * i / 48;
    shore.push_back({kLakeX + 330 * lm::cos(a), kLakeZ + 330 * lm::sin(a)});
  }
  gw::add_lake(world, shore, kLakeLevel, {.look = look}, {}, 25);
  return world;
}

gw::World sea_world(double wind, const std::string& look) {
  ge::wind::Field field;
  field.mean = wind;
  field.from = 0.0;  // from the north: onshore, the waves running toward the beach
  gw::World world(field);
  gw::add_ocean(world, 0, {.look = look, .density = 1025});
  return world;
}

// The share of a picture's pixels that differ from another's by more than a few levels.
double changed(const gl::Picture& a, const gl::Picture& b) {
  size_t n = 0;
  for (size_t i = 0; i < a.rgba.size(); i += 4) {
    int dmax = 0;
    for (size_t c = 0; c < 3; ++c)
      dmax = std::max(dmax, std::abs(int{a.rgba[i + c]} - int{b.rgba[i + c]}));
    n += dmax > 6;
  }
  return static_cast<double>(n) * 4 / static_cast<double>(a.rgba.size());
}

// The height the GPU's vertex keeps: the components at least `shortest` m long, faded in to
// twice that (water.wgsl's `height_at`).
double kept_height(const gw::Waves& waves, double x, double z, double t, double shortest) {
  double h = 0;
  for (int i = 0; i < waves.count; ++i) {
    const gw::Component& c = waves.c[i];
    const double len = 2 * kPi / c.k;
    double keep = std::clamp((len - shortest) / shortest, 0.0, 1.0);
    keep = keep * keep * (3 - 2 * keep);
    if (keep <= 0) continue;
    const double phi = c.kx * x + c.kz * z - c.w * t + std::fmod(c.phase, 2 * kPi);
    h += keep * (c.a * lm::sin(phi) - 0.5 * c.k * c.a * c.a * lm::cos(2 * phi));
  }
  return h;
}

bool checked(gl::Lab& lab, int errors, int late) {
  lab.metric("gpu-errors", errors, "count");
  lab.metric("pipelines-after-load", late, "count");
  if (errors != 0 || late != 0) {
    lab.fail(std::to_string(errors) + " GPU errors, " + std::to_string(late) +
                 " pipelines after the load",
             "Read the gpu: lines above; add any pipeline to create_water's manifest "
             "(engine/render/water.cpp). Skill: render.");
    return false;
  }
  return true;
}

void detail(gl::Lab& lab) {
  gl::LabTable table{"The detail band past the simulation's waves",
                     {"wind m/s", "sim Hs m", "detail mss", "tail mss", "detail shortest m"},
                     {}};
  for (const double wind : {3.0, 10.0, 20.0}) {
    const gw::World world = sea_world(wind, "adriatic");
    const gw::Body& b = world.bodies[0];
    const gr::WaterDetail dt = gr::detail_waves(b.waves, gr::kWaterDetailWaves, 7);
    const double shortest = dt.waves.empty() ? 0 : 2 * kPi / dt.waves.back().k;
    table.rows.push_back({fixed(wind, 0), fixed(b.waves.sea.hs, 2), fixed(dt.mss, 4),
                          fixed(dt.tail_mss, 4), fixed(shortest, 3)});
    lab.metric("detail.mss." + fixed(wind, 0), dt.mss, "");
    lab.metric("detail.tail." + fixed(wind, 0), dt.tail_mss, "");
  }
  lab.table(table);
}

void run(gl::Lab& lab) {
  detail(lab);
  const uint32_t w = static_cast<uint32_t>(lab.arg_num("width"));
  const uint32_t h = w * 9 / 16;
  auto device = ge::gpu::create_device({.label = "gpu-water", .validation = 1});
  if (!device) {
    if (gpu_required()) {
      lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
               "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
    }
    placeholders(lab,
                 {"lake-calm", "lake-fresh", "looks", "sea-light", "sea-fresh", "sea-gale",
                  "sunset", "fly-out"},
                 w, h);
    return;
  }
  std::vector<std::unique_ptr<ge::gpu::Device>> devices;
  devices.push_back(std::move(device));
  const auto fresh = [&]() -> ge::gpu::Device& {
    devices.push_back(ge::gpu::create_device({.label = "gpu-water", .validation = 1}));
    return *devices.back();
  };
  ge::gpu::Device& d = *devices.front();

  // THE LAKE.
  Scene lake;
  if (!make_scene(d, lake, lake_ground, kLakeLevel, 4, w, h)) {
    lab.fail("the water's pipelines did not build", "Read the gpu: lines above. Skill: render.");
    return;
  }
  const gr::FrameView shore =
      view_at(kLakeX - 262, kLakeLevel + 3, kLakeZ - 125, 1.0, -0.07, 0.6, 2.2, 3.0);
  struct Case {
    std::string id, title, caption, look_for;
    double wind;
  };
  for (const Case& c : std::vector<Case>{
           {"lake-calm", "A LAKE IN A LIGHT AIR",
            "A lake 650 m across in a bowl of hills under a wind of 1.5 m/s from the west, seen "
            "from 3 m over its surface just off the shore; the sun 34 degrees up behind the "
            "camera's right.",
            "the sky mirrored in the far water, the near water seen into (the sandy bed under "
            "the shallows), the shore where the ground leaves the water with no hard edge",
            1.5},
           {"lake-fresh", "THE SAME LAKE IN A FRESH WIND",
            "The same view under 10 m/s from the west: the sea the wind grows over the lake's "
            "fetch, its waves the simulation's components.",
            "short choppy waves across the whole lake, the reflection broken up, darker water "
            "where the waves' faces turn from the sky",
            10.0}}) {
    gw::World world = lake_world(c.wind, "nordic-clear");
    if (lake.water.bodies.empty()) {
      gr::add_water_body(d, lake.water, world.bodies[0]);
    } else {
      gr::set_water_body(d, lake.water, 0, world.bodies[0]);
    }
    const gl::Picture pic = draw(d, lake, shore);
    const gl::Picture dry = draw(d, lake, shore, false);
    const double share = changed(pic, dry);
    lab.metric(c.id + ".water-share", share, "", Det::measured);
    if (share < 0.05) {
      lab.fail("the " + c.id + " view shows no water (" + fixed(share * 100, 1) + "% changed)",
               "Check prepare_water's levels and the body's box (engine/render/water.cpp). "
               "Skill: render.");
    }
    lab.shot({c.id, c.title, c.caption, c.look_for, {{"wind", fixed(c.wind, 1) + " m/s"}}}, pic,
             Det::measured);
  }

  // THE LOOKS: the shallows from above, a sandy shelf from 0 to 6 m deep.
  {
    const int tw = static_cast<int>(w / 2), th = static_cast<int>(h / 2);
    Scene small;
    ge::gpu::Device& ds = fresh();
    make_scene(ds, small, lake_ground, kLakeLevel, 4, static_cast<uint32_t>(tw),
               static_cast<uint32_t>(th));
    const std::vector<std::string> looks = {"nordic-brown", "nordic-clear",      "baltic",
                                            "adriatic",     "tropical-shallows", "glacial",
                                            "algal-pond",   "muddy-river",       "lava"};
    gl::Picture sheet = gl::create_picture(3 * tw + 40, 3 * (th + 14) + 10, {250, 250, 248});
    const gr::FrameView over =
        view_at(kLakeX + 230, kLakeLevel + 12, kLakeZ - 180, 0.66, -0.55, 0.9, 2.6, 2.0);
    for (size_t i = 0; i < looks.size(); ++i) {
      gw::World world = lake_world(3, looks[i]);
      if (small.water.bodies.empty()) {
        gr::add_water_body(ds, small.water, world.bodies[0]);
      } else {
        gr::set_water_body(ds, small.water, 0, world.bodies[0]);
      }
      const gl::Picture pic = draw(ds, small, over);
      const int ox = 10 + static_cast<int>(i % 3) * (tw + 10),
                oy = 4 + static_cast<int>(i / 3) * (th + 14);
      gl::draw_text(sheet, ox, oy, looks[i], {24, 26, 32});
      paste(sheet, pic, ox, oy + 10);
    }
    lab.shot({"looks",
              "THE SHALLOWS IN NINE LOOKS",
              "A sandy shelf from the water's edge to 6 m deep, from 12 m up, in each named "
              "look: the colour comes from what is in the water (water/optics.hpp), the bed "
              "seen through it dimmed by the light's way down and back.",
              "the brown lake amber over the sand and black where deep; the clear lakes and "
              "the sea blue to green-blue with the sand visible far out; the tropical "
              "shallows turquoise; the glacial milky; the mud opaque within centimetres; the "
              "lava glowing",
              {}},
             sheet, Det::measured);
  }

  // THE SEA.
  Scene sea;
  ge::gpu::Device& dsea = fresh();
  make_scene(dsea, sea, sea_ground, 0, 32, w, h);
  const double shore_z = kBeach + 330;
  const gr::FrameView deck = view_at(4000, 8, shore_z, kPi, -0.08, 0.5, kPi + 0.35, 4.0);
  for (const Case& c : std::vector<Case>{
           {"sea-light", "THE SEA IN A LIGHT AIR",
            "The open sea off a beach under 3 m/s, from 8 m "
            "up on the beach looking out; the sun 29 degrees up, ahead and a little right.",
            "a long low swell of short wavelets, the sun's path a narrow glitter", 3.0},
           {"sea-fresh", "A FRESH BREEZE", "The same under 10 m/s, the fully grown sea.",
            "waves of a metre or two, the glitter spread into a broad path", 10.0},
           {"sea-gale", "A GALE", "The same under 20 m/s.",
            "a heavy sea, the long waves' faces dark, the glitter wide and broken", 20.0}}) {
    gw::World world = sea_world(c.wind, "adriatic");
    if (sea.water.bodies.empty()) {
      gr::add_water_body(dsea, sea.water, world.bodies[0]);
    } else {
      gr::set_water_body(dsea, sea.water, 0, world.bodies[0]);
    }
    const gl::Picture pic = draw(dsea, sea, deck);
    lab.shot({c.id, c.title, c.caption, c.look_for, {{"wind", fixed(c.wind, 0) + " m/s"}}}, pic,
             Det::measured);
  }
  // The crest: straight down from 6 m over the open sea, the depth read back against the
  // simulation's surface with the waves the vertex keeps at level 0 (0.5 m quads: 2 m and longer).
  {
    gw::World world = sea_world(10, "adriatic");
    gr::set_water_body(dsea, sea.water, 0, world.bodies[0]);
    const double t = 5.0;
    // Over a crest, not a node: the point of the highest kept surface along a line.
    double best = 0, bx = 4000.0;
    for (int i = 0; i < 200; ++i) {
      const double x = 4000.0 + 0.1 * i;
      const double hk = kept_height(world.bodies[0].waves, x, (kBeach - 400), t, 4 * 0.5);
      if (hk > best) {
        best = hk;
        bx = x;
      }
    }
    gr::FrameView down = view_at(bx, 6, (kBeach - 400), 0, -1.5707, 0.6, 2.2, t);
    draw(dsea, sea, down);
    const std::vector<float> dist = gr::read_distance(dsea, sea.targets, down);
    const float centre = dist[static_cast<size_t>(h / 2) * w + w / 2];
    const double gpu_y = down.eye[1] - centre;
    const double cpu_y = kept_height(world.bodies[0].waves, down.eye[0], down.eye[2], t, 4 * 0.5);
    lab.metric("crest.gpu", gpu_y, "m", Det::measured);
    lab.metric("crest.cpu", cpu_y, "m", Det::measured);
    if (std::fabs(gpu_y - cpu_y) > 0.03) {
      lab.fail("the drawn surface stands " + fixed(gpu_y - cpu_y, 3) +
                   " m off the simulation's under the eye",
               "Hold water.wgsl's height_at to water/waves.cpp's surface_at. Skill: render.");
    }
  }

  {
    gw::World world = sea_world(7, "adriatic");
    gr::set_water_body(dsea, sea.water, 0, world.bodies[0]);
    const gr::FrameView sunset = view_at(4000, 6, shore_z, kPi, -0.02, 0.05, kPi, 4.0);
    lab.shot({"sunset",
              "THE SEA AT SUNSET",
              "7 m/s, the sun 3 degrees over the horizon straight ahead.",
              "the sun's path a column of glitter down to the camera, the water dark beside it",
              {}},
             draw(dsea, sea, sunset), Det::measured);
  }
  // THE FLY-OUT: the same sea from 2, 20, 200 and 2000 m, one strip.
  {
    gw::World world = sea_world(10, "adriatic");
    const int tw = static_cast<int>(w / 2), th = static_cast<int>(h / 2);
    Scene small;
    ge::gpu::Device& df = fresh();
    make_scene(df, small, sea_ground, 0, 32, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    gr::add_water_body(df, small.water, world.bodies[0]);
    gl::Picture sheet = gl::create_picture(4 * tw + 50, th + 26, {250, 250, 248});
    const double heights[4] = {2, 20, 200, 2000};
    for (int i = 0; i < 4; ++i) {
      const gr::FrameView v =
          view_at(4000, heights[i], kBeach - 100, kPi, -0.1 - 0.12 * i, 0.5, kPi + 0.35, 4.0);
      const gl::Picture pic = draw(df, small, v);
      gl::draw_text(sheet, 10 + i * (tw + 10), 4, fixed(heights[i], 0) + " M UP", {24, 26, 32});
      paste(sheet, pic, 10 + i * (tw + 10), 16);
    }
    lab.shot({"fly-out",
              "THE SEA FROM 2 M TO 2 KM",
              "A fresh breeze from 2, 20, 200 and 2000 m up: the near water's waves are "
              "geometry, the far water's are folded into its roughness.",
              "the glitter one smooth path at every height, no shimmering grain or moire far "
              "out, the horizon a clean line",
              {}},
             sheet, Det::measured);
  }
  int errors = 0, late = 0;
  for (const auto& dev : devices) {
    errors += static_cast<int>(dev->errors.load());
    late += static_cast<int>(dev->pipelines_after_load);
  }
  checked(lab, errors, late);
}

const gl::LabSpec kSpec{
    "gpu-water",
    gl::LabKind::picture,
    "engine",
    "engine/render/water.hpp",
    "render",
    "Does the water's surface draw a lake and a sea by their wind, at sunset, in every look and "
    "at every distance, with the simulation's crest?",
    {{"width", "640", "a view's width, px (16:9)"}},
    "--width=256",
    run};
GE_LAB(kSpec);

}  // namespace
