---
name: ui
description: "Use for engine/ui and any game's menus, HUD and cards in gameengine2: the immediate-mode 2D layer (paints, the draw list, images, fonts over stb_truetype, the CPU raster), backdrops (colour, gradient, drifting picture, the live scene, scrims), menus as a model and a view (pages, items, focus, events, the responsive layout), the `menu` lab, the strings table, the HUD readouts and presses, the shell's cards and settings, and the Dear ImGui developer UI."
---

# UI

A game's 2D: the menus (front door, options, pause), the HUD, the cards and the drop-down
console's panel, all drawn by one immediate-mode 2D layer. `docs/architecture.md` § `engine/ui`
is the design; this skill is how to work in it.

## The layer (`engine/ui/`, `ge::ui`)

- **Paints** (`paint.hpp`): straight sRGB colour, linear and radial gradients of up to eight
  stops; `linear_at_angle` takes CSS's angles. The layer blends in sRGB, as UI does.
- **The draw list** (`draw.hpp`): rounded boxes (a paint, an optional image times the paint, a
  border, a feather: 1 px antialiases, 14 px is a soft shadow) and text runs, each under the clip
  in force. Rebuilt every frame; it holds no state.
- **Images** (`image.hpp`): RGBA8 with `lab::Picture`'s layout (move the bytes across), the
  bilinear `sample`, `blurred`, and `fit_uv` / `contain_rect` (cover crops round a focus point).
- **Fonts** (`font.hpp`): stb_truetype, compiled only in `font.cpp` with the engine's libm in
  place of the platform's, so glyph coverage is the same bits on every row. Glyph masks are cached
  by glyph, size and quarter-pixel phase. `ui_font(Weight)` is the built-in Barlow Condensed (two
  weights, OFL, `engine/ui/fonts/OFL.txt`), pinned and hashed in `cmake/Ui.cmake` and compiled in.
- **The CPU raster** (`raster.hpp`): the draw list into an image. The labs' pictures, the tests'
  pixels, and the reference the GPU pass is held to.
- **The GPU pass** (`engine/render/ui_pass.hpp`, the `render` skill): the same draw list in
  one instanced pass after post, into a UNORM output. It shares the raster's rules (coverage at
  the pixel's centre, the clip rule, `place_glyphs`), so a change to how the raster decides a
  pixel changes `ui_pass.wgsl` in the same PR. `tests/render_ui_pass_test.cpp` and the
  `menu-gpu` lab (`make lab LAB=menu-gpu`) hold the two to 3 levels of each other.
- **Backdrops** (`background.hpp`): layers in UNIT coordinates of the area they fill (so one
  backdrop reads the same on every screen shape): colour, gradient, glow, scrim, vignette, dim, a
  drifting image (`Drift`: a slow zoom and pan, out and back, eased) and the live scene (the frame
  handed to `draw_background`). The console's panel is a `Background` drawn into its box.
- **Menus** (`menu.hpp`, `menu_view.hpp`): the model is pages of items (action, toggle, choice,
  slider, label, back), the page stack, the focus and the events out; it never calls the game.
  The view lays a page out by the screen's SHAPE (landscape: a column to one side; portrait: the
  width, anchored low), scales to a 720 px reference and shrinks a page that would not fit, glides
  the highlight and fades a page in, stepped by the caller's dt so a recorded run draws the same.
  `layout_problems` names what a screenshot would show wrong.

## Rules

- **Every word the player reads** comes from the game's strings table; a menu item's `text` and
  `hint` are filled from it. No literal text in a draw call in game code.
- **Input is the game's**: the menu takes edge-triggered presses (`MenuInput`) that M19's `menu`
  context produced; key repeat is the input layer's.
- **Check every layout at the reference viewports**: 1280x720, 390x844 and 844x390 (`make lab
  LAB=menu`). A layout problem fails the lab; fix the layout or shorten the words, never the check.
- A menu's look is a `MenuStyle` (fonts, sizes at the 720 px reference, colours); a game sets its
  own and changes no engine code for it.
- Fonts that ship are licensed for it (OFL or similar), pinned by commit and hash, and their
  licence installs with the package.

## Fall Line's UI (`games/fall_line/app/`)

- The shell's surfaces (`splash | menu | loading | pause | run | replay`) and the rule that only
  the pause card stops the simulation; the front door sits over the live mountain (M22c).
- Settings are cvars (M21); records and ghosts are saved through the platform's save directory.

## Dev UI

Dear ImGui, compiled only with `GE_DEV_UI=ON`, for the developer page (the instruments, the free
camera, the REPRO line). It is never on a release path.

## Labs

- `menu` (M22a): the front door over every backdrop kind, both pages at the three viewports, the
  highlight's glide, a page's fade and a picture's drift, every picture captioned. Its smoke
  digest is in `tests/labs_digests.txt`: a change to the raster, a font or the layout moves it, so
  look at the pictures, then rewrite the row (`--write-digests`).
- M10 lands `screenshots`: the HUD and every card at the three viewports.
