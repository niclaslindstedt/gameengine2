---
title: A scripted session carries its own steps, and its pad reads through exact bindings
date: 2026-10-24
scope: games/fall_line/controls/script.hpp
concepts: [event-log, scripted-session, run-clock, steps-due, frame-time, dead-zone, by-hand, replay]
---

A device session played into a window's loop must step the same run on every machine. The run
clock cannot give that: it adds each frame's wall time as a double, and frame times kept in whole
nanoseconds (8333333 ns for 1/120 s) fall just short of a step, so a frame now and then gets no
step and the inputs land one step late. A 3000-frame session lost its first step that way. A
scripted frame is therefore granted the steps due by its time, counted in whole nanoseconds
(`steps_due`), and a script puts frame i at the first nanosecond its step is due (`frame_time`).
The second trap is the shape: the default pad bindings carry dead zones, so an input log held on a
pad reads back as other values. `kExactPadBindings` (deadzone=0,1, no curve) reads every axis
back to the bit, which is what lets the bot's run held on a pad finish on the bot's digest.
