// The version 46 fixture writer: the compile laid out in seventeen lumps; a shader a texture
// (`textures/fixture/<name>`); every face a polygon whose vertices carry their own texture
// coordinates (64 texels a texture), lightmap coordinates on a 128 × 128 page and normal, fanned
// in the mesh-vertex list; the faces' lightmaps packed into pages by shelves; uncompressed cluster
// rows; and, when the fixture names a vault room, a curved patch of 5 × 3 control points under
// its ceiling, lit on its own block of a page.
//
// ge:summary Writes a fixture as a version 46 map: shaders, vertices, meshverts, pages, a patch
// ge:keywords write v46 fixture shader vertex meshvert face polygon patch vault lightmap page
// ge:keywords visdata packer
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <array>

#include "brush/write_parts.hpp"

namespace ge::brush::detail {

namespace {

constexpr uint32_t kPage = 128;

void vertex(Out& o, const float pos[3], const float st[2], const float lm[2], const float n[3]) {
  for (int k = 0; k < 3; ++k) o.f32(pos[k]);
  for (int k = 0; k < 2; ++k) o.f32(st[k]);
  for (int k = 0; k < 2; ++k) o.f32(lm[k]);
  for (int k = 0; k < 3; ++k) o.f32(n[k]);
  for (int k = 0; k < 4; ++k) o.u8(255);
}

void face(Out& o, int32_t shader, int32_t type, int32_t first_vertex, int32_t vertices,
          int32_t first_meshvert, int32_t meshverts, int32_t page, const uint32_t lm_at[2],
          const uint32_t lm_size[2], const float n[3], int32_t w, int32_t h) {
  o.i32(shader);
  o.i32(-1);
  o.i32(type);
  o.i32(first_vertex);
  o.i32(vertices);
  o.i32(first_meshvert);
  o.i32(meshverts);
  o.i32(page);
  for (int k = 0; k < 2; ++k) o.i32(int32_t(lm_at[k]));
  for (int k = 0; k < 2; ++k) o.i32(int32_t(lm_size[k]));
  for (int k = 0; k < 9; ++k) o.f32(0);  // the lightmap's origin and vectors: the light step's
  for (int k = 0; k < 3; ++k) o.f32(n[k]);
  o.i32(w);
  o.i32(h);
}

// One biquadratic piece across `ctrl` (columns × 3 rows of points) at (u, v), u over every piece.
void patch_point(const std::vector<std::array<float, 3>>& ctrl, int cols, double u, double v,
                 double out[3]) {
  const int pieces = (cols - 1) / 2;
  const int p = std::min(int(u * pieces), pieces - 1);
  const double lu = u * pieces - p;
  const double bu[3] = {(1 - lu) * (1 - lu), 2 * lu * (1 - lu), lu * lu};
  const double bv[3] = {(1 - v) * (1 - v), 2 * v * (1 - v), v * v};
  out[0] = out[1] = out[2] = 0;
  for (int j = 0; j < 3; ++j) {
    for (int i = 0; i < 3; ++i) {
      const auto& q = ctrl[size_t(j) * size_t(cols) + size_t(2 * p + i)];
      for (int k = 0; k < 3; ++k) out[k] += bu[i] * bv[j] * q[size_t(k)];
    }
  }
}

}  // namespace

bool write_v46(const Fixture& fx, const Compiled& c, Out* out, std::string* err) {
  constexpr int kLumps = 17;
  std::array<Out, kLumps> L;
  Out& entities = L[0];
  Out& shaders = L[1];
  Out& planes = L[2];
  Out& nodes = L[3];
  Out& leafs = L[4];
  Out& leaffaces = L[5];
  Out& models = L[7];
  Out& vertexes = L[10];
  Out& meshverts = L[11];
  Out& faces = L[13];
  Out& lightmaps = L[14];
  Out& visdata = L[16];

  for (char ch : c.entities) entities.u8(uint8_t(ch));
  entities.u8(0);

  std::vector<std::string> names = c.textures;
  const bool vault = fx.vault_room >= 0 && size_t(fx.vault_room) < fx.rooms.size();
  if (vault) names.emplace_back("vault");
  for (const std::string& name : names) {
    shaders.name("textures/fixture/" + name, 64);
    shaders.i32(0);
    shaders.i32(1);
  }

  for (const CPlane& p : c.planes) {
    for (int k = 0; k < 3; ++k) planes.f32(k == p.axis ? 1.0f : 0.0f);
    planes.f32(p.d);
  }

  std::vector<std::vector<uint8_t>> pages;
  const auto luxel = [&](uint32_t page, uint32_t x, uint32_t y, uint8_t g) {
    while (pages.size() <= page) pages.emplace_back(size_t(kPage) * kPage * 3, 0);
    for (int k = 0; k < 3; ++k) pages[page][(size_t(y) * kPage + x) * 3 + size_t(k)] = g;
  };
  Packer pack;
  int32_t vertex_count = 0, meshvert_count = 0;
  for (const CFace& f : c.faces) {
    uint32_t page = 0, at[2] = {};
    const uint32_t size[2] = {uint32_t(f.size[0]), uint32_t(f.size[1])};
    if (!pack.place(size[0], size[1], &page, &at[0], &at[1])) {
      if (err) *err = "fixture: a face's lightmap is over 128 luxels; next: make the room smaller";
      return false;
    }
    for (uint32_t j = 0; j < size[1]; ++j) {
      for (uint32_t i = 0; i < size[0]; ++i) {
        luxel(page, at[0] + i, at[1] + j, f.luxels[size_t(j) * size[0] + i]);
      }
    }
    const auto m = int32_t(f.poly.size());
    for (const auto& p : f.poly) {
      float st[2], lm[2];
      for (int a = 0; a < 2; ++a) {
        const double s = double(p[0]) * f.vecs[a][0] + double(p[1]) * f.vecs[a][1] +
                         double(p[2]) * f.vecs[a][2] + f.vecs[a][3];
        st[a] = float(s / 64.0);
        lm[a] = float((double(at[a]) + s / 16.0 - double(f.base[a]) + 0.5) / kPage);
      }
      vertex(vertexes, p.data(), st, lm, f.normal);
    }
    for (int32_t k = 1; k + 1 < m; ++k) {
      for (int32_t v : {0, k, k + 1}) meshverts.i32(v);
    }
    face(faces, int32_t(f.texture), 1, vertex_count, m, meshvert_count, 3 * (m - 2), int32_t(page),
         at, size, f.normal, 0, 0);
    vertex_count += m;
    meshvert_count += 3 * (m - 2);
  }

  // The vault: a 5 × 3 patch rippling under the ceiling, lit on a 16 × 8 block.
  if (vault) {
    const FixtureRoom& rm = fx.rooms[size_t(fx.vault_room)];
    constexpr int kCols = 5, kRows = 3;
    std::vector<std::array<float, 3>> ctrl;
    for (int j = 0; j < kRows; ++j) {
      for (int i = 0; i < kCols; ++i) {
        const float x =
            float(rm.min[0]) + float(rm.max[0] - rm.min[0]) * float(i) / float(kCols - 1);
        const float y =
            float(rm.min[1] + 32) + float(rm.max[1] - rm.min[1] - 64) * float(j) / float(kRows - 1);
        const float z = float(rm.max[2] - (i % 2 ? 24 : 136));
        ctrl.push_back({x, y, z});
      }
    }
    const uint32_t size[2] = {16, 8};
    uint32_t page = 0, at[2] = {};
    pack.place(size[0], size[1], &page, &at[0], &at[1]);
    const float down[3] = {0, 0, -1};
    for (uint32_t j = 0; j < size[1]; ++j) {
      for (uint32_t i = 0; i < size[0]; ++i) {
        double p[3];
        patch_point(ctrl, kCols, (i + 0.5) / size[0], (j + 0.5) / size[1], p);
        luxel(page, at[0] + i, at[1] + j, light_at(fx, uint32_t(fx.vault_room), p, down));
      }
    }
    for (int j = 0; j < kRows; ++j) {
      for (int i = 0; i < kCols; ++i) {
        const auto& p = ctrl[size_t(j) * kCols + size_t(i)];
        const float st[2] = {p[0] / 64.0f, p[1] / 64.0f};
        const float lm[2] = {
            float((at[0] + 0.5 + double(i) / (kCols - 1) * (size[0] - 1)) / kPage),
            float((at[1] + 0.5 + double(j) / (kRows - 1) * (size[1] - 1)) / kPage)};
        vertex(vertexes, p.data(), st, lm, down);
      }
    }
    face(faces, int32_t(names.size() - 1), 2, vertex_count, kCols * kRows, 0, 0, int32_t(page), at,
         size, down, kCols, kRows);
  }
  for (const auto& p : pages) lightmaps.bytes(p);

  for (const CNode& n : c.nodes) {
    nodes.i32(int32_t(n.plane));
    for (int32_t ch : n.child) nodes.i32(ch);
    for (const float* b : {n.mins, n.maxs}) {
      for (int k = 0; k < 3; ++k) nodes.i32(int32_t(b[k]));
    }
  }

  const size_t rooms = c.room_faces.size();
  const auto leaf = [&](int32_t cluster, const float* lo, const float* hi, uint32_t first,
                        uint32_t count) {
    leafs.i32(cluster);
    leafs.i32(0);
    for (const float* b : {lo, hi}) {
      for (int k = 0; k < 3; ++k) leafs.i32(int32_t(b[k]));
    }
    leafs.i32(int32_t(first));
    leafs.i32(int32_t(count));
    leafs.i32(0);
    leafs.i32(0);
  };
  const float zero[3] = {0, 0, 0};
  leaf(-1, zero, zero, 0, 0);
  uint32_t first = 0;
  for (size_t r = 0; r < rooms; ++r) {
    const FixtureRoom& rm = fx.rooms[r];
    const float lo[3] = {float(rm.min[0]), float(rm.min[1]), float(rm.min[2])};
    const float hi[3] = {float(rm.max[0]), float(rm.max[1]), float(rm.max[2])};
    leaf(int32_t(r), lo, hi, first, uint32_t(c.room_faces[r].size()));
    for (uint32_t f : c.room_faces[r]) leaffaces.i32(int32_t(f));
    first += uint32_t(c.room_faces[r].size());
  }

  for (const float* b : {c.mins, c.maxs}) {
    for (int k = 0; k < 3; ++k) models.f32(b[k]);
  }
  models.i32(0);
  models.i32(int32_t(c.faces.size()) + (vault ? 1 : 0));
  models.i32(0);
  models.i32(0);

  const auto row = uint32_t((rooms + 7) / 8);
  visdata.i32(int32_t(rooms));
  visdata.i32(int32_t(row));
  for (size_t r = 0; r < rooms; ++r) visdata.bytes(vis_row(c, uint32_t(r)));

  Out& o = *out;
  o.name("IBSP", 4);
  o.i32(46);
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
