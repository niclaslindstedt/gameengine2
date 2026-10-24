// TEXT ON THE CPU from a charset atlas (charset.hpp): a run laid out on the monospace grid
// (each code one cell's advance, a newline down one cell) into a coverage canvas, which an
// effect stack (effects.hpp) then lights. It is how the labs draw every font and look, and how
// a CPU picture (a console snapshot, a captioned shot) writes text before a GPU does. A GPU
// draws the same layout: one quad a code, its cell's rect in the atlas, the same advance.
//
// SAMPLING: a pixel font scaled by a whole number is sampled nearest, so its pixels stay hard;
// every other draw is box-filtered from the atlas (each canvas pixel the mean of the atlas area
// under it, bilinear when it grows), so a 1024 atlas drawn 20 px tall does not shimmer.
//
// ge:summary Lays text out on the monospace grid and draws it from an atlas into a canvas
// ge:keywords text draw layout monospace advance canvas coverage sample box-filter nearest
// ge:keywords draw_text text_size canvas
// ge:skill ui
// ge:test tests/charset_effects_test.cpp
#pragma once

#include <string_view>
#include <vector>

#include "charset/charset.hpp"

namespace ge::charset {

// A coverage canvas, 0..1 a pixel, row-major from the top.
struct Canvas {
  int width = 0, height = 0;
  std::vector<double> cov;
  Canvas() = default;
  Canvas(int w, int h) : width(w), height(h), cov(static_cast<size_t>(w) * h, 0.0) {}
  double at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return cov[static_cast<size_t>(y) * width + x];
  }
};

// Where a run of text goes: its top-left, how tall a cell is drawn (px; the advance is half of
// it plus `tracking`), and the gap between lines (px, added to the cell's height).
struct TextPlace {
  double x = 0, y = 0;
  double cell_h = 32;
  double tracking = 0;
  double leading = 0;
};

// The canvas size a run of `text` takes at `place` (its widest line, its lines), px.
void text_size(std::string_view text, const TextPlace& place, double& w, double& h);

// `text` drawn from `atlas` into `canvas` (coverage added and clamped to 1).
void draw_text(Canvas& canvas, const Atlas& atlas, std::string_view text, const TextPlace& place);

}  // namespace ge::charset
