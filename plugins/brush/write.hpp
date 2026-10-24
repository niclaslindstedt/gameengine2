// FIXTURE MAPS: small maps of every classic BSP family written by our own writers, so the readers,
// the tests and the `bsp` lab have files of each version without shipping anyone's content.
//
// A fixture is a few axis-aligned rooms (map units, z up) that touch where a doorway joins them,
// a few point lights, and (46 only) a curved patch. One compile serves every family:
// - **Faces**: every inside side of every room, less the rectangle where a neighbour touches it,
//   wound clockwise seen from the room (the families' own convention), textured by an axial
//   projection (a floor's s along +x and t along −y; a wall's s along its run, t down).
// - **The tree**: axis-aligned splits between the rooms (each a plane some room's side lies on, so
//   no room is cut), then each room's own six sides; every leaf off a room is solid. Rooms laid
//   out so no plane separates them are refused, naming the rooms.
// - **Visibility**: a room sees itself, the rooms it touches and theirs (two doorways deep).
// - **Light**: per luxel (one every 16 texels), an ambient plus each light in its room, falling
//   off linearly to its radius and leaning with the angle; grey in 29 and BSP2, the same as RGB
//   in 38 and 46. Clip hulls (29, BSP2) are the point tree again; their expansion is the trace's.
//
// The bytes are a pure function of the fixture: the same file on every platform.
//
// ge:summary Writes fixture maps of every classic BSP family: rooms, tree, visibility, lightmaps
// ge:keywords write fixture bsp v29 bsp2 v38 v46 rooms doorway tree split visibility lightmap
// ge:keywords luxel light patch vault miptex writer
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "brush/map.hpp"

namespace ge::brush {

struct FixtureRoom {
  std::string name;
  int32_t min[3] = {}, max[3] = {};  // map units, z up
  std::string floor, wall, ceiling;  // texture names
};

struct FixtureLight {
  float pos[3] = {};
  float radius = 300;  // units to darkness
  float bright = 200;  // 0..255 at the light
};

struct Fixture {
  std::vector<FixtureRoom> rooms;
  std::vector<FixtureLight> lights;
  std::string message;  // the world's name, in its entity
  int vault_room = -1;  // 46: a curved patch under this room's ceiling
  int ambient = 28;     // 0..255 everywhere
};

// The fixture the tests and the lab use: a hall, a side room, a corridor and a vaulted room joined
// by three doorways, four lights.
Fixture sample_fixture();

// The fixture as a map of `family`; false with the reason in `*error`.
bool write_bsp(const Fixture& fixture, Family family, std::vector<uint8_t>* out,
               std::string* error);

}  // namespace ge::brush
