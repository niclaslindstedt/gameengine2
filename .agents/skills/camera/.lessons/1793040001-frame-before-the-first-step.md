---
title: Frame the camera before the first step, or an outing's boarding never hands over
date: 2026-10-24
scope: games/fall_line/camera/machines.cpp, games/fall_line/labs/gpu_camera_machines_lab.cpp, tests/fall_line_camera_test.cpp
concepts: [ride-camera, hand-over, outing, board, first-frame, free-map]
---

The outings stand him beside the machine and the pilot boards it on the very first step, so a
camera first framed after a step starts on the machine's ladder and no hand-over is ever seen.
Frame every ride camera once on the state as created, then step. The same trap measures a jump
that is not there: a lens's step needs its last pose from that first frame, not zero. And a
lens's step through a boarding is measured past his own move (his feet are put on the boards in
one step), not against his position. The machines wait in the free ride's ski area: take the
map from `free_map(seed, 10)`; the app's map version (`kMapVersion`) has no ski area and no
snowmobile or helicopter outing.
