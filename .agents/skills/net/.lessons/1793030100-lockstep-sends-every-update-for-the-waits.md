---
title: Lockstep's messages every update buy the waits on a lossy link, not on a clean one
date: 2026-10-24
scope: engine/net/lockstep.cpp, engine/lab/labs/net_lockstep_lab.cpp
concepts: [lockstep, send-every, bandwidth, stall, waits, redundancy]
---

A turn's input only changes once a turn (6 steps), so sending every update looked wasteful.
`--send-every` measured it: on `lan` and `wifi` sending every third update costs nothing in waits,
and it cuts the inputs' bytes by a third. On `bad`, though, the waits rise from 2% to 7%, and
sending once a turn raises them to 27%. Each message is also the resend of every unacked input,
so the message rate is the redundancy that rides out a loss burst. Keep `send_every` 1 for
lossy links. Most of a peer's wire bytes are node headers and acks anyway (about 60 kbit/s a peer
on a LAN whatever the rate), so cut those before you cut the messages.
