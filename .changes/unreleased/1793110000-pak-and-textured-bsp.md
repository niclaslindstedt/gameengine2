---
type: Added
title: Pack archives, and classic maps drawn with their own textures
---
`engine/vfs` reads the classic pack archive: a checked directory, each file a view into the archive, and a fixture writer. A version 29 or BSP2 map keeps its textures' pixels (`Texture::indexed`). The `bsp` lab reads maps and the palette out of a pack with `--pak` and draws each map textured from its starts; `--textures-out=<dir>` writes a map's textures as PNGs outside the tree for the procedural texture matcher.
