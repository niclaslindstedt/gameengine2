---
type: Added
title: Brushes, groups and entities in the room level format
---
A room level (version 2 of `.gelv`, and its text form) holds convex brushes of solid, water, slime, lava, sky or clip, groups of brushes an entity moves, entities with their keys, a texture's size and a procedural surface drawn in a material's place. `rooms::build_brushes` builds the brushes into the faces that can be seen (CSG), and the clip world meets them. A level without brushes still writes version 1. The vault sample (`plugins/rooms/levels/vault.gelv.txt`) shows each.
