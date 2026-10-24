// The room level's checks and links: `validate` names every problem in the diagnostics' shape,
// `link` finds the wall each portal pierces on its far side.
//
// ge:summary Validates a room level (limits, polygons, portals, solids) and links its portals
// ge:keywords validate link portal far-wall polygon simple counter-clockwise diagnostics
// ge:skill rooms
// ge:test tests/rooms_level_test.cpp
#include "rooms/level.hpp"

#include <cmath>
#include <cstdio>
#include <set>

namespace ge::rooms {

namespace {

// One stored step: two points nearer than this are the same point.
constexpr double kNearM = kUnitM;

std::string fmt(const char* f, auto... args) {
  char b[512];
  std::snprintf(b, sizeof b, f, args...);
  return b;
}

double cross2(const double a[2], const double b[2], const double c[2]) {
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

// Whether the closed segments ab and cd cross or touch.
bool segments_meet(const double a[2], const double b[2], const double c[2], const double d[2]) {
  const double d1 = cross2(c, d, a), d2 = cross2(c, d, b), d3 = cross2(a, b, c),
               d4 = cross2(a, b, d);
  if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
    return true;
  }
  auto on = [](const double p[2], const double q[2], const double r[2], double cr) {
    return cr == 0 && std::min(p[0], q[0]) <= r[0] && r[0] <= std::max(p[0], q[0]) &&
           std::min(p[1], q[1]) <= r[1] && r[1] <= std::max(p[1], q[1]);
  };
  return on(c, d, a, d1) || on(c, d, b, d2) || on(a, b, c, d3) || on(a, b, d, d4);
}

double signed_area(const Room& room) {
  double a = 0;
  const size_t n = room.corners.size();
  for (size_t i = 0; i < n; ++i) {
    const Corner& p = room.corners[i];
    const Corner& q = room.corners[(i + 1) % n];
    a += double(p.x) * double(q.z) - double(q.x) * double(p.z);
  }
  return 0.5 * a;
}

// The wall of `to` that the opening of portal `p` (on `from`) lies on, facing back, with the
// opening inside it; -1 when none does.
int far_wall(const Level& level, const Portal& p, std::string* why) {
  const Room& from = level.rooms[p.room];
  const Room& to = level.rooms[p.to];
  double a[2], b[2];
  corner_m(from, p.wall, a);
  corner_m(from, (p.wall + 1u) % from.corners.size(), b);
  const double len = wall_length_m(from, p.wall);
  const double dir[2] = {(b[0] - a[0]) / len, (b[1] - a[1]) / len};
  // The opening's two ends in the plan.
  const double o0 = metres(p.offset), o1 = metres(p.offset + p.width);
  const double e0[2] = {a[0] + dir[0] * o0, a[1] + dir[1] * o0};
  const double e1[2] = {a[0] + dir[0] * o1, a[1] + dir[1] * o1};
  for (size_t j = 0; j < to.corners.size(); ++j) {
    double c[2], d[2];
    corner_m(to, j, c);
    corner_m(to, (j + 1) % to.corners.size(), d);
    const double lj = wall_length_m(to, j);
    const double dj[2] = {(d[0] - c[0]) / lj, (d[1] - c[1]) / lj};
    if (dir[0] * dj[0] + dir[1] * dj[1] > -0.999) continue;  // not facing back
    // Both ends of the opening on wall j's line, within its length.
    bool fits = true;
    for (const double* e : {e0, e1}) {
      const double rel[2] = {e[0] - c[0], e[1] - c[1]};
      const double along = rel[0] * dj[0] + rel[1] * dj[1];
      const double off = rel[0] * dj[1] - rel[1] * dj[0];
      if (std::fabs(off) > kNearM || along < -kNearM || along > lj + kNearM) fits = false;
    }
    if (fits) return int(j);
  }
  *why =
      fmt("no wall of room '%s' lies on wall %u of room '%s' facing back with the opening "
          "inside it",
          to.name.c_str(), unsigned(p.wall), from.name.c_str());
  return -1;
}

}  // namespace

int32_t units_of(double m) { return static_cast<int32_t>(std::floor(m / kUnitM + 0.5)); }

void corner_m(const Room& room, size_t i, double out[2]) {
  const Corner& c = room.corners[i];
  out[0] = metres(int64_t(room.origin[0]) + c.x);
  out[1] = metres(int64_t(room.origin[2]) + c.z);
}

double wall_length_m(const Room& room, size_t wall) {
  double a[2], b[2];
  corner_m(room, wall, a);
  corner_m(room, (wall + 1) % room.corners.size(), b);
  return std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
}

int material_index(const Level& level, const std::string& name) {
  for (size_t i = 0; i < level.materials.size(); ++i) {
    if (level.materials[i].name == name) return int(i);
  }
  return -1;
}

int room_index(const Level& level, const std::string& name) {
  for (size_t i = 0; i < level.rooms.size(); ++i) {
    if (level.rooms[i].name == name) return int(i);
  }
  return -1;
}

std::vector<std::string> validate(const Level& level) {
  std::vector<std::string> out;
  const size_t nm = level.materials.size();
  if (nm == 0) out.push_back("level: no materials; declare one with `material <name> ...`");
  if (nm > kMaxMaterials) {
    out.push_back(fmt("level: %zu materials, the format holds %zu; merge some", nm, kMaxMaterials));
  }
  if (level.rooms.size() > kMaxRooms) {
    out.push_back(fmt("level: %zu rooms, the format holds %zu; split the level", level.rooms.size(),
                      kMaxRooms));
  }
  auto plain = [](const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
      if (c <= ' ' || c == '#' || c == '"') return false;
    }
    return true;
  };
  std::set<std::string> names;
  for (size_t i = 0; i < nm; ++i) {
    const Material& m = level.materials[i];
    if (!plain(m.name)) {
      out.push_back(fmt("material %zu: name '%s' is empty or has a space, '#' or '\"'; rename it",
                        i, m.name.c_str()));
    }
    if (!names.insert(m.name).second) {
      out.push_back(fmt("material '%s': named twice; rename one", m.name.c_str()));
    }
    if (m.name.size() > 255 || m.texture.size() > 255) {
      out.push_back(fmt("material '%s': a name or texture reference over 255 bytes; shorten it",
                        m.name.c_str()));
    }
    if (uint8_t(m.relief) > uint8_t(Relief::dents)) {
      out.push_back(fmt("material '%s': an unknown relief %u", m.name.c_str(), unsigned(m.relief)));
    } else if (m.relief != Relief::none && (m.relief_cm == 0 || m.scale_dm == 0)) {
      out.push_back(
          fmt("material '%s': a relief needs a depth > 0 cm and a scale > 0 dm", m.name.c_str()));
    }
  }
  auto mat_ok = [&](uint8_t m) { return m < nm; };
  names.clear();
  for (size_t r = 0; r < level.rooms.size(); ++r) {
    const Room& room = level.rooms[r];
    const char* rn = room.name.c_str();
    if (!plain(room.name)) {
      out.push_back(
          fmt("room %zu: name '%s' is empty or has a space, '#' or '\"'; rename it", r, rn));
    }
    if (room.name.size() > 255) out.push_back(fmt("room '%s': a name over 255 bytes", rn));
    if (!names.insert(room.name).second)
      out.push_back(fmt("room '%s': named twice; rename one", rn));
    const size_t n = room.corners.size();
    if (n < 3 || n > kMaxCorners) {
      out.push_back(fmt("room '%s': %zu corners; a room has 3 to %zu", rn, n, kMaxCorners));
      continue;
    }
    if (!mat_ok(room.floor) || !mat_ok(room.ceiling)) {
      out.push_back(
          fmt("room '%s': floor or ceiling material out of range (%zu materials)", rn, nm));
    }
    if (room.floor_y >= room.ceil_y) {
      out.push_back(fmt("room '%s': floor %d is not under ceiling %d (units of 1/128 m)", rn,
                        room.floor_y, room.ceil_y));
    }
    if (room.shape == Shape::cave && (room.cave_cell_cm < 5 || room.cave_scale_dm == 0)) {
      out.push_back(fmt("room '%s': a cave needs cave_cell_cm >= 5 and cave_scale_dm > 0", rn));
    }
    for (size_t i = 0; i < n; ++i) {
      if (!mat_ok(room.corners[i].wall)) {
        out.push_back(fmt("room '%s': wall %zu's material %u out of range", rn, i,
                          unsigned(room.corners[i].wall)));
      }
      if (wall_length_m(room, i) < kNearM) {
        out.push_back(
            fmt("room '%s': corners %zu and %zu are the same point; drop one", rn, i, (i + 1) % n));
      }
    }
    if (signed_area(room) <= 0) {
      out.push_back(
          fmt("room '%s': corners run clockwise seen from above; list them "
              "counter-clockwise (+x right, +z up the page)",
              rn));
    }
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = i + 2; j < n; ++j) {
        if (i == 0 && j == n - 1) continue;  // adjacent across the closing corner
        double a[2], b[2], c[2], d[2];
        corner_m(room, i, a);
        corner_m(room, (i + 1) % n, b);
        corner_m(room, j, c);
        corner_m(room, (j + 1) % n, d);
        if (segments_meet(a, b, c, d)) {
          out.push_back(
              fmt("room '%s': walls %zu and %zu cross; the footprint must be a simple "
                  "polygon",
                  rn, i, j));
        }
      }
    }
  }
  for (size_t k = 0; k < level.portals.size(); ++k) {
    const Portal& p = level.portals[k];
    if (p.room >= level.rooms.size()) {
      out.push_back(fmt("portal %zu: room %u out of range", k, unsigned(p.room)));
      continue;
    }
    const Room& room = level.rooms[p.room];
    const char* rn = room.name.c_str();
    if (room.corners.size() < 3 || p.wall >= room.corners.size()) {
      out.push_back(fmt("portal %zu: room '%s' has no wall %u", k, rn, unsigned(p.wall)));
      continue;
    }
    if (p.trim != kNoMaterial && !mat_ok(p.trim)) {
      out.push_back(fmt("portal %zu: trim material %u out of range", k, unsigned(p.trim)));
    }
    const double len = wall_length_m(room, p.wall);
    if (p.width <= 0 || p.offset < 0 || metres(p.offset + p.width) > len + kNearM) {
      out.push_back(
          fmt("portal %zu: the opening %.3f..%.3f m does not fit wall %u of room '%s' "
              "(%.3f m long)",
              k, metres(p.offset), metres(p.offset + p.width), unsigned(p.wall), rn, len));
    }
    if (p.sill < 0 || p.head <= p.sill || int(room.floor_y) + p.head > room.ceil_y) {
      out.push_back(
          fmt("portal %zu: sill %.3f m and head %.3f m do not fit room '%s' "
              "(%.3f m high)",
              k, metres(p.sill), metres(p.head), rn, metres(room.ceil_y - room.floor_y)));
    }
    if (p.to == kOutside) continue;
    if (p.to >= level.rooms.size() || p.to == p.room) {
      out.push_back(fmt("portal %zu: it opens into room %u; name another room or 'outside'", k,
                        unsigned(p.to)));
      continue;
    }
    const Room& to = level.rooms[p.to];
    if (to.corners.size() < 3) continue;
    if ((room.shape == Shape::cave || to.shape == Shape::cave) && p.trim == kNoMaterial) {
      out.push_back(
          fmt("portal %zu: it opens into a cave without a frame; give it trim=<material> "
              "to cover the rock's edge",
              k));
    }
    std::string why;
    if (far_wall(level, p, &why) < 0) out.push_back(fmt("portal %zu: %s", k, why.c_str()));
    const int64_t lo = int64_t(room.origin[1]) + room.floor_y + p.sill;
    const int64_t hi = int64_t(room.origin[1]) + room.floor_y + p.head;
    if (lo < int64_t(to.origin[1]) + to.floor_y || hi > int64_t(to.origin[1]) + to.ceil_y) {
      out.push_back(fmt("portal %zu: the opening's height does not fit room '%s' on its far side",
                        k, to.name.c_str()));
    }
  }
  for (size_t k = 0; k < level.solids.size(); ++k) {
    const Solid& s = level.solids[k];
    if (s.room >= level.rooms.size()) {
      out.push_back(fmt("solid %zu: room %u out of range", k, unsigned(s.room)));
      continue;
    }
    if (!mat_ok(s.material)) out.push_back(fmt("solid %zu: material out of range", k));
    if (s.min[0] >= s.max[0] || s.min[1] >= s.max[1] || s.min[2] >= s.max[2]) {
      out.push_back(fmt("solid %zu: min is not below max on every axis", k));
    }
  }
  return out;
}

bool link(Level& level, std::string* error) {
  const auto problems = validate(level);
  if (!problems.empty()) {
    if (error) *error = problems.front();
    return false;
  }
  for (Portal& p : level.portals) {
    std::string why;
    p.to_wall = p.to == kOutside ? 0 : uint8_t(far_wall(level, p, &why));
  }
  return true;
}

}  // namespace ge::rooms
