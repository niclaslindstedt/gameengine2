// THE RELIEFS: what a material's surface grows into, as a height in [0, 1] a point (times the
// material's depth, pushed into the room). Each is a pure function of the point and the room's
// seed, exact float operations only, so a surface grows to the same bits everywhere.
//
// | Relief    | What grows                                                   | Laid out by |
// | --------- | ------------------------------------------------------------ | ----------- |
// | none      | nothing: a flat wall (brick, plaster, concrete)              |             |
// | rock      | rough stone: four octaves of value noise                      | the world   |
// | crag      | broken cliff: ridged noise, sharp crests and creases         | the world   |
// | stones    | a fieldstone wall: rounded stones of uneven size in mortar   | the surface |
// | ashlar    | dressed blocks in running bond, bevelled, each its own proud | the surface |
// | tiles     | square tiles in grout, each tilted a little                  | the surface |
// | planks    | boards with grooves between, staggered ends, a little grain  | the surface |
// | corrugated| sheet metal pressed into arches across the surface           | the surface |
// | dents     | hammered or battered metal: a dimple round scattered points  | the surface |
//
// `scale` (m) is the largest feature: a stone, a block's height, a tile, a board's width, a
// corrugation's pitch, a dent's spacing. A pattern laid out by the surface reads (u, v): along a
// wall from its first corner and up from the floor, or x and z on a floor or ceiling; one laid out
// by the world reads the world point, so it runs on across a corner.
//
// ge:summary The relief kinds a surface grows and their heights
// ge:keywords relief surface displacement procedural pattern rock crag stones fieldstone ashlar
// ge:keywords blocks tiles grout planks boards wood corrugated metal dents hammered voronoi bevel
// ge:skill rooms
// ge:test tests/rooms_build_test.cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "rooms/level.hpp"

namespace ge::rooms {

inline constexpr int kReliefKinds = 9;
// The finest grid a relief grows on, m, and the most cells one surface may grow: detail finer than
// that is the texture's (the baker's normal map), not the geometry's.
inline constexpr float kMinReliefCell = 0.05f;
inline constexpr float kMaxReliefCells = 6000.0f;

// The relief's word in the text form ("rock"), and back (false when unknown).
std::string_view relief_name(Relief kind);
bool relief_of(std::string_view name, Relief* out);

// The height in [0, 1] at surface point (u, v), world point `p`, for a feature of `scale` m.
float relief_height(Relief kind, float u, float v, const float p[3], uint32_t seed, float scale);

// The grid a surface of this relief is grown on, m: fine enough to show its smallest feature.
float relief_cell(Relief kind, float scale);
// That grid on a surface of `area_m2`, coarsened so it grows at most kMaxReliefCells cells.
float relief_cell(Relief kind, float scale, float area_m2);

}  // namespace ge::rooms
