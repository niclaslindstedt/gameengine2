// The reader's front door and its shared parts: the family told by the first bytes, the lump
// directory checked, the visibility rows unpacked, polygons fanned and wound, lightmap extents,
// the tree checked, the entity lump parsed and a `.lit` file dropped in.
//
// ge:summary Reads a BSP: family dispatch, lumps, rle rows, fans, extents, tree checks, entities
// ge:keywords read dispatch family magic version lump rle fan newell winding extent lit entities
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "brush/read_parts.hpp"

namespace ge::brush {

namespace detail {

bool refuse(std::string* err, Family f, const std::string& what, const char* next) {
  if (err) *err = std::string("bsp ") + family_name(f) + ": " + what + "; next: " + next;
  return false;
}

bool read_lumps(Cursor& c, Family f, std::span<const char* const> names, size_t size,
                std::vector<Lump>* out, std::string* err) {
  out->clear();
  for (const char* name : names) {
    Lump l;
    l.name = name;
    l.off = c.u32();
    l.len = c.u32();
    if (c.bad) {
      return refuse(err, f, "the header ends inside the lump directory",
                    "the file is truncated: copy it again from its source");
    }
    if (l.off > size || l.len > size - l.off) {
      return refuse(err, f,
                    std::string("lump ") + name + " (offset " + std::to_string(l.off) +
                        ", length " + std::to_string(l.len) + ") runs past the end of the " +
                        std::to_string(size) + "-byte file",
                    "the file is truncated or damaged: copy it again from its source");
    }
    out->push_back(l);
  }
  return true;
}

bool count_of(const Lump& lump, size_t elem, Family f, size_t* n, std::string* err) {
  if (lump.len % elem != 0) {
    return refuse(err, f,
                  std::string("lump ") + lump.name + " is " + std::to_string(lump.len) +
                      " bytes, not a whole number of " + std::to_string(elem) + "-byte records",
                  "the file is damaged or of another family: check its version");
  }
  *n = lump.len / elem;
  return true;
}

Cursor at_lump(std::span<const uint8_t> file, const Lump& lump) {
  return Cursor{file.subspan(lump.off, lump.len)};
}

bool unpack_row(std::span<const uint8_t> vis, uint32_t offset, uint32_t row_bytes, uint8_t* row) {
  size_t at = offset;
  uint32_t out = 0;
  while (out < row_bytes) {
    if (at >= vis.size()) return false;
    const uint8_t b = vis[at++];
    if (b != 0) {
      row[out++] = b;
      continue;
    }
    if (at >= vis.size()) return false;
    const uint32_t run = vis[at++];
    if (run == 0 || run > row_bytes - out) return false;
    for (uint32_t k = 0; k < run; ++k) row[out++] = 0;
  }
  return true;
}

void fan(Map* map, std::span<const uint32_t> poly, const float n[3]) {
  // Newell's normal of the polygon as stored, against the face's: reversed when they disagree.
  double nn[3] = {0, 0, 0};
  for (size_t i = 0; i < poly.size(); ++i) {
    const float* a = map->vertices[poly[i]].pos;
    const float* b = map->vertices[poly[(i + 1) % poly.size()]].pos;
    nn[0] += (double(a[1]) - b[1]) * (double(a[2]) + b[2]);
    nn[1] += (double(a[2]) - b[2]) * (double(a[0]) + b[0]);
    nn[2] += (double(a[0]) - b[0]) * (double(a[1]) + b[1]);
  }
  const bool flip = nn[0] * n[0] + nn[1] * n[1] + nn[2] * n[2] < 0;
  const size_t m = poly.size();
  for (size_t i = 1; i + 1 < m; ++i) {
    map->indices.push_back(poly[0]);
    map->indices.push_back(poly[flip ? i + 1 : i]);
    map->indices.push_back(poly[flip ? i : i + 1]);
  }
}

Extent extent_of(const Map& map, uint32_t first_vertex, uint32_t count) {
  Extent e;
  for (uint32_t v = first_vertex; v < first_vertex + count; ++v) {
    for (int k = 0; k < 2; ++k) {
      e.lo[k] = std::min(e.lo[k], double(map.vertices[v].st[k]));
      e.hi[k] = std::max(e.hi[k], double(map.vertices[v].st[k]));
    }
  }
  for (int k = 0; k < 2; ++k) {
    // A damaged vertex or projection (huge, infinite or not a number) gives no lightmap.
    if (!(e.lo[k] > -1e7 && e.hi[k] < 1e7)) return Extent{};
    e.base[k] = int64_t(std::floor(e.lo[k] / 16.0));
    e.size[k] = int64_t(std::ceil(e.hi[k] / 16.0)) - e.base[k] + 1;
  }
  return e;
}

void place_lightmap(Map* map, uint32_t first_vertex, uint32_t count, const Extent& e) {
  for (uint32_t v = first_vertex; v < first_vertex + count; ++v) {
    Vertex& x = map->vertices[v];
    for (int k = 0; k < 2; ++k) {
      x.lm[k] = float((double(x.st[k]) / 16.0 - double(e.base[k]) + 0.5) / double(e.size[k]));
    }
  }
}

bool read_polys(std::span<const uint8_t> file, const Lump& verts, const Lump& edges,
                const Lump& surfedges, bool wide_edges, Family f, Polys* out, std::string* err) {
  size_t n = 0;
  if (!count_of(verts, 12, f, &n, err)) return false;
  Cursor c = at_lump(file, verts);
  out->verts.resize(n);
  for (auto& v : out->verts) {
    for (float& x : v) x = c.f32();
  }
  if (!count_of(edges, wide_edges ? 8 : 4, f, &n, err)) return false;
  c = at_lump(file, edges);
  out->edges.resize(n);
  for (auto& e : out->edges) {
    for (uint32_t& v : e) {
      v = wide_edges ? c.u32() : c.u16();
      if (v >= out->verts.size()) {
        return refuse(err, f, "an edge names a vertex not in the map",
                      "the file is damaged: rebuild the map");
      }
    }
  }
  if (!count_of(surfedges, 4, f, &n, err)) return false;
  c = at_lump(file, surfedges);
  out->surfedges.resize(n);
  for (int32_t& e : out->surfedges) {
    e = c.i32();
    const int64_t a = e < 0 ? -int64_t(e) : int64_t(e);
    if (size_t(a) >= out->edges.size()) {
      return refuse(err, f, "a surfedge names an edge not in the map",
                    "the file is damaged: rebuild the map");
    }
  }
  return true;
}

bool polygon_fits(const Polys& p, int32_t first, int32_t count) {
  return count >= 3 && count <= 1024 && first >= 0 && size_t(first) <= p.surfedges.size() &&
         size_t(count) <= p.surfedges.size() - size_t(first);
}

uint32_t add_polygon(Map* map, const Polys& p, int32_t first, int32_t count, const float nrm[3],
                     const float vecs[2][4]) {
  const auto base = uint32_t(map->vertices.size());
  std::vector<uint32_t> poly;
  poly.reserve(size_t(count));
  for (int32_t e = 0; e < count; ++e) {
    const int32_t se = p.surfedges[size_t(first) + size_t(e)];
    const uint32_t vi = se >= 0 ? p.edges[size_t(se)][0] : p.edges[size_t(-int64_t(se))][1];
    Vertex v;
    for (int k = 0; k < 3; ++k) {
      v.pos[k] = p.verts[vi][size_t(k)];
      v.nrm[k] = nrm[k];
    }
    for (int a = 0; a < 2; ++a) {
      v.st[a] = float(double(v.pos[0]) * vecs[a][0] + double(v.pos[1]) * vecs[a][1] +
                      double(v.pos[2]) * vecs[a][2] + double(vecs[a][3]));
    }
    poly.push_back(uint32_t(map->vertices.size()));
    map->vertices.push_back(v);
  }
  fan(map, poly, nrm);
  return base;
}

bool check_tree(const Map& map, std::string* err) {
  const Family f = map.family;
  const auto child_ok = [&](int32_t c) {
    return c >= 0 ? size_t(c) < map.nodes.size() : size_t(-(int64_t(c) + 1)) < map.leaves.size();
  };
  for (size_t i = 0; i < map.nodes.size(); ++i) {
    const Node& n = map.nodes[i];
    if (n.plane >= map.planes.size() || !child_ok(n.child[0]) || !child_ok(n.child[1])) {
      return refuse(err, f,
                    "node " + std::to_string(i) + " names a plane or child that is not in the map",
                    "the file is damaged: rebuild the map with its compiler");
    }
  }
  if (map.leaves.empty()) {
    return refuse(err, f, "the map has no leaves", "the file is damaged: rebuild the map");
  }
  if (map.models.empty()) {
    return refuse(err, f, "the map has no models (model 0 is the world)",
                  "the file is damaged: rebuild the map");
  }
  for (size_t i = 0; i < map.models.size(); ++i) {
    const Model& m = map.models[i];
    if (!child_ok(m.head_node) || m.first_surface > map.surfaces.size() ||
        m.surface_count > map.surfaces.size() - m.first_surface) {
      return refuse(err, f,
                    "model " + std::to_string(i) + " names a head node or surfaces not in the map",
                    "the file is damaged: rebuild the map");
    }
  }
  for (size_t i = 0; i < map.leaves.size(); ++i) {
    if (map.leaves[i].cluster >= map.clusters) {
      return refuse(err, f,
                    "leaf " + std::to_string(i) + " is in cluster " +
                        std::to_string(map.leaves[i].cluster) + " of " +
                        std::to_string(map.clusters),
                    "the visibility is damaged: re-run the map's vis step");
    }
  }
  if (map.pvs.size() != size_t(map.clusters) * map.row_bytes) {
    return refuse(err, f, "the visibility rows do not cover every cluster",
                  "the visibility is damaged: re-run the map's vis step");
  }
  for (size_t s = 0; s < map.surfaces.size(); ++s) {
    const Surface& x = map.surfaces[s];
    if (x.texture >= map.textures.size() ||
        (x.lightmap >= 0 && size_t(x.lightmap) >= map.lightmaps.size())) {
      return refuse(err, f,
                    "surface " + std::to_string(s) + " names a texture or lightmap not read",
                    "the file is damaged: rebuild the map");
    }
  }
  return true;
}

}  // namespace detail

bool read_bsp(std::span<const uint8_t> bytes, Map* out, std::string* error,
              const ReadOptions& opts) {
  *out = Map{};
  if (bytes.size() < 8) {
    if (error) *error = "bsp: the file is under 8 bytes; next: check the path names a map file";
    return false;
  }
  detail::Cursor c{bytes};
  const uint32_t first = c.u32();
  bool ok = false;
  if (first == 29) {
    ok = read_v29(bytes, false, out, error);
  } else if (first == 0x32505342u) {  // "BSP2"
    ok = read_v29(bytes, true, out, error);
  } else if (first == 0x50534249u) {  // "IBSP"
    const int32_t version = c.i32();
    if (version == 38) {
      ok = read_v38(bytes, out, error);
    } else if (version == 46) {
      ok = read_v46(bytes, out, error, opts);
    } else {
      if (error) {
        *error = "bsp: IBSP version " + std::to_string(version) +
                 " is not read (38 and 46 are); next: convert the map to one of those";
      }
      return false;
    }
  } else if (first == 0x42535032u) {  // "2PSB"
    if (error) {
      *error = "bsp: the early 2PSB form is not read; next: recompile the map as BSP2";
    }
    return false;
  } else {
    char hex[16];
    std::snprintf(hex, sizeof hex, "%08x", unsigned(first));
    if (error) {
      *error = std::string("bsp: unknown first word 0x") + hex +
               " (29, BSP2 and IBSP 38/46 are read); next: check the file is a map";
    }
    return false;
  }
  if (!ok) return false;
  if (!parse_entities(out->entity_text, &out->entities, error)) {
    *error = std::string("bsp ") + family_name(out->family) + ": entity lump: " + *error;
    return false;
  }
  return detail::check_tree(*out, error);
}

bool read_lit(std::span<const uint8_t> bytes, Map* map, std::string* error) {
  if (map->family != Family::v29 && map->family != Family::bsp2) {
    if (error) *error = "lit: only a 29 or BSP2 map takes a .lit; next: drop the .lit";
    return false;
  }
  detail::Cursor c{bytes};
  const uint32_t magic = c.u32();
  const int32_t version = c.i32();
  if (c.bad || magic != 0x54494c51u || version != 1) {  // "QLIT"
    if (error) *error = "lit: not a version 1 QLIT file; next: rebuild it with the map's light";
    return false;
  }
  if (bytes.size() - 8 != map->light_rgb.size()) {
    if (error) {
      *error = "lit: " + std::to_string(bytes.size() - 8) + " bytes of luxels for a map with " +
               std::to_string(map->light_rgb.size()) +
               "; next: use the .lit from the same compile as the map";
    }
    return false;
  }
  std::copy(bytes.begin() + 8, bytes.end(), map->light_rgb.begin());
  return true;
}

bool parse_entities(std::string_view text, std::vector<Entity>* out, std::string* error) {
  out->clear();
  size_t at = 0;
  int line = 1;
  const auto fail = [&](const std::string& what) {
    if (error) {
      *error = "line " + std::to_string(line) + ": " + what +
               "; next: write blocks of { \"key\" \"value\" ... }";
    }
    return false;
  };
  // The next token: '{', '}', a quoted string (returned without its quotes), or end ("").
  const auto next = [&](std::string* tok, bool* quoted) {
    *quoted = false;
    for (;;) {
      while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' ||
                                  text[at] == '\n' || text[at] == '\0')) {
        line += text[at] == '\n';
        ++at;
      }
      if (at + 1 < text.size() && text[at] == '/' && text[at + 1] == '/') {
        while (at < text.size() && text[at] != '\n') ++at;
        continue;
      }
      break;
    }
    tok->clear();
    if (at >= text.size()) return true;
    if (text[at] == '{' || text[at] == '}') {
      *tok = text[at++];
      return true;
    }
    if (text[at] != '"') return false;
    const size_t end = text.find('"', at + 1);
    if (end == std::string_view::npos) return false;
    *tok = std::string(text.substr(at + 1, end - at - 1));
    for (char ch : *tok) line += ch == '\n';
    at = end + 1;
    *quoted = true;
    return true;
  };
  std::string tok;
  bool quoted = false;
  for (;;) {
    if (!next(&tok, &quoted)) return fail("expected { or a quoted string");
    if (tok.empty() && !quoted) return true;
    if (tok != "{" || quoted) return fail("expected { to open an entity, got \"" + tok + "\"");
    Entity e;
    for (;;) {
      if (!next(&tok, &quoted)) return fail("expected a quoted key or }");
      if (!quoted && tok == "}") break;
      if (!quoted) return fail("the entity is not closed with }");
      std::string key = tok;
      const int key_line = line;
      if (!next(&tok, &quoted) || !quoted) {
        line = key_line;
        return fail("key \"" + key + "\" has no quoted value");
      }
      e.pairs.emplace_back(std::move(key), tok);
    }
    out->push_back(std::move(e));
  }
}

}  // namespace ge::brush
