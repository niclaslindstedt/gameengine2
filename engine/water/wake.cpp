// Wakes and rings: the impulse ring's far field, a hull's trail, the sum at a point.
//
// ge:summary Wakes: the ring's far field, the hull's trail, the sum at a point
// ge:keywords water wake ring kelvin cauchy-poisson trail emit_ring wake_at
// ge:skill water
// ge:test tests/water_wake_test.cpp
#include "water/wake.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "water/spectrum.hpp"

namespace ge::water {

namespace {

namespace lm = core::libm;
constexpr double kPi = 3.14159265358979323846;
constexpr double kRoot2 = 1.41421356237309504880;

// The share a ring keeps at `tau` s old: whole, then faded over the last third of its life.
double kept(const Wakes& wakes, double tau) {
  return 1 - core::smoothstep(wakes.life * (2.0 / 3.0), wakes.life, tau);
}

}  // namespace

void emit_ring(Wakes& wakes, const Ring& ring) {
  std::vector<Ring>& r = wakes.rings;
  size_t old = 0;
  while (old < r.size() && ring.t0 - r[old].t0 >= wakes.life) ++old;
  if (r.size() - old + 1 > static_cast<size_t>(kMaxRings)) old = r.size() + 1 - kMaxRings;
  if (old > 0) r.erase(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(old));
  r.push_back(ring);
}

void wake_trail(Wakes& wakes, Trail& trail, int body, double x, double z, double vx, double vz,
                double length, double beam, double draught, double t, double interval,
                double strength) {
  if (t - trail.last < interval) return;
  const double speed = core::hypot(vx, vz);
  const double gap = trail.last < -1e299 ? interval : t - trail.last;
  trail.last = t;
  if (speed <= 0) return;
  Ring ring;
  ring.x = x;
  ring.z = z;
  ring.t0 = t;
  ring.volume = strength * beam * draught * speed * gap;
  ring.size = 0.25 * length;
  ring.shortest = 0.5 * length > 4 * speed * gap ? 0.5 * length : 4 * speed * gap;
  ring.body = body;
  emit_ring(wakes, ring);
}

double ring_height(const Wakes& wakes, const Ring& ring, double r, double tau) {
  if (tau <= 0) return 0;
  const double rs = std::sqrt(r * r + ring.size * ring.size);
  const double phase = kGravity * tau * tau / (4 * rs);
  const double k = phase / rs;
  const double floor = wakes.shortest > ring.shortest ? wakes.shortest : ring.shortest;
  const double keep = core::smoothstep(floor, 2 * floor, 2 * kPi / k);
  if (keep <= 0) return 0;
  const double a = ring.volume * k / (kRoot2 * kPi * rs) * keep * kept(wakes, tau);
  return a * lm::cos(phase);
}

WakeAt wake_at(const Wakes& wakes, int body, double x, double z, double t) {
  WakeAt out;
  for (const Ring& ring : wakes.rings) {
    if (ring.body != body) continue;
    const double tau = t - ring.t0;
    if (tau <= 0) continue;
    const double ex = x - ring.x, ez = z - ring.z;
    const double rs = std::sqrt(ex * ex + ez * ez + ring.size * ring.size);
    const double phase = kGravity * tau * tau / (4 * rs);
    const double k = phase / rs;
    const double floor = wakes.shortest > ring.shortest ? wakes.shortest : ring.shortest;
    const double keep = core::smoothstep(floor, 2 * floor, 2 * kPi / k);
    if (keep <= 0) continue;
    const double a = ring.volume * k / (kRoot2 * kPi * rs) * keep * kept(wakes, tau);
    const double c = lm::cos(phase), s = lm::sin(phase);
    // The phase falls outward (d phase / dr = -k), so the slope is a k sin(phase) outward; the
    // water moves outward under a crest at the wave's frequency, omega = sqrt(g k).
    const double omega = kGravity * tau / (2 * rs);
    const double ox = ex / rs, oz = ez / rs;
    out.height += a * c;
    out.dx += a * k * s * ox;
    out.dz += a * k * s * oz;
    out.u += omega * a * c * ox;
    out.w += omega * a * c * oz;
    out.v -= omega * a * s;
  }
  return out;
}

}  // namespace ge::water
