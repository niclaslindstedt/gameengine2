---
type: Added
title: Floating bodies on any water
---

`water::float_push` (`engine/water/float.hpp`) floats any rigid body on a few probes fixed in its frame: buoyancy by each probe's submerged share, drag against the water's own motion (waves and currents), an entry slam, the added mass, planing plates that let a fast sled skim a pond and slow into it, and splash events for the picture and the sound. `step_floater` floats a crate, a log or a buoy in one call. The `float` lab draws ten boxes at their draughts, measures heave periods against theory, charts an undamped box's energy, drops balls, runs a sled onto a pond and rides buoys on a sea.
