// The camera's cvars (engine/camera/cvars.hpp): registered with a game's defaults, the defaults
// leave the reference ladder's rows as they were cut; a cfg's values reach the settings and the
// rows in metres and radians through the world unit; a 0 length or lens gives the row back its
// own; cl_camera changes the rung; comfort 0 stills every uncommanded motion of every rig.
//
// ge:summary Tests: the camera's cvars, defaults, units, the rows they write, cl_camera, comfort
// ge:keywords test camera cvars cl_fov cl_viewheight cl_bob cl_chasedist cl_motion cl_camera unit
// ge:skill camera testing
#include <cmath>

#include "camera/camera.hpp"
#include "camera/cvars.hpp"
#include "camera/ladders.hpp"
#include "camera/scenarios.hpp"
#include "check.hpp"
#include "console/console.hpp"
#include "core/math.hpp"

namespace cam = ge::camera;
namespace con = ge::console;

namespace {

constexpr double kDeg = ge::core::kPi / 180;

cam::Camera reference_camera() {
  const auto l = cam::reference_ladder();
  return cam::make_camera(l.data(), static_cast<int>(l.size()), 1);
}

const cam::Rig& rig(const cam::Camera& c, std::string_view name) {
  return c.rigs[cam::find_rung(c, name)];
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

}  // namespace

TEST(the_defaults_leave_the_reference_rows_as_they_were_cut) {
  con::Console console;
  cam::CameraCvars v;
  cam::add_camera_cvars(console.cvars(), v, {});
  cam::Camera c = reference_camera();
  CHECK(cam::apply_camera_cvars(v, cam::reference_ladder(), c));
  const auto l = cam::reference_ladder();
  for (int i = 0; i < c.count; ++i) {
    const cam::Rig& a = c.rigs[i];
    const cam::Rig& b = l[static_cast<size_t>(i)];
    CHECK(near(a.first_person.eye_stand_m, b.first_person.eye_stand_m));
    CHECK(near(a.first_person.bob, b.first_person.bob));
    CHECK(std::fabs(a.first_person.roll_angle - b.first_person.roll_angle) < 1e-4);
    CHECK(std::fabs(a.first_person.kick_pitch - b.first_person.kick_pitch) < 1e-4);
    CHECK(near(a.first_person.roll_speed_mps, b.first_person.roll_speed_mps));
    CHECK(near(a.boom.dist_m, b.boom.dist_m) && near(a.boom.height_m, b.boom.height_m));
  }
  CHECK(c.settings.motion == 1 && c.settings.shake == 1 && c.settings.fov_h == 0);
}

TEST(a_cfg_sets_every_camera_setting_through_the_world_unit) {
  con::Console console;
  cam::CameraCvars v;
  cam::CameraDefaults d;
  d.unit_m = 0.0254;  // a world in inches
  cam::add_camera_cvars(console.cvars(), v, d);
  console.execute(
      "cl_fov 110; cl_viewheight 64; cl_bob 0.01; cl_bobcycle 0.8; cl_bobup 0.4; "
      "cl_rollangle 3; cl_rollspeed 200; v_kicktime 0.3; v_kickpitch 1; v_kickroll 2; "
      "v_idlescale 1; cl_landdip 0.5; cl_chasedist 120; cl_chaseheight 60; cl_fovspeed 0.5; "
      "cl_camblend 0; cl_motion 0.5; cl_shake 0.25");
  cam::Camera c = reference_camera();
  cam::apply_camera_cvars(v, cam::reference_ladder(), c);
  CHECK(near(c.settings.fov_h, 110 * kDeg));
  CHECK(c.settings.motion == 0.5 && c.settings.shake == 0.25 && c.settings.blend == 0);
  CHECK(c.settings.fov_speed == 0.5);
  const cam::FirstPersonRow& fp = rig(c, "eyes").first_person;
  const cam::FirstPersonRow& base = cam::reference_ladder()[0].first_person;
  CHECK(near(fp.eye_stand_m, 64 * 0.0254));
  CHECK(near(fp.eye_crouch_m, base.eye_crouch_m * 64 * 0.0254 / base.eye_stand_m));
  CHECK(fp.bob == 0.01 && fp.bob_cycle_s == 0.8 && fp.bob_up == 0.4);
  CHECK(near(fp.roll_angle, 3 * kDeg) && near(fp.roll_speed_mps, 200 * 0.0254));
  CHECK(fp.kick_time_s == 0.3 && near(fp.kick_pitch, kDeg) && near(fp.kick_roll, 2 * kDeg));
  CHECK(near(fp.idle, kDeg));
  CHECK(near(fp.dip_per_mps, base.dip_per_mps * 0.5) && near(fp.dip_max_m, base.dip_max_m * 0.5));
  const cam::BoomRow& chase = rig(c, "chase").boom;
  CHECK(near(chase.dist_m, 120 * 0.0254) && near(chase.height_m, 60 * 0.0254));
  // The far boom is not the chase: the player's arm leaves it alone.
  CHECK(near(rig(c, "far").boom.dist_m, cam::reference_ladder()[2].boom.dist_m));
  // Back to 0: the rows' own again.
  console.execute("cl_chasedist 0; cl_viewheight 0");
  cam::apply_camera_cvars(v, cam::reference_ladder(), c);
  CHECK(near(rig(c, "chase").boom.dist_m, cam::reference_ladder()[1].boom.dist_m));
  CHECK(near(rig(c, "eyes").first_person.eye_stand_m, base.eye_stand_m));
}

TEST(a_value_out_of_range_is_clamped) {
  con::Console console;
  cam::CameraCvars v;
  cam::add_camera_cvars(console.cvars(), v, {});
  console.execute("cl_motion 3; cl_fov -10");
  CHECK(v.motion == 1 && v.fov == 0);
}

TEST(cl_camera_changes_the_rung_when_it_moves) {
  con::Console console;
  cam::CameraCvars v;
  cam::CameraDefaults d;
  d.camera = "chase";
  d.ladder_names = "eyes chase far high helmet tips orbit trackside";
  cam::add_camera_cvars(console.cvars(), v, d);
  cam::Camera c = reference_camera();
  c.rung = cam::find_rung(c, "far");
  // Unchanged since registration: the game's own rung stands.
  CHECK(cam::apply_camera_cvars(v, cam::reference_ladder(), c));
  CHECK(c.rung == cam::find_rung(c, "far"));
  console.execute("cl_camera helmet");
  CHECK(cam::apply_camera_cvars(v, cam::reference_ladder(), c));
  CHECK(c.rung == cam::find_rung(c, "helmet"));
  console.execute("cl_camera nowhere");
  CHECK(!cam::apply_camera_cvars(v, cam::reference_ladder(), c));
  CHECK(c.rung == cam::find_rung(c, "helmet"));
  // Without a ladder's names the game names its view its own way: no cl_camera.
  con::Console other;
  cam::CameraCvars w;
  cam::add_camera_cvars(other.cvars(), w, {});
  CHECK(other.cvars().find("cl_camera") == nullptr);
}

TEST(comfort_zero_stills_every_uncommanded_roll) {
  con::Console console;
  cam::CameraCvars v;
  cam::add_camera_cvars(console.cvars(), v, {});
  console.execute("cl_motion 0");
  for (const char* id : {"walk", "strafe", "carve", "fire"}) {
    const cam::Scenario* sc = cam::find_camera_scenario(id);
    const cam::World w = cam::scenario_world(*sc);
    cam::Camera c = reference_camera();
    cam::apply_camera_cvars(v, cam::reference_ladder(), c);
    std::vector<cam::Camera> cams(static_cast<size_t>(c.count), c);
    for (int i = 0; i < c.count; ++i) cams[static_cast<size_t>(i)].rung = i;
    cam::ScriptRun run = cam::start_script(*sc);
    double worst = 0;
    for (int f = 1; f <= static_cast<int>(sc->seconds * 60); ++f) {
      const cam::Subject s = cam::advance_to(run, f / 60.0);
      for (auto& k : cams) worst = std::fmax(worst, std::fabs(cam::update(k, s, w, 1.0 / 60).roll));
    }
    CHECK_MSG(worst < 1e-12, std::string(id) + ": a roll at comfort 0");
  }
}
