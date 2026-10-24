---
title: A plugin's planner takes the game's own places as arguments, never looks them up
date: 2026-10-24
scope: plugins/mountain/village/streets.hpp
concepts: [plugin, village, streets, pads, clearance, dependency, game, plan]
---

The village's streets keep 30 m off the helicopter's pad and the snowmobile's spot, but those
are placed by Fall Line's simulation (`heli_pad`, `sled_spot`), which the mountain plugin may not
include. `plan_streets(level, pads)` takes them as a span of plan points instead, and plans
without them when none are passed. Any clearance a plugin keeps from something a game places is
spelt the same way: the game hands the places in, and the plugin's tests stand a pad where a
street would run to prove it is kept off.
