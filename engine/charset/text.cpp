// The run's layout and the atlas sampler (nearest for a whole-number pixel font, box filtered
// otherwise).
//
// ge:summary Text layout on the cell grid and the atlas's nearest and box-filtered samplers
// ge:keywords text layout sample box-filter nearest bilinear draw_text text_size
// ge:skill ui
// ge:test tests/charset_effects_test.cpp
#include "charset/text.hpp"

#include <algorithm>
#include <cmath>

namespace ge::charset {
namespace {

// The atlas's coverage (0..1) over the source box [x0, x1) x [y0, y1) of one cell, clipped to
// it: the area-weighted mean of the texels under it.
double box_sample(const Atlas& a, const CellRect& c, double x0, double y0, double x1, double y1) {
  x0 = std::max(x0, 0.0);
  y0 = std::max(y0, 0.0);
  x1 = std::min(x1, static_cast<double>(c.w));
  y1 = std::min(y1, static_cast<double>(c.h));
  if (x1 <= x0 || y1 <= y0) return 0;
  double sum = 0;
  for (int ty = static_cast<int>(std::floor(y0)); ty < y1; ++ty) {
    const double hy = std::min(y1, ty + 1.0) - std::max(y0, static_cast<double>(ty));
    for (int tx = static_cast<int>(std::floor(x0)); tx < x1; ++tx) {
      const double hx = std::min(x1, tx + 1.0) - std::max(x0, static_cast<double>(tx));
      sum += hx * hy * a.at(c.x + tx, c.y + ty);
    }
  }
  return sum / ((x1 - x0) * (y1 - y0)) / 255.0;
}

double bilinear(const Atlas& a, const CellRect& c, double x, double y) {
  x -= 0.5;
  y -= 0.5;
  const double fx = std::floor(x), fy = std::floor(y);
  const double tx = x - fx, ty = y - fy;
  auto texel = [&](int ix, int iy) {
    if (ix < 0 || iy < 0 || ix >= c.w || iy >= c.h) return 0.0;
    return a.at(c.x + ix, c.y + iy) / 255.0;
  };
  const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
  const double top = texel(ix, iy) + (texel(ix + 1, iy) - texel(ix, iy)) * tx;
  const double bot = texel(ix, iy + 1) + (texel(ix + 1, iy + 1) - texel(ix, iy + 1)) * tx;
  return top + (bot - top) * ty;
}

}  // namespace

void text_size(std::string_view text, const TextPlace& place, double& w, double& h) {
  const double adv = place.cell_h / 2 + place.tracking;
  int cols = 0, widest = 0, lines = 1;
  for (char ch : text) {
    if (ch == '\n') {
      ++lines;
      cols = 0;
      continue;
    }
    widest = std::max(widest, ++cols);
  }
  w = widest * adv - (widest > 0 ? place.tracking : 0);
  h = lines * place.cell_h + (lines - 1) * place.leading;
}

void draw_text(Canvas& canvas, const Atlas& atlas, std::string_view text, const TextPlace& place) {
  const FontInfo& info = font_info(atlas.font);
  const double cell_w = place.cell_h / 2;
  const double adv = cell_w + place.tracking;
  const double scale = atlas.cell_h() / place.cell_h;  // atlas texels a canvas pixel
  // A pixel font at a whole multiple of its grid keeps its pixels hard.
  const double grid_px = info.pixel ? place.cell_h / info.grid_h : 0;
  const bool nearest = info.pixel && grid_px >= 1 && grid_px == std::floor(grid_px);
  double pen_x = place.x, pen_y = place.y;
  for (char ch : text) {
    if (ch == '\n') {
      pen_x = place.x;
      pen_y += place.cell_h + place.leading;
      continue;
    }
    const uint8_t code = code_of(static_cast<unsigned char>(ch));
    const CellRect c = cell_rect(atlas.size, code);
    const int x0 = static_cast<int>(std::floor(pen_x)), y0 = static_cast<int>(std::floor(pen_y));
    const int x1 = static_cast<int>(std::ceil(pen_x + cell_w));
    const int y1 = static_cast<int>(std::ceil(pen_y + place.cell_h));
    for (int y = std::max(y0, 0); y < std::min(y1, canvas.height); ++y) {
      for (int x = std::max(x0, 0); x < std::min(x1, canvas.width); ++x) {
        // The canvas pixel's box in the cell's texels.
        const double sx0 = (x - pen_x) * scale, sy0 = (y - pen_y) * scale;
        double v;
        if (nearest) {
          const int gx = static_cast<int>(std::floor((x + 0.5 - pen_x) / grid_px));
          const int gy = static_cast<int>(std::floor((y + 0.5 - pen_y) / grid_px));
          v = gx < 0 || gy < 0 || gx >= info.grid_w || gy >= info.grid_h
                  ? 0
                  : atlas.at(c.x + gx * c.w / info.grid_w, c.y + gy * c.h / info.grid_h) / 255.0;
        } else if (scale > 1) {
          v = box_sample(atlas, c, sx0, sy0, sx0 + scale, sy0 + scale);
        } else {
          v = bilinear(atlas, c, sx0 + scale / 2, sy0 + scale / 2);
        }
        double& d = canvas.cov[static_cast<size_t>(y) * canvas.width + x];
        d = std::min(1.0, d + v);
      }
    }
    pen_x += adv;
  }
}

}  // namespace ge::charset
