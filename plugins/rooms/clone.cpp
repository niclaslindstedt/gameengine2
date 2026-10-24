// The cloner (clone.hpp): the texture map read, the tree's cells collected and built into
// brushes, each face's texture and projection found from the surface lying on it, the brush
// models as groups, the entities' keys.
//
// ge:summary Clones a classic map: texture map, tree cells to brushes, projections, entities
// ge:keywords clone bsp cell leaf path half-space projection solve surface plane bucket entity
// ge:skill rooms brush
// ge:test tests/rooms_clone_test.cpp
#include "rooms/clone.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>

#include "core/parallel.hpp"
#include "rooms/brush_build.hpp"

namespace ge::rooms {

namespace {

namespace br = ge::brush;

// The words of a line up to its comment: a '#' at the start or after a space.
std::vector<std::string_view> words(std::string_view line) {
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
      line = line.substr(0, i);
      break;
    }
  }
  std::vector<std::string_view> w;
  size_t at = 0;
  while (at < line.size()) {
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) ++at;
    size_t end = at;
    while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r') ++end;
    if (end > at) w.push_back(line.substr(at, end - at));
    at = end;
  }
  return w;
}

std::string join(const std::vector<std::string_view>& w, size_t from) {
  std::string s;
  for (size_t i = from; i < w.size(); ++i) {
    if (!s.empty()) s += ' ';
    s += w[i];
  }
  return s;
}

// A texture's material name: lower case, `liq_` for a leading '*', `anim_` for '+', and '_' for
// anything a name in the text form cannot hold.
std::string material_name(const std::string& texture) {
  std::string out;
  size_t at = 0;
  if (!texture.empty() && texture[0] == '*') out = "liq_", at = 1;
  if (!texture.empty() && texture[0] == '+') out = "anim_", at = 1;
  for (size_t i = at; i < texture.size(); ++i) {
    const auto c = uint8_t(texture[i]);
    out += std::isalnum(c) || c == '_' || c == '-' ? char(std::tolower(c)) : '_';
  }
  return out.empty() ? std::string("unnamed") : out;
}

// A value with every '#' that would start a comment (at the start or after a space) made '_',
// cut to what a record holds.
std::string storable_value(std::string v) {
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\n' || v[i] == '\r') v[i] = ' ';
    if (v[i] == '#' && (i == 0 || v[i - 1] == ' ' || v[i - 1] == '\t')) v[i] = '_';
  }
  if (v.size() > 255) v.resize(255);
  return v;
}

// `v` as a float with no negative zero: the text form writes -0 as 0, so the clone and its text
// hold the same bits.
float unsigned_zero(double v) { return float(v) + 0.0f; }

std::string num(double v) {
  char b[48];
  std::snprintf(b, sizeof b, "%.6g", v);
  return b;
}

// A step on a cell's path: node `plane`, the side taken (0 in front, 1 behind).
struct Step {
  uint32_t plane;
  uint8_t side;
};

struct Cell {
  std::vector<Step> path;
  uint32_t model = 0;
  Contents contents = Contents::solid;
};

bool contents_of(const br::Leaf& leaf, Contents* out) {
  switch (leaf.contents) {
    case -2:
      *out = Contents::solid;
      return true;
    case -3:
      *out = Contents::water;
      return true;
    case -4:
      *out = Contents::slime;
      return true;
    case -5:
      *out = Contents::lava;
      return true;
    case -6:
      *out = Contents::sky;
      return true;
    default:
      return false;
  }
}

void collect(const br::Map& map, int32_t child, uint32_t model, std::vector<Step>& path,
             std::vector<Cell>& cells) {
  if (child < 0) {
    Contents c;
    if (contents_of(map.leaves[size_t(-(child + 1))], &c)) cells.push_back({path, model, c});
    return;
  }
  const br::Node& n = map.nodes[size_t(child)];
  for (uint8_t side = 0; side < 2; ++side) {
    path.push_back({n.plane, side});
    collect(map, n.child[side], model, path, cells);
    path.pop_back();
  }
}

// A plane in the engine's frame: the map's n·p = d with p = (x, z, y) / unit.
HalfSpace engine_half(const br::Plane& p, uint8_t side) {
  const double s = side == 0 ? -1.0 : 1.0;  // in front: n·p >= d, so -n·p <= -d
  return {{s * p.n[0], s * p.n[2], s * p.n[1]}, s * double(p.d) * br::kUnitM};
}

// A surface's plane, oriented so its first non-zero normal component is positive, as a key.
using PlaneKey = std::tuple<int64_t, int64_t, int64_t, int64_t>;

PlaneKey key_of(const double n[3], double d) {
  double m[4] = {n[0], n[1], n[2], d};
  for (int k = 0; k < 3; ++k) {
    if (std::fabs(m[k]) > 1e-6) {
      if (m[k] < 0) {
        for (double& x : m) x = -x;
      }
      break;
    }
  }
  return {std::llround(m[0] * 512), std::llround(m[1] * 512), std::llround(m[2] * 512),
          std::llround(m[3] * 64)};
}

struct SurfacePlane {
  double n[3] = {0, 0, 0}, d = 0;
  bool ok = false;
  Projection proj;
  uint32_t big = 0;  // its largest triangle
};

void engine_pos(const br::Vertex& v, double out[3]) { br::to_engine(v.pos, out); }

// The axis a with a·e1 = ds1, a·e2 = ds2 and a·n = 0, by Cramer's rule.
bool solve_axis(const double e1[3], const double e2[3], const double n[3], double ds1, double ds2,
                double out[3]) {
  const double m[3][3] = {{e1[0], e1[1], e1[2]}, {e2[0], e2[1], e2[2]}, {n[0], n[1], n[2]}};
  const double r[3] = {ds1, ds2, 0};
  const auto det = [](const double a[3][3]) {
    return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
           a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
  };
  const double dm = det(m);
  if (std::fabs(dm) < 1e-12) return false;
  for (int c = 0; c < 3; ++c) {
    double a[3][3];
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) a[i][j] = j == c ? r[i] : m[i][j];
    }
    out[c] = det(a) / dm;
  }
  return true;
}

SurfacePlane surface_plane(const br::Map& map, const br::Surface& s) {
  SurfacePlane sp;
  double best = 0;
  for (uint32_t t = 0; t < s.count / 3; ++t) {
    double p[3][3];
    for (uint32_t k = 0; k < 3; ++k)
      engine_pos(map.vertices[map.indices[s.first + t * 3 + k]], p[k]);
    const double e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
    const double e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
    const double c[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                         e1[0] * e2[1] - e1[1] * e2[0]};
    const double a = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    if (a <= best) continue;
    best = a;
    sp.big = t;
    const double len = std::sqrt(a);
    for (int k = 0; k < 3; ++k) sp.n[k] = c[k] / len;
    sp.d = sp.n[0] * p[0][0] + sp.n[1] * p[0][1] + sp.n[2] * p[0][2];
    const br::Vertex* v[3];
    for (int k = 0; k < 3; ++k) v[k] = &map.vertices[map.indices[s.first + t * 3 + uint32_t(k)]];
    double sa[3], ta[3];
    if (!solve_axis(e1, e2, sp.n, double(v[1]->st[0] - v[0]->st[0]),
                    double(v[2]->st[0] - v[0]->st[0]), sa) ||
        !solve_axis(e1, e2, sp.n, double(v[1]->st[1] - v[0]->st[1]),
                    double(v[2]->st[1] - v[0]->st[1]), ta)) {
      continue;
    }
    for (int k = 0; k < 3; ++k)
      sp.proj.s[k] = unsigned_zero(sa[k]), sp.proj.t[k] = unsigned_zero(ta[k]);
    sp.proj.s[3] =
        unsigned_zero(double(v[0]->st[0]) - (sa[0] * p[0][0] + sa[1] * p[0][1] + sa[2] * p[0][2]));
    sp.proj.t[3] =
        unsigned_zero(double(v[0]->st[1]) - (ta[0] * p[0][0] + ta[1] * p[0][1] + ta[2] * p[0][2]));
    sp.ok = true;
  }
  return sp;
}

// How far the point `c` on a surface's plane (normal `n`) is from the surface: 0 inside one of
// its triangles (wound either way, to a centimetre), else the distance to the nearest triangle's
// middle.
double distance_to_surface(const br::Map& map, const br::Surface& s, const double n[3],
                           const double c[3]) {
  double best = 1e30;
  for (uint32_t t = 0; t < s.count / 3; ++t) {
    double p[3][3];
    for (uint32_t k = 0; k < 3; ++k)
      engine_pos(map.vertices[map.indices[s.first + t * 3 + k]], p[k]);
    int pos = 0, neg = 0;
    for (int k = 0; k < 3; ++k) {
      const double* a = p[k];
      const double* b = p[(k + 1) % 3];
      const double e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
      const double w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
      const double x[3] = {e[1] * w[2] - e[2] * w[1], e[2] * w[0] - e[0] * w[2],
                           e[0] * w[1] - e[1] * w[0]};
      const double el = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
      const double sd = (x[0] * n[0] + x[1] * n[1] + x[2] * n[2]) / std::max(el, 1e-12);
      pos += sd > 0.01;
      neg += sd < -0.01;
    }
    if (pos == 0 || neg == 0) return 0;
    double m[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
      for (int j = 0; j < 3; ++j) m[j] += p[k][j] / 3;
    }
    const double d[3] = {m[0] - c[0], m[1] - c[1], m[2] - c[2]};
    best = std::min(best, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
  }
  return best;
}

// The texture map's row for `name`, made when it has none.
TextureChoice& row_of(TextureMap& map, const std::string& name) {
  for (TextureChoice& r : map.rows) {
    if (r.name == name) return r;
  }
  map.rows.push_back(TextureChoice{});
  map.rows.back().name = name;
  return map.rows.back();
}

}  // namespace

const TextureChoice* TextureMap::find(const std::string& name) const {
  for (const TextureChoice& r : rows) {
    if (r.name == name) return &r;
  }
  return nullptr;
}

bool parse_texture_map(std::string_view text, TextureMap* out, std::string* error) {
  TextureMap map;
  size_t at = 0, line_no = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    const auto w = words(text.substr(at, end - at));
    at = end + 1;
    ++line_no;
    if (w.empty()) continue;
    const auto fail = [&](const std::string& what) {
      if (error) *error = "line " + std::to_string(line_no) + ": " + what;
      return false;
    };
    if (w[0] == "texture") {
      if (w.size() < 3) return fail("write texture <name> <recipe reference...>");
      TextureChoice& c = row_of(map, std::string(w[1]));
      c.kind = TextureKind::texture;
      c.ref = join(w, 2);
    } else if (w[0] == "liquid") {
      if (w.size() < 3)
        return fail("write liquid <name> <water|slime|lava|teleport> [<recipe...>]");
      TextureChoice& c = row_of(map, std::string(w[1]));
      c.kind = TextureKind::liquid;
      c.liquid = std::string(w[2]);
      c.ref = join(w, 3);
    } else if (w[0] == "sky") {
      if (w.size() < 2) return fail("write sky <name> [<recipe reference...>]");
      TextureChoice& c = row_of(map, std::string(w[1]));
      c.kind = TextureKind::sky;
      c.ref = join(w, 2);
    } else if (w[0] == "surface") {
      if (w.size() < 3) return fail("write surface <name> <surface reference...>");
      row_of(map, std::string(w[1])).surface = join(w, 2);
    }
    // Any other line is the matcher's own (scores, classes, notes) and is passed over.
  }
  *out = std::move(map);
  return true;
}

bool clone_map(const brush::Map& map, const TextureMap& textures, const CloneOptions& options,
               Level* out, CloneReport* report, std::string* error) {
  CloneReport rep;
  const auto fail = [&](const std::string& what) {
    if (error) *error = "clone: " + what;
    return false;
  };
  if (map.family != br::Family::v29 && map.family != br::Family::bsp2) {
    return fail(std::string("the ") + br::family_name(map.family) +
                " family is not cloned yet; clone a version 29 or BSP2 map (docs/levels.md)");
  }
  Level level;

  // The materials: one a texture, from the texture map.
  std::vector<std::string> missing;
  for (const br::Texture& t : map.textures) {
    Material m;
    m.name = material_name(t.name);
    for (int n = 2;
         level.materials.end() != std::find_if(level.materials.begin(), level.materials.end(),
                                               [&](const Material& o) { return o.name == m.name; });
         ++n) {
      m.name = material_name(t.name) + "_" + std::to_string(n);
    }
    m.tex_w = uint16_t(std::min<uint32_t>(t.width, 0xffff));
    m.tex_h = uint16_t(std::min<uint32_t>(t.height, 0xffff));
    m.rgb[0] = m.rgb[1] = m.rgb[2] = 128;
    if (options.palette.size() == 768 && !t.indexed.empty()) {
      uint64_t sum[3] = {0, 0, 0};
      for (uint8_t i : t.indexed) {
        for (size_t k = 0; k < 3; ++k) sum[k] += options.palette[size_t(i) * 3 + k];
      }
      for (size_t k = 0; k < 3; ++k) m.rgb[k] = uint8_t(sum[k] / t.indexed.size());
    }
    if (const TextureChoice* c = textures.find(t.name);
        c != nullptr &&
        (!c->ref.empty() || c->kind != TextureKind::texture || !c->surface.empty())) {
      m.texture = c->ref;
      m.surface = c->surface;
    } else if (options.placeholders) {
      rep.placeholders.push_back(t.name);
    } else {
      missing.push_back(t.name);
    }
    level.materials.push_back(std::move(m));
  }
  if (!missing.empty()) {
    std::string names;
    for (const std::string& n : missing) names += (names.empty() ? "" : ", ") + n;
    return fail(std::to_string(missing.size()) + " textures have no line in the texture map (" +
                names +
                "); match them (ge texture match) and approve the map, or clone with "
                "placeholders");
  }
  const auto unseen = uint8_t(level.materials.size());
  if (level.materials.size() >= 255) {
    return fail("more than 254 textures; a level holds 255 materials");
  }
  {
    Material m;
    m.name = "unseen";
    m.rgb[0] = m.rgb[1] = m.rgb[2] = 96;
    level.materials.push_back(m);
  }

  // Each surface's plane and projection, bucketed by plane.
  std::vector<SurfacePlane> planes(map.surfaces.size());
  core::parallel_bands(planes.size(), options.workers, [&](size_t begin, size_t end) {
    for (size_t s = begin; s < end; ++s) planes[s] = surface_plane(map, map.surfaces[s]);
  });
  std::map<PlaneKey, std::vector<uint32_t>> bucket;
  for (size_t s = 0; s < planes.size(); ++s) {
    if (planes[s].ok) bucket[key_of(planes[s].n, planes[s].d)].push_back(uint32_t(s));
  }

  // The cells: every model's tree walked from its head.
  std::vector<Cell> cells;
  std::vector<std::vector<HalfSpace>> clamps(map.models.size());
  for (size_t k = 0; k < map.models.size(); ++k) {
    const br::Model& md = map.models[k];
    std::vector<Step> path;
    collect(map, md.head_node, uint32_t(k), path, cells);
    double lo[3], hi[3];
    const float mins[3] = {md.mins[0] - 1, md.mins[1] - 1, md.mins[2] - 1};
    const float maxs[3] = {md.maxs[0] + 1, md.maxs[1] + 1, md.maxs[2] + 1};
    br::to_engine(mins, lo);
    br::to_engine(maxs, hi);
    for (int a = 0; a < 3; ++a) {
      HalfSpace up, down;
      up.n[0] = up.n[1] = up.n[2] = 0;
      down = up;
      up.n[a] = 1, up.d = std::max(lo[a], hi[a]);
      down.n[a] = -1, down.d = -std::min(lo[a], hi[a]);
      clamps[k].push_back(up);
      clamps[k].push_back(down);
    }
  }
  rep.cells = cells.size();

  // Each cell's polygons, in parallel.
  struct Built {
    std::vector<HalfSpace> planes;
    std::vector<std::vector<double>> polys;
    std::vector<uint8_t> clamp;
    bool ok = false;
  };
  std::vector<Built> built(cells.size());
  core::parallel_bands(cells.size(), options.workers, [&](size_t begin, size_t end) {
    for (size_t c = begin; c < end; ++c) {
      Built& b = built[c];
      for (const Step& st : cells[c].path) {
        b.planes.push_back(engine_half(map.planes[st.plane], st.side));
        b.clamp.push_back(0);
      }
      for (const HalfSpace& h : clamps[cells[c].model]) b.planes.push_back(h), b.clamp.push_back(1);
      b.ok = cell_polygons(b.planes, &b.polys);
    }
  });

  // A brush model a trigger entity names is a trigger volume, not a wall.
  std::vector<uint8_t> trigger(map.models.size(), 0);
  for (const br::Entity& e : map.entities) {
    const std::string& m = e.get("model");
    if (e.get("classname").rfind("trigger_", 0) != 0 || m.size() < 2 || m[0] != '*') continue;
    const long k = std::strtol(m.c_str() + 1, nullptr, 10);
    if (k >= 1 && size_t(k) < trigger.size()) trigger[size_t(k)] = 1;
  }

  // The groups: one a brush model.
  for (size_t k = 1; k < map.models.size(); ++k)
    level.groups.push_back({"model_" + std::to_string(k)});
  rep.groups = level.groups.size();

  std::map<
      std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>,
      uint16_t>
      proj_of;
  const auto projection = [&](const Projection& p) -> uint16_t {
    std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t> key;
    uint32_t b[8];
    for (int k = 0; k < 4; ++k) std::memcpy(&b[k], &p.s[k], 4), std::memcpy(&b[4 + k], &p.t[k], 4);
    key = {b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]};
    const auto it = proj_of.find(key);
    if (it != proj_of.end()) return it->second;
    const auto i = uint16_t(level.projections.size());
    level.projections.push_back(p);
    proj_of.emplace(key, i);
    return i;
  };
  const uint16_t plain = projection(Projection{});

  for (size_t c = 0; c < cells.size(); ++c) {
    const Built& b = built[c];
    if (!b.ok) {
      ++rep.open;
      continue;
    }
    std::vector<size_t> used;
    for (size_t i = 0; i < b.polys.size(); ++i) {
      if (!b.polys[i].empty()) used.push_back(i);
    }
    if (used.size() > kMaxFaces) {
      ++rep.too_many;
      continue;
    }
    if (used.size() < 4) continue;
    if (level.projections.size() + used.size() > kMaxProjections) {
      return fail("more texture projections than a level holds (65535); split the map");
    }
    Brush brush;
    brush.contents = trigger[cells[c].model] ? Contents::trigger : cells[c].contents;
    brush.group = cells[c].model == 0 ? kWorld : uint16_t(cells[c].model - 1);
    brush.first_face = uint32_t(level.faces.size());
    for (size_t i : used) {
      const HalfSpace& h = b.planes[i];
      Face f;
      for (int k = 0; k < 3; ++k) f.n[k] = unsigned_zero(h.n[k]);
      f.d = unsigned_zero(h.d);
      f.smooth = options.smooth;
      f.material = unseen;
      f.projection = plain;
      f.hidden = true;
      if (!b.clamp[i]) {
        const std::vector<double>& poly = b.polys[i];
        double ctr[3] = {0, 0, 0};
        const size_t n = poly.size() / 3;
        for (size_t v = 0; v < n; ++v) {
          for (int k = 0; k < 3; ++k) ctr[k] += poly[v * 3 + size_t(k)] / double(n);
        }
        const auto it = bucket.find(key_of(h.n, h.d));
        if (it != bucket.end()) {
          double best = 1e30;
          uint32_t pick = 0;
          for (const uint32_t s : it->second) {
            const double dist = distance_to_surface(map, map.surfaces[s], h.n, ctr);
            if (dist < best) best = dist, pick = s;
          }
          f.material = uint8_t(map.surfaces[pick].texture);
          f.projection = projection(planes[pick].proj);
          f.hidden = false;
        }
      }
      if (f.hidden) ++rep.unseen;
      level.faces.push_back(f);
    }
    brush.faces = uint8_t(used.size());
    level.brushes.push_back(brush);
  }
  rep.brushes = level.brushes.size();
  rep.faces = level.faces.size();

  // The entities: their keys, `model` as the group, `origin` and `angle` in the engine's terms.
  for (const br::Entity& e : map.entities) {
    Entity out_e;
    out_e.classname = storable_value(e.get("classname"));
    for (char& ch : out_e.classname) {
      if (ch == ' ' || ch == '\t') ch = '_';
    }
    if (out_e.classname.empty()) out_e.classname = "unnamed";
    for (const auto& [k, v] : e.pairs) {
      if (k == "classname") continue;
      std::string key = storable_value(k);
      for (char& ch : key) {
        if (ch == ' ' || ch == '\t') ch = '_';
      }
      if (key.empty()) continue;
      if (k == "model" && v.size() > 1 && v[0] == '*') {
        const long m = std::strtol(v.c_str() + 1, nullptr, 10);
        if (m >= 1 && size_t(m) < map.models.size()) {
          out_e.group = uint16_t(m - 1);
          continue;
        }
      }
      if (k == "origin") {
        float p[3] = {0, 0, 0};
        if (std::sscanf(v.c_str(), "%f %f %f", &p[0], &p[1], &p[2]) == 3) {
          double q[3];
          br::to_engine(p, q);
          out_e.keys.emplace_back("origin", num(q[0]) + " " + num(q[1]) + " " + num(q[2]));
          continue;
        }
      }
      if (k == "angle") {
        const double a = std::strtod(v.c_str(), nullptr);
        if (a == -1 || a == -2) {
          out_e.keys.emplace_back("heading", num(a));
        } else {
          double h = 90 - a;
          h -= 360 * std::floor(h / 360);
          out_e.keys.emplace_back("heading", num(h));
        }
        continue;
      }
      if (out_e.keys.size() < 255) out_e.keys.emplace_back(key, storable_value(v));
    }
    level.entities.push_back(std::move(out_e));
  }
  rep.entities = level.entities.size();

  // The projections pooled in the order faces first use them, as the text form pools them, so
  // the clone and its text read back to the same bytes.
  {
    std::vector<Projection> pooled;
    std::vector<int32_t> to(level.projections.size(), -1);
    for (Face& f : level.faces) {
      if (to[f.projection] < 0) {
        to[f.projection] = int32_t(pooled.size());
        pooled.push_back(level.projections[f.projection]);
      }
      f.projection = uint16_t(to[f.projection]);
    }
    level.projections.swap(pooled);
  }
  std::string why;
  if (!link(level, &why)) return fail("the clone is not a sound level: " + why);
  *out = std::move(level);
  if (report) *report = std::move(rep);
  return true;
}

}  // namespace ge::rooms
