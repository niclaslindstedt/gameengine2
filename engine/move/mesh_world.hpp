// A WORLD OF TRIANGLES a mover sweeps its box through: a level's static mesh (a room's walls, a
// cave's rock, a terrain's patch) answering `World::trace` (world.hpp), so a game whose level is
// triangles, not convex brushes, needs no collision of its own.
//
// EXACT: each triangle is met by the separating-axis test of a box against a triangle, swept (the
// thirteen axes: the box's three, the triangle's normal and the nine crosses of its edges with the
// box's axes), so a box slides along a face, over an edge and past a corner where the triangle
// really is, never on an edge's extension. A triangle is solid on both sides and has no inside: a
// box that starts across one reports `start_solid` and is let out the way it came.
//
// FAST: the triangles are bucketed once in a uniform grid of `cell` L, and a trace visits the
// cells its swept box covers, each triangle once (in the first covered cell it lies in), with
// nothing allocated.
//
// ge:summary A world of triangles answering the mover's swept-box trace, bucketed in a grid
// ge:keywords move movement mesh-world triangle soup mesh collision trace sweep separating-axis
// ge:keywords sat grid bucket uniform-grid level static cave
// ge:skill move
// ge:test tests/move_mesh_world_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "move/world.hpp"

namespace ge::move {

struct MeshWorld {
  double skin = 0.03125;  // L: how far a trace stops short of a surface
  double cell = 0;        // L: the grid's cell (set by build_mesh_world)
  std::vector<Vec3> tri;  // three corners a triangle
  // The grid: `dim` cells from `lo`, each cell's triangles at `items[start[c]..start[c + 1])`.
  Vec3 lo{0, 0, 0};
  int32_t dim[3] = {0, 0, 0};
  std::vector<uint32_t> start, items;
  std::vector<int32_t> tri_cell;  // three a triangle: the first cell (x, y, z) it lies in
};

// The world of the triangles `corners` (three a triangle, in any winding), bucketed in cells of
// `cell` L (grown when the bounds would need more than about four million cells). A triangle with
// no area is left out.
MeshWorld build_mesh_world(std::span<const Vec3> corners, double cell);

// The swept box against the triangles: the nearest touch (world.hpp's contract).
Hit trace_mesh_world(const MeshWorld& w, const Box& box, const Vec3& from, const Vec3& to);

// The world as the mover's `World`; `w` must outlive it.
World as_world(const MeshWorld& w);

}  // namespace ge::move
