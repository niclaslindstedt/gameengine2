// A THREAD'S NAME AND ID, as the engine and the OS both see them. A thread names itself once
// when it starts (`name_this_thread("pool 2")`): the name is kept for the engine (the profiler's
// timeline lane, prof/timeline.hpp) and handed to the OS as `ge-pool-2` (spaces to dashes, cut
// to the 15 characters Linux keeps), so a debugger, `top -H` and the OS sampler
// (prof/cpu_sampler.hpp) show the same thread under the same name. `this_thread_os_id` is the
// id the OS lists the thread under (Linux's tid, macOS's thread id, Windows' thread id), which
// is how the sampler's threads and the timeline's lanes are matched.
//
// THE MAIN THREAD keeps its OS name, the process's (renaming it would rename the process in
// `ps`, and every thread a library starts from it inherits that name): name it with
// `tell_os = false`, so a thread that carries the process's name is one a library started and
// never named. Naming feeds nothing a simulation computes.
//
// ge:summary A thread's name (kept for the profiler and handed to the OS) and its OS thread id
// ge:keywords thread name thread-name os-id tid name_this_thread this_thread_name
// ge:keywords this_thread_os_id pthread_setname_np set-thread-description
// ge:skill perf
// ge:test tests/core_parallel_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ge::core {

// Names the calling thread for the engine and (unless `tell_os` is false) the OS. The engine's
// copy is kept whole.
void name_this_thread(std::string_view name, bool tell_os = true);
// The name given (empty when the thread never named itself).
const std::string& this_thread_name();
// The calling thread's id as the OS lists it.
uint64_t this_thread_os_id();

}  // namespace ge::core
