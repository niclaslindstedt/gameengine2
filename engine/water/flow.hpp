// FLOWING WATER (docs/water.md § Flowing water): a FLOW REGION is the shallow-water equations on
// a uniform grid over a bed, for water that has somewhere new to go: a wall broken, a basin
// emptying, a flood spreading, a river over a lip.
//
// THE SCHEME (finite volumes). Each cell holds its water as an exact count of volume quanta
// (`kQuantum` m^3) and its momentum a unit area (depth times velocity). Across each face the
// flux of water and momentum is the HLL solution of the Riemann problem between the cells, over
// the hydrostatic reconstruction of the bed (the face's bed the higher of the two, each side's
// depth what stands over it, and the bed's push on each side), so still water over any bed
// stays still, a front runs onto a dry bed at its own speed, and a dam break meets Ritter's
// solution. The water through a face is turned into a count of quanta taken from one cell and
// given to the other, the donor's outflows scaled so it never gives more than it holds, so the
// total is held to the bit. The bed's roughness (`manning`) and a loss of `loss` velocity heads
// a cell slow the momentum. Sub-steps hold the CFL condition (dt <= 0.4 dx / (|u| + sqrt(g h))).
// Each sub-step is passes over the rows, each row reading only the previous pass, so with
// `workers` > 1 the rows run side by side and give the same bits.
//
// THE BED is a height per cell (`set_bed`); a wall is bed raised to its top, and BREAKING A WALL
// IS LOWERING ITS CELLS. `fill_to` stands still water at a level over the bed (a lake woken into
// a region). SOURCES add a discharge at a cell (a spring, an inflow, rain); DRAINS take what a
// hole of an area lets through, Torricelli's Cd A sqrt(2 g h); each of the region's edges is a
// wall, or open (`FlowSpec::open`: the water leaves over it as over a free overfall).
//
// WATERFALLS. Where a face drops further than `fall_drop` below a cell's surface, the water over
// it leaves the grid as a sheet: it flies on a ballistic path from the lip at its velocity and
// lands, its volume intact, where the path first meets the water or the bed, after its time in
// the air. `Flow::falls` lists this step's sheets (lip, velocity, discharge, landing) for the
// curtain and the mist.
//
// LEVEL OF DETAIL, from the game's actors (never the camera): `set_tier` steps the region at its
// cell (tier 0), its cells merged 2x2 at half the rate (tier 1) or 4x4 at a quarter (tier 2); a
// merge adds the counts, a split levels each coarse cell's water over its fine beds, so tiers
// change with the volume held to the bit. `tier_for` picks a tier from the actors with
// hysteresis. At the far tier a region whose water has settled (no source, drain or sheet, and
// nothing faster than 1 mm/s) falls `asleep`, its state held and costing nothing; a nearer
// tier, a source, a drain or a breach (`set_bed`) wakes it.
//
// The region's surface and velocity are what `water::sample` returns inside it (world.hpp's
// `add_flow`), so floats drift on its current and swimmers are swept.
//
// ge:summary Flowing water: the shallow-water grid, walls broken, drains, waterfalls, its tiers
// ge:keywords water flow shallow-water finite-volume hll grid flood dam-break breach wall bed
// ge:keywords drain source spring torricelli weir waterfall sheet lip cfl conservation quanta
// ge:keywords lod tier coarsen refine sleep step_flow
// ge:skill water
// ge:test tests/water_flow_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ge::water {

inline constexpr double kQuantum = 1e-9;  // m^3, a flow region's unit of volume
inline constexpr int kTiers = 3;          // the cell, 2x2 merged, 4x4 merged

// The region's edges the water may leave over (`FlowSpec::open`).
inline constexpr int kWest = 1, kEast = 2, kSouth = 4, kNorth = 8, kEveryEdge = 15;

struct FlowSpec {
  int nx = 64, nz = 64;      // cells (multiples of 4)
  double cell = 1;           // m
  double x0 = 0, z0 = 0;     // the region's corner in the world's plan
  double gravity = 9.80665;  // m/s^2
  double loss = 0;           // velocity heads lost a cell (0: frictionless)
  double manning = 0.03;     // the bed's roughness, s/m^(1/3)
  int open = 0;              // the edges water leaves over (kWest | kEast ...); others walls
  double fall_drop = 1.5;    // m below the surface at which a face is a lip
  int workers = 1;           // rows side by side (the same bits for any number)
};

struct Source {
  double x = 0, z = 0;  // where, world plan
  double rate = 0;      // m^3/s
};

struct Drain {
  double x = 0, z = 0;
  double area = 0.05;  // m^2, the hole
  double cd = 0.6;     // its discharge coefficient
};

// One grid of the region: the cells at a tier.
struct FlowGrid {
  int nx = 0, nz = 0;
  double cell = 1;
  std::vector<double> bed;     // m
  std::vector<int64_t> vol;    // quanta
  std::vector<double> qx, qz;  // the momentum a unit area (depth times velocity), m^2/s
  // A coarse tier's crest on each face (x faces (nx + 1) a row, z faces nx a row for nz + 1
  // rows), m: the mean of the finer faces' crests along it, so a wall thinner than a coarse
  // cell still holds its water. Empty at the cell's own tier, where a face's crest is its beds.
  std::vector<double> sx, sz;
};

// A sheet of water over a lip this step.
struct Fall {
  double x = 0, y = 0, z = 0;  // the lip
  double vx = 0, vz = 0;       // the sheet's speed leaving it, m/s
  double discharge = 0;        // m^3/s
  double land_x = 0, land_y = 0, land_z = 0;
  double time = 0;  // s in the air
};

struct Flow {
  FlowSpec spec;
  FlowGrid grids[kTiers];  // [0] the cell; the active one is `grids[tier]`
  int tier = 0;
  bool asleep = false;
  std::vector<Source> sources;
  std::vector<Drain> drains;
  std::vector<Fall> falls;
  struct Packet {
    int64_t vol = 0;
    int cell = 0;     // where it lands, at the tier it left
    double time = 0;  // s until it lands
  };
  std::vector<Packet> air;
  std::vector<double> carry;       // the sources' fractions of a quantum
  int64_t added = 0, drained = 0;  // quanta in through sources, out through drains and edges
  double t = 0;
  int phase = 0;  // steps since the last coarse step
};

Flow make_flow(const FlowSpec& spec);
// The bed over [x0, x1) x [z0, z1) (world plan) set to `height` (a wall raised, a wall broken);
// the water there keeps its volume.
void set_bed(Flow& flow, double x0, double z0, double x1, double z1, double height);
// The bed of every cell from a function of the plan (a heightfield, a brush world's columns).
void set_bed(Flow& flow, double (*height)(const void* context, double x, double z),
             const void* context);
// Still water to `level` over [x0, x1) x [z0, z1).
void fill_to(Flow& flow, double x0, double z0, double x1, double z1, double level);

// One step of `dt` s.
void step_flow(Flow& flow, double dt);

// The water in the region, quanta (the cells and the sheets in the air).
int64_t volume(const Flow& flow);
// The depth, the surface and the velocity at a world point (depth 0: dry or outside).
struct FlowSample {
  bool inside = false;
  double depth = 0, surface = 0, bed = 0;
  double vx = 0, vz = 0;
};
FlowSample flow_at(const Flow& flow, double x, double z);
// The largest speed in the region, m/s (a settled region is near 0).
double flow_speed(const Flow& flow);

// The level of detail. `set_tier` merges or splits the cells, holding the volume to the bit.
void set_tier(Flow& flow, int tier);
// The tier the actors (x, z pairs) ask for, near < `near` m (tier 0), mid < `mid` (tier 1),
// farther tier 2, with `band` m of hysteresis about the current tier's edges.
int tier_for(const Flow& flow, std::span<const double> actors, double near, double mid,
             double band = 10);

}  // namespace ge::water
