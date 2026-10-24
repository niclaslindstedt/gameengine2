// The version 29 reader and its larger-limits variant BSP2: fifteen lumps, faces as polygons of
// surfedges, texinfo projections, miptex names and sizes, one byte a luxel, leaf-numbered
// visibility, three clip hulls. BSP2 widens the node, leaf, face, clipnode, marksurface and edge
// records' indices to 32 bits and stores node and leaf bounds as floats.
//
// ge:summary Reads version 29 and BSP2 maps: polygons, miptex, grey lightmaps, leaf visibility
// ge:keywords v29 bsp2 read faces surfedges edges texinfo miptex lighting clipnodes leafs visleafs
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <array>

#include "brush/read_parts.hpp"

namespace ge::brush {

namespace {

using detail::Cursor;
using detail::Lump;

enum : uint8_t {
  kEntities,
  kPlanes,
  kTextures,
  kVertexes,
  kVisibility,
  kNodes,
  kTexinfo,
  kFaces,
  kLighting,
  kClipnodes,
  kLeafs,
  kMarksurfaces,
  kEdges,
  kSurfedges,
  kModels
};
constexpr std::array<const char*, 15> kNames = {
    "entities", "planes",       "textures", "vertexes",  "visibility",
    "nodes",    "texinfo",      "faces",    "lighting",  "clipnodes",
    "leafs",    "marksurfaces", "edges",    "surfedges", "models"};

struct Texinfo {
  float v[2][4];
  uint32_t texture;
  int32_t flags;
};

bool is_sky(const std::string& name) {
  return name.size() >= 3 && (name[0] == 's' || name[0] == 'S') &&
         (name[1] == 'k' || name[1] == 'K') && (name[2] == 'y' || name[2] == 'Y');
}

}  // namespace

bool read_v29(std::span<const uint8_t> file, bool bsp2, Map* out, std::string* err) {
  Map& m = *out;
  m.family = bsp2 ? Family::bsp2 : Family::v29;
  m.version = 29;
  const Family f = m.family;
  Cursor head{file};
  head.skip(4);
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

  // Textures: a count, offsets from the lump's start, then the miptex records.
  {
    Cursor c = lump(kTextures);
    const uint32_t count = L[kTextures].len >= 4 ? c.u32() : 0;
    if (count > (L[kTextures].len - 4) / 4 && L[kTextures].len >= 4) {
      return detail::refuse(err, f, "the texture lump counts more offsets than it holds",
                            "the file is damaged: rebuild the map");
    }
    for (uint32_t i = 0; i < count; ++i) {
      const int32_t off = c.i32();
      Texture t;
      if (off < 0) {
        t.name = "missing";
      } else {
        Cursor r = lump(kTextures);
        if (uint32_t(off) > L[kTextures].len || L[kTextures].len - uint32_t(off) < 40) {
          return detail::refuse(err, f,
                                "texture " + std::to_string(i) + " lies past its lump's end",
                                "the file is damaged: rebuild the map");
        }
        r.skip(size_t(off));
        t.name = r.name(16);
        t.width = r.u32();
        t.height = r.u32();
        const uint32_t pixels = r.u32();
        if (t.width == 0 || t.height == 0 || t.width > 8192 || t.height > 8192 ||
            (pixels != 0 &&
             (pixels > L[kTextures].len - uint32_t(off) ||
              uint64_t(t.width) * t.height > L[kTextures].len - uint32_t(off) - pixels))) {
          return detail::refuse(
              err, f,
              "texture " + std::to_string(i) + " (" + t.name + ") has a size its pixels do not fit",
              "the file is damaged: rebuild the map");
        }
      }
      m.textures.push_back(t);
    }
  }

  std::vector<Texinfo> texinfo;
  if (!detail::count_of(L[kTexinfo], 40, f, &n, err)) return false;
  {
    Cursor c = lump(kTexinfo);
    texinfo.resize(n);
    for (size_t i = 0; i < n; ++i) {
      Texinfo& t = texinfo[i];
      for (auto& row : t.v) {
        for (float& x : row) x = c.f32();
      }
      t.texture = c.u32();
      t.flags = c.i32();
      if (t.texture >= m.textures.size()) {
        return detail::refuse(err, f,
                              "texinfo " + std::to_string(i) + " names texture " +
                                  std::to_string(t.texture) + " of " +
                                  std::to_string(m.textures.size()),
                              "the file is damaged: rebuild the map");
      }
    }
  }

  detail::Polys polys;
  if (!detail::read_polys(file, L[kVertexes], L[kEdges], L[kSurfedges], bsp2, f, &polys, err)) {
    return false;
  }

  m.light_rgb.resize(size_t(L[kLighting].len) * 3);
  for (uint32_t i = 0; i < L[kLighting].len; ++i) {
    const uint8_t g = file[L[kLighting].off + i];
    m.light_rgb[size_t(i) * 3] = m.light_rgb[size_t(i) * 3 + 1] = m.light_rgb[size_t(i) * 3 + 2] =
        g;
  }

  // Faces: each its own vertices (their texture and lightmap coordinates are the face's).
  if (!detail::count_of(L[kFaces], bsp2 ? 28 : 20, f, &n, err)) return false;
  {
    Cursor c = lump(kFaces);
    for (size_t i = 0; i < n; ++i) {
      const uint32_t plane = bsp2 ? c.u32() : c.u16();
      const int32_t side = bsp2 ? c.i32() : c.i16();
      const int32_t first = c.i32();
      const int32_t count = bsp2 ? c.i32() : c.i16();
      const uint32_t ti = bsp2 ? c.u32() : c.u16();
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
      const std::string& tex = m.textures[t.texture].name;
      s.kind = !tex.empty() && tex[0] == '*' ? SurfaceKind::liquid
               : is_sky(tex)                 ? SurfaceKind::sky
                                             : SurfaceKind::planar;
      float nrm[3];
      for (int k = 0; k < 3; ++k) nrm[k] = side ? -m.planes[plane].n[k] : m.planes[plane].n[k];
      const uint32_t base = detail::add_polygon(&m, polys, first, count, nrm, t.v);
      s.count = uint32_t(m.indices.size()) - s.first;
      for (uint8_t st : styles) {
        if (st == 255) break;
        ++s.styles;
      }
      if (lightofs >= 0 && (t.flags & 1) == 0 && s.styles > 0) {
        const detail::Extent e = detail::extent_of(m, base, uint32_t(count));
        if (e.size[0] < 1 || e.size[1] < 1 || e.size[0] > 1024 || e.size[1] > 1024 ||
            uint64_t(lightofs) + uint64_t(e.size[0] * e.size[1]) * s.styles > L[kLighting].len) {
          return detail::refuse(err, f, where + "'s lightmap lies outside the lighting lump",
                                "the file is damaged: re-run the map's light step");
        }
        detail::place_lightmap(&m, base, uint32_t(count), e);
        s.lightmap = int32_t(m.lightmaps.size());
        m.lightmaps.push_back(
            Lightmap{uint32_t(e.size[0]), uint32_t(e.size[1]), uint32_t(lightofs) * 3});
      }
      m.surfaces.push_back(s);
    }
  }

  if (!detail::count_of(L[kNodes], bsp2 ? 44 : 24, f, &n, err)) return false;
  {
    Cursor c = lump(kNodes);
    m.nodes.resize(n);
    for (Node& node : m.nodes) {
      node.plane = c.u32();
      for (int32_t& ch : node.child) ch = bsp2 ? c.i32() : c.i16();
      c.skip(bsp2 ? 32 : 16);
    }
  }

  if (!detail::count_of(L[kClipnodes], bsp2 ? 12 : 8, f, &n, err)) return false;
  m.clipnodes = uint32_t(n);
  {
    Cursor c = lump(kClipnodes);
    for (size_t i = 0; i < n; ++i) {
      const uint32_t plane = c.u32();
      for (int k = 0; k < 2; ++k) {
        const int32_t ch = bsp2 ? c.i32() : c.i16();
        if (plane >= m.planes.size() || (ch >= 0 && size_t(ch) >= n)) {
          return detail::refuse(err, f,
                                "clipnode " + std::to_string(i) +
                                    " names a plane or child not "
                                    "in the map",
                                "the file is damaged: rebuild the map");
        }
      }
    }
  }

  if (!detail::count_of(L[kModels], 64, f, &n, err)) return false;
  int32_t visleafs = 0;
  {
    Cursor c = lump(kModels);
    m.models.resize(n);
    for (size_t i = 0; i < n; ++i) {
      Model& md = m.models[i];
      for (float& x : md.mins) x = c.f32();
      for (float& x : md.maxs) x = c.f32();
      c.skip(12);
      md.head_node = c.i32();
      for (int h = 1; h < 4; ++h) {
        const int32_t hull = c.i32();
        if (h < 3 && hull >= 0 && uint32_t(hull) >= m.clipnodes && m.clipnodes > 0) {
          return detail::refuse(err, f,
                                "model " + std::to_string(i) + "'s clip hull " + std::to_string(h) +
                                    " names a clipnode not in the map",
                                "the file is damaged: rebuild the map");
        }
      }
      const int32_t vl = c.i32();
      if (i == 0) visleafs = vl;
      const int32_t first = c.i32(), count = c.i32();
      if (first < 0 || count < 0) {
        return detail::refuse(err, f, "model " + std::to_string(i) + " has a negative face range",
                              "the file is damaged: rebuild the map");
      }
      md.first_surface = uint32_t(first);
      md.surface_count = uint32_t(count);
    }
  }

  if (!detail::count_of(L[kLeafs], bsp2 ? 44 : 28, f, &n, err)) return false;
  if (visleafs < 0 || (n > 0 && size_t(visleafs) > n - 1)) {
    return detail::refuse(
        err, f,
        "the world counts " + std::to_string(visleafs) + " visible leaves of " + std::to_string(n),
        "the file is damaged: rebuild the map");
  }
  m.clusters = visleafs;
  m.row_bytes = uint32_t((visleafs + 7) / 8);
  m.pvs.assign(size_t(m.clusters) * m.row_bytes, 0);
  {
    Cursor c = lump(kLeafs);
    const auto vis = file.subspan(L[kVisibility].off, L[kVisibility].len);
    m.leaves.resize(n);
    for (size_t i = 0; i < n; ++i) {
      Leaf& lf = m.leaves[i];
      lf.contents = c.i32();
      const int32_t visofs = c.i32();
      for (float& x : lf.mins) x = bsp2 ? c.f32() : float(c.i16());
      for (float& x : lf.maxs) x = bsp2 ? c.f32() : float(c.i16());
      c.skip(bsp2 ? 12 : 8);
      lf.solid = lf.contents == -2;
      lf.cluster = i > 0 && int64_t(i) - 1 < visleafs ? int32_t(i - 1) : -1;
      if (lf.cluster < 0) continue;
      uint8_t* row = m.pvs.data() + size_t(lf.cluster) * m.row_bytes;
      if (visofs < 0 || vis.empty()) {
        for (uint32_t k = 0; k < m.row_bytes; ++k) row[k] = 0xff;
      } else if (!detail::unpack_row(vis, uint32_t(visofs), m.row_bytes, row)) {
        return detail::refuse(err, f,
                              "leaf " + std::to_string(i) + "'s visibility row at " +
                                  std::to_string(visofs) + " runs past the visibility lump",
                              "the visibility is damaged: re-run the map's vis step");
      }
    }
  }
  return true;
}

}  // namespace ge::brush
