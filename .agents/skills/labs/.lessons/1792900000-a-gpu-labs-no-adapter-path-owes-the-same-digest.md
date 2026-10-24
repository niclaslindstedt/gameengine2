---
title: A GPU lab's no-adapter path owes the same digest
date: 2026-10-24
scope: engine/lab/labs
concepts: [gpu, digest, no-adapter, placeholder, shot-id, windows, full-ci]
---

The Windows row has no GPU adapter, so a GPU lab runs its `if (!device)` branch there, and the
smoke digest is checked against the same baseline the Linux rows wrote with pictures. `Lab::shot`
hashes a shot's id even when its picture is `Det::measured`. A no-adapter branch that only notes
and returns therefore digests to the empty FNV value (`cbf29ce484222325`) and fails the queue.
The branch must emit, in the same order, a placeholder shot under each id and every deterministic
metric and table the full run emits (gpu-capture's placeholder and `liquid_scene.hpp`'s
`placeholders` are the pattern). Hiding Vulkan locally does not test it, because Dawn falls back
to its Null backend; force the branch (`if (true)`), check `--check-digests=tests/labs_digests.txt`,
and restore. Only the queue and the `full-ci` label run the Windows row.
