---
type: Added
title: Time sync for predicting clients
---

Replication now keeps each client's inputs arriving at the server just in time. Every snapshot tells the client how many of its inputs are waiting in the server's queue. The client speeds up or slows down its own tick by at most 3% to hold none waiting, so a clock that drifts no longer piles up lag or starves the server. The other players are drawn as far behind the server as the link's jitter needs instead of a fixed six ticks: 38 ms behind on a clean link instead of 100. The `net-predict` lab measures it under every network preset, with sync off and on and with skewed clocks.
