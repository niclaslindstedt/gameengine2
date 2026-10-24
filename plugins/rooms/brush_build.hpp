// A LEVEL'S BRUSHES BUILT INTO FACES: each brush's faces cut by its own planes into convex
// polygons, then the parts that lie inside another brush removed (constructive solid geometry),
// so what is left is what an eye or a body can meet. Pure: the same level gives the same
// triangles to the bit on any number of workers.
//
// WHAT REMOVES WHAT. A solid or sky brush removes the parts of any other brush's faces inside it
// (a wall's face buried in the next wall, a liquid's face against the floor). A liquid removes
// the parts of its own kind's faces inside it, so two water brushes side by side show no wall
// between them; a face of solid is kept under water. A clip brush removes nothing. A face lying
// ON another brush's face, facing it, is inside it (the two touching faces both go); two faces on
// one plane facing the same way overlap, and the earlier brush's face is kept.
//
// A brush whose planes do not close (a face reaching past `kReachM`) is left out and counted.
//
// ge:summary Builds a level's convex brushes into faces by CSG: clipped polygons, hidden parts out
// ge:keywords brush brushes csg constructive-solid-geometry polygon clip convex faces build
// ge:keywords triangles liquid sky clip-brush coplanar projection texels
// ge:skill rooms
// ge:test tests/rooms_brush_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "rooms/level.hpp"

namespace ge::rooms {

inline constexpr double kReachM = 8192;  // a face polygon starts as a square this far across

struct BrushMesh {
  std::vector<float> pos;         // xyz a vertex, world m
  std::vector<float> nrm;         // xyz a vertex: its face's outward normal (shaded round: smooth)
  std::vector<float> st;          // a vertex's texel coordinates (its face's projection)
  std::vector<uint32_t> idx;      // three a triangle, counter-clockwise seen from the normal's side
  std::vector<uint8_t> material;  // a triangle's
  std::vector<Contents> contents;  // a triangle's brush's
  std::vector<uint8_t> hidden;     // a triangle's: 1 never drawn (a hidden face or a clip brush)
  std::vector<uint32_t> brush;     // a triangle's brush
  size_t unbounded = 0;            // brushes left out because their planes do not close
  size_t triangles() const { return idx.size() / 3; }
};

struct Polygon {
  std::vector<double> xyz;  // corners, counter-clockwise seen from the normal's side
  uint32_t face = 0;        // into Level::faces
};

// A half-space: the points with n·p <= d (n unit, metres).
struct HalfSpace {
  double n[3] = {0, 1, 0};
  double d = 0;
};

// The convex cell the half-spaces bound: `(*out)[i]` the polygon plane i leaves on it (counter-
// clockwise seen from its normal's side; empty when the plane does not touch the cell). False,
// with `out` empty, when the planes do not close within `kReachM`.
bool cell_polygons(std::span<const HalfSpace> planes, std::vector<std::vector<double>>* out);

// Brush `b`'s own polygons, one a face its planes leave room for, before any other brush cuts
// them. Empty when the brush does not close.
std::vector<Polygon> brush_polygons(const Level& level, size_t b);
// Every brush built, `workers` at a time.
BrushMesh build_brushes(const Level& level, size_t workers);
uint64_t brush_mesh_digest(const BrushMesh& mesh);

}  // namespace ge::rooms
