// The colour of water: definitions.
//
// ge:summary The colour of water from what is in it: definitions and the looks' rows
// ge:keywords water optics absorption scattering reflectance shallow cie d65 srgb look table
// ge:skill water
// ge:test tests/water_optics_test.cpp
#include "water/optics.hpp"

#include <cmath>

#include "core/libm/libm.hpp"

namespace ge::water {

namespace {

namespace lm = core::libm;

// Pure water's absorption, /m, 400 to 700 nm every 10 nm (the measured spectrum of pure water).
constexpr Spectrum kWater{0.00663, 0.00473, 0.00454, 0.00495, 0.00635, 0.00922, 0.00979, 0.0106,
                          0.0127,  0.0150,  0.0204,  0.0325,  0.0409,  0.0434,  0.0474,  0.0565,
                          0.0619,  0.0695,  0.0896,  0.1351,  0.2224,  0.2644,  0.2755,  0.2916,
                          0.3108,  0.340,   0.410,   0.439,   0.465,   0.516,   0.624};

// Daylight (CIE D65), relative, 400 to 700 nm every 10 nm.
constexpr Spectrum kD65{82.75,  91.49,  93.43,  86.68,  104.86, 117.01, 117.81, 114.86,
                        115.92, 108.81, 109.35, 107.80, 104.79, 107.69, 104.41, 104.05,
                        100.00, 96.33,  95.79,  88.69,  90.01,  89.60,  87.70,  83.29,
                        83.70,  80.03,  80.21,  82.28,  78.28,  69.72,  71.61};

constexpr double kPi = 3.14159265358979323846;

// A lobe of the CIE observer's multi-lobe fit: a Gaussian with one width each side of its peak.
double lobe(double nm, double mu, double below, double above) {
  const double s = (nm < mu ? below : above);
  const double t = (nm - mu) / s;
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

// Phytoplankton's absorption, relative to 440 nm, every 10 nm (a typical shape: the chlorophyll's
// blue and red peaks, the accessory pigments' shoulder toward the green, the low window between).
constexpr Spectrum kPlankton{0.70, 0.78, 0.85, 0.93, 1.00, 0.96, 0.90, 0.83, 0.75, 0.65, 0.55,
                             0.45, 0.35, 0.27, 0.20, 0.15, 0.12, 0.10, 0.10, 0.10, 0.11, 0.12,
                             0.13, 0.14, 0.15, 0.20, 0.30, 0.45, 0.45, 0.20, 0.06};

// The bed's albedo between its three anchors (450, 550, 650 nm), held past them.
double bed_at(const Constituents& c, double nm) {
  if (nm <= 450) return c.bed[0];
  if (nm >= 650) return c.bed[2];
  if (nm <= 550) return c.bed[0] + (c.bed[1] - c.bed[0]) * (nm - 450) / 100;
  return c.bed[1] + (c.bed[2] - c.bed[1]) * (nm - 550) / 100;
}

// A spectrum's band average (daylight-weighted) over [lo, hi] nm.
double band(const Spectrum& s, double lo, double hi) {
  double sum = 0, w = 0;
  for (int i = 0; i < kBands; ++i) {
    const double nm = wavelength(i);
    if (nm < lo || nm > hi) continue;
    sum += s[i] * kD65[i];
    w += kD65[i];
  }
  return sum / w;
}

Rgb bands(const Spectrum& s) { return {band(s, 580, 700), band(s, 490, 580), band(s, 400, 490)}; }

constexpr double kSunInWater = 1.07;  // 1 / cos of the sun's refracted angle, 30 degrees up
constexpr double kVeil = 2;           // the veil's brightness over the upwelling's

}  // namespace

double wavelength(int band) { return kFirstNm + kStepNm * band; }

double water_absorption(double nm) {
  const double i = (nm - kFirstNm) / kStepNm;
  if (i <= 0) return kWater[0];
  if (i >= kBands - 1) return kWater[kBands - 1];
  const int k = static_cast<int>(i);
  return kWater[k] + (kWater[k + 1] - kWater[k]) * (i - k);
}

Iops iops(const Constituents& c) {
  Iops o;
  const double chl = c.chlorophyll;
  const double ph440 = chl > 0 ? 0.0654 * lm::pow(chl, 0.728) : 0;
  const double ph_b550 = chl > 0 ? 0.416 * lm::pow(chl, 0.766) : 0;
  for (int i = 0; i < kBands; ++i) {
    const double nm = wavelength(i);
    const double bw = 0.00288 * lm::pow(nm / 500, -4.32);
    const double ag = c.cdom * lm::exp(-c.cdom_slope * (nm - 440));
    const double aph = ph440 * kPlankton[i];
    const double bph = ph_b550 * (550 / nm);
    const double anap = c.sediment * 0.041 * lm::exp(-0.0123 * (nm - 443));
    const double bnap = c.sediment * c.sediment_scatter * lm::pow(550 / nm, c.sediment_slope);
    o.a[i] = kWater[i] + ag + aph + anap;
    o.b[i] = bw + bph + bnap + c.bubbles;
    o.bb[i] = 0.5 * bw + 0.01 * bph + 0.0183 * bnap + 0.05 * c.bubbles;
  }
  return o;
}

Spectrum kd(const Iops& in) {
  Spectrum k{};
  for (int i = 0; i < kBands; ++i)
    k[i] = 1.15 * in.a[i] + 4.18 * (1 - 0.52 * lm::exp(-10.8 * in.a[i])) * in.bb[i];
  return k;
}

Spectrum reflectance(const Iops& in, const Constituents& c, double depth) {
  Spectrum r{};
  for (int i = 0; i < kBands; ++i) {
    const double k = in.a[i] + in.bb[i];
    const double u = in.bb[i] / k;
    const double deep = (0.084 + 0.17 * u) * u;
    double rrs = deep;
    if (std::isfinite(depth)) {
      const double duc = 1.03 * std::sqrt(1 + 2.4 * u);
      const double dub = 1.04 * std::sqrt(1 + 5.4 * u);
      rrs = deep * (1 - lm::exp(-(kSunInWater + duc) * k * depth)) +
            bed_at(c, wavelength(i)) / kPi * lm::exp(-(kSunInWater + dub) * k * depth);
    }
    r[i] = kPi * 0.5 * rrs / (1 - 1.5 * rrs);
  }
  return r;
}

Spectrum light_at(const Iops& in, double depth) {
  const Spectrum k = kd(in);
  Spectrum e{};
  for (int i = 0; i < kBands; ++i) e[i] = lm::exp(-k[i] * depth);
  return e;
}

Spectrum under_view(const Iops& in, double depth, double distance) {
  const Spectrum e = light_at(in, depth);
  Spectrum v{};
  for (int i = 0; i < kBands; ++i) {
    const double k = in.a[i] + in.bb[i];
    const double u = in.bb[i] / k;
    const double veil = kVeil * kPi * (0.084 + 0.17 * u) * u;
    const double c = in.a[i] + in.b[i];
    const double seen = std::isfinite(distance) ? lm::exp(-c * distance) : 0;
    v[i] = e[i] * (seen + veil * (1 - seen));
  }
  return v;
}

Rgb colour_of(const Spectrum& r) {
  double x = 0, y = 0, z = 0, white = 0;
  for (int i = 0; i < kBands; ++i) {
    const double nm = wavelength(i);
    const double l = r[i] * kD65[i];
    x += l * xbar(nm);
    y += l * ybar(nm);
    z += l * zbar(nm);
    white += kD65[i] * ybar(nm);
  }
  x /= white;
  y /= white;
  z /= white;
  return {3.2406 * x - 1.5372 * y - 0.4986 * z, -0.9689 * x + 1.8758 * y + 0.0415 * z,
          0.0557 * x - 0.2040 * y + 1.0570 * z};
}

Optics optics(const Constituents& c) {
  const Iops in = iops(c);
  Optics o;
  Spectrum cs{}, bedspec{};
  for (int i = 0; i < kBands; ++i) {
    cs[i] = in.a[i] + in.b[i];
    bedspec[i] = bed_at(c, wavelength(i));
  }
  o.a = bands(in.a);
  o.b = bands(in.b);
  o.bb = bands(in.bb);
  o.c = bands(cs);
  o.kd = bands(kd(in));
  o.deep = colour_of(reflectance(in, c, INFINITY));
  o.veil = colour_of(under_view(in, 0, INFINITY));
  o.bed = colour_of(bedspec);
  o.emission = c.emission;
  double cv = 0, v = 0;
  for (int i = 0; i < kBands; ++i) {
    const double w = ybar(wavelength(i));
    cv += cs[i] * w;
    v += w;
  }
  o.visibility = 4.8 / (cv / v);
  return o;
}

namespace {

// The looks, each a row of what is in the water (docs/water.md § Colour).
const NamedLook kLooks[] = {
    // A forest lake dark with humus: amber shallows over peat, black depths.
    {"nordic-brown",
     {.cdom = 6, .chlorophyll = 4, .sediment = 1, .bed = {0.06, 0.08, 0.1}},
     {0.5, 3}},
    // A mountain lake above the tree line, over grey stones.
    {"nordic-clear",
     {.cdom = 0.25, .chlorophyll = 0.5, .sediment = 0.3, .bed = {0.18, 0.2, 0.22}},
     {5, 15}},
    // A brackish inland sea: green-brown.
    {"baltic",
     {.cdom = 0.8, .chlorophyll = 3, .sediment = 1.2, .bed = {0.25, 0.3, 0.32}},
     {1.5, 6}},
    // A clear, poor sea: deep blue over pale rock.
    {"adriatic",
     {.cdom = 0.01, .chlorophyll = 0.1, .sediment = 0.05, .bed = {0.3, 0.33, 0.33}},
     {15, 40}},
    // Next to nothing over white carbonate sand: turquoise.
    {"tropical-shallows",
     {.cdom = 0.005, .chlorophyll = 0.08, .sediment = 0.1, .bed = {0.45, 0.52, 0.5}},
     {15, 40}},
    // Rock flour: milky turquoise.
    {"glacial",
     {.cdom = 0.02,
      .chlorophyll = 0.2,
      .sediment = 3,
      .sediment_scatter = 0.5,
      .sediment_slope = 1.2,
      .bed = {0.2, 0.2, 0.2}},
     {1, 5}},
    // A pond in bloom.
    {"algal-pond",
     {.cdom = 0.4, .chlorophyll = 60, .sediment = 1.5, .bed = {0.08, 0.1, 0.1}},
     {0.2, 1}},
    // A river in spate.
    {"muddy-river",
     {.cdom = 2,
      .chlorophyll = 4,
      .sediment = 120,
      .sediment_scatter = 0.3,
      .sediment_slope = 0.3,
      .bed = {0.15, 0.13, 0.11}},
     {0.05, 0.3}},
    // Treated, filtered water over white tile.
    {"pool", {.sediment = 0.02, .bed = {0.75, 0.8, 0.8}}, {25, 80}},
    // A brush world's slime: thick, green and glowing faintly.
    {"slime",
     {.chlorophyll = 300, .sediment = 8, .bed = {0.1, 0.1, 0.1}, .emission = {0.02, 0.3, 0.03}},
     {0.02, 0.3}},
    // Lava: opaque and bright; its damage is the game's.
    {"lava",
     {.cdom = 50,
      .sediment = 4000,
      .sediment_scatter = 0.01,
      .bed = {0.05, 0.05, 0.05},
      .emission = {2.5, 0.25, 0.02}},
     {0, 0.1}},
};

}  // namespace

const Constituents* find_look(std::string_view name) {
  for (const NamedLook& l : kLooks)
    if (l.name == name) return &l.water;
  return nullptr;
}

std::span<const NamedLook> looks() { return kLooks; }

}  // namespace ge::water
