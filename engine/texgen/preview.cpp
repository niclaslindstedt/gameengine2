// The CPU light over baked maps: the shading model, and nearest sampling into a tiled picture.
//
// ge:summary The CPU shading of baked maps and the tiled map pictures
// ge:keywords preview shade blinn-phong highlight tiles lit_preview map_picture
// ge:skill textures labs
// ge:test tests/texgen_bake_test.cpp
#include "texgen/preview.hpp"

#include <algorithm>
#include <cmath>

#include "core/libm/libm.hpp"
#include "core/num.hpp"

namespace ge::texgen {
namespace {

uint8_t byte(double v) {
  return static_cast<uint8_t>(core::num::round(std::clamp(v, 0.0, 1.0) * 255));
}

// The source texel under picture pixel (x, y) when `tiles` repeats fill `px`.
size_t source(const Maps& m, int x, int y, int px, int tiles) {
  const int sx = static_cast<int>(static_cast<int64_t>(x) * tiles * m.size / px) % m.size;
  const int sy = static_cast<int>(static_cast<int64_t>(y) * tiles * m.size / px) % m.size;
  return (static_cast<size_t>(sy) * m.size + sx) * 4;
}

}  // namespace

lab::Picture lit_preview(const Maps& m, int px, int tiles) {
  lab::Picture pic = lab::create_picture(px, px, {0, 0, 0});
  // The light, in the tile's frame (+x right, +y up, +z out of the surface).
  const double lx = -0.55, ly = 0.55, lz = 0.63;
  const double ll = std::sqrt(lx * lx + ly * ly + lz * lz);
  const double hx = lx / ll, hy = ly / ll, hz = lz / ll + 1;
  const double hl = std::sqrt(hx * hx + hy * hy + hz * hz);
  for (int y = 0; y < px; ++y) {
    for (int x = 0; x < px; ++x) {
      const size_t s = source(m, x, y, px, tiles);
      const auto at = [&](MapKind k, int c) { return m.rgba[static_cast<int>(k)][s + c] / 255.0; };
      const double nx = at(MapKind::normal, 0) * 2 - 1, ny = at(MapKind::normal, 1) * 2 - 1,
                   nz = at(MapKind::normal, 2) * 2 - 1;
      const double ao = at(MapKind::orm, 0), rough = at(MapKind::orm, 1),
                   metal = at(MapKind::orm, 2);
      const double ndl = std::max(0.0, (nx * lx + ny * ly + nz * lz) / ll);
      const double ndh = std::max(0.0, (nx * hx + ny * hy + nz * hz) / hl);
      const double smooth = 1 - rough;
      const double shine = 4 + 240 * smooth * smooth * smooth;
      const double spec = smooth * smooth * core::libm::pow(ndh, shine) * ndl * 1.6;
      const size_t o = (static_cast<size_t>(y) * px + x) * 4;
      for (int c = 0; c < 3; ++c) {
        const double albedo = at(MapKind::albedo, c);
        const double diffuse = albedo * (1 - metal) * (0.28 * ao + 0.95 * ndl);
        const double tint = 0.04 + (albedo - 0.04) * metal;
        // A metal shows its surroundings: a grey sky's worth of reflection, tinted by its colour.
        const double sheen = metal * albedo * (0.45 * ao + 0.5 * ndl);
        pic.rgba[o + c] = byte(diffuse + sheen + tint * spec + at(MapKind::glow, c));
      }
    }
  }
  return pic;
}

lab::Picture map_picture(const Maps& m, MapKind kind, int px, int tiles) {
  lab::Picture pic = lab::create_picture(px, px, {0, 0, 0});
  const auto& src = m.rgba[static_cast<int>(kind)];
  for (int y = 0; y < px; ++y) {
    for (int x = 0; x < px; ++x) {
      const size_t s = source(m, x, y, px, tiles), o = (static_cast<size_t>(y) * px + x) * 4;
      for (int c = 0; c < 4; ++c) pic.rgba[o + c] = src[s + c];
    }
  }
  return pic;
}

}  // namespace ge::texgen
