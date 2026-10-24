// LAVA AND SLIME DRAWN (docs/liquids.md § Drawn): a body of a hot or a thick liquid from
// engine/water (`water::Body`: its level, its box) drawn over the opaque scene by its look, in a
// pass of its own after the water's ("liquid.copy", then "liquid"). Every look is a row of knobs
// a game tunes; the named rows match the liquids of the same name (water/liquids.hpp).
//
// - LAVA (`LavaLook`): the surface's TEMPERATURE is the picture, and its colour and brightness
//   follow from it as a black body's glow (water/glow.hpp's table, Planck under the CIE
//   observer: no colour is picked). The crust is a field of plates (a Voronoi tiling at the
//   plate's size) carried by the flow on two phases of a flow map so it never stretches; each
//   plate has an age, which cools its skin from `crust_hot` to `crust_cold` (dull red to black)
//   and widens the cracks between it and its neighbours, where the melt's own heat shows, falling
//   off exp(-d / w) from the crack's line. Over the plates lie the open patches (a slow noise over
//   `cover`), molten and nearly at `core`, a thin skin wrinkling on them; the crust's edge at a
//   patch is the hottest. Pahoehoe's ropy folds run across the flow on the crust. The crust is a
//   rough black glass lit by the sun and the sky; the melt a smoother one; both glow.
// - SLIME (`SlimeLook`): a turbid volume. Its own colour is the Kubelka-Munk reflectance of a
//   thick layer, R = 1 + K/S - sqrt((K/S)^2 + 2 K/S) per band (K the absorption, S the
//   scattering), so a gel with little scattering shows what lies in it and a toxic ooze is an
//   opaque green; what lies behind is seen through exp(-(K + S) d) of it. On the surface: the
//   wind's ripples cut by the liquid's viscosity (water/liquid.hpp's `ripples`), bubbles that
//   swell and burst into a ring, a scum of patches drifting with the surface, a thin oily film's
//   interference colours (the sheen on tar), a gloss of its own and a glow of its own.
//
// Not yet (docs/liquids.md § Not built yet): reflections of the scene (the sky's alone now), the
// heat shimmer over lava and its light on the walls round it, a flow region's surface.
//
// ge:summary Lava and slime drawn: crust plates, cracks and blackbody glow; turbid slime, bubbles
// ge:keywords render liquid lava slime crust plates cracks voronoi flow-map blackbody glow
// ge:keywords incandescence pahoehoe ropes kubelka-munk turbid bubbles scum iridescence
// ge:keywords thin-film sheen toxic gel mucus sludge tar create_liquid add_liquid_body
// ge:keywords lava_look slime_look liquid_look prepare_liquid encode_liquid
// ge:skill render water
// ge:test tests/render_liquid_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "render/frame.hpp"
#include "water/optics.hpp"
#include "water/world.hpp"

namespace ge::render {

inline constexpr uint32_t kLiquidGrid = 96;  // the surface's quads a side (liquid.wgsl's override)
inline constexpr int kLiquidRipples = 16;    // the ripple components a body draws
inline constexpr int kGlowEntries = 32;      // the glow table's entries
inline constexpr double kGlowLo = 600;       // K: the table's first (under any visible glow)
inline constexpr double kGlowHi = 1700;      // K: its last

enum class LiquidKind : uint8_t { lava, slime };

struct LavaLook {
  double core = 1150;       // degrees C: the melt
  double crust_hot = 820;   // degrees C: a crust's skin as it forms
  double crust_cold = 380;  // degrees C: an old crust's (black in daylight)
  double cover = 0.75;      // the share crusted over
  double plate = 1.6;       // m: a plate's size
  double crack = 0.06;      // m: a young plate's crack, the glow's fall-off from its line
  double ropes = 1;         // the ropy folds on the crust (pahoehoe), 0 none
  double age_rate = 0.02;   // 1/s: how fast a plate ages and is renewed
  double emissivity = 0.9;
  double crust_rough = 0.35;  // the crust's glassy rind
  double melt_rough = 0.12;
  water::Rgb crust_albedo{0.045, 0.041, 0.038};
};

struct SlimeLook {
  water::Rgb absorb{1, 1, 1};   // K, 1/m
  water::Rgb scatter{1, 1, 1};  // S, 1/m
  water::Rgb glow{0, 0, 0};     // its own light, cd/m^2 (a toxic ooze's)
  double rough = 0.08;          // the surface's gloss (a GGX roughness)
  double film = 0;              // nm: an oily film's mean thickness (0 none; 200..600 a sheen)
  double scum = 0;              // the share a scum covers
  water::Rgb scum_albedo{0.2, 0.2, 0.15};
  double bubbles = 0;         // bubbles a square metre a second
  double bubble_size = 0.04;  // m: a bubble's radius when it bursts
  double pulse = 0;           // the glow's slow pulse, a share of it
  double grime = 0;           // 0..1: silt and rot mottled dark over the pool (a sewer's)
  double debris = 0;          // floating bits (twigs, leaves, clumps) a square metre
};

struct LiquidLook {
  LiquidKind kind = LiquidKind::slime;
  LavaLook lava;
  SlimeLook slime;
  double flow[2] = {0, 0};  // m/s: the surface's own motion (a channel's current, the drift)
  // Under a roof (a cave's lava, a sewer's slime): the share of the dome the surface sees the sky
  // through, and the room's own light round it, a share of a white face's luminance under the
  // open day, over the rest.
  double open_sky = 1;
  double room = 0;
};

// The named looks: "lava", "lava-crusted", "slime", "slime-gel", "slime-mucus", "slime-sludge",
// "slime-tar" (the rows of water/liquids.hpp), and nullptr for any other name.
const LiquidLook* find_liquid_look(std::string_view name);
std::span<const std::string_view> liquid_look_names();

struct LiquidGpuBody {
  float info[4];     // level, kind (0 lava, 1 slime), ripples, the eye's footprint scale
  float box[4];      // x0, z0, x1, z1
  float flow[4];     // flow x, z (m/s), the open sky's share, the room's light
  float lava0[4];    // core K, crust_hot K, crust_cold K, cover
  float lava1[4];    // plate, crack, ropes, age_rate
  float lava2[4];    // emissivity, crust_rough, melt_rough, 0
  float crust[4];    // the crust's albedo
  float absorb[4];   // slime K
  float scatter[4];  // slime S
  float glow[4];     // slime's own light, cd/m^2; w: the pulse
  float slime0[4];   // rough, film nm, scum, bubbles
  float slime1[4];   // bubble_size, grime, debris, 0
  float scum[4];     // the scum's albedo
  float table[kGlowEntries][4];  // the glow table, cd/m^2 rgb (water/glow.hpp)
};

struct Liquids {
  wgpu::BindGroupLayout layout, copy_layout;
  uint32_t p_copy = 0, p_draw = 0;
  wgpu::Texture scene_colour, scene_depth;
  wgpu::TextureView scene_colour_view, scene_depth_view;
  wgpu::BindGroup copy_group;
  wgpu::Buffer indices;
  struct Slot {
    LiquidGpuBody staged{};
    wgpu::Buffer uniform, ripples;
    wgpu::BindGroup group;
    bool hidden = false;
    bool shown = true;
  };
  std::vector<Slot> bodies;
};

// The luminance an eye adapts to looking at this look (FrameView::adapt_cd), cd/m^2: `key` times
// its brightest part's (lava's open melt, a slime's glow). At 1 the melt is keyed as a white face
// is; its red is so saturated that the tone curve's shoulder whitens it, so a view of lava keys
// it higher (1.5 holds it orange, as a photograph exposed for the lava does). 0 for a look that
// does not glow.
double adapt_for(const LiquidLook& look, double key);

// The pipelines into the manifest.
void create_liquid(gpu::Device& device, gpu::PipelineSet& pipelines, Liquids& liquids,
                   const wgpu::BindGroupLayout& frame);
// The scene's copies at the targets' size (again when the targets change).
void create_liquid_targets(gpu::Device& device, Liquids& liquids, const FrameTargets& targets);
// A body drawn in `look`, its ripples grown by `wind` m/s toward `toward` over `fetch` m and cut
// by the body's liquid; its index.
int add_liquid_body(gpu::Device& device, Liquids& liquids, const water::Body& body,
                    const LiquidLook& look, double wind = 0, double toward = 0, double fetch = 10);
void set_liquid_body(gpu::Device& device, Liquids& liquids, int index, const water::Body& body,
                     const LiquidLook& look, double wind = 0, double toward = 0, double fetch = 10);
// Uploads this frame's; a body seen from under its level is not drawn.
void prepare_liquid(gpu::Device& device, Liquids& liquids, const FrameView& view);
// "liquid.copy" and "liquid".
void encode_liquid(const Liquids& liquids, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                   wgpu::CommandEncoder& encoder, const FrameTargets& targets,
                   const wgpu::BindGroup& frame);

}  // namespace ge::render
