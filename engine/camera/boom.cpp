// The boom's frame: the followed yaw and height, the incline, the speed, the fall look, the
// clearance (pull-in, push-off, the ground) and the composition.
//
// ge:summary The boom's frame: yaw, height, incline, speed, fall look, clearance, composition
// ge:keywords camera boom chase frame incline stretch surge tremor pull-in push-off composition
// ge:skill camera
// ge:test tests/camera_boom_test.cpp
#include "camera/boom.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"

namespace ge::camera {

namespace libm = core::libm;

namespace {

constexpr double kFallIn = 0.6;       // s the fall look takes to come in
constexpr double kFallOut = 1.0;      // s it takes to go once the subject is down
constexpr double kFallPlace = -0.45;  // where the subject stands in it: above the middle
constexpr double kFallSlip = 0.9;     // the travel's share of the yaw in it

// The ground's pitch along a bearing, rad, positive dropping ahead; 0 with no ground.
double ground_pitch(const World& w, const Subject& s, double lx, double lz, double behind,
                    double reach) {
  if (!w.ground) return 0;
  const double back = ground_at(w, s.pos.x - lx * behind, s.pos.z - lz * behind);
  const double front = ground_at(w, s.pos.x + lx * reach, s.pos.z + lz * reach);
  return core::clamp(libm::atan2(back - front, reach + behind), -0.6, 1.2);
}

// Push the eye sideways off the obstacles near it (the header's magnet).
void push_off(const BoomRow& row, const World& w, BoomState& st, double yaw, Vec3& eye) {
  if (!w.obstacles) return;
  Obstacle near[16];
  const double reach = row.push_gap_m + row.push_soft_m;
  const int n = obstacles_near(w, eye.x, eye.z, reach, near, 16);
  const double ax = libm::cos(yaw), az = -libm::sin(yaw);  // across the arm, to its right
  for (int i = 0; i < n; ++i) {
    const Obstacle& o = near[i];
    if (eye.y < o.y0 - 0.5 || eye.y > o.y1 + 0.5) continue;
    const double bark = core::hypot(eye.x - o.x, eye.z - o.z) - o.radius;
    if (bark >= reach) continue;
    // The side, remembered per obstacle so a lens running at one dead on is not flipped from
    // one side of it to the other half way past.
    int slot = -1, empty = -1;
    for (int k = 0; k < kPushMemory; ++k) {
      if (st.side[k] != 0 && st.side_id[k] == o.id) slot = k;
      if (st.side[k] == 0 && empty < 0) empty = k;
    }
    int side = 0;
    if (slot >= 0) {
      side = st.side[slot];
    } else {
      side = (o.x - eye.x) * ax + (o.z - eye.z) * az > 0 ? -1 : 1;
      const int k = empty >= 0 ? empty : static_cast<int>(o.id % kPushMemory);
      st.side_id[k] = o.id;
      st.side[k] = side;
    }
    const double into = (reach - bark) / row.push_soft_m;  // 0 at the soft edge
    double shove = row.push_soft_m * 0.5 * core::smoothstep(0.0, 1.0, into);
    if (bark + shove < row.push_gap_m) shove = row.push_gap_m - bark;
    eye.x += side * ax * shove;
    eye.z += side * az * shove;
  }
  // Forget the sides of obstacles left behind.
  for (int k = 0; k < kPushMemory; ++k) {
    if (st.side[k] == 0) continue;
    bool still = false;
    for (int i = 0; i < n; ++i) still = still || near[i].id == st.side_id[k];
    if (!still) st.side[k] = 0;
  }
}

}  // namespace

double fall_to_come(const Subject& s, const World& w, double gravity) {
  if (s.grounded || !w.ground) return 0;
  double x = s.pos.x, y = s.pos.y, z = s.pos.z, vy = s.vel.y;
  constexpr double kStep = 0.05;
  for (int i = 0; i < 200; ++i) {
    x += s.vel.x * kStep;
    z += s.vel.z * kStep;
    y += vy * kStep - 0.5 * gravity * kStep * kStep;
    vy -= gravity * kStep;
    const double g = ground_at(w, x, z);
    if (y <= g) return s.pos.y - g > 0 ? s.pos.y - g : 0;
  }
  return s.pos.y - y;
}

Pose frame_boom(const BoomRow& row, const Subject& s, const World& w, BoomState& st, double dt,
                double motion, double fov_speed) {
  bool snap = st.fresh;
  for (int i = 0; i < s.event_count; ++i) snap = snap || s.events[i].kind == Moment::teleport;
  st.fresh = false;
  const double speed = core::hypot3(s.vel.x, s.vel.y, s.vel.z);
  const double plan = core::hypot(s.vel.x, s.vel.z);

  // The fall look: in with a long fall, never let down in the air, out on the ground.
  const double to_come = fall_to_come(s, w, row.gravity);
  const double fall_want = s.grounded ? 0.0 : to_come > row.fall_from_m ? 1.0 : st.fallen;
  if (snap) {
    st.fallen = fall_want;
  } else {
    st.fallen =
        core::approach(st.fallen, fall_want, dt / (fall_want > st.fallen ? kFallIn : kFallOut));
  }
  const double fallen = core::smoothstep(0.0, 1.0, st.fallen);

  // The yaw.
  const double nose = s.heading;
  const double travel = plan > 2 ? libm::atan2(s.vel.x, s.vel.z) : nose;
  const double slip = core::lerp(row.slip, kFallSlip, fallen);
  const double want_yaw = nose + core::angle_diff(nose, travel) * slip;
  const double yaw = snap ? settle(st.yaw, want_yaw) : follow_angle(st.yaw, row.yaw, want_yaw, dt);
  const double lx = libm::sin(yaw), lz = libm::cos(yaw);

  // The incline.
  const double reach = core::num::max(row.lean_reach_m, row.ahead_s * speed);
  const double pitch_ground = ground_pitch(w, s, lx, lz, row.lean_behind_m, reach);
  const double slope =
      snap ? settle(st.slope, pitch_ground) : follow(st.slope, row.lean, pitch_ground, dt);

  // The height, its trend down the slope on the ground and the body's own climb in the air.
  const double trend = s.grounded ? -plan * libm::tan(slope) : s.vel.y;
  const double lifted =
      snap ? settle(st.y, s.pos.y)
           : follow_trend(st.y, s.grounded ? row.lift : row.lift_air, s.pos.y, trend, dt);
  const double lag = s.pos.y - lifted;
  const double y = s.pos.y - row.lag_max_m * libm::tanh(lag / row.lag_max_m);

  // The surge: the acceleration along the way, read and followed.
  if (snap || dt <= 0) {
    st.last_speed = speed;
    st.accel = 0;
    st.surge = 0;
  } else {
    const double raw = s.grounded ? (speed - st.last_speed) / dt : 0.0;
    st.last_speed = speed;
    st.accel = damp(st.accel, raw, 0.17, dt);
    const double want =
        core::clamp(st.accel * row.surge_m_per_mps2, -row.surge_max_m, row.surge_max_m);
    st.surge = damp(st.surge, want, 0.23, dt);
  }

  // The stretch and the arm.
  const double chased =
      core::num::min(row.fov_max, row.fov_y + row.fov_per_mps * speed * fov_speed);
  const double fov = chased + row.fall_fov * fallen;
  const double arm = 1.0 - row.hold * (1.0 - libm::tan(row.fov_y * 0.5) / libm::tan(chased * 0.5));
  const double dist = (row.dist_m + row.dist_per_mps * speed) * arm + st.surge;
  const double high = row.height_m * arm;
  const double len = core::hypot(dist, high) * (1.0 + (row.fall_reach - 1.0) * fallen);
  const double steep = core::smoothstep(row.steep_from, row.steep_full, slope);
  const double incline = core::lerp(row.incline, row.incline_steep, steep);
  const double up =
      (libm::atan2(high, dist) + incline * slope) * (1.0 - fallen) + row.fall_up * fallen;
  Vec3 eye{s.pos.x - lx * len * libm::cos(up), y + len * libm::sin(up),
           s.pos.z - lz * len * libm::cos(up)};

  // The tremor: an envelope by pace and ground, gone in the air.
  const double pace = core::smoothstep(row.tremor_from_mps, row.tremor_full_mps, speed);
  const double buzz_want = s.grounded ? pace * pace * (0.5 + 0.5 * s.surface) * motion : 0.0;
  st.buzz = snap ? buzz_want : damp(st.buzz, buzz_want, buzz_want > st.buzz ? 0.14 : 0.04, dt);
  st.clock += dt;
  double shake_yaw = 0, shake_pitch = 0, shake_roll = 0;
  if (st.buzz > 1e-4) {
    shake_yaw = row.tremor_rad * st.buzz * wave(st.tremor.axis[0], st.clock, row.tremor_hz);
    shake_pitch = row.tremor_rad * st.buzz * wave(st.tremor.axis[1], st.clock, row.tremor_hz);
    shake_roll = row.tremor_rad * st.buzz * wave(st.tremor.axis[2], st.clock, row.tremor_hz);
    eye.x += row.tremor_m * st.buzz * wave(st.tremor.axis[3], st.clock, row.tremor_hz);
    eye.y += row.tremor_m * st.buzz * wave(st.tremor.axis[4], st.clock, row.tremor_hz);
  }

  // Clearance: off the obstacles, pulled in to the first thing solid, over the ground.
  push_off(row, w, st, yaw, eye);
  const Vec3 head{s.pos.x, s.pos.y + row.aim_height_m, s.pos.z};
  const double open = clear_share(w, head, eye);
  const double arm_len = core::hypot3(eye.x - head.x, eye.y - head.y, eye.z - head.z);
  const double least = arm_len > 0 ? core::num::min(1.0, row.pull_min_m / arm_len) : 1.0;
  const double short_of = open < 1.0 && arm_len > 0 ? open - row.pull_margin_m / arm_len : open;
  const double want_pull = core::num::max(least, short_of);
  if (snap) {
    st.pull = want_pull;
  } else if (want_pull < st.pull) {
    const double most = arm_len > 0 ? row.pull_in_mps * dt / arm_len : 1.0;
    st.pull = core::num::max(want_pull, st.pull - most);
  } else {
    st.pull = damp(st.pull, want_pull, row.pull_halflife_s, dt);
  }
  eye = Vec3{head.x + (eye.x - head.x) * st.pull, head.y + (eye.y - head.y) * st.pull,
             head.z + (eye.z - head.z) * st.pull};
  const double floor = ground_at(w, eye.x, eye.z) + row.clearance_m;
  if (eye.y < floor) eye.y = floor;

  // The composition: the look's yaw through the subject, its pitch placing it in the frame.
  const double dx = head.x - eye.x, dz = head.z - eye.z;
  const double to_subject = libm::atan2(head.y - eye.y, core::hypot(dx, dz));
  const double half = fov * 0.5;
  const double place = core::lerp(row.place, kFallPlace, fallen);
  const double want_pitch = to_subject + place * half;
  double look = snap ? settle(st.pitch, want_pitch) : follow(st.pitch, row.look, want_pitch, dt);
  look = core::clamp(look, to_subject - row.frame * half, to_subject + row.frame * half);

  // The bank: a share of the body's roll on the ground, and its own share in the air (a craft's
  // turn, flown banked).
  const double body_roll = core::to_euler(s.q).roll;
  const double bank_want = body_roll * (s.grounded ? row.bank : row.bank_air) * motion;
  st.bank = snap ? bank_want : damp(st.bank, bank_want, 0.15, dt);

  Pose p;
  p.eye = eye;
  p.yaw = libm::atan2(dx, dz) + shake_yaw;
  p.pitch = look + shake_pitch;
  p.roll = st.bank + shake_roll;
  p.fov_y = fov;
  p.near_m = 0.1;
  return p;
}

}  // namespace ge::camera
