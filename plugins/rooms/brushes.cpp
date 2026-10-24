// The room level's brushes, groups and entities as data: their checks (validate's half for them),
// the group lookup, an entity's key, and the projection table kept deduplicated.
//
// ge:summary Checks a level's brushes, faces, projections, groups and entities; their lookups
// ge:keywords brush brushes face plane projection group entity contents validate keys classname
// ge:skill rooms
// ge:test tests/rooms_brush_test.cpp
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "rooms/level.hpp"

namespace ge::rooms {

namespace {

std::string fmt(const char* f, auto... args) {
  char b[512];
  std::snprintf(b, sizeof b, f, args...);
  return b;
}

bool finite(const float* v, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!std::isfinite(v[i])) return false;
  }
  return true;
}

// A key or a value both forms can hold: under 256 bytes, no line break (the text form is lines)
// and no `#` (which starts the text form's comment).
bool storable(const std::string& s) {
  if (s.size() >= 256 || s.find_first_of("\n\r") != std::string::npos) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) return false;
  }
  return true;
}

}  // namespace

const std::string& Entity::get(const std::string& key) const {
  static const std::string kNone;
  for (const auto& [k, v] : keys) {
    if (k == key) return v;
  }
  return kNone;
}

int group_index(const Level& level, const std::string& name) {
  for (size_t i = 0; i < level.groups.size(); ++i) {
    if (level.groups[i].name == name) return int(i);
  }
  return -1;
}

uint16_t projection_index(Level& level, const Projection& p) {
  for (size_t i = 0; i < level.projections.size(); ++i) {
    const Projection& q = level.projections[i];
    bool same = true;
    for (size_t k = 0; k < 4 && same; ++k) {
      same = std::bit_cast<uint32_t>(q.s[k]) == std::bit_cast<uint32_t>(p.s[k]) &&
             std::bit_cast<uint32_t>(q.t[k]) == std::bit_cast<uint32_t>(p.t[k]);
    }
    if (same) return uint16_t(i);
  }
  level.projections.push_back(p);
  return uint16_t(level.projections.size() - 1);
}

void validate_brushes(const Level& level, std::vector<std::string>& out) {
  if (level.projections.size() > kMaxProjections) {
    out.push_back(fmt("level: %zu texture projections, the format holds %zu; share them",
                      level.projections.size(), kMaxProjections));
  }
  if (level.groups.size() > kMaxGroups) {
    out.push_back(fmt("level: %zu groups, the format holds %zu", level.groups.size(), kMaxGroups));
  }
  for (size_t k = 0; k < level.projections.size(); ++k) {
    const Projection& p = level.projections[k];
    if (!finite(p.s, 4) || !finite(p.t, 4)) {
      out.push_back(
          fmt("projection %zu: a number is not finite; write finite axes and offsets", k));
    }
  }
  std::set<std::string> names;
  for (size_t k = 0; k < level.groups.size(); ++k) {
    const std::string& n = level.groups[k].name;
    if (n.empty() || !storable(n) || n.find(' ') != std::string::npos) {
      out.push_back(
          fmt("group %zu: the name '%s' is empty, too long or has a space", k, n.c_str()));
    } else if (!names.insert(n).second) {
      out.push_back(fmt("group %zu: the name '%s' is used twice; names are unique", k, n.c_str()));
    }
  }
  size_t next_face = 0;
  for (size_t k = 0; k < level.brushes.size(); ++k) {
    const Brush& b = level.brushes[k];
    if (b.group != kWorld && b.group >= level.groups.size()) {
      out.push_back(fmt("brush %zu: group %u out of range", k, unsigned(b.group)));
    }
    if (uint8_t(b.contents) > uint8_t(Contents::trigger)) {
      out.push_back(fmt("brush %zu: unknown contents %u", k, unsigned(b.contents)));
    }
    if (b.faces < 4) {
      out.push_back(
          fmt("brush %zu: %u faces; a closed convex solid has at least 4", k, unsigned(b.faces)));
    }
    // A brush's faces follow the previous brush's, so the faces are the brushes' in order.
    if (b.first_face != next_face) {
      out.push_back(fmt("brush %zu: its faces start at %u, not %zu where the last brush's end", k,
                        unsigned(b.first_face), next_face));
    }
    next_face = size_t(b.first_face) + b.faces;
    if (next_face > level.faces.size()) {
      out.push_back(
          fmt("brush %zu: its faces run past the %zu the level has", k, level.faces.size()));
      return;
    }
  }
  if (next_face != level.faces.size()) {
    out.push_back(fmt("level: %zu faces belong to no brush", level.faces.size() - next_face));
  }
  for (size_t k = 0; k < level.faces.size(); ++k) {
    const Face& f = level.faces[k];
    const double len =
        std::sqrt(double(f.n[0]) * f.n[0] + double(f.n[1]) * f.n[1] + double(f.n[2]) * f.n[2]);
    if (!finite(f.n, 3) || !std::isfinite(f.d) || std::fabs(len - 1) > 1e-3) {
      out.push_back(fmt("face %zu: the normal is not a finite unit vector (length %.6f)", k, len));
    }
    if (f.material >= level.materials.size()) {
      out.push_back(fmt("face %zu: material %u out of range", k, unsigned(f.material)));
    }
    if (f.projection >= level.projections.size()) {
      out.push_back(fmt("face %zu: projection %u out of range", k, unsigned(f.projection)));
    }
  }
  for (size_t k = 0; k < level.entities.size(); ++k) {
    const Entity& e = level.entities[k];
    if (e.classname.empty() || !storable(e.classname) ||
        e.classname.find(' ') != std::string::npos) {
      out.push_back(fmt("entity %zu: the class '%s' is empty, too long or has a space", k,
                        e.classname.c_str()));
    }
    if (e.group != kWorld && e.group >= level.groups.size()) {
      out.push_back(fmt("entity %zu: group %u out of range", k, unsigned(e.group)));
    }
    if (e.keys.size() > 255) out.push_back(fmt("entity %zu: over 255 keys", k));
    for (const auto& [key, value] : e.keys) {
      if (key.empty() || !storable(key) || key.find(' ') != std::string::npos || !storable(value)) {
        out.push_back(
            fmt("entity %zu: the key '%s' or its value is empty, too long, has a space "
                "in the key or a line break",
                k, key.c_str()));
      }
    }
  }
  for (size_t k = 0; k < level.materials.size(); ++k) {
    if (!storable(level.materials[k].surface) || !storable(level.materials[k].texture)) {
      out.push_back(
          fmt("material %zu: a texture or surface reference is too long, has a line "
              "break or a '#' after a space; shorten it or join the '#' to its word",
              k));
    }
  }
}

}  // namespace ge::rooms
