---
title: Camera cvars write over the ladder the camera was made from, never over themselves
date: 2026-10-24
scope: engine/camera/cvars.cpp, games/fall_line/camera/machines.cpp
concepts: [cvars, camera-settings, ladder, world-unit, zero-means-own]
---

A cvar's 0 means "the rig's own", so applying the cvars needs the row as the game cut it:
`apply_camera_cvars` takes the base ladder and writes each field from it every frame, never
from the camera's current row (which an earlier apply already overwrote, so setting a cvar
back to 0 would keep the last value). A camera held over several ladders (Fall Line's skis and
each machine) applies the cvars once a ladder, each with its own base. The degrees in the cvars
are rounded (2 deg, 0.6 deg), so the defaults equal the reference rows only to 1e-4 rad: compare
angles with a tolerance, and keep the reference goldens off the cvars' path.
