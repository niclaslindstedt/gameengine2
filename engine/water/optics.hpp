// THE COLOUR OF WATER IS WHAT IS IN IT (docs/water.md § Colour). A look is stated as its
// constituents and every colour is derived from the physics of light in water, once, so a lake's
// colour from the shore and its fog under water cannot drift apart.
//
// SPECTRALLY, every 10 nm from 400 to 700 nm, the inherent optical properties are summed from:
// - PURE WATER: absorption from the measured spectrum (about 0.006 /m in the blue, 0.06 /m in
//   the green, 0.3 to 0.6 /m in the red), scattering 0.00288 (lambda / 500)^-4.32 /m, half of
//   it backwards;
// - DISSOLVED ORGANIC MATTER (the brown): a_g(440) exp(-S (lambda - 440)), S about 0.015 /nm;
// - PHYTOPLANKTON (the green): absorption 0.0654 Chl^0.728 at 440 nm in a shape with its blue
//   and red peaks, scattering 0.416 Chl^0.766 (550 / lambda), 1 % of it backwards;
// - MINERAL SEDIMENT (the milk): per g/m^3, absorption 0.041 exp(-0.0123 (lambda - 443)) and
//   scattering `sediment_scatter` (550 / lambda)^`sediment_slope`, 1.83 % of it backwards;
// - BUBBLES: a grey scattering, 5 % of it backwards.
// The diffuse attenuation is Kd = 1.15 a + 4.18 (1 - 0.52 exp(-10.8 a)) bb (the sun 30 degrees
// up). The water-leaving reflectance over a bed at a depth is the shallow-water model:
//   rrs = rrs_deep (1 - exp(-(1.07 + DuC) k H)) + (bed / pi) exp(-(1.07 + DuB) k H),
//   rrs_deep = (0.084 + 0.17 u) u,  u = bb / (a + bb),  k = a + bb,
//   DuC = 1.03 sqrt(1 + 2.4 u),  DuB = 1.04 sqrt(1 + 5.4 u),
// taken through the surface as Rrs = 0.5 rrs / (1 - 1.5 rrs). Under water, the daylight at a
// depth is the surface's times exp(-Kd z); the veil a view looks through is the water's own
// upwelling colour twice over (2 rrs_deep of that light), and a target fades into it as
// exp(-c d), c = a + b. A black target is lost at 4.8 / c (c weighted by the eye's photopic
// response). Spectra become colours through the CIE 1931 observer (its multi-lobe fit) under
// daylight (D65), in linear sRGB, white = 1.
//
// THE RENDERER reads `Optics`: each coefficient averaged over three bands (red 580-700 nm,
// green 490-580, blue 400-490, weighted by daylight), the deep colour, the veil, the bed and the
// visibility. A look is a row of constituents (`find_look`); a game adds its own by building a
// `Constituents` and handing it to a body. Lava is not water: an emissive look on the same body.
//
// ge:summary The colour of water from what is in it: spectral IOPs, reflectance, the veil, looks
// ge:keywords water optics colour color absorption scattering backscatter iop cdom chlorophyll
// ge:keywords sediment bubbles bed shallow reflectance rrs kd attenuation visibility veil fog
// ge:keywords look nordic-brown baltic adriatic tropical glacial pool slime lava cie srgb d65
// ge:skill water
// ge:test tests/water_optics_test.cpp
#pragma once

#include <array>
#include <span>
#include <string_view>

namespace ge::water {

inline constexpr int kBands = 31;  // 400, 410 ... 700 nm
inline constexpr double kFirstNm = 400;
inline constexpr double kStepNm = 10;
using Spectrum = std::array<double, kBands>;

// Linear sRGB, white = 1.
struct Rgb {
  double r = 0, g = 0, b = 0;
};

// What is in the water.
struct Constituents {
  double cdom = 0;                // dissolved organic matter: its absorption at 440 nm, /m
  double cdom_slope = 0.015;      // /nm
  double chlorophyll = 0;         // mg/m^3
  double sediment = 0;            // mineral particles, g/m^3 (mg/L)
  double sediment_scatter = 0.5;  // m^2/g at 550 nm
  double sediment_slope = 0.5;    // the scattering's fall with wavelength (fine flour: 1 and up)
  double bubbles = 0;             // their scattering, /m
  std::array<double, 3> bed{0.2, 0.2, 0.2};  // the bed's albedo at 450, 550 and 650 nm
  Rgb emission{};                            // the water's own light (lava), linear sRGB over white
};

// The inherent optical properties, spectrally.
struct Iops {
  Spectrum a{}, b{}, bb{};
};

// What the renderer reads, per band (red, green, blue).
struct Optics {
  Rgb a, b, bb, c, kd;  // /m
  Rgb deep;             // the water-leaving colour of deep water (pi Rrs), white = 1
  Rgb veil;             // the fog a view under the surface looks through, at the surface
  Rgb bed;              // the bed's albedo
  Rgb emission;
  double g = 0.9;         // the particles' Henyey-Greenstein phase asymmetry
  double visibility = 0;  // m, a black target lost (4.8 / c photopic)
};

double wavelength(int band);
// The parts of the absorption: pure water's, and each constituent's.
double water_absorption(double nm);
Iops iops(const Constituents& c);
// The diffuse attenuation of daylight, /m.
Spectrum kd(const Iops& iops);
// The water-leaving reflectance (pi Rrs, above the surface) over the bed at `depth` m (an
// infinite depth: deep water).
Spectrum reflectance(const Iops& iops, const Constituents& c, double depth);
// The daylight at `depth` m under the surface, relative to the surface's.
Spectrum light_at(const Iops& iops, double depth);
// A white target `distance` m away level with a view `depth` m down, as seen (white at the
// surface = 1); an infinite distance is the veil.
Spectrum under_view(const Iops& iops, double depth, double distance);
// A spectrum (a reflectance under daylight) as a colour.
Rgb colour_of(const Spectrum& reflectance);
Optics optics(const Constituents& c);

// The named looks. nullptr for a name not among them.
const Constituents* find_look(std::string_view name);
struct NamedLook {
  std::string_view name;
  Constituents water;
  double seen[2];  // the band of visibility such water has in the field, m
};
std::span<const NamedLook> looks();

}  // namespace ge::water
