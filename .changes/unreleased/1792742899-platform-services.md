---
type: Added
title: Platform services, with a simulated and an offline store
---

`ge::services` gives a game one interface to a store's client: the player, achievements, stats committed by `store_stats`, leaderboards whose entries carry details and a blob (a ghost), cloud files under a quota, rich presence, friends, invites and joins, the overlay, ownership of the game and its DLC, and screenshots, every answer an event pumped by `poll`. A game declares its achievements, stats, boards and DLC once as a catalog (`check_catalog` names every fault). The simulated store plays many players on a virtual clock with latency, cut connections and staged failures, refusing what a real store refuses; the offline store keeps one player's progress in a directory for a build sold outside a store. The `services` lab plays a game's whole use of them. Steam's backend follows (docs/services.md).
