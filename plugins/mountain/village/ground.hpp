// THE GROUND UNDER THE VILLAGE: what the ground is at a point of a village's streets (a
// carriageway, the rest of a street to the back of its sidewalks or an open place, the margin the
// trees are felled over, the gardens they are thinned in), baked once a plan onto a metre grid,
// and which of the map's trees are gone for it.
//
// Pure functions of the map and its street plan; the gardens' thinning is off a hash of where
// each tree stands, never a stream.
//
// ge:summary The ground under the village's streets as a mask, and the trees felled for them
// ge:keywords village street-ground ground-mask carriageway on-street on-road felled garden
// ge:keywords felled-trees thinned
// ge:skill mountain
// ge:test tests/mountain_village_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "mountain/mapgen/types.hpp"
#include "mountain/village/streets.hpp"

namespace ge::mountain::village {

// What the ground is at a point of the village, each a step up from the one before.
enum class Ground : uint8_t {
  none = 0,    // nothing of the village's
  garden = 1,  // the gardens round it, where the trees are thinned
  felled = 2,  // the margin past a street or an open place the trees are felled over
  street = 3,  // a street to the back of its sidewalks, the square, the car park
  road = 4,    // a carriageway (its lanes and its parking strips) or a junction
};

inline const char* ground_name(Ground g) {
  switch (g) {
    case Ground::none:
      return "none";
    case Ground::garden:
      return "garden";
    case Ground::felled:
      return "felled";
    case Ground::street:
      return "street";
    case Ground::road:
      return "road";
  }
  return "?";
}

// The village's ground on a grid of `kCell` m cells from (x0, z0).
struct GroundMask {
  static constexpr double kCell = 1;
  double x0 = 0;
  double z0 = 0;
  int32_t cols = 0;
  int32_t rows = 0;
  std::vector<uint8_t> data;
};

// The ground under `plan`'s streets and open places.
GroundMask bake_ground(const Streets& plan);

// What the ground is at (x, z) (none outside the mask).
Ground ground_at(const GroundMask& mask, double x, double z);

// Whether (x, z) is on a street (to the back of its sidewalks), the square or the car park.
inline bool on_street(const GroundMask& mask, double x, double z) {
  return ground_at(mask, x, z) >= Ground::street;
}
// Whether (x, z) is on a carriageway or a junction.
inline bool on_road(const GroundMask& mask, double x, double z) {
  return ground_at(mask, x, z) >= Ground::road;
}

// THE TREES GONE from `level`: 1 for each of `level.trees` felled for the village (on a street,
// an open place or in the margin past them) or thinned out of its gardens (all but
// `lots::kKeep`).
std::vector<uint8_t> felled_trees(const mapgen::Level& level, const GroundMask& mask);

}  // namespace ge::mountain::village
