---
type: Added
title: Classic maps cloned into the room level format
---
`rooms::clone_map` clones a version 29 or BSP2 map into a room level: its tree's solid, liquid and sky cells as brushes with each face's texture projection, its brush models as groups (a trigger's as trigger volumes, a new brush contents), its entities with `origin` in metres and `angle` as `heading`. Textures are taken only from an agent-approved texture map (`parse_texture_map`) and never copied; an unnamed texture is refused unless placeholders are asked for. A face can be marked `smooth` and is shaded round with its smooth neighbours (`--smooth`). The `clone` lab draws each map beside its clone from the same eyes and stands a body at every start.
