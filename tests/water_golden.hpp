// The water tests' golden plumbing: GE_GOLDEN_WRITE read on every platform, and a table of bit
// rows written or held against tests/golden/<name>.txt with the message a moved golden owes.
//
// ge:summary Test helper: write or hold a water test's golden rows
// ge:keywords test golden water write rows
// ge:skill water testing
#pragma once

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "golden.hpp"

namespace ge::test {

inline bool golden_write_set() {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool set = _dupenv_s(&v, &n, "GE_GOLDEN_WRITE") == 0 && v != nullptr;
  std::free(v);
  return set;
#else
  return std::getenv("GE_GOLDEN_WRITE") != nullptr;
#endif
}

// Writes `got` to `rel` under GE_GOLDEN_WRITE, else holds it to the file's rows.
inline void hold_golden(const char* rel, const char* what,
                        const std::vector<std::vector<uint64_t>>& got) {
  if (golden_write_set()) {
    std::ofstream out(root_path(rel));
    out << "# " << what << "\n";
    for (const auto& row : got) {
      std::string line;
      for (const uint64_t word : row) {
        char b[24];
        std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(word));
        line += (line.empty() ? "" : " ") + std::string(b);
      }
      out << line << "\n";
    }
    return;
  }
  const auto want = golden_rows(rel);
  CHECK_MSG(!want.empty(), std::string(rel) + " is empty; write it with GE_GOLDEN_WRITE=1");
  CHECK_MSG(got == want, "the water moved; if the move is meant, rewrite " + std::string(rel) +
                             " with GE_GOLDEN_WRITE=1 on the release build and put the waves "
                             "lab's table before and after in the PR (skill: water)");
}

}  // namespace ge::test
