// The brush builder (brush_build.hpp): polygons from planes, the CSG pass over a grid of the
// brushes' bounds, the fan into triangles.
//
// ge:summary Brush polygons from planes, CSG against neighbouring brushes, triangles and digest
// ge:keywords brush csg polygon clip sutherland-hodgman grid bounds fan triangles digest
// ge:skill rooms
// ge:test tests/rooms_brush_test.cpp
#include "rooms/brush_build.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/parallel.hpp"

namespace ge::rooms {

namespace {

constexpr double kOnM = 1e-4;  // a point this near a plane is on it
constexpr double kCellM = 4;   // the bounds grid's cell
constexpr double kSliverM2 = 1e-8;
constexpr double kSmoothCos = 0.6;  // smooth neighbours under about 53 degrees apart blend

struct Plane {
  double n[3], d;
};

Plane plane_of(const Face& f) {
  return {{double(f.n[0]), double(f.n[1]), double(f.n[2])}, double(f.d)};
}

double side(const Plane& p, const double* x) {
  return p.n[0] * x[0] + p.n[1] * x[1] + p.n[2] * x[2] - p.d;
}

// `poly` cut by `p`: the part behind it (side <= kOnM) into `back`, the part in front into
// `front`. A point on the plane goes to both.
void split(const std::vector<double>& poly, const Plane& p, std::vector<double>* back,
           std::vector<double>* front) {
  const size_t n = poly.size() / 3;
  if (back) back->clear();
  if (front) front->clear();
  for (size_t i = 0; i < n; ++i) {
    const double* a = &poly[i * 3];
    const double* b = &poly[((i + 1) % n) * 3];
    const double sa = side(p, a), sb = side(p, b);
    const bool a_back = sa <= kOnM, a_front = sa >= -kOnM;
    if (a_back && back) back->insert(back->end(), a, a + 3);
    if (a_front && front) front->insert(front->end(), a, a + 3);
    if ((sa < -kOnM && sb > kOnM) || (sa > kOnM && sb < -kOnM)) {
      const double t = sa / (sa - sb);
      const double m[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
                           a[2] + (b[2] - a[2]) * t};
      if (back) back->insert(back->end(), m, m + 3);
      if (front) front->insert(front->end(), m, m + 3);
    }
  }
  if (back && back->size() < 9) back->clear();
  if (front && front->size() < 9) front->clear();
}

double area2(const std::vector<double>& poly) {
  double c[3] = {0, 0, 0};
  const size_t n = poly.size() / 3;
  for (size_t i = 1; i + 1 < n; ++i) {
    const double* a = &poly[0];
    const double* b = &poly[i * 3];
    const double* d = &poly[(i + 1) * 3];
    const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const double v[3] = {d[0] - a[0], d[1] - a[1], d[2] - a[2]};
    c[0] += u[1] * v[2] - u[2] * v[1];
    c[1] += u[2] * v[0] - u[0] * v[2];
    c[2] += u[0] * v[1] - u[1] * v[0];
  }
  return c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
}

struct Bounds {
  double lo[3], hi[3];
  bool ok = false;
};

bool removes(Contents by, Contents of) {
  if (by == Contents::solid || by == Contents::sky) return true;
  const bool liquid = by == Contents::water || by == Contents::slime || by == Contents::lava;
  return liquid && by == of;
}

// The smooth faces' corners shaded round: each takes the mean of the distinct face normals met at
// its place (to a millimetre) within kSmoothCos of its own, so a faceted arch's steps blend and a
// sharp corner stays sharp.
void smooth_normals(BrushMesh& m, const std::vector<uint32_t>& smooth) {
  struct At {
    int64_t q[3];
    uint32_t v;
  };
  std::vector<At> at;
  at.reserve(smooth.size());
  for (const uint32_t v : smooth) {
    At a{};
    for (size_t k = 0; k < 3; ++k) a.q[k] = std::llround(double(m.pos[size_t(v) * 3 + k]) * 1024.0);
    a.v = v;
    at.push_back(a);
  }
  const auto key_less = [](const At& a, const At& b) {
    return std::lexicographical_compare(a.q, a.q + 3, b.q, b.q + 3);
  };
  std::sort(at.begin(), at.end(), [&](const At& a, const At& b) {
    return key_less(a, b) || (!key_less(b, a) && a.v < b.v);
  });
  std::vector<float> out(m.nrm);
  std::vector<std::array<float, 3>> seen;
  for (size_t i = 0; i < at.size();) {
    size_t j = i;
    while (j < at.size() && !key_less(at[i], at[j])) ++j;
    seen.clear();
    for (size_t k = i; k < j; ++k) {
      const float* n = &m.nrm[size_t(at[k].v) * 3];
      const std::array<float, 3> nn{n[0], n[1], n[2]};
      if (std::find(seen.begin(), seen.end(), nn) == seen.end()) seen.push_back(nn);
    }
    for (size_t k = i; k < j; ++k) {
      const float* own = &m.nrm[size_t(at[k].v) * 3];
      double sum[3] = {0, 0, 0};
      for (const auto& n : seen) {
        const double dot = double(own[0]) * n[0] + double(own[1]) * n[1] + double(own[2]) * n[2];
        if (dot < kSmoothCos) continue;
        for (size_t c = 0; c < 3; ++c) sum[c] += n[c];
      }
      const double len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
      for (size_t c = 0; c < 3; ++c) out[size_t(at[k].v) * 3 + c] = float(sum[c] / len);
    }
    i = j;
  }
  m.nrm.swap(out);
}

}  // namespace

bool cell_polygons(std::span<const HalfSpace> planes, std::vector<std::vector<double>>* out) {
  out->assign(planes.size(), {});
  std::vector<double> poly, next;
  for (size_t f = 0; f < planes.size(); ++f) {
    const Plane p{{planes[f].n[0], planes[f].n[1], planes[f].n[2]}, planes[f].d};
    // A square on the plane, counter-clockwise seen from the normal's side: u × v = n.
    const double ax = std::fabs(p.n[0]), ay = std::fabs(p.n[1]), az = std::fabs(p.n[2]);
    double a[3] = {0, 0, 0};
    a[ax <= ay && ax <= az ? 0 : (ay <= az ? 1 : 2)] = 1;
    double u[3] = {a[1] * p.n[2] - a[2] * p.n[1], a[2] * p.n[0] - a[0] * p.n[2],
                   a[0] * p.n[1] - a[1] * p.n[0]};
    const double ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (double& x : u) x /= ul;
    const double v[3] = {p.n[1] * u[2] - p.n[2] * u[1], p.n[2] * u[0] - p.n[0] * u[2],
                         p.n[0] * u[1] - p.n[1] * u[0]};
    const double c[3] = {p.n[0] * p.d, p.n[1] * p.d, p.n[2] * p.d};
    poly.clear();
    for (const auto& [su, sv] : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) {
      for (int k = 0; k < 3; ++k) poly.push_back(c[k] + (su * u[k] + sv * v[k]) * kReachM);
    }
    for (size_t g = 0; g < planes.size() && !poly.empty(); ++g) {
      if (g == f) continue;
      const Plane q{{planes[g].n[0], planes[g].n[1], planes[g].n[2]}, planes[g].d};
      split(poly, q, &next, nullptr);
      poly.swap(next);
    }
    if (poly.empty() || area2(poly) < kSliverM2) continue;
    for (double x : poly) {
      if (std::fabs(x) > kReachM / 2) {
        out->clear();
        return false;  // the planes do not close
      }
    }
    (*out)[f] = poly;
  }
  return true;
}

std::vector<Polygon> brush_polygons(const Level& level, size_t b) {
  const Brush& br = level.brushes[b];
  std::vector<HalfSpace> planes;
  for (uint32_t f = br.first_face; f < br.first_face + br.faces; ++f) {
    const Face& face = level.faces[f];
    planes.push_back({{double(face.n[0]), double(face.n[1]), double(face.n[2])}, double(face.d)});
  }
  std::vector<std::vector<double>> polys;
  std::vector<Polygon> out;
  if (!cell_polygons(planes, &polys)) return out;
  for (size_t i = 0; i < polys.size(); ++i) {
    if (!polys[i].empty()) out.push_back(Polygon{std::move(polys[i]), br.first_face + uint32_t(i)});
  }
  return out;
}

BrushMesh build_brushes(const Level& level, size_t workers) {
  const size_t nb = level.brushes.size();
  std::vector<std::vector<Polygon>> own(nb);
  std::vector<Bounds> box(nb);
  core::parallel_bands(nb, workers, [&](size_t begin, size_t end) {
    for (size_t b = begin; b < end; ++b) {
      own[b] = brush_polygons(level, b);
      Bounds& x = box[b];
      for (int k = 0; k < 3; ++k) x.lo[k] = 1e300, x.hi[k] = -1e300;
      for (const Polygon& p : own[b]) {
        for (size_t i = 0; i < p.xyz.size(); ++i) {
          x.lo[i % 3] = std::min(x.lo[i % 3], p.xyz[i]);
          x.hi[i % 3] = std::max(x.hi[i % 3], p.xyz[i]);
        }
      }
      x.ok = !own[b].empty();
    }
  });

  // The bounds on a grid of cells, each cell listing the brushes that reach it.
  double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
  for (const Bounds& x : box) {
    if (!x.ok) continue;
    for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], x.lo[k]), hi[k] = std::max(hi[k], x.hi[k]);
  }
  int64_t dim[3] = {1, 1, 1};
  for (int k = 0; k < 3; ++k) {
    if (hi[k] >= lo[k]) dim[k] = std::clamp<int64_t>(int64_t((hi[k] - lo[k]) / kCellM) + 1, 1, 512);
  }
  const auto cell_of = [&](double x, int k) {
    return std::clamp<int64_t>(int64_t((x - lo[k]) / (hi[k] - lo[k] + 1e-9) * double(dim[k])), 0,
                               dim[k] - 1);
  };
  std::vector<std::vector<uint32_t>> cells(size_t(dim[0] * dim[1] * dim[2]));
  for (size_t b = 0; b < nb; ++b) {
    if (!box[b].ok) continue;
    int64_t c0[3], c1[3];
    for (int k = 0; k < 3; ++k) {
      c0[k] = cell_of(box[b].lo[k] - kOnM, k), c1[k] = cell_of(box[b].hi[k] + kOnM, k);
    }
    for (int64_t z = c0[2]; z <= c1[2]; ++z)
      for (int64_t y = c0[1]; y <= c1[1]; ++y)
        for (int64_t x = c0[0]; x <= c1[0]; ++x) {
          cells[size_t((z * dim[1] + y) * dim[0] + x)].push_back(uint32_t(b));
        }
  }

  // Each brush's faces cut by its neighbours, into its own list of kept polygons.
  std::vector<std::vector<Polygon>> kept(nb);
  core::parallel_bands(nb, workers, [&](size_t begin, size_t end) {
    std::vector<uint32_t> near;
    std::vector<double> back, front;
    for (size_t a = begin; a < end; ++a) {
      if (!box[a].ok) continue;
      near.clear();
      int64_t c0[3], c1[3];
      for (int k = 0; k < 3; ++k) {
        c0[k] = cell_of(box[a].lo[k] - kOnM, k), c1[k] = cell_of(box[a].hi[k] + kOnM, k);
      }
      for (int64_t z = c0[2]; z <= c1[2]; ++z)
        for (int64_t y = c0[1]; y <= c1[1]; ++y)
          for (int64_t x = c0[0]; x <= c1[0]; ++x) {
            const auto& list = cells[size_t((z * dim[1] + y) * dim[0] + x)];
            near.insert(near.end(), list.begin(), list.end());
          }
      std::sort(near.begin(), near.end());
      near.erase(std::unique(near.begin(), near.end()), near.end());
      const Contents ca = level.brushes[a].contents;
      for (const Polygon& start : own[a]) {
        std::vector<std::vector<double>> pieces{start.xyz};
        const Plane fp = plane_of(level.faces[start.face]);
        for (const uint32_t b : near) {
          if (b == a || !removes(level.brushes[b].contents, ca)) continue;
          bool overlap = true;
          for (int k = 0; k < 3; ++k) {
            overlap = overlap && box[b].lo[k] <= box[a].hi[k] + kOnM &&
                      box[a].lo[k] <= box[b].hi[k] + kOnM;
          }
          if (!overlap) continue;
          const Brush& bb = level.brushes[b];
          std::vector<std::vector<double>> out;
          for (std::vector<double>& piece : pieces) {
            std::vector<double> inside = piece;
            bool outside = false;
            for (uint32_t g = bb.first_face; g < bb.first_face + bb.faces && !inside.empty(); ++g) {
              const Plane q = plane_of(level.faces[g]);
              const double dot = q.n[0] * fp.n[0] + q.n[1] * fp.n[1] + q.n[2] * fp.n[2];
              if (dot > 1 - 1e-6 && std::fabs(q.d - fp.d) <= kOnM && b > a) {
                // On one plane facing the same way: the earlier brush's face is kept.
                out.push_back(std::move(inside));
                inside.clear();
                outside = true;
                break;
              }
              split(inside, q, &back, &front);
              // What lies wholly on the plane is behind it (inside the touching brush).
              if (!front.empty() && area2(front) >= kSliverM2) {
                bool all_on = true;
                for (size_t i = 0; i < front.size(); i += 3) {
                  all_on = all_on && std::fabs(side(q, &front[i])) <= kOnM;
                }
                if (!all_on) out.push_back(front);
              }
              inside.swap(back);
            }
            (void)outside;
          }
          pieces.swap(out);
          if (pieces.empty()) break;
        }
        for (std::vector<double>& p : pieces) {
          if (area2(p) >= kSliverM2) kept[a].push_back(Polygon{std::move(p), start.face});
        }
      }
    }
  });

  BrushMesh m;
  std::vector<uint32_t> smooth;
  for (size_t b = 0; b < nb; ++b) {
    if (!box[b].ok && level.brushes[b].faces > 0) ++m.unbounded;
    const Brush& br = level.brushes[b];
    for (const Polygon& p : kept[b]) {
      const Face& f = level.faces[p.face];
      const Projection& pr = level.projections[f.projection];
      const auto base = uint32_t(m.pos.size() / 3);
      const size_t n = p.xyz.size() / 3;
      for (size_t i = 0; i < n; ++i) {
        const double* x = &p.xyz[i * 3];
        for (int k = 0; k < 3; ++k) {
          m.pos.push_back(float(x[k]));
          m.nrm.push_back(f.n[k]);
        }
        m.st.push_back(float(pr.s[0] * x[0] + pr.s[1] * x[1] + pr.s[2] * x[2] + pr.s[3]));
        m.st.push_back(float(pr.t[0] * x[0] + pr.t[1] * x[1] + pr.t[2] * x[2] + pr.t[3]));
      }
      for (size_t i = 1; i + 1 < n; ++i) {
        m.idx.insert(m.idx.end(), {base, base + uint32_t(i), base + uint32_t(i + 1)});
        m.material.push_back(f.material);
        m.contents.push_back(br.contents);
        m.hidden.push_back(
            f.hidden || br.contents == Contents::clip || br.contents == Contents::trigger ? 1 : 0);
        m.brush.push_back(uint32_t(b));
      }
      if (f.smooth) {
        for (size_t i = 0; i < n; ++i) smooth.push_back(base + uint32_t(i));
      }
    }
  }
  smooth_normals(m, smooth);
  return m;
}

uint64_t brush_mesh_digest(const BrushMesh& mesh) {
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
  eat(mesh.st.data(), mesh.st.size() * sizeof(float));
  eat(mesh.idx.data(), mesh.idx.size() * sizeof(uint32_t));
  eat(mesh.material.data(), mesh.material.size());
  eat(mesh.contents.data(), mesh.contents.size());
  eat(mesh.hidden.data(), mesh.hidden.size());
  return h;
}

}  // namespace ge::rooms
