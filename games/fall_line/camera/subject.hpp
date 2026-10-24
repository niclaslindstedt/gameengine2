// WHAT FALL LINE'S CAMERA FOLLOWS: a skier's state as the camera's subject (engine/camera/
// subject.hpp), once a display frame.
//
// The subject's origin is the skis' base under the boots; its frame is the figure's (the skis'
// heading over the snow's normal, the flight's pitch in the air: skier/app/figure.hpp) rolled by
// the body's lean, so a worn lens tips into a carve with him and a boom's bank reads his roll.
// The surface under him is the packed share: the boom's tremor buzzes harder on the groomer than
// in powder, and nothing in the air.
//
// THE MOMENTS are read off the state between two frames, so a caller that steps the run several
// times a frame misses none: a LANDING (in the air last frame, on the snow this one) carries the
// fall speed he was carrying as the snow came up; a TELEPORT is a jump of more than `kTeleportM`
// between two frames (a reset, a start), and snaps every rig onto him.
//
// THE WORN EYES (`fit_worn`): the helmet's eye is set in front of the figure's head, where the
// tuck folds it down; the tips' a hand over the snow ahead of the boots, risen by the skis' sink
// so a lens over powder is not under its surface.
//
// THE FRAME (`frame_camera`): the worn eyes fitted, the subject read, the ladder framed, and one
// thing more. A hand-over to or from the helmet flies the lens through the space his head takes,
// so while a lens off the helmet rung is within `kHeadClip` m of his head's centre its near plane
// is pushed out past the head: the lens looks through his head on its way in or out, never at
// the inside of the helmet's shell. And no lens is let under `kLensFloor` m over the snow, nor
// nearer a trunk's bark than `kBarkClear` m: a worn one rides a body that a wipeout throws down
// on its side, or into the tree it met.
//
// ge:summary A skier as the camera's subject: pose, frame, surface, landings, resets, worn eyes
// ge:keywords fall-line camera subject skier frame lean landing teleport reset worn helmet tips
// ge:keywords framed_point see-through read_moments head_of helmet_eye settle_lens
// ge:keywords skier_subject fit_worn subject-memory frame_camera run-camera head-clip near-plane
// ge:skill camera
// ge:test tests/fall_line_camera_test.cpp
#pragma once

#include "camera/camera.hpp"
#include "fall_line/camera/rungs.hpp"
#include "skier/skier.hpp"

namespace fall_line::camera {

// A jump between two frames, m, that is a reset rather than motion.
inline constexpr double kTeleportM = 6.0;

// What the subject remembers between frames.
struct SubjectMemory {
  bool framed = false;
  bool air = false;
  double fall_mps = 0;  // his sinking speed in the air, last frame
  double x = 0, y = 0, z = 0;
};

// Read the moments (a landing, a teleport) off `out.pos` against the last frame's, with the
// body's sinking speed `vy` and whether it is `grounded` now, into `out`; and remember this frame.
void read_moments(ge::camera::Subject& out, double vy, bool grounded, SubjectMemory& memory);

ge::camera::Subject skier_subject(const ge::skier::SkierState& s, const ge::skier::World& world,
                                  SubjectMemory& memory);

// Set the camera's worn rows' eyes (tips, helmet) off the figure for `s`.
void fit_worn(ge::camera::Camera& cam, const ge::skier::SkierState& s,
              const ge::skier::World& world);

// The figure's head's centre, world m, and the helmet's eye in front of it (`head` the centre).
ge::core::Vec3 head_of(const ge::skier::SkierState& s, const ge::skier::World& world);
ge::core::Vec3 helmet_eye(const ge::skier::SkierState& s, const ge::skier::World& world,
                          const ge::core::Vec3& head);

// A lens nearer the head's centre than this, m, off the helmet rung, looks past the head.
inline constexpr double kHeadClip = 0.6;
// The head's reach from its centre the near plane is pushed past, m (the helmet's shell).
inline constexpr double kHeadReach = 0.22;

// The least a lens stands over the snow, m, and off the bark of a trunk.
inline constexpr double kLensFloor = 0.15;
inline constexpr double kBarkClear = 0.12;

// A framed lens settled: its near plane past his head (`head` the centre) when `clip_head` and it
// is within `kHeadClip`, held `kLensFloor` over the snow and `kBarkClear` off a trunk's bark
// (pushed out from a trunk's axis toward `toward_x`, `toward_z`).
void settle_lens(ge::camera::Pose& p, const ge::camera::World& camera_world,
                 const ge::core::Vec3& head, bool clip_head, double toward_x, double toward_z);

// The point a rung frames on him, world m: his middle, the height the booms look at, nearer the
// snow when he is down. The trees between a lens and it are seen through (app/frame.hpp's
// `FrameScene::subject`).
ge::camera::Vec3 framed_point(const ge::skier::SkierState& s);

// A run's camera: the ladder and what its subject remembers.
struct RunCamera {
  ge::camera::Camera cam;
  SubjectMemory memory;
};
RunCamera make_skier_camera(Rung start, uint32_t seed);

// One display frame of `dt` s of the camera on `s`.
ge::camera::Pose frame_camera(RunCamera& rc, const ge::skier::SkierState& s,
                              const ge::skier::World& skier_world,
                              const ge::camera::World& camera_world, double dt);

}  // namespace fall_line::camera
