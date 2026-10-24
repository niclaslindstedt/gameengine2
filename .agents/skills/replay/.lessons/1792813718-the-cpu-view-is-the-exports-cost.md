---
title: An export's time is the game's CPU view, not the muxer or the sound
date: 2026-10-24
scope: engine/play/record.cpp, games/fall_line/sim/play.cpp, plugins/mountain/picture/map_picture.cpp
concepts: [export, cost, cpu-view, near, draw-map, smoke]
---

`record --format=mkv` of Fall Line's `near` view costs about 0.8 s a frame in a debug build at any
output size: the view draws the map at its own 512-pixel size every frame and is then scaled.
The muxer and the I420 conversion cost about 26 ms a frame at 720p in the same build, the sound
less. Keep a lab's smoke export to a dozen frames, and time an export's own parts on the sync
fixture (`capture`), whose view costs nothing.
