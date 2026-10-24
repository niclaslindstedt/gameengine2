// The OS sampler (cpu_sampler.hpp): each OS's reading of its threads and cores, and the
// sampling thread.
//
// ge:summary The OS sampler's readings on Linux, macOS and Windows, and its sampling thread
// ge:keywords prof cpu sampler proc schedstat stat task_threads thread_info host_processor_info
// ge:keywords toolhelp get-thread-times get-system-times
// ge:skill perf
// ge:test tests/prof_cpu_sampler_test.cpp
#include "prof/cpu_sampler.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "core/thread_name.hpp"
#include "prof/zones.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
// windows.h first: the snapshot's header needs its types.
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <pthread.h>
#elif defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ge::prof {

namespace {

#if defined(__linux__)

// A small /proc file's text ("" when it cannot be read).
std::string slurp(const char* path) {
  const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  std::string s;
  char buf[4096];
  for (;;) {
    const ssize_t n = ::read(fd, buf, sizeof buf);
    if (n <= 0) break;
    s.append(buf, static_cast<size_t>(n));
  }
  ::close(fd);
  return s;
}

int64_t ticks_ns() {
  static const int64_t hz = [] {
    const long v = sysconf(_SC_CLK_TCK);
    return v > 0 ? static_cast<int64_t>(v) : int64_t{100};
  }();
  return 1000000000 / hz;
}

// /proc/<..>/stat's utime and stime (fields 14 and 15), after the name's closing parenthesis.
int64_t stat_cpu_ns(const std::string& stat) {
  const size_t close = stat.rfind(')');
  if (close == std::string::npos) return 0;
  const char* p = stat.c_str() + close + 1;
  long long field[16] = {};
  // Fields 3.. after the name: state is text, then numbers; utime is the 12th after state.
  char state = 0;
  if (std::sscanf(p, " %c %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld", &state,
                  &field[0], &field[1], &field[2], &field[3], &field[4], &field[5], &field[6],
                  &field[7], &field[8], &field[9], &field[10], &field[11]) != 13) {
    return 0;
  }
  return (field[10] + field[11]) * ticks_ns();
}

#endif

}  // namespace

bool read_threads(std::vector<ThreadTime>& out) {
  out.clear();
#if defined(__linux__)
  DIR* dir = ::opendir("/proc/self/task");
  if (!dir) return false;
  while (const dirent* e = ::readdir(dir)) {
    if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
    ThreadTime t;
    t.os_id = std::strtoull(e->d_name, nullptr, 10);
    const std::string dir_of = std::string("/proc/self/task/") + e->d_name + "/";
    const std::string sched = slurp((dir_of + "schedstat").c_str());
    if (!sched.empty()) {
      t.cpu_ns = std::strtoll(sched.c_str(), nullptr, 10);
    } else {
      t.cpu_ns = stat_cpu_ns(slurp((dir_of + "stat").c_str()));
    }
    t.name = slurp((dir_of + "comm").c_str());
    while (!t.name.empty() && (t.name.back() == '\n' || t.name.back() == '\r')) t.name.pop_back();
    out.push_back(std::move(t));
  }
  ::closedir(dir);
  return !out.empty();
#elif defined(__APPLE__)
  thread_act_array_t list = nullptr;
  mach_msg_type_number_t count = 0;
  if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS) return false;
  for (mach_msg_type_number_t i = 0; i < count; i++) {
    ThreadTime t;
    thread_basic_info_data_t basic{};
    mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT;
    if (thread_info(list[i], THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&basic), &n) ==
        KERN_SUCCESS) {
      t.cpu_ns =
          (static_cast<int64_t>(basic.user_time.seconds) + basic.system_time.seconds) * 1000000000 +
          (static_cast<int64_t>(basic.user_time.microseconds) + basic.system_time.microseconds) *
              1000;
    }
    thread_identifier_info_data_t ident{};
    n = THREAD_IDENTIFIER_INFO_COUNT;
    if (thread_info(list[i], THREAD_IDENTIFIER_INFO, reinterpret_cast<thread_info_t>(&ident), &n) ==
        KERN_SUCCESS) {
      t.os_id = ident.thread_id;
    }
    thread_extended_info_data_t ext{};
    n = THREAD_EXTENDED_INFO_COUNT;
    if (thread_info(list[i], THREAD_EXTENDED_INFO, reinterpret_cast<thread_info_t>(&ext), &n) ==
        KERN_SUCCESS) {
      t.name.assign(ext.pth_name, strnlen(ext.pth_name, sizeof ext.pth_name));
    }
    mach_port_deallocate(mach_task_self(), list[i]);
    out.push_back(std::move(t));
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list),
                count * sizeof(thread_act_t));
  return !out.empty();
#elif defined(_WIN32)
  const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  using GetDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);
  static const auto describe = reinterpret_cast<GetDescription>(reinterpret_cast<void*>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
  const DWORD self = GetCurrentProcessId();
  THREADENTRY32 e{};
  e.dwSize = sizeof e;
  for (BOOL more = Thread32First(snap, &e); more; more = Thread32Next(snap, &e)) {
    if (e.th32OwnerProcessID != self) continue;
    ThreadTime t;
    t.os_id = e.th32ThreadID;
    if (const HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, e.th32ThreadID)) {
      FILETIME created, exited, kernel, user;
      if (GetThreadTimes(h, &created, &exited, &kernel, &user)) {
        const auto ns = [](const FILETIME& f) {
          return static_cast<int64_t>((static_cast<uint64_t>(f.dwHighDateTime) << 32) |
                                      f.dwLowDateTime) *
                 100;
        };
        t.cpu_ns = ns(kernel) + ns(user);
      }
      PWSTR wide = nullptr;
      if (describe && SUCCEEDED(describe(h, &wide)) && wide) {
        for (const wchar_t* c = wide; *c; c++) t.name += *c < 128 ? static_cast<char>(*c) : '?';
        LocalFree(wide);
      }
      CloseHandle(h);
    }
    out.push_back(std::move(t));
  }
  CloseHandle(snap);
  return !out.empty();
#else
  return false;
#endif
}

bool read_cores(std::vector<CoreTime>& out) {
  out.clear();
#if defined(__linux__)
  const std::string stat = slurp("/proc/stat");
  size_t at = 0;
  while (at < stat.size()) {
    const size_t end = stat.find('\n', at);
    const std::string line = stat.substr(at, end == std::string::npos ? end : end - at);
    at = end == std::string::npos ? stat.size() : end + 1;
    // `cpuN user nice system idle iowait irq softirq steal ...`; the `cpu ` total row is skipped.
    if (line.size() < 4 || line.compare(0, 3, "cpu") != 0 || line[3] < '0' || line[3] > '9') {
      continue;
    }
    long long v[8] = {};
    const char* p = line.c_str() + 3;
    while (*p >= '0' && *p <= '9') p++;
    const int got = std::sscanf(p, " %lld %lld %lld %lld %lld %lld %lld %lld", &v[0], &v[1], &v[2],
                                &v[3], &v[4], &v[5], &v[6], &v[7]);
    if (got < 4) continue;
    CoreTime c;
    for (int i = 0; i < got; i++) c.total += v[i];
    c.busy = c.total - v[3] - (got > 4 ? v[4] : 0);
    out.push_back(c);
  }
  return !out.empty();
#elif defined(__APPLE__)
  natural_t cores = 0;
  processor_info_array_t info = nullptr;
  mach_msg_type_number_t n = 0;
  if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cores, &info, &n) !=
      KERN_SUCCESS) {
    return false;
  }
  const auto* load = reinterpret_cast<const processor_cpu_load_info_data_t*>(info);
  for (natural_t i = 0; i < cores; i++) {
    const unsigned* t = load[i].cpu_ticks;
    CoreTime c;
    c.busy = static_cast<int64_t>(t[CPU_STATE_USER]) + t[CPU_STATE_SYSTEM] + t[CPU_STATE_NICE];
    c.total = c.busy + t[CPU_STATE_IDLE];
    out.push_back(c);
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info), n * sizeof(integer_t));
  return !out.empty();
#elif defined(_WIN32)
  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user)) return false;
  const auto ticks = [](const FILETIME& f) {
    return static_cast<int64_t>((static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime);
  };
  CoreTime c;
  c.total = ticks(kernel) + ticks(user);  // the kernel's time holds the idle time
  c.busy = c.total - ticks(idle);
  out.push_back(c);
  return true;
#else
  return false;
#endif
}

bool cores_are_whole_machine() {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

namespace {
std::mutex g_wait_lock;
std::condition_variable g_wake;
}  // namespace

void CpuSampler::start(int64_t period_us, int64_t origin_ns) {
  stop();
  samples_.clear();
  period_us_ = period_us > 0 ? period_us : 5000;
  origin_ns_ = origin_ns;
  running_.store(true);
  thread_ = std::thread([this] {
    core::name_this_thread("sampler");
    os_id_.store(core::this_thread_os_id());
    auto next = std::chrono::steady_clock::now();
    while (running_.load()) {
      sample();
      next += std::chrono::microseconds(period_us_);
      std::unique_lock<std::mutex> lock(g_wait_lock);
      g_wake.wait_until(lock, next, [this] { return !running_.load(); });
    }
    sample();
  });
}

void CpuSampler::stop() {
  if (!thread_.joinable()) return;
  {
    const std::lock_guard<std::mutex> lock(g_wait_lock);
    running_.store(false);
  }
  g_wake.notify_all();
  thread_.join();
}

void CpuSampler::sample() {
  CpuSample s;
  const int64_t t0 = now_ns();
  read_threads(s.threads);
  read_cores(s.cores);
  s.t_ns = (t0 + now_ns()) / 2 - origin_ns_;  // the readings' middle
  samples_.push_back(std::move(s));
}

}  // namespace ge::prof
