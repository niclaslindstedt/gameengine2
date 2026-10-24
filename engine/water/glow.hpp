// THE GLOW OF A HOT SURFACE (docs/liquids.md § Lava's glow): the light a surface at a temperature
// sends out as a black body, as the eye sees it. Lava, a crust's cracks, molten metal: their
// colour and brightness are not picked but follow from the temperature alone, so a crust cooling
// from 1150 to 500 degrees C goes from orange-yellow through cherry red to black on its own.
//
// Planck's law gives the spectral radiance, L(lambda) = 2 h c^2 / lambda^5 / (exp(h c /
// (lambda k T)) - 1); weighted by the CIE 1931 observer (its multi-lobe fit) every 5 nm from 380
// to 780 nm and by 683 lm/W it is a luminance (cd/m^2) and a colour, made linear sRGB (the
// colours past red clipped into the gamut, their luminance kept). `emissivity` scales it. A body
// glows visibly in the dark from about 525 degrees C (the eye's dull red) and outshines a sunlit
// white face only past about 1700.
//
// `glow_table` lays the colour over a span of temperatures for the renderer, which reads a
// pixel's temperature into it (render/liquid.hpp).
//
// ge:summary A hot surface's glow: Planck, the CIE observer, luminance and colour, the table
// ge:keywords glow blackbody black-body planck incandescence temperature kelvin luminance
// ge:keywords cie colour color lava ember draper glow_of glow_table
// ge:skill water render
// ge:test tests/water_glow_test.cpp
#pragma once

#include <vector>

#include "water/optics.hpp"

namespace ge::water {

// A surface's glow: its luminance and its colour (linear sRGB, luminance 1).
struct Glow {
  double luminance = 0;  // cd/m^2
  Rgb colour;
};

// The glow of a black body at `kelvin`, times `emissivity`.
Glow glow_of(double kelvin, double emissivity = 1);

// `count` entries from `lo` to `hi` kelvin, each the glow's linear sRGB radiance in cd/m^2 (the
// colour times the luminance).
std::vector<Rgb> glow_table(double lo, double hi, int count, double emissivity = 1);

}  // namespace ge::water
