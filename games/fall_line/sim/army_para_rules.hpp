// THE ARMY'S PARAMOTORS' NUMBERS (army_para.hpp flies them): the riflemen the wanted level's
// roster asks for (`WantedRoster::paramotors`), how they come in, how they hold him in sight and
// when they fire. Every number carries its unit.
//
// THE RIG is the free ride's own paramotor (`plugins/paramotor`): the same wing on its lines, the
// same engine and polar, flown by the same four controls, with a soldier in the harness and a
// rifle across his chest instead of the skier. It cruises near 55 km/h and climbs a couple of
// metres a second on full throttle, so it cannot keep up with a man skiing hard downhill; it
// holds him in sight from a slow circle over him and catches him up where he slows.
//
// HOW IT HUNTS: a CIRCLE some 150 m round the man and 80 m over him (never under 100 m over the
// snow), its way round his own (the pilot looking in over his inward shoulder), the aim a point
// ahead on the circle so the wing turns onto it and follows it round; out of sight a wider, higher
// circle round where he was last seen. A rifleman fires a burst whenever he has the man in sight
// inside the rifle's reach and his last burst is far enough behind, by the hunt's rules of fire.
//
// ge:summary The army's paramotors' numbers: the call, the circle, the search, the rifle, home
// ge:keywords army paramotor army-para rules rifleman circle orbit search call stagger leave
// ge:keywords separation pursuer
// ge:skill sim
// ge:test tests/fall_line_army_para_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace fall_line::sim::army_para {

// THE CALL: a rig comes in `kRespondS` s after the roster first asks for it (the next `kStaggerS`
// after the one before), at the MAP'S EDGE on the bearing the army's helicopters come in on (each
// rig spread `kSpread` rad either side of it), but no further from him than `kNearM` m along that
// line, `kEnterM` m over the snow there and never under `kOrbitUpM` over him, flying at `kTrimMs`.
inline constexpr double kRespondS = 40;
inline constexpr double kStaggerS = 15;
inline constexpr double kSpread = 0.5;
inline constexpr double kNearM = 1500;
inline constexpr double kEnterM = 200;
inline constexpr double kTrimMs = 15;

// THE CIRCLE: `kOrbitM` round him and `kOrbitUpM` over him where he is seen; out of sight, round
// where he was last seen, `kSearchWider` times wider and `kSearchUpM` over that spot. The aim is a
// point `kLead` rad ahead round the circle; further out than `kFarShare` of the circle's radius
// the rig flies straight at its middle. Its aim never asks for less than `kHoldM` over the snow,
// and under `kLowM` (sinking air, or snow rising faster than it climbs) it leaves the circle and
// flies down the fall line until it is back over `kFloorM`; under `kFloorM` its toggles never
// pull past `kLowSteer`, since a hard turn sheds the height it is short of.
inline constexpr double kOrbitM = 150;
inline constexpr double kOrbitUpM = 80;
inline constexpr double kSearchWider = 1.6;
inline constexpr double kSearchUpM = 100;
inline constexpr double kLead = 0.6;
inline constexpr double kFarShare = 1.5;
inline constexpr double kHoldM = 100;
inline constexpr double kFloorM = 70;
inline constexpr double kLowM = 45;
inline constexpr double kLowSteer = 0.3;

// APART: a rig keeps `kApartM` m off another and `kHeliKeepM` m off an army helicopter's hub,
// its aim pushed away from either while they are closer than that and within `kLevelM` m of its
// height.
inline constexpr double kApartM = 80;
inline constexpr double kHeliKeepM = 60;
inline constexpr double kLevelM = 40;

// THE RIFLE (gunfire.hpp): fired from `kMuzzleUpM` over the harness, a burst at a time while he is
// in sight and inside the rifle's reach, no sooner than `kEveryS` s after the last (each man's
// rhythm his own, spread by up to `kEverySpread` of it off his id). Gun ids `kGunBase` + the rig's
// id.
inline constexpr double kMuzzleUpM = 0.3;
inline constexpr double kEveryS = 3;
inline constexpr double kEverySpread = 0.3;
inline constexpr int32_t kGunBase = 40000;

// HOME: the hunt over, out to the map's edge on the bearing it came in on, `kHomeUpM` over the
// snow, and gone once `kGoneM` from it: deep in the band where the free ride's wind off the edge
// turns every wing back (`paramotor::spec::edge`), so out of the hunt. It comes in `kPastM` inside
// the edge.
inline constexpr double kHomeUpM = 150;
inline constexpr double kGoneM = 120;
inline constexpr double kPastM = 100;

// BROUGHT DOWN (the wing onto the snow or into a crown): the soldier lies where he lands for
// `kLieS` s, then is gone. A rig lost so is counted (`ArmyState::paras_lost`): it is a fault.
inline constexpr double kLieS = 20;

// THE PURSUERS: the rifleman's eyes `kEyeM` m over the harness, pursuer `kPursuerBase` + the rig's
// id.
inline constexpr double kEyeM = 0.6;
inline constexpr int32_t kPursuerBase = 950;

// The most rigs out at once (the roster's top row).
inline constexpr size_t kMostParas = 4;

}  // namespace fall_line::sim::army_para
