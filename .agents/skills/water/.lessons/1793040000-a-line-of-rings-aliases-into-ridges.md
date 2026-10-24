---
title: A line of impulse rings is a hull's wake only with its short waves cut at four ring spacings
date: 2026-10-24
scope: engine/water/wake.cpp
concepts: [water, wake, kelvin, rings, aliasing, measurement, lab]
---

A hull drawn as rings set off every tenth of a second is a line of discrete sources, and waves
shorter than a few spacings of them do not add into a wake: they add into ridges along the track,
up to metres high, and the arm's angle read off them is nonsense. Keeping only waves at least
four ring spacings long (and at least half the hull's length, which a real hull cannot make
shorter than anyway) gives Kelvin's 19.4 degrees at a low Froude number. A smooth source
spectrum in place of the hard floor broke the measurement instead. Measure the arm 6 to 12
transverse wavelengths behind the hull, take the argmax of the envelope across each line and fit
a straight line through them; a ring's own wavelength is a full crossing-to-crossing span centred
on the radius, not twice a half span. A wake's slopes of a tenth (strength 0.3) read on a mirror;
steeper ones drew as a bright streak, and a wake is seen best looking along the track into a low
sun.
