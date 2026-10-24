// THE BSP RENDERER (r_bspmode): a classic map (draw_data.hpp's `Scene`) drawn into a frame's
// targets by one of two paths, picked per frame:
//
// - CLASSIC: the map as it was made to look: its textures times its lightmaps, its liquids as
//   their own warped textures and its sky faces as the sky texture's two scrolling layers.
// - V2: the same surfaces under the engine's world: the sky faces show our sky and clouds (they
//   write a mask, and a fix-up pass gives their pixels the cleared depth the sky pass draws
//   over); the liquids are bodies of our water, lava and slime (bsp_liquids.hpp), not drawn here;
//   and the light is the lightmaps' or REAL-TIME: the map's light entities as point lights (each
//   one's flicker, the nearest `shadowed` with a cube of depth), the lava's glow as lights over
//   it, the sun through the sky faces with its own shadow map, the sky's light by how open a
//   point is overhead, and the glowing texels.
//
// The passes, in a frame's order: "bsp.shadows" (v2 with lights), "bsp", "bsp.sky" (v2); then the
// caller's sky, clouds, water and liquids over the frame's targets.
//
// ge:summary The BSP renderer: classic or v2 (our sky, our liquids, real-time lights and shadows)
// ge:keywords bsp brush render renderer r_bspmode classic v2 lightmap point-light cube-shadow
// ge:keywords sun-shadow sky-mask flicker lava-light
// ge:skill brush render
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "brush/app/draw_data.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "render/frame.hpp"

namespace ge::brush::app {

enum class Mode : uint8_t { classic, v2 };
enum class Lighting : uint8_t { lightmaps, lights };
// "classic" or "v2"; false for any other word.
bool parse_mode(std::string_view word, Mode* out);

struct Options {
  Mode mode = Mode::v2;
  Lighting lighting = Lighting::lights;
  double ambient = 0.015;   // the light every face gets, the lightmaps' units
  double brightness = 2.0;  // a lightmap's 1 against a white face under the open day
  double strength = 1;      // the point lights' strength against the classic's
  int shadowed = 32;        // the nearest lights that cast shadows
  int most = 160;           // the lights a frame draws at most (the nearest)
  bool lava_lights = true;  // the lava's glow lights the walls round it
  // r_minlight: the light no face falls below, the lightmaps' units, as a soft floor
  // (sqrt(e^2 + m^2)): a dark room comes up to about `minlight`, a lit one keeps its light.
  double minlight = 0.3;
  // r_bounce: the share of the map's baked light added to the real-time lights as the light
  // bounced round the room (the lightmaps as light probes: the room's own light, not flat grey).
  double bounce = 0.35;
};

inline constexpr uint32_t kCubeSize = 256;    // a cube face's texels a side
inline constexpr uint32_t kOrthoSize = 2048;  // the sun's and the zenith's maps
inline constexpr int kMaxShadowed = 48;

struct GpuLight {
  float pos[4];
  float colour[4];
  float info[4];
};

struct GpuJob {
  float o[4], x[4], y[4], z[4], info[4];
};

struct GpuBsp {
  float info[4];
  float info2[4];
  float sky[4];
  float info3[4];  // the floor (minlight), the bounce
  GpuJob ortho[2];
};

struct BspDraw {
  const Scene* scene = nullptr;
  wgpu::Buffer vertices, indices;
  // Index ranges by kind (draw_data.hpp's `Kind`): first, count.
  uint32_t range[kKinds][2] = {};
  wgpu::Texture atlas, light;
  wgpu::Buffer uniform, lights, jobs;
  wgpu::Texture cubes, orthos;
  std::vector<wgpu::TextureView> cube_layers;  // a view a cube face
  wgpu::TextureView ortho_layers[2];
  wgpu::TextureView cubes_view, orthos_view;
  wgpu::Sampler light_sampler, compare;
  wgpu::BindGroupLayout layout, fix_layout, shadow_layout;
  wgpu::BindGroup group, fix_group, shadow_group;
  wgpu::Texture mask;
  wgpu::TextureView mask_view;
  uint32_t p_draw = 0, p_fix = 0, p_shadow = 0;
  // This frame's (prepare_bsp).
  GpuBsp staged{};
  std::vector<GpuLight> frame_lights;
  std::vector<GpuJob> frame_jobs;  // the cube faces, then the two orthographic maps
  int shadowed = 0;
  Options options;
  // The scene's lava lights (made once).
  std::vector<PointLight> lava;
};

void create_bsp(gpu::Device& device, gpu::PipelineSet& pipelines, BspDraw& draw, const Scene& scene,
                const wgpu::BindGroupLayout& frame);
// The mask at the targets' size (again when they change).
void create_bsp_targets(gpu::Device& device, BspDraw& draw, const render::FrameTargets& targets);
// This frame's lights (picked round the eye, flickered at the view's time) and shadow cameras.
void prepare_bsp(gpu::Device& device, BspDraw& draw, const render::FrameView& view,
                 const Options& options);
// "bsp.shadows", "bsp" and "bsp.sky": the map into the cleared targets.
void encode_bsp(const BspDraw& draw, const gpu::PipelineSet& pipelines, render::FrameRecorder& rec,
                wgpu::CommandEncoder& encoder, const render::FrameTargets& targets,
                const wgpu::BindGroup& frame);

// The lights the scene's lava gives off: a grid over each pool's top, each its share of the
// pool's glow (physical, cd), just over the surface. `core_c` the melt's temperature, `cover` the
// share crusted.
std::vector<PointLight> lava_lights(const Scene& scene, double core_c = 1150, double cover = 0.75);

}  // namespace ge::brush::app
