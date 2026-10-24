---
type: Added
title: Procedural textures
---

`engine/texgen` (`ge::texgen`) bakes a texture from a short text recipe that an agent writes: tileable noise, bricks, planks, panels, cracks, dirt, dust, grime, wear, rust, streaks, moss, snow and glow layers. The result is albedo, normal, ORM, glow and height maps, cached on disk once per seeded variant. `ge texture kinds`, `check` and `bake` work on recipes, and five `texgen-*` labs show the results.
