---
title: A fuzzed packet spoofed from a peer carries that peer's real key, so it can end the connection
date: 2026-10-24
scope: engine/net/stress_fuzz.cpp, engine/net/node.cpp, engine/lab/labs/net_fuzz_lab.cpp
concepts: [fuzz, spoof, session-key, disconnect, authentication, aead]
---

From strangers' addresses a million fuzzed datagrams change nothing for an honest session: the
node drops anything with no peer behind its address. Sent from a peer's own address, a mutated
copy of that peer's real packet still carries its real session key. A flipped type byte or a
bogus ack can then end the connection: in the `net-fuzz` lab's spoofed row, 2 of 4 clients
dropped. The key is a shared value, not a signature, so it proves nothing about a packet's bytes.
Do not try to harden the parser against this. Authenticate every payload (M17i's AEAD), and keep
the spoofed row in the lab as the measure that M17i moves to "honest".
