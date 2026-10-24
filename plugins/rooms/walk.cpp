// The tour's doorways and its routes on each room's grid, and the drive that walks it.
//
// ge:summary The tour's doorway points, grid routes found by the mover's sweeps, and the drive
// ge:keywords rooms walk tour route grid probe stand step-height doorway drive patience trail
// ge:skill rooms move
// ge:test tests/rooms_clip_test.cpp
#include "rooms/walk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "ai/path.hpp"
#include "core/libm/libm.hpp"
#include "rooms/clip.hpp"

namespace ge::rooms {

namespace {

using move::Vec3;

constexpr double kNone = std::numeric_limits<double>::quiet_NaN();
constexpr double kInside = 0.9;  // m: a doorway's points stand this far into each room
constexpr double kProbe = 1.2;   // m over the floor a probe is dropped from
constexpr double kLift = 0.05;   // m: the hull swept this far over the higher of two cells
constexpr double kDrop = 0.5;    // m: the deepest step down a route takes

double floor_of(const Room& room) { return metres(int64_t(room.origin[1]) + room.floor_y); }

std::string at_text(const Vec3& p) {
  char b[64];
  std::snprintf(b, sizeof b, "(%.2f, %.2f, %.2f)", p.x, p.y, p.z);
  return b;
}

struct Planner {
  const Level& level;
  const move::World& world;
  const move::MoveSpec& spec;
  double cell;
  ai::Search search;

  // Where the body stands at (x, z) of a room whose floor is at `y0`: NaN where it cannot.
  double stand_height(double x, double z, double y0) const {
    const move::Box thin{{-spec.radius, 0, -spec.radius}, {spec.radius, 0.1, spec.radius}};
    const move::Hit down = world.sweep(thin, {x, y0 + kProbe, z}, {x, y0 - 0.6, z});
    if (down.start_solid || !down.touched() || down.normal.y < spec.walk_normal) return kNone;
    const double h = down.end.y;
    const Vec3 at{x, h + kLift, z};
    if (world.sweep(move::hull(spec, false), at, at).start_solid) return kNone;
    return h;
  }

  // Whether the hull gets from (a, ha) to (b, hb) as a walk would: over the higher of the two,
  // the ground under every cell of the way within a step of the straight line between them.
  bool clear(const Vec3& a, const Vec3& b, double y0) const {
    if (b.y - a.y > spec.step_height || a.y - b.y > kDrop) return false;
    const move::Box h = move::hull(spec, false);
    const double y = std::max(a.y, b.y) + kLift;
    if (a.y + kLift < y && world.sweep(h, {a.x, a.y + kLift, a.z}, {a.x, y, a.z}).touched()) {
      return false;
    }
    if (world.sweep(h, {a.x, y, a.z}, {b.x, y, b.z}).touched()) return false;
    const double run = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
    const auto n = static_cast<int>(run / cell);
    for (int i = 1; i < n; ++i) {
      const double t = double(i) / double(n);
      const double g = stand_height(a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, y0);
      const double line = a.y + (b.y - a.y) * t;
      if (std::isnan(g) || g - line > spec.step_height || line - g > kDrop) return false;
    }
    return true;
  }

  // Whether (x, z) is within a hull's reach of a solid of room `r` (a closed box the probes
  // cannot tell from open floor: nothing inside it is a surface).
  bool in_solid(int32_t r, double x, double z) const {
    const Room& room = level.rooms[size_t(r)];
    const double m = spec.radius + cell;
    for (const Solid& o : level.solids) {
      if (o.room != r) continue;
      const double x0 = metres(int64_t(room.origin[0]) + o.min[0]) - m;
      const double x1 = metres(int64_t(room.origin[0]) + o.max[0]) + m;
      const double z0 = metres(int64_t(room.origin[2]) + o.min[2]) - m;
      const double z1 = metres(int64_t(room.origin[2]) + o.max[2]) + m;
      if (x > x0 && x < x1 && z > z0 && z < z1) return true;
    }
    return false;
  }

  // The room's middle: the standing spot nearest its footprint's centroid (for an L, the
  // nearest spot inside it) clear of its solids, on a grid of `cell`.
  Vec3 middle(int32_t r) const {
    const Room& room = level.rooms[size_t(r)];
    const double y0 = floor_of(room);
    double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300}, mid[2] = {0, 0};
    const auto n = double(room.corners.size());
    for (size_t i = 0; i < room.corners.size(); ++i) {
      double c[2];
      corner_m(room, i, c);
      for (int k = 0; k < 2; ++k) {
        lo[k] = std::min(lo[k], c[k]);
        hi[k] = std::max(hi[k], c[k]);
        mid[k] += c[k] / n;
      }
    }
    Vec3 best{mid[0], y0, mid[1]};
    double best_d = 1e300;
    const int32_t cols = static_cast<int32_t>((hi[0] - lo[0]) / cell);
    const int32_t rows = static_cast<int32_t>((hi[1] - lo[1]) / cell);
    for (int32_t j = 0; j < rows; ++j) {
      for (int32_t i = 0; i < cols; ++i) {
        const double x = lo[0] + (double(i) + 0.5) * cell, z = lo[1] + (double(j) + 0.5) * cell;
        const double d = (x - mid[0]) * (x - mid[0]) + (z - mid[1]) * (z - mid[1]);
        if (d >= best_d || room_at(level, {x, y0 + 0.3, z}) != r || in_solid(r, x, z)) continue;
        const double h = stand_height(x, z, y0);
        if (std::isnan(h)) continue;
        best = {x, h, z};
        best_d = d;
      }
    }
    return best;
  }

  // The way inside room `r` from `from` to `to` (both on its floor), string-pulled: the points
  // after `from`, `to` last. Empty with `*error` set when there is none.
  std::vector<Vec3> route(int32_t r, const Vec3& from, const Vec3& to, std::string* error) {
    const Room& room = level.rooms[size_t(r)];
    const double y0 = floor_of(room);
    double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
    for (size_t i = 0; i < room.corners.size(); ++i) {
      double c[2];
      corner_m(room, i, c);
      for (int k = 0; k < 2; ++k) {
        lo[k] = std::min(lo[k], c[k]);
        hi[k] = std::max(hi[k], c[k]);
      }
    }
    ai::Grid grid;
    grid.cell = cell;
    grid.cols = static_cast<int32_t>((hi[0] - lo[0]) / cell) + 1;
    grid.rows = static_cast<int32_t>((hi[1] - lo[1]) / cell) + 1;
    const auto centre = [&](int32_t k) {
      const int32_t col = k % grid.cols, row = k / grid.cols;
      return Vec3{lo[0] + (double(col) + 0.5) * cell, y0, lo[1] + (double(row) + 0.5) * cell};
    };
    const auto cell_of = [&](const Vec3& p) {
      const int32_t i = std::clamp(static_cast<int32_t>((p.x - lo[0]) / cell), 0, grid.cols - 1);
      const int32_t j = std::clamp(static_cast<int32_t>((p.z - lo[1]) / cell), 0, grid.rows - 1);
      return j * grid.cols + i;
    };
    const size_t cells = size_t(grid.cols) * size_t(grid.rows);
    std::vector<double> height(cells, 0);
    std::vector<uint8_t> known(cells, 0);
    const int32_t a = cell_of(from), b = cell_of(to);
    const auto at_cell = [&](int32_t k) { return k == a ? from : k == b ? to : centre(k); };
    const auto stand = [&](int32_t k) {
      if (!known[size_t(k)]) {
        known[size_t(k)] = 1;
        const Vec3 c = at_cell(k);
        const bool ends = k == a || k == b;
        double h =
            ends || room_at(level, {c.x, y0 + 0.3, c.z}) == r ? stand_height(c.x, c.z, y0) : kNone;
        if (std::isnan(h) && ends) h = c.y;  // the ends are where the tour says, standing or not
        height[size_t(k)] = h;
      }
      return height[size_t(k)];
    };
    const auto point = [&](int32_t k) {
      const Vec3 c = at_cell(k);
      return Vec3{c.x, stand(k), c.z};
    };
    const ai::GridPath path = ai::grid_path(
        grid, a, b,
        [&](int32_t p, int32_t q, double length) {
          if (std::isnan(stand(p)) || std::isnan(stand(q))) return ai::kUnreached;
          return clear(point(p), point(q), y0) ? length : ai::kUnreached;
        },
        1.0, search);
    if (path.cells.empty()) {
      *error = "no way in room '" + room.name + "' from " + at_text(from) + " to " + at_text(to) +
               "; next: widen the room or move what blocks it (make lab LAB=rooms draws it)";
      return {};
    }
    std::vector<Vec3> pts;
    pts.reserve(path.cells.size());
    for (const int32_t k : path.cells) pts.push_back(point(k));
    std::vector<Vec3> pulled =
        ai::string_pull<Vec3>(pts, [&](size_t i, size_t j) { return clear(pts[i], pts[j], y0); });
    pulled.erase(pulled.begin());
    if (pulled.empty()) pulled.push_back(to);  // already in the goal's cell
    pulled.back() = Vec3{to.x, pulled.back().y, to.z};
    return pulled;
  }
};

// A doorway's two points: `in_room` stands in the portal's own room, `in_to` in the room it
// opens into, each `kInside` m from the wall on the doorway's middle, on its room's floor.
void doorway(const Level& level, const Portal& p, Vec3* in_room, Vec3* in_to) {
  const Room& room = level.rooms[p.room];
  double c0[2], c1[2];
  corner_m(room, p.wall, c0);
  corner_m(room, (size_t(p.wall) + 1) % room.corners.size(), c1);
  const double len =
      std::sqrt((c1[0] - c0[0]) * (c1[0] - c0[0]) + (c1[1] - c0[1]) * (c1[1] - c0[1]));
  const double d[2] = {(c1[0] - c0[0]) / len, (c1[1] - c0[1]) / len};
  const double along = metres(p.offset) + metres(p.width) / 2;
  const double mid[2] = {c0[0] + d[0] * along, c0[1] + d[1] * along};
  const double left[2] = {-d[1], d[0]};  // into the room: the footprint is counter-clockwise
  *in_room = {mid[0] + left[0] * kInside, floor_of(room), mid[1] + left[1] * kInside};
  *in_to = {mid[0] - left[0] * kInside, floor_of(level.rooms[p.to]), mid[1] - left[1] * kInside};
}

}  // namespace

Tour tour(const Level& level, const move::World& world, const move::MoveSpec& spec,
          const Vec3& start, double cell) {
  Tour out;
  Planner plan{level, world, spec, cell, ai::Search{}};
  const int32_t first = room_at(level, {start.x, start.y + 0.3, start.z});
  if (first < 0) {
    out.error =
        "the tour's start " + at_text(start) + " is in no room; next: start on a room's floor";
    return out;
  }
  std::vector<uint8_t> seen(level.rooms.size(), 0);
  out.points.push_back({start, first});
  Vec3 at = start;
  // Depth first: out through each doorway to a room not yet seen, round it, and back.
  const auto leg = [&](int32_t room, const Vec3& to) {
    const std::vector<Vec3> way = plan.route(room, at, to, &out.error);
    for (const Vec3& v : way) out.points.push_back({v, room});
    if (!way.empty()) at = to;
    return !way.empty();
  };
  const auto go = [&](auto&& self, int32_t r) -> bool {
    seen[size_t(r)] = 1;
    if (!leg(r, plan.middle(r))) return false;  // into the room's middle first
    for (const Portal& p : level.portals) {
      if (p.to == kOutside || (p.room != r && p.to != r)) continue;
      const int32_t other = p.room == r ? int32_t(p.to) : int32_t(p.room);
      if (seen[size_t(other)]) continue;
      Vec3 in_room, in_to;
      doorway(level, p, &in_room, &in_to);
      const Vec3 here = p.room == r ? in_room : in_to;
      const Vec3 there = p.room == r ? in_to : in_room;
      if (!leg(r, here)) return false;
      out.points.push_back({there, other});
      at = there;
      if (!self(self, other)) return false;
      if (!leg(other, there)) return false;
      out.points.push_back({here, r});
      at = here;
    }
    return true;
  };
  go(go, first);
  return out;
}

Walk walk(const Level& level, const move::World& world, const move::MoveSpec& spec,
          const Tour& tour, const WalkOptions& options) {
  Walk out;
  if (tour.points.empty()) {
    out.error = "the tour has no points; next: read the tour's error";
    return out;
  }
  move::MoveState s = move::stand_at(spec, world, tour.points[0].at);
  const auto note = [&]() {
    const int32_t r = room_at(level, {s.pos.x, s.pos.y + 0.5, s.pos.z});
    if (r < 0) return;
    if (std::find(out.rooms.begin(), out.rooms.end(), r) == out.rooms.end()) out.rooms.push_back(r);
    out.lowest_below_floor =
        std::max(out.lowest_below_floor, floor_of(level.rooms[size_t(r)]) - s.pos.y);
  };
  note();
  out.trail.push_back(s.pos);
  out.reached = 1;
  const auto limit = static_cast<size_t>(options.patience / options.dt);
  for (size_t i = 1; i < tour.points.size(); ++i) {
    const Vec3& target = tour.points[i].at;
    size_t took = 0;
    for (;;) {
      const double dx = target.x - s.pos.x, dz = target.z - s.pos.z;
      if (dx * dx + dz * dz <= options.reach * options.reach) break;
      if (took++ >= limit) {
        out.error = "stuck at " + at_text(s.pos) + " short of tour point " + std::to_string(i) +
                    " " + at_text(target) + " after " + std::to_string(options.patience) +
                    " s; next: draw the walk (make lab LAB=rooms) and widen what holds it";
        out.end = s;
        return out;
      }
      move::MoveCmd cmd;
      cmd.forward = 1;
      cmd.yaw = core::libm::atan2(dx, dz);
      cmd.sprint = options.sprint;
      move::step_move(spec, world, s, cmd, options.dt);
      ++out.steps;
      note();
      if (out.steps % options.trail_every == 0) out.trail.push_back(s.pos);
    }
    ++out.reached;
  }
  out.trail.push_back(s.pos);
  out.end = s;
  return out;
}

}  // namespace ge::rooms
