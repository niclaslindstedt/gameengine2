// The noise layers' builders: tileable value noise and fBm on a wrapped lattice, cellular
// cracks, scratches, a dot screen and streaks, each a pure function of its kind, size and seed.
//
// ge:summary Builds each tileable noise layer from a wrapped lattice, cells, lines and dots
// ge:keywords layer tileable value-noise fbm worley cellular scratches halftone brushed grain
// ge:skill ui
// ge:test tests/charset_effects_test.cpp
#include "charset/layers.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/libm/libm.hpp"
#include "core/noise.hpp"
#include "core/num.hpp"
#include "core/parallel.hpp"
#include "core/prng.hpp"

namespace ge::charset {
namespace {

constexpr std::array<LayerInfo, kLayerCount> kLayers = {{
    {Layer::grain, "grain", "film grain over a console or a title: a fill or an overlay"},
    {Layer::grunge, "grunge", "dirt and wear: erode stencils and stamps, dirty a fill"},
    {Layer::scratches, "scratches", "a scratched plate or a scuffed sign: a fill"},
    {Layer::scanlines, "scanlines", "a CRT's lines over a terminal: an overlay"},
    {Layer::rust, "rust", "corroded metal plates for titles: a fill"},
    {Layer::cracks, "cracks", "cracked stone, paint or ice: a fill or erosion"},
    {Layer::halftone, "halftone", "a printed dot screen: comics, posters, a board's lamps"},
    {Layer::brushed, "brushed", "brushed steel and chrome: a fill"},
}};

constexpr double kTau = 6.283185307179586;

// Value noise on a lattice of `px` x `py` cells that wraps at the texture's edge: (u, v) are
// texel coordinates over a `size` texture.
double wrapped_noise(double u, double v, int size, int px, int py, double seed) {
  const double gx = u * px / size, gy = v * py / size;
  const double ix = std::floor(gx), iy = std::floor(gy);
  const double fx = core::smooth(gx - ix), fy = core::smooth(gy - iy);
  auto h = [&](double x, double y) {
    const double wx = x - std::floor(x / px) * px, wy = y - std::floor(y / py) * py;
    return core::hash2(wx, wy, seed);
  };
  const double a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
  return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

// Octaves of wrapped noise from `period` cells across, each twice as fine and half as loud.
double fbm(double u, double v, int size, int period, int octaves, double seed) {
  double sum = 0, amp = 1, norm = 0;
  for (int o = 0; o < octaves; ++o) {
    const int p = std::min(period << o, size);
    sum += amp * wrapped_noise(u, v, size, p, p, seed + o * 17.0);
    norm += amp;
    amp *= 0.5;
  }
  return sum / norm;
}

double smoothstep(double a, double b, double x) {
  const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

uint8_t byte(double v) {
  return static_cast<uint8_t>(core::num::round(std::clamp(v, 0.0, 1.0) * 255));
}

// The two nearest feature points' distances (F1, F2) of a wrapped cellular field with one point
// a cell, `cells` cells across, in cell units.
void worley(double u, double v, int size, int cells, double seed, double& f1, double& f2) {
  const double gx = u * cells / size, gy = v * cells / size;
  const double ix = std::floor(gx), iy = std::floor(gy);
  f1 = f2 = 1e9;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const double cx = ix + dx, cy = iy + dy;
      const double wx = cx - std::floor(cx / cells) * cells,
                   wy = cy - std::floor(cy / cells) * cells;
      const double fx = cx + core::hash2(wx, wy, seed), fy = cy + core::hash2(wx, wy, seed + 1);
      const double d = std::sqrt((fx - gx) * (fx - gx) + (fy - gy) * (fy - gy));
      if (d < f1) {
        f2 = f1;
        f1 = d;
      } else if (d < f2) {
        f2 = d;
      }
    }
  }
}

void scratches(Texture& t, uint32_t seed) {
  const int s = t.size;
  std::vector<double> lit(static_cast<size_t>(s) * s, 0);
  core::Rng rng = core::make_rng(seed * 2654435761u + 7);
  const int count = s / 3;
  for (int k = 0; k < count; ++k) {
    const double x0 = core::range(rng, 0, s), y0 = core::range(rng, 0, s);
    // Most scratches run one way, as a plate is scuffed along; a few cross them.
    const double base = core::chance(rng, 0.8) ? 0.15 : 0.6;
    const double a = (base + core::range(rng, -0.12, 0.12)) * kTau;
    const double l = core::range(rng, 0.05, 0.3) * s;
    const double b = core::range(rng, 0.35, 1.0);
    const double dx = core::libm::cos(a), dy = core::libm::sin(a);
    for (int step = 0; step * 0.5 < l; ++step) {
      const double d = step * 0.5;
      // A scratch fades in and out along its length.
      const double fade = std::min(1.0, std::min(d, l - d) / (0.15 * l));
      const int x = static_cast<int>(std::floor(x0 + dx * d)),
                y = static_cast<int>(std::floor(y0 + dy * d));
      const size_t at = static_cast<size_t>(((y % s) + s) % s) * s + ((x % s) + s) % s;
      lit[at] = std::max(lit[at], b * fade);
    }
  }
  for (size_t i = 0; i < lit.size(); ++i) t.value[i] = byte(lit[i]);
}

}  // namespace

std::span<const LayerInfo> layers() { return kLayers; }
const LayerInfo& layer_info(Layer id) { return kLayers[static_cast<size_t>(id)]; }
const LayerInfo* find_layer(std::string_view name) {
  for (const LayerInfo& l : kLayers) {
    if (name == l.name) return &l;
  }
  return nullptr;
}

Texture make_layer(Layer layer, int size, uint32_t seed) {
  Texture t;
  t.size = size;
  t.value.assign(static_cast<size_t>(size) * size, 0);
  const double sd = seed * 101.0 + static_cast<double>(layer) * 1000.0;
  if (layer == Layer::scratches) {
    scratches(t, seed + static_cast<uint32_t>(layer));
    return t;
  }
  // Every texel is a pure function of its place, so the rows bake on any number of workers.
  core::parallel_bands(static_cast<size_t>(size), [&](size_t begin, size_t end) {
    for (int y = static_cast<int>(begin); y < static_cast<int>(end); ++y) {
      for (int x = 0; x < size; ++x) {
        const double u = x + 0.5, v = y + 0.5;
        double out = 0;
        switch (layer) {
          case Layer::grain: {
            const double g =
                (core::hash2(x, y, sd) + core::hash2(x, y, sd + 1) + core::hash2(x, y, sd + 2)) / 3;
            out = 0.5 + (g - 0.5) * 1.6;
            break;
          }
          case Layer::grunge: {
            const double n = fbm(u, v, size, 4, 6, sd);
            const double speck = core::hash2(x, y, sd + 9) > 0.992 ? 0.35 : 0;
            out = smoothstep(0.32, 0.68, n) - speck;
            break;
          }
          case Layer::scanlines: {
            const int p = std::max(2, size / 64);
            const double t01 = ((y % p) + 0.5) / p;
            out = 0.42 + 0.58 * (1 - std::fabs(2 * t01 - 1)) + (core::hash2(x, y, sd) - 0.5) * 0.06;
            break;
          }
          case Layer::rust: {
            const double body = fbm(u, v, size, 6, 6, sd);
            const double turb = std::fabs(fbm(u, v, size, 3, 5, sd + 50) - 0.5) * 2;
            double f1, f2;
            worley(u, v, size, 24, sd + 70, f1, f2);
            const double pit = f1 < 0.12 ? (0.12 - f1) / 0.12 : 0;
            out = 0.1 + 0.95 * body - 0.45 * turb - 0.25 * pit * core::hash2(f2 * 64, 0, sd);
            break;
          }
          case Layer::cracks: {
            double f1, f2;
            worley(u, v, size, 7, sd, f1, f2);
            double g1, g2;
            worley(u, v, size, 19, sd + 30, g1, g2);
            const double big = smoothstep(0.0, 0.07, f2 - f1);
            const double fine = 0.4 + 0.6 * smoothstep(0.0, 0.06, g2 - g1);
            out = (0.55 + 0.45 * fbm(u, v, size, 8, 3, sd + 5)) * big * (0.7 + 0.3 * fine);
            break;
          }
          case Layer::halftone: {
            // Dots on a staggered grid, each sized by a slow noise; a big dot spills into its
            // neighbours' cells, so every texel takes the most of the nine dots round it.
            const int cells = 32;
            const double c = static_cast<double>(size) / cells;
            const int row = static_cast<int>(v / c);
            for (int dr = -1; dr <= 1; ++dr) {
              const int r = row + dr;
              const double ox = (((r % 2) + 2) % 2) * c / 2;
              const double cy = (r + 0.5) * c;
              const double base = std::floor((u - ox) / c) * c + ox + c / 2;
              for (int dc = -1; dc <= 1; ++dc) {
                const double cx = base + dc * c;
                const double level = fbm(cx, cy, size, 2, 3, sd);
                const double rad = c * 0.62 * std::sqrt(std::clamp(level * 1.3 - 0.15, 0.0, 1.0));
                const double d = std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy));
                out = std::max(out, std::clamp(rad - d + 0.5, 0.0, 1.0));
              }
            }
            break;
          }
          case Layer::brushed: {
            double sum = 0, norm = 0, amp = 1;
            for (int o = 0; o < 4; ++o) {
              sum += amp * wrapped_noise(u, v, size, std::min(size, 3 << o),
                                         std::max(1, size / (4 >> std::min(o, 2))), sd + o);
              norm += amp;
              amp *= 0.6;
            }
            out = 0.35 + 0.65 * sum / norm + (core::hash2(x, y, sd + 7) - 0.5) * 0.08;
            break;
          }
          case Layer::scratches:
          case Layer::count:
            break;
        }
        t.value[static_cast<size_t>(y) * size + x] = byte(out);
      }
    }
  });
  return t;
}

std::string layer_path(Layer layer, int size) {
  return std::string("engine/charset/layers/") + layer_info(layer).name + "_" +
         std::to_string(size) + ".png";
}

}  // namespace ge::charset
