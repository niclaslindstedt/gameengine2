// THE MACHINES' LENSES: the free ride's snowmobile, helicopter and paramotor each seen from a
// ladder of its own (four rungs of engine rigs), and the hand-over between the skis' ladder and a
// machine's when he boards it or leaves it.
//
//   sled  seat (worn on the boards), chase (low, close, banked through a turn), far, high
//   heli  seat (worn on the skid, looking down past it), chase (well back, banked with it), far,
//         down (the arm over it, looking down the long fall under it)
//   para  pilot (worn in the harness, rolled with the wing), chase, wing (up by the canopy,
//         looking down on him), down
//
// The booms in the air keep the fall look off (it would be on for the whole flight) except on
// the `down` rung, where it is the point; they bank with the machine's roll in the air
// (`BoomRow::bank_air`), as a chase craft flies a turn banked with the one it follows.
//
// THE SUBJECTS: the sled's CoG and its own frame; the heli's skid datum and its fuselage's
// heading, pitch and roll; the para's pilot (his skis' base, hung under the wing) and the wing's
// heading and bank. The worn rungs' eyes are the figure's helmet eye (subject.hpp) carried into
// the machine's frame every frame, so they ride where the drawn head is. The moments (a landing,
// a teleport) are read as the skier's are.
//
// THE RIDE (`RideCamera`): the skis' run camera (subject.hpp) and the three machines' cameras.
// The rung follows the ride across the change (worn to worn, chase to chase, the third and fourth
// to theirs), and the change is a HAND-OVER of `kRideHandOverS` s: the arriving camera is snapped
// onto its subject, both are framed through it, and the picture blends from the leaving one's
// live pose to the arriving one's on a smootherstep, never a cut. Every lens is settled as a
// skier's is (the head, the snow, the bark: `settle_lens`). The camera key walks the active
// ladder (`next_lens`).
//
// ge:summary The machines' lenses: sled, heli and para ladders; the hand-over on and off a ride
// ge:keywords fall-line camera machine sled snowmobile heli helicopter para paramotor ride
// ge:keywords seat skid pilot wing down chase bank bank_air hand-over board hop drop
// ge:keywords ride_of machine_ladder lens_name ride-camera make_ride_camera next_lens set_lens
// ge:keywords frame_ride apply_ride_cvars
// ge:skill camera
// ge:test tests/fall_line_camera_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "camera/camera.hpp"
#include "camera/cvars.hpp"
#include "fall_line/camera/rungs.hpp"
#include "fall_line/camera/subject.hpp"
#include "fall_line/sim/state.hpp"

namespace fall_line::camera {

enum class Ride : uint8_t { skis, sled, heli, para };
inline constexpr int kMachines = 3;      // sled, heli, para
inline constexpr int kMachineRungs = 4;  // worn, chase, far, high/down

std::string_view ride_name(Ride r);
// What the player (racer 0) is on.
Ride ride_of(const sim::GameState& state);

// A machine's ladder rows (empty for the skis).
std::span<const ge::camera::Rig> machine_ladder(Ride r);
// The rung's name on the ride's ladder (the skis': `rung_name`).
std::string_view lens_name(Ride r, int rung);
// The rung named `name` on the ride's ladder into `out`; false for a name not on it.
bool lens_named(Ride r, std::string_view name, int& out);

// A machine's subject for the state (the ride must be on).
ge::camera::Subject machine_subject(const sim::GameState& state, Ride r, SubjectMemory& memory);

// The skis' rung a machine's rung hands over to, and back.
int machine_rung_of(Rung skis);
Rung skis_rung_of(int machine_rung);

// A hand-over between the skis and a machine, s.
inline constexpr double kRideHandOverS = 1.0;

struct RideCamera {
  RunCamera skis;
  ge::camera::Camera machines[kMachines];
  SubjectMemory memory[kMachines];
  Ride ride = Ride::skis;
  bool framed = false;
  // The hand-over in flight: the ride it leaves and its clock; `from == ride` when none.
  Ride from = Ride::skis;
  double t = 0;
};
RideCamera make_ride_camera(Rung start, uint32_t seed);

// The rung the active ladder is on.
int active_rung(const RideCamera& rc);
std::string_view active_lens(const RideCamera& rc);
bool handing_over(const RideCamera& rc);
// The camera key: the next rung of the active ladder.
void next_lens(RideCamera& rc);
// Put the camera at once on the ride `state` has the player on, on its rung named `name` (a skis'
// rung named on a machine puts it on that rung's counterpart); false for a name on neither.
bool set_lens(RideCamera& rc, const sim::GameState& state, std::string_view name);

// The player's camera settings (engine/camera/cvars.hpp) written over every ladder the ride
// camera holds, the skis' and each machine's, before a frame.
void apply_ride_cvars(ge::camera::CameraCvars& v, RideCamera& rc);

// One display frame of `dt` s of the camera on the player in `state`.
ge::camera::Pose frame_ride(RideCamera& rc, const sim::GameState& state,
                            const ge::skier::World& skier_world,
                            const ge::camera::World& camera_world, double dt);

}  // namespace fall_line::camera
