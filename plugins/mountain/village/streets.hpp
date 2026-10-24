// THE VILLAGE'S STREETS: where a ski area's village lays its streets on the valley floor below
// the hub, as one compact network the traffic drives and the village's buildings will line.
//
// THE PLAN, as a base village is laid out below its lifts:
// - THE MAIN STREET runs along the hub's valley edge a row of buildings back from it, bowed a
//   little off straight, as a street that follows its ground is.
// - THE BACK STREET runs behind it.
// - CROSS STREETS join the two, at both ends and once or twice between, so the four make a LOOP
//   traffic can circulate on (a lane each way, on the right).
// - THE ROAD OUT leaves the back street at one end of the village (on half the maps at both) and
//   runs down the valley off the map's edge, so traffic comes and goes.
// - THE SQUARE stands on the main street's hub side below the main lift (the walk from the street
//   to the lifts crosses it) and THE DAY CAR PARK a little along from it: two aisles in from the
//   street with the bays either side, joined at the far end.
// Every street, to the back of its sidewalks, keeps off the runs and lanes, out of the lifts'
// lines and away from their stations and queues, out of the hub and its wind tunnels, away from
// the pads the caller names, the gates, the start, the finish arena and the kickers, and inside
// the map; it climbs and leans no more than a street does. A village whose loop cannot be laid
// clear anywhere near its point has no streets.
//
// A pure function of the map, off hashes of its seed (never a stream): nothing a map builds or a
// run steps moves for it. The caller keeps the plan; the ground under it is ground.hpp's.
//
// ge:summary The village's street plan: main, back and cross streets, the roads out, square, car
// ge:summary park
// ge:keywords village streets plan-streets street junction road-out square car-park loop
// ge:keywords street-at beside-point resample streets-hash
// ge:skill mountain
// ge:test tests/mountain_village_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mountain/mapgen/types.hpp"
#include "mountain/village/street_rules.hpp"

namespace ge::mountain::village {

// A point of a street's centreline: in the world, the snow's height there, the arc from the
// line's start and the way it runs (heading 0 = +z, clockwise from above).
struct StreetPoint {
  double x;
  double y;
  double z;
  double s;
  double heading;
};

// ONE STREET between two junctions: its kind and cross-section, its centreline every
// `streets::kStep` m from its first junction (`from`) to its last (`to`), and whether it runs off
// the map's edge there (`exit`: its last point on the edge, `to` the junction there).
struct Street {
  std::string id;
  StreetKind kind;
  Section section;
  std::vector<StreetPoint> points;
  double length;
  int32_t from;
  int32_t to;
  bool exit;
};

// A place streets meet (or a street leaves the map, `exit`), and the streets there.
struct Junction {
  int32_t id;
  double x;
  double y;
  double z;
  std::vector<int32_t> streets;
  bool exit;
};

// An open place along the main street: the middle of its rectangle, its heading (out of the
// street, toward the hub), half its length along the street and half its depth.
enum class AreaKind : uint8_t { square, carpark };
struct VillageArea {
  AreaKind kind;
  double x;
  double y;
  double z;
  double heading;
  double half;
  double depth;
};

// THE STREET PLAN of a village.
struct Streets {
  int32_t valley;   // which way z runs out of the hub into the valley, +1 or -1
  double centre_x;  // the village's middle along the main street
  double centre_z;
  double edge;  // the z of the hub's valley edge the plan is laid off
  std::vector<Street> streets;
  std::vector<Junction> junctions;
  std::vector<VillageArea> areas;
  // The main street's and the back street's pieces in order along x (indices into `streets`),
  // each end's cross street (west, east) and road out (-1 for none).
  std::vector<int32_t> main;
  std::vector<int32_t> back;
  std::array<int32_t, 2> end_cross{-1, -1};
  std::array<int32_t, 2> end_road{-1, -1};
  double square = 0;         // the square's middle along the main street (x)
  int32_t carpark_side = 0;  // which way along it the car park lies: +1 east, -1 west, 0 none
};

// THE STREETS of `level`'s village, laid clear of everything above and of `pads` (the places a
// machine stands, which the game places), or nothing where the map has no ski area or no loop
// fits near its village.
std::optional<Streets> plan_streets(const mapgen::Level& level,
                                    std::span<const mapgen::PlanPoint> pads = {});

// The point `lat` m to the RIGHT of a centreline point (right of a heading is (cos h, -sin h)).
mapgen::PlanPoint beside_point(const StreetPoint& p, double lat);

// A street's point `s` m along it.
StreetPoint street_at(const Street& st, double s);

// A polyline through `pts` resampled every `step` m, the snow's height, arc and heading on every
// point.
std::vector<StreetPoint> resample(const mapgen::Level& level,
                                  std::span<const mapgen::PlanPoint> pts,
                                  double step = streets::kStep);

// The plan folded into `h` (FNV-1a over every number's bits), for goldens and lab digests.
uint64_t streets_hash(const Streets& plan, uint64_t h);

// A uniform share in [0, 1) off a hash of the seed, a name, an index and a salt: what the plan
// deals by, never a stream.
double pick(uint32_t seed, const char* id, int32_t k, int32_t salt);

}  // namespace ge::mountain::village
