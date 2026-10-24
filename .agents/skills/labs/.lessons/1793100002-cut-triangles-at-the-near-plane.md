---
title: A view from inside a room lost its floor because the raster dropped triangles behind the eye
date: 2026-10-24
scope: engine/lab/model_view.cpp
concepts: [model-view, near-plane, clipping, raster, floor, ceiling, inside, first-person]
---

Pictures taken from inside a room showed the flat floors and ceilings as background, while
relief floors drew. A flat cap is two big triangles, so with the eye standing on it one vertex
is always behind the eye, and the model view dropped any triangle with a vertex behind the eye.
A relief floor's small triangles mostly lay in front of the eye and drew.

The model view now cuts such a triangle at a near plane 5 cm in front of the eye and fills what
is left (one or two triangles, their light interpolated). When a first-person picture is missing
big surfaces, suspect clipping before suspecting the mesh.
