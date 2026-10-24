---
title: A lake reads as water only when it mirrors the scene; a sky-only reflection is a white sheet
date: 2026-10-23
scope: engine/render/water.wgsl
concepts: [water, reflection, ssr, fresnel, grazing, lab, in-game]
---

Seen from a skier's eye height, nearly all of a lake is at grazing angles, where Fresnel sends
back almost everything. With only the sky's model to reflect, the far half of the lake drew as a
pale sheet of the horizon's glow and did not read as water at all; the open-sea views hid it
because a sea's horizon really is sky. Marching the reflected ray through the depth copy fixed
it: the shore and the trees mirrored. Two things the first march got wrong: test a crossing
after halving down to it, not at the coarse step (a long step lands far behind the first
surface and the hit is thrown away), and let a march that leaves a side of the screen go on
along the edge's column, or the lake's ends turn back into sky. Judge water in a game's own
scene (the `lake` lab), not only over the engine lab's open sea. And a lab's dry picture uses
the slot's `hidden`: `shown` is rewritten by `prepare_water` every frame.
