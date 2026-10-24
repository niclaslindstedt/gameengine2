// The version 29 and BSP2 fixture writer: the compile laid out in fifteen lumps, with 64 × 64
// miptex (four levels, palette indices in a pattern of their own), one texinfo and one grey
// lightmap a face, leaf-numbered visibility rows, and the point tree written again as the clip
// hulls' nodes.
//
// ge:summary Writes a fixture as a version 29 or BSP2 map: miptex, faces, grey luxels, leaf rows
// ge:keywords write v29 bsp2 fixture miptex texinfo faces edges surfedges lighting clipnodes leafs
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <array>

#include "brush/write_parts.hpp"

namespace ge::brush::detail {

namespace {

// 64 × 64 pixels and their three smaller levels: a pattern of two palette indices told by the
// texture's number.
void miptex(Out& o, const std::string& name, uint32_t k) {
  const uint32_t size = 64;
  o.name(name, 16);
  o.u32(size);
  o.u32(size);
  uint32_t at = 40;
  for (int level = 0; level < 4; ++level) {
    o.u32(at);
    at += (size >> level) * (size >> level);
  }
  const auto a = uint8_t(16 + 16 * (k % 12)), b = uint8_t(a + 6);
  for (int level = 0; level < 4; ++level) {
    const uint32_t s = size >> level, cell = 8u >> level;
    for (uint32_t y = 0; y < s; ++y) {
      for (uint32_t x = 0; x < s; ++x) o.u8(((x / cell + y / cell + k) % 2) ? a : b);
    }
  }
}

}  // namespace

bool write_v29(const Fixture& fx, const Compiled& c, bool bsp2, Out* out, std::string* err) {
  (void)fx;
  (void)err;
  constexpr int kLumps = 15;
  std::array<Out, kLumps> L;
  Out& entities = L[0];
  Out& planes = L[1];
  Out& textures = L[2];
  Out& vertexes = L[3];
  Out& visibility = L[4];
  Out& nodes = L[5];
  Out& texinfo = L[6];
  Out& faces = L[7];
  Out& lighting = L[8];
  Out& clipnodes = L[9];
  Out& leafs = L[10];
  Out& marksurfaces = L[11];
  Out& edges = L[12];
  Out& surfedges = L[13];
  Out& models = L[14];

  for (char ch : c.entities) entities.u8(uint8_t(ch));
  entities.u8(0);

  for (const CPlane& p : c.planes) {
    for (int k = 0; k < 3; ++k) planes.f32(k == p.axis ? 1.0f : 0.0f);
    planes.f32(p.d);
    planes.i32(p.axis);
  }

  textures.i32(int32_t(c.textures.size()));
  std::vector<Out> records(c.textures.size());
  uint32_t at = 4 + 4 * uint32_t(c.textures.size());
  for (size_t k = 0; k < c.textures.size(); ++k) {
    miptex(records[k], c.textures[k], uint32_t(k));
    textures.i32(int32_t(at));
    at += uint32_t(records[k].b.size());
  }
  for (const Out& r : records) textures.bytes(r.b);

  // Edge 0 is never used: a surfedge's sign could not tell it reversed.
  edges.u32(0);
  if (bsp2) edges.u32(0);
  uint32_t vertex_count = 0, edge_count = 1, surfedge_count = 0;
  for (size_t i = 0; i < c.faces.size(); ++i) {
    const CFace& f = c.faces[i];
    for (int a = 0; a < 2; ++a) {
      for (int k = 0; k < 4; ++k) texinfo.f32(f.vecs[a][k]);
    }
    texinfo.i32(int32_t(f.texture));
    texinfo.i32(0);

    const uint32_t first_edge = surfedge_count;
    const auto m = uint32_t(f.poly.size());
    for (uint32_t k = 0; k < m; ++k) {
      for (float x : f.poly[k]) vertexes.f32(x);
      const uint32_t a = vertex_count + k, b = vertex_count + (k + 1) % m;
      if (bsp2) {
        edges.u32(a);
        edges.u32(b);
      } else {
        edges.u16(uint16_t(a));
        edges.u16(uint16_t(b));
      }
      surfedges.i32(int32_t(edge_count++));
      ++surfedge_count;
    }
    vertex_count += m;

    const auto lightofs = int32_t(lighting.b.size());
    lighting.bytes(f.luxels);
    if (bsp2) {
      faces.i32(int32_t(f.plane));
      faces.i32(f.back ? 1 : 0);
      faces.i32(int32_t(first_edge));
      faces.i32(int32_t(m));
      faces.i32(int32_t(i));
    } else {
      faces.i16(int32_t(f.plane));
      faces.i16(f.back ? 1 : 0);
      faces.i32(int32_t(first_edge));
      faces.i16(int32_t(m));
      faces.i16(int32_t(i));
    }
    for (uint8_t s : {uint8_t(0), uint8_t(255), uint8_t(255), uint8_t(255)}) faces.u8(s);
    faces.i32(lightofs);
  }

  const size_t rooms = c.room_faces.size();
  std::vector<int32_t> visofs(rooms);
  for (size_t r = 0; r < rooms; ++r) {
    visofs[r] = int32_t(visibility.b.size());
    visibility.bytes(rle_row(vis_row(c, uint32_t(r))));
  }

  for (const CNode& n : c.nodes) {
    nodes.i32(int32_t(n.plane));
    for (int32_t ch : n.child) bsp2 ? nodes.i32(ch) : nodes.i16(ch);
    for (const float* b : {n.mins, n.maxs}) {
      for (int k = 0; k < 3; ++k) bsp2 ? nodes.f32(b[k]) : nodes.i16(int32_t(b[k]));
    }
    if (bsp2) {
      nodes.u32(0);
      nodes.u32(0);
    } else {
      nodes.u16(0);
      nodes.u16(0);
    }
    // The clip hulls: the same tree, its leaves as contents (−2 solid, −1 empty).
    clipnodes.i32(int32_t(n.plane));
    for (int32_t ch : n.child) {
      const int32_t v = ch >= 0 ? ch : (ch == -1 ? -2 : -1);
      bsp2 ? clipnodes.i32(v) : clipnodes.i16(v);
    }
  }

  const auto leaf = [&](int32_t contents, int32_t vis, const float* lo, const float* hi,
                        uint32_t first_mark, uint32_t marks) {
    leafs.i32(contents);
    leafs.i32(vis);
    for (const float* b : {lo, hi}) {
      for (int k = 0; k < 3; ++k) bsp2 ? leafs.f32(b[k]) : leafs.i16(int32_t(b[k]));
    }
    if (bsp2) {
      leafs.u32(first_mark);
      leafs.u32(marks);
    } else {
      leafs.u16(uint16_t(first_mark));
      leafs.u16(uint16_t(marks));
    }
    for (int k = 0; k < 4; ++k) leafs.u8(0);
  };
  const float zero[3] = {0, 0, 0};
  leaf(-2, -1, zero, zero, 0, 0);
  uint32_t marks = 0;
  for (size_t r = 0; r < rooms; ++r) {
    const FixtureRoom& rm = fx.rooms[r];
    const float lo[3] = {float(rm.min[0]), float(rm.min[1]), float(rm.min[2])};
    const float hi[3] = {float(rm.max[0]), float(rm.max[1]), float(rm.max[2])};
    leaf(-1, visofs[r], lo, hi, marks, uint32_t(c.room_faces[r].size()));
    for (uint32_t f : c.room_faces[r]) bsp2 ? marksurfaces.u32(f) : marksurfaces.u16(uint16_t(f));
    marks += uint32_t(c.room_faces[r].size());
  }

  for (const float* b : {c.mins, c.maxs}) {
    for (int k = 0; k < 3; ++k) models.f32(b[k]);
  }
  for (int k = 0; k < 3; ++k) models.f32(0);
  for (int h = 0; h < 4; ++h) models.i32(0);
  models.i32(int32_t(rooms));
  models.i32(0);
  models.i32(int32_t(c.faces.size()));

  // The header, then every lump on a four-byte boundary.
  Out& o = *out;
  if (bsp2) {
    o.name("BSP2", 4);
  } else {
    o.i32(29);
  }
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
