---
name: services
description: "Use for engine/services in gameengine2: the platform services a store's client gives a game (who is playing, achievements, stats, leaderboards with blobs, cloud files, rich presence, friends, invites and joins, the overlay, ownership and DLC, screenshots) behind one Backend; the game's catalog; the simulated store and the offline store; the services lab; Steam and other stores' backends and the SDK kept out of the tree (docs/goals.md M20)."
---

# Platform services

`engine/services/` (`ge_services`, namespace `ge::services`) is how a game talks to a store:
one interface, `Backend`, with a backend per store. The design, every Steamworks call's mapping
and the plan are [`docs/services.md`](../../../docs/services.md); the rows are `docs/goals.md`
M20a–M20h.

| File | What |
| --- | --- |
| `services/backend.hpp` | THE INTERFACE: `Backend`, `Result`, `Event` and its kinds, the board, cloud, friend and environment types |
| `services/catalog.hpp` | The game's catalog (achievements, stats, boards, DLC) as constant tables; `check_catalog`, the lookups |
| `services/sim_store.hpp` | The simulated store (players, latency, staged failures, the client's actions, the journal) and `open_offline` |
| `services/sim_store_state.hpp` | The simulated store's state, shared by its two `.cpp` files; not installed |
| `services/sim_store_io.cpp` | Saving and loading a store to a directory; the offline store |

## Rules

- **One pump.** Answers and notices arrive only from `poll(now_us, events)`, on the main thread,
  once a frame. A backend never calls back from another thread and never delivers between polls.
  A call that goes to a store's servers returns a `RequestId`; match the event's `request`.
- **Commit at a pause.** `unlock`, `set_stat` and `add_stat` are local; `store_stats` commits.
  Call it at a run's end, not every frame (stores rate-limit it). A refused commit
  (`max_change`) throws the local values back to the stored ones.
- **The catalog is the game's, stated once.** A new achievement, stat or board is a row in the
  game's catalog tables, then the same id in the store's partner configuration. Run
  `check_catalog` in a test of the game so a bad id fails there.
- **No store word above a backend.** The interface says achievement, board, presence; a
  Steamworks name appears only in `engine/steam` and in `docs/services.md`'s mapping table.
- **The SDK never enters the tree.** A store's SDK is supplied by path at build time
  (`GE_STEAMWORKS_SDK`), on CI from a GitHub secret. Never commit its headers, its libraries or
  `steam_appid.txt`, and never copy its declarations into the tree to make a build work.
- **Everything works without a store.** A new call lands on the simulated store first, with the
  store's rules (its limits, its refusals) and a test, so a game can be built and tested on the
  GPU-less container with no SDK.
- **Determinism.** The simulated store draws only from its own seeded stream (the jitter) and
  reads only the time its polls pass in, so a script replays to the same journal: the `services`
  lab's digest holds it.

## Adding a call

1. Declare it on `Backend` in the right group of `backend.hpp`, with its contract (what it
   returns, which event answers it, the store's limits) in the comment.
2. Implement it on the simulated store (`sim_store.cpp`): enforce the limits a store enforces,
   queue its answer with `answer_time()`, write it to the journal. Persist it in
   `sim_store_io.cpp` if the store keeps it between sessions.
3. Test it in `tests/services_sim_store_test.cpp` (and the offline round trip when it is kept).
4. Add it to the `services` lab's script, then `make lab LAB=services` and rewrite its digest
   (`labs --all --smoke --write-digests`, the `labs` skill).
5. Add its row to `docs/services.md`'s interface table and its Steamworks mapping, and to each
   store backend that exists.

## Labs

`make lab LAB=services` (`--latency-ms`, `--jitter-ms`, `--seed`): four players on one script,
every event in order, the store's journal, and how long each kind of answer took. Read the
events table first; a `lab.fail` line names the event the script was owed and did not get.
