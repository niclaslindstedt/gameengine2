// A snow layer's snapshot (snapshot.hpp).
//
// ge:summary The snow snapshot's writer and readers, little-endian
// ge:keywords snow snapshot save load read_info load_bare little-endian
// ge:skill snow
// ge:test tests/snow_layer_test.cpp
#include "snow/snapshot.hpp"

#include <cstring>

namespace ge::snow {

namespace {

constexpr char kMagic[8] = {'G', 'E', 'S', 'N', 'O', 'W', '1', '\0'};
constexpr size_t kHeader = 8 + 5 * 8 + 2 * 8 + 4;
constexpr size_t kCellBytes = 8;
constexpr size_t kTileBytes = 8 + static_cast<size_t>(kTileCells) * kTileCells * kCellBytes;

void put(std::vector<uint8_t>& out, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put_double(std::vector<uint8_t>& out, double d) {
  uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof bits);
  put(out, bits, 8);
}

uint64_t get(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; i++) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}

double get_double(const uint8_t* p) {
  const uint64_t bits = get(p, 8);
  double d = 0;
  std::memcpy(&d, &bits, sizeof d);
  return d;
}

}  // namespace

std::vector<uint8_t> save(const Layer& layer) {
  std::vector<uint8_t> out;
  out.reserve(kHeader + layer.tiles.size() * kTileBytes);
  out.insert(out.end(), kMagic, kMagic + 8);
  const Region& r = layer.spec.region;
  for (const double d : {r.x0, r.z0, r.x1, r.z1, layer.spec.cell}) put_double(out, d);
  put(out, layer.mass_in, 8);
  put(out, layer.spilled, 8);
  put(out, layer.tiles.size(), 4);
  for (const Tile& t : layer.tiles) {
    put(out, static_cast<uint32_t>(t.tx), 4);
    put(out, static_cast<uint32_t>(t.tz), 4);
    for (const Cell& c : t.cells) {
      put(out, c.depth_mm, 2);
      put(out, c.mass, 2);
      put(out, static_cast<uint8_t>(c.kind), 1);
      put(out, c.flags, 1);
      put(out, c.minute, 2);
    }
  }
  return out;
}

bool read_info(const std::vector<uint8_t>& bytes, SnapshotInfo& info, std::string& error) {
  if (bytes.size() < kHeader || std::memcmp(bytes.data(), kMagic, 8) != 0) {
    error = "not a snow snapshot (no GESNOW1 header): pass a file written by snow::save";
    return false;
  }
  const uint8_t* p = bytes.data() + 8;
  info.region = {get_double(p), get_double(p + 8), get_double(p + 16), get_double(p + 24)};
  info.cell = get_double(p + 32);
  info.mass_in = get(p + 40, 8);
  info.spilled = get(p + 48, 8);
  info.tiles = static_cast<uint32_t>(get(p + 56, 4));
  if (bytes.size() != kHeader + static_cast<size_t>(info.tiles) * kTileBytes) {
    error = "the snapshot is " + std::to_string(bytes.size()) + " bytes but its " +
            std::to_string(info.tiles) + " tiles need " +
            std::to_string(kHeader + static_cast<size_t>(info.tiles) * kTileBytes) +
            ": the file is cut short or not a snapshot; write it again";
    return false;
  }
  return true;
}

bool load(Layer& layer, const std::vector<uint8_t>& bytes, std::string& error) {
  SnapshotInfo info;
  if (!read_info(bytes, info, error)) return false;
  const Region& r = layer.spec.region;
  if (info.region.x0 != r.x0 || info.region.z0 != r.z0 || info.region.x1 != r.x1 ||
      info.region.z1 != r.z1 || info.cell != layer.spec.cell) {
    error =
        "the snapshot was taken of another layer (its region or cell differs): load it into "
        "a layer made from the spec it was saved from";
    return false;
  }
  if (info.tiles > layer.spec.max_tiles) {
    error = "the snapshot holds " + std::to_string(info.tiles) +
            " tiles, over the layer's budget of " + std::to_string(layer.spec.max_tiles) +
            ": raise LayerSpec::max_tiles";
    return false;
  }
  layer.tiles.clear();
  layer.slots.assign(layer.slots.size(), -1);
  const uint8_t* p = bytes.data() + kHeader;
  for (uint32_t i = 0; i < info.tiles; i++, p += kTileBytes) {
    Tile t;
    t.tx = static_cast<int32_t>(get(p, 4));
    t.tz = static_cast<int32_t>(get(p + 4, 4));
    if (t.tx < 0 || t.tz < 0 || t.tx >= layer.tile_cols || t.tz >= layer.tile_rows) {
      error = "tile " + std::to_string(i) + " lies outside the layer: the snapshot is corrupt";
      return false;
    }
    int32_t& slot = layer.slots[static_cast<size_t>(t.tz) * layer.tile_cols + t.tx];
    if (slot >= 0) {
      error = "tile " + std::to_string(i) + " is written twice: the snapshot is corrupt";
      return false;
    }
    const uint8_t* q = p + 8;
    for (Cell& c : t.cells) {
      c.depth_mm = static_cast<uint16_t>(get(q, 2));
      c.mass = static_cast<uint16_t>(get(q + 2, 2));
      c.kind = static_cast<Kind>(q[4] < static_cast<uint8_t>(Kind::count) ? q[4] : 0);
      c.flags = q[5];
      c.minute = static_cast<uint16_t>(get(q + 6, 2));
      q += kCellBytes;
    }
    slot = static_cast<int32_t>(layer.tiles.size());
    layer.tiles.push_back(t);
  }
  layer.mass_in = info.mass_in;
  layer.spilled = info.spilled;
  return true;
}

bool load_bare(SnowWorld& world, const std::vector<uint8_t>& bytes, std::string& error) {
  SnapshotInfo info;
  if (!read_info(bytes, info, error)) return false;
  LayerSpec spec;
  spec.region = info.region;
  spec.cell = info.cell;
  spec.cover.depth = 0;
  spec.max_tiles = info.tiles;
  Layer& layer = add_layer(world, spec);
  return load(layer, bytes, error);
}

}  // namespace ge::snow
