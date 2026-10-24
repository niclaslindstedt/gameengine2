// A SCRIPTED WALK THROUGH A ROOM LEVEL: the proof that a body can cross every room the level
// links, and the path a lab draws over it.
//
// - THE TOUR (`tour`): every room reached from the start room by its doorways (portals to
//   another room; a window onto nothing is left alone), depth first in the order the portals are
//   written: into each room's middle (the standing spot nearest its centroid), then out through
//   each doorway to a room not yet seen and back. Inside a room the way from one point to the next
//   is found on a grid of `cell` m over its footprint (ai/path.hpp's grid A*, then string-pulled):
//   a cell is where the body can stand (a thin probe dropped from a metre over the floor finds
//   walkable ground, and the standing hull fits there), and a step between two cells is one the
//   hull sweeps clear, climbing no more than the step height and dropping no more than a stair.
//   The world decides all of it through the mover's own trace, so a cave's rock, a pillar and a
//   flight of steps need no rule of their own.
// - THE DRIVE (`walk`): the body (engine/move) stood at the tour's first point and walked to each
//   next one, its view turned toward the point and the stick held forward, until it is within
//   `reach` m in the plan, for at most `patience` s a point.
//
// Pure: the same level, world and spec give the same tour and the same steps to the bit.
//
// ge:summary A scripted walk through a room level: a tour of every room by its doors, then walked
// ge:keywords rooms walk tour route path grid a-star string-pull doorway portal mover drive
// ge:keywords scripted walkthrough visit every room
// ge:skill rooms move
// ge:test tests/rooms_clip_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "move/spec.hpp"
#include "move/step.hpp"
#include "rooms/level.hpp"

namespace ge::rooms {

struct TourPoint {
  move::Vec3 at;      // world m, on the floor
  int32_t room = -1;  // the room it is in
};

struct Tour {
  std::vector<TourPoint> points;
  std::string error;  // why the tour stops short (empty: every linked room is on it)
};

// The tour from `start` (world m, on the floor of a room) through every room linked to it.
Tour tour(const Level& level, const move::World& world, const move::MoveSpec& spec,
          const move::Vec3& start, double cell = 0.25);

struct WalkOptions {
  double dt = 1.0 / 120.0;  // s a step
  double reach = 0.3;       // m in the plan: a point counts as reached
  double patience = 30;     // s: the longest a point may take
  size_t trail_every = 12;  // a feet position kept every this many steps
  bool sprint = false;      // the sprint button held all the way
};

struct Walk {
  size_t steps = 0;
  size_t reached = 0;             // tour points reached, in order
  std::vector<move::Vec3> trail;  // the feet every `trail_every` steps
  std::vector<int32_t> rooms;     // each room entered, in order of first entry
  move::MoveState end;            // the body when the walk stopped
  double lowest_below_floor = 0;  // m: the deepest the feet went under their room's floor
  std::string error;              // why the walk stopped short (empty: every point reached)
};

Walk walk(const Level& level, const move::World& world, const move::MoveSpec& spec,
          const Tour& tour, const WalkOptions& options = {});

}  // namespace ge::rooms
