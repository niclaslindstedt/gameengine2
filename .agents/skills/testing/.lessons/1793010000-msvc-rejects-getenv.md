---
title: MSVC's /W4 /WX fails a test that calls std::getenv; read GE_GOLDEN_WRITE through _dupenv_s
date: 2026-10-24
scope: tests
concepts: [golden, getenv, msvc, windows, c4996, env, golden-write]
---

`std::getenv("GE_GOLDEN_WRITE")` builds clean with GCC and Clang, and so passes the PR's checks.
MSVC flags it as C4996 (deprecated as unsafe), `/WX` makes that an error, and the failure only
shows up in the merge queue's Windows rows. Every golden test reads the flag through the same
small `env_set` helper (`tests/core_moon_test.cpp`): `_dupenv_s` under `_MSC_VER`, `getenv`
everywhere else. Copy that helper. Never call `getenv` directly in a test.
