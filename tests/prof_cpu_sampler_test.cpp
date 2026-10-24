// engine/prof/cpu_sampler.hpp: the OS lists this thread under the id core/thread_name.hpp gives,
// its CPU time grows with work, the cores read, and the sampler's rows come in time order with
// its own thread named. On an OS with no answer the readers say so and the test skips.
//
// ge:summary Test: the OS sampler's threads, cores and samples
// ge:keywords test prof cpu sampler os threads cores cpu-time
// ge:skill testing perf
#include "prof/cpu_sampler.hpp"

#include <chrono>
#include <thread>

#include "check.hpp"
#include "core/thread_name.hpp"

namespace {

namespace pf = ge::prof;

const pf::ThreadTime* find(const std::vector<pf::ThreadTime>& ts, uint64_t id) {
  for (const pf::ThreadTime& t : ts) {
    if (t.os_id == id) return &t;
  }
  return nullptr;
}

void burn(std::chrono::milliseconds d) {
  volatile uint64_t x = 1;
  const auto end = std::chrono::steady_clock::now() + d;
  while (std::chrono::steady_clock::now() < end) {
    for (int i = 0; i < 1000; i++) x = x * 6364136223846793005ULL + 1;
  }
}

}  // namespace

TEST(sampler_lists_this_thread_and_its_time_grows) {
  std::vector<pf::ThreadTime> a, b;
  if (!pf::read_threads(a)) {
    ge::test::skip("the OS gives no per-thread CPU time here");
    return;
  }
  const uint64_t me = ge::core::this_thread_os_id();
  const pf::ThreadTime* before = find(a, me);
  CHECK(before != nullptr);
  const int64_t t0 = before ? before->cpu_ns : 0;
  burn(std::chrono::milliseconds(40));
  CHECK(pf::read_threads(b));
  const pf::ThreadTime* after = find(b, me);
  CHECK(after && after->cpu_ns > t0);
}

TEST(sampler_reads_the_cores) {
  std::vector<pf::CoreTime> c;
  if (!pf::read_cores(c)) {
    ge::test::skip("the OS gives no core times here");
    return;
  }
  CHECK(!c.empty());
  if (pf::cores_are_whole_machine()) CHECK(c.size() == 1);
  for (const pf::CoreTime& k : c) CHECK(k.total >= k.busy && k.busy >= 0);
}

TEST(sampler_samples_in_order_and_names_itself) {
  std::vector<pf::ThreadTime> probe;
  if (!pf::read_threads(probe)) {
    ge::test::skip("the OS gives no per-thread CPU time here");
    return;
  }
  pf::CpuSampler s;
  s.start(1000, 0);
  burn(std::chrono::milliseconds(20));
  s.stop();
  CHECK(s.samples().size() >= 2);
  CHECK(s.os_id() != 0);
  for (size_t i = 1; i < s.samples().size(); i++) {
    CHECK(s.samples()[i].t_ns >= s.samples()[i - 1].t_ns);
  }
  const pf::ThreadTime* self = find(s.samples().back().threads, s.os_id());
  CHECK(self && self->name.rfind("ge-sampler", 0) == 0);
  // A restart clears the last run's rows.
  s.start(1000, 0);
  s.stop();
  CHECK(s.samples().size() < 10);
}
