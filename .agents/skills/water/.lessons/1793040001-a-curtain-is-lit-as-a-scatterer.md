---
title: A waterfall's curtain reads white only when it is lit as a scatterer, not as a face
date: 2026-10-24
scope: engine/render/water_stream.wgsl
concepts: [water, waterfall, curtain, foam, lighting, lab]
---

The curtain is nearly vertical, so lit by its own normal it faces away from a sun behind it and
drew as a grey-blue slab even at a foam share of one. Aerated water scatters every way: lighting
it as a face tipped well up toward the sky (the geometric normal plus 1.5 up) turned it white
without touching the shared `shade_water`. Judge a waterfall from its pool with the sun behind
the cliff, the hard case.
