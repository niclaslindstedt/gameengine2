// WAKES AND RINGS (docs/water.md § Wakes): the waves a moving hull, a swimmer's stroke or a body
// dropped in leaves on the water, as RINGS: each a volume of water pushed aside at a point and a
// moment, spreading as the deep-water ring of an impulse (the Cauchy-Poisson problem). Its far
// field at a distance r and a time tau after it was set off is
//
//   eta = V g tau^2 / (4 sqrt(2) pi r^3) cos(g tau^2 / (4 r)),
//
// a train whose local wavenumber k = g tau^2 / (4 r^2) shortens behind its front, each length
// running at its own group speed. A hull moving on emits one ring every `interval` s along its
// track (`wake_trail`); summed, the rings interfere into the Kelvin wake: transverse waves behind
// the hull and diverging waves along its two arms, which open at 19.47 degrees either side of the
// track whatever the speed (the `float` lab measures it). A splash is one ring (`splash_ring`,
// float.hpp).
//
// THE TIER. Rings are evaluated where asked, like the waves; what is kept is bounded:
// `Wakes::life` s of rings (faded out over its last third), at most kMaxRings (the oldest go
// first), waves shorter than `Wakes::shortest` m damped out (the short waves near the source,
// which viscosity and the picture's pixels would lose anyway), and a ring's centre softened over
// its source's size so nothing is infinite. A game drops the rings of a hull no actor is near.
//
// `water::sample` adds a body's rings to its lake's or ocean's surface (world.hpp), so a float
// rocks in a passing boat's wake; the renderer draws the same rings (render/water.hpp).
//
// ge:summary Wakes and rings: impulse rings on deep water, a hull's trail, the Kelvin wake
// ge:keywords water wake ring rings kelvin cauchy-poisson impulse trail hull boat swimmer splash
// ge:keywords emit_ring wake_trail wake_at wakes dispersion group-speed
// ge:skill water
// ge:test tests/water_wake_test.cpp
#pragma once

#include <cstdint>
#include <vector>

namespace ge::water {

inline constexpr int kMaxRings = 1024;

// One impulse: `volume` m^3 pushed aside at (x, z) at `t0` over a source `size` m across.
struct Ring {
  double x = 0, z = 0;
  double t0 = 0;
  double volume = 0;
  double size = 0.5;
  double shortest = 0;  // m: its own shortest wave (a trail's: four times its rings' spacing)
  int body = 0;         // the body whose surface it is on
};

struct Wakes {
  std::vector<Ring> rings;  // oldest first
  double life = 24;         // s a ring is kept
  double shortest = 0.3;    // m: shorter waves are damped out
};

// What a body's rings add at a point: the height, its slope, and the water's velocity.
struct WakeAt {
  double height = 0;
  double dx = 0, dz = 0;       // d height / dx, dz
  double u = 0, v = 0, w = 0;  // m/s
};

// A ring set off now; rings older than the wakes' life, and the oldest past kMaxRings, go.
void emit_ring(Wakes& wakes, const Ring& ring);

// Where a hull last set off a ring.
struct Trail {
  double last = -1e300;  // s
};
// A hull `length` m long and `beam` m wide drawing `draught` m, at (x, z) moving at (vx, vz) m/s:
// one ring every `interval` s carrying a share of the water it pushes aside meanwhile (beam x
// draught x speed x interval, times `strength`; 0.3 stands a small boat's wake waves at the
// steepness measured on real ones, slopes of about 0.1), set off over the hull's length. A ring
// keeps no wave shorter than the hull (a hull makes few waves much shorter than itself: so a fast
// small hull's wake looks narrower than Kelvin's angle, as measured on real ones), nor than four
// times the track between two rings (a shorter one would not cancel between them, and the sum would
// alias into ridges along the track). A hull at rest sets off nothing.
void wake_trail(Wakes& wakes, Trail& trail, int body, double x, double z, double vx, double vz,
                double length, double beam, double draught, double t, double interval = 0.1,
                double strength = 0.3);

// One ring's height at a distance `r` m, `tau` s after it was set off (the far field above,
// softened and damped as the wakes keep it).
double ring_height(const Wakes& wakes, const Ring& ring, double r, double tau);

// Every ring of `body` at (x, z) at `t`.
WakeAt wake_at(const Wakes& wakes, int body, double x, double z, double t);

}  // namespace ge::water
