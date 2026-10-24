// engine/core/parallel.hpp: the bands cover every index once, in order, and a bake gives the
// same bits on any number of workers; the pool takes calls back to back, a call from inside a
// band, and calls from two threads at once; its listener hears every band; its workers name
// themselves (core/thread_name.hpp).
//
// ge:summary Test: core/parallel's bands, a bake's bits across worker counts, the pool's reentry
// ge:keywords test parallel threads workers bands pool nested concurrent listener thread-name
// ge:skill testing
#include "core/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "check.hpp"
#include "core/noise.hpp"
#include "core/thread_name.hpp"

namespace {

using ge::core::parallel_bands;

// Each band records itself; the bands sorted by start must tile [0, count).
std::vector<std::pair<size_t, size_t>> bands_of(size_t count, size_t workers) {
  std::vector<std::pair<size_t, size_t>> bands;
  std::mutex m;
  parallel_bands(count, workers, [&](size_t b, size_t e) {
    const std::lock_guard<std::mutex> lock(m);
    bands.emplace_back(b, e);
  });
  std::sort(bands.begin(), bands.end());
  return bands;
}

std::vector<double> bake(size_t workers) {
  constexpr size_t kN = 257;
  std::vector<double> out(kN * kN);
  parallel_bands(kN, workers, [&](size_t r0, size_t r1) {
    for (size_t r = r0; r < r1; r++) {
      ge::core::NoiseField f = ge::core::noise_field(37.5, 38);
      for (size_t c = 0; c < kN; c++) {
        out[r * kN + c] = ge::core::sample_noise(f, double(c) * 2.0, double(r) * 2.0);
      }
    }
  });
  return out;
}

}  // namespace

TEST(parallel_bands_tile_the_range) {
  for (const size_t count : {size_t{0}, size_t{1}, size_t{5}, size_t{1000}}) {
    for (const size_t workers : {size_t{0}, size_t{1}, size_t{3}, size_t{8}, size_t{16}}) {
      const auto bands = bands_of(count, workers);
      size_t at = 0;
      for (const auto& [b, e] : bands) {
        CHECK(b == at);
        CHECK(e > b);
        at = e;
      }
      CHECK(at == count);
      CHECK(bands.size() <= std::max<size_t>(workers, 1));
    }
  }
}

TEST(parallel_bake_is_the_same_bits_on_any_worker_count) {
  const std::vector<double> one = bake(1);
  for (const size_t workers : {size_t{2}, size_t{3}, size_t{7}, ge::core::default_workers()}) {
    const std::vector<double> many = bake(workers);
    CHECK(many.size() == one.size());
    for (size_t i = 0; i < one.size(); i++) CHECK_BITS(many[i], one[i]);
  }
}

TEST(parallel_default_workers_is_in_range) {
  const size_t w = ge::core::default_workers();
  CHECK(w >= 1);
  CHECK(w <= ge::core::kMaxWorkers);
}

// Thousands of small calls back to back: each sees every index once.
TEST(parallel_back_to_back_calls_each_cover_the_range) {
  std::vector<uint32_t> hits(64, 0);
  for (int call = 0; call < 5000; call++) {
    parallel_bands(hits.size(), 8, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; i++) hits[i]++;
    });
  }
  for (const uint32_t h : hits) CHECK(h == 5000);
}

// A band that calls parallel_bands again: the inner call runs on the band's thread, whole.
TEST(parallel_call_from_inside_a_band_covers_the_range) {
  constexpr size_t kOuter = 16, kInner = 100;
  std::vector<std::atomic<uint32_t>> hits(kOuter * kInner);
  parallel_bands(kOuter, 8, [&](size_t b, size_t e) {
    for (size_t o = b; o < e; o++) {
      parallel_bands(kInner, 4, [&](size_t ib, size_t ie) {
        for (size_t i = ib; i < ie; i++) hits[o * kInner + i]++;
      });
    }
  });
  for (const auto& h : hits) CHECK(h.load() == 1);
}

// Two threads baking at once: one owns the pool, the other runs its bands itself; both bakes
// are the one-worker bits.
TEST(parallel_bakes_from_two_threads_at_once_are_the_same_bits) {
  const std::vector<double> one = bake(1);
  std::vector<double> a, b;
  std::thread other([&] {
    for (int k = 0; k < 20; k++) b = bake(8);
  });
  for (int k = 0; k < 20; k++) a = bake(8);
  other.join();
  CHECK(a.size() == one.size());
  CHECK(b.size() == one.size());
  for (size_t i = 0; i < one.size(); i++) {
    CHECK_BITS(a[i], one[i]);
    CHECK_BITS(b[i], one[i]);
  }
}

namespace {
std::atomic<int> g_begins{0}, g_ends{0}, g_waits{0}, g_wait_ends{0};
void count_events(ge::core::PoolEvent e, bool) {
  switch (e) {
    case ge::core::PoolEvent::band_begin:
      g_begins++;
      break;
    case ge::core::PoolEvent::band_end:
      g_ends++;
      break;
    case ge::core::PoolEvent::spin_begin:
    case ge::core::PoolEvent::sleep_begin:
      g_waits++;
      break;
    case ge::core::PoolEvent::wait_end:
      g_wait_ends++;
      break;
  }
}
}  // namespace

// The listener hears every band begin and end, the same bits come out, and removing it stops it.
TEST(parallel_listener_hears_every_band) {
  const std::vector<double> one = bake(1);
  g_begins = g_ends = 0;
  ge::core::set_pool_listener(count_events);
  const std::vector<double> many = bake(4);
  ge::core::set_pool_listener(nullptr);
  for (size_t i = 0; i < one.size(); i++) CHECK_BITS(many[i], one[i]);
  if (ge::core::default_workers() > 1) {
    CHECK(g_begins.load() == static_cast<int>(std::min<size_t>(4, 257)));
    CHECK(g_ends.load() == g_begins.load());
  }
  const int heard = g_begins.load();
  bake(4);
  CHECK(g_begins.load() == heard);
}

// The pool's workers name themselves `pool N`; a thread's name and OS id are its own.
TEST(parallel_workers_name_themselves) {
  ge::core::name_this_thread("test main", false);
  CHECK(ge::core::this_thread_name() == "test main");
  const uint64_t me = ge::core::this_thread_os_id();
  CHECK(me != 0);
  std::mutex m;
  std::vector<std::string> names;
  std::vector<uint64_t> ids;
  parallel_bands(64, 8, [&](size_t, size_t) {
    // Long enough that the workers take bands too.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const std::lock_guard<std::mutex> lock(m);
    names.push_back(ge::core::this_thread_name());
    ids.push_back(ge::core::this_thread_os_id());
  });
  for (size_t i = 0; i < names.size(); i++) {
    if (ids[i] == me) {
      CHECK(names[i] == "test main");
    } else {
      CHECK(names[i].rfind("pool ", 0) == 0);
    }
  }
}
