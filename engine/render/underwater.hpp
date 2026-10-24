// THE VIEW UNDER WATER (docs/water.md § Drawn, M24h): when the eye is under a body's level
// (render/water.hpp's `Water::eye_under`), one full-screen pass ("water.under", after the
// water's own) redraws the picture as seen through the water, from the copies "water.copy"
// made of the opaque frame:
//
// - THE FOG: the scene fades into the water's own veil (water/optics.hpp's `veil`, lit by the
//   day's light dimmed by the eye's depth, exp(-Kd d)) over the path's length, by the body's
//   beam attenuation per band, so red goes first and a murky lake closes in at a few metres.
// - THE BLUR: the bare eye loses most of its focusing power in water, so the scene is blurred
//   by its distance (`blur` px at 1080 rows, half of it at `blur_range` m); a mask sharpens it
//   and magnifies by about a third (`magnify`).
// - THE SURFACE FROM BELOW: a ray reaching the surface before the scene sees through it only
//   within Snell's window, 48.6 degrees round straight up (the world above, dimmed by Fresnel
//   and the water over the eye); beyond it the surface mirrors the water below (total internal
//   reflection: the veil).
// - THE EYE ADAPTS: the picture lifted (at most six times) so the veil's luma reaches `adapt`,
//   as an eye or a camera does in the dark under a brown lake.
// - THE WOBBLE (`warp`, `warp_hz`) and a flat stylised tint (`tint`, `tint_mix`) for a game
//   that wants one.
//
// Presets (`underwater_look`): "eyes-open", "mask", "murky", "classic" (a flat tint and a
// screen warp, no blur, a short clear view). Not yet: light shafts, caustics, particles,
// the waterline split and the lens's drops.
//
// ge:summary The view under water: the water's fog, blur by distance, Snell's window, presets
// ge:keywords render underwater under-water fog veil blur snell window total-internal-reflection
// ge:keywords mask goggles magnify warp tint classic murky create_underwater prepare_underwater
// ge:keywords encode_underwater underwater_look
// ge:skill render water
// ge:test tests/render_water_test.cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "render/frame.hpp"
#include "render/water.hpp"

namespace ge::render {

struct UnderwaterLook {
  double fog = 1;         // times the body's beam attenuation
  double blur = 6;        // px at 1080 rows, at range
  double blur_range = 2;  // m at which half the blur is reached
  double magnify = 1;     // a mask's 1.33
  double warp = 0.0015;   // the wobble, of the screen's height
  double warp_hz = 0.25;  // its rate
  double window = 1;      // Snell's window and the mirror past it (0: the surface as clear air)
  double adapt = 0.08;    // the veil's luma the eye adapts toward (at most 6x); 0: none
  double tint[3] = {1, 1, 1};
  double tint_mix = 0;  // 0: none .. 1: the picture's luma in the tint alone
};

// A preset by name; false (and `out` untouched) for an unknown one.
bool underwater_look(std::string_view name, UnderwaterLook& out);

// What the pass reads, as the shader has it.
struct UnderGpu {
  float info[4];  // the level, the eye's depth, the blur (px at the render's rows), its range
  float c[4], kd[4], veil[4];
  float look[4];  // magnify, warp, warp rate, window
  float tint[4];  // rgb, mix
  float fog[4];   // the fog's scale, the adaptation's target, 0, 0
};

struct Underwater {
  UnderwaterLook look;  // the game's (a mask, the classic tint); the default: eyes open
  wgpu::BindGroupLayout layout;
  uint32_t p_under = 0;
  wgpu::Buffer uniform;
  wgpu::BindGroup group;
  UnderGpu staged{};
  bool active = false;  // this frame's: the eye is under a body
};

void create_underwater(gpu::Device& device, gpu::PipelineSet& pipelines, Underwater& under,
                       const wgpu::BindGroupLayout& frame);
// Bound to the water's copies (again whenever create_water_targets made new ones).
void bind_underwater(gpu::Device& device, Underwater& under, const Water& water);
// After prepare_water: the body the eye is under, its optics and the look, uploaded.
void prepare_underwater(gpu::Device& device, Underwater& under, const Water& water,
                        const FrameView& view, uint32_t render_height);
// "water.under": an empty row when the eye is in the air.
void encode_underwater(const Underwater& under, const gpu::PipelineSet& pipelines,
                       FrameRecorder& rec, wgpu::CommandEncoder& encoder,
                       const FrameTargets& targets, const wgpu::BindGroup& frame);

}  // namespace ge::render
