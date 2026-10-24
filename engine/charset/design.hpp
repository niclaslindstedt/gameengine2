// A GLYPH'S DESIGN: the skeleton every drawn font (charset.hpp) strokes with its own pen. Each
// code's design is a short source string in the design space (a cell 100 x 200 units, y up
// from the baseline: capitals 0..100, the x-height 74, ascenders 108, descenders -32), parsed
// here into paths, dots, filled polygons and filled ellipses. The pen (pen.hpp) decides how a
// path looks; the design only says where it runs, so ten looks share one set of skeletons and a
// fix to a letter's shape lands in every font at once.
//
// THE SOURCE LANGUAGE, numbers separated by spaces:
//   M x y              start a path at (x, y)
//   L x y              a line on from the current point
//   A cx cy rx ry a0 a1  an elliptical arc round (cx, cy) from a0 to a1 degrees (0 = +x, 90 = up,
//                      counter-clockwise positive; a1 < a0 runs clockwise). It joins the current
//                      path when it starts where that path is, else starts its own
//   O cx cy rx ry      a closed ellipse, a path of its own
//   Z                  close the current path back to its start
//   D x y              a dot the pen makes (the i's, the full stop)
//   F n x1 y1 ... xn yn  a filled convex polygon (arrows, the cursor block)
//   E cx cy rx ry      a filled ellipse (the bullet)
//
// ge:summary A glyph's skeleton: paths, arcs, dots and fills parsed from its source string
// ge:keywords glyph design skeleton path arc ellipse stroke dot fill parse source outline
// ge:keywords design_of variant zero slashed-zero single-storey
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ge::charset {

struct Pt {
  double x, y;
};

// One piece of a path: a line from the previous point to `to`, or an arc.
struct Piece {
  bool arc = false;
  Pt to{};                                // a line's end
  double cx = 0, cy = 0, rx = 0, ry = 0;  // an arc's ellipse
  double a0 = 0, a1 = 0;                  // its sweep, degrees
};

struct Path {
  Pt start{};
  std::vector<Piece> pieces;
  bool closed = false;
  bool ellipse = false;  // an `O`: one arc all the way round
};

struct Design {
  std::vector<Path> paths;
  std::vector<Pt> dots;
  std::vector<std::vector<Pt>> fills;        // convex, in order round
  std::vector<std::array<double, 4>> discs;  // cx cy rx ry
  int dither = 0;    // the shade symbols: 1 a 1-in-4 dither, 2 a checkerboard, of the whole cell
  bool bad = false;  // the source had an operator the language lacks (the parse stopped there)
};

// The alternates a font picks between.
enum class Zero : uint8_t { plain, slashed, dotted };
struct Variant {
  Zero zero = Zero::dotted;
  bool single_storey_a = false;  // 'a' as a bowl and a stem
};

// The skeleton source of `code` (empty for a code with no ink), and its parse.
std::string_view design_source(uint8_t code, const Variant& variant);
Design parse_design(std::string_view source);
Design design_of(uint8_t code, const Variant& variant);

// Where a path is at its end, with every piece applied (an arc's end is on its ellipse).
Pt path_end(const Path& path);

}  // namespace ge::charset
