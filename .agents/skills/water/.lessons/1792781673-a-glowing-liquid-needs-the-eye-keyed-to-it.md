---
title: A glowing liquid needs the eye keyed to its own light, and a liquid's look needs a close-up lab
date: 2026-10-23
scope: engine/render/liquid.wgsl
concepts: [lava, slime, exposure, adapt-cd, tonemap, viscosity, ripples, bubbles, lab]
---

Lava is thousands of cd/m^2. Under a night sky keyed to 0.001 it clipped to white, and the
night's pre-exposure overflowed fp16 into NaN-olive. The fix was the frame's own key,
`FrameView::adapt_cd`, read by both the white point and `night.cpp`'s pre-exposure. A view of
lava keys at 1.5 times the melt, because the neutral tone curve desaturates a saturated red into
salmon. Look at the lab's night shot before believing any glow.

Three more things the lab caught:

- **A slime's surface detail drowns at grazing angles.** Bubbles, scum and film only read close
  up. Keep a close-up shot in every liquid lab, and make a bubble's dome visibly lighter, its
  rim darker and its highlight sharp. A dome that only bends the normal is invisible on an
  opaque slime.
- **Thin-film interference at full strength is psychedelic.** Mix it in patches at about half
  strength.
- **The wind-growth model leaves a 0.5 Pa s slime glassy in a gale.** That is honest for that
  viscosity. Runny slimes sit near 0.05 Pa s, so tune the row, not the model.
