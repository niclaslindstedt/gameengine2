// The room builder's prism half and the parts every room shares: openings in a wall's own terms,
// the floor and ceiling ear-clipped, gridded walls with seeded rock relief, frames and solids,
// and the level built room by room on the worker pool.
//
// ge:summary Builds prism rooms (gridded walls, rock relief, openings), frames, solids, digests
// ge:keywords build prism wall grid relief rock ear-clipping floor ceiling frame solid digest
// ge:keywords build_level parallel
// ge:skill rooms
// ge:test tests/rooms_build_test.cpp
#include "rooms/build.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/parallel.hpp"
#include "rooms/build_parts.hpp"
#include "rooms/noise3.hpp"
#include "rooms/relief.hpp"

namespace ge::rooms {

namespace detail {

namespace {

float smoothstep01(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// The footprint's triangles by ear clipping (corners counter-clockwise, x/z), as corner indices.
std::vector<uint32_t> ear_clip(const std::vector<double>& x, const std::vector<double>& z) {
  std::vector<uint32_t> left(x.size()), out;
  for (size_t i = 0; i < left.size(); ++i) left[i] = uint32_t(i);
  auto cr = [&](uint32_t a, uint32_t b, uint32_t c) {
    return (x[b] - x[a]) * (z[c] - z[a]) - (z[b] - z[a]) * (x[c] - x[a]);
  };
  while (left.size() > 3) {
    bool cut = false;
    const size_t n = left.size();
    for (size_t i = 0; i < n && !cut; ++i) {
      const uint32_t a = left[(i + n - 1) % n], b = left[i], c = left[(i + 1) % n];
      if (cr(a, b, c) <= 0) continue;  // a reflex or flat corner is no ear
      bool inside = false;
      for (uint32_t p : left) {
        if (p == a || p == b || p == c) continue;
        if (cr(a, b, p) >= 0 && cr(b, c, p) >= 0 && cr(c, a, p) >= 0) {
          inside = true;
          break;
        }
      }
      if (inside) continue;
      out.insert(out.end(), {a, b, c});
      left.erase(left.begin() + std::ptrdiff_t(i));
      cut = true;
    }
    if (!cut) break;  // only a degenerate polygon gets here; `validate` refuses those
  }
  if (left.size() == 3) out.insert(out.end(), {left[0], left[1], left[2]});
  return out;
}

void normalize(float v[3]) {
  const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 0) {
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
  }
}

// The wall's vertices from `first` on get the mean of their triangles' normals.
void smooth_normals(Mesh& m, uint32_t first_vertex, size_t first_index) {
  for (size_t v = first_vertex; v < m.vertices(); ++v)
    m.nrm[v * 3] = m.nrm[v * 3 + 1] = m.nrm[v * 3 + 2] = 0;
  for (size_t t = first_index; t + 2 < m.idx.size(); t += 3) {
    const float* a = &m.pos[size_t(m.idx[t]) * 3];
    const float* b = &m.pos[size_t(m.idx[t + 1]) * 3];
    const float* c = &m.pos[size_t(m.idx[t + 2]) * 3];
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                        e1[0] * e2[1] - e1[1] * e2[0]};
    for (int k = 0; k < 3; ++k) {
      float* to = &m.nrm[size_t(m.idx[t + size_t(k)]) * 3];
      to[0] += n[0];
      to[1] += n[1];
      to[2] += n[2];
    }
  }
  for (size_t v = first_vertex; v < m.vertices(); ++v) normalize(&m.nrm[v * 3]);
}

// Sorted breaks over [0, len]: the ends, `extra`, and a step every `cell` when cell > 0.
std::vector<float> breaks(float len, const std::vector<float>& extra, float cell) {
  std::vector<float> b{0.0f, len};
  for (float e : extra) {
    if (e > 0 && e < len) b.push_back(e);
  }
  if (cell > 0) {
    const int n = int(std::ceil(len / cell));
    for (int k = 1; k < n; ++k) b.push_back(len * float(k) / float(n));
  }
  std::sort(b.begin(), b.end());
  std::vector<float> out;
  for (float v : b) {
    if (out.empty() || v - out.back() > 1e-4f) out.push_back(v);
  }
  if (out.back() < len) out.back() = len;
  return out;
}

void build_wall(const Level& level, size_t r, size_t w, const std::vector<Opening>& open,
                Emitter& e) {
  const Room& room = level.rooms[r];
  const uint8_t mi = room.corners[w].wall;
  const Material& mat = level.materials[mi];
  const WallFrame f = wall_frame(room, w);
  struct Rect {
    float u0, u1, v0, v1;
  };
  std::vector<Rect> holes;
  std::vector<float> ue, ve;
  for (const Opening& o : open) {
    if (o.wall != w) continue;
    const Rect h{std::max(0.0f, o.u0), std::min(f.length, o.u1), std::max(0.0f, o.v0),
                 std::min(f.height, o.v1)};
    holes.push_back(h);
    ue.insert(ue.end(), {h.u0, h.u1});
    ve.insert(ve.end(), {h.v0, h.v1});
  }
  const bool rock = mat.relief != Relief::none;
  const float depth = float(mat.relief_cm) / 100.0f, scale = float(mat.scale_dm) / 10.0f;
  const float cell = relief_cell(mat.relief, scale, f.length * f.height);
  const float fade_m = std::max(depth * 2.5f, cell * 2.0f);
  const uint32_t seed = mix_seed(room.seed, uint32_t(w) + 1);
  const auto us = breaks(f.length, ue, cell), vs = breaks(f.height, ve, cell);
  const size_t nu = us.size(), nv = vs.size();
  std::vector<int64_t> id(nu * nv, -1);
  const uint32_t first_vertex = uint32_t(e.m.vertices());
  const size_t first_index = e.m.idx.size();
  auto vertex = [&](size_t a, size_t b) -> uint32_t {
    int64_t& slot = id[b * nu + a];
    if (slot >= 0) return uint32_t(slot);
    const float u = us[a], v = vs[b];
    float p[3] = {f.o[0] + f.u[0] * u, f.o[1] + v, f.o[2] + f.u[2] * u};
    if (rock) {
      float edge = std::min(std::min(u, f.length - u), std::min(v, f.height - v));
      for (const Rect& h : holes) {
        const float du = std::max({h.u0 - u, 0.0f, u - h.u1});
        const float dv = std::max({h.v0 - v, 0.0f, v - h.v1});
        edge = std::min(edge, std::sqrt(du * du + dv * dv));
      }
      const float d =
          depth * relief_height(mat.relief, u, v, p, seed, scale) * smoothstep01(edge / fade_m);
      for (int k = 0; k < 3; ++k) p[k] += f.n[k] * d;
    }
    slot = e.vertex(p, f.n, mi);
    return uint32_t(slot);
  };
  for (size_t b = 0; b + 1 < nv; ++b) {
    for (size_t a = 0; a + 1 < nu; ++a) {
      const float uc = 0.5f * (us[a] + us[a + 1]), vc = 0.5f * (vs[b] + vs[b + 1]);
      bool hole = false;
      for (const Rect& h : holes) hole |= uc > h.u0 && uc < h.u1 && vc > h.v0 && vc < h.v1;
      if (hole) continue;
      // The corners are made in order: a call's arguments are evaluated in an order the language
      // leaves open, and `vertex` appends.
      const uint32_t q0 = vertex(a, b), q1 = vertex(a + 1, b), q2 = vertex(a + 1, b + 1);
      const uint32_t q3 = vertex(a, b + 1);
      e.quad(q0, q1, q2, q3, f.n);
    }
  }
  if (rock) smooth_normals(e.m, first_vertex, first_index);
}

// A grown floor (side 0) or ceiling (side 1): a grid over the footprint, every cell wholly inside
// it, pushed into the room by the material's relief, faded to nothing a fade's width from the
// walls. It lies over the flat cap, which stays under it, so the grid's ragged edge (where it
// meets the fade, flat) never opens a hole.
void build_cap(const Level& level, size_t r, int side, const std::vector<float>& px,
               const std::vector<float>& pz, Emitter& e) {
  const Room& room = level.rooms[r];
  const uint8_t mi = side ? room.ceiling : room.floor;
  const Material& mat = level.materials[mi];
  if (mat.relief == Relief::none) return;
  const float depth = float(mat.relief_cm) / 100.0f, scale = float(mat.scale_dm) / 10.0f;
  const uint32_t seed = mix_seed(room.seed, side ? 0xce11u : 0xf100u);
  const float y = float(metres(int64_t(room.origin[1]) + (side ? room.ceil_y : room.floor_y)));
  const float n[3] = {0, side ? -1.0f : 1.0f, 0};
  float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
  for (size_t i = 0; i < px.size(); ++i) {
    lo[0] = std::min(lo[0], px[i]);
    hi[0] = std::max(hi[0], px[i]);
    lo[1] = std::min(lo[1], pz[i]);
    hi[1] = std::max(hi[1], pz[i]);
  }
  const float cell = relief_cell(mat.relief, scale, (hi[0] - lo[0]) * (hi[1] - lo[1]));
  const float fade_m = std::max(depth * 2.5f, cell * 2.0f);
  const size_t nx = size_t(std::ceil((hi[0] - lo[0]) / cell)) + 1;
  const size_t nz = size_t(std::ceil((hi[1] - lo[1]) / cell)) + 1;
  std::vector<float> dist(nx * nz);
  for (size_t k = 0; k < nz; ++k) {
    for (size_t i = 0; i < nx; ++i) {
      dist[k * nx + i] = polygon_distance(px, pz, lo[0] + float(i) * cell, lo[1] + float(k) * cell);
    }
  }
  std::vector<int64_t> id(nx * nz, -1);
  const uint32_t first_vertex = uint32_t(e.m.vertices());
  const size_t first_index = e.m.idx.size();
  auto vertex = [&](size_t i, size_t k) -> uint32_t {
    int64_t& slot = id[k * nx + i];
    if (slot >= 0) return uint32_t(slot);
    float p[3] = {lo[0] + float(i) * cell, y, lo[1] + float(k) * cell};
    const float d = depth * relief_height(mat.relief, p[0], p[2], p, seed, scale) *
                    smoothstep01(-dist[k * nx + i] / fade_m);
    p[1] += n[1] * d;
    slot = e.vertex(p, n, mi);
    return uint32_t(slot);
  };
  for (size_t k = 0; k + 1 < nz; ++k) {
    for (size_t i = 0; i + 1 < nx; ++i) {
      if (dist[k * nx + i] >= 0 || dist[k * nx + i + 1] >= 0 || dist[(k + 1) * nx + i] >= 0 ||
          dist[(k + 1) * nx + i + 1] >= 0) {
        continue;
      }
      const uint32_t q0 = vertex(i, k), q1 = vertex(i + 1, k), q2 = vertex(i + 1, k + 1);
      const uint32_t q3 = vertex(i, k + 1);  // in order, as in build_wall
      e.quad(q0, q1, q2, q3, n);
    }
  }
  smooth_normals(e.m, first_vertex, first_index);
}

}  // namespace

float polygon_distance(const std::vector<float>& x, const std::vector<float>& z, float px,
                       float pz) {
  float best = 1e30f;
  bool inside = false;
  const size_t n = x.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const float ex = x[i] - x[j], ez = z[i] - z[j];
    const float wx = px - x[j], wz = pz - z[j];
    const float t = std::clamp((wx * ex + wz * ez) / (ex * ex + ez * ez), 0.0f, 1.0f);
    const float dx = wx - ex * t, dz = wz - ez * t;
    best = std::min(best, dx * dx + dz * dz);
    if ((z[i] > pz) != (z[j] > pz) && px < ex * (pz - z[j]) / ez + x[j]) inside = !inside;
  }
  const float d = std::sqrt(best);
  return inside ? -d : d;
}

WallFrame wall_frame(const Room& room, size_t wall) {
  double a[2], b[2];
  corner_m(room, wall, a);
  corner_m(room, (wall + 1) % room.corners.size(), b);
  const double len = wall_length_m(room, wall);
  const double ux = (b[0] - a[0]) / len, uz = (b[1] - a[1]) / len;
  WallFrame f{};
  f.o[0] = float(a[0]);
  f.o[1] = float(metres(int64_t(room.origin[1]) + room.floor_y));
  f.o[2] = float(a[1]);
  f.u[0] = float(ux);
  f.u[2] = float(uz);
  f.n[0] = float(-uz);
  f.n[2] = float(ux);
  f.length = float(len);
  f.height = float(metres(int64_t(room.ceil_y) - room.floor_y));
  return f;
}

std::vector<Opening> openings_of(const Level& level, size_t room) {
  std::vector<Opening> out;
  const Room& here = level.rooms[room];
  const double floor_m = metres(int64_t(here.origin[1]) + here.floor_y);
  for (size_t k = 0; k < level.portals.size(); ++k) {
    const Portal& p = level.portals[k];
    const Room& from = level.rooms[p.room];
    const double y0 = metres(int64_t(from.origin[1]) + from.floor_y + p.sill);
    const double y1 = metres(int64_t(from.origin[1]) + from.floor_y + p.head);
    if (p.room == room) {
      out.push_back({p.wall, float(metres(p.offset)), float(metres(p.offset + p.width)),
                     float(y0 - floor_m), float(y1 - floor_m), p.trim, true, k});
    } else if (p.to == room) {
      // The opening's ends on the far wall, in this wall's own terms.
      double a[2], b[2], c[2];
      corner_m(from, p.wall, a);
      corner_m(from, (p.wall + 1u) % from.corners.size(), b);
      corner_m(here, p.to_wall, c);
      const double len = wall_length_m(from, p.wall), lh = wall_length_m(here, p.to_wall);
      double d[2];
      corner_m(here, (p.to_wall + 1u) % here.corners.size(), d);
      const double dir[2] = {(b[0] - a[0]) / len, (b[1] - a[1]) / len};
      const double hd[2] = {(d[0] - c[0]) / lh, (d[1] - c[1]) / lh};
      double u[2];
      for (int s = 0; s < 2; ++s) {
        const double along = metres(s ? p.offset + p.width : p.offset);
        const double e[2] = {a[0] + dir[0] * along - c[0], a[1] + dir[1] * along - c[1]};
        u[s] = e[0] * hd[0] + e[1] * hd[1];
      }
      out.push_back({p.to_wall, float(std::min(u[0], u[1])), float(std::max(u[0], u[1])),
                     float(y0 - floor_m), float(y1 - floor_m), p.trim, false, k});
    }
  }
  return out;
}

uint32_t Emitter::vertex(const float p[3], const float n[3], uint8_t mat) {
  m.pos.insert(m.pos.end(), {p[0], p[1], p[2]});
  m.nrm.insert(m.nrm.end(), {n[0], n[1], n[2]});
  m.mat.push_back(mat);
  return uint32_t(m.mat.size() - 1);
}

void Emitter::tri(uint32_t a, uint32_t b, uint32_t c, const float facing[3]) {
  const float* pa = &m.pos[size_t(a) * 3];
  const float* pb = &m.pos[size_t(b) * 3];
  const float* pc = &m.pos[size_t(c) * 3];
  const float e1[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
  const float e2[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
  const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                      e1[0] * e2[1] - e1[1] * e2[0]};
  if (n[0] * facing[0] + n[1] * facing[1] + n[2] * facing[2] < 0) std::swap(b, c);
  m.idx.insert(m.idx.end(), {a, b, c});
}

void Emitter::quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, const float facing[3]) {
  tri(a, b, c, facing);
  tri(a, c, d, facing);
}

void emit_box(Emitter& e, const float o[3], const float u[3], const float v[3], const float w[3],
              const float lo[3], const float hi[3], uint8_t mat, unsigned skip) {
  const float* axis[3] = {u, v, w};
  auto at = [&](float a, float b, float c, float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = o[k] + u[k] * a + v[k] * b + w[k] * c;
  };
  for (int f = 0; f < 6; ++f) {
    if (skip & (1u << f)) continue;
    const int ax = f / 2, s1 = (ax + 1) % 3, s2 = (ax + 2) % 3;
    const bool high = f % 2 == 1;
    float n[3];
    for (int k = 0; k < 3; ++k) n[k] = axis[ax][k] * (high ? 1.0f : -1.0f);
    uint32_t q[4];
    for (int c = 0; c < 4; ++c) {
      float coord[3];
      coord[ax] = high ? hi[ax] : lo[ax];
      coord[s1] = (c == 1 || c == 2) ? hi[s1] : lo[s1];
      coord[s2] = (c >= 2) ? hi[s2] : lo[s2];
      float p[3];
      at(coord[0], coord[1], coord[2], p);
      q[c] = e.vertex(p, n, mat);
    }
    e.quad(q[0], q[1], q[2], q[3], n);
  }
}

FrameSize frame_size(const Level& level, const Portal& portal) {
  float cell = 0;
  for (uint16_t r : {portal.room, portal.to}) {
    if (r == kOutside) continue;
    const Room& room = level.rooms[r];
    if (room.shape == Shape::cave) cell = std::max(cell, float(room.cave_cell_cm) / 100.0f);
  }
  return {0.1f + cell * 0.5f, cell, 0.06f + cell};
}

void emit_frames(Emitter& e, const Level& level, size_t room, const std::vector<Opening>& open) {
  const Room& here = level.rooms[room];
  for (const Opening& o : open) {
    if (!o.own || o.trim == kNoMaterial) continue;
    const FrameSize fs = frame_size(level, level.portals[o.portal]);
    const WallFrame f = wall_frame(here, o.wall);
    const float up[3] = {0, 1, 0};
    const float ul = std::max(0.0f, o.u0 - fs.out), ur = std::min(f.length, o.u1 + fs.out);
    const float top = std::min(f.height, o.v1 + fs.out);
    const float d0 = -fs.half_depth, d1 = fs.half_depth;
    const bool sill = o.v0 > 0;
    const float foot = sill ? o.v0 + fs.in : o.v0;
    // The head, the jambs and, above the floor, the sill.
    const float boxes[4][4] = {{ul, ur, o.v1 - fs.in, top},
                               {ul, o.u0 + fs.in, foot, o.v1 - fs.in},
                               {o.u1 - fs.in, ur, foot, o.v1 - fs.in},
                               {ul, ur, std::max(0.0f, o.v0 - fs.out), o.v0 + fs.in}};
    for (int b = 0; b < (sill ? 4 : 3); ++b) {
      const float lo[3] = {boxes[b][0], boxes[b][2], d0}, hi[3] = {boxes[b][1], boxes[b][3], d1};
      if (lo[0] >= hi[0] || lo[1] >= hi[1]) continue;
      emit_box(e, f.o, f.u, up, f.n, lo, hi, o.trim);
    }
  }
}

void build_prism(const Level& level, size_t r, const std::vector<Opening>& open, Mesh& out) {
  const Room& room = level.rooms[r];
  Emitter e{out};
  const size_t n = room.corners.size();
  std::vector<double> x(n), z(n);
  for (size_t i = 0; i < n; ++i) {
    double c[2];
    corner_m(room, i, c);
    x[i] = c[0];
    z[i] = c[1];
  }
  const auto tris = ear_clip(x, z);
  const float yf = float(metres(int64_t(room.origin[1]) + room.floor_y));
  const float yc = float(metres(int64_t(room.origin[1]) + room.ceil_y));
  for (int side = 0; side < 2; ++side) {
    const float y = side ? yc : yf;
    const float n3[3] = {0, side ? -1.0f : 1.0f, 0};
    const uint8_t mat = side ? room.ceiling : room.floor;
    const uint32_t base = uint32_t(out.vertices());
    for (size_t i = 0; i < n; ++i) {
      const float p[3] = {float(x[i]), y, float(z[i])};
      e.vertex(p, n3, mat);
    }
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
      e.tri(base + tris[t], base + tris[t + 1], base + tris[t + 2], n3);
    }
  }
  std::vector<float> fx(n), fz(n);
  for (size_t i = 0; i < n; ++i) {
    fx[i] = float(x[i]);
    fz[i] = float(z[i]);
  }
  for (int side = 0; side < 2; ++side) build_cap(level, r, side, fx, fz, e);
  for (size_t w = 0; w < n; ++w) build_wall(level, r, w, open, e);
}

}  // namespace detail

Mesh build_room(const Level& level, size_t room) {
  Mesh out;
  const auto open = detail::openings_of(level, room);
  if (level.rooms[room].shape == Shape::cave) {
    detail::build_cave(level, room, open, out);
  } else {
    detail::build_prism(level, room, open, out);
  }
  detail::Emitter e{out};
  detail::emit_frames(e, level, room, open);
  const Room& here = level.rooms[room];
  for (const Solid& s : level.solids) {
    if (s.room != room) continue;
    const float o[3] = {float(metres(here.origin[0])), float(metres(here.origin[1])),
                        float(metres(here.origin[2]))};
    const float ax[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
      lo[k] = float(metres(s.min[k]));
      hi[k] = float(metres(s.max[k]));
    }
    // A box standing on a prism's floor has no foot to draw.
    const unsigned skip = here.shape == Shape::prism && s.min[1] <= here.floor_y ? 1u << 2 : 0u;
    detail::emit_box(e, o, ax[0], ax[1], ax[2], lo, hi, s.material, skip);
  }
  return out;
}

std::vector<Mesh> build_level(const Level& level, size_t workers) {
  std::vector<Mesh> out(level.rooms.size());
  core::parallel_bands(out.size(), workers, [&](size_t begin, size_t end) {
    for (size_t r = begin; r < end; ++r) out[r] = build_room(level, r);
  });
  return out;
}

uint64_t mesh_digest(const Mesh& mesh) {
  uint64_t h = 0xcbf29ce484222325ull;
  auto eat = [&](const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 0x100000001b3ull;
    }
  };
  eat(mesh.pos.data(), mesh.pos.size() * sizeof(float));
  eat(mesh.nrm.data(), mesh.nrm.size() * sizeof(float));
  eat(mesh.mat.data(), mesh.mat.size());
  eat(mesh.idx.data(), mesh.idx.size() * sizeof(uint32_t));
  return h;
}

}  // namespace ge::rooms
