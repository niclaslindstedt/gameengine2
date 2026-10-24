// engine/render/water.hpp: the detail band carries the spectrum on past the simulation's waves
// (its components shorter than the laid band's, its slope variance growing with the wind, the
// same components for the same seed); and on a device, a sea drawn over a sea floor fills the
// lower half of the frame with water, darker than the sky over it, with no GPU error and no
// pipeline after the load.
//
// ge:summary Tests for the water's surface drawn: the detail band, a sea on a device
// ge:keywords test render water detail-waves spectrum-tail slope-variance gpu sea
// ge:skill render water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "core/heightfield.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/target.hpp"
#include "gpu_support.hpp"
#include "render/frame.hpp"
#include "render/post.hpp"
#include "render/sky.hpp"
#include "render/terrain.hpp"
#include "render/underwater.hpp"
#include "render/water.hpp"
#include "water/world.hpp"
#include "wind/field.hpp"

namespace {

namespace r = ge::render;
namespace gw = ge::water;

gw::World sea(double wind) {
  ge::wind::Field field;
  field.mean = wind;
  gw::World world(field);
  gw::add_ocean(world, 0);
  return world;
}

}  // namespace

TEST(the_underwater_presets_are_named_and_differ_where_they_should) {
  r::UnderwaterLook open, mask, murky, classic, kept;
  CHECK(r::underwater_look("eyes-open", open));
  CHECK(r::underwater_look("mask", mask));
  CHECK(r::underwater_look("murky", murky));
  CHECK(r::underwater_look("classic", classic));
  kept.fog = 7;
  CHECK(!r::underwater_look("no-such-look", kept));
  CHECK(kept.fog == 7);                                // an unknown name leaves it alone
  CHECK(mask.blur < open.blur && mask.magnify > 1.3);  // a mask: sharp and a third larger
  CHECK(murky.fog > open.fog);                         // murky: the view closes in
  CHECK(classic.window == 0 && classic.tint_mix > 0);  // classic: no window, a flat tint
}

TEST(the_detail_band_carries_the_spectrum_past_the_simulations_waves) {
  double last = 0;
  for (const double wind : {3.0, 10.0, 20.0}) {
    const gw::World world = sea(wind);
    const gw::Waves& laid = world.bodies[0].waves;
    const r::WaterDetail a = r::detail_waves(laid, r::kWaterDetailWaves, 7);
    const r::WaterDetail b = r::detail_waves(laid, r::kWaterDetailWaves, 7);
    CHECK(static_cast<int>(a.waves.size()) == r::kWaterDetailWaves);
    double longest_sim = 0;
    for (int i = 0; i < laid.count; ++i) longest_sim = std::fmax(longest_sim, laid.c[i].k);
    for (const gw::Component& c : a.waves) CHECK(c.k > laid.c[0].k * 0.5 && c.a >= 0);
    CHECK(a.waves.back().k > longest_sim);  // past the simulation's shortest
    CHECK(a.mss > last);                    // rougher in a stronger wind
    CHECK(a.tail_mss > 0);
    CHECK(a.waves[3].phase == b.waves[3].phase && a.waves[3].kx == b.waves[3].kx);
    last = a.mss;
  }
}

TEST(a_sea_draws_under_the_sky) {
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
  for (float& h : field.data) h = -30;
  std::vector<uint8_t> albedo(size_t{65} * 65 * 4, 180);
  r::TerrainSource src;
  src.height = &field;
  src.albedo = &albedo;
  src.levels = 6;
  r::Terrain terrain;
  r::create_terrain(d, pipelines, terrain, src, layout);
  r::Water water;
  r::create_water(d, pipelines, water, layout);
  r::Post post;
  r::create_post(d, pipelines, post, layout, wgpu::TextureFormat::RGBA8Unorm);
  CHECK(pipelines.finish(d));
  ge::gpu::end_load(d);
  wgpu::BindGroup frame = r::frame_group(d, layout, uniforms, sky);
  r::create_water_targets(d, water, targets);
  const gw::World world = sea(8);
  r::add_water_body(d, water, world.bodies[0]);
  wgpu::BindGroup pg = r::post_group(d, post, targets.hdr_view);
  r::FrameRecorder rec;
  rec.init(d, "test.timer", 9);
  const ge::gpu::Target out = ge::gpu::create_target(d, "test.final", kW, kH);

  r::FrameView v;
  v.eye[0] = 256;
  v.eye[1] = 10;
  v.eye[2] = 256;
  v.pitch = -0.1;
  v.sun_elevation = 0.5;
  v.time_s = 2;
  r::FrameUniforms staged{};
  rec.begin_frame(d);
  r::fill_uniforms(staged, v, kW, kH);
  d.queue.WriteBuffer(uniforms, 0, &staged, sizeof staged);
  r::prepare_sky(sky, v);
  r::prepare_terrain(d, terrain, v);
  r::prepare_water(d, water, v);
  wgpu::CommandEncoder e = d.device.CreateCommandEncoder();
  r::encode_sky_tables(sky, pipelines, rec, e);
  r::encode_terrain_cull(terrain, pipelines, rec, e, frame);
  r::encode_terrain_horizon(terrain, pipelines, rec, e);
  r::encode_terrain_depth(terrain, pipelines, rec, e, targets, frame);
  r::encode_terrain(terrain, pipelines, rec, e, targets, frame);
  r::encode_sky(sky, pipelines, rec, e, targets, frame);
  r::encode_water(water, pipelines, rec, e, targets, frame);
  r::encode_post(post, pipelines, rec, e, frame, pg, out.view);
  rec.end_frame(e);
  wgpu::CommandBuffer c = e.Finish();
  d.queue.Submit(1, &c);
  rec.after_submit(d);
  const ge::gpu::Image img = ge::gpu::read_now(d, out);
  CHECK(d.errors.load() == 0);
  CHECK(d.pipelines_after_load == 0);
  if (img.rgba.size() != size_t{kW} * kH * 4) return;
  auto lum = [&](uint32_t x, uint32_t y) {
    const size_t i = (size_t{y} * kW + x) * 4;
    return int{img.rgba[i]} + img.rgba[i + 1] + img.rgba[i + 2];
  };
  // The water below the horizon is darker than the sky above it, and bluer than red.
  CHECK(lum(kW / 2, kH - 4) < lum(kW / 2, 2));
  const size_t b = (size_t{kH - 4} * kW + kW / 2) * 4;
  CHECK(img.rgba[b + 2] > img.rgba[b]);
  // The water's depth is in the depth buffer: the distance under the frame's centre-bottom is the
  // sea's, not the sea floor's 30 m further down.
  const std::vector<float> dist = r::read_distance(d, targets, v);
  const float near = dist[size_t{kH - 4} * kW + kW / 2];
  CHECK(near < 60);
}
