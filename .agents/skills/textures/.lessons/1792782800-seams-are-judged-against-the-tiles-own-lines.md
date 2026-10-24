---
title: A seam score compares the wrapped edge with the tile's own lines, and layers that must agree share a salt
date: 2026-10-23
scope: engine/lab/labs/texgen_specimens_lab.cpp, engine/texgen/bake.cpp, engine/texgen/library
concepts: [seam, tiling, salt, layer-seed, specimens, clang-tidy]
---

A seam metric that compared the step across the wrapped edge with the AVERAGE step flagged a
tech panel whose seams lie on the tile's edge on purpose. A pattern's own joints are steps too,
so the edge is judged against the largest step across any other row or column, and a score of 1
or less means "no worse than a line the texture already draws". Second trap: every layer draws
its own seed from its place, so a glow layer and the cracks it should fill lit two different
crack patterns, and flagstone colours ignored the joints. Give both layers the same `salt=` and
they share one pattern. Third: `make precheck`'s lint stops at the first file with findings, so
one run shows only some of them; `tools/lint.sh build/debug origin/main` lists every changed
file's findings in one pass (casts of `x + 0.5`, `int * int` widened to an index, string `+` in a
loop are the usual ones).
