// A hot surface's glow: definitions.
//
// ge:summary A hot surface's glow: Planck's law weighed by the CIE observer's fit: definitions
// ge:keywords glow planck cie multi-lobe xyz srgb luminance gamut
// ge:skill water render
// ge:test tests/water_glow_test.cpp
#include "water/glow.hpp"

#include "core/libm/libm.hpp"
#include "core/num.hpp"

namespace ge::water {

namespace {

namespace lm = core::libm;
constexpr double kH = 6.62607015e-34;  // J s
constexpr double kC = 2.99792458e8;    // m/s
constexpr double kK = 1.380649e-23;    // J/K
constexpr double kLumens = 683;        // lm/W
constexpr double kGlowStepNm = 5;

// One lobe of the observer's fit: a Gaussian with a width either side of its peak.
double lobe(double nm, double mu, double s1, double s2) {
  const double t = (nm - mu) / (nm < mu ? s1 : s2);
  return lm::exp(-0.5 * t * t);
}

double xbar(double nm) {
  return 1.056 * lobe(nm, 599.8, 37.9, 31.0) + 0.362 * lobe(nm, 442.0, 16.0, 26.7) -
         0.065 * lobe(nm, 501.1, 20.4, 26.2);
}
double ybar(double nm) {
  return 0.821 * lobe(nm, 568.8, 46.9, 40.5) + 0.286 * lobe(nm, 530.9, 16.3, 31.1);
}
double zbar(double nm) {
  return 1.217 * lobe(nm, 437.0, 11.8, 36.0) + 0.681 * lobe(nm, 459.0, 26.0, 13.8);
}

// The spectral radiance at `nm` of a black body at `kelvin`, W/(m^2 sr nm).
double planck(double nm, double kelvin) {
  const double l = nm * 1e-9;
  const double x = kH * kC / (l * kK * kelvin);
  if (x > 700) return 0;
  return 2 * kH * kC * kC / (l * l * l * l * l) / (lm::exp(x) - 1) * 1e-9;
}

}  // namespace

Glow glow_of(double kelvin, double emissivity) {
  Glow g;
  if (kelvin <= 0) return g;
  double x = 0, y = 0, z = 0;
  const int steps = static_cast<int>((780 - 380) / kGlowStepNm);
  for (int i = 0; i <= steps; ++i) {
    const double nm = 380 + i * kGlowStepNm;
    const double l = planck(nm, kelvin) * kGlowStepNm;
    x += l * xbar(nm);
    y += l * ybar(nm);
    z += l * zbar(nm);
  }
  g.luminance = kLumens * y * emissivity;
  if (y <= 0) return g;
  // XYZ (Y = 1) to linear sRGB; a colour past the gamut's red is clipped and scaled back to
  // luminance 1.
  x /= y;
  z /= y;
  double r = 3.2406 * x - 1.5372 - 0.4986 * z;
  double gr = -0.9689 * x + 1.8758 + 0.0415 * z;
  double b = 0.0557 * x - 0.2040 + 1.0570 * z;
  r = core::num::max(0.0, r);
  gr = core::num::max(0.0, gr);
  b = core::num::max(0.0, b);
  const double lum = 0.2126 * r + 0.7152 * gr + 0.0722 * b;
  if (lum > 0) {
    r /= lum;
    gr /= lum;
    b /= lum;
  }
  g.colour = {r, gr, b};
  return g;
}

std::vector<Rgb> glow_table(double lo, double hi, int count, double emissivity) {
  std::vector<Rgb> out;
  for (int i = 0; i < count; ++i) {
    const double t = count > 1 ? lo + (hi - lo) * i / (count - 1) : lo;
    const Glow g = glow_of(t, emissivity);
    out.push_back({g.colour.r * g.luminance, g.colour.g * g.luminance, g.colour.b * g.luminance});
  }
  return out;
}

}  // namespace ge::water
