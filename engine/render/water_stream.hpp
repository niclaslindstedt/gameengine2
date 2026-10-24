// MOVING WATER DRAWN (docs/water.md § Drawn, M24i): the water that runs, drawn over the opaque
// scene in the "water" pass after the still bodies: a CHANNEL (a river, a stream) as a ribbon
// down its centreline, a FLOW region (water/flow.hpp) as its grid of wet cells, and each
// WATERFALL's sheet as a curtain along its ballistic path. Everything is built on the CPU from
// the one query (`water::sample`) and the flow's own cells and sheets, each frame, so the drawn
// water is where the simulation's is.
//
// - THE SURFACE: the mesh's own slope (a river falling down its bed, a flood's front), and the
//   ripples a current carries: a few short waves read at the point moved back along the current
//   by a phase of a cycle, in two phases half a cycle apart, each weighed by how far it is from
//   its own reset (a FLOW MAP in two phases: the pattern runs downstream at the water's own
//   speed, fastest mid-stream, and never stretches); rougher where the water runs faster.
// - FOAM: where the water runs fast and shallow, where the flow is supercritical (a Froude number
//   over one: white water), and round where a sheet lands; drawn as lace (a pattern carried by
//   the same flow map, cut to the share), never a white sheet.
// - THE CURTAIN: a ribbon along each sheet's path from the lip to where it lands, as wide as the
//   lip, aerated white with streaks running down it at the sheet's speed.
// - THE MIST AND THE SPRAY: births for the particle pool (render/particles.hpp) where each sheet
//   lands (`waterfall_spray`) and where a splash goes through the surface (`splash_spray`), drawn
//   off a `core::Rng` the caller keeps, so the picture is the same on every machine.
// - THE LIGHT is the still water's (render/wgsl/water_light.wgsl): the mirror, the water seen
//   into with the body's optics, the glitter, and the foam over it.
//
// ge:summary Moving water drawn: river ribbons, flow grids, waterfall curtains, flow maps, foam
// ge:keywords render water river stream channel flow flow-map two-phase ripples foam lace
// ge:keywords waterfall curtain sheet mist spray white-water froude build_water_streams
// ge:keywords prepare_water_streams waterfall_spray splash_spray
// ge:skill render water
// ge:test tests/render_water_stream_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "core/prng.hpp"
#include "render/particles.hpp"
#include "render/water.hpp"
#include "water/float.hpp"
#include "water/world.hpp"

namespace ge::render {

inline constexpr int kStreamBodies = 16;  // bodies whose optics a frame's streams read

// One vertex of moving water, as water_stream.wgsl reads it.
struct StreamVertex {
  float p[3];   // world m
  float foam;   // the surface's foam share, 0..1; a curtain's distance across it, m
  float v[2];   // the surface's current, m/s; a curtain's speed down it and 0
  float along;  // a curtain's distance down its path from the lip, m; the surface's depth, m
  float tag;    // the body's index times 2, plus 1 on a curtain
};
static_assert(sizeof(StreamVertex) == 32, "match water_stream.wgsl's vertex");

struct StreamMesh {
  std::vector<StreamVertex> vertices;
  std::vector<uint32_t> indices;
};

// The ribbons, the grids and the curtains of every channel and flow body in `world` at `t`
// (bodies past kStreamBodies are left out). A channel's ribbon has a row every `step` m.
void build_water_streams(const water::World& world, double t, StreamMesh& out, double step = 1.0);

// The particle births of the sheets landing over `dt` s: a fine mist and heavier spray, their
// number by the discharge.
void waterfall_spray(const water::Flow& flow, double dt, core::Rng& rng,
                     std::vector<Particle>& out);
// A splash's spray: drops thrown up and out by its speed and size.
void splash_spray(const water::Splash& splash, core::Rng& rng, std::vector<Particle>& out);

// This frame's moving water uploaded (`mesh` from `build_water_streams` over `world`), drawn by
// encode_water's "water" pass after the still bodies.
void prepare_water_streams(gpu::Device& device, Water& water, const StreamMesh& mesh,
                           const water::World& world);

}  // namespace ge::render
