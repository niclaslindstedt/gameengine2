// The grass tests' golden plumbing: rows of bit words written under GE_GOLDEN_WRITE or held
// against tests/golden/<name>.txt, with the message a moved golden owes.
//
// ge:summary Test helper: write or hold a grass test's golden rows
// ge:keywords test golden grass write rows
// ge:skill grass testing
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "grass/kind.hpp"
#include "water_golden.hpp"

namespace ge::test {

// A preset by name; the test fails to build its scene if the name is wrong (an empty Kind's
// zero density grows nothing, which every test that uses one would see).
inline ge::grass::Kind grass_preset(std::string_view name) {
  for (const auto& k : ge::grass::presets())
    if (k.name == name) return k;
  return ge::grass::Kind{.name = "missing", .density = 0};
}

inline uint64_t fbits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof u);
  return u;
}

inline void hold_grass_golden(const char* rel, const std::vector<std::vector<uint64_t>>& got) {
  if (golden_write_set()) {
    hold_golden(rel, "grass golden: engine/grass, written with GE_GOLDEN_WRITE=1", got);
    return;
  }
  const auto want = golden_rows(rel);
  CHECK_MSG(!want.empty(), std::string(rel) + " is empty; write it with GE_GOLDEN_WRITE=1");
  CHECK_MSG(got == want, "the grass moved; if the move is meant, rewrite " + std::string(rel) +
                             " with GE_GOLDEN_WRITE=1 and put the grass lab's pictures before "
                             "and after in the PR (skill: grass)");
}

}  // namespace ge::test
