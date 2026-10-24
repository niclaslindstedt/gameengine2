---
title: A click's onset is its first non-silent sample, not its first loud one
date: 2026-10-24
scope: tests/capture_av_export_test.cpp, engine/lab/labs/capture_lab.cpp, engine/capture/sync_fixture.hpp
concepts: [av-sync, onset, click, threshold, ramp, lag]
---

Measuring when a click starts by the first sample over 0.05 put it 52 samples (about 1 ms) late:
the synth ramps a voice in, so a loud threshold reads the ramp as lag. The first sample with
|x| >= 1e-6 lands within 4 samples of the step's sample, which is the export's real accuracy.
Measure a sound's onset by the first sample off silence, and keep the fixture silent between
clicks so nothing else crosses it.
