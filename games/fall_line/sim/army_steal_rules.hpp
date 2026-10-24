// THE ARMY'S HELICOPTERS TAKEN OR BROUGHT DOWN: the numbers army_steal.hpp works by. Every number
// carries its unit.
//
// TAKEN: a machine set down on its skids (to put its men out or take them in) is his to take at
// its pilot's door, forward of the cabin's door on the right, a step out from the cabin's side.
// He is in the pilot's seat with the rotor turning as its crew left it, and it is the free
// ride's helicopter from then on.
//
// BROUGHT DOWN: the free ride's helicopter flown into one of theirs (its disc into theirs, at
// the height of their disc, or its airframe into their airframe) brings theirs down: spinning,
// its tail rotor gone, down onto the snow, where it lies as a wreck. His crashes by its own
// rules.
//
// ge:summary The numbers of an army helicopter taken off the snow or rammed out of the sky
// ge:keywords army helicopter steal stolen ram rammed downed fall wreck pilot-door rules
// ge:skill sim
// ge:test tests/fall_line_army_steal_test.cpp
#pragma once

namespace fall_line::sim::army_steal {

// THE PILOT'S DOOR: `kPilotAheadM` m fore of the skid datum and `kPilotOutM` m out to the right
// of the middle. He takes the machine within `kReachM` m of it, no faster than `kFastestM` m/s.
inline constexpr double kPilotAheadM = 2.0;
inline constexpr double kPilotOutM = 2.0;
inline constexpr double kReachM = 6.6;
inline constexpr double kFastestMs = 12;

// THE RAM: their disc's hub `kHubM` m over the skid datum; the two discs meet when their hubs are
// nearer than the two radii across and within `kDiscM` m of each other's height, and the
// airframes when the two datums are within `kHullM` m.
inline constexpr double kHubM = 3.6;
inline constexpr double kDiscM = 2.5;
inline constexpr double kHullM = 6;

// THE FALL: it keeps `kKeepShare` of its own way and takes the rest of his; it falls at
// `kFallShare` of gravity, its way bled at `kDragPerS` a second, and spins up to `kSpin` rad/s
// with its tail rotor gone, rolled toward `kRoll` and nosed down toward `kPitch` rad. On the
// snow it lies as a wreck at `kWreckRoll` and `kWreckPitch` rad for `kWreckS` s, then is gone.
inline constexpr double kKeepShare = 0.6;
inline constexpr double kFallShare = 0.8;
inline constexpr double kDragPerS = 0.3;
inline constexpr double kSpin = 2.4;
inline constexpr double kRoll = 0.5;
inline constexpr double kPitch = -0.2;
inline constexpr double kWreckRoll = 0.35;
inline constexpr double kWreckPitch = -0.08;
inline constexpr double kWreckS = 120;

}  // namespace fall_line::sim::army_steal
