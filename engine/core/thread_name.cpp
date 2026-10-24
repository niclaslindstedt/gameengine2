// The thread's name and OS id (thread_name.hpp): the per-thread copy and each OS's calls.
//
// ge:summary A thread's name and OS id: the thread-local copy and the OS's naming calls
// ge:keywords thread name tid pthread_setname_np set-thread-description gettid
// ge:skill perf
// ge:test tests/core_parallel_test.cpp
#include "core/thread_name.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#else
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ge::core {

namespace {

std::string& name_slot() {
  thread_local std::string name;
  return name;
}

// The OS's spelling: `ge-` and the name, spaces as dashes, at most 15 characters.
std::string os_name(std::string_view name) {
  std::string s = "ge-";
  for (const char c : name) s += c == ' ' ? '-' : c;
  if (s.size() > 15) s.resize(15);
  return s;
}

}  // namespace

void name_this_thread(std::string_view name, bool tell_os) {
  name_slot().assign(name);
  if (!tell_os) return;
  const std::string os = os_name(name);
#if defined(_WIN32)
  // SetThreadDescription is Windows 10's; looked up so an older system only skips the name.
  using SetDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);
  static const auto set = reinterpret_cast<SetDescription>(reinterpret_cast<void*>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
  if (set) {
    std::wstring wide(os.begin(), os.end());
    set(GetCurrentThread(), wide.c_str());
  }
#elif defined(__APPLE__)
  pthread_setname_np(os.c_str());
#else
  pthread_setname_np(pthread_self(), os.c_str());
#endif
}

const std::string& this_thread_name() { return name_slot(); }

uint64_t this_thread_os_id() {
#if defined(_WIN32)
  return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(__APPLE__)
  uint64_t id = 0;
  pthread_threadid_np(nullptr, &id);
  return id;
#else
  return static_cast<uint64_t>(syscall(SYS_gettid));
#endif
}

}  // namespace ge::core
