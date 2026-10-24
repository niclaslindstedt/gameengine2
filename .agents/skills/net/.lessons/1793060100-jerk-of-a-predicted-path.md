---
title: The jerk a network adds to a predicted path is the drawn path's, less the game's own input steps
date: 2026-10-24
scope: engine/net/replicate_trial.cpp, engine/lab/labs/net_predict_lab.cpp
concepts: [jerk, smoothing, spring, prediction, correction, measure]
---

Two wrong ways to measure the jerk smoothing removes, both tried on M17o:

- **The whole drawn path.** A held input that changes every half second makes a velocity step the
  game itself draws. Its third difference sets a floor the same on every link (163,665 m/s³ at
  72 Hz), so smoothing looks like it does nothing.
- **The error offset alone (drawn less predicted).** A correction moves the prediction by -d and
  the error by +d. The drawn path is continuous, but the offset jumps by d, so the metric counts a
  jump nobody sees and rates the spring worse than a decay.

What works is the drawn path's jerk, leaving out the four ticks after each change of the held
input, since a third difference spans four positions. With that, the spring cuts the 99th
percentile four to ten times on lossy links.
