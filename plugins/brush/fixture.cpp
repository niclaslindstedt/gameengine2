// The fixture compile: the sample fixture, every room's inside faces less their doorways, the
// axis-aligned tree, the two-doorways-deep visibility, the luxels, the run-length rows, the page
// packer, and `write_bsp` handing the compile to its family's writer.
//
// ge:summary Compiles a fixture of rooms into faces, tree, visibility and luxels for the writers
// ge:keywords fixture compile sample rooms doorway faces subtract tree split visibility luxel light
// ge:keywords rle packer entities write_bsp
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

#include "brush/write_parts.hpp"

namespace ge::brush {

Fixture sample_fixture() {
  Fixture f;
  f.message = "The fixture halls";
  const auto room = [&](const char* name, std::array<int32_t, 6> b, const char* floor,
                        const char* wall, const char* ceiling) {
    FixtureRoom r;
    r.name = name;
    for (int k = 0; k < 3; ++k) r.min[k] = b[size_t(k)], r.max[k] = b[size_t(k) + 3];
    r.floor = floor;
    r.wall = wall;
    r.ceiling = ceiling;
    f.rooms.push_back(r);
  };
  room("hall", {0, 0, 0, 512, 384, 256}, "flagstone", "brick", "plaster");
  room("east door", {512, 160, 0, 544, 224, 128}, "flagstone", "metal", "metal");
  room("side room", {544, 64, 0, 864, 448, 192}, "tile", "panel", "plaster");
  room("north door", {192, 384, 0, 256, 448, 128}, "flagstone", "metal", "metal");
  room("corridor", {128, 448, 0, 320, 736, 160}, "concrete", "concrete", "concrete");
  room("vault door", {320, 640, 0, 352, 704, 128}, "flagstone", "metal", "metal");
  room("vault", {352, 544, 0, 736, 800, 320}, "flagstone", "stone", "plaster");
  f.vault_room = 6;
  f.lights = {
      FixtureLight{{256, 192, 200}, 420, 230},
      FixtureLight{{760, 256, 150}, 340, 210},
      FixtureLight{{224, 600, 128}, 240, 150},
      FixtureLight{{544, 672, 150}, 400, 220},
  };
  return f;
}

namespace detail {

namespace {

// The two in-plane axes of a face whose normal is along `axis`.
constexpr int kU[3] = {1, 0, 0}, kV[3] = {2, 2, 1};

struct Rect {
  double u0, v0, u1, v1;
};

// `r` less `cut`: up to four rectangles.
void subtract(const Rect& r, const Rect& cut, std::vector<Rect>* out) {
  const double u0 = std::max(r.u0, cut.u0), u1 = std::min(r.u1, cut.u1);
  const double v0 = std::max(r.v0, cut.v0), v1 = std::min(r.v1, cut.v1);
  if (u0 >= u1 || v0 >= v1) {
    out->push_back(r);
    return;
  }
  if (r.v0 < v0) out->push_back({r.u0, r.v0, r.u1, v0});
  if (v1 < r.v1) out->push_back({r.u0, v1, r.u1, r.v1});
  if (r.u0 < u0) out->push_back({r.u0, v0, u0, v1});
  if (u1 < r.u1) out->push_back({u1, v0, r.u1, v1});
}

// Whether rooms a and b touch across a face of positive area.
bool touch(const FixtureRoom& a, const FixtureRoom& b) {
  for (int ax = 0; ax < 3; ++ax) {
    if (a.max[ax] != b.min[ax] && b.max[ax] != a.min[ax]) continue;
    const int u = kU[ax], v = kV[ax];
    if (std::min(a.max[u], b.max[u]) > std::max(a.min[u], b.min[u]) &&
        std::min(a.max[v], b.max[v]) > std::max(a.min[v], b.min[v])) {
      return true;
    }
  }
  return false;
}

// The texture projection for a face along `axis`: s along +u, t down (or along −y on a floor).
void texture_vecs(int axis, float vecs[2][4]) {
  for (int a = 0; a < 2; ++a) {
    for (int k = 0; k < 4; ++k) vecs[a][k] = 0;
  }
  vecs[0][kU[axis]] = 1;
  vecs[1][kV[axis]] = -1;
}

// The point at texture coordinates (s, t) on a face along `axis` at `coord`.
void point_at(int axis, double coord, double s, double t, double p[3]) {
  p[axis] = coord;
  p[kU[axis]] = s;
  p[kV[axis]] = -t;
}

}  // namespace

int room_of(const Fixture& fx, const float p[3]) {
  for (size_t r = 0; r < fx.rooms.size(); ++r) {
    const FixtureRoom& rm = fx.rooms[r];
    bool in = true;
    for (int k = 0; k < 3; ++k) in = in && p[k] >= float(rm.min[k]) && p[k] <= float(rm.max[k]);
    if (in) return int(r);
  }
  return -1;
}

uint8_t light_at(const Fixture& fx, uint32_t room, const double p[3], const float n[3]) {
  double b = fx.ambient;
  for (const FixtureLight& l : fx.lights) {
    if (room_of(fx, l.pos) != int(room)) continue;
    const double d3[3] = {l.pos[0] - p[0], l.pos[1] - p[1], l.pos[2] - p[2]};
    const double d = std::sqrt(d3[0] * d3[0] + d3[1] * d3[1] + d3[2] * d3[2]);
    if (d >= l.radius) continue;
    const double cosine = d > 0 ? (n[0] * d3[0] + n[1] * d3[1] + n[2] * d3[2]) / d : 1;
    b += l.bright * (1 - d / l.radius) * (0.3 + 0.7 * std::max(0.0, cosine));
  }
  return uint8_t(std::min(255.0, b));
}

std::vector<uint8_t> rle_row(const std::vector<uint8_t>& row) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < row.size();) {
    if (row[i] != 0) {
      out.push_back(row[i++]);
      continue;
    }
    uint8_t run = 0;
    while (i < row.size() && row[i] == 0 && run < 255) ++run, ++i;
    out.push_back(0);
    out.push_back(run);
  }
  return out;
}

std::vector<uint8_t> vis_row(const Compiled& c, uint32_t room) {
  std::vector<uint8_t> row((c.sees.size() + 7) / 8, 0);
  for (size_t q = 0; q < c.sees.size(); ++q) {
    if (c.sees[room][q]) row[q / 8] |= uint8_t(1u << (q % 8));
  }
  return row;
}

bool Packer::place(uint32_t w, uint32_t h, uint32_t* p, uint32_t* px, uint32_t* py) {
  if (w > 128 || h > 128) return false;
  if (x + w > 128) x = 0, y += shelf, shelf = 0;
  if (y + h > 128) ++page, x = 0, y = 0, shelf = 0;
  *p = page;
  *px = x;
  *py = y;
  x += w;
  shelf = std::max(shelf, h);
  return true;
}

bool compile(const Fixture& fx, Compiled* out, std::string* err) {
  Compiled& c = *out;
  c = Compiled{};
  const size_t n = fx.rooms.size();
  if (n == 0) {
    if (err) *err = "fixture: no rooms; next: add at least one room";
    return false;
  }
  for (int k = 0; k < 3; ++k) c.mins[k] = 1e30f, c.maxs[k] = -1e30f;
  for (const FixtureRoom& r : fx.rooms) {
    for (int k = 0; k < 3; ++k) {
      if (r.min[k] >= r.max[k]) {
        if (err) *err = "fixture: room " + r.name + " is empty along an axis; next: fix its box";
        return false;
      }
      c.mins[k] = std::min(c.mins[k], float(r.min[k]));
      c.maxs[k] = std::max(c.maxs[k], float(r.max[k]));
    }
  }

  std::map<std::string, uint32_t> tex;
  const auto texture = [&](const std::string& name) {
    const auto [it, added] = tex.emplace(name, uint32_t(c.textures.size()));
    if (added) c.textures.push_back(name);
    return it->second;
  };
  std::map<std::pair<int, int32_t>, uint32_t> planes;
  const auto plane = [&](int axis, int32_t d) {
    const auto [it, added] = planes.emplace(std::make_pair(axis, d), uint32_t(c.planes.size()));
    if (added) c.planes.push_back(CPlane{axis, float(d)});
    return it->second;
  };

  // Faces: every side of every room, less where a neighbour touches it.
  c.room_faces.resize(n);
  for (uint32_t r = 0; r < n; ++r) {
    const FixtureRoom& rm = fx.rooms[r];
    for (int axis = 0; axis < 3; ++axis) {
      for (int hi = 0; hi < 2; ++hi) {
        const int32_t coord = hi ? rm.max[axis] : rm.min[axis];
        const int u = kU[axis], v = kV[axis];
        std::vector<Rect> rects = {
            {double(rm.min[u]), double(rm.min[v]), double(rm.max[u]), double(rm.max[v])}};
        for (uint32_t q = 0; q < n; ++q) {
          const FixtureRoom& o = fx.rooms[q];
          if (q == r || (hi ? o.min[axis] : o.max[axis]) != coord) continue;
          const Rect cut{double(o.min[u]), double(o.min[v]), double(o.max[u]), double(o.max[v])};
          std::vector<Rect> next;
          for (const Rect& x : rects) subtract(x, cut, &next);
          rects = std::move(next);
        }
        const std::string& name = axis == 2 ? (hi ? rm.ceiling : rm.floor) : rm.wall;
        for (const Rect& x : rects) {
          CFace f;
          f.room = r;
          f.plane = plane(axis, coord);
          f.back = hi != 0;  // a room's high side looks back down the axis
          f.normal[axis] = hi ? -1.0f : 1.0f;
          f.texture = texture(name);
          // (u0,v0) → (u1,v0) → (u1,v1) → (u0,v1) turns about +u × +v: +x, −y or +z by axis.
          const int about = axis == 1 ? -1 : 1;
          const bool ccw_about_normal = about == (hi ? -1 : 1);
          const double corners[4][2] = {{x.u0, x.v0}, {x.u1, x.v0}, {x.u1, x.v1}, {x.u0, x.v1}};
          for (int k = 0; k < 4; ++k) {
            const auto* uv = corners[ccw_about_normal ? 3 - k : k];
            std::array<float, 3> p{};
            p[size_t(axis)] = float(coord);
            p[size_t(u)] = float(uv[0]);
            p[size_t(v)] = float(uv[1]);
            f.poly.push_back(p);
          }
          texture_vecs(axis, f.vecs);
          double lo[2] = {1e30, 1e30}, hi2[2] = {-1e30, -1e30};
          for (const auto& p : f.poly) {
            for (int a = 0; a < 2; ++a) {
              const double s = double(p[0]) * f.vecs[a][0] + double(p[1]) * f.vecs[a][1] +
                               double(p[2]) * f.vecs[a][2] + f.vecs[a][3];
              lo[a] = std::min(lo[a], s);
              hi2[a] = std::max(hi2[a], s);
            }
          }
          for (int a = 0; a < 2; ++a) {
            f.base[a] = int64_t(std::floor(lo[a] / 16.0));
            f.size[a] = int64_t(std::ceil(hi2[a] / 16.0)) - f.base[a] + 1;
          }
          for (int64_t j = 0; j < f.size[1]; ++j) {
            for (int64_t i = 0; i < f.size[0]; ++i) {
              double p[3];
              point_at(axis, coord, double((f.base[0] + i) * 16), double((f.base[1] + j) * 16), p);
              for (int k = 0; k < 3; ++k) {
                p[k] = std::clamp(p[k], double(rm.min[k]), double(rm.max[k]));
              }
              f.luxels.push_back(light_at(fx, r, p, f.normal));
            }
          }
          c.room_faces[r].push_back(uint32_t(c.faces.size()));
          c.faces.push_back(std::move(f));
        }
      }
    }
  }

  // The tree: splits between rooms, then each room's own sides.
  struct Region {
    double lo[3], hi[3];
  };
  bool ok = true;
  std::string why;
  std::function<int32_t(std::vector<uint32_t>, Region)> build = [&](std::vector<uint32_t> set,
                                                                    Region reg) -> int32_t {
    if (!ok) return -1;
    if (set.empty()) return -1;  // leaf 0, solid
    const auto node = [&](int axis, int32_t d) {
      CNode nd;
      nd.plane = plane(axis, d);
      for (int k = 0; k < 3; ++k) nd.mins[k] = float(reg.lo[k]), nd.maxs[k] = float(reg.hi[k]);
      c.nodes.push_back(nd);
      return int32_t(c.nodes.size() - 1);
    };
    if (set.size() == 1) {
      const FixtureRoom& rm = fx.rooms[set[0]];
      for (int axis = 0; axis < 3; ++axis) {
        if (reg.lo[axis] < rm.min[axis]) {
          const int32_t at = node(axis, rm.min[axis]);
          Region in = reg;
          in.lo[axis] = rm.min[axis];
          const int32_t front = build(set, in);
          c.nodes[size_t(at)].child[0] = front;
          c.nodes[size_t(at)].child[1] = -1;
          return at;
        }
        if (reg.hi[axis] > rm.max[axis]) {
          const int32_t at = node(axis, rm.max[axis]);
          Region in = reg;
          in.hi[axis] = rm.max[axis];
          const int32_t back = build(set, in);
          c.nodes[size_t(at)].child[0] = -1;
          c.nodes[size_t(at)].child[1] = back;
          return at;
        }
      }
      return -int32_t(set[0] + 2);  // leaf room + 1
    }
    for (int axis = 0; axis < 3; ++axis) {
      for (uint32_t r : set) {
        for (int32_t d : {fx.rooms[r].min[axis], fx.rooms[r].max[axis]}) {
          if (d <= reg.lo[axis] || d >= reg.hi[axis]) continue;
          std::vector<uint32_t> below, above;
          bool clean = true;
          for (uint32_t q : set) {
            if (fx.rooms[q].max[axis] <= d) {
              below.push_back(q);
            } else if (fx.rooms[q].min[axis] >= d) {
              above.push_back(q);
            } else {
              clean = false;
            }
          }
          if (!clean || below.empty() || above.empty()) continue;
          const int32_t at = node(axis, d);
          Region a = reg, b = reg;
          a.lo[axis] = d;
          b.hi[axis] = d;
          const int32_t front = build(above, a);
          const int32_t back = build(below, b);
          c.nodes[size_t(at)].child[0] = front;
          c.nodes[size_t(at)].child[1] = back;
          return at;
        }
      }
    }
    ok = false;
    why = "no axis plane separates rooms";
    for (uint32_t r : set) why += " '" + fx.rooms[r].name + "'";
    return -1;
  };
  Region all;
  for (int k = 0; k < 3; ++k) all.lo[k] = c.mins[k] - 64.0, all.hi[k] = c.maxs[k] + 64.0;
  std::vector<uint32_t> every(n);
  for (uint32_t r = 0; r < n; ++r) every[r] = r;
  build(every, all);
  if (!ok) {
    if (err) *err = "fixture: " + why + "; next: lay the rooms out so a plane splits them";
    return false;
  }

  // Visibility: itself, its neighbours and theirs.
  c.sees.assign(n, std::vector<uint8_t>(n, 0));
  std::vector<std::vector<uint8_t>> near(n, std::vector<uint8_t>(n, 0));
  for (size_t a = 0; a < n; ++a) {
    near[a][a] = 1;
    for (size_t b = 0; b < n; ++b) {
      if (a != b && touch(fx.rooms[a], fx.rooms[b])) near[a][b] = 1;
    }
  }
  for (size_t a = 0; a < n; ++a) {
    for (size_t m = 0; m < n; ++m) {
      if (!near[a][m]) continue;
      for (size_t b = 0; b < n; ++b) {
        if (near[m][b]) c.sees[a][b] = c.sees[b][a] = 1;
      }
    }
  }

  // The entities: the world, a start in the first room, the lights.
  const auto num = [](double v) { return std::to_string(int64_t(std::floor(v + 0.5))); };
  const FixtureRoom& first = fx.rooms[0];
  c.entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"" + fx.message + "\"\n}\n";
  c.entities += "{\n\"classname\" \"info_player_start\"\n\"origin\" \"" +
                num((first.min[0] + first.max[0]) / 2.0) + " " +
                num((first.min[1] + first.max[1]) / 2.0) + " " + num(first.min[2] + 24.0) +
                "\"\n}\n";
  for (const FixtureLight& l : fx.lights) {
    c.entities += "{\n\"classname\" \"light\"\n\"origin\" \"" + num(l.pos[0]) + " " +
                  num(l.pos[1]) + " " + num(l.pos[2]) + "\"\n\"light\" \"" + num(l.bright) +
                  "\"\n}\n";
  }
  return true;
}

}  // namespace detail

bool write_bsp(const Fixture& fixture, Family family, std::vector<uint8_t>* out,
               std::string* error) {
  detail::Compiled c;
  if (!detail::compile(fixture, &c, error)) return false;
  detail::Out o;
  bool ok = false;
  switch (family) {
    case Family::v29:
    case Family::bsp2:
      ok = detail::write_v29(fixture, c, family == Family::bsp2, &o, error);
      break;
    case Family::v38:
      ok = detail::write_v38(fixture, c, &o, error);
      break;
    case Family::v46:
      ok = detail::write_v46(fixture, c, &o, error);
      break;
  }
  if (ok) *out = std::move(o.b);
  return ok;
}

}  // namespace ge::brush
