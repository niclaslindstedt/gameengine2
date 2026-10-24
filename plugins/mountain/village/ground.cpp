// The ground under the village (ground.hpp): every street's piece and every open place painted
// onto the grid, the higher ground kept where two overlap.
//
// ge:summary The ground under the village painted onto its grid, and the trees felled
// ge:keywords village ground bake-ground felled-trees mask paint
// ge:skill mountain
// ge:test tests/mountain_village_test.cpp
#include "mountain/village/ground.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"

namespace ge::mountain::village {

namespace {

namespace num = ge::core::num;
using ge::core::hypot;

constexpr double kCell = GroundMask::kCell;

struct Painter {
  GroundMask& m;
  void mark(int i, int j, Ground value) const {
    if (i < 0 || j < 0 || i >= m.cols || j >= m.rows) return;
    uint8_t& at = m.data[size_t(j) * size_t(m.cols) + size_t(i)];
    at = std::max(at, uint8_t(value));
  }
  double px(int i) const { return m.x0 + (i + 0.5) * kCell; }
  double pz(int j) const { return m.z0 + (j + 0.5) * kCell; }
  int col(double x) const { return int(std::floor((x - m.x0) / kCell)); }
  int row(double z) const { return int(std::floor((z - m.z0) / kCell)); }
};

// A street's pieces: its carriageway, its sides, the margins past them.
void paint_street(const Painter& p, const Street& st) {
  const Section& s = st.section;
  const double reach[2] = {side_reach(s, 0), side_reach(s, 1)};
  const double road[2] = {road_reach(s, 0), road_reach(s, 1)};
  const double out = num::max(reach[0], reach[1]) + lots::kFell + lots::kThin;
  for (size_t n = 1; n < st.points.size(); n++) {
    const StreetPoint& a = st.points[n - 1];
    const StreetPoint& b = st.points[n];
    const double ex = b.x - a.x;
    const double ez = b.z - a.z;
    const double len2 = num::max(1e-9, ex * ex + ez * ez);
    const double len = std::sqrt(len2);
    const int i0 = p.col(num::min(a.x, b.x) - out);
    const int i1 = p.col(num::max(a.x, b.x) + out) + 1;
    const int j0 = p.row(num::min(a.z, b.z) - out);
    const int j1 = p.row(num::max(a.z, b.z) + out) + 1;
    for (int j = j0; j <= j1; j++) {
      for (int i = i0; i <= i1; i++) {
        const double x = p.px(i);
        const double z = p.pz(j);
        const double t = num::max(0.0, num::min(1.0, ((x - a.x) * ex + (z - a.z) * ez) / len2));
        const double dx = x - (a.x + ex * t);
        const double dz = z - (a.z + ez * t);
        const double d = hypot(dx, dz);
        // Right of the way is (ez, -ex)/len: side 1.
        const int side = (dx * ez - dz * ex) / len > 0 ? 1 : 0;
        if (d <= road[side]) {
          p.mark(i, j, Ground::road);
        } else if (d <= reach[side]) {
          p.mark(i, j, Ground::street);
        } else if (d <= reach[side] + lots::kFell) {
          p.mark(i, j, Ground::felled);
        } else if (d <= reach[side] + lots::kFell + lots::kThin) {
          p.mark(i, j, Ground::garden);
        }
      }
    }
  }
}

// An open place: its rectangle street, the margin past it felled, the gardens past that.
void paint_area(const Painter& p, const VillageArea& a) {
  const double fx = ge::core::libm::sin(a.heading);
  const double fz = ge::core::libm::cos(a.heading);
  const double out = hypot(a.half, a.depth) + lots::kFell + lots::kThin + 2;
  for (int j = p.row(a.z - out); j <= p.row(a.z + out) + 1; j++) {
    for (int i = p.col(a.x - out); i <= p.col(a.x + out) + 1; i++) {
      const double dx = p.px(i) - a.x;
      const double dz = p.pz(j) - a.z;
      const double lx = dx * fz - dz * fx;
      const double lz = dx * fx + dz * fz;
      const double ox = num::max(0.0, std::fabs(lx) - a.half);
      const double oz = num::max(0.0, std::fabs(lz) - a.depth);
      const double d = hypot(ox, oz);
      if (d <= 0) {
        p.mark(i, j, Ground::street);
      } else if (d <= lots::kFell) {
        p.mark(i, j, Ground::felled);
      } else if (d <= lots::kFell + lots::kThin) {
        p.mark(i, j, Ground::garden);
      }
    }
  }
}

}  // namespace

GroundMask bake_ground(const Streets& plan) {
  GroundMask m;
  double x0 = std::numeric_limits<double>::infinity();
  double z0 = x0;
  double x1 = -x0;
  double z1 = -x0;
  for (const Street& st : plan.streets) {
    for (const StreetPoint& p : st.points) {
      x0 = num::min(x0, p.x);
      z0 = num::min(z0, p.z);
      x1 = num::max(x1, p.x);
      z1 = num::max(z1, p.z);
    }
  }
  if (!std::isfinite(x0)) return m;
  const double grow = lots::kThin + lots::kFell + 40;
  m.x0 = std::floor(x0 - grow);
  m.z0 = std::floor(z0 - grow);
  m.cols = int32_t(std::ceil((x1 + grow - m.x0) / kCell));
  m.rows = int32_t(std::ceil((z1 + grow - m.z0) / kCell));
  m.data.assign(size_t(m.cols) * size_t(m.rows), 0);
  const Painter p{m};
  for (const Street& st : plan.streets) paint_street(p, st);
  // THE JUNCTIONS: the carriageways' meeting, all road.
  for (const Junction& jn : plan.junctions) {
    if (jn.exit) continue;
    double r = 0;
    for (const int32_t k : jn.streets) {
      const Section& s = plan.streets[size_t(k)].section;
      r = num::max(r, num::max(road_reach(s, 0), road_reach(s, 1)));
    }
    for (int j = p.row(jn.z - r); j <= p.row(jn.z + r) + 1; j++) {
      for (int i = p.col(jn.x - r); i <= p.col(jn.x + r) + 1; i++) {
        if (hypot(p.px(i) - jn.x, p.pz(j) - jn.z) <= r) p.mark(i, j, Ground::road);
      }
    }
  }
  for (const VillageArea& a : plan.areas) paint_area(p, a);
  return m;
}

Ground ground_at(const GroundMask& m, double x, double z) {
  if (m.cols == 0) return Ground::none;
  const int i = int(std::floor((x - m.x0) / kCell));
  const int j = int(std::floor((z - m.z0) / kCell));
  if (i < 0 || j < 0 || i >= m.cols || j >= m.rows) return Ground::none;
  return Ground(m.data[size_t(j) * size_t(m.cols) + size_t(i)]);
}

std::vector<uint8_t> felled_trees(const mapgen::Level& level, const GroundMask& mask) {
  std::vector<uint8_t> out(level.trees.size(), 0);
  const uint32_t seed = num::to_uint32(level.seed);
  for (size_t i = 0; i < level.trees.size(); i++) {
    const mapgen::TreeDef& t = level.trees[i];
    const Ground at = ground_at(mask, t.x, t.z);
    if (at >= Ground::felled) {
      out[i] = 1;
    } else if (at == Ground::garden) {
      const auto k = int32_t(num::round(t.x * 7 + t.z * 13));
      out[i] = pick(seed, "garden", k, 3) < lots::kKeep ? 0 : 1;
    }
  }
  return out;
}

}  // namespace ge::mountain::village
