---
title: A glyph's margin is measured after the style, so wide and slanted letters are fitted, never hand-nudged
date: 2026-10-23
scope: engine/charset/pen.cpp, tests/charset_charset_test.cpp
concepts: [charset, glyph, margin, atlas, slant, wobble, fit]
---

A skeleton that sits inside its cell does not keep its ink there under every font. A wider
`Style::width`, a slant, a hand's wobble and jitter, serifs, a square pen's corners and rough
edges each push M, W, `_` and the box's right-hand glyphs over the edge at 256 px. The first
build failed 132 margin checks across six fonts. Do not edit skeletons font by font. `fit`, at
the end of `flatten`, measures the transformed strokes plus the pen's reach and narrows the
glyph, or moves it back in, so that it clears a pixel at 256. A new pen or effect that reaches
further than `weight` must add to `reach` there. Two more traps: a quoted heredoc is the only
safe way to write `'\n'` into a C++ file from a script, and a black shadow is invisible to a
test that composes over black, so test the looks over grey.
