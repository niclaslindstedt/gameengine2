// A LAYER'S SNAPSHOT: what its tiles hold, as bytes, for a save game, a late joiner and `ge snow`.
// The cover and the ground are the game's data and are not written: a snapshot is loaded into a
// layer made from the same spec, and only the tiles (and the mass bookkeeping) come back.
//
// THE FORMAT, little-endian throughout:
//   "GESNOW1\0"                     8 bytes
//   region x0 z0 x1 z1, cell        5 doubles (the layer it was taken of, checked on load)
//   mass_in, spilled                2 uint64
//   tile count                      uint32
//   per tile: tx, tz (int32), then 4096 cells of depth_mm (u16) mass (u16) kind (u8) flags (u8)
//   minute (u16), in sample order
//
// ge:summary A snow layer's tiles saved to bytes and loaded back (save games, ge snow)
// ge:keywords snow snapshot save load bytes gesnow format serialize late-join save-game
// ge:skill snow
// ge:test tests/snow_layer_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "snow/layer.hpp"

namespace ge::snow {

std::vector<uint8_t> save(const Layer& layer);

// Loads `bytes` into `layer` (made from the spec it was taken of): true, or false with `error`
// saying what did not match and what to do.
bool load(Layer& layer, const std::vector<uint8_t>& bytes, std::string& error);

// The header alone, for a reader with no spec (`ge snow`): the region, the cell and the tiles.
struct SnapshotInfo {
  Region region;
  double cell = 0;
  uint64_t mass_in = 0, spilled = 0;
  uint32_t tiles = 0;
};
bool read_info(const std::vector<uint8_t>& bytes, SnapshotInfo& info, std::string& error);

// A layer rebuilt from a snapshot alone, over a flat ground and an empty cover (`ge snow`): its
// tiles are the snapshot's and every untouched sample reads no snow.
bool load_bare(SnowWorld& world, const std::vector<uint8_t>& bytes, std::string& error);

}  // namespace ge::snow
