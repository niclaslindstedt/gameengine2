// THE LIQUID LABS' SCENE (shared by `gpu-lava` and `gpu-slime`): a quarry's ground with a pit dug
// into it, a liquid volume (water/world.hpp) filling the pit, drawn under the sky with the
// liquid's passes (render/liquid.hpp) and the bloom, on a device of its own, read back as a
// picture.
//
// ge:summary The liquid labs' scene: a pit in a quarry floor, the liquid in it, read back
// ge:keywords lab liquid scene pit quarry lava slime picture make_scene gpu-lava gpu-slime
// ge:skill render water labs
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/heightfield.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/target.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "render/bloom.hpp"
#include "render/frame.hpp"
#include "render/liquid.hpp"
#include "render/post.hpp"
#include "render/sky.hpp"
#include "render/terrain.hpp"
#include "water/liquids.hpp"
#include "water/world.hpp"

namespace ge::lab::liquid_scene {

namespace gl = ge::lab;
namespace gr = ge::render;
namespace gw = ge::water;

// Whether GE_GPU_REQUIRE=1 asks for a device (so a missing adapter fails the lab, not skips it).
inline bool gpu_required() {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool on = _dupenv_s(&v, &n, "GE_GPU_REQUIRE") == 0 && v && std::strcmp(v, "1") == 0;
  std::free(v);
  return on;
#else
  const char* v = std::getenv("GE_GPU_REQUIRE");
  return v && std::strcmp(v, "1") == 0;
#endif
}

// THE PIT: a quarry floor at 10 m, a pit 40 x 40 m round (200, 200) with walls 3 m high and a
// bed at 6 m, stones on its bed; the liquid's top at 9 m. The field is 256 m a side at 1 m.
constexpr double kFloor = 10, kBed = 6, kLevel = 9;
constexpr double kPitX = 128, kPitZ = 128, kHalf = 20;

inline float pit_ground(double x, double z) {
  const double dx = std::fabs(x - kPitX) - kHalf, dz = std::fabs(z - kPitZ) - kHalf;
  const double out = std::max(dx, dz);  // > 0 outside the pit
  double h = out > 0 ? kFloor : kBed;
  // A steep wall over the last 2 m, and the floor's own swell.
  if (out > -2 && out <= 0) h = kBed + (kFloor - kBed) * (out + 2) / 2;
  if (out > 0) h += 0.6 * ge::core::libm::sin(x / 23) * ge::core::libm::cos(z / 31);
  // Stones on the bed.
  for (int i = 0; i < 5; ++i) {
    const double sx = kPitX - 12 + 6 * i, sz = kPitZ + (i % 2 ? 4 : -5);
    const double r = ge::core::hypot(x - sx, z - sz);
    if (r < 2.2) h = std::max(h, kBed + 2.2 * std::sqrt(1 - (r / 2.2) * (r / 2.2)));
  }
  return static_cast<float>(h);
}

struct Ground {
  ge::core::Heightfield field;
  std::vector<uint8_t> albedo;
};

// `rock`: the ground's colour, sRGB 0..255.
inline Ground make_ground(const uint8_t rock[3]) {
  Ground g;
  constexpr int n = 257;
  constexpr double cell = 1;
  g.field = ge::core::create_heightfield(0, 0, cell, n, n);
  g.albedo.resize(size_t{n} * n * 4);
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const size_t i = static_cast<size_t>(r) * n + static_cast<size_t>(c);
      g.field.data[i] = pit_ground(c * cell, r * cell);
      const double grain =
          0.85 + 0.15 * ge::core::libm::sin(c * 1.7) * ge::core::libm::cos(r * 2.3);
      for (int k = 0; k < 3; ++k)
        g.albedo[i * 4 + k] = static_cast<uint8_t>(std::clamp(rock[k] * grain, 0.0, 255.0));
      g.albedo[i * 4 + 3] = 255;
    }
  }
  return g;
}

// The pit's liquid as a volume: one box hull from the bed to the level.
inline std::vector<gw::Hull> pit_hull() {
  gw::Hull h;
  h.planes = {{0, 1, 0, kLevel},        {0, -1, 0, -kBed + 0.5},
              {1, 0, 0, kPitX + kHalf}, {-1, 0, 0, -(kPitX - kHalf)},
              {0, 0, 1, kPitZ + kHalf}, {0, 0, -1, -(kPitZ - kHalf)}};
  return {h};
}

// A world with the pit filled with the named liquid, under `wind` m/s from the west; the
// body's box is the pit's.
inline gw::World pit_world(const std::string& liquid, double wind) {
  ge::wind::Field f;
  f.mean = wind;
  f.from = 1.5 * 3.14159265358979;
  gw::World world(f);
  const int i = gw::add_volume(world, pit_hull(), gw::liquid(liquid));
  gw::Body& b = world.bodies[static_cast<size_t>(i)];
  b.level = kLevel;
  b.box[0] = kPitX - kHalf;
  b.box[1] = kPitZ - kHalf;
  b.box[2] = kPitX + kHalf;
  b.box[3] = kPitZ + kHalf;
  gw::lay_waves(world, i, 2 * kHalf);
  return world;
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
  gr::Liquids liquids;
  gr::Bloom bloom;
  gr::Post post;
  gr::FrameRecorder rec;
  Ground ground;
  ge::gpu::Target out;
  uint32_t w = 0, h = 0;
};

inline bool make_scene(ge::gpu::Device& d, Scene& s, const uint8_t rock[3], uint32_t w, uint32_t h,
                       double bloom = 0.05) {
  s.w = w;
  s.h = h;
  s.ground = make_ground(rock);
  s.targets = gr::create_targets(d, "gpu-liquid", w, h);
  s.uniforms = gr::make_buffer(d, "gpu-liquid.uniforms", sizeof(gr::FrameUniforms),
                               wgpu::BufferUsage::Uniform);
  s.layout = gr::frame_layout(d);
  gr::create_sky(d, s.pipelines, s.sky, s.uniforms, s.layout);
  gr::TerrainSource src;
  src.height = &s.ground.field;
  src.albedo = &s.ground.albedo;
  src.base_spacing = 0.5;
  src.levels = 8;
  gr::create_terrain(d, s.pipelines, s.terrain, src, s.layout);
  gr::create_liquid(d, s.pipelines, s.liquids, s.layout);
  gr::BloomSettings bs;
  bs.strength = bloom;
  gr::create_bloom_pipelines(d, s.pipelines, s.bloom, bs);
  gr::PostSettings ps;
  ps.bloom = bloom;
  gr::create_post(d, s.pipelines, s.post, s.layout, wgpu::TextureFormat::RGBA8Unorm, ps);
  if (!s.pipelines.finish(d)) return false;
  ge::gpu::end_load(d);
  s.frame = gr::frame_group(d, s.layout, s.uniforms, s.sky);
  gr::create_liquid_targets(d, s.liquids, s.targets);
  const wgpu::TextureView inputs[2] = {s.targets.hdr_view, s.targets.hdr_view};
  gr::create_bloom_targets(d, s.bloom, w, h, inputs);
  s.post_group = gr::post_group(d, s.post, s.targets.hdr_view, s.bloom.view);
  s.rec.init(d, "gpu-liquid.timer", 10);
  s.out = ge::gpu::create_target(d, "gpu-liquid.final", w, h);
  return true;
}

inline gl::Picture draw(ge::gpu::Device& d, Scene& s, const gr::FrameView& v, bool liquid = true) {
  s.rec.begin_frame(d);
  gr::fill_uniforms(s.staged, v, s.targets.width, s.targets.height);
  d.queue.WriteBuffer(s.uniforms, 0, &s.staged, sizeof s.staged);
  gr::prepare_sky(s.sky, v);
  gr::prepare_terrain(d, s.terrain, v);
  gr::prepare_liquid(d, s.liquids, v);
  wgpu::CommandEncoder e = d.device.CreateCommandEncoder();
  gr::encode_sky_tables(s.sky, s.pipelines, s.rec, e);
  gr::encode_terrain_cull(s.terrain, s.pipelines, s.rec, e, s.frame);
  gr::encode_terrain_horizon(s.terrain, s.pipelines, s.rec, e);
  gr::encode_terrain_depth(s.terrain, s.pipelines, s.rec, e, s.targets, s.frame);
  gr::encode_terrain(s.terrain, s.pipelines, s.rec, e, s.targets, s.frame);
  gr::encode_sky(s.sky, s.pipelines, s.rec, e, s.targets, s.frame);
  if (liquid) gr::encode_liquid(s.liquids, s.pipelines, s.rec, e, s.targets, s.frame);
  gr::encode_bloom(s.bloom, s.pipelines, s.rec, e, 0);
  gr::encode_post(s.post, s.pipelines, s.rec, e, s.frame, s.post_group, s.out.view);
  s.rec.end_frame(e);
  wgpu::CommandBuffer c = e.Finish();
  d.queue.Submit(1, &c);
  s.rec.after_submit(d);
  const ge::gpu::Image img = ge::gpu::read_now(d, s.out);
  return gl::Picture{static_cast<int>(img.width), static_cast<int>(img.height), img.rgba};
}

inline gr::FrameView view_at(double x, double y, double z, double heading, double pitch,
                             double sun_el, double sun_az, double t) {
  gr::FrameView v;
  v.eye[0] = x;
  v.eye[1] = y;
  v.eye[2] = z;
  v.heading = heading;
  v.pitch = pitch;
  v.fov_y = 0.9;
  v.sun_elevation = sun_el;
  v.sun_azimuth = sun_az;
  v.altitude_m = 300;
  v.time_s = t;
  return v;
}

inline std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

inline void paste(gl::Picture& sheet, const gl::Picture& p, int ox, int oy) {
  for (int y = 0; y < p.height; ++y) {
    for (int x = 0; x < p.width; ++x) {
      const size_t s = (static_cast<size_t>(y) * p.width + x) * 4;
      const size_t t = (static_cast<size_t>(oy + y) * sheet.width + ox + x) * 4;
      std::memcpy(&sheet.rgba[t], &p.rgba[s], 4);
    }
  }
}

// The share of pixels that differ by more than a few levels.
inline double changed(const gl::Picture& a, const gl::Picture& b) {
  size_t n = 0;
  for (size_t i = 0; i < a.rgba.size(); i += 4) {
    int dmax = 0;
    for (size_t c = 0; c < 3; ++c)
      dmax = std::max(dmax, std::abs(int{a.rgba[i + c]} - int{b.rgba[i + c]}));
    n += dmax > 6;
  }
  return static_cast<double>(n) * 4 / static_cast<double>(a.rgba.size());
}

// A machine with no GPU adapter: a placeholder for each of the lab's shots, by the same ids in the
// same order (a shot's id is in the digest even when its picture is measured), so the digest is
// the same on every row.
inline void placeholders(gl::Lab& lab, std::initializer_list<const char*> ids) {
  for (const char* id : ids) {
    gl::Picture none = gl::create_picture(160, 90, {40, 40, 48});
    gl::draw_text(none, 4, 4, "NO GPU ADAPTER", {230, 230, 230});
    lab.shot({id, "NO GPU ADAPTER", "This machine has no GPU adapter.", "nothing", {}}, none,
             gl::Det::measured);
  }
}

// GPU errors and pipelines made after the load, failed with the next action.
inline bool checked(gl::Lab& lab, int errors, int late) {
  lab.metric("gpu-errors", errors, "count");
  lab.metric("pipelines-after-load", late, "count");
  if (errors != 0 || late != 0) {
    lab.fail(std::to_string(errors) + " GPU errors, " + std::to_string(late) +
                 " pipelines after the load",
             "Read the gpu: lines above; add any pipeline to create_liquid's manifest "
             "(engine/render/liquid.cpp). Skill: render.");
    return false;
  }
  return true;
}

}  // namespace ge::lab::liquid_scene
