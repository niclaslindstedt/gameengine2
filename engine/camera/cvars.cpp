// The camera's cvars: registered with a game's defaults, applied over its ladder's rows.
//
// ge:summary The camera's cvars registered with a game's defaults and applied over its ladder
// ge:keywords camera cvars add_camera_cvars apply_camera_cvars world-unit degrees
// ge:skill camera console
// ge:test tests/camera_cvars_test.cpp
#include "camera/cvars.hpp"

#include <string>

#include "core/math.hpp"
#include "core/num_text.hpp"

namespace ge::camera {

namespace con = ge::console;

namespace {

constexpr double kDeg = core::kPi / 180;

std::string text(double v) { return core::num::to_string(v); }

}  // namespace

void add_camera_cvars(con::Cvars& c, CameraCvars& out, const CameraDefaults& d) {
  out.unit_m = d.unit_m;
  constexpr uint32_t kPlayer = con::kArchive;
  struct Row {
    std::string_view name;
    double fallback, min, max;
    std::string_view help;
    double* field;
  };
  const Row rows[] = {
      {"cl_fov", d.fov, 0, 170, "the first-person lens, deg horizontal at 4:3; 0: the rig's own",
       &out.fov},
      {"cl_viewheight", d.viewheight, 0, 1e4,
       "the first-person eyes over the body, world units; 0: the rig's own", &out.viewheight},
      {"cl_bob", d.bob, 0, 0.2, "the stride's bob a unit of ground speed (0 off)", &out.bob},
      {"cl_bobcycle", d.bobcycle, 0.05, 5, "the stride's period, s", &out.bobcycle},
      {"cl_bobup", d.bobup, 0.01, 0.99, "the rising share of the stride", &out.bobup},
      {"cl_rollangle", d.rollangle, 0, 30, "the strafe roll at full side speed, deg (0 off)",
       &out.rollangle},
      {"cl_rollspeed", d.rollspeed, 0.01, 1e5,
       "the side speed that reaches cl_rollangle, world units/s", &out.rollspeed},
      {"v_kicktime", d.kicktime, 0, 5, "how long a hit's view kick lasts, s", &out.kicktime},
      {"v_kickpitch", d.kickpitch, 0, 30, "a hit's view kick in pitch, deg a unit of its size",
       &out.kickpitch},
      {"v_kickroll", d.kickroll, 0, 30, "a hit's view kick in roll, deg a unit of its size",
       &out.kickroll},
      {"v_idlescale", d.idlescale, 0, 10, "the idle sway at its most, deg (0 off)", &out.idlescale},
      {"cl_landdip", d.landdip, 0, 4, "the landing dip, a share of the rig's own (0 off)",
       &out.landdip},
      {"cl_chasedist", d.chasedist, 0, 1e5,
       "the chase camera's arm behind, world units; 0: the rig's own", &out.chasedist},
      {"cl_chaseheight", d.chaseheight, 0, 1e5,
       "the chase camera's arm over, world units; 0: the rig's own", &out.chaseheight},
      {"cl_fovspeed", d.fovspeed, 0, 1, "how much a following lens widens with speed, 0 .. 1",
       &out.fovspeed},
      {"cl_camblend", d.camblend, 0, 4, "every camera change's length, a share; 0 cuts",
       &out.camblend},
      {"cl_motion", d.motion, 0, 1,
       "comfort: every camera motion you did not command (bob, roll, sway, tremor, shakes), 0 .. 1",
       &out.motion},
      {"cl_shake", d.shake, 0, 1, "the camera's shakes alone, 0 .. 1", &out.shake},
  };
  for (const Row& r : rows) {
    // The registry copies the fallback and the help (console/cvar.hpp's Cvar).
    const std::string fallback = text(r.fallback);
    c.bind({.name = r.name,
            .fallback = fallback,
            .flags = kPlayer,
            .help = r.help,
            .kind = con::CvarKind::number,
            .min = r.min,
            .max = r.max},
           r.field);
  }
  if (!d.camera.empty()) {
    const std::string help = "the camera's rung: " + std::string(d.ladder_names);
    out.camera_cvar = &c.bind({.name = "cl_camera",
                               .fallback = d.camera,
                               .flags = kPlayer,
                               .help = help,
                               .kind = con::CvarKind::text},
                              &out.camera);
    out.camera_seen = out.camera_cvar->revision;
  }
}

CameraSettings camera_settings(const CameraCvars& v) {
  CameraSettings s;
  s.motion = v.motion;
  s.shake = v.shake;
  s.blend = v.camblend;
  s.fov_speed = v.fovspeed;
  s.fov_h = v.fov * kDeg;
  return s;
}

bool apply_camera_cvars(CameraCvars& v, std::span<const Rig> ladder, Camera& cam, Edit edit,
                        double seconds) {
  cam.settings = camera_settings(v);
  const double u = v.unit_m;
  const int n =
      cam.count < static_cast<int>(ladder.size()) ? cam.count : static_cast<int>(ladder.size());
  for (int i = 0; i < n; ++i) {
    const Rig& base = ladder[static_cast<size_t>(i)];
    Rig& rig = cam.rigs[i];
    if (rig.kind == RigKind::first_person) {
      FirstPersonRow& r = rig.first_person;
      const FirstPersonRow& b = base.first_person;
      if (v.viewheight > 0) {
        r.eye_stand_m = v.viewheight * u;
        r.eye_crouch_m = b.eye_crouch_m * r.eye_stand_m / b.eye_stand_m;
      } else {
        r.eye_stand_m = b.eye_stand_m;
        r.eye_crouch_m = b.eye_crouch_m;
      }
      r.bob = v.bob;  // a length over a speed: the same in any unit
      r.bob_cycle_s = v.bobcycle;
      r.bob_up = v.bobup;
      r.roll_angle = v.rollangle * kDeg;
      r.roll_speed_mps = v.rollspeed * u;
      r.kick_time_s = v.kicktime;
      r.kick_pitch = v.kickpitch * kDeg;
      r.kick_roll = v.kickroll * kDeg;
      r.idle = v.idlescale * kDeg;
      r.dip_per_mps = b.dip_per_mps * v.landdip;
      r.dip_max_m = b.dip_max_m * v.landdip;
    } else if (rig.kind == RigKind::boom && rig.name == "chase") {
      rig.boom.dist_m = v.chasedist > 0 ? v.chasedist * u : base.boom.dist_m;
      rig.boom.height_m = v.chaseheight > 0 ? v.chaseheight * u : base.boom.height_m;
    }
  }
  if (!v.camera_cvar || v.camera_cvar->revision == v.camera_seen) return true;
  v.camera_seen = v.camera_cvar->revision;
  const int rung = find_rung(cam, v.camera);
  if (rung < 0) return false;
  if (rung != cam.rung) set_rung(cam, rung, edit, seconds);
  return true;
}

}  // namespace ge::camera
