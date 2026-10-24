// FALL LINE'S FRAME: the game's pass table over engine/render, and the one object that owns a
// frame's GPU half (docs/rendering.md § The frame). `kPassTable` lists the passes in the order
// they run, with the named targets each reads and writes; `draw` runs exactly that order, and
// `pass_table_faults` holds the order to the engine's check.
//
//   sky.tables           -> sky.transmittance sky.multiscatter sky.view sky.moon_view (at night)
//                           sky.aerial sky.light
//   skinning             -> figures.posed (every racer's skier model posed into his mesh)
//   terrain.cull         -> terrain.tiles terrain.indirect (and terrain.normals, once)
//   instances.cull       -> instances.early (the forest, the figures and the crowd: the
//                           frustum, the last frame's pyramid)
//   terrain.horizon      -> terrain.horizon (the bakes due)
//   trails.fine          -> trails.fine (the tracks laid since the last frame, or the whole
//                           window redrawn from the history when it moved)
//   trails.coarse        -> trails.coarse (the tracks laid since the last frame)
//   particles.sim        -> particles (the spray thrown since the last frame, flown a frame)
//   snowfall.sim         -> snowfall (the flakes round the eye, carried a frame on the wind)
//   shadows.cull         -> shadows.lists (the casters of the maps drawn this frame)
//   shadows.near         -> shadows.maps (the near cascade: the forest, when due)
//   shadows.far          -> shadows.maps (the far cascade, when due)
//   shadows.figures      -> shadows.maps (the figures' atlas, every frame: the racers, the
//                           nearest amateurs; each tile its own casters)
//   terrain.depth        -> depth (the prepass)
//   instances.depth      -> depth (the early lists' prepass)
//   hiz                  -> hiz (the depth's pyramid)
//   instances.cull.late  -> instances.late (what the last frame's pyramid hid, against this one)
//   instances.depth.late -> depth
//   terrain              -> hdr motion (shaded where the depth is equal, in the snow's material;
//                           reads shadows.maps and the trail maps)
//   instances            -> hdr motion (both lists, where the depth is equal)
//   sky                  -> hdr motion (behind depth)
//   clouds               -> hdr (the cloud's layers, behind depth)
//   water.copy           -> water.scene (the opaque colour and depth the water reads through)
//   water                -> hdr motion depth (every body of water added, render/water.hpp)
//   water.under          -> hdr (the view under water, when the eye is under a body)
//   particles            -> hdr motion (the spray, over the picture; the motion's reactive b)
//   snowfall             -> hdr motion (the falling snow round the eye, as the particles)
//   taa                  -> taa.history (at the output's size; with FrameOptions::taa)
//   bloom                -> bloom (the glow's mip chain, from what post reads)
//   post                 -> final (the bloom mixed in, the grade's table: look.hpp's)
//
// With TAA the frame renders at FrameOptions::scale of the output's size, jittered, and the
// resolve brings it up to the output; without it, at the output's size, unjittered, and post
// reads the HDR target.
//
// The frame reads the game only through a FrameView (views.hpp), the map it was made for, and a
// FrameScene: the racers' skiers this frame, drawn as skinned skiers (scene.hpp: each racer's
// pose from skier/app/rig_pose.hpp's poser, its lags kept here between frames), laying their tracks
// (`track`: skier/app/tracks.hpp's prints into the trail maps) and throwing their spray
// (skier/app/spray.hpp's grains into the particle pool, render/particles.hpp, over the time between
// two frames' `FrameView::time_s`: a frame at the same time throws nothing and holds the spray
// still). The air is the map's at the view's time (mountain/air/air.hpp): its wind carries the
// spray and the falling snow, and its fall says how much of the snow is drawn. Every pipeline is
// made in `create_frame` (the manifest), and the device's load ends there: a pipeline made after it
// is an error.
//
// ge:summary Fall Line's frame: its pass table over engine/render and the GPU half it owns
// ge:keywords fall-line frame pass-table game-frame create_frame draw pass_table_faults
// ge:keywords render offscreen surface stats draw_wire wire-probe wireframe water add_water
// ge:skill render
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "fall_line/app/scene.hpp"
#include "fall_line/cfg/cvars.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "mountain/app/snow.hpp"
#include "mountain/app/trails.hpp"
#include "mountain/mapgen/types.hpp"
#include "render/bloom.hpp"
#include "render/clouds.hpp"
#include "render/frame.hpp"
#include "render/hiz.hpp"
#include "render/instances.hpp"
#include "render/particles.hpp"
#include "render/post.hpp"
#include "render/shadows.hpp"
#include "render/skinning.hpp"
#include "render/sky.hpp"
#include "render/snowfall.hpp"
#include "render/taa.hpp"
#include "render/terrain.hpp"
#include "render/underwater.hpp"
#include "render/water.hpp"
#include "skier/app/rig_pose.hpp"
#include "skier/app/spray.hpp"
#include "skier/app/tracks.hpp"
#include "skier/skier.hpp"

namespace fall_line::app {

// The ground's clipmap: level 0's quad, m, and its levels (render/terrain.hpp): 8 km round the eye.
inline constexpr double kGroundSpacing = 1.0;
inline constexpr int kGroundLevels = 8;

struct FrameOptions {
  bool taa = true;
  double scale = 1.0;           // the render size over the output's, with TAA
  bool overdraw_probe = false;  // labs: the terrain's and the instances' overdraw pipelines
  bool wire_probe = false;      // labs and recordings: the terrain's and the instances' wireframes
  bool occlusion = true;        // cull the instances by the depth pyramid (off: the frustum only)
  uint32_t shadow_size = 2048;  // the sun's shadow maps' texels a side
  double bloom = 0.04;          // the share of the bloom's glow in the picture; 0: none
  bool snow = true;             // the snow's material (mountain/app/snow.hpp); off: the albedo lit
  bool trails = true;           // the tracks (mountain/app/trails.hpp); drawn only with the snow
  bool spray = true;            // the racers' spray (skier/app/spray.hpp) thrown into the particles
  ge::render::ParticleSettings particles;
  bool snowfall = true;  // the falling snow, when the map's sky snows
  bool weather =
      true;  // the map's sky and cloud in the picture (mountain/app/cloud_field.hpp): off, clear
  double clouds = 1.0;        // the share of the cloud march's steps (0.25..1); 0: no layer drawn
  bool cloud_quarter = true;  // the cloud marched at a quarter of the pixels (false: every one)
  bool cloud_spread = true;   // the quarter's march spread over four frames, the rest reprojected
  double reach_m = 0;  // how far the picture draws, misted (render/view.hpp); 0: the whole ground
  ge::render::SnowfallSettings flakes;
  ge::mountain::app::TrailOptions trail_maps;  // their sizes
};

// The options a player's cfg chose (the r_ cvars, cfg/cvars.hpp), the rest at their defaults.
FrameOptions frame_options(const cfg::Settings& s);

// What of the run a frame draws, besides the map: the racers, the player's first. `settle` poses
// every figure settled, keeping nothing of the last frame (a still, or a jump in the run); off, the
// figures' lags run on over `dt`, the run's seconds since the last frame (0: no step was taken).
struct FrameScene {
  std::span<const ge::skier::SkierState> skiers;
  double dt = 0;
  bool settle = true;
  // The free ride's crowd (sim/crowd.hpp), by id: each drawn as the skier model in his own pose
  // (scene.hpp's `crowd_instance`); empty, none.
  std::span<const sim::Amateur> crowd = {};
  // The point the camera frames, world m (a rung's: camera/subject.hpp's `framed_point`): the
  // trees between the eye and it are seen through (render/instances.hpp § Seeing through). None:
  // every tree whole (a view of the mountain).
  std::optional<std::array<double, 3>> subject = std::nullopt;
};

struct GameFrame {
  FrameOptions options;
  ge::gpu::PipelineSet pipelines;
  uint32_t output_width = 0, output_height = 0;
  ge::render::FrameTargets targets;  // at the render size
  ge::render::FrameHistory history;
  wgpu::Buffer uniforms;
  ge::render::FrameUniforms staged{};
  wgpu::BindGroupLayout frame_layout;
  wgpu::BindGroup frame_group;
  wgpu::BindGroup post_groups[2];  // over TAA's histories; [0] over the HDR target without TAA
  ge::render::Sky sky;
  ge::render::Terrain terrain;
  ge::mountain::app::SnowMaterial snow;
  ge::mountain::app::Trails trails;
  bool tracks = false;                    // the trails are made (FrameOptions::snow and ::trails)
  std::vector<ge::skier::TrackPen> pens;  // each racer's, by his place in the scene
  std::vector<ge::mountain::app::TrailStamp> laid;
  ge::render::Particles particles;
  ge::render::Snowfall snowfall;
  ge::render::CloudPass clouds;
  ge::render::Water water;       // its bodies, `add_water`'s; none: the passes draw nothing
  ge::render::Underwater under;  // the view when the eye is under a body (`under.look`)
  std::vector<ge::skier::SprayPen> spray_pens;  // each racer's, by his place in the scene
  std::vector<ge::skier::SprayGrain> thrown;
  std::vector<ge::render::Particle> births;
  double spray_time = 0;      // the last drawn frame's time_s
  bool spray_placed = false;  // a frame has been drawn since the spray was last forgotten
  bool spray_forget = false;  // kill every particle at the next draw
  ge::render::Hiz hiz;
  ge::render::Taa taa;
  ge::render::Bloom bloom;
  ge::render::Post post;
  ge::render::InstancePipelines instancing;
  ge::render::InstanceFamily forest, figures, crowd;
  ge::render::InstanceFamily* families[3] = {&forest, &figures, &crowd};
  wgpu::BindGroup instance_light;
  ge::render::SunShadows shadows;
  const ge::mountain::mapgen::Level* level = nullptr;
  // The skinned skiers: the model (scene.hpp's `skier_model`), its rig binding, one skin and one
  // poser a racer, and the pose's scratch.
  const ge::assets::Model* skier = nullptr;
  ge::skier::RigBinding binding;
  uint32_t materials_per_racer = 0;
  ge::render::SkinPipelines skinning;
  std::array<ge::render::Skin, kMaxFigures> skins;
  std::array<ge::render::Skin*, kMaxFigures> skin_list{};
  std::array<ge::skier::RigPoser, kMaxFigures> posers;
  ge::skier::RigPose rig_pose;
  std::vector<ge::assets::Trs> locals;
  std::vector<ge::assets::Affine> globals, palette;
  std::vector<ge::render::Instance> staged_figures;
  std::vector<std::array<double, 3>> figure_centres;
  // The crowd: a poser an amateur by id, every amateur's palette in a row, his instance.
  std::vector<ge::skier::RigPoser> crowd_posers;
  std::vector<ge::assets::Affine> crowd_palettes;
  std::vector<ge::render::Instance> staged_crowd;
  ge::render::FrameRecorder recorder;
  std::vector<uint8_t> albedo;
};

// Empty when the pass table's order holds (render/frame.hpp's check).
std::string pass_table_faults();

// The frame for `level`, drawing outputs of width x height in `output` format. Null after an
// error line when a pipeline fails. Ends the device's load.
std::unique_ptr<GameFrame> create_frame(ge::gpu::Device& device,
                                        const ge::mountain::mapgen::Level& level, uint32_t width,
                                        uint32_t height, wgpu::TextureFormat output,
                                        const FrameOptions& options = {});

// New targets for outputs of width x height (a resized window); the history starts over.
void resize(ge::gpu::Device& device, GameFrame& frame, uint32_t width, uint32_t height);

// The camera cut: the next frame keeps nothing of the last (no motion, no history). The tracks
// stay: they are the run's, not the camera's.
void cut(GameFrame& frame);

// Lay the scene's racers' tracks from where each was at the last call (or the last draw, which
// lays them too) to where they are now. A lab that steps a run between frames calls it every few
// steps so a turn is laid as a curve.
void track(GameFrame& frame, const FrameScene& scene);

// A body of water drawn in the frame (water/world.hpp's: a lake on the map, its waves laid by
// the wind); its index. `set_water` reads it again (its waves laid again, a new look).
int add_water(ge::gpu::Device& device, GameFrame& frame, const ge::water::Body& body,
              const ge::render::WaterDraw& draw = {});
void set_water(ge::gpu::Device& device, GameFrame& frame, int index, const ge::water::Body& body);

// Forget every track and all the spray (a new run on the same map).
void forget_tracks(GameFrame& frame);

// Encode and submit one frame of `view` and `scene` into `output` (a view of a texture of the
// output format at the output's size). `view.frame` indexes the jitter. The stats' GPU times
// arrive a frame or more later.
void draw(ge::gpu::Device& device, GameFrame& frame, const ge::render::FrameView& view,
          const FrameScene& scene, const wgpu::TextureView& output);

// The last drawn frame's meshes as lines (needs FrameOptions::wire_probe): the ground's tiles
// banded by distance and the trees and figures in one warm colour, over black, into `colour` (a
// kWireFormat view at the render size), hidden lines hidden by the frame's depth.
void draw_wire(ge::gpu::Device& device, GameFrame& frame, const wgpu::TextureView& colour);

}  // namespace fall_line::app
