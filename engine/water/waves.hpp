// THE WAVES THE SIMULATION READS (docs/water.md § Waves, the simulation tier): a sea's energetic
// band laid as a few components and summed where asked, so a probe, a swimmer and the GPU (which
// draws the same components in the same formula) all see the same crest. Pure: a `Waves` is laid
// once a body and read at any (x, z, t).
//
// - LAYING (`lay`): the band from half the peak frequency up to the shortest wave the simulation
//   keeps (`Lay::shortest`, a wavelength; shorter ones are the picture's detail tier) is cut into
//   `Lay::count` slices of EQUAL WEIGHT, the weight the energy times 1 + (w / wp)^2: half by
//   energy, so the components crowd onto the peak and the peak reads as one wave train, and half
//   by slope, so the tail is not left to one or two short waves that would each carry a slice of
//   its steepness and stripe the surface. Each slice's component carries the slice's energy
//   (a = sqrt(2 E)) at the slice's weighted median frequency; its direction is drawn through the
//   spread at that frequency by inverse transform at a stratified quantile, the quantiles shuffled
//   across the slices, so the fan is even and neighbouring frequencies point different ways. The
//   phases and the shuffle come from a `core::Rng` on the body's seed.
// - THE SURFACE (`surface_at`): each component a linear wave with its second-order crest,
//   eta = a sin(phi) - 1/2 k a^2 cos(2 phi), phi = k.x - w t + phase: a sharp crest and a flat
//   trough with the height a function of the undisplaced point. The slopes are exact
//   derivatives; the water's velocity at the surface is linear theory's orbital motion (the
//   horizontal along each wave times coth(k d) in finite depth, the vertical the surface's own
//   rise). `scale` multiplies every amplitude (a body's local fetch, the shallows).
// - BREAKING (`breaking_at`): three 0..1 shares the foam reads: a crest steeper than the limiting
//   wave's 30 degrees spilling, a wave over 0.78 of the depth on the shoal, and the wind blowing
//   the tops off (the whitecap cover, 3.84e-6 U^3.41).
//
// ge:summary The simulation tier's waves: components by energy and slope, the 2nd-order surface
// ge:keywords water waves components lay equal-energy slope-weighted stokes second-order crest
// ge:keywords orbital velocity slope surface_at breaking whitecap steepness shoal breaking_at
// ge:skill water determinism
// ge:test tests/water_waves_test.cpp
#pragma once

#include <cstdint>

#include "water/spectrum.hpp"

namespace ge::water {

inline constexpr int kMaxComponents = 32;

struct Component {
  double w = 0;   // angular frequency, rad/s
  double k = 0;   // wavenumber, rad/m
  double kx = 0;  // k along x and z (the heading it runs toward)
  double kz = 0;
  double a = 0;      // amplitude, m
  double phase = 0;  // rad
  double coth = 1;   // coth(k d): the orbital speed's growth in finite depth
};

// How a sea's band is laid.
struct Lay {
  int count = 24;         // components, up to kMaxComponents
  double shortest = 1.5;  // m: the shortest wavelength the simulation keeps
  uint32_t seed = 1;
};

struct Waves {
  Sea sea;
  int count = 0;
  Component c[kMaxComponents]{};
  double m0 = 0;              // the laid band's variance, m^2 (its Hs is 4 sqrt(m0))
  double m2 = 0;              // its second moment, m^2 / s^2 (its mean period 2 pi sqrt(m0 / m2))
  double w_lo = 0, w_hi = 0;  // the band laid, rad/s
};

Waves lay(const Sea& sea, const Lay& how);

// The surface at a point and a moment.
struct Surface {
  double height = 0;           // m over the still level
  double dx = 0, dz = 0;       // slopes d(height)/dx, d(height)/dz
  double u = 0, v = 0, w = 0;  // the water's velocity at the surface, m/s (x, up, z)
};

Surface surface_at(const Waves& waves, double x, double z, double t, double scale = 1);

struct Breaking {
  double crest = 0;  // a steep crest spilling
  double shoal = 0;  // the bed tripping it
  double wind = 0;   // the wind blowing the tops off (the whitecap cover)
};

// The breaking shares at a surface over `depth` m of water under a wind of `wind` m/s at 10 m.
Breaking breaking_at(const Surface& s, double depth, double wind);

}  // namespace ge::water
