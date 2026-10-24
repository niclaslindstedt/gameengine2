// THE CAMERA'S CVARS: every camera setting a player turns, registered on a console (engine/console)
// with a game's own defaults and its WORLD UNIT, and applied to a camera stack once a frame.
//
//   cl_fov                         deg   the first-person lens, horizontal at 4:3 (0: the rows')
//   cl_viewheight                  wu    the eyes over the body, first person (0: the rows')
//   cl_bob cl_bobcycle cl_bobup    share, s, share   the stride's height a unit of speed, its
//                                        period, its rising share
//   cl_rollangle cl_rollspeed      deg, wu/s   the strafe roll and the sideways speed reaching it
//   v_kicktime v_kickpitch v_kickroll   s, deg, deg   the view punch of a hit
//   v_idlescale                    deg   the idle sway at its most (0 off)
//   cl_landdip                     share of the rows' landing dip
//   cl_camera                      name  the rung (the game's ladder); only when a game asks
//   cl_chasedist cl_chaseheight    wu    the chase boom's arm (0: the rows')
//   cl_fovspeed                    0..1  how much a boom's lens widens with speed
//   cl_camblend                    share every transition's length; 0 cuts
//   cl_motion                      0..1  comfort: every motion the player did not command
//   cl_shake                       0..1  the shakes alone
//
// The cvars are BOUND to a `CameraCvars` in their own units, so a set (a cfg, the console, the
// command line) writes a field and the camera never looks a name up. `apply_camera_cvars` turns
// them into metres, seconds and radians (a distance times the world unit, `unit_m`) and writes them
// over the ladder's own rows: the settings struct, every first-person row and the boom named
// `chase`. A length or a lens of 0 leaves the row's own, so a game's ladder stands as it was cut
// until a player turns it. `cl_camera` changes the rung by the edit the game names when it moves.
//
// ge:summary The camera's cvars: cl_fov, cl_bob, v_kick, cl_chase, cl_motion; units and defaults
// ge:keywords camera cvars cvar settings cfg cl_fov cl_viewheight cl_bob cl_rollangle v_kickpitch
// ge:keywords cl_camera cl_chasedist cl_fovspeed cl_camblend cl_motion cl_shake comfort world-unit
// ge:keywords add_camera_cvars apply_camera_cvars camera-cvars camera-defaults
// ge:skill camera console
// ge:test tests/camera_cvars_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "camera/camera.hpp"
#include "console/cvar.hpp"

namespace ge::camera {

// A game's defaults, in the cvars' own units, and its world unit.
struct CameraDefaults {
  double unit_m = 1.0;  // metres a world unit
  double fov = 0;       // deg; 0: the rows'
  double viewheight = 0;
  double bob = 0.02, bobcycle = 0.6, bobup = 0.5;
  double rollangle = 2, rollspeed = 5;
  double kicktime = 0.5, kickpitch = 0.6, kickroll = 0.6;
  double idlescale = 0;
  double landdip = 1;
  double chasedist = 0, chaseheight = 0;
  double fovspeed = 1, camblend = 1, motion = 1, shake = 1;
  // The rung `cl_camera` starts on; empty: no `cl_camera` (the game names its view its own way).
  std::string_view camera = {};
  std::string_view ladder_names = {};  // the rungs, for cl_camera's help
};

// The values the cvars write, in their own units.
struct CameraCvars {
  double unit_m = 1.0;
  double fov = 0, viewheight = 0;
  double bob = 0, bobcycle = 0, bobup = 0;
  double rollangle = 0, rollspeed = 0;
  double kicktime = 0, kickpitch = 0, kickroll = 0;
  double idlescale = 0, landdip = 1;
  double chasedist = 0, chaseheight = 0;
  double fovspeed = 1, camblend = 1, motion = 1, shake = 1;
  std::string camera;
  console::Cvar* camera_cvar = nullptr;  // null when the game did not ask for cl_camera
  uint32_t camera_seen = 0;              // the revision of cl_camera last applied
};

// Register every camera cvar on `cvars` (a player's: archived), bound to `out`.
void add_camera_cvars(console::Cvars& cvars, CameraCvars& out, const CameraDefaults& d);

// The settings struct the cvars say (metres, radians, shares).
CameraSettings camera_settings(const CameraCvars& v);

// Write the cvars over `cam`: its settings, and each rig's row over `ladder`'s own (the rows the
// camera was made from, in its rung order). When `cl_camera` moved since the last apply, the rung
// it names is taken by `edit` over `seconds` (a name not on the ladder is left, and false
// returned).
bool apply_camera_cvars(CameraCvars& v, std::span<const Rig> ladder, Camera& cam,
                        Edit edit = Edit::blend, double seconds = 0.6);

}  // namespace ge::camera
