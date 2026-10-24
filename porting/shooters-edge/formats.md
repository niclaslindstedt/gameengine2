# The file formats, specified

Byte layouts of every file Shooter's Edge reads, in our words (README.md, the rules). All
integers and floats are little endian; floats are IEEE 754 single precision. **Source** says
where each fact came from: `spec` (the community's public format descriptions), `measure`
(checked against a file of the free content by a lab, once M18a/M18b land), `verify` (believed,
to be checked by `measure` before code depends on it).

## pak (archive)

| Field | Bytes | Meaning | Source |
| --- | --- | --- | --- |
| magic | 4 | `PACK` | spec |
| dir_offset | i32 | where the directory starts | spec |
| dir_length | i32 | directory size in bytes; entries = length / 64 | spec |

A directory entry is 64 bytes: the name (56 bytes, NUL padded, forward slashes, compared
case-insensitively by the clients that are in use: verify), the offset (i32) and the size
(i32). Files are stored uncompressed.

## pk3 (archive)

A zip file (`spec`: the zip format's own public description). Read through the central
directory at the end, not the local headers. Methods to support: 0 (stored) and 8 (deflate).
Directory entries (names ending in `/`) are skipped. A `.pk3dir` is a plain directory treated as
if it were a pk3 of the same name (verify the ordering rule against today's clients).

## Search order

Within a game directory: `pak0.pak`, `pak1.pak`, … in numeric order with no gaps (a missing
number ends the sequence: verify), then pk3 files in name order (verify), with later archives
overriding earlier ones and loose files on top. The base directory is searched first, then the
mod's, so a mod overrides the base. To be measured against the free content and today's clients
before M18a's search order is frozen.

## BSP version 29 (the map)

Header: the version (i32, 29), then 15 lumps, each an offset and a length (two i32).

| # | Lump | Element | Size | Fields | Source |
| --- | --- | --- | --- | --- | --- |
| 0 | entities | text | — | the entity lump: `{ "key" "value" … }` blocks, NUL terminated | spec |
| 1 | planes | plane | 20 | normal (3 f32), dist (f32), type (i32: 0–2 axial x/y/z, 3–5 nearest axis) | spec |
| 2 | textures | — | — | i32 count, i32 offsets from the lump's start (−1 = missing), then miptex records | spec |
| 3 | vertexes | vertex | 12 | position (3 f32) | spec |
| 4 | visibility | bytes | — | run-length coded PVS rows (below) | spec |
| 5 | nodes | node | 24 | plane (i32), children (2 i16), mins (3 i16), maxs (3 i16), first face (u16), face count (u16) | spec |
| 6 | texinfo | texinfo | 40 | s and t vectors (2 × 4 f32: xyz and offset), miptex index (i32), flags (i32; 1 = no lightmap, e.g. sky and liquids) | spec |
| 7 | faces | face | 20 | plane (i16), side (i16), first surfedge (i32), edge count (i16), texinfo (i16), light styles (4 u8; 255 = none), lightmap offset (i32; −1 = none) | spec |
| 8 | lighting | bytes | — | one byte a luxel, a face's maps for each style one after another | spec |
| 9 | clipnodes | clipnode | 8 | plane (i32), children (2 i16; negative = a contents value) | spec |
| 10 | leafs | leaf | 28 | contents (i32), vis offset (i32; −1 = none), mins (3 i16), maxs (3 i16), first marksurface (u16), count (u16), ambient sound levels (4 u8: water, sky, slime, lava) | spec |
| 11 | marksurfaces | u16 | 2 | face indices | spec |
| 12 | edges | edge | 4 | two vertex indices (u16) | spec |
| 13 | surfedges | i32 | 4 | edge index; negative means the edge reversed | spec |
| 14 | models | model | 64 | mins, maxs, origin (3 f32 each), head nodes (4 i32: the drawing tree and three clip hulls), vis leafs (i32), first face (i32), face count (i32) | spec |

- **Node children**: non-negative is a node; negative `c` is leaf `−(c + 1)`.
- **Contents**: −1 empty, −2 solid, −3 water, −4 slime, −5 lava, −6 sky (spec). Larger negative
  values used by later tools for currents and clips: verify.
- **Hulls**: hull 0 is the drawing tree (point traces); hulls 1 and 2 are clip trees for the
  player-sized and the large box; their extents (spec, verify): hull 1 from (−16, −16, −24) to
  (16, 16, 32), hull 2 from (−32, −32, −24) to (32, 32, 64). Hull 0's clipping uses the node
  tree with leaf contents.
- **Visibility**: leaf `i`'s row starts at its vis offset and covers leaves 1…n (leaf 0, the
  outside, has no bit). A non-zero byte is eight bits literally; a zero byte is followed by a
  count of zero bytes it stands for (spec).
- **Lightmaps**: a face's texture-space extents are found from its vertices projected with
  the texinfo vectors; the lightmap is one luxel every 16 texels, sized
  `floor(max / 16) − floor(min / 16) + 1` each way, rounded outward (spec; verify the rounding
  on free maps).
- **miptex**: a 16-byte name, width and height (u32), four offsets to the mip levels from the
  record's start; 8-bit palette indices. Names: `*` liquids (warped, drawn without lightmaps),
  `sky` (two layers, the left half and the right half of the texture), `+0`…`+9` an
  animation, `+a`…`+j` the alternate animation a brush entity switches to (spec).
- **`.lit`**: beside a map, `QLIT`, version 1 (i32), then three bytes a luxel in the same order
  as the lighting lump (spec, verify).

## BSP2

The larger-limits variant: the magic `BSP2` in place of the version, the same fifteen lumps, with
these records widened (spec; written and read by `plugins/brush`, measured on a free BSP2 map:
verify):

| Lump | Size | Fields |
| --- | --- | --- |
| nodes | 44 | plane (i32), children (2 i32), mins (3 f32), maxs (3 f32), first face (u32), face count (u32) |
| clipnodes | 12 | plane (i32), children (2 i32) |
| leafs | 44 | contents (i32), vis offset (i32), mins (3 f32), maxs (3 f32), first marksurface (u32), count (u32), ambient (4 u8) |
| faces | 28 | plane (i32), side (i32), first surfedge (i32), edge count (i32), texinfo (i32), styles (4 u8), lightmap offset (i32) |
| marksurfaces | 4 | u32 |
| edges | 8 | two u32 |

An early form with the magic `2PSB` kept 16-bit bounds; it is refused (verify whether any free map
needs it before M18b).

## BSP version 38

Header: `IBSP`, version 38 (i32), 19 lumps (offset, length). Spec throughout; to be measured on a
free map of this family.

| # | Lump | Size | Fields |
| --- | --- | --- | --- |
| 0 | entities | — | text |
| 1 | planes | 20 | normal (3 f32), dist (f32), type (i32) |
| 2 | vertices | 12 | 3 f32 |
| 3 | visibility | — | cluster count (i32), then per cluster two offsets from the lump's start (PVS, PHS: i32), then run-length rows as in 29 |
| 4 | nodes | 28 | plane (i32), children (2 i32), mins (3 i16), maxs (3 i16), first face (u16), count (u16) |
| 5 | texinfo | 76 | s and t vectors (8 f32), flags (i32: 0x1 light, 0x4 sky, 0x8 warp, 0x10/0x20 translucent, 0x40 flowing, 0x80 no draw), value (i32), texture name (32 bytes), next texinfo of an animation (i32, −1 none) |
| 6 | faces | 20 | plane (u16), side (i16), first surfedge (i32), edge count (i16), texinfo (i16), styles (4 u8), lightmap offset (i32, into RGB bytes) |
| 7 | lighting | — | three bytes a luxel |
| 8 | leafs | 28 | contents (i32, a bit set: 1 solid), cluster (i16), area (i16), mins (3 i16), maxs (3 i16), first leaf face (u16), count (u16), first leaf brush (u16), count (u16) |
| 9 | leaffaces | 2 | u16 |
| 10 | leafbrushes | 2 | u16 |
| 11 | edges | 4 | two u16 |
| 12 | surfedges | 4 | i32 |
| 13 | models | 48 | mins, maxs, origin (3 f32 each), head node (i32), first face (i32), face count (i32) |
| 14 | brushes | 12 | first side (i32), side count (i32), contents (i32) |
| 15 | brushsides | 4 | plane (u16), texinfo (i16) |
| 16 | pop | — | unused bytes |
| 17 | areas | 8 | area portal count (i32), first area portal (i32) |
| 18 | areaportals | 8 | portal number (i32), other area (i32) |

## BSP version 46

Header: `IBSP`, version 46 (i32), 17 lumps (offset, length). Spec throughout; to be measured on a
free map of this family.

| # | Lump | Size | Fields |
| --- | --- | --- | --- |
| 0 | entities | — | text |
| 1 | shaders | 72 | name (64 bytes), surface flags (i32; 0x4 sky), contents (i32; 1 solid, 8 lava, 16 slime, 32 water) |
| 2 | planes | 16 | normal (3 f32), dist (f32) |
| 3 | nodes | 36 | plane (i32), children (2 i32), mins (3 i32), maxs (3 i32) |
| 4 | leafs | 48 | cluster (i32, −1 none), area (i32), mins (3 i32), maxs (3 i32), first leaf face (i32), count (i32), first leaf brush (i32), count (i32) |
| 5 | leaffaces | 4 | i32 |
| 6 | leafbrushes | 4 | i32 |
| 7 | models | 40 | mins, maxs (3 f32 each), first face (i32), count (i32), first brush (i32), count (i32) |
| 8 | brushes | 12 | first side (i32), count (i32), shader (i32) |
| 9 | brushsides | 8 | plane (i32), shader (i32) |
| 10 | vertexes | 44 | position (3 f32), texture coordinates (2 f32, whole textures), lightmap coordinates (2 f32, across a page), normal (3 f32), colour (4 u8) |
| 11 | meshverts | 4 | i32, an offset from the face's first vertex |
| 12 | effects | 72 | name (64 bytes), brush (i32), unused (i32) |
| 13 | faces | 104 | shader, effect, type (1 polygon, 2 patch, 3 mesh, 4 billboard), first vertex, vertex count, first meshvert, meshvert count, lightmap page (i32 each); lightmap start and size (2 i32 each); lightmap origin (3 f32), its s and t vectors (2 × 3 f32); normal (3 f32); a patch's control grid size (2 i32) |
| 14 | lightmaps | 49152 | a 128 × 128 RGB page |
| 15 | lightvols | 8 | ambient RGB, directional RGB, direction (2 u8) |
| 16 | visdata | — | cluster count (i32), row bytes (i32), then the rows, uncompressed |

- **Patches**: a grid of control points, both sizes odd, read as biquadratic Bézier pieces of
  3 × 3 points sharing their edges.
- **Winding**: polygons and meshes are wound clockwise seen from the front in the files we write;
  the reader checks against the normals rather than trusting it.

## MDL (alias model)

Header (spec): magic `IDPO`, version 6, scale (3 f32), translate (3 f32), bounding radius (f32),
eye position (3 f32), skin count, skin width, skin height, vertex count, triangle count, frame
count, sync type, flags, size (f32). Vertex positions are three bytes each, decoded as
`byte × scale + translate`, plus a fourth byte indexing a fixed table of 162 unit normals.

- **Skins**: a type (i32: 0 single, 1 group), then pixels (width × height bytes) or a group
  (count, a time per skin, the skins).
- **Texture coordinates**: per vertex, on-seam (i32), s, t (i32). A vertex on the seam takes
  `s + width / 2` on triangles facing backwards.
- **Triangles**: faces-front (i32), three vertex indices (i32).
- **Frames**: a type (i32: 0 single, otherwise a group). A single frame: its bounding box (two
  packed vertices), a 16-byte name, the vertices. A group: count, bounds, a time per frame, the
  frames.
- **Flags** (bits): the model's trail and rotation effects (rocket, grenade, gib, rotate, the
  tracers): the bit order to be written from the spec with M18i.
- **The normal table**: the format defines it, but its values come to us only as a table in
  existing code. M18i derives it (it is a fixed subdivided-sphere point set: the derivation is
  written here and checked against the normals measured from the free models' lighting) rather
  than copying it.

## SPR (sprite)

Header (spec): magic `IDSP`, version 1, type (i32: 0 parallel-upright, 1 facing-upright,
2 parallel, 3 oriented, 4 parallel-oriented), bounding radius (f32), maximum width and height
(i32), frame count, beam length (f32), sync type. Frames: a type (single or group); a single
frame is its origin (2 i32), width, height and 8-bit pixels (255 transparent: verify).

## WAD2 (2D graphics)

Header (spec): magic `WAD2`, lump count (i32), directory offset (i32). Entries are 32 bytes:
offset, size on disk, size (i32 each), type (u8), compression (u8; 0 only in practice), two
pad bytes, a 16-byte name. Types to read: the status-bar and font pictures (a picture is width,
height, pixels) and miptex (the same record as the map's). Type codes: verify.

## LMP (pictures and tables)

- A picture: width and height (i32), then pixels; index 255 is transparent (spec).
- `palette.lmp`: 256 RGB triples, 768 bytes.
- `colormap.lmp`: 64 light levels × 256 palette indices; the last few palette rows are
  fullbright and stay unshaded (which rows: verify).
- The console font is a 128 × 128 picture of 16 × 16 characters of 8 × 8 pixels (spec).

## WAV

RIFF WAVE, PCM, 8-bit unsigned or 16-bit signed, mono (spec). A `cue ` chunk's first cue
point marks the loop start; a `LIST` chunk's `ltxt` sample length, when present, sets the loop's
length (spec, verify).
