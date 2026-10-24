// A damaged map is refused or read whole, never read out of range: the fixture of every family
// mutated thousands of ways (bytes flipped, the file cut short, words written over the lump
// directory and the records) from a seeded stream, every mutant either refused with a reason and
// a next step, or read into a view whose every index is in range and whose tree a point walk
// leaves. The sanitizer rows make an out-of-range read a failure here.
//
// ge:summary Test: thousands of mutated BSP maps are refused or read in range, never overrun
// ge:keywords test brush bsp fuzz mutate damage refuse bounds sanitizer v29 bsp2 v38 v46
// ge:skill brush testing
#include <string>

#include "brush/read.hpp"
#include "brush/write.hpp"
#include "check.hpp"
#include "core/prng.hpp"

namespace {

namespace br = ge::brush;
namespace core = ge::core;

void mutate(core::Rng& rng, std::vector<uint8_t>& b) {
  if (b.size() < 8) return;
  switch (core::int_in(rng, 0, 4)) {
    case 0:  // a few bytes flipped anywhere
      for (int k = core::int_in(rng, 1, 8); k > 0; --k) {
        b[core::pick_index(rng, b.size())] ^= uint8_t(core::int_in(rng, 1, 255));
      }
      break;
    case 1:  // cut short
      b.resize(core::pick_index(rng, b.size()));
      break;
    case 2: {  // a word over the lump directory
      const size_t at = 4 + 4 * core::pick_index(rng, 40);
      const auto v = uint32_t(core::int_in(rng, -100000, 100000));
      for (int k = 0; k < 4 && at + size_t(k) < b.size(); ++k)
        b[at + size_t(k)] = uint8_t(v >> (8 * k));
      break;
    }
    case 3: {  // a small word over a record (an index, a count, an offset)
      const size_t at = core::pick_index(rng, b.size() / 4) * 4;
      const auto v = uint32_t(core::int_in(rng, -70000, 70000));
      for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (8 * k));
      break;
    }
    default: {  // a byte set to its extremes
      b[core::pick_index(rng, b.size())] = core::chance(rng, 0.5) ? 0xff : 0x80;
      break;
    }
  }
}

// Every index in the view in range.
bool sound(const br::Map& m) {
  for (uint32_t i : m.indices) {
    if (i >= m.vertices.size()) return false;
  }
  for (const br::Surface& s : m.surfaces) {
    if (s.first > m.indices.size() || s.count > m.indices.size() - s.first) return false;
    if (s.texture >= m.textures.size()) return false;
    if (s.lightmap >= 0) {
      if (size_t(s.lightmap) >= m.lightmaps.size()) return false;
      const br::Lightmap& l = m.lightmaps[size_t(s.lightmap)];
      if (l.offset + uint64_t(l.width) * l.height * 3 > m.light_rgb.size()) return false;
    }
  }
  return true;
}

}  // namespace

TEST(mutated_maps_are_refused_or_read_in_range) {
  const br::Family families[] = {br::Family::v29, br::Family::bsp2, br::Family::v38,
                                 br::Family::v46};
  core::Rng rng = core::make_rng(20261023u);
  for (br::Family f : families) {
    std::vector<uint8_t> clean;
    std::string error;
    CHECK(br::write_bsp(br::sample_fixture(), f, &clean, &error));
    int read = 0, refused = 0;
    for (int trial = 0; trial < 800; ++trial) {
      std::vector<uint8_t> b = clean;
      for (int k = core::int_in(rng, 1, 3); k > 0 && !b.empty(); --k) mutate(rng, b);
      br::Map m;
      if (!br::read_bsp(b, &m, &error)) {
        ++refused;
        CHECK(error.find("next:") != std::string::npos);
        continue;
      }
      ++read;
      CHECK(sound(m));
      for (int k = 0; k < 16; ++k) {
        const float p[3] = {float(core::range(rng, -200, 1000)),
                            float(core::range(rng, -200, 1000)), float(core::range(rng, -50, 400))};
        const uint32_t leaf = br::leaf_at(m, p, uint32_t(core::pick_index(rng, m.models.size())));
        CHECK(leaf < m.leaves.size());
        br::cluster_sees(m, m.leaves[leaf].cluster, int32_t(core::int_in(rng, -2, 9)));
      }
    }
    // Both outcomes are exercised: a mutant often lands in pixels or luxels and still reads.
    CHECK(read > 50 && refused > 50);
  }
}
