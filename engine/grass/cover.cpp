// Covers from a heightfield or a mesh, each triangle's seed, and the blade scatter. Every float
// operation here is written in the order render/grass_scatter.wgsl writes it.
//
// ge:summary Grass cover definitions: heightfield and mesh covers, the triangle seed, the scatter
// ge:keywords grass cover scatter blade triangle seed clump share rank
// ge:skill grass
// ge:test tests/grass_cover_test.cpp
#include "grass/cover.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ge::grass {

namespace {

uint32_t centimetres(float v) {
  const auto q = static_cast<int32_t>(std::floor(v * 100.0f + 0.5f));
  uint32_t u;
  std::memcpy(&u, &q, sizeof u);
  return u;
}

uint32_t as_bits(int32_t v) {
  uint32_t u;
  std::memcpy(&u, &v, sizeof u);
  return u;
}

// A triangle onto the cover, halved across its longest edge until its blades fit a triangle's
// budget (half of kMaxBladesPerTri at its kind's density), so a large floor is never capped.
void push_split(Cover& cover, const Tri& t, int depth = 0) {
  const float density = t.kind < cover.kinds.size() ? cover.kinds[t.kind].density : 0.0f;
  if (depth >= 16 || tri_area(t) * density <= 0.5f * static_cast<float>(kMaxBladesPerTri)) {
    cover.tris.push_back(t);
    return;
  }
  const float* p[3] = {t.a, t.b, t.c};
  const auto len2 = [&](int i, int j) {
    float d = 0;
    for (int k = 0; k < 3; ++k) d += (p[j][k] - p[i][k]) * (p[j][k] - p[i][k]);
    return d;
  };
  // The longest edge, from corner e to f = e + 1; its midpoint m splits the triangle in two.
  int e = 0;
  if (len2(1, 2) > len2(e, (e + 1) % 3)) e = 1;
  if (len2(2, 0) > len2(e, (e + 1) % 3)) e = 2;
  const int f = (e + 1) % 3;
  float m[3];
  for (int k = 0; k < 3; ++k) m[k] = 0.5f * (p[e][k] + p[f][k]);
  const float ms = 0.5f * (t.share[e] + t.share[f]);
  Tri one = t, two = t;
  float* d1[3] = {one.a, one.b, one.c};
  float* d2[3] = {two.a, two.b, two.c};
  for (int k = 0; k < 3; ++k) {
    d1[f][k] = m[k];  // e, m, o
    d2[e][k] = m[k];  // m, f, o
  }
  one.share[f] = ms;
  two.share[e] = ms;
  push_split(cover, one, depth + 1);
  push_split(cover, two, depth + 1);
}

}  // namespace

float tri_area(const Tri& t) {
  const float e1[3] = {t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
  const float e2[3] = {t.c[0] - t.a[0], t.c[1] - t.a[1], t.c[2] - t.a[2]};
  const float x = e1[1] * e2[2] - e1[2] * e2[1];
  const float y = e1[2] * e2[0] - e1[0] * e2[2];
  const float z = e1[0] * e2[1] - e1[1] * e2[0];
  return 0.5f * std::sqrt(x * x + y * y + z * z);
}

uint32_t tri_seed(const Tri& t, uint32_t cover_seed) {
  uint32_t h = hash(cover_seed ^ 0x9e3779b9u);
  for (const float* p : {t.a, t.b, t.c}) {
    for (int k = 0; k < 3; ++k) h = hash(h ^ centimetres(p[k]));
  }
  return h;
}

uint32_t blade_slots(const Tri& t, const Kind& kind, uint32_t seed) {
  const float most = std::max(std::max(t.share[0], t.share[1]), t.share[2]);
  const float expect = tri_area(t) * kind.density * most;
  float n = std::floor(expect);
  if (unit(hash(seed ^ 0xa511e9b3u)) < expect - n) n += 1.0f;
  return static_cast<uint32_t>(std::min(n, static_cast<float>(kMaxBladesPerTri)));
}

bool scatter_one(const Tri& t, const Kind& kind, uint32_t seed, uint32_t slot, Blade& out,
                 float& rank) {
  const uint32_t h0 = hash(seed + slot * 0x9e3779b9u);
  const uint32_t h1 = hash(h0), h2 = hash(h1), h3 = hash(h2), h4 = hash(h3), h5 = hash(h4),
                 h6 = hash(h5), h7 = hash(h6), h8 = hash(h7);
  // A uniform point: the square-root fold.
  const float s = std::sqrt(unit(h0));
  const float u = 1.0f - s;
  const float v = unit(h1) * s;
  const float w = 1.0f - u - v;
  float p[3];
  for (int k = 0; k < 3; ++k) p[k] = t.a[k] * u + t.b[k] * v + t.c[k] * w;
  const float share = t.share[0] * u + t.share[1] * v + t.share[2] * w;
  const float most = std::max(std::max(t.share[0], t.share[1]), t.share[2]);
  if (unit(h2) * most >= share) return false;
  rank = unit(h3);

  // The clump: the nearest jittered centre of the clump grid.
  float scale = 1.0f, toward[2] = {0, 0};
  if (kind.clump > 0) {
    const float gx = std::floor(p[0] / kind.clump), gz = std::floor(p[2] / kind.clump);
    float best = 1e30f;
    uint32_t best_h = 0;
    for (int dz = -1; dz <= 1; ++dz) {
      for (int dx = -1; dx <= 1; ++dx) {
        const float cx = gx + static_cast<float>(dx), cz = gz + static_cast<float>(dz);
        const uint32_t ch =
            hash(hash(as_bits(static_cast<int32_t>(cx))) ^ as_bits(static_cast<int32_t>(cz)));
        const float mx = (cx + 0.5f + (unit(ch) - 0.5f) * 0.8f) * kind.clump;
        const float mz = (cz + 0.5f + (unit(hash(ch)) - 0.5f) * 0.8f) * kind.clump;
        const float ex = mx - p[0], ez = mz - p[2];
        const float d2 = ex * ex + ez * ez;
        if (d2 < best) {
          best = d2;
          best_h = ch;
          toward[0] = ex;
          toward[1] = ez;
        }
      }
    }
    scale = 0.75f + 0.5f * unit(hash(best_h ^ 0x5bd1e995u));
    const float tl = std::sqrt(best);
    if (tl > 1e-6f) {
      toward[0] = toward[0] / tl;
      toward[1] = toward[1] / tl;
    }
  }
  float dx = unit(h4) * 2.0f - 1.0f, dz = unit(h5) * 2.0f - 1.0f;
  float dl = std::sqrt(dx * dx + dz * dz) + 1e-6f;
  dx = dx / dl;
  dz = dz / dl;
  float lx = dx * (1.0f - kind.pull) + toward[0] * kind.pull;
  float lz = dz * (1.0f - kind.pull) + toward[1] * kind.pull;
  dl = std::sqrt(lx * lx + lz * lz);
  if (dl < 1e-4f) {
    lx = dx;
    lz = dz;
  } else {
    lx = lx / dl;
    lz = lz / dl;
  }
  const float angle = kind.lean + kind.lean_jitter * (unit(h6) * 2.0f - 1.0f);

  out.root[0] = p[0];
  out.root[1] = p[1];
  out.root[2] = p[2];
  out.seed = (h0 & 0xffffffu) | (t.kind << 24u);
  out.height = kind.height * (1.0f + kind.height_jitter * (unit(h7) * 2.0f - 1.0f)) * scale;
  out.width = kind.width * (0.8f + 0.4f * unit(h8));
  out.lean[0] = lx * angle;
  out.lean[1] = lz * angle;
  return true;
}

void scatter(const Cover& cover, size_t i, std::vector<Blade>& out, float keep) {
  const Tri& t = cover.tris[i];
  const Kind& kind = cover.kinds[t.kind];
  const uint32_t seed = tri_seed(t, cover.seed);
  const uint32_t n = blade_slots(t, kind, seed);
  for (uint32_t slot = 0; slot < n; ++slot) {
    Blade b{};
    float rank = 0;
    if (scatter_one(t, kind, seed, slot, b, rank) && rank < keep) out.push_back(b);
  }
}

std::vector<Blade> scatter_all(const Cover& cover) {
  std::vector<Blade> out;
  for (size_t i = 0; i < cover.tris.size(); ++i) scatter(cover, i, out);
  return out;
}

void cover_heightfield(Cover& cover, const core::Heightfield& field, const Region& region,
                       double cell, const std::function<CoverAt(double x, double z)>& at) {
  const int nx = static_cast<int>(std::floor((region.x1 - region.x0) / cell));
  const int nz = static_cast<int>(std::floor((region.z1 - region.z0) / cell));
  std::vector<CoverAt> corner(static_cast<size_t>(nx + 1) * static_cast<size_t>(nz + 1));
  std::vector<float> height(corner.size());
  for (int k = 0; k <= nz; ++k) {
    for (int i = 0; i <= nx; ++i) {
      const double x = region.x0 + i * cell, z = region.z0 + k * cell;
      const size_t c =
          static_cast<size_t>(k) * (static_cast<size_t>(nx) + 1) + static_cast<size_t>(i);
      corner[c] = at(x, z);
      height[c] = static_cast<float>(core::sample_field(field, x, z));
    }
  }
  for (int k = 0; k < nz; ++k) {
    for (int i = 0; i < nx; ++i) {
      const size_t row = static_cast<size_t>(nx) + 1;
      const size_t c00 = static_cast<size_t>(k) * row + static_cast<size_t>(i);
      const size_t idx[4] = {c00, c00 + 1, c00 + row, c00 + row + 1};
      float most = 0;
      for (const size_t c : idx) most = std::max(most, corner[c].share);
      if (most <= 0) continue;
      const auto put = [&](float* dst, size_t c) {
        const int ci = static_cast<int>(c % row), ck = static_cast<int>(c / row);
        dst[0] = static_cast<float>(region.x0 + ci * cell);
        dst[1] = height[c];
        dst[2] = static_cast<float>(region.z0 + ck * cell);
      };
      // Split along the diagonal from (i, k) to (i + 1, k + 1).
      const size_t tris[2][3] = {{idx[0], idx[3], idx[1]}, {idx[0], idx[2], idx[3]}};
      for (const auto& tri : tris) {
        Tri t;
        put(t.a, tri[0]);
        put(t.b, tri[1]);
        put(t.c, tri[2]);
        for (int j = 0; j < 3; ++j) t.share[j] = std::clamp(corner[tri[j]].share, 0.0f, 1.0f);
        t.kind = corner[idx[0]].kind;
        push_split(cover, t);
      }
    }
  }
}

void add_mesh(Cover& cover, std::span<const float> positions, std::span<const uint32_t> indices,
              uint32_t kind, float share, float min_up) {
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    Tri t;
    float* dst[3] = {t.a, t.b, t.c};
    for (int j = 0; j < 3; ++j) {
      const size_t v = indices[i + static_cast<size_t>(j)];
      for (int k = 0; k < 3; ++k) dst[j][k] = positions[v * 3 + static_cast<size_t>(k)];
    }
    const float e1[3] = {t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
    const float e2[3] = {t.c[0] - t.a[0], t.c[1] - t.a[1], t.c[2] - t.a[2]};
    const float nx = e1[1] * e2[2] - e1[2] * e2[1];
    const float ny = e1[2] * e2[0] - e1[0] * e2[2];
    const float nz = e1[0] * e2[1] - e1[1] * e2[0];
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 0 || std::fabs(ny) / len < min_up) continue;
    t.share[0] = t.share[1] = t.share[2] = share;
    t.kind = kind;
    push_split(cover, t);
  }
}

}  // namespace ge::grass
