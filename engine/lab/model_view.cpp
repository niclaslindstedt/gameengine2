// The labs' CPU model view: posing a model's meshes into a shape, the cameras, the z-buffered
// raster, the levels of detail and a joint's region.
//
// ge:summary The labs' CPU model view: shapes, cameras, the z-buffered raster, LODs, regions
// ge:keywords lab model-view raster z-buffer camera orbit lod region supersample
// ge:skill labs
// ge:test tests/lab_model_view_test.cpp
#include "lab/model_view.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/libm/libm.hpp"

namespace ge::lab {

namespace {

namespace a = assets;
namespace lm = core::libm;

constexpr double kRad = 3.14159265358979323846 / 180.0;

void sub(const double* x, const double* y, double* out) {
  for (int i = 0; i < 3; ++i) out[i] = x[i] - y[i];
}
double dot(const double* x, const double* y) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; }
void cross(const double* x, const double* y, double* out) {
  out[0] = x[1] * y[2] - x[2] * y[1];
  out[1] = x[2] * y[0] - x[0] * y[2];
  out[2] = x[0] * y[1] - x[1] * y[0];
}
void unit(double* v) {
  const double l = std::sqrt(dot(v, v));
  if (l > 0) {
    for (int i = 0; i < 3; ++i) v[i] /= l;
  }
}

// The camera's frame: right = up x forward (the world is left-handed), up' = forward x right.
struct Basis {
  double eye[3], r[3], u[3], f[3];
  double focal;  // perspective: px a unit of x / z; orthographic: px a metre
  bool ortho;
  double cx, cy;
};

Basis basis_of(const ViewCam& cam, int w, int h) {
  Basis b{};
  for (int i = 0; i < 3; ++i) b.eye[i] = cam.eye[i];
  sub(cam.target, cam.eye, b.f);
  unit(b.f);
  double up[3] = {0, 1, 0};
  if (std::fabs(b.f[1]) > 0.999) up[1] = 0, up[2] = b.f[1] > 0 ? 1 : -1;
  cross(up, b.f, b.r);
  unit(b.r);
  cross(b.f, b.r, b.u);
  b.ortho = cam.fov_deg <= 0;
  b.focal = b.ortho ? (h / 2.0) / std::max(cam.half_m, 1e-6)
                    : (h / 2.0) / lm::tan(cam.fov_deg * kRad / 2);
  b.cx = w / 2.0;
  b.cy = h / 2.0;
  return b;
}

// A point to the panel: x, y in px, `inv` 1 / depth (orthographic: 1 / (depth + a far offset)),
// larger nearer. False behind the eye.
bool project(const Basis& b, const double* p, double* x, double* y, double* inv) {
  double d[3];
  sub(p, b.eye, d);
  const double cx = dot(d, b.r), cy = dot(d, b.u), cz = dot(d, b.f);
  if (b.ortho) {
    *x = b.cx + cx * b.focal;
    *y = b.cy - cy * b.focal;
    *inv = 1.0 / (cz + 1e4);
    return true;
  }
  if (cz < 1e-3) return false;
  *x = b.cx + cx / cz * b.focal;
  *y = b.cy - cy / cz * b.focal;
  *inv = 1.0 / cz;
  return true;
}

// A perspective view's near plane, m in front of the eye: a triangle reaching behind it is cut
// there rather than dropped, so a room's big floor still draws with the eye standing on it.
constexpr double kNear = 0.05;

// The camera-space point (right, up, forward) of `p`.
void to_camera(const Basis& b, const double* p, double* out) {
  double d[3];
  sub(p, b.eye, d);
  out[0] = dot(d, b.r);
  out[1] = dot(d, b.u);
  out[2] = dot(d, b.f);
}

// The panel being drawn, supersampled: its colour and its depth (1 / depth, 0 nothing).
struct Canvas {
  int w, h;
  std::vector<float> rgb;  // linear
  std::vector<double> inv;
};

void put(Canvas& c, int x, int y, double inv, const float* rgb, float alpha) {
  if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
  const size_t i = size_t(y) * size_t(c.w) + size_t(x);
  if (inv < c.inv[i]) return;
  if (alpha >= 1) c.inv[i] = inv;
  for (int k = 0; k < 3; ++k) c.rgb[i * 3 + k] += (rgb[k] - c.rgb[i * 3 + k]) * alpha;
}

void fill(Canvas& c, const double* x, const double* y, const double* inv, const float (*lit)[3]) {
  const int lx = std::max(0, static_cast<int>(std::floor(std::min({x[0], x[1], x[2]}))));
  const int hx = std::min(c.w - 1, static_cast<int>(std::ceil(std::max({x[0], x[1], x[2]}))));
  const int ly = std::max(0, static_cast<int>(std::floor(std::min({y[0], y[1], y[2]}))));
  const int hy = std::min(c.h - 1, static_cast<int>(std::ceil(std::max({y[0], y[1], y[2]}))));
  const double area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
  if (std::fabs(area) < 1e-12) return;
  for (int py = ly; py <= hy; ++py) {
    for (int px = lx; px <= hx; ++px) {
      const double sx = px + 0.5, sy = py + 0.5;
      const double w0 = ((x[1] - sx) * (y[2] - sy) - (x[2] - sx) * (y[1] - sy)) / area;
      const double w1 = ((x[2] - sx) * (y[0] - sy) - (x[0] - sx) * (y[2] - sy)) / area;
      const double w2 = 1 - w0 - w1;
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      const double d = w0 * inv[0] + w1 * inv[1] + w2 * inv[2];
      // Perspective-correct: each vertex's share weighed by its 1 / depth.
      const double p0 = w0 * inv[0] / d, p1 = w1 * inv[1] / d, p2 = w2 * inv[2] / d;
      float rgb[3];
      for (int k = 0; k < 3; ++k) {
        rgb[k] = static_cast<float>(p0 * lit[0][k] + p1 * lit[1][k] + p2 * lit[2][k]);
      }
      put(c, px, py, d, rgb, 1);
    }
  }
}

// A line depth-tested a hair in front of what is drawn, `thick` px wide.
void line(Canvas& c, double xa, double ya, double ia, double xb, double yb, double ib,
          const float* rgb, float alpha, int thick, bool test) {
  const double len = std::max(std::fabs(xb - xa), std::fabs(yb - ya));
  const int steps = std::max(1, static_cast<int>(std::ceil(len)));
  for (int s = 0; s <= steps; ++s) {
    const double t = double(s) / steps;
    const double x = xa + (xb - xa) * t, y = ya + (yb - ya) * t;
    const double inv = (ia + (ib - ia) * t) * (test ? 1.002 : 1e9);
    for (int dy = 0; dy < thick; ++dy) {
      for (int dx = 0; dx < thick; ++dx) {
        const int px = static_cast<int>(std::floor(x)) + dx - thick / 2;
        const int py = static_cast<int>(std::floor(y)) + dy - thick / 2;
        if (px < 0 || py < 0 || px >= c.w || py >= c.h) continue;
        const size_t i = size_t(py) * size_t(c.w) + size_t(px);
        if (inv < c.inv[i]) continue;
        for (int k = 0; k < 3; ++k) c.rgb[i * 3 + k] += (rgb[k] - c.rgb[i * 3 + k]) * alpha;
      }
    }
  }
}

float to_linear(uint8_t v) {
  const float s = v / 255.0f;
  return s * s;
}
uint8_t to_byte(float lin) {
  return static_cast<uint8_t>(
      std::clamp(std::sqrt(std::max(lin, 0.0f)) * 255.0f + 0.5f, 0.0f, 255.0f));
}
void linear_of(Rgba c, float out[3]) {
  out[0] = to_linear(c.r);
  out[1] = to_linear(c.g);
  out[2] = to_linear(c.b);
}

// The LOD a mesh's name says: its base name and level.
void lod_of(const std::string& name, std::string* base, int* level) {
  *base = name;
  *level = 0;
  const size_t at = name.rfind("lod");
  if (at == std::string::npos || at == 0 || at + 3 >= name.size()) return;
  const char sep = name[at - 1];
  if (sep != '.' && sep != '_') return;
  int n = 0;
  for (size_t k = at + 3; k < name.size(); ++k) {
    if (name[k] < '0' || name[k] > '9') return;
    n = n * 10 + (name[k] - '0');
  }
  *base = name.substr(0, at - 1);
  *level = n;
}

}  // namespace

Shape shape_of(const a::Model& m, std::span<const a::Affine> global, std::span<const int> meshes) {
  Shape s;
  s.bones.assign(global.begin(), global.end());
  std::vector<a::Affine> palette(m.joints.size());
  a::skin_palette(m, global, palette);
  auto add = [&](const a::Mesh& mesh) {
    const auto base = static_cast<uint32_t>(s.part.size());
    std::vector<float> pos(size_t{mesh.vertex_count()} * 3), nrm(pos.size());
    a::skin_mesh(mesh, palette, pos, nrm);
    s.pos.insert(s.pos.end(), pos.begin(), pos.end());
    s.nrm.insert(s.nrm.end(), nrm.begin(), nrm.end());
    s.part.insert(s.part.end(), mesh.parts.begin(), mesh.parts.end());
    for (uint32_t v = 0; v < mesh.vertex_count(); ++v) {
      int best = 0;
      for (int k = 1; k < 4; ++k) {
        if (mesh.weights[size_t{v} * 4 + size_t(k)] > mesh.weights[size_t{v} * 4 + size_t(best)]) {
          best = k;
        }
      }
      s.top.push_back(mesh.joints[size_t{v} * 4 + size_t(best)]);
    }
    for (uint32_t i : mesh.indices) s.idx.push_back(base + i);
  };
  const std::vector<int> finest = meshes.empty() ? meshes_at_lod(lod_groups(m), 0)
                                                 : std::vector<int>(meshes.begin(), meshes.end());
  for (int k : finest) {
    if (k >= 0 && static_cast<size_t>(k) < m.meshes.size()) add(m.meshes[static_cast<size_t>(k)]);
  }
  return s;
}

Shape shape_at(const a::Model& m, int clip, double t_s, std::span<const int> meshes) {
  std::vector<a::Trs> local(m.joints.size());
  std::vector<a::Affine> global(m.joints.size());
  if (clip < 0 || static_cast<size_t>(clip) >= m.clips.size()) {
    a::rest_pose(m, local);
  } else {
    a::sample_clip(m, m.clips[static_cast<size_t>(clip)], t_s, false, local);
  }
  a::pose_globals(m, local, global);
  return shape_of(m, global, meshes);
}

a::Box shape_box(const Shape& s) {
  a::Box b;
  for (size_t v = 0; v * 3 < s.pos.size(); ++v) {
    for (int c = 0; c < 3; ++c) {
      const float p = s.pos[v * 3 + static_cast<size_t>(c)];
      b.min[c] = v ? std::min(b.min[c], p) : p;
      b.max[c] = v ? std::max(b.max[c], p) : p;
    }
  }
  return b;
}

ViewCam close_cam(const float centre[3], double radius_m, double az, double el, double fov) {
  ViewCam c;
  c.fov_deg = fov;
  const double dist = radius_m / lm::sin(fov * kRad / 2) * 1.05;
  const double ce = lm::cos(el * kRad);
  const double dir[3] = {lm::sin(az * kRad) * ce, lm::sin(el * kRad), lm::cos(az * kRad) * ce};
  for (int i = 0; i < 3; ++i) {
    c.target[i] = centre[i];
    c.eye[i] = centre[i] + dir[i] * dist;
  }
  return c;
}

ViewCam orbit_cam(const a::Box& box, double az, double el, double fov, double fill, double aspect) {
  float centre[3];
  for (int i = 0; i < 3; ++i) centre[i] = (box.min[i] + box.max[i]) / 2;
  ViewCam c = close_cam(centre, 1, az, el, fov);
  if (fov <= 0) return c;
  // The nearest eye along the same line that keeps all eight corners inside `fill` of the
  // panel: a corner at (x, y) across and up and `along` toward the eye needs the eye at least
  // |x| / tan_x + along away (and the same for y).
  double f[3];
  sub(c.target, c.eye, f);
  unit(f);
  double up[3] = {0, 1, 0};
  if (std::fabs(f[1]) > 0.999) up[1] = 0, up[2] = f[1] > 0 ? 1 : -1;
  double r[3], u[3];
  cross(up, f, r);
  unit(r);
  cross(f, r, u);
  const double ty = lm::tan(fov * kRad / 2) * std::clamp(fill, 0.2, 1.0);
  const double tx = ty * std::max(aspect, 0.05);
  double dist = 0.05;
  for (int k = 0; k < 8; ++k) {
    const double p[3] = {(k & 1 ? box.max[0] : box.min[0]) - centre[0],
                         (k & 2 ? box.max[1] : box.min[1]) - centre[1],
                         (k & 4 ? box.max[2] : box.min[2]) - centre[2]};
    const double along = -dot(p, f);
    dist = std::max(dist, std::fabs(dot(p, r)) / tx + along);
    dist = std::max(dist, std::fabs(dot(p, u)) / ty + along);
  }
  for (int i = 0; i < 3; ++i) c.eye[i] = centre[i] - f[i] * dist;
  return c;
}

ViewCam axis_cam(const a::Box& box, const std::string& side, double fill) {
  ViewCam c;
  c.fov_deg = 0;
  double centre[3], half[3];
  for (int i = 0; i < 3; ++i) {
    centre[i] = (box.min[i] + box.max[i]) / 2.0;
    half[i] = (box.max[i] - box.min[i]) / 2.0;
  }
  double dir[3] = {0, 0, 1};
  double span = std::max(half[0], half[1]);  // across, up
  if (side == "back") {
    dir[2] = -1;
  } else if (side == "right" || side == "left") {
    dir[0] = side == "right" ? 1 : -1;
    dir[2] = 0;
    span = std::max(half[2], half[1]);
  } else if (side == "top") {
    dir[1] = 1;
    dir[2] = 0;
    span = std::max(half[0], half[2]);
  }
  c.half_m = std::max(span, 1e-3) / std::clamp(fill, 0.2, 1.0);
  for (int i = 0; i < 3; ++i) {
    c.target[i] = centre[i];
    c.eye[i] = centre[i] + dir[i] * 50;
  }
  return c;
}

void draw_shape(Picture& pic, int x0, int y0, int w, int h, const ViewCam& cam, const a::Model& m,
                const Shape& s, const ViewStyle& st, double floor_y) {
  const int ss = std::clamp(st.supersample, 1, 4);
  Canvas c{w * ss, h * ss, {}, {}};
  c.rgb.resize(size_t(c.w) * size_t(c.h) * 3);
  c.inv.assign(size_t(c.w) * size_t(c.h), 0.0);
  float back[3];
  linear_of(st.background, back);
  for (size_t i = 0; i < c.inv.size(); ++i) {
    for (int k = 0; k < 3; ++k) c.rgb[i * 3 + size_t(k)] = back[k];
  }
  const Basis b = basis_of(cam, c.w, c.h);

  // The floor's grid: a line a metre, 6 m round the target, under the model.
  if (st.floor) {
    const float grid[3] = {back[0] * 2.2f + 0.01f, back[1] * 2.2f + 0.01f, back[2] * 2.2f + 0.01f};
    const double cx = std::round(cam.target[0]), cz = std::round(cam.target[2]);
    for (int k = -6; k <= 6; ++k) {
      for (int axis = 0; axis < 2; ++axis) {
        double pa[3] = {cx + k, floor_y, cz - 6}, pb[3] = {cx + k, floor_y, cz + 6};
        if (axis) pa[0] = cx - 6, pa[2] = cz + k, pb[0] = cx + 6, pb[2] = cz + k;
        double xa, ya, ia, xb, yb, ib;
        if (project(b, pa, &xa, &ya, &ia) && project(b, pb, &xb, &yb, &ib)) {
          line(c, xa, ya, ia, xb, yb, ib, grid, 1, ss, false);
        }
      }
    }
    // The grid is under everything: it keeps no depth.
    std::fill(c.inv.begin(), c.inv.end(), 0.0);
  }

  // Each vertex lit: a key over the right shoulder from the front, a cool fill, an ambient.
  const double* key = st.key;
  const double fill_l[3] = {-0.6, 0.2, -0.5};
  const size_t n = s.part.size();
  std::vector<double> sx(n), sy(n), si(n);
  std::vector<uint8_t> seen(n);
  std::vector<float> lit(n * 3);
  for (size_t v = 0; v < n; ++v) {
    const double p[3] = {s.pos[v * 3], s.pos[v * 3 + 1], s.pos[v * 3 + 2]};
    seen[v] = project(b, p, &sx[v], &sy[v], &si[v]) ? 1 : 0;
    double nn[3] = {s.nrm[v * 3], s.nrm[v * 3 + 1], s.nrm[v * 3 + 2]};
    unit(nn);
    const double k = std::max(0.0, dot(nn, key)), f = std::max(0.0, dot(nn, fill_l));
    const float* rgb = s.part[v] < m.materials.size() ? m.materials[s.part[v]].rgb : nullptr;
    for (int ch = 0; ch < 3; ++ch) {
      const double base = rgb ? rgb[ch] : 0.6;
      const double sky = ch == 2 ? 0.30 : 0.24;
      lit[v * 3 + size_t(ch)] =
          static_cast<float>(st.baked ? base : base * (0.95 * k + sky * f + 0.22));
    }
  }
  struct Tri {
    uint32_t v[3];
  };
  std::vector<Tri> front, cut;
  for (size_t t = 0; t + 2 < s.idx.size(); t += 3) {
    const uint32_t v[3] = {s.idx[t], s.idx[t + 1], s.idx[t + 2]};
    const bool whole = seen[v[0]] && seen[v[1]] && seen[v[2]];
    if (!whole && (b.ortho || (!seen[v[0]] && !seen[v[1]] && !seen[v[2]]))) continue;
    // Facing: the vertices' normals against the way from the eye.
    double nsum[3] = {0, 0, 0}, mid[3] = {0, 0, 0};
    for (uint32_t k : v) {
      for (int i = 0; i < 3; ++i) {
        nsum[i] += s.nrm[size_t{k} * 3 + size_t(i)];
        mid[i] += s.pos[size_t{k} * 3 + size_t(i)] / 3.0;
      }
    }
    double toward[3];
    if (b.ortho) {
      for (int i = 0; i < 3; ++i) toward[i] = -b.f[i];
    } else {
      sub(b.eye, mid, toward);
    }
    if (dot(nsum, toward) < 0) continue;
    (whole ? front : cut).push_back({{v[0], v[1], v[2]}});
  }
  if (st.shade) {
    for (const Tri& t : front) {
      const double x[3] = {sx[t.v[0]], sx[t.v[1]], sx[t.v[2]]};
      const double y[3] = {sy[t.v[0]], sy[t.v[1]], sy[t.v[2]]};
      const double inv[3] = {si[t.v[0]], si[t.v[1]], si[t.v[2]]};
      float l[3][3];
      for (int k = 0; k < 3; ++k) {
        const size_t at = size_t(t.v[k]) * 3;
        for (int ch = 0; ch < 3; ++ch) l[k][ch] = lit[at + size_t(ch)];
      }
      fill(c, x, y, inv, l);
    }
    // The triangles that reach behind the eye, cut at the near plane: one or two to fill.
    for (const Tri& t : cut) {
      double q[4][3];
      float ql[4][3];
      int nq = 0;
      double cam_p[3][3];
      for (int k = 0; k < 3; ++k) {
        const size_t at = size_t(t.v[k]) * 3;
        const double p[3] = {s.pos[at], s.pos[at + 1], s.pos[at + 2]};
        to_camera(b, p, cam_p[k]);
      }
      for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        const double* a = cam_p[k];
        const double* e = cam_p[j];
        const float* la = &lit[size_t(t.v[k]) * 3];
        const float* le = &lit[size_t(t.v[j]) * 3];
        const bool a_in = a[2] >= kNear, e_in = e[2] >= kNear;
        if (a_in) {
          for (int i = 0; i < 3; ++i) q[nq][i] = a[i], ql[nq][i] = la[i];
          ++nq;
        }
        if (a_in != e_in) {
          const double u = (kNear - a[2]) / (e[2] - a[2]);
          for (int i = 0; i < 3; ++i) {
            q[nq][i] = a[i] + (e[i] - a[i]) * u;
            ql[nq][i] = static_cast<float>(la[i] + (le[i] - la[i]) * u);
          }
          ++nq;
        }
      }
      double px[4], py[4], pi[4];
      for (int k = 0; k < nq; ++k) {
        px[k] = b.cx + q[k][0] / q[k][2] * b.focal;
        py[k] = b.cy - q[k][1] / q[k][2] * b.focal;
        pi[k] = 1.0 / q[k][2];
      }
      for (int k = 1; k + 1 < nq; ++k) {
        const double x[3] = {px[0], px[k], px[k + 1]};
        const double y[3] = {py[0], py[k], py[k + 1]};
        const double inv[3] = {pi[0], pi[k], pi[k + 1]};
        const float l[3][3] = {{ql[0][0], ql[0][1], ql[0][2]},
                               {ql[k][0], ql[k][1], ql[k][2]},
                               {ql[k + 1][0], ql[k + 1][1], ql[k + 1][2]}};
        fill(c, x, y, inv, l);
      }
    }
  }
  if (st.wire) {
    float e[3];
    linear_of(st.edge, e);
    const float alpha = st.edge.a / 255.0f;
    for (const Tri& t : front) {
      for (int k = 0; k < 3; ++k) {
        const uint32_t p = t.v[k], q = t.v[(k + 1) % 3];
        line(c, sx[p], sy[p], si[p], sx[q], sy[q], si[q], e, alpha, 1, st.shade);
      }
    }
  }
  struct Label {
    double x, y;
    const std::string* name;
  };
  std::vector<Label> labels;
  if (st.bones) {
    float bone[3];
    linear_of(st.bone, bone);
    const float alpha = st.bone.a / 255.0f;
    for (size_t j = 0; j < s.bones.size() && j < m.joints.size(); ++j) {
      const float* g = s.bones[j].m;
      const double at[3] = {g[3], g[7], g[11]};
      double x, y, inv;
      if (!project(b, at, &x, &y, &inv)) continue;
      const int32_t parent = m.joints[j].parent;
      if (parent >= 0) {
        const float* q = s.bones[static_cast<size_t>(parent)].m;
        const double pa[3] = {q[3], q[7], q[11]};
        double px, py, pi;
        if (project(b, pa, &px, &py, &pi))
          line(c, px, py, pi, x, y, inv, bone, alpha, 2 * ss, false);
      }
      for (int dy = -2 * ss; dy <= 2 * ss; ++dy) {
        for (int dx = -2 * ss; dx <= 2 * ss; ++dx) {
          if (dx * dx + dy * dy <= 4 * ss * ss) {
            put(c, static_cast<int>(x) + dx, static_cast<int>(y) + dy, 1e9, bone, alpha);
          }
        }
      }
      if (st.labels) labels.push_back({x / ss, y / ss, &m.joints[j].name});
    }
  }

  // Down to the panel: each pixel the mean of its ss x ss.
  for (int py = 0; py < h; ++py) {
    for (int px = 0; px < w; ++px) {
      float sum[3] = {0, 0, 0};
      for (int dy = 0; dy < ss; ++dy) {
        for (int dx = 0; dx < ss; ++dx) {
          const size_t i = size_t(py * ss + dy) * size_t(c.w) + size_t(px * ss + dx);
          for (int k = 0; k < 3; ++k) sum[k] += c.rgb[i * 3 + size_t(k)];
        }
      }
      const float d = float(ss * ss);
      set_pixel(pic, x0 + px, y0 + py,
                {to_byte(sum[0] / d), to_byte(sum[1] / d), to_byte(sum[2] / d)});
    }
  }
  for (const Label& l : labels) {
    draw_text(pic, x0 + l.x + 5, y0 + l.y - 3, *l.name, {220, 224, 232});
  }
}

LodGroups lod_groups(const a::Model& m) {
  LodGroups g;
  for (size_t k = 0; k < m.meshes.size(); ++k) {
    std::string base;
    int level = 0;
    lod_of(m.meshes[k].name, &base, &level);
    size_t at = 0;
    while (at < g.names.size() && g.names[at] != base) ++at;
    if (at == g.names.size()) {
      g.names.push_back(base);
      g.levels.emplace_back();
    }
    std::vector<int>& lv = g.levels[at];
    if (lv.size() <= static_cast<size_t>(level)) lv.resize(static_cast<size_t>(level) + 1, -1);
    lv[static_cast<size_t>(level)] = static_cast<int>(k);
    g.most = std::max(g.most, static_cast<int>(lv.size()));
  }
  return g;
}

std::vector<int> meshes_at_lod(const LodGroups& lods, int n) {
  std::vector<int> out;
  for (const std::vector<int>& lv : lods.levels) {
    int pick = -1;
    for (int k = std::min(n, static_cast<int>(lv.size()) - 1); k >= 0 && pick < 0; --k) {
      pick = lv[static_cast<size_t>(k)];
    }
    if (pick >= 0) out.push_back(pick);
  }
  return out;
}

bool region_of(const a::Model& m, const Shape& s, int joint, int depth, float centre[3],
               float* radius) {
  if (joint < 0 || static_cast<size_t>(joint) >= m.joints.size()) return false;
  // Whether joint j is `joint` or a descendant within `depth` generations.
  auto under = [&](int j) {
    for (int g = 0; g <= depth && j >= 0; ++g) {
      if (j == joint) return true;
      j = m.joints[static_cast<size_t>(j)].parent;
    }
    return false;
  };
  double sum[3] = {0, 0, 0};
  std::vector<size_t> picked;
  for (size_t v = 0; v < s.top.size(); ++v) {
    if (!under(s.top[v])) continue;
    for (int i = 0; i < 3; ++i) sum[i] += s.pos[v * 3 + size_t(i)];
    picked.push_back(v);
  }
  if (picked.empty()) return false;
  for (int i = 0; i < 3; ++i) centre[i] = static_cast<float>(sum[i] / double(picked.size()));
  double r = 0;
  for (size_t v : picked) {
    double d2 = 0;
    for (int i = 0; i < 3; ++i) {
      const double d = s.pos[v * 3 + size_t(i)] - centre[i];
      d2 += d * d;
    }
    r = std::max(r, d2);
  }
  *radius = static_cast<float>(std::sqrt(r));
  return true;
}

}  // namespace ge::lab
