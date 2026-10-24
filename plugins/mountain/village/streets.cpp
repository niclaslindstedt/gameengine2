// The village's street plan (streets.hpp): the candidates tried round the village's point, the
// clearance every street keeps, and the plan built from the first that fits.
//
// ge:summary The village's street plan: the candidates tried, the clearance kept, the build
// ge:keywords village streets plan-streets clearance draft junctions
// ge:skill mountain
// ge:test tests/mountain_village_test.cpp
#include "mountain/village/streets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include "core/heightfield.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "mountain/mapgen/hub.hpp"
#include "mountain/mapgen/lift_line.hpp"
#include "mountain/mapgen/query.hpp"
#include "mountain/mapgen/regions.hpp"

namespace ge::mountain::village {

namespace {

namespace mg = ge::mountain::mapgen;
namespace num = ge::core::num;
namespace libm = ge::core::libm;
using ge::core::hypot;
using mg::PlanPoint;

constexpr double kInf = std::numeric_limits<double>::infinity();

double clamp(double v, double lo, double hi) { return num::max(lo, num::min(hi, v)); }

// The distance from (x, z) to the segment a-b.
double to_segment(double x, double z, double ax, double az, double bx, double bz) {
  const double ex = bx - ax;
  const double ez = bz - az;
  const double len2 = num::max(1e-6, ex * ex + ez * ez);
  const double t = clamp(((x - ax) * ex + (z - az) * ez) / len2, 0, 1);
  return hypot(ax + ex * t - x, az + ez * t - z);
}

// A lift's queue lane in its line's frame (u up the line from the bottom wheel, v right of it):
// a gondola's straight back from its platform, a chair's or a drag's out of its corral's mouth.
std::vector<mg::LinePoint> queue_lane(mg::LiftKind kind) {
  const mg::LiftLookLane look = mg::lift_look(kind);
  if (kind == mg::LiftKind::gondola) return {{look.entry_at, 0}, {look.entry_at - 40, 0}};
  const double clear = look.house_width / 2 + look.gauge / 2 + 3;
  const mg::LinePoint mouth{look.entry_at - 7, clear + 1};
  const double du = mouth.u - (look.entry_at - 1);
  const double dv = mouth.v - look.entry_side;
  const double h = hypot(du, dv);
  const double k = 40 / (h != 0 ? h : 1);
  return {{look.entry_at, look.entry_side},
          {look.entry_at - 1, look.entry_side},
          mouth,
          {mouth.u + du * k, mouth.v + dv * k}};
}

// Whether (x, z) stands clear of a lift's two station houses and its line.
bool clear_of_lift(const mg::Lift& lift, double x, double z) {
  const double ex = lift.top.x - lift.bottom.x;
  const double ez = lift.top.z - lift.bottom.z;
  const double len = num::max(1.0, hypot(ex, ez));
  const double dx = ex / len;
  const double dz = ez / len;
  const double u = (x - lift.bottom.x) * dx + (z - lift.bottom.z) * dz;
  const double v = std::fabs((x - lift.bottom.x) * dz - (z - lift.bottom.z) * dx);
  const mg::LiftLookLane look = mg::lift_look(lift.kind);
  if (u > -lift_clear::kLine && u < len + lift_clear::kLine &&
      v < look.gauge / 2 + lift_clear::kLine) {
    return false;
  }
  const double reach =
      lift_clear::house_length(lift.kind) + lift_clear::house_past(lift.kind) + lift_clear::kHouse;
  const double across = (look.house_width + look.gauge) / 2 + lift_clear::kHouse;
  return !(v < across && ((u <= 0 && u > -reach) || (u >= len && u < len + reach)));
}

// What a street keeps away from, worked out once a plan: is (x, z) clear? `hub_pad` overrides
// the hub's clearance (the square and the car park reach up to it, onto the hub's fading snow,
// where the groomed snow is not asked).
using Clear = std::function<bool(double x, double z, double hub_pad)>;

Clear clearance_of(const mg::Level& level, std::span<const PlanPoint> pads) {
  namespace S = streets;
  const mg::ResortMap& area = *level.resort;
  // The runs' lines as tracks the query can index, and the piste raced.
  auto runs = std::make_shared<std::vector<mg::Track>>();
  runs->reserve(area.runs.size() + 1);
  for (const mg::Run& r : area.runs) runs->push_back(mg::Track{r.points, r.length, nullptr});
  runs->push_back(mg::Track{level.track.points, level.track.length, nullptr});
  // Every queue lane in the world.
  auto queues = std::make_shared<std::vector<std::vector<PlanPoint>>>();
  for (const mg::Lift& lift : area.lifts) {
    const double ex = lift.top.x - lift.bottom.x;
    const double ez = lift.top.z - lift.bottom.z;
    const double len = num::max(1.0, hypot(ex, ez));
    const double dx = ex / len;
    const double dz = ez / len;
    std::vector<PlanPoint> q;
    for (const mg::LinePoint& p : queue_lane(lift.kind)) {
      q.push_back({lift.bottom.x + dx * p.u + dz * p.v, lift.bottom.z + dz * p.u - dx * p.v});
    }
    queues->push_back(std::move(q));
  }
  const mg::Region& region = mg::region_row(level.region);
  const bool crusted = level.face && level.crust && region.crust;
  const double support = crusted ? region.crust->packed : 0;
  const mg::Level* lv = &level;
  const std::vector<PlanPoint> pad_list(pads.begin(), pads.end());
  // The groomer's snow (on a real face, not the region's wind crust).
  const auto groomed = [lv, crusted, support](double x, double z) {
    const double p = lv->packed_at(x, z);
    if (!crusted) return p > S::kGroomed;
    return p > S::kGroomed && p > ge::core::sample_field(*lv->crust, x, z) * support + 0.05;
  };
  return [lv, runs, queues, pad_list, groomed](double x, double z, double hub_pad) {
    const mg::Level& map = *lv;
    const mg::ResortMap& resort = *map.resort;
    if (x < 0 || z < 0 || x > map.size || z > map.size) return false;
    if (hub_pad >= S::kClearHub && groomed(x, z)) return false;
    for (const mg::Lift& lift : resort.lifts) {
      if (!clear_of_lift(lift, x, z)) return false;
    }
    if (mg::outside_hub(resort.hub, x, z) < hub_pad) return false;
    mg::TrackHit hit;
    for (const mg::Track& line : *runs) {
      if (line.points.size() < 2) continue;
      mg::nearest_within(line, x, z, S::kClearLine + 40, hit);
      if (hit.distance == kInf) continue;
      if (hit.distance - line.points[size_t(hit.index)].width / 2 < S::kClearLine) return false;
    }
    for (const mg::Lift& lift : resort.lifts) {
      for (const mg::Vec3& end : {lift.bottom, lift.top}) {
        if (hypot(end.x - x, end.z - z) < S::kClearStation) return false;
      }
      for (const mg::SummitRamp& ramp : lift.ramps) {
        const double d = to_segment(x, z, ramp.from.x, ramp.from.z, ramp.to.x, ramp.to.z);
        if (d < ramp.width / 2 + S::kClearLine) return false;
      }
    }
    for (const std::vector<PlanPoint>& q : *queues) {
      for (size_t i = 0; i + 1 < q.size(); i++) {
        if (to_segment(x, z, q[i].x, q[i].z, q[i + 1].x, q[i + 1].z) < S::kClearQueue) {
          return false;
        }
      }
    }
    for (const mg::WindTunnel& t : resort.tunnels) {
      if (t.points.empty()) continue;
      const auto& a = t.points.front();
      const auto& b = t.points.back();
      if (to_segment(x, z, a.x, a.z, b.x, b.z) < t.width / 2 + S::kClearTunnel) return false;
    }
    for (const PlanPoint& p : pad_list) {
      if (hypot(p.x - x, p.z - z) < S::kClearPad) return false;
    }
    for (const mg::Checkpoint& cp : map.checkpoints) {
      if (hypot(cp.x - x, cp.z - z) < S::kClearGate) return false;
    }
    if (hypot(map.spawn.x - x, map.spawn.z - z) < S::kClearStart) return false;
    if (!map.checkpoints.empty()) {
      const mg::Checkpoint& finish = map.checkpoints.back();
      if (hypot(finish.x - x, finish.z - z) < S::kClearFinish) return false;
    }
    for (const mg::Kicker& k : map.kickers) {
      if (hypot(k.x - x, k.z - z) < S::kClearKicker + k.landing / 2) return false;
    }
    return true;
  };
}

// Whether a street's line is clear to the back of both its sides, and no steeper along it than a
// street is built.
bool street_clear(std::span<const StreetPoint> pts, const Section& section, const Clear& clear) {
  const double r0 = side_reach(section, 0);
  const double r1 = side_reach(section, 1);
  for (size_t i = 0; i < pts.size(); i++) {
    const StreetPoint& p = pts[i];
    for (const double lat : {-r0, -r0 / 2, 0.0, r1 / 2, r1}) {
      const PlanPoint q = beside_point(p, lat);
      if (!clear(q.x, q.z, streets::kClearHub)) return false;
    }
    if (i > 0) {
      const StreetPoint& a = pts[i - 1];
      if (std::fabs(p.y - a.y) > streets::kGradeAlong * num::max(1e-6, p.s - a.s)) return false;
    }
  }
  return true;
}

// A street's lean across at worst (both reaches), rise over run.
double lean_across(const mg::Level& level, std::span<const StreetPoint> pts,
                   const Section& section) {
  const double r0 = side_reach(section, 0);
  const double r1 = side_reach(section, 1);
  double most = 0;
  for (const StreetPoint& p : pts) {
    const PlanPoint a = beside_point(p, -r0);
    const PlanPoint b = beside_point(p, r1);
    most = num::max(most,
                    std::fabs(level.ground_at(b.x, b.z) - level.ground_at(a.x, a.z)) / (r0 + r1));
  }
  return most;
}

using Profile = std::function<double(double x)>;

// One candidate laid out: the skeleton's lines before they are split at their junctions.
struct Draft {
  double xa = 0, xb = 0, z0 = 0;
  int32_t sv = 1;
  Profile main_v, back_v;
  std::vector<double> crosses;
  struct Road {
    int end;
    std::vector<PlanPoint> pts;
  };
  std::vector<Road> roads;
  double square = 0;
  struct Carpark {
    int side;
    double p0, p1;
  };
  std::optional<Carpark> carpark;
};

struct Ask {
  double xa, xb, z0;
  int32_t sv;
  Profile main_v, back_v;
  double v_max;
  double lift_x;
  int n_cross;
  int road_end;
  bool two_roads;
  int park_side;
  uint32_t seed;
};

// Try one centre and span: the main street, the back street and the two end cross streets must
// all be clear (the loop), with one road out at least; the square, the car park and the middle
// cross streets are fitted round them.
std::optional<Draft> lay_draft(const mg::Level& level, const Clear& clear, const Ask& a) {
  const auto z_of = [&](double v) { return a.z0 + a.sv * v; };
  const auto along = [&](const Profile& v_of, double x0, double x1) {
    std::vector<PlanPoint> out;
    const int n = std::max(2, int(std::ceil(std::fabs(x1 - x0) / 10)));
    for (int i = 0; i <= n; i++) {
      const double x = x0 + ((x1 - x0) * i) / n;
      out.push_back({x, z_of(v_of(x))});
    }
    return out;
  };
  const auto line_ok = [&](const std::vector<PlanPoint>& pts, StreetKind kind) {
    const std::vector<StreetPoint> r = resample(level, pts);
    const Section s = section_of(kind);
    return street_clear(r, s, clear) && lean_across(level, r, s) <= streets::kGradeAcross;
  };
  if (!line_ok(along(a.main_v, a.xa, a.xb), StreetKind::main)) return std::nullopt;
  if (!line_ok(along(a.back_v, a.xa, a.xb), StreetKind::back)) return std::nullopt;
  const auto cross_line = [&](double x) {
    return std::vector<PlanPoint>{{x, z_of(a.main_v(x))}, {x, z_of(a.back_v(x))}};
  };
  for (const double x : {a.xa, a.xb}) {
    if (!line_ok(cross_line(x), StreetKind::cross)) return std::nullopt;
  }
  // THE SQUARE below the main lift, slid along until its ground is clear.
  const double sq = areas::kSquareAlong / 2;
  const double main_reach_hub = side_reach(section_of(StreetKind::main), a.sv > 0 ? 1 : 0);
  const auto area_ok = [&](double u0, double u1) {
    for (int i = 0; u0 + 4 * i <= u1 + 1e-6; i++) {
      const double x = u0 + 4 * i;
      const double v_top = a.main_v(x) - main_reach_hub - 1;
      for (int j = 0; streets::kAreaHub + 4 * j <= v_top; j++) {
        if (!clear(x, z_of(streets::kAreaHub + 4 * j), streets::kAreaHub)) return false;
      }
    }
    return true;
  };
  const double lo = a.xa + 75;
  const double hi = a.xb - 75;
  if (hi < lo) return std::nullopt;
  double square = std::nan("");
  const double want = clamp(a.lift_x, lo, hi);
  for (int k = 0; k <= 12 && std::isnan(square); k++) {
    const std::vector<double> offs =
        k == 0 ? std::vector<double>{0} : std::vector<double>{k * 8.0, -k * 8.0};
    for (const double s : offs) {
      const double u = want + s;
      if (u < lo || u > hi) continue;
      if (area_ok(u - sq, u + sq)) {
        square = u;
        break;
      }
    }
  }
  if (std::isnan(square)) return std::nullopt;
  Draft d;
  d.xa = a.xa;
  d.xb = a.xb;
  d.z0 = a.z0;
  d.sv = a.sv;
  d.main_v = a.main_v;
  d.back_v = a.back_v;
  d.square = square;
  // THE CAR PARK on the dealt side of the square, or the other.
  const double aisle = section_of(StreetKind::aisle).lane;
  const double w = 2 * (areas::kBayLength * 2 + aisle * 2);
  for (const int side : {a.park_side, -a.park_side}) {
    const double p0 =
        side > 0 ? square + sq + areas::kCarparkGap : square - sq - areas::kCarparkGap - w;
    const double p1 = p0 + w;
    if (p0 < a.xa + 12 || p1 > a.xb - 12) continue;
    if (area_ok(p0, p1)) {
      d.carpark = Draft::Carpark{side, p0, p1};
      break;
    }
  }
  // THE MIDDLE CROSS STREETS: dealt along, clear of the ends, of each other, of the square and the
  // car park.
  const auto cross_free = [&](double x) {
    if (std::fabs(a.xa - x) < streets::kCrossesApart) return false;
    if (std::fabs(a.xb - x) < streets::kCrossesApart) return false;
    for (const double o : d.crosses) {
      if (std::fabs(o - x) < streets::kCrossesApart) return false;
    }
    if (std::fabs(x - square) < sq + 30) return false;
    if (d.carpark && x > d.carpark->p0 - 20 && x < d.carpark->p1 + 20) return false;
    return line_ok(cross_line(x), StreetKind::cross);
  };
  for (int i = 0; i < a.n_cross; i++) {
    for (int t = 0; t < 8; t++) {
      const double u =
          a.xa +
          (a.xb - a.xa) *
              ((i + 0.5 + (pick(a.seed, "village-streets", t, 20 + i) - 0.5) * 0.7) / a.n_cross);
      const double x = num::round(u);
      if (!cross_free(x)) continue;
      d.crosses.push_back(x);
      break;
    }
  }
  std::sort(d.crosses.begin(), d.crosses.end());
  // THE ROAD OUT off the back street's end, down the valley off the map's edge, wandering a little.
  const auto road_at = [&](int end, int k) {
    const double x0 = end == 0 ? a.xa : a.xb;
    const double v0 = a.back_v(x0);
    const double v_end = a.v_max - 0.5;
    const double drift = (pick(a.seed, "village-streets", k, 30 + end) * 2 - 1) * 30;
    std::vector<PlanPoint> pts;
    const int n = std::max(2, int(std::ceil((v_end - v0) / 10)));
    for (int i = 0; i <= n; i++) {
      const double t = double(i) / n;
      const double e = t * t * (3 - 2 * t);
      pts.push_back({x0 + drift * e, z_of(v0 + (v_end - v0) * t)});
    }
    return pts;
  };
  std::vector<int> ends{a.road_end};
  if (a.two_roads) ends.push_back(1 - a.road_end);
  for (const int end : ends) {
    for (int k = 0; k < 4; k++) {
      std::vector<PlanPoint> pts = road_at(end, k);
      if (line_ok(pts, StreetKind::road)) {
        d.roads.push_back({end, std::move(pts)});
        break;
      }
    }
  }
  if (d.roads.empty()) {
    const int other = 1 - a.road_end;
    for (int k = 0; k < 4 && d.roads.empty(); k++) {
      std::vector<PlanPoint> pts = road_at(other, k);
      if (line_ok(pts, StreetKind::road)) d.roads.push_back({other, std::move(pts)});
    }
  }
  if (d.roads.empty()) return std::nullopt;
  return d;
}

// Split the draft's lines at their junctions into the street graph.
Streets build(const mg::Level& level, const Draft& d) {
  const auto z_of = [&](double v) { return d.z0 + d.sv * v; };
  Streets plan;
  plan.valley = d.sv;
  plan.edge = d.z0;
  const auto node = [&](double x, double z, bool exit = false) {
    const auto id = int32_t(plan.junctions.size());
    plan.junctions.push_back({id, x, level.ground_at(x, z), z, {}, exit});
    return id;
  };
  const auto add = [&](const std::string& id, StreetKind kind, const std::vector<PlanPoint>& pts,
                       int32_t from, int32_t to, bool exit = false) {
    std::vector<StreetPoint> points = resample(level, pts);
    const double length = points.back().s;
    const auto k = int32_t(plan.streets.size());
    plan.streets.push_back({id, kind, section_of(kind), std::move(points), length, from, to, exit});
    plan.junctions[size_t(from)].streets.push_back(k);
    plan.junctions[size_t(to)].streets.push_back(k);
    return k;
  };
  const auto along = [&](const Profile& v_of, double x0, double x1) {
    std::vector<PlanPoint> out;
    const int n = std::max(1, int(std::ceil(std::fabs(x1 - x0) / 10)));
    for (int i = 0; i <= n; i++) {
      const double x = x0 + ((x1 - x0) * i) / n;
      out.push_back({x, z_of(v_of(x))});
    }
    return out;
  };
  // The car park's two aisles meet the main street at these x.
  const double lane = section_of(StreetKind::aisle).lane;
  const double bay = areas::kBayLength;
  std::vector<double> aisle_x;
  if (d.carpark) aisle_x = {d.carpark->p0 + bay + lane, d.carpark->p1 - bay - lane};
  // The main street's junctions: its ends, the cross streets, the aisles.
  std::vector<double> main_xs{d.xa};
  main_xs.insert(main_xs.end(), d.crosses.begin(), d.crosses.end());
  main_xs.insert(main_xs.end(), aisle_x.begin(), aisle_x.end());
  main_xs.push_back(d.xb);
  std::sort(main_xs.begin(), main_xs.end());
  std::vector<double> back_xs{d.xa};
  back_xs.insert(back_xs.end(), d.crosses.begin(), d.crosses.end());
  back_xs.push_back(d.xb);
  std::vector<std::pair<double, int32_t>> main_node;
  std::vector<std::pair<double, int32_t>> back_node;
  main_node.reserve(main_xs.size());
  back_node.reserve(back_xs.size());
  for (const double x : main_xs) main_node.emplace_back(x, node(x, z_of(d.main_v(x))));
  for (const double x : back_xs) back_node.emplace_back(x, node(x, z_of(d.back_v(x))));
  const auto at = [](const std::vector<std::pair<double, int32_t>>& nodes, double x) {
    for (const auto& [nx, id] : nodes) {
      if (nx == x) return id;
    }
    return int32_t(-1);
  };
  for (size_t i = 0; i + 1 < main_xs.size(); i++) {
    plan.main.push_back(add("M" + std::to_string(i + 1), StreetKind::main,
                            along(d.main_v, main_xs[i], main_xs[i + 1]), main_node[i].second,
                            main_node[i + 1].second));
  }
  for (size_t i = 0; i + 1 < back_xs.size(); i++) {
    plan.back.push_back(add("B" + std::to_string(i + 1), StreetKind::back,
                            along(d.back_v, back_xs[i], back_xs[i + 1]), back_node[i].second,
                            back_node[i + 1].second));
  }
  std::vector<int32_t> cross_ids;
  for (size_t i = 0; i < back_xs.size(); i++) {
    const double x = back_xs[i];
    cross_ids.push_back(add("X" + std::to_string(i + 1), StreetKind::cross,
                            {{x, z_of(d.main_v(x))}, {x, z_of(d.back_v(x))}}, at(main_node, x),
                            at(back_node, x)));
  }
  for (size_t i = 0; i < d.roads.size(); i++) {
    const Draft::Road& r = d.roads[i];
    const PlanPoint last = r.pts.back();
    const int32_t exit = node(last.x, last.z, true);
    plan.end_road[size_t(r.end)] = add("R" + std::to_string(i + 1), StreetKind::road, r.pts,
                                       at(back_node, r.end == 0 ? d.xa : d.xb), exit, true);
  }
  plan.end_cross = {cross_ids.front(), cross_ids.back()};
  // THE OPEN PLACES and THE CAR PARK'S AISLES: in from the main street, along its far end, and
  // back out, a U the cars drive round.
  const int hub_side = d.sv > 0 ? 1 : 0;
  const double main_reach_hub = side_reach(section_of(StreetKind::main), hub_side);
  const auto area_of = [&](AreaKind kind, double u0, double u1) {
    const double um = (u0 + u1) / 2;
    const double v_top = d.main_v(um) - main_reach_hub - 0.5;
    const double v_low = streets::kAreaHub;
    const double vm = (v_top + v_low) / 2;
    const double z = z_of(vm);
    return VillageArea{kind,
                       um,
                       level.ground_at(um, z),
                       z,
                       d.sv > 0 ? ge::core::kPi : 0.0,
                       (u1 - u0) / 2,
                       (v_top - v_low) / 2};
  };
  const double sq = areas::kSquareAlong / 2;
  plan.areas.push_back(area_of(AreaKind::square, d.square - sq, d.square + sq));
  if (d.carpark) {
    plan.areas.push_back(area_of(AreaKind::carpark, d.carpark->p0, d.carpark->p1));
    const double v_in = streets::kAreaHub + areas::kCarparkBack + lane;
    const double ax = aisle_x[0];
    const double bx = aisle_x[1];
    const int32_t in_a = node(ax, z_of(v_in));
    const int32_t in_b = node(bx, z_of(v_in));
    add("P1", StreetKind::aisle, {{ax, z_of(d.main_v(ax))}, {ax, z_of(v_in)}}, at(main_node, ax),
        in_a);
    add("P2", StreetKind::aisle, {{ax, z_of(v_in)}, {bx, z_of(v_in)}}, in_a, in_b);
    add("P3", StreetKind::aisle, {{bx, z_of(v_in)}, {bx, z_of(d.main_v(bx))}}, in_b,
        at(main_node, bx));
  }
  plan.centre_x = (d.xa + d.xb) / 2;
  plan.centre_z = plan.junctions[size_t(main_node.front().second)].z;
  plan.square = d.square;
  plan.carpark_side = d.carpark ? d.carpark->side : 0;
  return plan;
}

}  // namespace

double pick(uint32_t seed, const char* id, int32_t k, int32_t salt) {
  uint32_t h = 2166136261u ^ seed;
  const size_t n = std::strlen(id);
  for (size_t i = 0; i < n; i++) {
    h = uint32_t(num::imul(h ^ uint32_t(uint8_t(id[i])), 16777619u));
  }
  h = uint32_t(num::imul(h ^ uint32_t(k), 16777619u));
  h = uint32_t(num::imul(h ^ uint32_t(salt), 16777619u));
  h ^= h >> 13;
  h = uint32_t(num::imul(h, 2246822507u));
  h ^= h >> 16;
  return double(h) / 4294967296.0;
}

PlanPoint beside_point(const StreetPoint& p, double lat) {
  return {p.x + libm::cos(p.heading) * lat, p.z - libm::sin(p.heading) * lat};
}

StreetPoint street_at(const Street& st, double s) {
  const auto& pts = st.points;
  const auto n = int(pts.size());
  const double u = clamp(s, 0, st.length);
  int i = std::min(n - 2,
                   std::max(0, int(std::floor((u / num::max(1e-6, st.length)) * double(n - 1)))));
  while (i > 0 && pts[size_t(i)].s > u) i--;
  while (i < n - 2 && pts[size_t(i) + 1].s <= u) i++;
  const StreetPoint& a = pts[size_t(i)];
  const StreetPoint& b = pts[size_t(std::min(n - 1, i + 1))];
  const double t = b.s > a.s ? (u - a.s) / (b.s - a.s) : 0;
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, u,
          libm::atan2(b.x - a.x, b.z - a.z)};
}

std::vector<StreetPoint> resample(const mg::Level& level, std::span<const PlanPoint> pts,
                                  double step) {
  std::vector<double> cum{0};
  for (size_t i = 1; i < pts.size(); i++) {
    cum.push_back(cum[i - 1] + hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z));
  }
  const double total = cum.back();
  const int n = std::max(1, int(num::round(total / step)));
  std::vector<StreetPoint> out;
  size_t j = 0;
  for (int k = 0; k <= n; k++) {
    const double s = (total * k) / n;
    while (j < pts.size() - 2 && cum[j + 1] < s) j++;
    const PlanPoint& a = pts[j];
    const PlanPoint& b = pts[j + 1];
    const double seg = num::max(1e-9, cum[j + 1] - cum[j]);
    const double t = clamp((s - cum[j]) / seg, 0, 1);
    const double x = a.x + (b.x - a.x) * t;
    const double z = a.z + (b.z - a.z) * t;
    out.push_back({x, level.ground_at(x, z), z, s, libm::atan2(b.x - a.x, b.z - a.z)});
  }
  // A smoother heading: each point's from its neighbours.
  for (size_t i = 0; i < out.size(); i++) {
    const StreetPoint& a = out[i == 0 ? 0 : i - 1];
    const StreetPoint& b = out[std::min(out.size() - 1, i + 1)];
    out[i].heading = libm::atan2(b.x - a.x, b.z - a.z);
  }
  return out;
}

std::optional<Streets> plan_streets(const mg::Level& level, std::span<const PlanPoint> pads) {
  namespace S = streets;
  if (!level.resort) return std::nullopt;
  const mg::ResortMap& resort = *level.resort;
  const mg::Hub& hub = resort.hub;
  if (hub.top.size() < 2) return std::nullopt;
  const uint32_t seed = num::to_uint32(level.seed);
  const double hx0 = hub.x0;
  const double hx1 = hub.x0 + hub.step * double(hub.top.size() - 1);
  const std::optional<mg::HubEdges> mid = mg::hub_at(hub, (hx0 + hx1) / 2);
  if (!mid) return std::nullopt;
  // The valley's side of the hub: away from the summit.
  const int32_t sv = level.mountain.summit.z < (mid->top + mid->bottom) / 2 ? 1 : -1;
  const Clear clear = clearance_of(level, pads);
  const mg::Vec3 village = resort.village;
  // The main lift: of the lifts leaving the hub, the one whose bottom station stands nearest the
  // village; the square stands below it.
  double lift_x = village.x;
  double best = kInf;
  for (const mg::Lift& lift : resort.lifts) {
    const std::optional<mg::HubEdges> e = mg::hub_at(hub, lift.bottom.x);
    if (!e || lift.bottom.z < e->top - 10 || lift.bottom.z > e->bottom + 10) continue;
    const double d = hypot(lift.bottom.x - village.x, lift.bottom.z - village.z);
    if (d < best) {
      best = d;
      lift_x = lift.bottom.x;
    }
  }
  const auto deal = [&](int32_t salt) { return pick(seed, "village-streets", 0, salt); };
  const double span0 = S::kSpanLeast + (S::kSpanMost - S::kSpanLeast) * deal(1);
  const double v_main = S::kMainLeast + (S::kMainMost - S::kMainLeast) * deal(2);
  const double v_back = v_main + S::kBackLeast + (S::kBackMost - S::kBackLeast) * deal(3);
  const double bend_m = (deal(4) * 2 - 1) * S::kBend;
  const double bend_b = (deal(5) * 2 - 1) * S::kBend;
  const int n_cross =
      S::kCrossesLeast + int(std::floor(deal(6) * (S::kCrossesMost - S::kCrossesLeast + 1)));
  const int road_end = deal(7) < 0.5 ? 0 : 1;
  const bool two_roads = deal(8) < S::kTwoRoads;
  const int park_side = deal(9) < 0.5 ? -1 : 1;
  for (const double share : S::kSpanShares) {
    const double span = span0 * share;
    for (const double off : S::kCentres) {
      const double cx = village.x + off;
      const double xa = num::round(cx - span / 2);
      const double xb = num::round(cx + span / 2);
      if (xa < hx0 + 10 || xb > hx1 - 10) continue;
      // The hub's valley edge furthest out along the village, so no street reaches into it.
      double z0 = sv > 0 ? -kInf : kInf;
      for (int i = 0; xa - 40 + 10 * i <= xb + 40; i++) {
        const double x = xa - 40 + 10 * i;
        const std::optional<mg::HubEdges> e = mg::hub_at(hub, clamp(x, hx0, hx1));
        if (!e) continue;
        z0 = sv > 0 ? num::max(z0, e->bottom) : num::min(z0, e->top);
      }
      if (!std::isfinite(z0)) continue;
      const double v_max = sv > 0 ? level.size - z0 : z0;
      if (v_max < v_back + 20) continue;
      const auto bow = [xa, xb](double amp, double x, double ph) {
        return amp * libm::sin(ge::core::kPi * ((x - xa) / (xb - xa)) + ph);
      };
      const Profile main_v = [=](double x) { return v_main + bow(bend_m, x, 0); };
      const Profile back_v = [=](double x) { return v_back + bow(bend_b, x, 0.4 * ge::core::kPi); };
      const Ask ask{xa,     xb,      z0,       sv,        main_v,    back_v, v_max,
                    lift_x, n_cross, road_end, two_roads, park_side, seed};
      const std::optional<Draft> draft = lay_draft(level, clear, ask);
      if (draft) return build(level, *draft);
    }
  }
  return std::nullopt;
}

uint64_t streets_hash(const Streets& plan, uint64_t h) {
  const auto mix = [&h](double v) {
    uint64_t u = 0;
    std::memcpy(&u, &v, sizeof u);
    for (int b = 0; b < 64; b += 8) {
      h ^= (u >> b) & 0xffu;
      h *= 0x100000001b3ull;
    }
  };
  mix(plan.valley);
  mix(plan.centre_x);
  mix(plan.centre_z);
  mix(plan.edge);
  mix(plan.square);
  mix(plan.carpark_side);
  for (const Street& st : plan.streets) {
    mix(double(st.kind));
    mix(st.from);
    mix(st.to);
    mix(st.exit ? 1 : 0);
    mix(st.length);
    for (const StreetPoint& p : st.points) {
      mix(p.x);
      mix(p.y);
      mix(p.z);
      mix(p.heading);
    }
  }
  for (const Junction& j : plan.junctions) {
    mix(j.x);
    mix(j.z);
    mix(double(j.streets.size()));
  }
  for (const VillageArea& a : plan.areas) {
    mix(double(a.kind));
    mix(a.x);
    mix(a.z);
    mix(a.half);
    mix(a.depth);
  }
  return h;
}

}  // namespace ge::mountain::village
