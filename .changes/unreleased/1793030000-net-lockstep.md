---
type: Added
title: Lockstep and rollback over the network
---

`engine/net/lockstep.hpp` runs a session where only inputs cross the wire. A strategy game steps in turns with an input delay and waits for every seat's input. A fighting game steps at once, predicts the other seats' inputs and rolls back when a real input differs. Every peer checks every other's step digests, so a desync is named by the step it happened on. The `net-lockstep` lab measures both under each network preset and plants a desync to show it caught.
