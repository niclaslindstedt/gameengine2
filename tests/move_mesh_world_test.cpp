// The mesh world (engine/move/mesh_world.hpp): a box dropped onto a floor of two triangles stops a
// skin above it facing up; one swept at a wall of triangles stops short facing back; a box passing
// beyond a triangle's slanted edge misses it where a face-only test would not; the inside of a
// room of twelve triangles traces as the box world's room of six slabs does; and the grid answers
// as one cell holding everything does.
//
// ge:summary Tests: the mesh world's swept-box trace (floor, wall, edges, box-world parity, grid)
// ge:keywords test move mesh-world triangle trace sweep separating-axis floor wall edge grid
// ge:skill move testing
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "check.hpp"
#include "move/box_world.hpp"
#include "move/mesh_world.hpp"

namespace mv = ge::move;
using mv::Vec3;

namespace {

const mv::Box kHull{{-16, 0, -16}, {16, 56, 16}};

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

void quad(std::vector<Vec3>& out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
  out.insert(out.end(), {a, b, c, a, c, d});
}

// The six inside faces of the box lo..hi, two triangles each.
std::vector<Vec3> room_faces(const Vec3& lo, const Vec3& hi) {
  std::vector<Vec3> t;
  quad(t, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z});
  quad(t, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z});
  quad(t, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z});
  quad(t, {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z});
  quad(t, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {lo.x, lo.y, hi.z});
  quad(t, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z});
  return t;
}

// A small deterministic stream for the sweeps (the test's own, not a simulation's).
struct Lcg {
  uint64_t s = 0x9e3779b97f4a7c15ull;
  double next(double lo, double hi) {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return lo + (hi - lo) * static_cast<double>(s >> 11) / 9007199254740992.0;
  }
};

}  // namespace

TEST(a_box_dropped_on_a_floor_of_triangles_stops_a_skin_above_it_facing_up) {
  std::vector<Vec3> t;
  quad(t, {-1000, 0, -1000}, {1000, 0, -1000}, {1000, 0, 1000}, {-1000, 0, 1000});
  const mv::MeshWorld w = mv::build_mesh_world(t, 64);
  const mv::Hit h = mv::trace_mesh_world(w, kHull, {0, 100, 0}, {0, -100, 0});
  CHECK(h.touched() && !h.start_solid);
  CHECK_MSG(near(h.end.y, w.skin, 1e-9), "stopped at y " + std::to_string(h.end.y));
  CHECK(near(h.normal.y, 1, 1e-12));
}

TEST(a_box_swept_at_a_wall_of_triangles_stops_short_facing_back) {
  std::vector<Vec3> t;
  quad(t, {-100, 0, 200}, {100, 0, 200}, {100, 100, 200}, {-100, 100, 200});
  const mv::MeshWorld w = mv::build_mesh_world(t, 64);
  const mv::Hit h = mv::trace_mesh_world(w, kHull, {0, 1, 0}, {0, 1, 400});
  CHECK(h.touched());
  CHECK_MSG(near(h.end.z, 200 - 16 - w.skin, 1e-9), "stopped at z " + std::to_string(h.end.z));
  CHECK(near(h.normal.z, -1, 1e-12));
}

TEST(a_box_beyond_a_slanted_edge_misses_the_triangle) {
  // A triangle standing in the plane z = 0 with its slanted edge along x + y = 100.
  const std::vector<Vec3> t = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}};
  const mv::MeshWorld w = mv::build_mesh_world(t, 64);
  const mv::Box cube{{-16, -16, -16}, {16, 16, 16}};
  // At (70, 70) the cube's nearest corner is at (54, 54): past the edge, though inside the
  // triangle's bounds and plane. A face-and-bounds test would stop it.
  const mv::Hit past = mv::trace_mesh_world(w, cube, {70, 70, -50}, {70, 70, 50});
  CHECK_MSG(!past.touched(), "touched at fraction " + std::to_string(past.fraction));
  const mv::Hit inside = mv::trace_mesh_world(w, cube, {30, 30, -50}, {30, 30, 50});
  CHECK(inside.touched());
  CHECK(near(inside.end.z, -16 - w.skin, 1e-9));
  // A cube standing across the triangle starts solid and is let out.
  const mv::Hit edge = mv::trace_mesh_world(w, cube, {20, 20, 0}, {20, 200, 0});
  CHECK(edge.start_solid);
}

TEST(a_room_of_triangles_traces_as_the_box_worlds_room_of_slabs) {
  const Vec3 lo{0, 0, 0}, hi{400, 200, 300};
  const mv::MeshWorld mesh = mv::build_mesh_world(room_faces(lo, hi), 64);
  mv::BoxWorld boxes;
  mv::add_box(boxes, {-50, -50, -50}, {450, 0, 350});
  mv::add_box(boxes, {-50, 200, -50}, {450, 250, 350});
  mv::add_box(boxes, {-50, -50, -50}, {0, 250, 350});
  mv::add_box(boxes, {400, -50, -50}, {450, 250, 350});
  mv::add_box(boxes, {-50, -50, -50}, {450, 250, 0});
  mv::add_box(boxes, {-50, -50, 300}, {450, 250, 350});
  Lcg r;
  int worst = 0;
  double worst_gap = 0;
  for (int i = 0; i < 400; ++i) {
    const Vec3 from{r.next(17, 383), r.next(1, 143), r.next(17, 283)};
    const Vec3 to{r.next(-200, 600), r.next(-200, 400), r.next(-200, 500)};
    const mv::Hit a = mv::trace_mesh_world(mesh, kHull, from, to);
    const mv::Hit b = mv::trace_box_world(boxes, kHull, from, to);
    const double gap = std::fabs(a.fraction - b.fraction);
    if (gap > worst_gap) {
      worst_gap = gap;
      worst = i;
    }
    CHECK(!a.start_solid);
  }
  CHECK_MSG(worst_gap < 1e-9,
            "sweep " + std::to_string(worst) + " differs by " + std::to_string(worst_gap));
}

TEST(the_grid_answers_as_one_cell_holding_everything_does) {
  // A bumpy floor of 40 x 40 tiles: many triangles, many cells.
  std::vector<Vec3> t;
  const auto h = [](int i, int k) { return 4.0 * ((i * 7 + k * 13) % 5); };
  for (int k = 0; k < 40; ++k) {
    for (int i = 0; i < 40; ++i) {
      const double x = i * 25.0, z = k * 25.0;
      quad(t, {x, h(i, k), z}, {x + 25, h(i + 1, k), z}, {x + 25, h(i + 1, k + 1), z + 25},
           {x, h(i, k + 1), z + 25});
    }
  }
  const mv::MeshWorld grid = mv::build_mesh_world(t, 32);
  const mv::MeshWorld one = mv::build_mesh_world(t, 1e6);
  CHECK(grid.dim[0] > 10 && one.dim[0] == 1);
  Lcg r;
  for (int i = 0; i < 300; ++i) {
    const Vec3 from{r.next(0, 1000), r.next(30, 120), r.next(0, 1000)};
    const Vec3 to{r.next(-100, 1100), r.next(-50, 150), r.next(-100, 1100)};
    const mv::Hit a = mv::trace_mesh_world(grid, kHull, from, to);
    const mv::Hit b = mv::trace_mesh_world(one, kHull, from, to);
    CHECK(a.fraction == b.fraction && a.start_solid == b.start_solid);
  }
}
