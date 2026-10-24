// GRASS DRAWN BLADE BY BLADE (docs/grass.md § Drawn): a cover of triangles (engine/grass's
// `Cover`: a heightfield's region, a level's floors) grown into blades on the GPU every frame and
// drawn as curved, tapering, two-sided strips that sway in the engine's wind.
//
// - THE SCATTER ("grass.scatter", render/grass_scatter.wgsl): one workgroup a triangle, culled by
//   the frustum and the reach, its blades scattered exactly as engine/grass's `scatter_one` does
//   (so the CPU knows every blade the GPU draws) and kept by rank against the distance's share:
//   all within `full_m`, (full_m / d)² of them past it, none past `reach_m`; a blade near its
//   threshold or the reach shrinks to nothing rather than popping, and the fewer far blades widen
//   so the field keeps its cover. A kept blade goes to the FINE list (within `near_m`, drawn with
//   `near_segments` segments) or the COARSE one (`far_segments`), each counted into its own
//   indirect draw and held to `capacity`. Nothing about a blade is stored between frames.
// - THE AIR: `prepare_grass` samples the engine's wind round the eye into a grid
//   (engine/grass/sway.hpp's `fill_wind_grid`: the field's air at the blades' height, sheltered,
//   with every local source, its gusts carried downwind as fronts) that the vertex stage reads.
// - THE DRAW ("grass", render/grass.wgsl): after the ground's colour and before the sky, with
//   depth written. Each vertex bends its blade's Bézier in the air at its root (sway.hpp's
//   `blade_shape`, its WGSL twin), never thinner than about a pixel, turned toward the eye when
//   edge-on. Lit two-sided by the sun past the ground's horizon and the shadow maps, through the
//   blade from behind, by the sky dimmed toward the root, hazed; the motion target as a solid
//   surface's (the camera's motion; a blade's own sway is not a motion vector yet).
// - FAR AWAY the blades are gone and the ground's own albedo carries the field: a game paints it
//   with each kind's `far_colour` (engine/grass/kind.hpp) under the cover.
//
// ge:summary Grass drawn blade by blade: GPU scatter, distance thinning, Bézier blades in the wind
// ge:keywords render grass blades scatter cover thinning lod reach indirect-draw triangle-strip
// ge:keywords bezier sway wind-grid translucency two-sided create_grass set_grass_cover
// ge:keywords prepare_grass encode_grass_scatter encode_grass read_grass_blades probe_grass_shapes
// ge:skill render grass
// ge:test tests/render_grass_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "grass/cover.hpp"
#include "grass/sway.hpp"
#include "render/frame.hpp"
#include "render/terrain.hpp"
#include "render/view.hpp"

namespace ge::render {

struct GrassSettings {
  uint32_t capacity = 1u << 20;  // blades in each list a frame
  float full_m = 10;             // every blade kept within it
  float near_m = 14;             // fine blades within it
  float reach_m = 90;            // no blade past it
  float min_keep = 0.01f;        // the least share kept anywhere within the reach
  uint32_t near_segments = 5;
  uint32_t far_segments = 2;
  int wind_nodes = 32;  // the wind grid's nodes a side, over twice the reach
  bool probe = false;   // tests: build the shapes probe
};

struct Grass {
  GrassSettings settings;
  uint32_t tri_count = 0;
  grass::WindGrid grid;
  wgpu::Buffer params, tris, kinds, wind, near_blades, far_blades, args;
  wgpu::BindGroupLayout scatter_layout, draw_layout, light_layout, probe_layout;
  wgpu::BindGroup scatter_group, draw_group, light_group, shadows;
  uint32_t p_scatter = 0, p_clamp = 0, p_near = 0, p_far = 0, p_shapes = 0;
  float time = 0;
};

// The buffers and pipelines (into `pipelines`, before the load ends), with an empty cover;
// `terrain` lends its horizon maps to the light. `frame` is group 0's layout.
void create_grass(gpu::Device& device, gpu::PipelineSet& pipelines, Grass& grass,
                  const wgpu::BindGroupLayout& frame, const Terrain& terrain,
                  const GrassSettings& settings = {});

// The cover the grass grows on (its triangles and up to grass::kMaxKinds kinds), replacing the
// last. Returns false (and keeps the last) when the cover has too many kinds.
bool set_grass_cover(gpu::Device& device, Grass& grass, const grass::Cover& cover);

void bind_grass_shadows(Grass& grass, const wgpu::BindGroup& shadows);

// The frame's uploads: the wind grid round the eye at `t` s from `air` (no field: still air), the
// params, the lists' counts reset.
void prepare_grass(gpu::Device& device, Grass& grass, const FrameView& view,
                   const grass::GrassAir& air, double t);

// "grass.scatter" (before the draw, after the frame's uniforms are written) and "grass" (after the
// ground's colour, before the sky).
void encode_grass_scatter(const Grass& grass, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                          wgpu::CommandEncoder& encoder, const wgpu::BindGroup& frame);
void encode_grass(const Grass& grass, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                  wgpu::CommandEncoder& encoder, const FrameTargets& targets,
                  const wgpu::BindGroup& frame);

// Labs and tests (blocks): the blades the last scatter kept, fine and coarse.
struct GrassLists {
  std::vector<grass::Blade> near, far;
  uint32_t near_count = 0, far_count = 0;  // as counted, before the capacity's clamp
};
GrassLists read_grass_blades(gpu::Device& device, const Grass& grass);

// Tests (blocks; `GrassSettings::probe`): the shape and the air the GPU gives each fine blade at
// the params' time, as three points a blade (the air's x and z in the first two's w).
std::vector<float> probe_grass_shapes(gpu::Device& device, const gpu::PipelineSet& pipelines,
                                      const Grass& grass, const wgpu::BindGroup& frame);

}  // namespace ge::render
