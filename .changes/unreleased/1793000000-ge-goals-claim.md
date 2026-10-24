---
type: Added
title: ge goals claim reserves a roadmap row before anyone else takes it
---

`ge goals claim "<what>"` takes the next free roadmap row (`--under=M17` for the next letter under one), writes it to `docs/goals/reserved.md`, commits that file alone and pushes it to the current branch at once. Five seconds later it reads every branch again. If another branch claimed the same row with an earlier commit, that branch keeps it: this one commits its line away and claims the next row, up to three times. Two threads that start together no longer take the same number. A `reserved.md` with no table is written fresh.
