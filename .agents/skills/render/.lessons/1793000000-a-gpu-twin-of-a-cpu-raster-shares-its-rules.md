---
title: A GPU twin of a CPU raster matches it when it shares the raster's rules, not its look
date: 2026-10-23
scope: engine/render/ui_pass.hpp
concepts: [2d-pass, ui, raster, parity, premultiplied, blend, glyph-atlas, srgb, unorm]
---

The 2D pass came out within 3 levels of `ui::render` at every pixel on its first run because it
copied the raster's RULES rather than imitating its pictures. It works coverage from the same
signed distance at the same pixel centre (fragment `position.xy` is the centre, as the raster's
`+ 0.5` is). It keeps a pixel by the same centre-in-clip test. It sets text with the same
`place_glyphs` from the same cached masks, keying the atlas by the mask's address (an
`unordered_map` node does not move). It samples images through a clamp-to-edge linear sampler,
whose texel centres are where `ui::sample` puts them. A fill and its border, which the raster
blends one after the other, fold into one premultiplied colour with a (one, one minus source
alpha) blend. The raster blends sRGB-encoded bytes, so the pass needs a UNORM target: on an
`*Srgb` view the same blend runs in linear light and every soft edge drifts. When adding a
GPU path for something a lab pictures on the CPU, factor the shared rule (here the pen's walk)
into one function both call, then hold them together with a per-pixel diff test.
