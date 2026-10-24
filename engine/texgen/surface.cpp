// The surface's start and the height readings: the wrapped box blur (running sums, any radius
// at the same cost), the relief off it, and the up-facing share off the height's slope.
//
// ge:summary The surface's start, the wrapped blur, relief and up-facing readings of the height
// ge:keywords surface blur box-blur relief cavity up-facing make_surface
// ge:skill textures
// ge:test tests/texgen_bake_test.cpp
#include "texgen/surface.hpp"

#include <algorithm>

namespace ge::texgen {
namespace {

// One wrapped box pass along x (`along_x`) or y, radius r, `in` to `out`. Each line is copied
// into a buffer padded by r at both ends, so the running sum reads without wrapping.
void box_pass(const std::vector<float>& in, std::vector<float>& out, int size, int r,
              bool along_x) {
  const double norm = 1.0 / (2 * r + 1);
  core::parallel_bands(static_cast<size_t>(size), [&](size_t begin, size_t end) {
    std::vector<double> line(static_cast<size_t>(size + 2 * r + 1));
    for (int l = static_cast<int>(begin); l < static_cast<int>(end); ++l) {
      const size_t base = along_x ? static_cast<size_t>(l) * size : static_cast<size_t>(l);
      const size_t stride = along_x ? 1 : static_cast<size_t>(size);
      for (int k = -r; k <= size + r; ++k) {
        const int w = k < 0 ? k + size : (k >= size ? k - size : k);
        const int j = k + r;
        line[static_cast<size_t>(j)] = in[base + static_cast<size_t>(w) * stride];
      }
      double sum = 0;
      for (int k = 0; k <= 2 * r; ++k) sum += line[static_cast<size_t>(k)];
      for (int k = 0; k < size; ++k) {
        out[base + static_cast<size_t>(k) * stride] = static_cast<float>(sum * norm);
        const int j = k + 2 * r + 1;
        sum += line[static_cast<size_t>(j)] - line[static_cast<size_t>(k)];
      }
    }
  });
}

}  // namespace

Surface make_surface(int size) {
  Surface s;
  s.size = size;
  const size_t n = static_cast<size_t>(size) * size;
  for (auto& p : s.plane) p.assign(n, 0.0f);
  for (int c : {kR, kG, kB, kHeight}) std::fill(s.plane[c].begin(), s.plane[c].end(), 0.5f);
  std::fill(s.plane[kRough].begin(), s.plane[kRough].end(), 0.8f);
  return s;
}

std::vector<float> blur(const std::vector<float>& in, int size, int radius) {
  radius = std::clamp(radius, 1, size / 2 - 1);
  std::vector<float> a(in.size()), b(in.size());
  box_pass(in, a, size, radius, true);
  box_pass(a, b, size, radius, false);
  box_pass(b, a, size, radius, true);
  box_pass(a, b, size, radius, false);
  return b;
}

std::vector<float> relief(const Surface& s, int radius) {
  std::vector<float> out = blur(s.plane[kHeight], s.size, radius);
  const auto& h = s.plane[kHeight];
  for (size_t i = 0; i < out.size(); ++i) out[i] -= h[i];
  return out;
}

std::vector<float> up_facing(const Surface& s) {
  std::vector<float> out(s.plane[kHeight].size());
  const auto& h = s.plane[kHeight];
  // A height step of 1 over the tile's width is a slope of `size` texels; a texture's relief is
  // about a fiftieth of its tile, so that is the scale a 45 degree bevel reads 1 at.
  const double k = s.size / 64.0;
  for_texels(s.size, [&](int x, int y, double, double) {
    const double dhdy = (h[s.wrapped(x, y + 1)] - h[s.wrapped(x, y - 1)]) * 0.5;
    out[s.at(x, y)] = static_cast<float>(clamp01(dhdy * k));
  });
  return out;
}

}  // namespace ge::texgen
