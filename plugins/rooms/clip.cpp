// The clip world: prisms built flat, caves as grown, every triangle handed to the mesh world.
//
// ge:summary The room level's mover world (flat prisms, grown caves) and a point's room
// ge:keywords rooms clip collision mesh-world flat relief cave room_at point-in-polygon
// ge:skill rooms move
// ge:test tests/rooms_clip_test.cpp
#include "rooms/clip.hpp"

#include <vector>

#include "core/parallel.hpp"
#include "rooms/build.hpp"

namespace ge::rooms {

move::MeshWorld clip_world(const Level& level, size_t workers, double cell_m) {
  Level flat = level;
  for (Material& m : flat.materials) m.relief = Relief::none;
  std::vector<Mesh> meshes(level.rooms.size());
  core::parallel_bands(meshes.size(), workers, [&](size_t begin, size_t end) {
    for (size_t r = begin; r < end; ++r) {
      meshes[r] = build_room(level.rooms[r].shape == Shape::cave ? level : flat, r);
    }
  });
  std::vector<move::Vec3> corners;
  size_t count = 0;
  for (const Mesh& m : meshes) count += m.idx.size();
  corners.reserve(count);
  for (const Mesh& m : meshes) {
    for (const uint32_t i : m.idx) {
      const float* p = &m.pos[size_t(i) * 3];
      corners.push_back({double(p[0]), double(p[1]), double(p[2])});
    }
  }
  move::MeshWorld w = move::build_mesh_world(corners, cell_m);
  // A millimetre: a walker blocked by a step's riser climbs it only once a step's move from rest
  // carries it past the skin, so the skin is kept far under one step's creep.
  w.skin = 1.0 / 1024.0;
  return w;
}

int room_at(const Level& level, const move::Vec3& p) {
  for (size_t r = 0; r < level.rooms.size(); ++r) {
    const Room& room = level.rooms[r];
    const double y0 = metres(int64_t(room.origin[1]) + room.floor_y);
    const double y1 = metres(int64_t(room.origin[1]) + room.ceil_y);
    if (p.y < y0 || p.y > y1) continue;
    // Crossings of a ray along +x with the footprint's edges: odd is inside.
    bool inside = false;
    const size_t n = room.corners.size();
    for (size_t i = 0; i < n; ++i) {
      double a[2], b[2];
      corner_m(room, i, a);
      corner_m(room, (i + 1) % n, b);
      if ((a[1] > p.z) != (b[1] > p.z)) {
        const double x = a[0] + (p.z - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
        if (x > p.x) inside = !inside;
      }
    }
    if (inside) return int(r);
  }
  return -1;
}

}  // namespace ge::rooms
