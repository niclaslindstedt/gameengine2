# Player movement, specified

What the player's movement does, as numbers and rules in our words (README.md, the rules). The
feel is matched by measurement (`se-parity`), so every number here is a starting value that
the lab confirms or corrects.

## The server's movement settings (spec: their documented defaults; verify each)

| Setting | Default | Meaning |
| --- | --- | --- |
| gravity | 800 units/s² | down |
| max speed | 320 units/s | the wish speed's cap on the ground |
| accelerate | 10 | ground acceleration factor |
| air accelerate | 0.7 (multiplayer) | air acceleration factor; with the 30 units/s cap below it gives air strafing |
| friction | 4 | ground friction factor |
| stop speed | 100 units/s | friction acts as if the speed were at least this |
| water accelerate | 10 | |
| water friction | 1 | |
| jump speed | 270 units/s | upward speed set on a jump |
| step height | 18 units | |

## The model (spec: the well-known form of this family of movement; verify by measurement)

- **Wish direction** from the view's yaw and the forward and side axes; the up axis only in
  water and flight.
- **Friction** on the ground: the speed drops by `max(speed, stop) × friction × dt`, clamped at
  zero; doubled at an edge with a drop below (verify).
- **Acceleration**: the speed already along the wish direction is subtracted from the wish speed;
  what is left, if positive, is added up to `accel × wish speed × dt`. In the air the wish
  speed used for the subtraction is capped at 30 units/s, which is what lets a player gain speed
  by turning while strafing.
- **Jumping** is held off until the button is released and pressed again (verify against
  holding the button: today's servers differ).
- **Stepping**: a blocked move is retried raised by the step height and set down again.
- **Clipping** against the player hull's planes, up to four bump iterations a move (verify).
- **The step's length** is the usercmd's milliseconds, capped (verify the cap) and split when
  long.

## In water (spec: the well-known form; verify every number by measurement)

The engine's swimmer (`engine/water/swim`, M24d, `docs/water.md` § Swimming) has a kinematic
regime for this; `plugins/shooter` fills its preset with these numbers.

- **The water level**, 0 to 3, sampled at three heights of the player's hull: 1 when the feet
  are in, 2 at the waist (about the hull's middle), 3 when the eyes (the view's height) are
  under. The contents at the eyes (water, slime, lava) are kept as the water type.
- **Swimming** at level 2 or more: the wish direction is the full view direction (pitch
  included) plus the up axis from jump; the wish speed is scaled by 0.7 and capped at the max
  speed; with no input the wish is straight down at 60 units/s (the slow idle sink).
- **Friction in water**: `speed × water friction × water level × dt` taken off (verify the form
  against `stop speed`), so deeper water drags harder.
- **Acceleration in water**: the ground's form with the water accelerate factor.
- **Gravity** is not applied while swimming (the idle sink replaces it).
- **The water jump**: at level 2, facing a wall whose top is within reach of the view's
  height, holding forward and jump sets an upward speed (about 225 units/s, verify) and a
  forward push, lifting the player out over the lip; held for a short time (verify).
- **Leaving the water** at level 1 or less returns to the ground or air model.

`se-parity` adds: idle sink from the surface, swimming up and down at each pitch, a water jump
onto a ledge, and entering water from a fall.

`se-parity` records an existing server's trajectories for scripted inputs (standing start,
strafe jump, circle jump, bunny hop on flat ground, a ramp, stairs, water entry, a knock-back)
and holds ours to them within a tolerance.
