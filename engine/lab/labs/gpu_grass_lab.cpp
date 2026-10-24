// LAB `gpu-grass`: THE GRASS DRAWN (render/grass.hpp), on the software adapter, over rolling
// ground 512 m across painted under its cover with each kind's far colour: a meadow with a path
// and stands of long grass in a breeze; every preset from the same eye; long grass in a calm, a
// breeze, a fresh wind and a gale; six moments of its sway; a low sun behind long grass; the
// field from eye height out to the reach, where the blades thin into the painted ground; a gust
// crossing it from above; and lawn on a terraced slope's treads and none on its risers (a mesh's
// floors, the way a level's grass material is grown). It fails on a GPU error, a pipeline made
// after the load, or a view the grass does not change.
//
// ge:summary Lab: the grass drawn: meadow, every kind, wind sweep, sway, backlight, reach, floors
// ge:keywords lab gpu grass meadow kinds wind sway backlight translucency reach thinning terrace
// ge:keywords picture software-adapter blades
// ge:skill grass render labs
// ge:test tests/render_grass_test.cpp
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/heightfield.hpp"
#include "core/libm/libm.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/target.hpp"
#include "grass/cover.hpp"
#include "grass/kind.hpp"
#include "grass/sway.hpp"
#include "lab/lab.hpp"
#include "lab/labs/water_scene.hpp"
#include "lab/raster.hpp"
#include "render/frame.hpp"
#include "render/grass.hpp"
#include "render/post.hpp"
#include "render/sky.hpp"
#include "render/terrain.hpp"
#include "wind/field.hpp"

namespace {

namespace gl = ge::lab;
namespace gr = ge::render;
namespace gg = ge::grass;

// A preset the lab names (every name it uses is one of grass::presets()).
gg::Kind named(std::string_view name) {
  for (const auto& k : gg::presets())
    if (k.name == name) return k;
  return {};
}
namespace lm = ge::core::libm;
using ge::lab::water_scene::fixed;
using ge::lab::water_scene::gpu_required;
using ge::lab::water_scene::paste;
using ge::lab::water_scene::placeholders;
using ge::lab::water_scene::view_at;
constexpr double kPi = 3.14159265358979323846;

// THE GROUND: rolling hills round (256, 256), a terraced slope at x > 360 for the floors' view:
// treads 8 m deep and risers 2.4 m high over one 2 m cell of the field (50 degrees).
double terrace_rise(double x) {
  const double step = std::floor((x - 360) / 10);
  const double into = (x - 360) - step * 10;
  return 2.4 * step + 2.4 * std::clamp((into - 8.0) / 2.0, 0.0, 1.0);
}
float ground_at(double x, double z) {
  double h = 40 + 3 * lm::sin(x / 41) * lm::cos(z / 53) + 1.5 * lm::sin((x + z) / 23);
  if (x > 360) h = 40 + terrace_rise(x);
  return static_cast<float>(h);
}
// The path, a bare strip winding along z.
double path_off(double x, double z) { return std::fabs(x - (250 + 6 * lm::sin(z / 17))); }
// Stands of long grass where the noise is high.
bool long_stand(double x, double z) { return lm::sin(x / 9) * lm::cos(z / 11) > 0.55; }

uint8_t srgb(float linear) {
  const double c = std::clamp(static_cast<double>(linear), 0.0, 1.0);
  const double s = c <= 0.0031308 ? 12.92 * c : 1.055 * lm::pow(c, 1 / 2.4) - 0.055;
  return static_cast<uint8_t>(std::clamp(s * 255 + 0.5, 0.0, 255.0));
}

struct Scene {
  ge::gpu::PipelineSet pipelines;
  gr::FrameTargets targets;
  wgpu::Buffer uniforms;
  gr::FrameUniforms staged{};
  wgpu::BindGroupLayout layout;
  wgpu::BindGroup frame, post_group;
  gr::Sky sky;
  gr::Terrain terrain;
  gr::Grass grass;
  gr::Post post;
  gr::FrameRecorder rec;
  ge::core::Heightfield field;
  std::vector<uint8_t> albedo;
  ge::gpu::Target out;
  uint32_t w = 0, h = 0;
};

// The ground painted under the meadow's cover: each kind's far colour, dirt on the path.
void paint(Scene& s, const gg::Kind& meadow, const gg::Kind& tall) {
  const auto m = gg::far_colour(meadow), l = gg::far_colour(tall);
  for (int r = 0; r < s.field.rows; ++r) {
    for (int c = 0; c < s.field.cols; ++c) {
      const double x = c * s.field.cell, z = r * s.field.cell;
      const size_t i = (static_cast<size_t>(r) * s.field.cols + c) * 4;
      const auto& k = long_stand(x, z) ? l : m;
      uint8_t px[4] = {srgb(k[0]), srgb(k[1]), srgb(k[2]), 255};
      const bool riser = x > 360 && (x - 360) - std::floor((x - 360) / 10) * 10 >= 7.0;
      if ((path_off(x, z) < 1.4 && x < 360) || riser) {
        px[0] = 120;
        px[1] = 100;
        px[2] = 74;
      }
      std::memcpy(&s.albedo[i], px, 4);
    }
  }
}

bool make_scene(ge::gpu::Device& d, Scene& s, uint32_t w, uint32_t h, const gr::GrassSettings& gs) {
  s.w = w;
  s.h = h;
  s.field = ge::core::create_heightfield(0, 0, 2, 257, 257);
  for (int r = 0; r < 257; ++r) {
    for (int c = 0; c < 257; ++c) {
      s.field.data[static_cast<size_t>(r) * 257 + c] = ground_at(c * 2.0, r * 2.0);
    }
  }
  s.albedo.assign(size_t{257} * 257 * 4, 0);
  paint(s, named("meadow"), named("long"));
  s.targets = gr::create_targets(d, "gpu-grass", w, h);
  s.uniforms = gr::make_buffer(d, "gpu-grass.uniforms", sizeof(gr::FrameUniforms),
                               wgpu::BufferUsage::Uniform);
  s.layout = gr::frame_layout(d);
  gr::create_sky(d, s.pipelines, s.sky, s.uniforms, s.layout);
  gr::TerrainSource src;
  src.height = &s.field;
  src.albedo = &s.albedo;
  src.base_spacing = 0.5;
  src.levels = 8;
  gr::create_terrain(d, s.pipelines, s.terrain, src, s.layout);
  gr::create_grass(d, s.pipelines, s.grass, s.layout, s.terrain, gs);
  gr::create_post(d, s.pipelines, s.post, s.layout, wgpu::TextureFormat::RGBA8Unorm);
  if (!s.pipelines.finish(d)) return false;
  ge::gpu::end_load(d);
  s.frame = gr::frame_group(d, s.layout, s.uniforms, s.sky);
  s.post_group = gr::post_group(d, s.post, s.targets.hdr_view);
  s.rec.init(d, "gpu-grass.timer", 12);
  s.out = ge::gpu::create_target(d, "gpu-grass.final", w, h);
  return true;
}

// The meadow round (cx, cz): `kinds[0]` everywhere, `kinds[1]` in the stands, nothing on the
// path; or one kind everywhere (`one`).
gg::Cover meadow_cover(const Scene& s, const std::vector<gg::Kind>& kinds, double cx, double cz,
                       double half, bool one) {
  gg::Cover c;
  c.kinds = kinds;
  gg::cover_heightfield(
      c, s.field, {cx - half, cz - half, cx + half, cz + half}, 2.0, [&](double x, double z) {
        if (x > 358) return gg::CoverAt{0, 0};
        if (one) return gg::CoverAt{0, 1};
        const float bare = static_cast<float>(std::clamp((path_off(x, z) - 0.6) / 1.4, 0.0, 1.0));
        return gg::CoverAt{long_stand(x, z) ? 1u : 0u, bare};
      });
  return c;
}

// The terraced slope as a mesh (two triangles a cell), lawn on what faces up.
gg::Cover terrace_cover(const Scene& s) {
  std::vector<float> p;
  std::vector<uint32_t> idx;
  const double cell = 1.0;
  const int nx = 60, nz = 40;
  for (int k = 0; k <= nz; ++k) {
    for (int i = 0; i <= nx; ++i) {
      const double x = 362 + i * cell, z = 230 + k * cell;
      p.push_back(static_cast<float>(x));
      p.push_back(static_cast<float>(ge::core::sample_field(s.field, x, z)));
      p.push_back(static_cast<float>(z));
    }
  }
  for (int k = 0; k < nz; ++k) {
    for (int i = 0; i < nx; ++i) {
      const auto v = static_cast<uint32_t>(k * (nx + 1) + i);
      const auto r = static_cast<uint32_t>(nx + 1);
      idx.insert(idx.end(), {v, v + r + 1, v + 1, v, v + r, v + r + 1});
    }
  }
  gg::Cover c;
  c.kinds.push_back(named("lawn"));
  gg::add_mesh(c, p, idx, 0, 1, 0.8f);
  return c;
}

struct Air {
  ge::wind::Field field;
  gg::GrassAir air;
};
std::unique_ptr<Air> air_of(double mean, double from) {
  auto a = std::make_unique<Air>();
  a->field.mean = mean;
  a->field.from = from;
  ge::wind::lay_gusts(a->field, 38, 0.4);
  a->air.field = &a->field;
  return a;
}

gl::Picture draw(ge::gpu::Device& d, Scene& s, const gr::FrameView& v, const gg::GrassAir& air,
                 double t, bool grass, uint32_t* blades = nullptr) {
  s.rec.begin_frame(d);
  gr::fill_uniforms(s.staged, v, s.w, s.h);
  d.queue.WriteBuffer(s.uniforms, 0, &s.staged, sizeof s.staged);
  gr::prepare_sky(s.sky, v);
  gr::prepare_terrain(d, s.terrain, v);
  gg::GrassAir ground_air = air;
  ground_air.ground = &s.field;
  gr::prepare_grass(d, s.grass, v, ground_air, t);
  wgpu::CommandEncoder e = d.device.CreateCommandEncoder();
  gr::encode_sky_tables(s.sky, s.pipelines, s.rec, e);
  gr::encode_terrain_cull(s.terrain, s.pipelines, s.rec, e, s.frame);
  gr::encode_terrain_horizon(s.terrain, s.pipelines, s.rec, e);
  gr::encode_terrain_depth(s.terrain, s.pipelines, s.rec, e, s.targets, s.frame);
  if (grass) gr::encode_grass_scatter(s.grass, s.pipelines, s.rec, e, s.frame);
  gr::encode_terrain(s.terrain, s.pipelines, s.rec, e, s.targets, s.frame);
  if (grass) gr::encode_grass(s.grass, s.pipelines, s.rec, e, s.targets, s.frame);
  gr::encode_sky(s.sky, s.pipelines, s.rec, e, s.targets, s.frame);
  gr::encode_post(s.post, s.pipelines, s.rec, e, s.frame, s.post_group, s.out.view);
  s.rec.end_frame(e);
  wgpu::CommandBuffer c = e.Finish();
  d.queue.Submit(1, &c);
  s.rec.after_submit(d);
  const ge::gpu::Image img = ge::gpu::read_now(d, s.out);
  if (blades && grass) {
    const gr::GrassLists lists = gr::read_grass_blades(d, s.grass);
    *blades = lists.near_count + lists.far_count;
  }
  return gl::Picture{static_cast<int>(img.width), static_cast<int>(img.height), img.rgba};
}

double changed(const gl::Picture& a, const gl::Picture& b) {
  size_t n = 0;
  for (size_t i = 0; i < a.rgba.size(); i += 4) {
    int most = 0;
    for (size_t c = 0; c < 3; ++c) most = std::max(most, std::abs(a.rgba[i + c] - b.rgba[i + c]));
    n += most > 6;
  }
  return static_cast<double>(n) * 4 / static_cast<double>(a.rgba.size());
}

// Pictures pasted into a sheet `cols` wide, each labelled.
gl::Picture sheet(const std::vector<gl::Picture>& pics, const std::vector<std::string>& labels,
                  int cols) {
  const int w = pics[0].width, h = pics[0].height;
  const int rows = (static_cast<int>(pics.size()) + cols - 1) / cols;
  gl::Picture out = gl::create_picture(w * cols, h * rows, {20, 20, 24});
  for (size_t i = 0; i < pics.size(); ++i) {
    const int x = static_cast<int>(i) % cols * w, y = static_cast<int>(i) / cols * h;
    paste(out, pics[i], x, y);
    gl::fill_rect(out, x, y, gl::text_width(labels[i], 2) + 8, 20, {20, 20, 24});
    gl::draw_text(out, x + 4, y + 3, labels[i], {240, 240, 240}, 2);
  }
  return out;
}

bool checked(gl::Lab& lab, ge::gpu::Device& d) {
  const int errors = static_cast<int>(d.errors.load());
  const int late = static_cast<int>(d.pipelines_after_load);
  lab.metric("gpu-errors", errors, "count");
  lab.metric("pipelines-after-load", late, "count");
  if (errors != 0 || late != 0) {
    lab.fail(std::to_string(errors) + " GPU errors, " + std::to_string(late) +
                 " pipelines after the load",
             "Read the gpu: lines above; every pipeline is made in create_grass "
             "(engine/render/grass.cpp). Skill: render.");
    return false;
  }
  return true;
}

void run(gl::Lab& lab) {
  const uint32_t w = static_cast<uint32_t>(lab.arg_num("width"));
  const uint32_t h = w * 9 / 16;
  auto device = ge::gpu::create_device({.label = "gpu-grass", .validation = 1});
  const std::vector<std::string> ids = {"meadow",  "kinds", "wind",       "sway",
                                        "backlit", "reach", "from-above", "floors"};
  if (!device) {
    if (gpu_required()) {
      lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
               "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
    }
    placeholders(lab, ids, w, h);
    return;
  }
  ge::gpu::Device& d = *device;
  gr::GrassSettings gs;
  gs.reach_m = static_cast<float>(lab.arg_num("reach"));
  gs.capacity = 1u << 21;
  Scene s;
  if (!make_scene(d, s, w, h, gs)) {
    lab.fail("the grass's pipelines did not build", "Read the gpu: lines above. Skill: render.");
    return;
  }
  const gg::Kind meadow = named("meadow"), tall = named("long");
  const double half = gs.reach_m + 8;
  const auto breeze = air_of(5, 1.5 * kPi);  // from the west: toward +x

  // THE MEADOW.
  const double ex = 236, ez = 120;
  if (!gr::set_grass_cover(d, s.grass, meadow_cover(s, {meadow, tall}, ex, ez + 40, half, false))) {
    lab.fail("the meadow's cover did not upload", "Skill: grass.");
    return;
  }
  const double eye_y = ge::core::sample_field(s.field, ex, ez) + 1.6;
  const gr::FrameView meadow_view = view_at(ex, eye_y, ez, 0.25, -0.12, 0.6, 2.4, 0);
  uint32_t blades = 0;
  const gl::Picture bare = draw(d, s, meadow_view, breeze->air, 10, false);
  const gl::Picture grown = draw(d, s, meadow_view, breeze->air, 10, true, &blades);
  lab.metric("meadow.blades", blades, "count", gl::Det::measured);
  lab.metric("meadow.changed", changed(bare, grown), "share", gl::Det::measured);
  if (changed(bare, grown) < 0.1) {
    lab.fail("the grass changed " + fixed(changed(bare, grown) * 100, 1) + " % of the meadow",
             "Read encode_grass_scatter's cull and keep in engine/render/grass_scatter.wgsl. "
             "Skill: grass.");
  }
  lab.shot({"meadow",
            "A MEADOW WITH A PATH IN A BREEZE",
            "Meadow grass over rolling ground with stands of long grass and a bare path, seen "
            "from 1.6 m under a 5 m/s westerly with gusts; the sun 34 degrees up behind the "
            "camera's left. The ground past the blades' reach is painted with each kind's far "
            "colour.",
            "blades standing up near the camera, clumped and of mixed heights; the path bare; the "
            "stands of long grass taller; the field thinning into the painted ground with no edge",
            {}},
           grown, gl::Det::measured);

  // EVERY KIND from one eye.
  {
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    const auto light = air_of(3, 1.5 * kPi);
    for (const gg::Kind& k : gg::presets()) {
      gr::set_grass_cover(d, s.grass, meadow_cover(s, {k}, ex, ez + 15, 30, true));
      pics.push_back(draw(d, s, view_at(ex, eye_y, ez, 0.25, -0.2, 0.6, 2.4, 0), light->air, 7,
                          true, &blades));
      labels.push_back(k.name);
      lab.metric("kinds.blades." + k.name, blades, "count", gl::Det::measured);
    }
    lab.shot({"kinds",
              "EVERY PRESET FROM ONE EYE",
              "Each preset grown over the same ground in a 3 m/s breeze from the same eye at "
              "1.6 m: lawn, meadow, long, tussock, steppe and reeds.",
              "the lawn a short dense carpet, the meadow shin high, the long grass knee to thigh "
              "high and soft, the tussock in tufts, the steppe thin and straw-coloured, the reeds "
              "tall, sparse and upright",
              {}},
             sheet(pics, labels, 3), gl::Det::measured);
  }

  // THE WIND SWEEP and the sway, in long grass.
  gr::set_grass_cover(d, s.grass, meadow_cover(s, {tall}, ex, ez + 15, 30, true));
  const gr::FrameView close = view_at(ex, eye_y - 0.2, ez, 0.25, -0.18, 0.6, 2.4, 0);
  {
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    for (const double wind : {0.0, 4.0, 10.0, 20.0}) {
      const auto a = air_of(wind, 1.5 * kPi);
      pics.push_back(draw(d, s, close, a->air, 12, true));
      labels.push_back(fixed(wind, 0) + " M/S");
    }
    lab.shot({"wind",
              "LONG GRASS IN A CALM, A BREEZE, A FRESH WIND AND A GALE",
              "Long grass from 1.4 m with the wind at 10 m blowing from the left at 0, 4, 10 and "
              "20 m/s (about half that at the blades).",
              "upright with a rest lean in the calm; bowed to the right and rippling in the "
              "breeze; laid well over in the fresh wind; nearly flat in the gale",
              {}},
             sheet(pics, labels, 2), gl::Det::measured);
  }
  {
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    const auto a = air_of(8, 1.5 * kPi);
    for (int i = 0; i < 6; ++i) {
      const double t = 20 + i * 0.3;
      pics.push_back(draw(d, s, close, a->air, t, true));
      labels.push_back("T " + fixed(t, 1) + " S");
    }
    lab.shot({"sway",
              "SIX MOMENTS OF LONG GRASS SWAYING",
              "Long grass from the same eye in an 8 m/s westerly, 0.3 s apart.",
              "the blades move between frames, each at its own phase, the field never in step; "
              "gusts lay patches over further, then let them up",
              {}},
             sheet(pics, labels, 3), gl::Det::measured);
  }

  // BACKLIT: a low sun ahead.
  lab.shot({"backlit",
            "LONG GRASS AGAINST A LOW SUN",
            "Long grass in a 4 m/s breeze looking toward a sun 8 degrees up ahead and to the "
            "right.",
            "the blades' tips glowing where the sun shines through them, their faces toward the "
            "camera in shade",
            {}},
           draw(d, s, view_at(ex, eye_y - 0.2, ez, 0.25, -0.1, 0.14, 0.6, 0),
                air_of(4, 1.5 * kPi)->air, 3, true),
           gl::Det::measured);

  // THE REACH: the meadow out to the blades' reach and the painted ground past it.
  gr::set_grass_cover(d, s.grass, meadow_cover(s, {meadow, tall}, ex, ez + 60, half + 20, false));
  {
    const gl::Picture far = draw(d, s, view_at(ex, eye_y + 0.4, ez - 20, 0.0, -0.03, 0.6, 2.4, 0),
                                 breeze->air, 10, true, &blades);
    lab.metric("reach.blades", blades, "count", gl::Det::measured);
    lab.shot({"reach",
              "THE MEADOW OUT TO THE REACH",
              "The meadow from 2 m looking along the ground to the horizon; blades are kept all "
              "within " +
                  fixed(gs.full_m, 0) +
                  " m, thinning as the square of the distance past "
                  "it, none past " +
                  fixed(gs.reach_m, 0) + " m, the fewer blades widened.",
              "a continuous field from the foreground to the hills, no ring where the blades end "
              "and the painted ground takes over",
              {}},
             far, gl::Det::measured);
  }

  // FROM ABOVE: a gust crossing the field.
  {
    const auto gale = air_of(12, 1.5 * kPi);
    gr::set_grass_cover(d, s.grass, meadow_cover(s, {tall}, 236, 170, 40, true));
    const double top = ge::core::sample_field(s.field, 236, 150) + 14;
    lab.shot({"from-above",
              "A GUST CROSSING LONG GRASS, FROM ABOVE",
              "Long grass from 14 m up looking down the slope in a 12 m/s westerly with gusts, "
              "the sun high.",
              "bands of grass laid further over (lighter, showing their blades' backs) standing "
              "across the wind, with smaller cat's-paws between",
              {}},
             draw(d, s, view_at(236, top, 150, 0.0, -0.8, 1.0, 2.4, 0), gale->air, 30, true),
             gl::Det::measured);
  }

  // FLOORS: lawn on a terraced slope's treads, none on its risers.
  gr::set_grass_cover(d, s.grass, terrace_cover(s));
  {
    const double fx = 366, fz = 228;
    const double fy = ge::core::sample_field(s.field, fx, fz) + 7;
    lab.shot({"floors",
              "LAWN ON A MESH'S FLOORS",
              "A terraced slope as a mesh (treads 8 m deep, risers 2.4 m high): the lawn grows on "
              "every face whose normal is within 37 degrees of up, so on the treads and not the "
              "risers, the way a level's floors with a grass material are grown.",
              "a short lawn on each tread, the risers bare ground",
              {}},
             draw(d, s, view_at(fx, fy, fz, 0.7, -0.35, 0.7, 2.4, 0), breeze->air, 5, true),
             gl::Det::measured);
  }
  checked(lab, d);
}

const gl::LabSpec kSpec{
    "gpu-grass",
    gl::LabKind::picture,
    "engine",
    "engine/render/grass.hpp",
    "grass",
    "Does the grass draw every kind on the ground and on a mesh's floors, bend and sway in the "
    "engine's wind, glow against a low sun and thin into the ground at its reach?",
    {{"width", "640", "a view's width, px (16:9)"}, {"reach", "70", "the blades' reach, m"}},
    "--width=192 --reach=30",
    run};
GE_LAB(kSpec);

}  // namespace
