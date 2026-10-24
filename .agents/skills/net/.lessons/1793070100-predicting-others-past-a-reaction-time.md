---
title: Predicting another player helps only while the replay is shorter than a reaction time
date: 2026-10-24
scope: engine/net/replicate_client.cpp, engine/lab/labs/net_predict_lab.cpp
concepts: [prediction, predicted-world, horizon, contact-events, joust, noise]
---

In M17p's joust, a partner predicted by its last input cut corrections on `lan`, `broadband` and
`wifi`. It made them worse on `mobile` and slower links. There the replay ran past about 0.1 s,
and the partner had changed its input inside it. Three findings, in order:

- **A horizon, not tuning.** Past `predict_max_s` the others fade back to their curve. Measured
  on `wifi` and `mobile`, 0.1 s won and 0.15 s broke `mobile`. Leave the horizon only at three
  quarters of it. A time-based hold (stay faded for a second) broke `lan`, whose replay crosses
  the line for a tick now and then.
- **An event the client could not predict is not a miss.** A server contact with an entity the
  client was not predicting counts as `heard`. Counted as `unpredicted`, it made the slow links
  look like 0% confirmed.
- **Check the seeds before chasing a row.** Past the horizon, the predicted rows still differed
  from the interpolated ones. The snapshot trailer's bytes move the simulated network's draws,
  so the run is a different random realization. Seeds 2 and 3 swapped which row was lower.
