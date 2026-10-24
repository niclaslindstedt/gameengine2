// THE POOL behind core::parallel_bands (parallel.hpp): its worker threads, how a call hands them
// its bands, and how a call waits for the last one.
//
// A call: take ownership of the pool (or, when it is owned, run the bands in order here),
// publish the job (reset the counters, store the job, bump the generation), wake any sleeping
// worker, take bands off `next` itself until none is left, wait for `done` to reach the band
// count, withdraw the job and wait for every worker that may still hold it to let go. A worker:
// wait for a new generation (spinning, then asleep), announce itself in `inside`, read the job,
// take bands off `next` until none is left, leave `inside`. Every atomic is sequentially
// consistent: the caller's "withdraw, then read `inside`" and the worker's "enter `inside`, then
// read the job" are the two halves of one handshake, as are "bump the generation, then read
// `sleepers`" and "count yourself a sleeper, then read the generation".
//
// ge:summary The persistent worker pool behind parallel_bands: publish, take bands, wait
// ge:keywords parallel thread-pool pool workers spin sleep wake generation bands dispatch
// ge:keywords pool-listener tell
// ge:skill perf determinism
// ge:test tests/core_parallel_test.cpp
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/parallel.hpp"
#include "core/thread_name.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace ge::core {

namespace {
std::atomic<PoolListener> g_listener{nullptr};
}  // namespace

void set_pool_listener(PoolListener listener) { g_listener.store(listener); }

namespace detail {
namespace {

// Tells the listener, when there is one.
inline void tell(PoolEvent e, bool idle) {
  if (const PoolListener l = g_listener.load(std::memory_order_relaxed)) l(e, idle);
}

// How long an idle worker keeps looking for the next call before it sleeps, and how long a
// caller spins for the last band before it sleeps. Short: a spinning thread takes a core from
// the serial work between two bakes.
constexpr auto kSpin = std::chrono::microseconds(50);

// A spin loop's breath: tells the core this thread is waiting.
inline void relax() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield");
#else
  std::this_thread::yield();
#endif
}

// Spins until `ready()` or kSpin has passed; true when ready. A wait that is ready at once is
// not a wait, so the listener hears of it only after the first round.
template <class Ready>
bool spin_for(Ready&& ready, bool idle) {
  for (int i = 0; i < 64; i++) {
    if (ready()) return true;
    relax();
  }
  tell(PoolEvent::spin_begin, idle);
  const auto until = std::chrono::steady_clock::now() + kSpin;
  for (;;) {
    for (int i = 0; i < 64; i++) {
      if (ready()) {
        tell(PoolEvent::wait_end, idle);
        return true;
      }
      relax();
    }
    if (std::chrono::steady_clock::now() >= until) {
      const bool r = ready();
      if (r) tell(PoolEvent::wait_end, idle);
      return r;
    }
  }
}

class Pool {
 public:
  Pool() {
    const size_t helpers = default_workers() - 1;
    threads_.reserve(helpers);
    for (size_t i = 0; i < helpers; i++) {
      threads_.emplace_back([this, i] {
        name_this_thread("pool " + std::to_string(i + 1));
        work();
      });
    }
  }

  ~Pool() {
    {
      const std::lock_guard<std::mutex> lock(m_);
      stop_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : threads_) t.join();
  }

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  // Runs every band of `job`; false (nothing run) when another call owns the pool.
  bool run(const BandJob& job) {
    if (threads_.empty() || owned_.exchange(true)) return false;
    next_.store(0);
    done_.store(0);
    job_.store(&job);
    generation_.fetch_add(1);
    if (sleepers_.load() > 0) {
      {
        const std::lock_guard<std::mutex> lock(m_);
      }
      wake_.notify_all();
    }
    take_bands(job);
    if (!spin_for([&] { return done_.load() == job.bands; }, false)) {
      tell(PoolEvent::sleep_begin, false);
      std::unique_lock<std::mutex> lock(m_);
      finished_.wait(lock, [&] { return done_.load() == job.bands; });
      lock.unlock();
      tell(PoolEvent::wait_end, false);
    }
    job_.store(nullptr);
    while (inside_.load() != 0) relax();  // a worker between reading the job and its last band
    owned_.store(false);
    return true;
  }

 private:
  void take_bands(const BandJob& job) {
    for (size_t b = next_.fetch_add(1); b < job.bands; b = next_.fetch_add(1)) {
      tell(PoolEvent::band_begin, false);
      job.run(job.body, job.count * b / job.bands, job.count * (b + 1) / job.bands);
      tell(PoolEvent::band_end, false);
      if (done_.fetch_add(1) + 1 == job.bands) {
        {
          const std::lock_guard<std::mutex> lock(m_);
        }
        finished_.notify_all();
      }
    }
  }

  void work() {
    uint64_t seen = 0;
    for (;;) {
      if (!spin_for([&] { return generation_.load() != seen; }, true)) {
        tell(PoolEvent::sleep_begin, true);
        std::unique_lock<std::mutex> lock(m_);
        sleepers_.fetch_add(1);
        wake_.wait(lock, [&] { return stop_ || generation_.load() != seen; });
        sleepers_.fetch_sub(1);
        if (stop_) return;
        lock.unlock();
        tell(PoolEvent::wait_end, true);
      }
      seen = generation_.load();
      inside_.fetch_add(1);
      if (const BandJob* job = job_.load()) take_bands(*job);
      inside_.fetch_sub(1);
    }
  }

  std::vector<std::thread> threads_;
  std::mutex m_;
  std::condition_variable wake_;      // a worker asleep: a new generation, or stop
  std::condition_variable finished_;  // a caller asleep: the last band done
  bool stop_ = false;                 // under m_
  std::atomic<bool> owned_{false};
  std::atomic<const BandJob*> job_{nullptr};
  std::atomic<uint64_t> generation_{0};
  std::atomic<size_t> next_{0};
  std::atomic<size_t> done_{0};
  std::atomic<size_t> inside_{0};
  std::atomic<size_t> sleepers_{0};
};

}  // namespace

void run_bands(const BandJob& job) {
  static Pool pool;
  if (pool.run(job)) return;
  for (size_t b = 0; b < job.bands; b++) {
    job.run(job.body, job.count * b / job.bands, job.count * (b + 1) / job.bands);
  }
}

}  // namespace detail
}  // namespace ge::core
