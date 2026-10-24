// THE WATER LABS' SCENE (shared by `gpu-water` and `gpu-underwater`): a heightfield's ground
// drawn under the sky with the water's passes and the view under water over it, on a device of
// its own (its pipelines all made before that device's load ends), read back as a picture.
//
// ge:summary The water labs' scene: ground, sky, the water and the view under it, read back
// ge:keywords lab water scene heightfield picture make_scene view_at gpu-water gpu-underwater
// ge:skill render water labs
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/heightfield.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/target.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "render/frame.hpp"
#include "render/post.hpp"
#include "render/sky.hpp"
#include "render/terrain.hpp"
#include "render/underwater.hpp"
#include "render/water.hpp"

namespace ge::lab::water_scene {

namespace gl = ge::lab;
namespace gr = ge::render;

// GE_GPU_REQUIRE=1: a missing adapter fails the lab (CI's GPU rows) rather than skipping it.
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

// The no-device path: a grey card under each shot id the device path takes, in its order, and
// the device path's two checks at 0. A shot's id enters the digest even when its picture is
// measured, so a row with no adapter (Windows on CI) cuts the same digest as one with a GPU.
inline void placeholders(gl::Lab& lab, const std::vector<std::string>& ids, uint32_t w,
                         uint32_t h) {
  lab.note("No GPU adapter on this machine: the pictures are placeholders.");
  for (const std::string& id : ids) {
    gl::Picture none = gl::create_picture(static_cast<int>(w), static_cast<int>(h), {40, 40, 48});
    gl::draw_text(none, 4, 4, "NO GPU ADAPTER", {230, 230, 230});
    lab.shot({id, id, "", "", {}}, none, gl::Det::measured);
  }
  lab.metric("gpu-errors", 0, "count");
  lab.metric("pipelines-after-load", 0, "count");
}

inline std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

struct Ground {
  ge::core::Heightfield field;
  std::vector<uint8_t> albedo;
};

inline Ground make_ground(float (*height)(double, double), double level, double cell) {
  Ground g;
  g.field = ge::core::create_heightfield(0, 0, cell, 257, 257);
  g.albedo.resize(size_t{257} * 257 * 4);
  for (int r = 0; r < 257; ++r) {
    for (int c = 0; c < 257; ++c) {
      const size_t i = static_cast<size_t>(r) * 257 + static_cast<size_t>(c);
      const float h = height(c * cell, r * cell);
      g.field.data[i] = h;
      // Sand to two metres over the water, grass above it.
      const bool sand = h < level + 2;
      const uint8_t px[4] = {static_cast<uint8_t>(sand ? 196 : 92),
                             static_cast<uint8_t>(sand ? 182 : 118),
                             static_cast<uint8_t>(sand ? 150 : 64), 255};
      std::memcpy(&g.albedo[i * 4], px, 4);
    }
  }
  return g;
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
  gr::Water water;
  gr::Underwater under;
  gr::Post post;
  gr::FrameRecorder rec;
  Ground ground;
  ge::gpu::Target out;
  uint32_t w = 0, h = 0;
};

// A scene on a device of its own, so its pipelines are all made before that device's load ends.
// `paint`, when given, repaints the ground's albedo before the terrain is made of it.
inline bool make_scene(ge::gpu::Device& d, Scene& s, float (*height)(double, double), double level,
                       double cell, uint32_t w, uint32_t h, void (*paint)(Ground&) = nullptr) {
  s.w = w;
  s.h = h;
  s.ground = make_ground(height, level, cell);
  if (paint) paint(s.ground);
  s.targets = gr::create_targets(d, "gpu-water", w, h);
  s.uniforms = gr::make_buffer(d, "gpu-water.uniforms", sizeof(gr::FrameUniforms),
                               wgpu::BufferUsage::Uniform);
  s.layout = gr::frame_layout(d);
  gr::create_sky(d, s.pipelines, s.sky, s.uniforms, s.layout);
  gr::TerrainSource src;
  src.height = &s.ground.field;
  src.albedo = &s.ground.albedo;
  src.base_spacing = 1.0;
  src.levels = 7;
  gr::create_terrain(d, s.pipelines, s.terrain, src, s.layout);
  gr::create_water(d, s.pipelines, s.water, s.layout);
  gr::create_underwater(d, s.pipelines, s.under, s.layout);
  gr::create_post(d, s.pipelines, s.post, s.layout, wgpu::TextureFormat::RGBA8Unorm);
  if (!s.pipelines.finish(d)) return false;
  ge::gpu::end_load(d);
  s.frame = gr::frame_group(d, s.layout, s.uniforms, s.sky);
  gr::create_water_targets(d, s.water, s.targets);
  gr::bind_underwater(d, s.under, s.water);
  s.post_group = gr::post_group(d, s.post, s.targets.hdr_view);
  s.rec.init(d, "gpu-water.timer", 10);
  s.out = ge::gpu::create_target(d, "gpu-water.final", w, h);
  return true;
}

inline gl::Picture draw(ge::gpu::Device& d, Scene& s, const gr::FrameView& v, bool water = true) {
  s.rec.begin_frame(d);
  gr::fill_uniforms(s.staged, v, s.targets.width, s.targets.height);
  d.queue.WriteBuffer(s.uniforms, 0, &s.staged, sizeof s.staged);
  gr::prepare_sky(s.sky, v);
  gr::prepare_terrain(d, s.terrain, v);
  gr::prepare_water(d, s.water, v);
  gr::prepare_underwater(d, s.under, s.water, v, s.targets.height);
  wgpu::CommandEncoder e = d.device.CreateCommandEncoder();
  gr::encode_sky_tables(s.sky, s.pipelines, s.rec, e);
  gr::encode_terrain_cull(s.terrain, s.pipelines, s.rec, e, s.frame);
  gr::encode_terrain_horizon(s.terrain, s.pipelines, s.rec, e);
  gr::encode_terrain_depth(s.terrain, s.pipelines, s.rec, e, s.targets, s.frame);
  gr::encode_terrain(s.terrain, s.pipelines, s.rec, e, s.targets, s.frame);
  gr::encode_sky(s.sky, s.pipelines, s.rec, e, s.targets, s.frame);
  if (water) {
    gr::encode_water(s.water, s.pipelines, s.rec, e, s.targets, s.frame);
    gr::encode_underwater(s.under, s.pipelines, s.rec, e, s.targets, s.frame);
  }
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

// A picture pasted into a sheet at (ox, oy).
inline void paste(gl::Picture& sheet, const gl::Picture& p, int ox, int oy) {
  for (int y = 0; y < p.height; ++y) {
    for (int x = 0; x < p.width; ++x) {
      const size_t s = (static_cast<size_t>(y) * p.width + x) * 4;
      const size_t t = (static_cast<size_t>(oy + y) * sheet.width + ox + x) * 4;
      std::memcpy(&sheet.rgba[t], &p.rgba[s], 4);
    }
  }
}

}  // namespace ge::lab::water_scene
