// The triangles bucketed in their grid, and the swept separating-axis test against each one.
//
// ge:summary Mesh world's grid build and the swept box-against-triangle test
// ge:keywords move mesh-world triangle grid bucket separating-axis sweep enter leave skin
// ge:skill move
// ge:test tests/move_mesh_world_test.cpp
#include "move/mesh_world.hpp"

#include <algorithm>
#include <cmath>

namespace ge::move {

namespace {

constexpr double kMaxCells = 4.0e6;

int32_t cell_of(double v, double lo, double cell, int32_t dim) {
  const double k = std::floor((v - lo) / cell);
  if (!(k > 0)) return 0;  // also NaN
  return k >= dim - 1 ? dim - 1 : static_cast<int32_t>(k);
}

// The axes a box and a triangle are separated along when they are apart: the box's three, the
// triangle's normal and its edges crossed with the box's axes; unit length, `count` of them.
int axes_of(const Vec3 t[3], Vec3 out[13]) {
  int count = 0;
  out[count++] = {1, 0, 0};
  out[count++] = {0, 1, 0};
  out[count++] = {0, 0, 1};
  const Vec3 e[3] = {sub(t[1], t[0]), sub(t[2], t[1]), sub(t[0], t[2])};
  const Vec3 n = cross(e[0], e[1]);
  const double scale_sq = dot(e[0], e[0]) * dot(e[1], e[1]);
  if (dot(n, n) > 1e-24 * scale_sq) out[count++] = unit(n);
  const Vec3 box_axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (const Vec3& edge : e) {
    const double el = dot(edge, edge);
    for (const Vec3& b : box_axes) {
      const Vec3 c = cross(edge, b);
      if (dot(c, c) > 1e-18 * el) out[count++] = unit(c);
    }
  }
  return count;
}

}  // namespace

MeshWorld build_mesh_world(std::span<const Vec3> corners, double cell) {
  MeshWorld w;
  Vec3 lo{0, 0, 0}, hi{0, 0, 0};
  bool any = false;
  for (size_t i = 0; i + 2 < corners.size(); i += 3) {
    const Vec3* t = &corners[i];
    if (dot(cross(sub(t[1], t[0]), sub(t[2], t[0])), cross(sub(t[1], t[0]), sub(t[2], t[0]))) <=
        0) {
      continue;
    }
    for (int k = 0; k < 3; ++k) {
      w.tri.push_back(t[k]);
      if (!any) {
        lo = hi = t[k];
        any = true;
      }
      lo = {std::min(lo.x, t[k].x), std::min(lo.y, t[k].y), std::min(lo.z, t[k].z)};
      hi = {std::max(hi.x, t[k].x), std::max(hi.y, t[k].y), std::max(hi.z, t[k].z)};
    }
  }
  w.cell = cell > 0 ? cell : 1;
  const Vec3 span = sub(hi, lo);
  while ((span.x / w.cell + 1) * (span.y / w.cell + 1) * (span.z / w.cell + 1) > kMaxCells) {
    w.cell *= 2;
  }
  w.lo = lo;
  w.dim[0] = static_cast<int32_t>(span.x / w.cell) + 1;
  w.dim[1] = static_cast<int32_t>(span.y / w.cell) + 1;
  w.dim[2] = static_cast<int32_t>(span.z / w.cell) + 1;
  const size_t cells = size_t(w.dim[0]) * size_t(w.dim[1]) * size_t(w.dim[2]);
  const size_t tris = w.tri.size() / 3;
  w.tri_cell.resize(tris * 6);
  // Each triangle's cell range, then a count a cell, then the items in place.
  for (size_t t = 0; t < tris; ++t) {
    const Vec3* p = &w.tri[t * 3];
    const double l[3] = {std::min({p[0].x, p[1].x, p[2].x}), std::min({p[0].y, p[1].y, p[2].y}),
                         std::min({p[0].z, p[1].z, p[2].z})};
    const double h[3] = {std::max({p[0].x, p[1].x, p[2].x}), std::max({p[0].y, p[1].y, p[2].y}),
                         std::max({p[0].z, p[1].z, p[2].z})};
    const double o[3] = {w.lo.x, w.lo.y, w.lo.z};
    for (int k = 0; k < 3; ++k) {
      w.tri_cell[t * 6 + size_t(k)] = cell_of(l[k], o[k], w.cell, w.dim[k]);
      w.tri_cell[t * 6 + 3 + size_t(k)] = cell_of(h[k], o[k], w.cell, w.dim[k]);
    }
  }
  w.start.assign(cells + 1, 0);
  const auto each_cell = [&](size_t t, auto&& fn) {
    const int32_t* c = &w.tri_cell[t * 6];
    for (int32_t z = c[2]; z <= c[5]; ++z) {
      for (int32_t y = c[1]; y <= c[4]; ++y) {
        for (int32_t x = c[0]; x <= c[3]; ++x) {
          fn((size_t(z) * size_t(w.dim[1]) + size_t(y)) * size_t(w.dim[0]) + size_t(x));
        }
      }
    }
  };
  for (size_t t = 0; t < tris; ++t) each_cell(t, [&](size_t c) { ++w.start[c + 1]; });
  for (size_t c = 0; c < cells; ++c) w.start[c + 1] += w.start[c];
  w.items.resize(w.start[cells]);
  std::vector<uint32_t> fill(w.start.begin(), w.start.end() - 1);
  for (size_t t = 0; t < tris; ++t) {
    each_cell(t, [&](size_t c) { w.items[fill[c]++] = static_cast<uint32_t>(t); });
  }
  // Keep only each triangle's first cell (its lowest corner's) in tri_cell, for the query's dedupe.
  for (size_t t = 0; t < tris; ++t) {
    for (int k = 0; k < 3; ++k) w.tri_cell[t * 3 + size_t(k)] = w.tri_cell[t * 6 + size_t(k)];
  }
  w.tri_cell.resize(tris * 3);
  return w;
}

Hit trace_mesh_world(const MeshWorld& w, const Box& box, const Vec3& from, const Vec3& to) {
  const Vec3 mid = scale(add(box.mins, box.maxs), 0.5);
  const Vec3 ext = scale(sub(box.maxs, box.mins), 0.5);
  const Vec3 c0 = add(from, mid);
  const Vec3 c1 = add(to, mid);
  Hit best;
  best.end = to;
  if (w.tri.empty()) return best;
  // The cells the swept box covers.
  const double pad = w.skin;
  const double ql[3] = {std::min(c0.x, c1.x) - ext.x - pad, std::min(c0.y, c1.y) - ext.y - pad,
                        std::min(c0.z, c1.z) - ext.z - pad};
  const double qh[3] = {std::max(c0.x, c1.x) + ext.x + pad, std::max(c0.y, c1.y) + ext.y + pad,
                        std::max(c0.z, c1.z) + ext.z + pad};
  const double o[3] = {w.lo.x, w.lo.y, w.lo.z};
  int32_t lo[3], hi[3];
  for (int k = 0; k < 3; ++k) {
    // A query wholly off one side of the grid touches nothing.
    if (qh[k] < o[k] - w.cell || ql[k] > o[k] + w.cell * w.dim[k] + w.cell) return best;
    lo[k] = cell_of(ql[k], o[k], w.cell, w.dim[k]);
    hi[k] = cell_of(qh[k], o[k], w.cell, w.dim[k]);
  }
  const Vec3 motion = sub(c1, c0);
  for (int32_t z = lo[2]; z <= hi[2]; ++z) {
    for (int32_t y = lo[1]; y <= hi[1]; ++y) {
      for (int32_t x = lo[0]; x <= hi[0]; ++x) {
        const size_t c = (size_t(z) * size_t(w.dim[1]) + size_t(y)) * size_t(w.dim[0]) + size_t(x);
        for (uint32_t i = w.start[c]; i < w.start[c + 1]; ++i) {
          const uint32_t t = w.items[i];
          const int32_t* tc = &w.tri_cell[size_t(t) * 3];
          // Visit a triangle in the first covered cell it lies in, and only there.
          if (std::max(tc[0], lo[0]) != x || std::max(tc[1], lo[1]) != y ||
              std::max(tc[2], lo[2]) != z) {
            continue;
          }
          const Vec3* p = &w.tri[size_t(t) * 3];
          Vec3 axes[13];
          const int n = axes_of(p, axes);
          double enter = -1, leave = 1;
          bool starts_out = false, misses = false;
          Vec3 normal{0, 1, 0};
          // Each axis is the two planes of the triangle's slab across it, pushed out by the box.
          for (int a = 0; a < n && !misses; ++a) {
            const Vec3& ax = axes[a];
            const double p0 = dot(ax, p[0]), p1 = dot(ax, p[1]), p2 = dot(ax, p[2]);
            const double reach =
                std::fabs(ax.x) * ext.x + std::fabs(ax.y) * ext.y + std::fabs(ax.z) * ext.z;
            const double s0 = dot(ax, c0), s1 = s0 + dot(ax, motion);
            const double bounds[2] = {std::max({p0, p1, p2}), -std::min({p0, p1, p2})};
            for (int side = 0; side < 2; ++side) {
              const double sign = side ? -1.0 : 1.0;
              const double d1 = sign * s0 - (bounds[side] + reach);
              const double d2 = sign * s1 - (bounds[side] + reach);
              if (d1 > 0) starts_out = true;
              if (d1 > 0 && d2 >= d1) {
                misses = true;
                break;
              }
              if (d1 <= 0 && d2 <= 0) continue;
              if (d1 > d2) {
                const double f = (d1 - w.skin) / (d1 - d2);
                if (f > enter) {
                  enter = f;
                  normal = scale(ax, sign);
                }
              } else {
                const double f = (d1 + w.skin) / (d1 - d2);
                if (f < leave) leave = f;
              }
            }
          }
          if (misses) continue;
          if (!starts_out) {
            best.start_solid = true;  // across the triangle already: let out the way it came
            continue;
          }
          if (enter < leave && enter > -1 && enter < best.fraction) {
            best.fraction = enter < 0 ? 0 : enter;
            best.normal = normal;
          }
        }
      }
    }
  }
  if (best.fraction < 1) best.end = add_scaled(from, sub(to, from), best.fraction);
  return best;
}

namespace {
Hit trace_thunk(const void* ctx, const Box& box, const Vec3& from, const Vec3& to) {
  return trace_mesh_world(*static_cast<const MeshWorld*>(ctx), box, from, to);
}
}  // namespace

World as_world(const MeshWorld& w) { return World{&w, trace_thunk}; }

}  // namespace ge::move
