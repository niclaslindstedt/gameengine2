---
title: Name the main thread for the engine only, or every library thread wears its name
date: 2026-10-23
scope: engine/core/thread_name.hpp
concepts: [thread-name, os-thread, sampler, load, lanes, llvmpipe, unmarked-threads, pool-listener]
---

The load monitor matches its lanes to the OS's threads by the OS's thread id, and it names the
unmarked threads (the ones the timeline never hears) by their OS names. Giving the main thread
the OS name `ge-main` made every thread a library started from it later inherit that name, so
the GPU driver's and llvmpipe's threads all showed up as `ge-main`. Name the main thread with
`tell_os = false`. Its lane is still `main`, and any thread that carries the process's name is
one a library started and never named. Two more traps from the same work:

- A thread's recording room may only grow, because a slice array is never freed while a thread
  could still be writing to it. So a recording's own room has to be a separate limit, or a
  second `timeline_start` with less room silently keeps the first one's.
- Keep the pool's listener installed after a recording stops. A worker that was asleep when the
  recording ended still sends its wake, and that end has to close the slice its begin opened,
  or the thread's nesting is off by one when the next recording starts.
