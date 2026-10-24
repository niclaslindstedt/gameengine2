---
title: Never create mesh vertices (or draw, or append) inside a call's argument list
date: 2026-10-23
scope: plugins/rooms/build.cpp
concepts: [evaluation-order, unspecified-behaviour, lazy-vertex, quad, golden, compiler, merge-queue]
---

`e.quad(vertex(a, b), vertex(a + 1, b), vertex(a + 1, b + 1), vertex(a, b + 1), n)` looked
harmless, but `vertex` was a lazy cache that appends a vertex the first time it sees a corner.
C++ leaves the order of a call's arguments unspecified. GCC on x86-64 happened to go right to
left, Clang (and GCC on arm64 at -O2) went left to right, so the same seed built the same
triangles over vertices numbered in a different order. The golden matched only the compiler it
was cut on. The pull request's single GCC debug row passed, and the merge queue's Clang, arm64
and macOS rows ejected it.

Name each side effect in its own statement (`const uint32_t q0 = vertex(a, b), q1 = …;` is
fine, because a declaration's initialisers run in order), then pass the results. The same goes
for any argument that draws from an `Rng`, appends to a vector or bumps a counter.

When a golden fails on only some compilers and the counts match while the bits don't, dump the
values from both builds and diff them. A reordering, rather than a rounding difference, points
at evaluation order or uninitialised memory, not at FP flags.
