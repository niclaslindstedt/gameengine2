// THE BOOM: the chase camera. A lens on an arm that stands behind the subject on a yaw that
// FOLLOWS the way it travels, at a height that is sprung rather than bolted, so a bump under the
// subject is not a bump under the lens. Every reading it chases is a second-order follower whose
// poles the row places (follow.hpp).
//
//   THE YAW        follows the heading (turned round when the subject runs backward, `switched`)
//                  with `slip` of the travel direction blended in, so a skid shows its slide.
//   THE HEIGHT     follows the subject's on the ground (`lift`) and softer in the air
//                  (`lift_air`), which is what makes a flight hang; its lag is let run only to
//                  `lag_max_m`, eased into (tanh), so a drop never takes the subject out of frame.
//   THE INCLINE    the ground's pitch is read along the yaw from `lean_behind_m` behind the subject
//                  to where it will be in `ahead_s` (at least `lean_reach_m`), followed (`lean`),
//                  and the arm rises up the slope behind by `incline` of it (`incline_steep` on
//                  the steepest faces): a lens that aims level shows half a picture of sky, one
//                  that tips all the way reads every slope as flat.
//   COMPOSITION    the look pitches to stand the subject at `place` of the half-fov below the
//                  middle of the frame (`look`), and never lets it out of `frame` of the half-fov.
//                  The look's yaw always runs through the subject.
//   SPEED          THE STRETCH: the fov widens by `fov_per_mps` (to `fov_max`) while the arm pulls
//                  in along its own line by `hold` of what keeps the subject its size, so the world
//                  rushes out past the edges. THE SURGE: the arm is let out under acceleration and
//                  in under braking (`surge_m_per_mps2`, capped). THE TREMOR: past
//                  `tremor_from_mps` the lens buzzes, more on rough ground (`Subject::surface`),
//                  none in the air.
//   THE BANK       the horizon takes `bank` of the subject's roll on the ground, and `bank_air`
//                  of it in the air (0: level; a craft's chase banks with its turns).
//   THE FALL LOOK  when the subject's ballistic arc says it still has `fall_from_m` to fall, the
//                  arm climbs over its back toward `fall_up` and the look tips down to hold the
//                  landing; let go over a second once it is back on the ground.
//   CLEARANCE      never closer than `clearance_m` to the ground under it; the arm is pulled in AT
//                  ONCE (at `pull_in_mps`, quick but
//                  never a cut) to the first thing solid between the subject's head and the lens
//                  (`World::clear`) and let back out by a half-life; and pushed sideways off point
//                  obstacles (`World::obstacles`) inside `push_soft_m` of `push_gap_m` from their
//                  bark, the way a magnet answers its like pole, remembering which side it went.
//
// `motion` (the player's comfort, 0..1) scales the tremor and the bank; `fov_speed` (0..1) the
// stretch. Both are the camera settings' (camera.hpp).
//
// ge:summary The boom (chase) rig: followed yaw and height, incline, composition, speed, clearance
// ge:keywords camera boom chase third-person follow arm yaw height lag incline slope lean
// ge:keywords composition framing place frame stretch fov-speed sense-of-speed surge tremor bank
// ge:keywords fall-look clearance pull-in occlusion push-off magnet obstacles trunks frame_boom
// ge:skill camera
// ge:test tests/camera_boom_test.cpp
#pragma once

#include <cstdint>

#include "camera/follow.hpp"
#include "camera/pose.hpp"
#include "camera/shake.hpp"
#include "camera/subject.hpp"
#include "camera/world.hpp"

namespace ge::camera {

struct BoomRow {
  double dist_m = 4.3;  // the arm behind the subject on level ground
  double dist_per_mps = 0.0;
  double height_m = 1.7;       // over the subject
  double aim_height_m = 1.0;   // the point of the subject the look is composed on
  double fov_y = 1.08;         // rad at rest
  double fov_per_mps = 0.013;  // rad of widening a m/s of speed
  double fov_max = 1.5;
  double hold = 0.5;  // share of the widening the arm pulls in for
  double surge_m_per_mps2 = 0.16;
  double surge_max_m = 1.1;
  double tremor_from_mps = 10, tremor_full_mps = 26;
  double tremor_rad = 0.0012;  // the look's swing at the whole of it
  double tremor_m = 0.01;
  double tremor_hz = 7.3;
  Poles yaw{1.2, 0.9, 0.0};
  double slip = 0.3;  // share of the travel direction against the heading
  Poles lift{1.8, 0.7, 2.0};
  Poles lift_air{0.9, 0.85, 2.0};
  double lag_max_m = 2.2;
  double incline = 0.5, incline_steep = 0.75;
  double steep_from = 0.35, steep_full = 0.6;  // rad of ground pitch
  double ahead_s = 0.6, lean_reach_m = 6, lean_behind_m = 2;
  Poles lean{0.7, 1.0, 0.0};
  double place = 0.3;   // share of the half-fov the subject stands below the middle
  double frame = 0.66;  // share of the half-fov it is never let out of
  Poles look{1.6, 0.85, 0.0};
  double bank = 0.12;
  double bank_air = 0;      // the share of its roll in the air
  double fall_from_m = 12;  // the fall still to come that brings the fall look in
  double fall_up = 1.1;     // rad over the subject the arm climbs to
  double fall_reach = 1.3;  // the arm's length in it, a share
  double fall_fov = 0.17;   // rad wider in it
  double gravity = 9.81;    // m/s^2, for the arc
  double clearance_m = 0.9;
  double pull_min_m = 1.6;       // the arm is never pulled in shorter
  double pull_margin_m = 0.3;    // it stops this short of what it was pulled in for
  double pull_in_mps = 30;       // how fast it is pulled in: quick, but never a cut
  double pull_halflife_s = 0.4;  // how it lets back out
  double push_gap_m = 1.0, push_soft_m = 2.5;
};

inline constexpr int kPushMemory = 8;

struct BoomState {
  bool fresh = true;
  Follower yaw{}, y{}, slope{}, pitch{};
  double pull = 1;  // the share of the arm the lens is let stand out along
  double last_speed = 0, accel = 0, surge = 0;
  double clock = 0, buzz = 0;
  double bank = 0;
  double fallen = 0;                // the fall look's share, 0..1
  uint32_t side_id[kPushMemory]{};  // the push-off's memory: an obstacle's id and its side
  int side[kPushMemory]{};
  Waves tremor = make_waves(0x5eed);
};

// How far the subject still has to fall on its ballistic arc before it meets the ground, m (0
// on the ground).
double fall_to_come(const Subject& s, const World& w, double gravity);

Pose frame_boom(const BoomRow& row, const Subject& s, const World& w, BoomState& st, double dt,
                double motion, double fov_speed);

}  // namespace ge::camera
