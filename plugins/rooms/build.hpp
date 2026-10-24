// GROWING A ROOM: a level.hpp room turned into the triangles a renderer and a collider read,
// from the few numbers stored for it and its seed. Pure: the same level gives the same bits on
// every machine and any number of threads, and nothing is cached on disk.
//
// - A PRISM room: the floor and the ceiling as its footprint (ear-clipped), each wall a grid with
//   the openings cut out of it. A wall whose material has a relief (relief.hpp: rock, crag, stones,
//   ashlar, tiles, planks, corrugated, dents) is gridded fine enough for its smallest feature and
//   every vertex pushed into the room by the relief's height times its depth, faded to nothing at
//   the wall's edges and round every opening, so walls, floor and neighbours still meet on straight
//   lines. A flat wall is as few quads as its openings allow. A floor or ceiling with a relief gets
//   a grid over the flat cap, grown the same way and faded to flat at the walls.
// - A CAVE room: a field over a grid at the room's cell size, solid where positive: the prism's
//   rounded distance (its corners and edges eased by a radius) plus the seed's fbm of the cave's
//   depth (eased to a third on the floor so it stays walkable, faded to nothing round a mouth),
//   with a tunnel cut in at every opening. The noise is crag's when wall 0's material is crag. The
//   surface is found by surface nets (a vertex a cell, at the mean of its edges' crossings; a quad
//   an edge crossed) and its normals by the field's gradient. The rock only ever grows inward, so a
//   cave never reaches past its footprint, and the cap the field leaves across a mouth is cut away.
//   A cave wears its floor material where the rock faces up, its ceiling's where it faces down and
//   wall 0's elsewhere.
// - Every opening with a `trim` gets a frame (jambs, a head and, above the floor, a sill) built by
//   the room the portal is written in, half a frame's depth into each room; across a cave's mouth
//   it covers the rock's cell-sized edge.
// - Every solid is a box, its faces turned out (its foot left off when it stands on the floor).
//
// Every triangle of a room faces into it: its winding is counter-clockwise seen from the side its
// normal points to, and its vertices' normals point the same way, so a view from outside with back
// faces culled is a cutaway (the walls nearest the eye vanish). Positions are world metres.
//
// ge:summary Grows a room's triangles from its level record and seed: rock walls, caves, frames
// ge:keywords build generate grow room mesh triangles prism cave surface-nets sdf field relief
// ge:keywords rock wall opening frame trim solid ear-clipping seed procedural fast parallel
// ge:skill rooms
// ge:test tests/rooms_build_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rooms/level.hpp"

namespace ge::rooms {

struct Mesh {
  std::vector<float> pos;     // xyz a vertex, world m
  std::vector<float> nrm;     // xyz a vertex, unit, into the room
  std::vector<uint8_t> mat;   // a vertex's material
  std::vector<uint32_t> idx;  // three a triangle
  size_t vertices() const { return mat.size(); }
  size_t triangles() const { return idx.size() / 3; }
};

// Room `room` of a linked level (level.hpp `link`).
Mesh build_room(const Level& level, size_t room);
// Every room, rooms side by side on `workers` threads (core/parallel.hpp); the same bits for any
// count.
std::vector<Mesh> build_level(const Level& level, size_t workers);

// FNV-1a 64 over a mesh's bits, in order: what a test and a lab hold a build to.
uint64_t mesh_digest(const Mesh& mesh);

}  // namespace ge::rooms
