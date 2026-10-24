// The room builder's cave half: the seeded rock field over a grid and its surface by surface
// nets, tunnels cut at the mouths and the caps across them cut away (build.hpp states the model).
//
// Speed: the footprint's 2D distance is found once a column, and the noise only where the rock
// can reach the surface: where the prism's distance is beyond the cave's depth and five cells,
// the noise (in [0, depth]) cannot change the sign, and no cell the surface crosses has that point
// for a corner (a corner is within two cells of a crossing), so neither a vertex nor its normal
// reads it; its value is the distance alone.
//
// ge:summary Grows a cave room: rounded prism field plus seeded fbm, tunnels, surface nets
// ge:keywords cave surface-nets sdf field isosurface rock tunnel mouth gradient normals grid
// ge:skill rooms
// ge:test tests/rooms_build_test.cpp
#include <algorithm>
#include <cmath>

#include "rooms/build_parts.hpp"
#include "rooms/noise3.hpp"
#include "rooms/relief.hpp"

namespace ge::rooms::detail {

namespace {

float smoothstep01(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// An opening in world terms, for the field.
struct Mouth {
  WallFrame f;
  float u0, u1, v0, v1;
};

}  // namespace

void build_cave(const Level& level, size_t r, const std::vector<Opening>& open, Mesh& out) {
  const Room& room = level.rooms[r];
  const float cell = float(room.cave_cell_cm) / 100.0f;
  const float depth = float(room.cave_dm) / 10.0f;
  const float scale = float(room.cave_scale_dm) / 10.0f;
  const uint32_t seed = mix_seed(room.seed, 0);
  // The rock's grain: its wall material's when that is one that runs on in the world (rock,
  // crag), plain rock otherwise.
  const Relief wall_relief = level.materials[room.corners[0].wall].relief;
  const Relief grain = wall_relief == Relief::crag ? Relief::crag : Relief::rock;
  const size_t nc = room.corners.size();
  std::vector<float> px(nc), pz(nc);
  float lo[3] = {1e30f, 0, 1e30f}, hi[3] = {-1e30f, 0, -1e30f};
  for (size_t i = 0; i < nc; ++i) {
    double c[2];
    corner_m(room, i, c);
    px[i] = float(c[0]);
    pz[i] = float(c[1]);
    lo[0] = std::min(lo[0], px[i]);
    hi[0] = std::max(hi[0], px[i]);
    lo[2] = std::min(lo[2], pz[i]);
    hi[2] = std::max(hi[2], pz[i]);
  }
  const float yf = float(metres(int64_t(room.origin[1]) + room.floor_y));
  const float yc = float(metres(int64_t(room.origin[1]) + room.ceil_y));
  lo[1] = yf;
  hi[1] = yc;
  const float pad = 2.0f * cell;
  int dim[3];
  for (int k = 0; k < 3; ++k) {
    lo[k] -= pad;
    hi[k] += pad;
    dim[k] = int(std::ceil((hi[k] - lo[k]) / cell)) + 1;
  }
  const size_t nx = size_t(dim[0]), ny = size_t(dim[1]), nz = size_t(dim[2]);
  const float radius = std::min(0.35f * (yc - yf), std::max(2.0f * cell, depth));

  std::vector<Mouth> mouths;
  mouths.reserve(open.size());
  for (const Opening& o : open) {
    mouths.push_back({wall_frame(room, o.wall), o.u0, o.u1, o.v0, o.v1});
  }
  const float tunnel = depth + 3.0f * cell;  // how deep a mouth's tunnel reaches in
  const float mouth_fade = 2.0f * depth + 2.0f * cell;

  // The footprint's distance, once a column.
  std::vector<float> d2(nx * nz);
  for (size_t k = 0; k < nz; ++k) {
    for (size_t i = 0; i < nx; ++i) {
      d2[k * nx + i] = polygon_distance(px, pz, lo[0] + float(i) * cell, lo[2] + float(k) * cell);
    }
  }
  std::vector<float> field(nx * ny * nz);
  const float far = depth + 5.0f * cell;
  for (size_t k = 0; k < nz; ++k) {
    for (size_t j = 0; j < ny; ++j) {
      for (size_t i = 0; i < nx; ++i) {
        const float p[3] = {lo[0] + float(i) * cell, lo[1] + float(j) * cell,
                            lo[2] + float(k) * cell};
        // The prism, its edges rounded by `radius`.
        const float dy = std::max(yf - p[1], p[1] - yc);
        const float qx = d2[k * nx + i] + radius, qy = dy + radius;
        const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        float f = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
        float mouth = 1.0f, cut = 1e30f;
        for (const Mouth& m : mouths) {
          const float rel[3] = {p[0] - m.f.o[0], p[1] - m.f.o[1], p[2] - m.f.o[2]};
          const float a = rel[0] * m.f.u[0] + rel[2] * m.f.u[2];
          const float b = rel[1];
          const float c = rel[0] * m.f.n[0] + rel[2] * m.f.n[2];
          // The tunnel: the opening carried `tunnel` into the room, solid behind the wall.
          const float ta = std::max(m.u0 - a, a - m.u1), tb = std::max(m.v0 - b, b - m.v1);
          const float tc = std::max(-c, c - tunnel);
          const float ea = std::max(ta, 0.0f), eb = std::max(tb, 0.0f), ec = std::max(tc, 0.0f);
          cut = std::min(
              cut, std::sqrt(ea * ea + eb * eb + ec * ec) + std::min(std::max({ta, tb, tc}), 0.0f));
          const float da = std::max(ta, 0.0f), db = std::max(tb, 0.0f), dc = std::max(c, 0.0f);
          mouth =
              std::min(mouth, smoothstep01(std::sqrt(da * da + db * db + dc * dc) / mouth_fade));
        }
        if (std::fabs(f) <= far) {
          // The rock: in only, a third of it on the floor so it stays walkable.
          const float floor_w = 0.35f + 0.65f * smoothstep01((p[1] - yf) / (2.0f * depth + cell));
          f += depth * relief_height(grain, 0, 0, p, seed, scale) * floor_w * mouth;
        }
        field[(k * ny + j) * nx + i] = std::min(f, cut);
      }
    }
  }

  // Surface nets: a vertex in every cell the surface crosses.
  const size_t cx = nx - 1, cy = ny - 1, cz = nz - 1;
  std::vector<int64_t> vid(cx * cy * cz, -1);
  Emitter e{out};
  const uint8_t wall_mat = room.corners[0].wall;
  auto g = [&](size_t i, size_t j, size_t k) { return field[(k * ny + j) * nx + i]; };
  static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                        {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (size_t k = 0; k < cz; ++k) {
    for (size_t j = 0; j < cy; ++j) {
      for (size_t i = 0; i < cx; ++i) {
        float c[8];
        int air = 0;
        for (int q = 0; q < 8; ++q) {
          c[q] = g(i + size_t(q & 1), j + size_t((q >> 1) & 1), k + size_t((q >> 2) & 1));
          air += c[q] < 0;
        }
        if (air == 0 || air == 8) continue;
        float sum[3] = {0, 0, 0};
        int crossings = 0;
        for (const auto& ed : kEdges) {
          const float a = c[ed[0]], b = c[ed[1]];
          if ((a < 0) == (b < 0)) continue;
          const float t = a / (a - b);
          for (int ax = 0; ax < 3; ++ax) {
            const float pa = float((ed[0] >> ax) & 1), pb = float((ed[1] >> ax) & 1);
            sum[ax] += pa + (pb - pa) * t;
          }
          ++crossings;
        }
        const float p[3] = {lo[0] + (float(i) + sum[0] / float(crossings)) * cell,
                            lo[1] + (float(j) + sum[1] / float(crossings)) * cell,
                            lo[2] + (float(k) + sum[2] / float(crossings)) * cell};
        // Into the air: down the field's gradient, the cell's mean difference on each axis.
        float n[3] = {-((c[1] - c[0]) + (c[3] - c[2]) + (c[5] - c[4]) + (c[7] - c[6])),
                      -((c[2] - c[0]) + (c[3] - c[1]) + (c[6] - c[4]) + (c[7] - c[5])),
                      -((c[4] - c[0]) + (c[5] - c[1]) + (c[6] - c[2]) + (c[7] - c[3]))};
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 0) {
          n[0] /= l;
          n[1] /= l;
          n[2] /= l;
        }
        const uint8_t mat = n[1] > 0.55f ? room.floor : n[1] < -0.55f ? room.ceiling : wall_mat;
        vid[(k * cy + j) * cx + i] = e.vertex(p, n, mat);
      }
    }
  }
  // A quad round every crossed grid edge, facing the end in the air.
  auto cell_v = [&](size_t i, size_t j, size_t k) { return vid[(k * cy + j) * cx + i]; };
  for (size_t k = 1; k + 1 < nz; ++k) {
    for (size_t j = 1; j + 1 < ny; ++j) {
      for (size_t i = 1; i + 1 < nx; ++i) {
        const float f0 = g(i, j, k);
        for (int ax = 0; ax < 3; ++ax) {
          const size_t i1 = i + (ax == 0), j1 = j + (ax == 1), k1 = k + (ax == 2);
          if (i1 >= nx || j1 >= ny || k1 >= nz) continue;
          const float f1 = g(i1, j1, k1);
          if ((f0 < 0) == (f1 < 0)) continue;
          int64_t q[4];
          if (ax == 0) {
            q[0] = cell_v(i, j - 1, k - 1), q[1] = cell_v(i, j, k - 1), q[2] = cell_v(i, j, k),
            q[3] = cell_v(i, j - 1, k);
          } else if (ax == 1) {
            q[0] = cell_v(i - 1, j, k - 1), q[1] = cell_v(i, j, k - 1), q[2] = cell_v(i, j, k),
            q[3] = cell_v(i - 1, j, k);
          } else {
            q[0] = cell_v(i - 1, j - 1, k), q[1] = cell_v(i, j - 1, k), q[2] = cell_v(i, j, k),
            q[3] = cell_v(i - 1, j, k);
          }
          if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[3] < 0) continue;
          float facing[3] = {0, 0, 0};
          facing[ax] = f0 < 0 ? -1.0f : 1.0f;
          // Split along the shorter diagonal: the longer one folds the quad over a ridge.
          auto span2 = [&](int64_t a, int64_t b) {
            float s = 0;
            for (int c = 0; c < 3; ++c) {
              const float d =
                  out.pos[size_t(a) * 3 + size_t(c)] - out.pos[size_t(b) * 3 + size_t(c)];
              s += d * d;
            }
            return s;
          };
          if (span2(q[0], q[2]) <= span2(q[1], q[3])) {
            e.quad(uint32_t(q[0]), uint32_t(q[1]), uint32_t(q[2]), uint32_t(q[3]), facing);
          } else {
            e.quad(uint32_t(q[1]), uint32_t(q[2]), uint32_t(q[3]), uint32_t(q[0]), facing);
          }
        }
      }
    }
  }
  // The caps across the mouths, cut away: triangles lying on a wall's plane inside an opening.
  if (mouths.empty()) return;
  const float near = 0.6f * cell, inset = 0.3f * cell;
  std::vector<uint32_t> kept;
  kept.reserve(out.idx.size());
  for (size_t t = 0; t + 2 < out.idx.size(); t += 3) {
    bool cap = false;
    for (const Mouth& m : mouths) {
      bool all = true;
      for (int s = 0; s < 3 && all; ++s) {
        const float* p = &out.pos[size_t(out.idx[t + size_t(s)]) * 3];
        const float rel[3] = {p[0] - m.f.o[0], p[1] - m.f.o[1], p[2] - m.f.o[2]};
        const float a = rel[0] * m.f.u[0] + rel[2] * m.f.u[2];
        const float c = rel[0] * m.f.n[0] + rel[2] * m.f.n[2];
        all = std::fabs(c) < near && a > m.u0 + inset && a < m.u1 - inset &&
              rel[1] > m.v0 + inset && rel[1] < m.v1 - inset;
      }
      cap |= all;
    }
    if (!cap) kept.insert(kept.end(), {out.idx[t], out.idx[t + 1], out.idx[t + 2]});
  }
  out.idx = std::move(kept);
}

}  // namespace ge::rooms::detail
