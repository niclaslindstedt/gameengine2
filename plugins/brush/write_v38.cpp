// The version 38 fixture writer: the compile laid out in nineteen lumps, one texinfo a face
// naming its texture (`fixture/<name>`), RGB luxels, leaves naming their clusters and area, and
// the visibility's cluster count, offsets and run-length rows.
//
// ge:summary Writes a fixture as a version 38 map: named texinfo, RGB luxels, clusters and rows
// ge:keywords write v38 fixture texinfo texture name rgb lighting leafs cluster area visibility
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <array>

#include "brush/write_parts.hpp"

namespace ge::brush::detail {

bool write_v38(const Fixture& fx, const Compiled& c, Out* out, std::string* err) {
  (void)err;
  constexpr int kLumps = 19;
  std::array<Out, kLumps> L;
  Out& entities = L[0];
  Out& planes = L[1];
  Out& vertices = L[2];
  Out& visibility = L[3];
  Out& nodes = L[4];
  Out& texinfo = L[5];
  Out& faces = L[6];
  Out& lighting = L[7];
  Out& leafs = L[8];
  Out& leaffaces = L[9];
  Out& edges = L[11];
  Out& surfedges = L[12];
  Out& models = L[13];
  Out& areas = L[17];

  for (char ch : c.entities) entities.u8(uint8_t(ch));
  entities.u8(0);

  for (const CPlane& p : c.planes) {
    for (int k = 0; k < 3; ++k) planes.f32(k == p.axis ? 1.0f : 0.0f);
    planes.f32(p.d);
    planes.i32(p.axis);
  }

  edges.u32(0);
  uint32_t vertex_count = 0, edge_count = 1, surfedge_count = 0;
  for (size_t i = 0; i < c.faces.size(); ++i) {
    const CFace& f = c.faces[i];
    for (int a = 0; a < 2; ++a) {
      for (int k = 0; k < 4; ++k) texinfo.f32(f.vecs[a][k]);
    }
    texinfo.i32(0);
    texinfo.i32(0);
    texinfo.name("fixture/" + c.textures[f.texture], 32);
    texinfo.i32(-1);

    const uint32_t first_edge = surfedge_count;
    const auto m = uint32_t(f.poly.size());
    for (uint32_t k = 0; k < m; ++k) {
      for (float x : f.poly[k]) vertices.f32(x);
      edges.u16(uint16_t(vertex_count + k));
      edges.u16(uint16_t(vertex_count + (k + 1) % m));
      surfedges.i32(int32_t(edge_count++));
      ++surfedge_count;
    }
    vertex_count += m;

    const auto lightofs = int32_t(lighting.b.size());
    for (uint8_t g : f.luxels) {
      for (int k = 0; k < 3; ++k) lighting.u8(g);
    }
    faces.u16(uint16_t(f.plane));
    faces.i16(f.back ? 1 : 0);
    faces.i32(int32_t(first_edge));
    faces.i16(int32_t(m));
    faces.i16(int32_t(i));
    for (uint8_t s : {uint8_t(0), uint8_t(255), uint8_t(255), uint8_t(255)}) faces.u8(s);
    faces.i32(lightofs);
  }

  // The visibility: the count, each cluster's two offsets (the same row twice), the rows.
  const size_t rooms = c.room_faces.size();
  std::vector<std::vector<uint8_t>> rows(rooms);
  visibility.i32(int32_t(rooms));
  auto row_at = uint32_t(4 + 8 * rooms);
  for (size_t r = 0; r < rooms; ++r) {
    rows[r] = rle_row(vis_row(c, uint32_t(r)));
    visibility.i32(int32_t(row_at));
    visibility.i32(int32_t(row_at));
    row_at += uint32_t(rows[r].size());
  }
  for (const auto& row : rows) visibility.bytes(row);

  for (const CNode& n : c.nodes) {
    nodes.i32(int32_t(n.plane));
    for (int32_t ch : n.child) nodes.i32(ch);
    for (const float* b : {n.mins, n.maxs}) {
      for (int k = 0; k < 3; ++k) nodes.i16(int32_t(b[k]));
    }
    nodes.u16(0);
    nodes.u16(0);
  }

  const auto leaf = [&](int32_t contents, int32_t cluster, int32_t area, const float* lo,
                        const float* hi, uint32_t first, uint32_t count) {
    leafs.i32(contents);
    leafs.i16(cluster);
    leafs.i16(area);
    for (const float* b : {lo, hi}) {
      for (int k = 0; k < 3; ++k) leafs.i16(int32_t(b[k]));
    }
    leafs.u16(uint16_t(first));
    leafs.u16(uint16_t(count));
    leafs.u16(0);
    leafs.u16(0);
  };
  const float zero[3] = {0, 0, 0};
  leaf(1, -1, 0, zero, zero, 0, 0);
  uint32_t first = 0;
  for (size_t r = 0; r < rooms; ++r) {
    const FixtureRoom& rm = fx.rooms[r];
    const float lo[3] = {float(rm.min[0]), float(rm.min[1]), float(rm.min[2])};
    const float hi[3] = {float(rm.max[0]), float(rm.max[1]), float(rm.max[2])};
    leaf(0, int32_t(r), 1, lo, hi, first, uint32_t(c.room_faces[r].size()));
    for (uint32_t f : c.room_faces[r]) leaffaces.u16(uint16_t(f));
    first += uint32_t(c.room_faces[r].size());
  }

  for (const float* b : {c.mins, c.maxs}) {
    for (int k = 0; k < 3; ++k) models.f32(b[k]);
  }
  for (int k = 0; k < 3; ++k) models.f32(0);
  models.i32(0);
  models.i32(0);
  models.i32(int32_t(c.faces.size()));

  // Area 0 is never used; every room is in area 1, with no area portals.
  for (int k = 0; k < 4; ++k) areas.i32(0);

  Out& o = *out;
  o.name("IBSP", 4);
  o.i32(38);
  const size_t dir = o.b.size();
  for (int k = 0; k < kLumps; ++k) {
    o.i32(0);
    o.i32(0);
  }
  for (int k = 0; k < kLumps; ++k) {
    o.align4();
    o.put_i32(dir + size_t(k) * 8, int32_t(o.b.size()));
    o.put_i32(dir + size_t(k) * 8 + 4, int32_t(L[size_t(k)].b.size()));
    o.bytes(L[size_t(k)].b);
  }
  return true;
}

}  // namespace ge::brush::detail
