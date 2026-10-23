---
title: A saved store writes every player before any record that names one
date: 2026-10-23
scope: engine/services/sim_store_io.cpp
concepts: [save, load, offline-store, record-order, friend]
---

The first round trip of a two-player store failed on load: `friend 1001 1002` was written in the
first player's block, before `player 1002`, and the loader refuses a record naming a player it
has not met. The loader is right to refuse (a dangling id is a damaged file), so the saver writes
in dependency order: every `player` and `env` line first, then each player's friends, licences,
stats and achievements, then the boards. A new record kind that names another record goes after
it; `tests/services_offline_test.cpp`'s whole-store test catches a break with two players.
