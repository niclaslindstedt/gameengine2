---
type: Added
title: Swimming, tuned per game
---

`water::step_swimmer` (`engine/water/swim.hpp`) swims a player in any water: the immersion level from the feet, the waist and the eyes; a kinematic regime for a first-person feel in the game's own units (the wish by the view's pitch, friction by depth, the slow sink, the water jump over a lip the game reports); and a body regime for a physical one (buoyancy on the body, the lungs and every garment, clothes soaking and letting their air go, drag, the stroke, the cold shock, swim failure and the breath). `water::default_swimmer()` is a person in a swimsuit, and `skier::dressed_skier()` a skier who floats on his jacket's air, soaks and sinks slowly. The `swim` and `skier-swim` labs chart them.
