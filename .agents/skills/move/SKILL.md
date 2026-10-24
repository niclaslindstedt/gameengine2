---
name: move
description: "Use for engine/move in gameengine2 and anything about how a character's body moves: walking, running, friction, acceleration, jumping (buffer, coyote time, jump cut, double jumps, held jumps), falling, strafing, strafe jumping and bunny hopping, air control, stairs and step height, slopes and surf ramps, crouching and crouch jumps, slides, free flight and noclip; the MoveSpec knobs and their names (the cvars a game binds), the presets (classic_arena, strafe_arena, modern_shooter, platformer, walker), the step, the trace callback a world answers, the box world, the strafer's best angle, the scenarios and the move-strafe and move-trace labs; the M28 rows."
---

# Character movement

`engine/move/` (`ge_move`, namespace `ge::move`) moves a body for any game: a shooter's player, a
platformer's hero, a third-person walker, a spectator. The design is
[`docs/movement.md`](../../../docs/movement.md); the rows are `docs/goals.md` M28–M28d. Nothing
here names a genre's word: a feel is a ROW of knobs, never a code path.

| File | What |
| --- | --- |
| `move/spec.hpp` | `MoveSpec`: every knob with its unit; the presets; `scale_lengths` |
| `move/knobs.hpp` | The knobs by name (unit, range, help): `apply_knobs`, `knob_text`, the fields a console binds |
| `move/presets.cpp` | The five preset rows and what each is for |
| `move/world.hpp` | `World::trace`: the one question a mover asks a world, and its contract |
| `move/box_world.hpp` | Boxes, ramps and stairs as brushes; the swept-box trace |
| `move/mesh_world.hpp` | A world of triangles: the swept separating-axis trace, bucketed in a grid |
| `move/step.hpp` | `step_move`: the step's order, `MoveCmd`, `MoveState`, `MoveEvents`, `accelerate`, `wish_of` |
| `move/strafe.hpp` | `best_wish_angle`, `strafe_cmd`: the strafe jumper's hands |
| `move/scenarios.hpp` | Staged runs, their worlds and scripts; `run_move_scenario` and its digest |

## Tuning a feel

1. Start from the nearest preset; never add a code path for one game's feel. A behaviour no
   knob can give is a new knob: a field in `MoveSpec` with its unit, its row in `knobs.cpp`
   (the name stated once), its place in the step, a scenario that shows it, a test that holds it.
2. Run the labs BEFORE: `make lab LAB=move-trace ARGS="--tag=before"` and
   `make lab LAB=move-strafe ARGS="--tag=before"`.
3. Try values without a rebuild: `--set=airaccelerate=1,airspeedcap=0` on either lab, and
   `--preset=<row>` on `move-trace`. Read the pictures' `look_for` lines.
4. Put the chosen numbers in the preset (or the game's row), run AFTER with `--compare=before`,
   and put both in the PR.

## Rules

- **Pure and allocation-free.** `step_move` reads no clock, draws nothing, allocates nothing, and
  calls only `core/libm` for transcendentals (`make libm-check` scans `engine/move`).
- **A world is a trace.** Never give the mover a world type; a world answers `World::trace` with
  the contract in `world.hpp` (the skin, the start-solid flags). A trace that does not pull back
  by a skin makes bodies stick in floors.
- **Units are the row's.** A metre is `spec.unit` L; scenarios lay their worlds in metres times
  the unit, and labs print metres. Never type a length into a scenario without the unit.
- **A model change moves goldens.** `tests/move_scenarios_test.cpp` holds every scenario's
  digest: rewrite it with `GE_GOLDEN_WRITE=1` only for a change you meant, say which runs moved
  and why, and update the lab digests (`make ge ARGS="digest move-trace move-strafe --write"`).
- **The camera reads, never writes** (`docs/movement.md` § What a camera reads). Swimming is the
  water module's (§ Water).

Read this skill's lessons first: `make lessons ARGS=move`.
