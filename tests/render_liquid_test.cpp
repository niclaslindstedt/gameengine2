// engine/render/liquid.hpp: every named look is found and of its kind, the eye keyed to a look's
// brightest part, the body's uniform laid out as the shader reads it; and on a device, a lava
// pool drawn at night under a dark sky glows red-orange, brighter than the ground round it, and a
// slime's ripples are staged only for a liquid thin enough to keep them, with no GPU error and
// no pipeline after the load.
//
// ge:summary Tests for lava and slime drawn: the looks, the eye's key, a lava pool on a device
// ge:keywords test render liquid lava slime looks adapt_for glow-table ripples gpu
// ge:skill render water testing
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "core/heightfield.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/target.hpp"
#include "gpu_support.hpp"
#include "render/frame.hpp"
#include "render/liquid.hpp"
#include "render/post.hpp"
#include "render/sky.hpp"
#include "render/terrain.hpp"
#include "water/liquids.hpp"
#include "water/world.hpp"
#include "wind/field.hpp"

namespace {

namespace r = ge::render;
namespace gw = ge::water;

// A box of `name` 20 m across round (256, 256), its top at 1 m, its box set, under `wind` m/s.
gw::World pool(const std::string& name, double wind) {
  ge::wind::Field field;
  field.mean = wind;
  gw::World world(field);
  gw::Hull h;
  h.planes = {{0, 1, 0, 1},     {0, -1, 0, 2},  {1, 0, 0, 266},
              {-1, 0, 0, -246}, {0, 0, 1, 266}, {0, 0, -1, -246}};
  const std::vector<gw::Hull> hulls{h};
  const int i = gw::add_volume(world, hulls, gw::liquid(name));
  gw::Body& b = world.bodies[static_cast<size_t>(i)];
  b.level = 1;
  b.box[0] = 246;
  b.box[1] = 246;
  b.box[2] = 266;
  b.box[3] = 266;
  gw::lay_waves(world, i, 20);
  return world;
}

}  // namespace

TEST(every_named_look_is_found_and_of_its_kind) {
  CHECK(r::liquid_look_names().size() == 7);
  for (const std::string_view name : r::liquid_look_names()) {
    const r::LiquidLook* look = r::find_liquid_look(name);
    CHECK(look != nullptr);
    if (!look) continue;
    const bool lava = name.substr(0, 4) == "lava";
    CHECK((look->kind == r::LiquidKind::lava) == lava);
    CHECK(gw::find_liquid(name) != nullptr);  // every look draws a named liquid
  }
  CHECK(r::find_liquid_look("no-such-look") == nullptr);
}

TEST(the_eye_is_keyed_to_a_looks_brightest_part) {
  const r::LiquidLook& lava = *r::find_liquid_look("lava");
  const r::LiquidLook& crusted = *r::find_liquid_look("lava-crusted");
  const r::LiquidLook& toxic = *r::find_liquid_look("slime");
  const r::LiquidLook& tar = *r::find_liquid_look("slime-tar");
  // A melt at 1150 C is a few thousand cd/m^2; the cooler crusted lava's melt dimmer.
  CHECK(r::adapt_for(lava, 1) > 1000 && r::adapt_for(lava, 1) < 20000);
  CHECK(r::adapt_for(crusted, 1) < r::adapt_for(lava, 1));
  CHECK(std::fabs(r::adapt_for(lava, 1.5) - 1.5 * r::adapt_for(lava, 1)) < 1e-9);
  // The toxic slime glows; the tar does not.
  CHECK(r::adapt_for(toxic, 1) > 10);
  CHECK(r::adapt_for(tar, 1) == 0);
}

TEST(the_bodys_uniform_is_laid_out_in_vec4s) {
  CHECK(sizeof(r::LiquidGpuBody) % 16 == 0);
  CHECK(sizeof(r::LiquidGpuBody) == size_t{13 + r::kGlowEntries} * 16);
}

TEST(a_lava_pool_glows_at_night) {
  auto device = ge::test::test_device();
  if (!device) return;
  ge::gpu::Device& d = *device;
  constexpr uint32_t kW = 64, kH = 48;
  ge::gpu::PipelineSet pipelines;
  r::FrameTargets targets = r::create_targets(d, "test", kW, kH);
  wgpu::Buffer uniforms =
      r::make_buffer(d, "test.uniforms", sizeof(r::FrameUniforms), wgpu::BufferUsage::Uniform);
  wgpu::BindGroupLayout layout = r::frame_layout(d);
  r::Sky sky;
  r::create_sky(d, pipelines, sky, uniforms, layout);
  ge::core::Heightfield field = ge::core::create_heightfield(0, 0, 8, 65, 65);
  for (float& h : field.data) h = 0;
  std::vector<uint8_t> albedo(size_t{65} * 65 * 4, 120);
  r::TerrainSource src;
  src.height = &field;
  src.albedo = &albedo;
  src.levels = 6;
  r::Terrain terrain;
  r::create_terrain(d, pipelines, terrain, src, layout);
  r::Liquids liquids;
  r::create_liquid(d, pipelines, liquids, layout);
  r::Post post;
  r::create_post(d, pipelines, post, layout, wgpu::TextureFormat::RGBA8Unorm);
  CHECK(pipelines.finish(d));
  ge::gpu::end_load(d);
  wgpu::BindGroup frame = r::frame_group(d, layout, uniforms, sky);
  r::create_liquid_targets(d, liquids, targets);
  const r::LiquidLook& look = *r::find_liquid_look("lava");
  const gw::World lava = pool("lava", 10);
  CHECK(r::add_liquid_body(d, liquids, lava.bodies[0], look, 10, 0, 20) == 0);
  CHECK(liquids.bodies[0].staged.info[1] == 0);  // lava
  CHECK(liquids.bodies[0].staged.info[2] == 0);  // no ripples on lava
  // The glow table brightens with temperature.
  const auto& table = liquids.bodies[0].staged.table;
  for (int i = 1; i < r::kGlowEntries; ++i) CHECK(table[i][1] >= table[i - 1][1]);
  // A runny slime keeps the gale's ripples, a thick one none.
  {
    r::Liquids probe;
    probe.layout = liquids.layout;
    const gw::World mucus = pool("slime-mucus", 18);
    const gw::World gel = pool("slime-gel", 18);
    r::add_liquid_body(d, probe, mucus.bodies[0], *r::find_liquid_look("slime-mucus"), 18, 0, 20);
    r::add_liquid_body(d, probe, gel.bodies[0], *r::find_liquid_look("slime-gel"), 18, 0, 20);
    CHECK(probe.bodies[0].staged.info[2] > 0);
    CHECK(probe.bodies[1].staged.info[2] == 0);
  }
  wgpu::BindGroup pg = r::post_group(d, post, targets.hdr_view);
  r::FrameRecorder rec;
  rec.init(d, "test.timer", 9);
  const ge::gpu::Target out = ge::gpu::create_target(d, "test.final", kW, kH);

  r::FrameView v;
  v.eye[0] = 256;
  v.eye[1] = 12;
  v.eye[2] = 226;
  v.pitch = -0.35;
  v.sun_elevation = -0.4;
  v.time_s = 2;
  v.adapt_cd = r::adapt_for(look, 1.5);
  r::FrameUniforms staged{};
  rec.begin_frame(d);
  r::fill_uniforms(staged, v, kW, kH);
  d.queue.WriteBuffer(uniforms, 0, &staged, sizeof staged);
  r::prepare_sky(sky, v);
  r::prepare_terrain(d, terrain, v);
  r::prepare_liquid(d, liquids, v);
  wgpu::CommandEncoder e = d.device.CreateCommandEncoder();
  r::encode_sky_tables(sky, pipelines, rec, e);
  r::encode_terrain_cull(terrain, pipelines, rec, e, frame);
  r::encode_terrain_horizon(terrain, pipelines, rec, e);
  r::encode_terrain_depth(terrain, pipelines, rec, e, targets, frame);
  r::encode_terrain(terrain, pipelines, rec, e, targets, frame);
  r::encode_sky(sky, pipelines, rec, e, targets, frame);
  r::encode_liquid(liquids, pipelines, rec, e, targets, frame);
  r::encode_post(post, pipelines, rec, e, frame, pg, out.view);
  rec.end_frame(e);
  wgpu::CommandBuffer c = e.Finish();
  d.queue.Submit(1, &c);
  rec.after_submit(d);
  const ge::gpu::Image img = ge::gpu::read_now(d, out);
  CHECK(d.errors.load() == 0);
  CHECK(d.pipelines_after_load == 0);
  if (img.rgba.size() != size_t{kW} * kH * 4) return;
  // The frame's middle (the pool, mostly dark crust at this size) glows red-orange, its cracks
  // bright; its bottom row (the ground at the eye's feet, short of the pool) is dark.
  double red = 0, green = 0, blue = 0, ground = 0, peak = 0;
  for (uint32_t y = kH / 2 - 4; y < kH / 2 + 4; ++y) {
    for (uint32_t x = kW / 2 - 6; x < kW / 2 + 6; ++x) {
      const size_t i = (size_t{y} * kW + x) * 4;
      red += img.rgba[i];
      peak = std::fmax(peak, img.rgba[i]);
      green += img.rgba[i + 1];
      blue += img.rgba[i + 2];
    }
  }
  for (uint32_t x = 0; x < kW; ++x) ground += img.rgba[(size_t{kH - 1} * kW + x) * 4];
  CHECK(red > green && green > blue);
  CHECK(peak > 80);  // a crack or the open melt, bright
  CHECK(red / 96 > ground / kW + 5);
}
