---
type: Added
title: The 2D layer drawn on the GPU (render/ui_pass)
---

`ge::render` draws a `ge::ui` draw list in one instanced pass over the frame (`create_ui_pass`, `prepare_ui`, `encode_ui`). A box's rounded rectangle, paint, image and border are worked out in the fragment shader, and text reads a glyph atlas filled on demand. Images are uploaded when first drawn, and a GPU texture such as the live frame can be bound to a key (`ui_texture`). The pictures match the CPU raster's within a few levels. `ui::place_glyphs` is the shared rule both use to set a run of text. The new `menu-gpu` lab draws the menu's pages both ways and maps where they differ.
