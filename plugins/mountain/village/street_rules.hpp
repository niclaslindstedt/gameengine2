// THE VILLAGE'S STREETS, the numbers: every street's cross-section, where the plan lays them
// below the hub, what a street keeps clear of, the open places along the main street and how far
// round them the trees go. Measured off how a mountain village's streets are built and kept in
// winter; every number is metres unless it says otherwise.
//
// A street's CROSS-SECTION is stated side by side out from its centreline: the carriageway (two
// lanes of `lane`, one each way, traffic on the right), then on each side a parking strip along
// the kerb (`park`, 0 for none), the plough's windrow (`bank` wide and `heap` tall: the snow the
// plough throws off the carriageway) and a sidewalk (`walk`, 0 for none) raised a kerb over the
// road. Side 0 is the street's LEFT going from its first junction to its last (in the world's
// convention: right of a heading is (cos h, -sin h)), side 1 its right.
//
// ge:summary The village's street numbers: the cross-sections, the plan's spans and clearances
// ge:keywords village streets street-section side-reach street-rules carriageway sidewalk
// ge:keywords windrow car-park square felled garden lift-clearance
// ge:skill mountain
// ge:test tests/mountain_village_test.cpp
#pragma once

#include <array>
#include <cstdint>

#include "mountain/mapgen/types.hpp"

namespace ge::mountain::village {

// One side of a street, out from the carriageway's edge.
struct StreetSide {
  double park;
  double bank;
  double heap;
  double walk;
};

// A street's cross-section: one lane's width and its two sides.
struct Section {
  double lane;
  std::array<StreetSide, 2> sides;
};

// The kinds of street a village has.
//   MAIN: the street along the hub, the shops and the base's buildings either side: two lanes of
//     3.5 m, a sidewalk of 2.2 m each side, the windrow kept low (loaded away on a shop street).
//   BACK: the residential street behind it: two lanes of 2.9 m, cars parked along the kerb on
//     one side, one sidewalk, the windrows tall where nobody carts them off.
//   CROSS: the short streets between them: as the back street, no parking.
//   ROAD: the valley road out: two lanes of 3.25 m, no sidewalks, banks a metre tall.
//   AISLE: the car park's aisles: 6 m between the bays, no banks.
enum class StreetKind : uint8_t { main, back, cross, road, aisle };

inline const char* street_kind_name(StreetKind k) {
  switch (k) {
    case StreetKind::main:
      return "main";
    case StreetKind::back:
      return "back";
    case StreetKind::cross:
      return "cross";
    case StreetKind::road:
      return "road";
    case StreetKind::aisle:
      return "aisle";
  }
  return "?";
}

inline constexpr Section section_of(StreetKind k) {
  switch (k) {
    case StreetKind::main:
      return {3.5, {{{0, 0.7, 0.35, 2.2}, {0, 0.7, 0.35, 2.2}}}};
    case StreetKind::back:
      return {2.9, {{{2.2, 1.0, 0.7, 1.6}, {0, 1.4, 0.9, 0}}}};
    case StreetKind::cross:
      return {2.9, {{{0, 0.9, 0.6, 1.6}, {0, 1.3, 0.85, 0}}}};
    case StreetKind::road:
      return {3.25, {{{0, 1.6, 1.0, 0}, {0, 1.6, 1.0, 0}}}};
    case StreetKind::aisle:
      break;
  }
  return {3, {{{0, 0, 0, 0}, {0, 0, 0, 0}}}};
}

// A street's half-width on one side: from its centreline to the back of its sidewalk (or its
// bank where it has none).
inline constexpr double side_reach(const Section& s, int side) {
  const StreetSide& x = s.sides[size_t(side)];
  return s.lane + x.park + x.bank + x.walk;
}

// The carriageway's half-width on one side: the lane and the parking strip.
inline constexpr double road_reach(const Section& s, int side) {
  return s.lane + s.sides[size_t(side)].park;
}

namespace streets {

// The main street's length, dealt between the two.
inline constexpr double kSpanLeast = 330;
inline constexpr double kSpanMost = 400;
// The main street's line off the hub's valley edge, dealt; how far behind it the back street
// runs, dealt; the most either street bows off straight.
inline constexpr double kMainLeast = 47;
inline constexpr double kMainMost = 53;
inline constexpr double kBackLeast = 54;
inline constexpr double kBackMost = 62;
inline constexpr double kBend = 5;
// How many cross streets join them between the two ends, and the least gap between two.
inline constexpr int kCrossesLeast = 1;
inline constexpr int kCrossesMost = 2;
inline constexpr double kCrossesApart = 80;
// The share of villages with a second road out.
inline constexpr double kTwoRoads = 0.5;
// How often a street's line is sampled.
inline constexpr double kStep = 4;
// The steepest a street may climb along it and lean across it, rise over run.
inline constexpr double kGradeAlong = 0.09;
inline constexpr double kGradeAcross = 0.07;
// The centres tried either way of the village's point, and the shares of the span tried at each.
inline constexpr std::array<double, 11> kCentres{0,   -40,  40,  -80,  80, -130,
                                                 130, -190, 190, -260, 260};
inline constexpr std::array<double, 3> kSpanShares{1, 0.85, 0.7};

// What a street (to the back of its sidewalks) keeps away from: a lift's station, its queue's
// lane, a wind tunnel, a run's edge, the hub, a pad a machine stands on, a gate, the start, the
// finish arena, a kicker (past its landing's half).
inline constexpr double kClearStation = 20;
inline constexpr double kClearQueue = 8;
inline constexpr double kClearTunnel = 8;
inline constexpr double kClearLine = 8;
inline constexpr double kClearHub = 3;
inline constexpr double kClearPad = 30;
inline constexpr double kClearGate = 20;
inline constexpr double kClearStart = 35;
inline constexpr double kClearFinish = 75;
inline constexpr double kClearKicker = 30;
// The snow a street keeps off is the groomer's: packed past this share.
inline constexpr double kGroomed = 0.25;
// How near the hub's edge the square and the car park reach.
inline constexpr double kAreaHub = 0.5;

}  // namespace streets

// A lift's station houses and line, as a street keeps off them: each house's length behind its
// wheel, how far past the house the station's own ground runs, and the clearance added to both.
namespace lift_clear {

inline constexpr double house_length(mapgen::LiftKind k) {
  return k == mapgen::LiftKind::gondola ? 12.8 : k == mapgen::LiftKind::chair ? 7.2 : 4;
}
inline constexpr double house_past(mapgen::LiftKind k) {
  return k == mapgen::LiftKind::gondola ? 4.7 : k == mapgen::LiftKind::chair ? 5.3 : 2;
}
inline constexpr double kHouse = 3;
inline constexpr double kLine = 3.5;

}  // namespace lift_clear

// The base's open places, along the main street on the hub's side: the SQUARE at the main lift
// (`kSquareAlong` long, open from the street to the hub) and the DAY CAR PARK `kCarparkGap` along
// from it (room for the ticket office between): two aisles in from the street with a row of bays
// either side of each, joined at the far end, a bay `kBayWidth` across and `kBayLength` deep.
namespace areas {

inline constexpr double kSquareAlong = 36;
inline constexpr double kCarparkGap = 34;
inline constexpr double kBayWidth = 2.5;
inline constexpr double kBayLength = 5;
inline constexpr double kCarparkBack = 3;

}  // namespace areas

// The ground round the streets: how far past a street's reach or an open place the trees are
// felled, how far past that they are thinned (the gardens), and the share kept standing there.
namespace lots {

inline constexpr double kFell = 3;
inline constexpr double kThin = 30;
inline constexpr double kKeep = 0.25;

}  // namespace lots

}  // namespace ge::mountain::village
