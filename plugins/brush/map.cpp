// The map view's queries: a point's leaf down a model's tree, one cluster seeing another, the
// engine's frame, and the view's digest.
//
// ge:summary The map view's queries: leaf_at, cluster_sees, to_engine, map_digest, family names
// ge:keywords leaf_at point leaf tree walk cluster_sees pvs to_engine frame digest fnv
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#include <cstring>

#include "brush/map.hpp"

namespace ge::brush {

const char* family_name(Family f) {
  switch (f) {
    case Family::v29:
      return "29";
    case Family::bsp2:
      return "BSP2";
    case Family::v38:
      return "38";
    case Family::v46:
      return "46";
  }
  return "?";
}

const std::string& Entity::get(const std::string& key) const {
  static const std::string kNone;
  for (const auto& [k, v] : pairs) {
    if (k == key) return v;
  }
  return kNone;
}

uint32_t leaf_at(const Map& map, const float p[3], uint32_t model) {
  int32_t at = model < map.models.size() ? map.models[model].head_node : 0;
  // The reader checked every child is in range; a tree that loops still ends here.
  for (size_t steps = 0; at >= 0 && steps <= map.nodes.size(); ++steps) {
    const Node& n = map.nodes[size_t(at)];
    const Plane& pl = map.planes[n.plane];
    const float d = pl.n[0] * p[0] + pl.n[1] * p[1] + pl.n[2] * p[2] - pl.d;
    at = n.child[d >= 0 ? 0 : 1];
  }
  return at < 0 ? uint32_t(-(int64_t(at) + 1)) : 0;
}

bool cluster_sees(const Map& map, int32_t from, int32_t to) {
  if (from < 0 || to < 0 || from >= map.clusters || to >= map.clusters) return false;
  if (map.pvs.empty()) return true;
  const uint8_t byte = map.pvs[size_t(from) * map.row_bytes + size_t(to) / 8];
  return (byte >> (to % 8)) & 1;
}

void to_engine(const float p[3], double out[3]) {
  out[0] = double(p[0]) * kUnitM;
  out[1] = double(p[2]) * kUnitM;
  out[2] = double(p[1]) * kUnitM;
}

uint64_t map_digest(const Map& map) {
  uint64_t h = 0xcbf29ce484222325ull;
  const auto mix = [&](const void* data, size_t n) {
    const auto* b = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 0x100000001b3ull;
    }
  };
  const auto mix_u = [&](uint64_t v) { mix(&v, sizeof v); };
  mix_u(uint64_t(map.family));
  for (const Vertex& v : map.vertices) mix(&v, sizeof v);
  mix(map.indices.data(), map.indices.size() * 4);
  for (const Surface& s : map.surfaces) {
    mix_u(s.texture);
    mix_u(s.first);
    mix_u(s.count);
    mix_u(uint64_t(int64_t(s.lightmap)));
    mix_u(uint64_t(s.kind));
  }
  for (const Texture& t : map.textures) {
    mix(t.name.data(), t.name.size());
    mix_u(t.width);
    mix_u(t.height);
  }
  for (const Lightmap& l : map.lightmaps) {
    mix_u(l.width);
    mix_u(l.height);
    mix_u(l.offset);
  }
  mix(map.light_rgb.data(), map.light_rgb.size());
  for (const Plane& p : map.planes) mix(&p, sizeof p);
  for (const Node& n : map.nodes) mix(&n, sizeof n);
  for (const Leaf& l : map.leaves) {
    mix_u(uint64_t(int64_t(l.contents)));
    mix_u(uint64_t(int64_t(l.cluster)));
    mix(l.mins, sizeof l.mins);
    mix(l.maxs, sizeof l.maxs);
  }
  mix(map.pvs.data(), map.pvs.size());
  mix(map.entity_text.data(), map.entity_text.size());
  return h;
}

}  // namespace ge::brush
