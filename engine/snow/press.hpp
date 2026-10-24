// A CONTACT LEAVES AN IMPRINT (docs/snow.md § `press`). Every contact a game simulates (a ski, a
// boot, a tyre's patch, a tread, a body) calls `press` once a step with its outline on the plan
// and what it puts through the snow, and the layer keeps what that did:
//
// - THE TARGET: under every sample the outline covers, the depth the snow is pressed down to,
//   measured from the UNTOUCHED cover there. Either the caller says how far its bottom stands
//   under the untouched surface (`sink`: a physics that has its own law, the skier's planing),
//   or the layer works it out from the pressure (`load` over `area`) with the contact laws
//   (contact.hpp, `column_sink`): Bekker's sink for the outline's width, capped by what the
//   column can compact before it reaches the density that pressure packs snow to
//   (`reach_density`). Across the outline the target eases as 1 - u^wall (u: 0 on the axis, 1 at
//   the edge), a round groove at wall 2, a square one at 10.
// - MASS IS KEPT. A column above its target comes down to it. Of the snow cut away, the share
//   `side_push` is THROWN ASIDE (a skid's band, a plough's bow wave, a tyre's spoil) and the rest
//   COMPACTED into what stays; a column is never packed past its kind's cap, so what would be
//   denser than that is thrown too. The thrown mass is laid as a BERM over the ring just outside
//   the outline (out to `berm_reach` of its half width again), heaped toward the edge, to the unit:
//   the mass the tiles hold, plus what spilled off the region, is always what they started with.
// - THE DEEPER OF TWO PASSES: a column already at or under its target is left alone, so a
//   contact standing still, or a groove ridden twice, presses nothing more.
//
// An outline narrower than the layer's samples (a 7 cm ski on 10 cm cells) is read three quarters
// of a sample wide, so it always reaches one, with its pressure its own; its berm reaches at least
// a sample and a half out. The fine marks keep the true width.
//
// Every press appends an IMPRINT RECORD (layer.hpp) to the layer's list for the readers that are
// not the physics: the fine marks, the spray and the crunch read where it was and what it threw.
// A press visits only the samples under its outline's box, and allocates only a tile it is the
// first to touch (and a record past the reserve, never: they are dropped and counted).
//
// The density a pressure packs snow to rises with the log of the pressure, about 350 kg/m3 under
// a boot's 10 kPa and 480 under a tyre's 100; it is the cap on Bekker's sink, never the sink.
//
// ge:summary press: a contact's imprint in a snow layer, mass kept, the berm thrown, a record
// ge:keywords snow press imprint footprint groove rut trench crater berm compaction mass-kept
// ge:keywords capsule disc box ellipse sink load pressure side_push reach_density motion
// ge:skill snow
// ge:test tests/snow_press_test.cpp
#pragma once

#include "snow/layer.hpp"

namespace ge::snow {

inline Shape capsule(double ax, double az, double bx, double bz, double half_width) {
  return {ShapeKind::capsule, ax, az, bx, bz, half_width, 0, 0};
}
inline Shape disc(double x, double z, double radius) {
  return {ShapeKind::disc, x, z, x, z, radius, 0, 0};
}
inline Shape box(double x, double z, double heading, double half_length, double half_width) {
  return {ShapeKind::box, x, z, x, z, half_width, half_length, heading};
}
inline Shape ellipse(double x, double z, double heading, double half_length, double half_width) {
  return {ShapeKind::ellipse, x, z, x, z, half_width, half_length, heading};
}

// The outline's area, m2.
double shape_area(const Shape& s);
// Where (x, z) lies across the outline: 0 on its axis, 1 on its edge, more outside.
double shape_u(const Shape& s, double x, double z);

struct Imprint {
  Shape shape;
  double load = 0;        // N through the contact
  double area = 0;        // m2 of it on the snow; 0 is the outline's own
  double sink = -1;       // m its bottom stands under the untouched surface; < 0: from the load
  double side_push = -1;  // 0..1 of the cut snow thrown aside; < 0: the motion's default
  int wall = 6;           // the cross-section's power
  double berm_reach = 0.9;
  Motion motion = Motion::step;
};

// The share of the cut snow a motion throws aside when the imprint does not say: a step and a
// roll press, a slide and a plough push, a dig throws all of it.
double default_side_push(Motion m);

// The density kg/m3 a pressure `pa` Pa compacts snow to (no denser than `cap`).
double reach_density(double pa, double cap);

struct Pressed {
  double sink = 0;       // m the deepest sample pressed stands under the untouched surface
  double pressure = 0;   // Pa
  double compacted = 0;  // kg pressed into the columns that stayed (each sample a cell square)
  double thrown = 0;     // kg thrown aside
  double spilled = 0;    // kg of it that found no sample to land on
  int samples = 0;       // samples brought down
  int refused = 0;       // samples left as they were: no tile left in the budget
};

Pressed press(Layer& layer, const Imprint& imp, double t);

}  // namespace ge::snow
