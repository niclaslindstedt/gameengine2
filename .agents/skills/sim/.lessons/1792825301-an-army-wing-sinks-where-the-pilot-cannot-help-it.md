---
title: An army paramotor that turns hard under its floor flies itself into the snow
date: 2026-10-24
scope: games/fall_line/sim/army_para.cpp, games/fall_line/sim/army_para_rules.hpp, plugins/paramotor/pilot.cpp
concepts: [army, paramotor, height-hold, floor, sinking-air, toggles, hysteresis, staging, test]
---

The army's rigs are the free ride's wing flown by `paramotor::para_pilot`, so they sink in
sinking air even at full throttle, and a banked turn sheds more. Three things together kept every
rig up on seed 38: the aim never asks for less than `kHoldM` (100 m) over the snow, so the
throttle law is working above the floor rather than at it; under `kFloorM` the toggles are capped
(`kLowSteer`), since a rig holding a 150 m circle at full toggle loses a metre a second; and the
escape to the fall line has hysteresis (in under `kLowM`, out over `kFloorM`), since flipping the
aim every step between the circle and the fall line swings the toggles end to end and dives the
wing. Read a lost rig off the `army-para` lab's height chart before touching the pilot.
