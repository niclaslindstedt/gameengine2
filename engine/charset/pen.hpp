// THE PEN a drawn font strokes its designs (design.hpp) with, and the rasterizer that turns
// the strokes into one cell's coverage. A `Style` is a font's whole look: the pen's shape and
// weight, the slant, how square its curves are and how finely they are drawn, serifs, stencil
// breaks, ink roughness, a hand's wobble, the proportions. Ten fonts are ten styles (and three
// pixel grids, pixel.hpp) over one set of skeletons.
//
// THE RASTER is analytic: every pixel takes the signed distance to the nearest stroke (a
// capsule for a round pen, an oriented box for a square one, the swept hull of a broad nib, a
// convex polygon for a fill) and its coverage is that distance over one pixel, clamped. No
// supersampling, the same bytes on every compiler (sqrt and core::libm only), and a cell's cost
// is the pixels under its strokes' boxes.
//
// ge:summary The pen: a font's style, its strokes flattened and rasterized to cell coverage
// ge:keywords pen stroke style weight slant serif stencil nib calligraphy superellipse squareness
// ge:keywords rough wobble rasterize coverage signed-distance anti-aliasing capsule
// ge:keywords render_glyph flatten
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "charset/design.hpp"

namespace ge::charset {

enum class PenShape : uint8_t {
  round,   // a disc: round ends and joins
  square,  // a square swept along each piece: square ends, constant width
  nib,     // a broad nib held at an angle: thick and thin by direction
};

struct Style {
  PenShape pen = PenShape::round;
  double weight = 8;       // the pen's half-width, units (a 100-unit cell)
  double nib_angle = 40;   // the nib's angle, degrees from +x
  double nib_thin = 0.25;  // the nib's thin side as a share of its broad side
  double slant = 0;        // x shifted by slant * y: 0.2 leans about 11 degrees
  double squareness = 2;   // arcs as superellipses of this exponent: 2 round, 4+ squared
  double arc_step = 6;     // degrees an arc is flattened by: 45 draws it in facets
  double cap = 100;        // the capitals' height, units
  double x_height = 74;    // the lower case's
  double width = 1;        // the design's width scaled about the cell's centre
  bool serifs = false;     // slabs on free stem ends
  double serif = 12;       // a slab's half-length
  bool lozenges = false;   // a nib's lozenge on free stem ends instead (blackletter feet)
  bool stencil = false;    // break every stroke where it meets another
  double stencil_gap = 7;  // the break's half-width
  double rough = 0;        // the edge's noise, units (ink on paper, a worn stamp)
  double rough_scale = 6;  // its grain, units
  double wobble = 0;       // a hand's drift of the skeleton, units
  double jitter = 0;       // each glyph's own lift, units, and tilt, a tenth of it in degrees
  double dot = 1.25;       // a dot's radius over the pen's weight
  uint32_t seed = 1;       // the noise's seed (a font's own)
  Variant variant{};
};

// The strokes of a design under a style, in design units with every transform applied: the
// paths flattened to polylines, the dots, the fills (ellipses flattened to polygons).
struct Strokes {
  std::vector<std::vector<Pt>> lines;
  std::vector<Pt> dots;
  std::vector<std::vector<Pt>> fills;
  int dither = 0;
};
Strokes flatten(const Design& design, const Style& style, uint8_t code);

// `design` (the skeleton of `code`, which picks the lower-case proportions and whether it
// tiles) drawn under `style` into one cell `w` x `h` px, written to `out` (row stride `stride`
// bytes), 0..255 coverage.
void render_design(const Design& design, uint8_t code, const Style& style, int w, int h,
                   uint8_t* out, int stride);
// The same for `code`'s own skeleton (design_of).
void render_glyph(uint8_t code, const Style& style, int w, int h, uint8_t* out, int stride);

}  // namespace ge::charset
