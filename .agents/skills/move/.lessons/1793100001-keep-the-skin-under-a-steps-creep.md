---
title: A world's skin larger than one step's creep from rest leaves a walker stuck at a low sill
date: 2026-10-24
scope: engine/move/step.cpp, engine/move/mesh_world.hpp, plugins/rooms/clip.cpp
concepts: [skin, step-up, riser, sill, creep, walker, stuck, trace]
---

The walker sprinted into a 10 cm sill across a cave's mouth and stopped dead for good, though
its step height is 40 cm. The step-up keeps the lifted try only when it sets down on ground, and
the lifted try moves the body no farther than this step's velocity carries it. Stopped against
the riser, the body is a skin short of it, and the next step's velocity from rest is
`accelerate * wish * dt`, which carries it `accelerate * wish * dt^2`: about 3 mm at 120 Hz. With
a skin of 7.8 mm the lifted try never got past the riser's edge, so it set down on the same
floor, lost to the plain move, and the body never climbed.

Keep a world's skin far below that creep: a millimetre for a body in metres (the room level's
world uses 1/1024 m). Watch the box world's default of 1/32 L when L is a metre. To find such a
stall, drive the body into it step by step and replay the step-up's three sweeps by hand.
