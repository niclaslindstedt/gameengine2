// The fetch over a body's outline: definitions.
//
// ge:summary The fetch over a body's outline: definitions
// ge:keywords water fetch ray polygon crossing grid bilinear
// ge:skill water
// ge:test tests/water_fetch_test.cpp
#include "water/fetch.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"

namespace ge::water {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr int kRays = 15;                         // -42 .. +42 degrees
constexpr double kRayStep = 0.10471975511965977;  // 6 degrees

}  // namespace

bool inside(std::span<const Point> outline, double x, double z) {
  bool in = false;
  const size_t n = outline.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const Point& a = outline[i];
    const Point& b = outline[j];
    if ((a.z > z) != (b.z > z)) {
      const double cx = a.x + (z - a.z) * (b.x - a.x) / (b.z - a.z);
      if (x < cx) in = !in;
    }
  }
  return in;
}

double to_shore(std::span<const Point> outline, double x, double z, double heading) {
  if (outline.size() < 3 || !inside(outline, x, z)) return 0;
  const double dx = core::libm::sin(heading), dz = core::libm::cos(heading);
  double best = 1e300;
  const size_t n = outline.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double ex = outline[i].x - outline[j].x, ez = outline[i].z - outline[j].z;
    const double den = dx * ez - dz * ex;
    if (den == 0) continue;
    const double wx = outline[j].x - x, wz = outline[j].z - z;
    const double t = (wx * ez - wz * ex) / den;  // along the ray
    const double u = (wx * dz - wz * dx) / den;  // along the edge, 0..1
    if (t > 0 && u >= 0 && u <= 1) best = core::num::min(best, t);
  }
  return best < 1e300 ? best : 0;
}

double effective_fetch(std::span<const Point> outline, double x, double z, double toward) {
  const double upwind = toward + kPi;
  double num = 0, den = 0;
  constexpr int kMiddle = (kRays - 1) / 2;
  for (int r = 0; r < kRays; ++r) {
    const double off = (r - kMiddle) * kRayStep;
    const double c = core::libm::cos(off);
    num += to_shore(outline, x, z, upwind + off) * c * c;
    den += c;
  }
  return den > 0 ? num / den : 0;
}

FetchGrid bake_fetch(std::span<const Point> outline, double toward, int cells) {
  FetchGrid g;
  if (outline.size() < 3) return g;
  double x0 = outline[0].x, x1 = x0, z0 = outline[0].z, z1 = z0;
  for (const Point& p : outline) {
    x0 = core::num::min(x0, p.x);
    x1 = core::num::max(x1, p.x);
    z0 = core::num::min(z0, p.z);
    z1 = core::num::max(z1, p.z);
  }
  g.cell = core::num::max(x1 - x0, z1 - z0) / (cells < 2 ? 2 : cells);
  if (g.cell <= 0) return g;
  g.x0 = x0;
  g.z0 = z0;
  g.nx = static_cast<int>((x1 - x0) / g.cell) + 2;
  g.nz = static_cast<int>((z1 - z0) / g.cell) + 2;
  g.fetch.assign(static_cast<size_t>(g.nx) * static_cast<size_t>(g.nz), 0.0f);
  for (int k = 0; k < g.nz; ++k) {
    for (int i = 0; i < g.nx; ++i) {
      const double f = effective_fetch(outline, x0 + i * g.cell, z0 + k * g.cell, toward);
      g.fetch[static_cast<size_t>(k) * static_cast<size_t>(g.nx) + static_cast<size_t>(i)] =
          static_cast<float>(f);
      g.longest = core::num::max(g.longest, f);
    }
  }
  return g;
}

double fetch_at(const FetchGrid& g, double x, double z) {
  if (g.nx <= 0 || g.nz <= 0) return 0;
  const double fx = core::clamp((x - g.x0) / g.cell, 0, g.nx - 1);
  const double fz = core::clamp((z - g.z0) / g.cell, 0, g.nz - 1);
  const int i0 = static_cast<int>(fx), k0 = static_cast<int>(fz);
  const int i1 = i0 + 1 < g.nx ? i0 + 1 : i0, k1 = k0 + 1 < g.nz ? k0 + 1 : k0;
  const double u = fx - i0, v = fz - k0;
  const auto at = [&](int i, int k) {
    return static_cast<double>(
        g.fetch[static_cast<size_t>(k) * static_cast<size_t>(g.nx) + static_cast<size_t>(i)]);
  };
  return core::lerp(core::lerp(at(i0, k0), at(i1, k0), u), core::lerp(at(i0, k1), at(i1, k1), u),
                    v);
}

}  // namespace ge::water
