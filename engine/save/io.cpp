// The save Reader's refusals: the first problem kept with the byte it happened at.
//
// ge:summary The save Reader's refusals: overruns, lengths past the end, bools not 0 or 1
// ge:keywords save reader problem overrun length bool refuse
// ge:skill save
// ge:test tests/save_io_test.cpp
#include "save/io.hpp"

namespace ge::save {

void Reader::fail(const std::string& what) {
  if (ok()) problem_ = "at byte " + std::to_string(offset()) + ": " + what;
  at_ = end_;
}

void Reader::overrun(size_t n) {
  fail("a read of " + std::to_string(n) + " bytes runs past the end (" + std::to_string(left()) +
       " left)");
}

void Reader::length(uint32_t& n, size_t min_item) {
  raw(&n, 4);
  const size_t least = min_item ? min_item : 1;
  if (ok() && n > left() / least) {
    const uint32_t bad = n;
    n = 0;
    fail("a length of " + std::to_string(bad) + " items of at least " + std::to_string(least) +
         " bytes runs past the end (" + std::to_string(left()) + " left)");
  }
  if (!ok()) n = 0;
}

void Reader::boolean(bool& b) {
  uint8_t v = 0;
  raw(&v, 1);
  if (v > 1) fail("a bool reads " + std::to_string(v) + ", not 0 or 1");
  b = v == 1;
}

}  // namespace ge::save
