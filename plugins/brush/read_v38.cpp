// The version 38 reader: nineteen lumps; faces are polygons of surfedges as in 29, but a texinfo
// carries its texture's name (the pixels live in other files), luxels are RGB, every leaf names
// its own cluster and area, and the visibility starts with the cluster count and each cluster's
// offsets to its potentially visible and potentially hearable rows.
//
// ge:summary Reads version 38 maps: polygons, texinfo names, RGB lightmaps, clusters and rows
// ge:keywords v38 read texinfo texture name surface flags rgb lighting leafs cluster area brushes
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <algorithm>
#include <array>
#include <map>

#include "brush/read_parts.hpp"

namespace ge::brush {

namespace {

using detail::Cursor;
using detail::Lump;

enum : uint8_t {
  kEntities,
  kPlanes,
  kVertices,
  kVisibility,
  kNodes,
  kTexinfo,
  kFaces,
  kLighting,
  kLeafs,
  kLeaffaces,
  kLeafbrushes,
  kEdges,
  kSurfedges,
  kModels,
  kBrushes,
  kBrushsides,
  kPop,
  kAreas,
  kAreaportals
};
constexpr std::array<const char*, 19> kNames = {
    "entities", "planes",     "vertices",  "visibility",  "nodes",      "texinfo",   "faces",
    "lighting", "leafs",      "leaffaces", "leafbrushes", "edges",      "surfedges", "models",
    "brushes",  "brushsides", "pop",       "areas",       "areaportals"};

// Surface flags a face's texinfo carries: sky and warped (liquid) faces have no lightmap.
constexpr int32_t kSky = 0x4, kWarp = 0x8, kNoDraw = 0x80;

struct Texinfo {
  float v[2][4];
  int32_t flags;
  uint32_t texture;
};

}  // namespace

bool read_v38(std::span<const uint8_t> file, Map* out, std::string* err) {
  Map& m = *out;
  m.family = Family::v38;
  m.version = 38;
  const Family f = m.family;
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

  if (!detail::count_of(L[kPlanes], 20, f, &n, err)) return false;
  m.planes.resize(n);
  {
    Cursor c = lump(kPlanes);
    for (Plane& p : m.planes) {
      for (float& x : p.n) x = c.f32();
      p.d = c.f32();
      c.skip(4);
    }
  }

  detail::Polys polys;
  if (!detail::read_polys(file, L[kVertices], L[kEdges], L[kSurfedges], false, f, &polys, err)) {
    return false;
  }

  // Texinfo: the texture by name, each name one texture of the view.
  std::vector<Texinfo> texinfo;
  if (!detail::count_of(L[kTexinfo], 76, f, &n, err)) return false;
  {
    Cursor c = lump(kTexinfo);
    std::map<std::string, uint32_t> by_name;
    texinfo.resize(n);
    for (Texinfo& t : texinfo) {
      for (auto& row : t.v) {
        for (float& x : row) x = c.f32();
      }
      t.flags = c.i32();
      c.skip(4);
      const std::string name = c.name(32);
      c.skip(4);
      const auto [it, added] = by_name.emplace(name, uint32_t(m.textures.size()));
      if (added) m.textures.push_back(Texture{name, 0, 0, uint32_t(t.flags), {}});
      t.texture = it->second;
    }
  }

  m.light_rgb.assign(file.begin() + L[kLighting].off,
                     file.begin() + L[kLighting].off + L[kLighting].len);

  if (!detail::count_of(L[kFaces], 20, f, &n, err)) return false;
  {
    Cursor c = lump(kFaces);
    for (size_t i = 0; i < n; ++i) {
      const uint32_t plane = c.u16();
      const int32_t side = c.i16();
      const int32_t first = c.i32();
      const int32_t count = c.i16();
      const uint32_t ti = c.u16();
      uint8_t styles[4];
      for (uint8_t& s : styles) s = c.u8();
      const int32_t lightofs = c.i32();
      const std::string where = "face " + std::to_string(i);
      if (plane >= m.planes.size() || ti >= texinfo.size()) {
        return detail::refuse(err, f, where + " names a plane or texinfo not in the map",
                              "the file is damaged: rebuild the map");
      }
      if (!detail::polygon_fits(polys, first, count)) {
        return detail::refuse(err, f, where + " has edges outside the surfedge lump",
                              "the file is damaged: rebuild the map");
      }
      const Texinfo& t = texinfo[ti];
      Surface s;
      s.texture = t.texture;
      s.first = uint32_t(m.indices.size());
      s.kind = (t.flags & kSky)    ? SurfaceKind::sky
               : (t.flags & kWarp) ? SurfaceKind::liquid
                                   : SurfaceKind::planar;
      float nrm[3];
      for (int k = 0; k < 3; ++k) nrm[k] = side ? -m.planes[plane].n[k] : m.planes[plane].n[k];
      const uint32_t base = detail::add_polygon(&m, polys, first, count, nrm, t.v);
      s.count = uint32_t(m.indices.size()) - s.first;
      for (uint8_t st : styles) {
        if (st == 255) break;
        ++s.styles;
      }
      if (lightofs >= 0 && (t.flags & (kSky | kWarp | kNoDraw)) == 0 && s.styles > 0) {
        const detail::Extent e = detail::extent_of(m, base, uint32_t(count));
        if (e.size[0] < 1 || e.size[1] < 1 || e.size[0] > 1024 || e.size[1] > 1024 ||
            uint64_t(lightofs) + uint64_t(e.size[0] * e.size[1]) * 3 * s.styles >
                L[kLighting].len) {
          return detail::refuse(err, f, where + "'s lightmap lies outside the lighting lump",
                                "the file is damaged: re-run the map's light step");
        }
        detail::place_lightmap(&m, base, uint32_t(count), e);
        s.lightmap = int32_t(m.lightmaps.size());
        m.lightmaps.push_back(
            Lightmap{uint32_t(e.size[0]), uint32_t(e.size[1]), uint32_t(lightofs)});
      }
      m.surfaces.push_back(s);
    }
  }

  if (!detail::count_of(L[kNodes], 28, f, &n, err)) return false;
  {
    Cursor c = lump(kNodes);
    m.nodes.resize(n);
    for (Node& node : m.nodes) {
      node.plane = c.u32();
      for (int32_t& ch : node.child) ch = c.i32();
      c.skip(16);
    }
  }

  if (!detail::count_of(L[kModels], 48, f, &n, err)) return false;
  {
    Cursor c = lump(kModels);
    m.models.resize(n);
    for (size_t i = 0; i < n; ++i) {
      Model& md = m.models[i];
      for (float& x : md.mins) x = c.f32();
      for (float& x : md.maxs) x = c.f32();
      c.skip(12);
      md.head_node = c.i32();
      const int32_t first = c.i32(), count = c.i32();
      if (first < 0 || count < 0) {
        return detail::refuse(err, f, "model " + std::to_string(i) + " has a negative face range",
                              "the file is damaged: rebuild the map");
      }
      md.first_surface = uint32_t(first);
      md.surface_count = uint32_t(count);
    }
  }

  if (!detail::count_of(L[kBrushes], 12, f, &n, err)) return false;
  m.brushes = uint32_t(n);

  // The visibility: the cluster count, two offsets a cluster, the rows they point at.
  const auto vis = file.subspan(L[kVisibility].off, L[kVisibility].len);
  if (!vis.empty()) {
    Cursor c{vis};
    const int32_t clusters = c.i32();
    if (c.bad || clusters < 0 || uint64_t(clusters) * 8 > vis.size() - 4) {
      return detail::refuse(err, f, "the visibility counts more clusters than it holds",
                            "the visibility is damaged: re-run the map's vis step");
    }
    m.clusters = clusters;
    m.row_bytes = uint32_t((clusters + 7) / 8);
    m.pvs.assign(size_t(clusters) * m.row_bytes, 0);
    for (int32_t k = 0; k < clusters; ++k) {
      const uint32_t pvs = c.u32();
      c.skip(4);
      if (!detail::unpack_row(vis, pvs, m.row_bytes, m.pvs.data() + size_t(k) * m.row_bytes)) {
        return detail::refuse(err, f,
                              "cluster " + std::to_string(k) + "'s row at " + std::to_string(pvs) +
                                  " runs past the visibility lump",
                              "the visibility is damaged: re-run the map's vis step");
      }
    }
  }

  if (!detail::count_of(L[kLeafs], 28, f, &n, err)) return false;
  {
    Cursor c = lump(kLeafs);
    m.leaves.resize(n);
    for (Leaf& lf : m.leaves) {
      lf.contents = c.i32();
      lf.cluster = c.i16();
      c.skip(2);
      for (float& x : lf.mins) x = float(c.i16());
      for (float& x : lf.maxs) x = float(c.i16());
      c.skip(8);
      lf.solid = (lf.contents & 1) != 0;
      if (lf.cluster < 0) lf.cluster = -1;
    }
  }
  // A map compiled without its vis step: every cluster sees every other.
  if (vis.empty()) {
    for (const Leaf& lf : m.leaves) m.clusters = std::max(m.clusters, lf.cluster + 1);
    m.row_bytes = uint32_t((m.clusters + 7) / 8);
    m.pvs.assign(size_t(m.clusters) * m.row_bytes, 0xff);
  }
  return true;
}

}  // namespace ge::brush
