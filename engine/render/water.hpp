// THE WATER'S SURFACE DRAWN (docs/water.md § Drawn, M24g): a body of water from engine/water
// (`water::Body`: its level, its box, the waves laid on it, its optics) drawn over the opaque
// scene, the reflection, the refraction, the colour and the sun's glitter per pixel.
//
// - THE PASSES: "water.copy" copies the opaque frame's colour and depth (the water reads what
//   is behind it), then "water" draws every body into the HDR and motion targets, testing and
//   writing the depth. They run after the sky and before TAA-U (docs/rendering.md § The frame).
// - THE GEOMETRY: `kWaterLevels`-deep nested squares of kWaterGrid quads round the eye,
//   snapped (no vertex swims), morphed at their seams, the last level's rim out to the horizon;
//   clamped into the body's box. A lake's shore is where the ground stands over the water.
// - THE WAVES are the simulation's own components (water/waves.hpp), the same formula, so the
//   drawn crest is the one a float rides; the DETAIL components (`detail_waves`) carry the
//   spectrum on from the simulation's shortest wave to a few centimetres, for the pixels alone,
//   and the slope variance past them (`WaterDetail::tail_mss`) is the glitter's roughness.
// - THE PICTURE'S TIERS, from the camera: a vertex keeps the waves at least four of its level's
//   spacings long, a pixel the waves at least four pixels long; what each drops becomes
//   roughness, so the far water glints right and never shimmers.
//
// - THE MIRROR: the reflected ray marched in screen space through the depth copy, so the shore,
//   the trees and the mountain are mirrored where the screen holds them; the sky's own model
//   where it does not.
//
// - THE WAKES: the rings a hull, a stroke or a splash set off (water/wake.hpp), summed by the
//   vertex and the pixel in the simulation's formula, each faded as the picture's tiers fade a
//   wave (`set_water_wakes`).
//
// Not yet (M24g2): foam, caustics, the shore's wet band, rain. Under the surface: M24h.
//
// ge:summary The water's surface drawn: the snapped rings, the waves, Fresnel, refraction, glitter
// ge:keywords render water surface ocean lake rings clipmap snapped morph fresnel reflection
// ge:keywords refraction ssr screen-space-reflection glitter beckmann slope-variance
// ge:keywords detail-waves water-copy create_water add_water_body set_water_body
// ge:keywords prepare_water encode_water detail_waves hidden wake rings set_water_wakes kelvin
// ge:skill render water
// ge:test tests/render_water_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "render/frame.hpp"
#include "water/waves.hpp"
#include "water/world.hpp"

namespace ge::render {

inline constexpr uint32_t kWaterGrid = 64;    // a level's quads a side (water.wgsl's override)
inline constexpr int kWaterLevels = 12;       // at most
inline constexpr int kWaterDetailWaves = 32;  // the detail components
inline constexpr int kWaterBodies = 8;        // bodies a Water draws
inline constexpr int kWaterRings = 512;       // a body's wake rings drawn (the newest)

// The detail band past the simulation's waves: components from the laid band's top frequency
// to waves `shortest` m long, and the slope variance of the spectrum from there to `finest` m.
struct WaterDetail {
  std::vector<water::Component> waves;
  double tail_mss = 0;  // the mean square slope past the detail band
  double mss = 0;       // the detail components' own
};
WaterDetail detail_waves(const water::Waves& laid, int count, uint32_t seed, double shortest = 0.04,
                         double finest = 0.004);

// How a body is drawn, beside what engine/water says of it.
struct WaterDraw {
  double base_spacing = 0.5;  // level 0's quad, m
  int levels = 9;             // ≤ kWaterLevels
  double rim = 40;            // the last level's rim pushed out this many times its distance
  double refraction = 0.04;   // how far the normal bends the view into the water
  double calm_mss = 2e-4;     // a calm surface's own roughness (the capillary ripples)
  uint32_t seed = 7;          // the detail components' directions and phases
};

struct WaterGpuBody {
  float info[4];
  float box[4];
  float grid[4];
  float rough[4];
  float c[4], kd[4], deep[4], emission[4];
  float wake[4];  // rings, their life (s), the shortest wave kept (m), 0
  float levels[kWaterLevels][4];
};

struct Water {
  wgpu::BindGroupLayout layout, copy_layout;
  uint32_t p_copy = 0, p_draw = 0;
  wgpu::Texture scene_colour, scene_depth;
  wgpu::TextureView scene_colour_view, scene_depth_view;
  wgpu::BindGroup copy_group;
  wgpu::Buffer indices;
  struct Slot {
    WaterGpuBody staged{};
    WaterDraw draw;
    wgpu::Buffer uniform, waves, rings;
    wgpu::BindGroup group;
    bool hidden = false;           // the caller's switch: not drawn (a lab's dry picture)
    bool shown = true;             // this frame's: not hidden and seen from over its level
    float veil[4] = {0, 0, 0, 0};  // the fog a view under it looks through (water::Optics)
  };
  std::vector<Slot> bodies;
  // The moving water (render/water_stream.hpp): this frame's mesh and the bodies' optics.
  struct Streams {
    wgpu::BindGroupLayout layout;
    uint32_t p_draw = 0;
    wgpu::Buffer vertices, indices, optics;
    uint64_t vertex_bytes = 0, index_bytes = 0;
    uint32_t count = 0;  // indices
    wgpu::BindGroup group;
  } streams;
  int eye_under = -1;  // this frame's: the body the eye is under (render/underwater.hpp), or -1
};

// The pipelines into the manifest.
void create_water(gpu::Device& device, gpu::PipelineSet& pipelines, Water& water,
                  const wgpu::BindGroupLayout& frame);
// The scene's copies at the targets' size (again when the targets change).
void create_water_targets(gpu::Device& device, Water& water, const FrameTargets& targets);
// A body to draw (its waves, box, level and optics read now); its index.
int add_water_body(gpu::Device& device, Water& water, const water::Body& body,
                   const WaterDraw& draw = {});
// The body read again (a lake whose waves were laid again, a new look).
void set_water_body(gpu::Device& device, Water& water, int index, const water::Body& body);
// The body's wake rings (water/wake.hpp: the rings of `body` in the world's `wakes`, the newest
// kWaterRings of them), drawn as the simulation sums them; again whenever rings are set off.
void set_water_wakes(gpu::Device& device, Water& water, int index, const water::Wakes& wakes,
                     int body);
// The levels round this frame's eye, uploaded; a body seen from under its level is not drawn:
// the eye is under it (`Water::eye_under`, render/underwater.hpp's pass).
void prepare_water(gpu::Device& device, Water& water, const FrameView& view);
// "water.copy" and "water" (the still bodies, then the moving water).
void encode_water(const Water& water, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                  wgpu::CommandEncoder& encoder, const FrameTargets& targets,
                  const wgpu::BindGroup& frame);

}  // namespace ge::render
