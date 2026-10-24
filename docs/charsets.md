# Charsets: the engine's bitmap fonts

The engine ships ten monospaced ASCII fonts that any game can use for its messages, menus,
console and HUD. They live in `engine/charset` (`ge::charset`, `ge_charset`, published as
`ge::charset`). Each font is a square glyph atlas that the engine's own C++ builder makes. The
atlases are committed at 256, 512 and 1024 px, together with eight tileable noise layers and a
stack of text effects that give a run of text the look of a world. This page covers the
research behind them, the format, the ten looks, the effects, how a renderer draws them and
how they are held. The roadmap row is M27 (`docs/goals.md`).

## What a charset is, and why one is still worth having

A charset (a "character set picture") is one image that holds every character a game can
write. The image is cut into a fixed grid of equal cells, and character code `c` sits in cell
`c`. To write a string, the renderer draws one textured quad per character and moves the pen
by one cell each time. This is why 1990s first-person games could share a single 128 x 128
picture (16 x 16 cells of 8 x 8, 256 codes) across the console, the HUD, the menus and the chat.
The upper half of that picture repeated the lower half in a second colour, which gave the game
highlighted text without a second draw.

The idea still pays for itself:

- **Layout is multiplication.** A monospaced line has no kerning, no shaping and no bidi. Its
  width is the number of characters times the advance, so a console column lines up, a
  scoreboard aligns without tab stops, and the console, the HUD and a log file share one
  model of where text goes.
- **One texture, one draw.** A whole screen of text is a single batch of quads over a single
  texture. That makes it cheap enough for a debug overlay that redraws every frame.
- **The look is the picture.** A charset is drawn, not rendered from outlines at run time.
  That leaves room for rough edges, a stencil's breaks or a pixel font's hard squares, which
  are exactly what give a game its voice.

TrueType text still has a job. `engine/ui`'s own text (M22) is for proportional, international
and accessible copy. The charsets are for what a game *writes in its world*.

## The format

| | |
| --- | --- |
| Codes | 0..127 only. 32..126 are printable ASCII. 1..31 are the UI symbols ASCII lacks (`Sym`: block and underline cursors, four arrows, a bullet, a checkbox empty and ticked, a cross, a tick, a slider's ends, track and knob, box drawing, light and medium shades, the degree sign, a star, a heart, a diamond, a radio button off and on). 10 (the newline) stays empty. 127 is the **missing glyph** that `code_of` draws for any other byte. No Latin-1, and no UTF-8 beyond ASCII. |
| Grid | 16 columns x 8 rows. Code `c` is at column `c % 16`, row `c / 16` (`cell_rect`). |
| Cell | Twice as tall as it is wide: 16 x 32 px at 256, 32 x 64 at 512, 64 x 128 at 1024. A monospaced line is about twice its advance tall, so the cell is both the line and the advance. |
| Pixels | One channel of coverage, 0 (no ink) to 255 (full ink), committed as 8-bit grey PNG: `engine/charset/atlases/<font>_<size>.png`. Upload it as an R8 texture and premultiply in the shader (colour x coverage). |
| Metrics | `FontInfo::metrics`: the baseline, the cap height and the x-height as shares of the cell's height, measured from the top. The drawn fonts share the design space's baseline (0.72). |
| Margins | No glyph's ink touches its cell's outer ring of pixels at 256 px, so a bilinear or mipmapped sample never reads a neighbour. A glyph too wide for its cell (an M, a W, a hand's wobble, a slant's lean) is narrowed, or moved back in, until it clears that ring. The test holds this. The box-drawing and shade symbols are the exception: they reach the edges on purpose so that they tile. |

Three square sizes are committed, and `atlas_size_for(cell_h)` picks the smallest whose cell is
at least as tall as the text, so an atlas is only ever shrunk. On the CPU (`text.hpp`) each
screen pixel is the area mean of the texels under it. On the GPU the atlas carries mipmaps. A
pixel font (`FontInfo::pixel`, the terminal) is sampled nearest at whole multiples of its 8 x 16
grid, so its pixels stay square.

**Why 16 x 8 rather than the classic 16 x 16.** We only need 128 codes, because the set is
ASCII. A 16 x 8 grid of 1:2 cells puts them in a square power-of-two texture whose cells already
have a text line's proportions. A 16 x 16 grid of square cells would either waste half the
atlas or squash every glyph.

**Why coverage and not a distance field.** At these sizes a coverage atlas is exact. MSDF is
worth its cost for display text drawn far above its atlas, but a monospaced UI rarely draws
above 128 px, and 1024's cells already are 128 px. The effects need a distance, and they get it
from the coverage. `distance_field` runs an exact Euclidean transform over a run (on the CPU
today). A GPU renderer can bake a distance channel beside each atlas when it uploads it (M27b).

## The ten fonts

Every glyph is drawn here. No outline, bitmap or metric is taken from an existing font, so
the fonts carry the repository's licence and nothing else. Each one is *inspired by* a genre of
real lettering, and the table names that genre.

| Font | For | Inspired by | How it is built |
| --- | --- | --- | --- |
| `terminal` | the console, debug readouts | the 8 x 16 character ROMs of text-mode PC screens | 8 x 16 pixel art (`pixel_art.cpp`), 2 px stems, scaled nearest |
| `matrix` | scoreboards, timing boards, departures | 5 x 7 LED and lamp dot-matrix boards | 5 x 7 art (two rows more for descenders), each pixel a round lamp |
| `segment` | clocks, speed and score readouts | 16-segment VFD and LCD displays | the lit bars of a 16-segment display with a point and a colon, slanted. Like the display, it has no lower case |
| `mono` | the default: chat, menus, the console | humanist programming monospaces | the skeletons under a round pen, dotted zero |
| `typewriter` | notes, letters, briefings | office typewriter slab serifs | a thin round pen, slab serifs on free stem ends, inky edges |
| `square` | a HUD, a cockpit, a sci-fi menu | sixties squared extended grotesques | a square pen, curves as superellipses, set wide, slashed zero |
| `stencil` | crates, signs, a military menu | spray-painted military stencils | a level square nib, every stroke broken where it meets another, bowls split top and bottom |
| `gothic` | titles, a dark fantasy menu | broad-nib blackletter and its angular hands | a broad nib at 38 degrees, curves in 45-degree facets, lozenge feet, condensed |
| `rounded` | big friendly HUD numbers, an arcade menu | heavy rounded arcade and signage sans | a heavy round pen, squarish curves, single-storey a |
| `marker` | hand-written notes, a scrawl | felt-tip comic and sign lettering | a round pen with a hand's wobble, each glyph lifted and tilted on its own, rough edges |

**One set of skeletons, ten pens.** The seven drawn fonts share one design per code
(`design_glyphs.cpp`). A design is a few lines, arcs, dots and fills in a 100 x 200 unit cell
(capitals 0..100, the x-height 74, ascenders 108, descenders -32), written in a tiny source
language (`design.hpp`). A font is a `Style` (`pen.hpp`): the pen's shape and weight, the slant,
how square its curves are and how finely they are drawn, serifs or lozenges, stencil breaks,
roughness, wobble, jitter and the proportions. Fixing a letter's shape fixes it in all seven
fonts. The raster is analytic: every pixel takes its signed distance to the nearest stroke
(a capsule, an oriented box, a broad nib's swept hull or a convex fill), and its coverage is that
distance over one pixel. There is no supersampling, and the same bytes come out on every
compiler. The 30 atlases build in under a second.

## Effects: making text atmospheric

The effects are two layers of work.

**The noise layers** (`layers.hpp`) are tileable grey textures, each a pure function of its
kind, size and seed. They are committed at 256 and 512 px in `engine/charset/layers/`.

| Layer | What it is | Used as |
| --- | --- | --- |
| `grain` | film grain | a fill or an overlay: a console, a title |
| `grunge` | blotches of dirt and wear | erosion: stencils, stamps |
| `scratches` | thin bright scratches, most running one way | a fill: a scuffed plate |
| `scanlines` | a CRT's lines | an overlay: a terminal |
| `rust` | pitted, turbulent corrosion | a fill: metal title plates |
| `cracks` | a cellular web of cracks | a fill or erosion: stone, paint, ice |
| `halftone` | a dot screen whose dots swell and shrink | a fill: print, a board's lamps |
| `brushed` | long streaks one way | a fill: steel, chrome |

**The effect stack** (`effects.hpp`) lights a run's coverage, in this order. Every size is a
share of the text's cell height, so a look stays the same at 12 px and at 120 px.

1. **Erosion.** Coverage is multiplied by `clamp((layer - strength) * 5 + 0.5)`, which wears the
   ink away where the layer is dark.
2. **The distance field** of what is left: negative inside, with the anti-aliased rim read from
   the coverage.
3. **The shadow.** The ink and its outline, offset, blurred (three box blurs, close to a
   Gaussian) and laid down at an alpha.
4. **The glow.** `strength * (1 - d / reach)^2` past the edge, added.
5. **The outline.** `clamp(width - d + 0.5)`, over the glow.
6. **The ink.** A vertical gradient from the cap height to the baseline, multiplied by a fill
   layer (`1 - s + s * texel * 2`) and shaded by a **bevel**. The bevel treats the ink as a
   height that rises over `bevel` from the edge, takes the height's normal and lights it from
   the top left, raised or `inset` (carved).
7. **The overlay.** A layer multiplied over everything drawn (ink, outline and glow).

The ten **looks** combine these for jobs a game has:

| Look | For | Made of |
| --- | --- | --- |
| `plain` | a tinted message | ink |
| `shadow` | a HUD over bright snow or sky | white ink, soft shadow |
| `outlined` | subtitles, names over heads, a kill feed | gold gradient, dark outline |
| `rusted` | a corroded title plate | rust fill, bevel, outline, hard shadow |
| `phosphor` | a green CRT console | green ink, glow, grain, scanlines |
| `neon` | a sign at night | pale core, wide magenta glow |
| `worn` | stamped paint, half gone | grunge erosion, grain fill |
| `chrome` | a logo, a high score | brushed fill, steel gradient, bevel, outline |
| `carved` | an inscription in stone | cracks fill, inset bevel |
| `amber` | a lamp board | amber ink, glow, grain |

### On the GPU (M27b)

A renderer does the same work per fragment: `cov = atlas.r`, `d` from a distance channel baked
beside the atlas at upload, the layers as repeat-wrapped R8 textures sampled at `pixel * scale`,
and the stack above in a single fragment shader. The shadow is a second instance of the run
drawn first, offset, with its own blur radius. The CPU compositor stays as the reference that a
GPU capture is diffed against (`make lab LAB=charset-looks`).

## Using them

```cpp
#include "charset/charset.hpp"
#include "charset/effects.hpp"

namespace cs = ge::charset;
const cs::Atlas atlas = cs::build_atlas(cs::FontId::terminal, cs::atlas_size_for(32));
// GPU: upload atlas.coverage as R8; one quad per byte of the string at
// cell_rect(atlas.size, cs::code_of(byte)), advancing cell_h / 2.
// CPU: draw and light a run straight onto an RGBA picture.
cs::LayerCache layers;
cs::TextPlace at{16, 10, 32};
cs::paint_text(rgba, width, height, atlas, "] say_prox anyone at the lift?", at,
               cs::look(cs::LookId::phosphor), layers);
```

A game that would rather read files than build atlases embeds the committed PNG it needs with
`ge_embed_binary` and decodes it at start-up. The builder and the file are the same bytes, and
a test holds them to each other.

**Who draws with them:** the console (M21d) draws its view in `terminal` or `mono`, the menus
(M22) can use any of the ten for a game's own look, and Fall Line's HUD readouts can use
`segment`, `rounded` and `matrix`.

## How they are held

- `tests/charset_charset_test.cpp`: the grid; every design parses; every glyph but the space
  has ink; no glyph touches its cell's edge; the build is the same on any number of workers;
  **every committed atlas is what the builder makes, pixel for pixel**.
- `tests/charset_effects_test.cpp`: the layout; a pixel font at 2x is its pixels; every layer
  spans 80 levels or more and wraps; every committed layer is what `make_layer` makes; a disc's
  distance field; every look draws, and its glow, shadow or outline reaches past the ink.
- A deliberate change to a design, a style or a layer rewrites the committed files:
  `GE_GOLDEN_WRITE=1 build/<preset>/tests/charset_charset_test` (and `charset_effects_test`).
  Show the `charsets` and `charset-looks` labs' pictures before and after.
- The labs: `make lab LAB=charsets` (every atlas, a specimen of each font in use, every font on
  one sheet, each atlas's ink and its packed size) and `make lab LAB=charset-looks` (the layers,
  every look on its font, every font under every look, a mock game screen).
- Atlases and layers are packed by the tree's own deflater (`engine/lab/deflate.hpp`). It gives
  the same bytes on every platform, and all 30 atlases come to about 0.9 MB.
