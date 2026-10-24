// press: a contact's imprint in a snow layer (press.hpp).
//
// ge:summary press's implementation: targets under the outline, mass kept, the berm laid
// ge:keywords snow press imprint berm compaction shape_u shape_area reach_density column_sink
// ge:skill snow
// ge:test tests/snow_press_test.cpp
#include "snow/press.hpp"

#include <cmath>
#include <cstdint>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "snow/contact.hpp"

namespace ge::snow {

namespace {

// THE DENSITY A PRESSURE PACKS SNOW TO: 350 kg/m3 at 10 kPa, 60 kg/m3 more each e-fold.
constexpr double kReachAt = 350;   // kg/m3
constexpr double kReachPa = 1e4;   // Pa
constexpr double kReachPerE = 60;  // kg/m3
// A berm's snow is loosened by the throw: laid no denser than this, and no looser than the second.
constexpr double kBermDensest = 450;
constexpr double kBermLoosest = 150;
// The narrowest an outline is read at, and the least reach of its berm, in samples.
constexpr double kNarrowest = 0.75;
constexpr double kBermFewest = 1.5;

double ipow(double v, int n) {
  double r = 1;
  for (int i = 0; i < n; i++) r *= v;
  return r;
}

uint16_t minute_of(double t) {
  const double m = std::floor(t / 60);
  return m <= 0 ? 0 : m >= 65535 ? 65535 : static_cast<uint16_t>(m);
}

// The outline's box, widened by `grow` of its half width, in samples clipped to the layer.
struct Span {
  int c0, r0, c1, r1;
};

Span span_of(const Layer& l, const Shape& s, double grow) {
  const double w = s.half_width * grow;
  double x0, x1, z0, z1;
  if (s.kind == ShapeKind::capsule) {
    x0 = (s.ax < s.bx ? s.ax : s.bx) - w;
    x1 = (s.ax > s.bx ? s.ax : s.bx) + w;
    z0 = (s.az < s.bz ? s.az : s.bz) - w;
    z1 = (s.az > s.bz ? s.az : s.bz) + w;
  } else {
    const double r = (s.half_length > s.half_width ? s.half_length : s.half_width) * grow;
    x0 = s.ax - r;
    x1 = s.ax + r;
    z0 = s.az - r;
    z1 = s.az + r;
  }
  const double inv = 1 / l.spec.cell;
  Span sp;
  sp.c0 = static_cast<int>(std::ceil((x0 - l.spec.region.x0) * inv));
  sp.c1 = static_cast<int>(std::floor((x1 - l.spec.region.x0) * inv));
  sp.r0 = static_cast<int>(std::ceil((z0 - l.spec.region.z0) * inv));
  sp.r1 = static_cast<int>(std::floor((z1 - l.spec.region.z0) * inv));
  sp.c0 = sp.c0 < 0 ? 0 : sp.c0;
  sp.r0 = sp.r0 < 0 ? 0 : sp.r0;
  sp.c1 = sp.c1 >= l.cols ? l.cols - 1 : sp.c1;
  sp.r1 = sp.r1 >= l.rows ? l.rows - 1 : sp.r1;
  return sp;
}

// The berm's weight at `u` (1 .. 1 + reach): heaped at the edge, falling to nothing at the reach.
int berm_weight(double u, double reach) {
  const double f = 1 - (u - 1) / reach;
  return f <= 0 ? 0 : 1 + static_cast<int>(std::floor(f * 100));
}

}  // namespace

double shape_area(const Shape& s) {
  const double w = s.half_width;
  switch (s.kind) {
    case ShapeKind::capsule:
      return 2 * w * core::hypot(s.bx - s.ax, s.bz - s.az) + core::kPi * w * w;
    case ShapeKind::disc:
      return core::kPi * w * w;
    case ShapeKind::box:
      return 4 * s.half_length * w;
    case ShapeKind::ellipse:
      return core::kPi * s.half_length * w;
  }
  return 0;
}

namespace {

// Where (x, z) lies across `s`, its heading's sine and cosine given (a press turns them once).
double u_in(const Shape& s, double sh, double ch, double x, double z) {
  const double w = s.half_width;
  if (w <= 0) return 2;
  if (s.kind == ShapeKind::capsule) {
    const double dx = s.bx - s.ax, dz = s.bz - s.az;
    const double len2 = dx * dx + dz * dz;
    double t = len2 > 0 ? ((x - s.ax) * dx + (z - s.az) * dz) / len2 : 0;
    t = core::clamp(t, 0, 1);
    const double px = s.ax + t * dx - x, pz = s.az + t * dz - z;
    return std::sqrt(px * px + pz * pz) / w;
  }
  if (s.kind == ShapeKind::disc) {
    const double px = x - s.ax, pz = z - s.az;
    return std::sqrt(px * px + pz * pz) / w;
  }
  // Box and ellipse: in the outline's own frame (forward along the heading, right across it).
  const double px = x - s.ax, pz = z - s.az;
  const double along = px * sh + pz * ch, across = px * ch - pz * sh;
  const double a = s.half_length > 0 ? along / s.half_length : 0;
  const double b = across / w;
  if (s.kind == ShapeKind::box) {
    const double fa = std::fabs(a), fb = std::fabs(b);
    return fa > fb ? fa : fb;
  }
  return std::sqrt(a * a + b * b);
}

}  // namespace

double shape_u(const Shape& s, double x, double z) {
  return u_in(s, core::libm::sin(s.heading), core::libm::cos(s.heading), x, z);
}

double default_side_push(Motion m) {
  switch (m) {
    case Motion::step:
      return 0;
    case Motion::roll:
      return 0.1;
    case Motion::slide:
      return 0.4;
    case Motion::plough:
      return 0.8;
    case Motion::dig:
      return 1;
  }
  return 0;
}

double reach_density(double pa, double cap) {
  if (pa <= 0) return 0;
  const double rho = kReachAt + kReachPerE * core::libm::log(pa / kReachPa);
  return rho < 0 ? 0 : rho > cap ? cap : rho;
}

Pressed press(Layer& layer, const Imprint& imp, double t) {
  Pressed out;
  const Shape& s = imp.shape;
  if (s.half_width <= 0) return out;
  const double area = imp.area > 0 ? imp.area : shape_area(s);
  out.pressure = area > 0 ? imp.load / area : 0;
  const bool by_sink = imp.sink >= 0;
  if (!by_sink && out.pressure <= 0) return out;
  const double push =
      core::clamp(imp.side_push >= 0 ? imp.side_push : default_side_push(imp.motion), 0, 1);
  const int wall = imp.wall < 1 ? 1 : imp.wall;
  // An outline narrower than the samples could fall between them: it is widened to reach one (its
  // pressure stays its own), and its berm to reach the next ring out. The fine marks drawn on the
  // GPU keep the true width (docs/snow.md § Drawn).
  const double pitch = layer.spec.cell;
  Shape g = s;
  if (g.half_width < kNarrowest * pitch) g.half_width = kNarrowest * pitch;
  if (g.kind != ShapeKind::capsule && g.half_length < kNarrowest * pitch) {
    g.half_length = kNarrowest * pitch;
  }
  double reach = imp.berm_reach > 0 ? imp.berm_reach : 0;
  if (reach > 0 && reach * g.half_width < kBermFewest * pitch)
    reach = kBermFewest * pitch / g.half_width;
  const double kg = kMassUnit * pitch * pitch;  // a mass unit of one sample, kg
  const double sh = core::libm::sin(g.heading), ch = core::libm::cos(g.heading);
  // Bekker's width is the true outline's narrower side, not the widened one's.
  const double b = bearing_width(s.half_width, s.kind == ShapeKind::capsule ? 0 : s.half_length);
  const uint16_t minute = minute_of(t);

  // THE OUTLINE: every sample brought down to its target, the thrown mass summed.
  uint64_t thrown = 0;
  double thrown_volume = 0;  // m3 a m2 of what was thrown, for the berm's density
  uint64_t compacted = 0;
  Kind kind = layer.spec.cover.kind;
  const Span in = span_of(layer, g, 1);
  for (int r = in.r0; r <= in.r1; r++) {
    for (int c = in.c0; c <= in.c1; c++) {
      const double x = sample_x(layer, c), z = sample_z(layer, r);
      const double u = u_in(g, sh, ch, x, z);
      if (u > 1) continue;
      const double ease = 1 - ipow(u, wall);
      const CoverSample cov = cover_at(layer.spec.cover, x, z);
      double target;
      if (by_sink) {
        target = cov.depth - imp.sink * ease;
      } else {
        target = cov.depth - column_sink(cov.kind, cov.depth, cov.density, b, out.pressure * ease);
      }
      if (target < 0) target = 0;
      const auto target_mm = static_cast<uint16_t>(core::num::round(target * 1000));
      const Cell* seen = cell_at(layer, c, r);
      if (seen && seen->depth_mm <= target_mm) continue;
      if (!seen && core::num::round(cov.depth * 1000) <= target_mm) continue;
      Cell* cell = cell_for(layer, c, r);
      if (!cell) {
        out.refused++;
        continue;
      }
      if (cell->depth_mm <= target_mm) continue;
      const double h = cell_depth(*cell);
      const double rho = cell_density(*cell);
      const double cut = h - target_mm * 0.001;
      const double cap = kind_row(cell->kind).cap;
      // Thrown: the pushed share of what was cut, and whatever the cap will not let stay.
      double throw_units = core::num::round(push * cut * rho / kMassUnit);
      const double keep_most = core::num::round(cap * target_mm * 0.001 / kMassUnit);
      if (cell->mass - throw_units > keep_most) throw_units = cell->mass - keep_most;
      if (target_mm == 0) throw_units = cell->mass;
      if (throw_units < 0) throw_units = 0;
      if (throw_units > cell->mass) throw_units = cell->mass;
      const auto thrown_here = static_cast<uint16_t>(throw_units);
      thrown += thrown_here;
      if (rho > 0) thrown_volume += thrown_here * kMassUnit / rho;
      compacted += cell->mass - thrown_here;
      cell->mass = static_cast<uint16_t>(cell->mass - thrown_here);
      cell->depth_mm = target_mm;
      cell->flags = static_cast<uint8_t>((cell->flags | kTouched | kPressed) & ~kBerm);
      cell->minute = minute;
      kind = cell->kind;
      out.samples++;
      const double sunk = cov.depth - target_mm * 0.001;
      if (sunk > out.sink) out.sink = sunk;
    }
  }
  out.thrown = static_cast<double>(thrown) * kg;

  // THE BERM: the thrown mass over the ring outside the outline, heaped at its edge.
  if (thrown > 0) {
    const double berm_rho = core::clamp(
        thrown_volume > 0 ? out.thrown / thrown_volume : kBermLoosest, kBermLoosest, kBermDensest);
    const Span ring = span_of(layer, g, 1 + reach);
    uint64_t total_weight = 0;
    for (int pass = 0; pass < 2; pass++) {
      uint64_t left = thrown;  // what is still to lay (pass 1)
      for (int r = ring.r0; r <= ring.r1; r++) {
        for (int c = ring.c0; c <= ring.c1; c++) {
          const double u = u_in(g, sh, ch, sample_x(layer, c), sample_z(layer, r));
          if (u <= 1) continue;
          const int w = berm_weight(u, reach);
          if (w <= 0) continue;
          if (pass == 0) {
            total_weight += w;
            continue;
          }
          uint64_t share = total_weight ? thrown * w / total_weight : 0;
          if (share > left) share = left;
          Cell* cell = cell_for(layer, c, r);
          if (!cell) {
            out.refused++;
            continue;  // its share stays in `left` and spills
          }
          if (cell->mass + share > kMaxMass) share = kMaxMass - cell->mass;
          if (share == 0) continue;
          left -= share;
          const double add_mm =
              core::num::round(static_cast<double>(share) * kMassUnit / berm_rho * 1000);
          const double depth = cell->depth_mm + add_mm;
          cell->depth_mm = depth > kMaxDepthMm ? kMaxDepthMm : static_cast<uint16_t>(depth);
          cell->mass = static_cast<uint16_t>(cell->mass + share);
          cell->flags = static_cast<uint8_t>(cell->flags | kTouched | kBerm);
          cell->minute = minute;
        }
      }
      if (pass == 1 && left > 0) {
        // The rounding's remainder, and whatever found no sample: onto the ring's samples in
        // order, a unit each, until none is left or none takes one.
        for (int r = ring.r0; r <= ring.r1 && left > 0; r++) {
          for (int c = ring.c0; c <= ring.c1 && left > 0; c++) {
            const double u = u_in(g, sh, ch, sample_x(layer, c), sample_z(layer, r));
            if (u <= 1 || berm_weight(u, reach) <= 0) continue;
            Cell* cell = cell_for(layer, c, r);
            if (!cell || cell->mass == kMaxMass) continue;
            cell->mass = static_cast<uint16_t>(cell->mass + 1);
            cell->flags = static_cast<uint8_t>(cell->flags | kTouched | kBerm);
            left--;
          }
        }
        layer.spilled += left;
        out.spilled = static_cast<double>(left) * kg;
      }
    }
  }
  out.compacted = static_cast<double>(compacted) * kg;

  if (out.samples > 0) {
    if (layer.records.size() < layer.spec.max_records) {
      layer.records.push_back(ImprintRecord{s, imp.motion, kind, static_cast<float>(out.sink),
                                            static_cast<float>(out.thrown), t});
    } else {
      layer.records_dropped++;
    }
  }
  return out;
}

}  // namespace ge::snow
