// INDEPENDENT ROWS SIDE BY SIDE. A bake whose every output (a grid row, a cell) is a pure
// function of its index splits [0, count) into contiguous bands and runs them on the calling
// thread and a persistent pool of worker threads. The result is the same bits for any number of
// workers and any schedule, because no output is written by two bands and nothing is summed
// across them; the caller keeps that promise. Never inside a simulation step: a step runs on one
// thread (the `perf` skill, § Levers).
//
// THE POOL is started on the first call that has two bands or more, holds the machine's hardware
// threads less one (the caller is the other), at most kMaxWorkers - 1, and lives until the
// process exits. A call publishes its bands and every thread takes the next one left until none
// is, so a call costs a wake-up, not a thread start. Between calls a worker spins briefly
// (back-to-back calls, a bake's usual shape, find it awake), then sleeps. One call owns the pool
// at a time: a call made while it is owned (a band calling parallel_bands, or another thread's
// bake) runs its bands in order on its own thread, the same bits.
//
// ge:summary Runs a bake's independent rows in contiguous bands on a persistent worker pool
// ge:keywords parallel threads workers bands rows bake parallel_bands default_workers
// ge:keywords multithreading thread-pool pool persistent dispatch overhead
// ge:keywords pool-listener set_pool_listener pool-event utilisation idle spin
// ge:skill perf determinism
// ge:test tests/core_parallel_test.cpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>

namespace ge::core {

// At most this many threads a bake: beyond it the bands' memory traffic, not the cores, sets
// the pace of a grid bake.
inline constexpr size_t kMaxWorkers = 8;

// The machine's hardware threads, 1 to kMaxWorkers (1 when the platform cannot tell).
inline size_t default_workers() {
  const size_t n = std::thread::hardware_concurrency();
  return std::clamp<size_t>(n, 1, kMaxWorkers);
}

// What a pool thread tells the listener. A wait is a spin (the thread keeps its core) or a
// sleep (it gives it back), and ends at `wait_end` (a spin that gives up and sleeps is ended by
// its `sleep_begin`); `idle` says what it waited for: true, a worker
// waiting for the next call; false, a caller waiting for the last band of its own.
enum class PoolEvent : uint8_t { band_begin, band_end, spin_begin, sleep_begin, wait_end };
using PoolListener = void (*)(PoolEvent event, bool idle);

// Installs the listener (nullptr removes it), at any time: a listener installed mid-call hears
// ends whose begins it missed, and ignores them.
void set_pool_listener(PoolListener listener);

namespace detail {

// One call's bands with its body behind a plain function pointer, so the pool is compiled once.
struct BandJob {
  void (*run)(void* body, size_t begin, size_t end);
  void* body;
  size_t count;
  size_t bands;
};

// Every band of `job` once, on the calling thread and the pool's; returns when all are done.
void run_bands(const BandJob& job);

}  // namespace detail

// body(begin, end) once per band, the bands covering [0, count) with no overlap: band b is
// [count * b / bands, count * (b + 1) / bands), bands = min(workers, count). Which thread runs a
// band, and in which order, is not fixed; body must not throw. Returns once every band is done.
template <class Body>
void parallel_bands(size_t count, size_t workers, Body&& body) {
  const size_t bands = std::min(std::max<size_t>(workers, 1), count);
  if (bands <= 1) {
    if (count > 0) body(size_t{0}, count);
    return;
  }
  using B = std::remove_reference_t<Body>;
  detail::run_bands({[](void* b, size_t lo, size_t hi) { (*static_cast<B*>(b))(lo, hi); },
                     const_cast<void*>(static_cast<const void*>(std::addressof(body))), count,
                     bands});
}

template <class Body>
void parallel_bands(size_t count, Body&& body) {
  parallel_bands(count, default_workers(), static_cast<Body&&>(body));
}

}  // namespace ge::core
