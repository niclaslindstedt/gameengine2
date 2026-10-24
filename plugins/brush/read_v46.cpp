// The version 46 reader: seventeen lumps; vertices carry their own texture and lightmap
// coordinates, normals and colours; a face is a polygon or a mesh (triangles from a mesh-vertex
// list), a curved patch (a grid of control points, tessellated here), or a billboard; textures are
// shaders by name with their surface and content flags; lightmaps are 128 × 128 RGB pages; the
// visibility is one uncompressed row a cluster.
//
// ge:summary Reads version 46 maps: meshes, tessellated patches, shaders, lightmap pages, rows
// ge:keywords v46 read shader vertex meshvert face polygon mesh patch billboard tessellate bezier
// ge:keywords biquadratic lightmap page visdata cluster
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <algorithm>
#include <array>
#include <cmath>

#include "brush/read_parts.hpp"

namespace ge::brush {

namespace {

using detail::Cursor;
using detail::Lump;

enum : uint8_t {
  kEntities,
  kShaders,
  kPlanes,
  kNodes,
  kLeafs,
  kLeaffaces,
  kLeafbrushes,
  kModels,
  kBrushes,
  kBrushsides,
  kVertexes,
  kMeshverts,
  kEffects,
  kFaces,
  kLightmaps,
  kLightvols,
  kVisdata
};
constexpr std::array<const char*, 17> kNames = {
    "entities",    "shaders", "planes",    "nodes",      "leafs",    "leaffaces",
    "leafbrushes", "models",  "brushes",   "brushsides", "vertexes", "meshverts",
    "effects",     "faces",   "lightmaps", "lightvols",  "visdata"};

constexpr int32_t kSkyFlag = 0x4;               // a shader's surface flag
constexpr int32_t kLiquid = 0x8 | 0x10 | 0x20;  // lava, slime, water contents
constexpr uint32_t kPage = 128, kPageBytes = 128 * 128 * 3;
// A patch's tessellation is bounded so a hostile file cannot ask for unbounded memory.
constexpr uint64_t kMaxVertices = 1u << 23;
enum FaceType : uint8_t { kPolygon = 1, kPatch = 2, kMesh = 3, kBillboard = 4 };

// Turns a surface's triangles round when, summed, they face against its vertices' normals.
void orient(Map* m, const Surface& s) {
  double agree = 0;
  for (uint32_t t = s.first; t + 2 < s.first + s.count; t += 3) {
    const float* a = m->vertices[m->indices[t]].pos;
    const float* b = m->vertices[m->indices[t + 1]].pos;
    const float* c = m->vertices[m->indices[t + 2]].pos;
    const double e1[3] = {double(b[0]) - a[0], double(b[1]) - a[1], double(b[2]) - a[2]};
    const double e2[3] = {double(c[0]) - a[0], double(c[1]) - a[1], double(c[2]) - a[2]};
    const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                         e1[0] * e2[1] - e1[1] * e2[0]};
    for (int v = 0; v < 3; ++v) {
      const float* vn = m->vertices[m->indices[t + uint32_t(v)]].nrm;
      agree += n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2];
    }
  }
  if (agree >= 0) return;
  for (uint32_t t = s.first; t + 2 < s.first + s.count; t += 3) {
    std::swap(m->indices[t + 1], m->indices[t + 2]);
  }
}

// A patch's control grid (`w` × `h`, both odd) as `level` steps across every 3 × 3 piece:
// biquadratic, every point of the whole grid evaluated once, so pieces share their edges.
void tessellate(Map* m, std::span<const Vertex> ctrl, int w, int h, int level) {
  const int nx = (w - 1) / 2, ny = (h - 1) / 2;
  const int cols = nx * level + 1, rows = ny * level + 1;
  const auto base = uint32_t(m->vertices.size());
  for (int r = 0; r < rows; ++r) {
    const int py = std::min(r / level, ny - 1);
    const double v = double(r - py * level) / level;
    const double bv[3] = {(1 - v) * (1 - v), 2 * v * (1 - v), v * v};
    for (int c = 0; c < cols; ++c) {
      const int px = std::min(c / level, nx - 1);
      const double u = double(c - px * level) / level;
      const double bu[3] = {(1 - u) * (1 - u), 2 * u * (1 - u), u * u};
      double pos[3] = {}, st[2] = {}, lm[2] = {}, nrm[3] = {};
      for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
          const Vertex& q = ctrl[size_t(2 * py + j) * size_t(w) + size_t(2 * px + i)];
          const double k = bu[i] * bv[j];
          for (int a = 0; a < 3; ++a) pos[a] += k * q.pos[a], nrm[a] += k * q.nrm[a];
          for (int a = 0; a < 2; ++a) st[a] += k * q.st[a], lm[a] += k * q.lm[a];
        }
      }
      const double len = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
      Vertex out;
      for (int a = 0; a < 3; ++a) {
        out.pos[a] = float(pos[a]);
        out.nrm[a] = len > 0 ? float(nrm[a] / len) : 0.0f;
      }
      for (int a = 0; a < 2; ++a) out.st[a] = float(st[a]), out.lm[a] = float(lm[a]);
      m->vertices.push_back(out);
    }
  }
  for (int r = 0; r + 1 < rows; ++r) {
    for (int c = 0; c + 1 < cols; ++c) {
      const uint32_t a = base + uint32_t(r * cols + c), b = a + 1;
      const uint32_t d = a + uint32_t(cols), e = d + 1;
      m->indices.insert(m->indices.end(), {a, b, e, a, e, d});
    }
  }
}

}  // namespace

bool read_v46(std::span<const uint8_t> file, Map* out, std::string* err, const ReadOptions& o) {
  Map& m = *out;
  m.family = Family::v46;
  m.version = 46;
  m.st_texels = false;
  const Family f = m.family;
  if (o.patch_level < 1 || o.patch_level > 32) {
    return detail::refuse(err, f, "patch_level " + std::to_string(o.patch_level) + " is not 1..32",
                          "pass ReadOptions::patch_level between 1 and 32");
  }
  Cursor head{file};
  head.skip(8);
  std::vector<Lump> L;
  if (!detail::read_lumps(head, f, kNames, file.size(), &L, err)) return false;
  const auto lump = [&](size_t i) { return detail::at_lump(file, L[i]); };
  size_t n = 0;

  {
    Cursor c = lump(kEntities);
    m.entity_text = c.name(L[kEntities].len);
  }

  if (!detail::count_of(L[kShaders], 72, f, &n, err)) return false;
  {
    Cursor c = lump(kShaders);
    for (size_t i = 0; i < n; ++i) {
      Texture t;
      t.name = c.name(64);
      t.flags = c.u32();
      c.skip(4);
      m.textures.push_back(t);
    }
  }
  std::vector<int32_t> shader_contents(n);
  {
    Cursor c = lump(kShaders);
    for (int32_t& x : shader_contents) {
      c.skip(68);
      x = c.i32();
    }
  }

  if (!detail::count_of(L[kPlanes], 16, f, &n, err)) return false;
  m.planes.resize(n);
  {
    Cursor c = lump(kPlanes);
    for (Plane& p : m.planes) {
      for (float& x : p.n) x = c.f32();
      p.d = c.f32();
    }
  }

  if (!detail::count_of(L[kNodes], 36, f, &n, err)) return false;
  m.nodes.resize(n);
  {
    Cursor c = lump(kNodes);
    for (Node& node : m.nodes) {
      node.plane = c.u32();
      for (int32_t& ch : node.child) ch = c.i32();
      c.skip(24);
    }
  }

  if (!detail::count_of(L[kVertexes], 44, f, &n, err)) return false;
  m.vertices.resize(n);
  {
    Cursor c = lump(kVertexes);
    for (Vertex& v : m.vertices) {
      for (float& x : v.pos) x = c.f32();
      for (float& x : v.st) x = c.f32();
      for (float& x : v.lm) x = c.f32();
      for (float& x : v.nrm) x = c.f32();
      c.skip(4);
    }
  }
  const size_t file_vertices = n;

  std::vector<int32_t> meshverts;
  if (!detail::count_of(L[kMeshverts], 4, f, &n, err)) return false;
  meshverts.resize(n);
  {
    Cursor c = lump(kMeshverts);
    for (int32_t& x : meshverts) x = c.i32();
  }

  if (L[kLightmaps].len % kPageBytes != 0) {
    return detail::refuse(err, f, "the lightmap lump is not whole 128 × 128 pages",
                          "the file is damaged: re-run the map's light step");
  }
  m.light_rgb.assign(file.begin() + L[kLightmaps].off,
                     file.begin() + L[kLightmaps].off + L[kLightmaps].len);
  for (uint32_t p = 0; p < L[kLightmaps].len / kPageBytes; ++p) {
    m.lightmaps.push_back(Lightmap{kPage, kPage, p * kPageBytes});
  }

  if (!detail::count_of(L[kFaces], 104, f, &n, err)) return false;
  {
    Cursor c = lump(kFaces);
    for (size_t i = 0; i < n; ++i) {
      const int32_t shader = c.i32();
      c.skip(4);
      const int32_t type = c.i32();
      const int32_t vertex = c.i32(), n_vertexes = c.i32();
      const int32_t meshvert = c.i32(), n_meshverts = c.i32();
      const int32_t lm_index = c.i32();
      c.skip(4 * 4 + 4 * 3 + 4 * 6 + 4 * 3);
      const int32_t size_w = c.i32(), size_h = c.i32();
      const std::string where = "face " + std::to_string(i);
      if (shader < 0 || size_t(shader) >= m.textures.size() ||
          (lm_index >= 0 && size_t(lm_index) >= m.lightmaps.size())) {
        return detail::refuse(err, f, where + " names a shader or lightmap page not in the map",
                              "the file is damaged: rebuild the map");
      }
      if (vertex < 0 || n_vertexes < 0 || size_t(vertex) > file_vertices ||
          size_t(n_vertexes) > file_vertices - size_t(vertex)) {
        return detail::refuse(err, f, where + "'s vertices lie outside the vertex lump",
                              "the file is damaged: rebuild the map");
      }
      Surface s;
      s.texture = uint32_t(shader);
      s.lightmap = lm_index >= 0 ? lm_index : -1;
      s.first = uint32_t(m.indices.size());
      const Texture& tex = m.textures[size_t(shader)];
      s.kind = (tex.flags & kSkyFlag)                        ? SurfaceKind::sky
               : (shader_contents[size_t(shader)] & kLiquid) ? SurfaceKind::liquid
                                                             : SurfaceKind::planar;
      if (type == kPolygon || type == kMesh) {
        if (meshvert < 0 || n_meshverts < 0 || n_meshverts % 3 != 0 ||
            size_t(meshvert) > meshverts.size() ||
            size_t(n_meshverts) > meshverts.size() - size_t(meshvert)) {
          return detail::refuse(err, f, where + "'s mesh vertices lie outside their lump",
                                "the file is damaged: rebuild the map");
        }
        for (int32_t k = 0; k < n_meshverts; ++k) {
          const int32_t mv = meshverts[size_t(meshvert) + size_t(k)];
          if (mv < 0 || mv >= n_vertexes) {
            return detail::refuse(err, f, where + " has a mesh vertex outside its own vertices",
                                  "the file is damaged: rebuild the map");
          }
          m.indices.push_back(uint32_t(vertex + mv));
        }
        if (type == kMesh) s.kind = SurfaceKind::mesh;
      } else if (type == kPatch) {
        if (size_w < 3 || size_h < 3 || size_w % 2 == 0 || size_h % 2 == 0 || size_w > 65 ||
            size_h > 65 || size_w * size_h != n_vertexes) {
          return detail::refuse(err, f,
                                where + " is a patch of " + std::to_string(size_w) + " × " +
                                    std::to_string(size_h) + " control points (odd, 3..65, and " +
                                    "its vertex count)",
                                "the file is damaged: rebuild the map");
        }
        const uint64_t grid = uint64_t((size_w - 1) / 2 * o.patch_level + 1) *
                              uint64_t((size_h - 1) / 2 * o.patch_level + 1);
        if (m.vertices.size() + grid > kMaxVertices) {
          return detail::refuse(
              err, f,
              where + "'s patch takes the map past " + std::to_string(kMaxVertices) + " vertices",
              "read it with a lower ReadOptions::patch_level");
        }
        const std::vector<Vertex> ctrl(m.vertices.begin() + vertex,
                                       m.vertices.begin() + vertex + n_vertexes);
        tessellate(&m, ctrl, size_w, size_h, o.patch_level);
        s.kind = SurfaceKind::patch;
        ++m.patches;
      } else if (type == kBillboard) {
        ++m.billboards;
      } else {
        return detail::refuse(err, f, where + " is of type " + std::to_string(type) + " (1..4)",
                              "the file is damaged or of a later variant: rebuild the map");
      }
      s.count = uint32_t(m.indices.size()) - s.first;
      orient(&m, s);
      m.surfaces.push_back(s);
    }
  }

  if (!detail::count_of(L[kModels], 40, f, &n, err)) return false;
  {
    Cursor c = lump(kModels);
    m.models.resize(n);
    for (size_t i = 0; i < n; ++i) {
      Model& md = m.models[i];
      for (float& x : md.mins) x = c.f32();
      for (float& x : md.maxs) x = c.f32();
      const int32_t first = c.i32(), count = c.i32();
      c.skip(8);
      if (first < 0 || count < 0) {
        return detail::refuse(err, f, "model " + std::to_string(i) + " has a negative face range",
                              "the file is damaged: rebuild the map");
      }
      md.first_surface = uint32_t(first);
      md.surface_count = uint32_t(count);
      md.head_node = m.nodes.empty() ? -1 : 0;  // brush models have no tree of their own
    }
  }

  if (!detail::count_of(L[kBrushes], 12, f, &n, err)) return false;
  m.brushes = uint32_t(n);

  const auto vis = file.subspan(L[kVisdata].off, L[kVisdata].len);
  if (!vis.empty()) {
    Cursor c{vis};
    const int32_t clusters = c.i32(), row = c.i32();
    if (c.bad || clusters < 0 || row < (clusters + 7) / 8 ||
        uint64_t(clusters) * uint64_t(row) > vis.size() - 8) {
      return detail::refuse(err, f, "the visibility's rows do not fit its lump",
                            "the visibility is damaged: re-run the map's vis step");
    }
    m.clusters = clusters;
    m.row_bytes = uint32_t(row);
    m.pvs.assign(vis.begin() + 8, vis.begin() + 8 + int64_t(clusters) * row);
  }

  if (!detail::count_of(L[kLeafs], 48, f, &n, err)) return false;
  {
    Cursor c = lump(kLeafs);
    m.leaves.resize(n);
    for (Leaf& lf : m.leaves) {
      lf.cluster = c.i32();
      c.skip(4);
      for (float& x : lf.mins) x = float(c.i32());
      for (float& x : lf.maxs) x = float(c.i32());
      c.skip(16);
      if (lf.cluster < 0) lf.cluster = -1;
      lf.solid = lf.cluster < 0;
    }
  }
  if (vis.empty()) {
    for (const Leaf& lf : m.leaves) m.clusters = std::max(m.clusters, lf.cluster + 1);
    m.row_bytes = uint32_t((m.clusters + 7) / 8);
    m.pvs.assign(size_t(m.clusters) * m.row_bytes, 0xff);
  }
  return true;
}

}  // namespace ge::brush
