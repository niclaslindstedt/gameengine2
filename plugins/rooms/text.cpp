// The `.gelv.txt` reader and writer: words split on spaces, `key=value` options, metres rounded to
// the format's step, numbers printed exactly.
//
// ge:summary Parses and prints the room level's text form, with line-numbered diagnostics
// ge:keywords parse print words options metres tokens line-number
// ge:skill rooms
// ge:test tests/rooms_level_test.cpp
#include "rooms/text.hpp"

#include "rooms/relief.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ge::rooms {

namespace {

std::vector<std::string_view> words_of(std::string_view line) {
  std::vector<std::string_view> w;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const size_t s = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
    if (i > s) w.push_back(line.substr(s, i - s));
  }
  return w;
}

bool number(std::string_view s, double* out) {
  if (s.empty() || s.size() > 40) return false;
  char b[48];
  s.copy(b, s.size());
  b[s.size()] = 0;
  char* end = nullptr;
  *out = std::strtod(b, &end);
  return end == b + s.size() && std::isfinite(*out);
}

// "a,b,c" into `n` numbers.
bool numbers(std::string_view s, double* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const size_t comma = i + 1 < n ? s.find(',') : s.size();
    if (comma == std::string_view::npos || !number(s.substr(0, comma), &out[i])) return false;
    s = comma < s.size() ? s.substr(comma + 1) : std::string_view{};
  }
  return s.empty();
}

// Metres printed exactly: a stored step is a multiple of 1/128, at most seven decimals.
std::string m_of(int64_t units) {
  char b[48];
  std::snprintf(b, sizeof b, "%.7f", metres(units));
  std::string s = b;
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  return s;
}

// A float printed in the fewest digits that read back to its bits.
std::string f_of(float v) {
  char b[48];
  for (int digits = 1; digits <= 9; ++digits) {
    std::snprintf(b, sizeof b, "%.*g", digits, double(v));
    if (std::strtof(b, nullptr) == v) break;
  }
  std::string s = b;
  if (s == "-0") s = "0";
  return s;
}

// "a,b,c,..." into `n` floats, each read straight to the nearest float.
bool floats(std::string_view s, float* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const size_t comma = i + 1 < n ? s.find(',') : s.size();
    if (comma == std::string_view::npos || comma == 0 || comma > 40) return false;
    char b[48];
    s.copy(b, comma);
    b[comma] = 0;
    char* end = nullptr;
    out[i] = std::strtof(b, &end);
    if (end != b + comma || !std::isfinite(out[i])) return false;
    s = comma < s.size() ? s.substr(comma + 1) : std::string_view{};
  }
  return s.empty();
}

const char* const kContents[] = {"solid", "water", "slime", "lava", "sky", "clip", "trigger"};

// A face line's trailing words: `hidden`, `smooth`, each at most once.
bool face_flags(const std::vector<std::string_view>& w, Face* f) {
  for (size_t i = 6; i < w.size(); ++i) {
    if (w[i] == "hidden" && !f->hidden) {
      f->hidden = true;
    } else if (w[i] == "smooth" && !f->smooth) {
      f->smooth = true;
    } else {
      return false;
    }
  }
  return true;
}

// The rest of `line` from word `w`, trailing blanks trimmed ("" when there is no such word).
std::string rest_of(std::string_view line, const std::vector<std::string_view>& w, size_t i) {
  if (i >= w.size()) return {};
  std::string_view r = line.substr(size_t(w[i].data() - line.data()));
  while (!r.empty() && (r.back() == ' ' || r.back() == '\t')) r.remove_suffix(1);
  return std::string(r);
}

}  // namespace

bool parse(std::string_view text, Level* out, std::string* error) {
  Level level;
  int line_no = 0;
  std::string problem;
  auto fail = [&](const std::string& what) {
    if (error) *error = "line " + std::to_string(line_no) + ": " + what;
    return false;
  };
  auto mat = [&](std::string_view name, uint8_t* m) {
    const int i = material_index(level, std::string(name));
    if (i < 0) {
      problem = "no material '" + std::string(name) + "'; declare it with `material` first";
      return false;
    }
    *m = uint8_t(i);
    return true;
  };
  auto units16 = [&](double m, int16_t* u) {
    const int32_t v = units_of(m);
    if (v < -32768 || v > 32767) {
      problem = "a room-relative " + std::to_string(m) + " m is past 256 m from the room's origin";
      return false;
    }
    *u = int16_t(v);
    return true;
  };
  auto byte = [&](double v, uint8_t* b) {
    if (v < 0 || v > 255 || v != std::floor(v)) {
      problem = "expected a whole number 0..255";
      return false;
    }
    *b = uint8_t(v);
    return true;
  };
  size_t at = 0;
  while (at <= text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(at, end - at);
    at = end + 1;
    ++line_no;
    // A comment starts at a '#' that begins the line or follows a space, so a value may hold one
    // inside a word (a texture reference's `tint=#8a5a40`).
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
        line = line.substr(0, i);
        break;
      }
    }
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto w = words_of(line);
    if (w.empty()) continue;
    const std::string_view kind = w[0];
    if (kind == "material") {
      if (w.size() < 2) return fail("`material` needs a name");
      Material m;
      m.name = std::string(w[1]);
      for (size_t i = 2; i < w.size(); ++i) {
        double v[3];
        if (w[i] == "tex") {
          if (i + 1 >= w.size()) return fail("`tex` needs a texture reference after it");
          const size_t from = size_t(w[i + 1].data() - line.data());
          std::string_view ref = line.substr(from);
          while (!ref.empty() && (ref.back() == ' ' || ref.back() == '\t')) ref.remove_suffix(1);
          m.texture = std::string(ref);
          break;
        } else if (w[i].starts_with("size=") && numbers(w[i].substr(5), v, 2) && v[0] >= 0 &&
                   v[1] >= 0 && v[0] <= 65535 && v[1] <= 65535 && v[0] == std::floor(v[0]) &&
                   v[1] == std::floor(v[1])) {
          m.tex_w = uint16_t(v[0]);
          m.tex_h = uint16_t(v[1]);
        } else if (w[i].starts_with("rgb=") && numbers(w[i].substr(4), v, 3)) {
          for (int k = 0; k < 3; ++k) {
            if (!byte(v[k], &m.rgb[k])) return fail("rgb: " + problem);
          }
        } else if (w[i].starts_with("relief=")) {
          const std::string_view val = w[i].substr(7);
          const size_t comma = val.find(',');
          if (comma == std::string_view::npos || !relief_of(val.substr(0, comma), &m.relief) ||
              !numbers(val.substr(comma + 1), v, 2)) {
            return fail(
                "write relief=<kind>,<depth_cm>,<scale_dm>; kinds: rock crag stones "
                "ashlar tiles planks corrugated dents");
          }
          if (!byte(v[0], &m.relief_cm) || !byte(v[1], &m.scale_dm)) {
            return fail("relief=<kind>,<depth_cm>,<scale_dm>: " + problem);
          }
        } else {
          return fail("unknown material option '" + std::string(w[i]) +
                      "'; write rgb=r,g,b, relief=<kind>,<cm>,<dm>, size=<w>,<h> or tex "
                      "<reference>");
        }
      }
      level.materials.push_back(std::move(m));
    } else if (kind == "room") {
      if (w.size() < 2) return fail("`room` needs a name");
      Room r;
      r.name = std::string(w[1]);
      bool has_at = false, has_floor = false, has_ceiling = false;
      for (size_t i = 2; i < w.size(); ++i) {
        const std::string_view o = w[i];
        double v[3];
        if (o.starts_with("at=") && numbers(o.substr(3), v, 3)) {
          for (int k = 0; k < 3; ++k) r.origin[k] = units_of(v[k]);
          has_at = true;
        } else if (o.starts_with("seed=") && number(o.substr(5), v) && v[0] >= 0 &&
                   v[0] <= 4294967295.0 && v[0] == std::floor(v[0])) {
          r.seed = uint32_t(v[0]);
        } else if (o.starts_with("floor=") || o.starts_with("ceiling=")) {
          const bool fl = o[0] == 'f';
          const std::string_view val = o.substr(fl ? 6 : 8);
          const size_t colon = val.find(':');
          if (colon == std::string_view::npos || !number(val.substr(0, colon), v)) {
            return fail("write " + std::string(fl ? "floor" : "ceiling") + "=<y>:<material>");
          }
          if (!units16(v[0], fl ? &r.floor_y : &r.ceil_y) ||
              !mat(val.substr(colon + 1), fl ? &r.floor : &r.ceiling)) {
            return fail(problem);
          }
          (fl ? has_floor : has_ceiling) = true;
        } else if (o.starts_with("cave=") && numbers(o.substr(5), v, 3)) {
          r.shape = Shape::cave;
          if (!byte(v[0], &r.cave_dm) || !byte(v[1], &r.cave_scale_dm) ||
              !byte(v[2], &r.cave_cell_cm)) {
            return fail("cave=<depth_dm>,<scale_dm>,<cell_cm>: " + problem);
          }
        } else {
          return fail("unknown room option '" + std::string(o) +
                      "'; write at=x,y,z seed=n floor=y:mat ceiling=y:mat [cave=dm,dm,cm]");
        }
      }
      if (!has_at || !has_floor || !has_ceiling) {
        return fail("room '" + r.name + "' needs at=, floor= and ceiling=");
      }
      level.rooms.push_back(std::move(r));
    } else if (kind == "corner") {
      if (level.rooms.empty()) return fail("`corner` before any `room`; name the room first");
      double x, z;
      Corner c;
      if (w.size() != 4 || !number(w[1], &x) || !number(w[2], &z)) {
        return fail("write corner <x> <z> <wall material>");
      }
      if (!units16(x, &c.x) || !units16(z, &c.z) || !mat(w[3], &c.wall)) return fail(problem);
      level.rooms.back().corners.push_back(c);
    } else if (kind == "portal") {
      if (w.size() < 4)
        return fail("write portal <room> <wall> <room|outside> at= width= sill= head=");
      Portal p;
      const int r = room_index(level, std::string(w[1]));
      double v;
      if (r < 0) return fail("no room '" + std::string(w[1]) + "' above this line");
      p.room = uint16_t(r);
      if (!number(w[2], &v) || !byte(v, &p.wall))
        return fail("the wall is a corner's index 0..254");
      if (w[3] != "outside") {
        const int t = room_index(level, std::string(w[3]));
        if (t < 0) return fail("no room '" + std::string(w[3]) + "' above this line");
        p.to = uint16_t(t);
      }
      int seen = 0;
      for (size_t i = 4; i < w.size(); ++i) {
        const std::string_view o = w[i];
        const size_t eq = o.find('=');
        const std::string_view key = o.substr(0, eq), val = eq == o.npos ? "" : o.substr(eq + 1);
        int16_t* field = key == "at"      ? &p.offset
                         : key == "width" ? &p.width
                         : key == "sill"  ? &p.sill
                         : key == "head"  ? &p.head
                                          : nullptr;
        if (field && number(val, &v)) {
          if (!units16(v, field)) return fail(problem);
          ++seen;
        } else if (key == "trim") {
          if (!mat(val, &p.trim)) return fail(problem);
        } else {
          return fail("unknown portal option '" + std::string(o) +
                      "'; write at= width= sill= head= [trim=material]");
        }
      }
      if (seen != 4) return fail("a portal needs at=, width=, sill= and head=");
      level.portals.push_back(p);
    } else if (kind == "solid") {
      Solid s;
      const int r = w.size() == 9 ? room_index(level, std::string(w[1])) : -1;
      if (r < 0) return fail("write solid <room> <material> <min x y z> <max x y z>, room above");
      s.room = uint16_t(r);
      if (!mat(w[2], &s.material)) return fail(problem);
      for (int k = 0; k < 6; ++k) {
        double v;
        if (!number(w[3 + size_t(k)], &v)) return fail("a solid's box is six numbers in metres");
        if (!units16(v, k < 3 ? &s.min[k] : &s.max[k - 3])) return fail(problem);
      }
      level.solids.push_back(s);
    } else if (kind == "surface") {
      uint8_t m = 0;
      if (w.size() < 3) return fail("write surface <material> <surface reference...>");
      if (!mat(w[1], &m)) return fail(problem);
      level.materials[m].surface = rest_of(line, w, 2);
    } else if (kind == "group") {
      if (w.size() != 2) return fail("write group <name>");
      level.groups.push_back(Group{std::string(w[1])});
    } else if (kind == "brush") {
      Brush b;
      size_t c = 0;
      while (w.size() >= 2 && c < 7 && w[1] != kContents[c]) ++c;
      if (w.size() < 2 || w.size() > 3 || c == 7) {
        return fail("write brush <solid|water|slime|lava|sky|clip|trigger> [group=<name>]");
      }
      b.contents = Contents(c);
      if (w.size() == 3) {
        const int g =
            w[2].starts_with("group=") ? group_index(level, std::string(w[2].substr(6))) : -1;
        if (g < 0)
          return fail("no group '" + std::string(w[2]) + "'; declare it with `group` first");
        b.group = uint16_t(g);
      }
      b.first_face = uint32_t(level.faces.size());
      level.brushes.push_back(b);
    } else if (kind == "face") {
      if (level.brushes.empty()) return fail("`face` before any `brush`; start the brush first");
      Face f;
      Projection p;
      if (w.size() < 6 || w.size() > 8 || !floats(w[1], f.n, 3) || !floats(w[2], &f.d, 1) ||
          !w[4].starts_with("s=") || !floats(w[4].substr(2), p.s, 4) || !w[5].starts_with("t=") ||
          !floats(w[5].substr(2), p.t, 4) || !face_flags(w, &f)) {
        return fail(
            "write face <nx>,<ny>,<nz> <d> <material> s=<x>,<y>,<z>,<off> "
            "t=<x>,<y>,<z>,<off> [hidden] [smooth]");
      }
      if (!mat(w[3], &f.material)) return fail(problem);
      f.projection = projection_index(level, p);
      Brush& b = level.brushes.back();
      if (b.faces == 255) return fail("a brush holds at most 255 faces; split it");
      ++b.faces;
      level.faces.push_back(f);
    } else if (kind == "entity") {
      Entity e;
      if (w.size() < 2 || w.size() > 3) return fail("write entity <class> [group=<name>]");
      e.classname = std::string(w[1]);
      if (w.size() == 3) {
        const int g =
            w[2].starts_with("group=") ? group_index(level, std::string(w[2].substr(6))) : -1;
        if (g < 0)
          return fail("no group '" + std::string(w[2]) + "'; declare it with `group` first");
        e.group = uint16_t(g);
      }
      level.entities.push_back(std::move(e));
    } else if (kind == "key") {
      if (level.entities.empty()) return fail("`key` before any `entity`; start the entity first");
      if (w.size() < 2) return fail("write key <name> <value...>");
      level.entities.back().keys.emplace_back(std::string(w[1]), rest_of(line, w, 2));
    } else {
      return fail("unknown line '" + std::string(kind) +
                  "'; a line is material, surface, room, corner, portal, solid, group, brush, "
                  "face, entity or key");
    }
  }
  line_no = 0;
  std::string why;
  if (!link(level, &why)) {
    if (error) *error = "the level is not sound: " + why;
    return false;
  }
  *out = std::move(level);
  return true;
}

std::string print(const Level& level) {
  std::string s;
  char b[160];
  for (const Material& m : level.materials) {
    s += "material " + m.name;
    std::snprintf(b, sizeof b, " rgb=%u,%u,%u", m.rgb[0], m.rgb[1], m.rgb[2]);
    s += b;
    if (m.relief != Relief::none) {
      std::snprintf(b, sizeof b, " relief=%s,%u,%u", relief_name(m.relief).data(), m.relief_cm,
                    m.scale_dm);
      s += b;
    }
    if (m.tex_w != 0 || m.tex_h != 0)
      s += " size=" + std::to_string(m.tex_w) + "," + std::to_string(m.tex_h);
    if (!m.texture.empty()) s += " tex " + m.texture;
    s += '\n';
  }
  for (const Material& m : level.materials) {
    if (!m.surface.empty()) s += "surface " + m.name + " " + m.surface + "\n";
  }
  const auto mname = [&](uint8_t i) { return level.materials[i].name; };
  for (const Room& r : level.rooms) {
    s += "\nroom " + r.name + " at=" + m_of(r.origin[0]) + "," + m_of(r.origin[1]) + "," +
         m_of(r.origin[2]) + " seed=" + std::to_string(r.seed) + " floor=" + m_of(r.floor_y) + ":" +
         mname(r.floor) + " ceiling=" + m_of(r.ceil_y) + ":" + mname(r.ceiling);
    if (r.shape == Shape::cave) {
      std::snprintf(b, sizeof b, " cave=%u,%u,%u", r.cave_dm, r.cave_scale_dm, r.cave_cell_cm);
      s += b;
    }
    s += '\n';
    for (const Corner& c : r.corners) {
      s += "corner " + m_of(c.x) + " " + m_of(c.z) + " " + mname(c.wall) + "\n";
    }
  }
  if (!level.portals.empty()) s += '\n';
  for (const Portal& p : level.portals) {
    s += "portal " + level.rooms[p.room].name + " " + std::to_string(p.wall) + " " +
         (p.to == kOutside ? std::string("outside") : level.rooms[p.to].name) +
         " at=" + m_of(p.offset) + " width=" + m_of(p.width) + " sill=" + m_of(p.sill) +
         " head=" + m_of(p.head);
    if (p.trim != kNoMaterial) s += " trim=" + mname(p.trim);
    s += '\n';
  }
  if (!level.solids.empty()) s += '\n';
  for (const Solid& o : level.solids) {
    s += "solid " + level.rooms[o.room].name + " " + mname(o.material);
    for (int16_t v : o.min) s += " " + m_of(v);
    for (int16_t v : o.max) s += " " + m_of(v);
    s += '\n';
  }
  if (!level.groups.empty()) s += '\n';
  for (const Group& g : level.groups) s += "group " + g.name + "\n";
  const auto group_opt = [&](uint16_t g) {
    return g == kWorld ? std::string() : " group=" + level.groups[g].name;
  };
  for (const Brush& br : level.brushes) {
    s += "\nbrush " + std::string(kContents[uint8_t(br.contents)]) + group_opt(br.group) + "\n";
    for (uint32_t i = br.first_face; i < br.first_face + br.faces; ++i) {
      const Face& f = level.faces[i];
      const Projection& p = level.projections[f.projection];
      s += "face " + f_of(f.n[0]) + "," + f_of(f.n[1]) + "," + f_of(f.n[2]) + " " + f_of(f.d) +
           " " + mname(f.material) + " s=" + f_of(p.s[0]) + "," + f_of(p.s[1]) + "," +
           f_of(p.s[2]) + "," + f_of(p.s[3]) + " t=" + f_of(p.t[0]) + "," + f_of(p.t[1]) + "," +
           f_of(p.t[2]) + "," + f_of(p.t[3]) + (f.hidden ? " hidden" : "") +
           (f.smooth ? " smooth" : "") + "\n";
    }
  }
  for (const Entity& e : level.entities) {
    s += "\nentity " + e.classname + group_opt(e.group) + "\n";
    for (const auto& [k, v] : e.keys) s += "key " + k + (v.empty() ? "" : " " + v) + "\n";
  }
  return s;
}

}  // namespace ge::rooms
