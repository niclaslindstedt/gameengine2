# Rendering

How the engine draws a frame: the GPU backend, the frame's passes, and the techniques
each pass uses. Milestones M6 to M8 in `docs/goals.md` build it, and this page is the plan
they build to: M6 built the backend (`engine/gpu`, `engine/platform`); the passes are M7 and
M8. Change this page in the same PR that
changes the plan.

The aim is a picture that looks good and costs little. Every technique below was
chosen against the workload in § The workload, and each one names what it replaces and
what it costs. A technique that does not pay for itself on that workload is listed under
§ Considered and not taken, with the reason, so the question is not reopened without
new evidence.

## The workload

The demo game draws an open mountain to a horizon several kilometres away, seen from a
fast chase camera close to the ground. Per frame, at the top of its picture ladder:

| What | Size | What it implies |
| --- | --- | --- |
| Terrain | One heightfield, about 4 km of reach, a few cm a texel near the camera through a deformable patch | Shading the ground's pixels is the largest single GPU cost, most of all on tile-based (phone) GPUs |
| Vegetation | 30–40 thousand instanced trees in a few shapes, three levels of detail plus impostor cards | The CPU cost of choosing and sending instances, and the shadow pass that draws them all again |
| Props and buildings | Lifts and their moving carriers, a village, gates, fences: hundreds of instanced meshes | Draw submission; per-instance culling, not per-mesh |
| Figures | The player, a few rivals, and up to a few hundred people in a crowd, all skinned | Skinning once and drawing many times (main view and shadows) |
| Snow marks | Trail maps stamped from the skiers' prints every frame | Render-to-texture with max blending, a fine window that follows the camera redrawn from the stamps' history, and a coarse map over the whole mountain |
| Particles | Tens of thousands of spray, snowfall and cloud particles | Overdraw of soft transparent quads; simulated on the GPU |
| Sky | A physical atmosphere, a raymarched cloud volume, a night sky, aerial perspective on every material | Lookup tables computed on the GPU; a temporal cloud buffer |
| Light | One sun or moon, the sky's light, at most a handful of point lamps at night | No need for a many-light pipeline |
| Shadows | The mountain shading itself over kilometres, sharp shadows under the figures | Two different problems, solved by two different techniques |

Measured on this scene, the costs that matter most are: 200–350 draw calls and about
1 M triangles a frame when meshes are drawn one by one; a sun shadow pass that redraws
most of the scene; CPU time spent choosing instances; shader programs compiled in the
middle of a run, which cause visible hitches; and, on a phone, the ground's fragment
shader taking more than half of the GPU's time. Each of those has a direct answer below.

## The backend: Dawn, native only

The targets are macOS, Linux and Windows on the desktop, and iOS, iPadOS, the Mac App
Store and Android on the stores. There is no web build. Under the hood that is Metal on
Apple, Vulkan on Linux and Android, and D3D12 or Vulkan on Windows.

**Decision: Dawn, compiled natively on every target, through its standard `webgpu.h` C
header.** The header's name comes from the API's origin as a browser standard; nothing
here runs in a browser or ships for the web. Dawn is a native library that translates
that API to Metal, Vulkan and D3D12, and the engine uses it only for that. With no web
build, the case rests on these reasons:

- **One API over every native backend.** Dawn translates to Metal, Vulkan and D3D12.
  Our own layer would mean at least two backends (Metal for the App Store, Vulkan for
  Linux and Android) for agents to keep correct, plus our own barrier tracking and
  memory management.
- **Mobile drivers come with workarounds.** Dawn is the GPU layer of a large browser on
  Android, macOS and Windows, so it carries per-vendor workarounds for driver bugs
  (Adreno, Mali, PowerVR, Apple, and the desktop vendors) that a hand-written Vulkan
  backend would have to rediscover one crash at a time.
- **Validation an agent can act on.** In debug builds, every call is validated and an
  error names the object by its label and says what was wrong. A GPU-side fault in raw
  Vulkan or Metal is often a hang or a corrupt frame with no message, which only agents
  can debug here. Release builds turn validation off (Dawn's `skip_validation` toggle)
  and drop the shader robustness transform (`disable_robustness`), so the checks cost
  nothing in a shipped frame. `gpu::create_device` sets both in a release build
  (`tests/gpu_device_test.cpp` draws with them on).
- **WGSL everywhere, compiled by one compiler.** Tint compiles WGSL to MSL, SPIR-V and
  HLSL, so there is no shader cross-compiler toolchain in the build, and
  `make shaders-check` validates every shader offline.
- **Native extensions without forking the code.** Dawn native exposes what the web
  spec lacks (multi-draw indirect, subgroups, push-constant-like "immediates",
  timestamps inside passes, memoryless transient attachments). The engine turns them
  on per adapter and keeps a path for when they are missing (§ Feature tiers).
- **A software GPU for the main development environment and CI.** Dawn runs on
  SwiftShader or Mesa's lavapipe (Vulkan drivers on the CPU) on Linux. The everyday
  environment and the CI runners have no GPU, and both can still run the renderer and
  capture screenshots (§ Where it is developed).

**What we give up**, and why it is acceptable for this workload:

| Missing from Dawn's core API | What it would buy | Why the game does not need it |
| --- | --- | --- |
| Bindless resources | Any texture from any shader with no binding | Few materials; texture arrays per size class and a material table cover it (§ Geometry and resources) |
| Mesh shaders | Meshlet culling inside the pipeline | Compute culling plus indirect draws gets the same culling at instance granularity, which is what a forest of identical shapes needs; most phones lack mesh shaders anyway |
| Ray tracing | RT shadows, reflections, GI | Outdoor snow under one sun; the techniques below cover the look, and phones could not afford it |
| Async compute queues | Overlapping compute and graphics | One queue. Compute work is ordered so it sits where the GPU would otherwise be idle, and a frame is small enough that overlap gains little |
| Explicit barriers | Hand-tuned synchronisation | Dawn places them. Passes are ordered so that independent work is not forced to wait |

If one of these gaps ever blocks a measured goal, the escape hatch is a Dawn-native
extension behind a feature tier, never a second graphics API.

**Alternatives** (the full comparison is in `docs/research.md` § GPU API):

- **Vulkan everywhere, with MoltenVK or KosmicKrisp on Apple.** One API with bindless,
  async compute and full control. It was rejected because agents would own manual
  synchronisation and memory, mobile driver bugs and the Metal translation layer, and
  the workload has no measured need for what it adds.
- **Our own thin layer over Metal and Vulkan.** The most control and the best fit to each
  platform, at the cost of two backends to keep in step.
- **SDL3's GPU API** (stable since SDL 3.2) covers Vulkan, Metal and D3D12, including
  iOS and Android, and SDL is already a dependency. It was rejected for now because it
  has no GPU timestamp queries, which the perf-first goal needs, and it needs a shader
  cross-compiler (SPIR-V, MSL, DXIL) in the build.
- **wgpu-native** implements the same header. Two implementations would double the
  platform matrix for shader and validation behaviour. Code that keeps to the standard
  header keeps the option open.
- **bgfx, sokol_gfx, Diligent, Filament**: each brings its own shader dialect, build and
  abstraction for agents to learn, and none gives more of what this workload needs.

**Where Dawn is built.** The prebuilt GitHub release covers Linux x64, Windows x64 and
macOS (arm64): `cmake/Dawn.cmake` pins one release tag and each archive's SHA-256, and
downloads it into `.cache/deps/` at configure time (the archive also carries `tint`, which
`make shaders-check` runs). The prebuilt library is a Release build, so an MSVC Debug build
refuses `GE_GPU=ON`. iOS, iPadOS and Android (and Linux arm64) will build Dawn from source with
its CMake build, cached in CI (M11). Until then those rows build with `GE_GPU=OFF`, which
builds and tests everything except `engine/gpu`, `engine/render` and the app.

**The C++ surface.** `engine/gpu` calls Dawn's `webgpu_cpp.h` (`wgpu::Device`,
`wgpu::Buffer`, …), whose handles are reference-counted RAII already. The engine adds
behaviour (the device's choices, the error lines, the manifest's counter, readback, timing)
and no second layer of handles, so the code reads as Dawn's API spells it.

## Mobile GPUs

Phones and tablets (Apple's GPUs, Adreno, Mali) render in tiles. That changes a few
choices, and each is a feature tier, not a separate renderer:

- **No depth prepass on tile GPUs.** Apple's GPUs remove hidden surfaces in hardware,
  and Adreno and Mali have their own early-depth schemes, so a prepass mostly costs a
  second vertex pass. On a tiler, the opaque pass draws front to back with
  `less-equal`, and the two-phase occlusion culling runs one phase against last frame's
  pyramid. The rule against `discard` in opaque shaders matters even more there.
- **Memoryless attachments.** Depth, MSAA colour and other targets that do not outlive a
  pass are transient (Dawn's transient attachments: memoryless on Metal, lazily
  allocated on Vulkan), so they never leave tile memory.
- **Bandwidth over arithmetic.** Targets are 16-bit floats or packed formats where the
  look allows; post work is merged into as few full-screen passes as possible; MSAA
  resolves in the tile.
- **Thermal headroom.** A phone holds its clock only below full load, so the budget for
  the middle of the ladder leaves headroom (§ Measurement), and the game can cap the
  frame rate at 30 or 60 Hz.

## Feature tiers

The device asks the adapter for its own limits (the defaults are far below what a
desktop GPU offers) and turns on every optional feature from the list below that the
adapter has. Each feature has a path for when it is absent. Both paths are captured by
the lab tests, so neither one can rot.

| Feature | Used for | When it is missing |
| --- | --- | --- |
| `timestamp-query` | GPU time per pass | CPU timing only; the frame's stats show `gpu ms: n/a` |
| `indirect-first-instance` | Indirect draws that start at an offset into a culled instance list | Each draw's base offset is read from a small per-draw storage buffer |
| `multi-draw-indirect` (Dawn extension) | One call for every LOD bucket of a family | A loop of `drawIndexedIndirect`, one per bucket |
| `subgroups` | Prefix sums and reductions in culling and the depth pyramid | Workgroup shared memory |
| `shader-f16` | Half-precision particle and post maths | 32-bit floats |
| `float32-filterable` | Filtering the height and sky tables in 32-bit | 16-bit float copies of those tables |
| `texture-compression-bc` / `-astc` / `-etc2` | Compressed textures for authored assets (BC on the desktop, ASTC on Apple and most phones) | Uncompressed RGBA8 |
| Transient attachments (Dawn extension) | Depth and MSAA targets kept in tile memory on mobile | Ordinary textures |

Only Dawn's core API is a hard requirement. Compatibility mode (Dawn over GLES or D3D11)
is not targeted, because its limits (few or no storage buffers in the vertex stage)
would rule out vertex pulling. On Android that means a Vulkan 1.1 device, which covers
the phones that can run the game at all.

## The frame

The frame is a fixed, ordered **pass table**: one function in the game's app lists the
passes in order, and each pass declares the named targets it reads and writes. Dawn
schedules barriers from those uses. There is no general render graph; the table is the
graph, written out where an agent can read it. A pass has two halves:

```cpp
void prepare_terrain(gpu::Device&, Terrain&, const FrameView&);   // CPU: decide, stage, upload
void encode_terrain(const Terrain&, const gpu::PipelineSet&,      // GPU: bind and draw;
                    FrameRecorder&, wgpu::CommandEncoder&, ...);  // makes no decisions
```

**As built (M7a).** The table is a `constexpr` array of `render::PassRow {name, reads,
writes}` in the game's app (`games/fall_line/app/frame.cpp`, `kPassTable`), the read and
write lists space-separated target names; `render::check_pass_table` names any read before
its write (or a pass listed twice) with no GPU, and the `world` lab holds the game's table
to it. Each pass is opened through `render::FrameRecorder`, which gives it its timestamp
pair and its row in `FrameStats` (GPU ms a frame late, draws, dispatches, the CPU half).
The camera is at the origin of every matrix (the GPU works camera-relative in float, so a
point kilometres away keeps its millimetres), and depth is **reversed-z with an infinite
far plane** in `Depth32Float`, cleared to 0 and tested `greater`. The frame block (`FrameUniforms`,
`render/wgsl/frame_types.wgsl`) and the sky's tables are group 0 of every draw
(`render/wgsl/frame.wgsl`).

The order, with what each stage is for:

1. **Upload.** Each buffer that changed gets one `queue.writeBuffer` from a CPU staging
   array that `prepare` filled. Instance data sends only the ranges that changed.
2. **Compute, before any view is drawn:**
   - the sky's tables, only when the sun moved more than a threshold (§ Sky);
   - the aerial-perspective volume for this camera;
   - the terrain's horizon map, a slice a frame while the sun's bearing moves (§ Shadows);
   - stamping this frame's contacts into the snow-mark maps;
   - particle simulation;
   - skinning for figures that are drawn in more than one view;
   - instance culling, phase one (§ GPU-driven instances).
3. **Shadow views:** the cached sun cascades when they are due, and the figures' shadow
   atlas every frame.
4. **Depth prepass** (desktop GPUs; skipped on tile GPUs, § Mobile GPUs) of the terrain and opaque instances, then a **depth pyramid**
   (Hi-Z) built from it, then culling **phase two** for what the pyramid shows was
   wrongly culled.
5. **Opaque colour** with the depth test set to `equal`: every pixel of the expensive
   snow shader runs exactly once, whatever the draw order. This is the direct answer to
   the ground's fragment cost, and it lets the ground's shader drop `discard`, which
   tile-based GPUs pay for across a whole draw.
6. **Sky and clouds:** the sky behind the depth buffer; the cloud volume over it (marched at a
   quarter of the pixels, a quarter of those each frame and the rest reprojected from the last,
   then upsampled by depth and resolved by TAA: § Sky).
7. **Water** (`render/water.hpp`, `docs/water.md` § Drawn): the opaque frame's colour and
   depth copied (`water.copy`), then every body drawn over it (`water`), reading the copies
   for what lies under it and what it mirrors, testing and writing the depth, so what follows
   sits in front of or behind it; both rows run empty with no water in view. With the eye
   under a body, `water.under` redraws the picture as seen through the water
   (`render/underwater.hpp`).
8. **Transparent:** particles and glows, softened against the depth buffer.
9. **Temporal resolve:** anti-aliasing and upscaling to the output size (§ Anti-aliasing).
10. **Post:** one compute pass for exposure, bloom, the colour grade, tone mapping, dither
   and the output encoding (§ Post).
11. **UI:** the HUD and cards, at output resolution.

**As built (M7c).** Fall Line's table runs `sky.tables`, `terrain.cull`, `terrain.horizon`,
`terrain.depth`, `hiz`, `terrain`, `sky`, `taa`, `post`. The terrain draws twice from one
vertex stage whose position is `@invariant`: depth only (`terrain.depth`, `greater`, writing),
then shaded where the depth is `equal` with the depth read only (`terrain`), so a covered
pixel's snow shader runs once. The `world` lab's overdraw probe (`encode_terrain_overdraw`)
counts it: exactly 1 a covered pixel, but where two clipmap rings meet at the very same depth
and both pass `equal` (a few pixels in ten thousand); with no prepass the same draw shades 1.0
to 2.2 a pixel on seed 38's views, by how much of the mountain hides behind itself. The
**Hi-Z pyramid** (`render/hiz.hpp`) reduces the prepass's depth to an `R32Float` mip chain from
half the render size down to 1 × 1, each texel the farthest depth (reversed-z: the minimum)
of every texel below it that overlaps it, so an odd edge is never dropped; it matches its CPU
reference (`hiz_reduce`) texel for texel, and the instance families' second culling phase
reads it (§ GPU-driven instances). The colour pass and the sky write the **motion target** (`RGBA16Float` at
the render size: rg the uv this pixel's surface moved since the last frame, unjittered; b the
reactive mask, 0 for the ground and the sky but where the snow's glitter writes it (M8a), and
particles' later) from
the frame block's `prev_view_proj` and `eye_delta` (`FrameHistory`, `frame_types.wgsl`'s
`screen_motion`); the sky's motion is the camera's turn alone.

**As built (M7d1).** The table grows the instances around the terrain's: `sky.tables`,
`terrain.cull`, `instances.cull` (phase one, reading the last frame's pyramid),
`terrain.horizon`, `terrain.depth`, `instances.depth`, `hiz`, `instances.cull.late`,
`instances.depth.late`, `terrain`, `instances` (both phases' lists, tested `equal`), `sky`,
`taa`, `bloom` (M7d3), `post`; M8e2 adds `skinning` straight after `sky.tables`, writing
`figures.posed`, which every pass that reads the figures' family reads. The late phase's depth lands before the colour pass, so it is still shaded
once; the late phase's draws are not in this frame's pyramid, which only makes the next
frame's phase one keep a little more.

**No pipeline is created after load.** Every pipeline the game can use is listed in a
manifest and created with `createRenderPipelineAsync` behind the loading card. The GPU
layer counts pipeline creations, and after load a creation is an error that names the
pipeline and says to add it to the manifest. A missing entry fails the lab tests
instead of causing a hitch in a run.

**As built (M7d2).** The sun's shadows land between the horizon bake and the prepass:
`shadows.cull` (the casters' cull into each map drawn this frame), then `shadows.near`,
`shadows.far` and `shadows.figures`, each a depth-only pass into its layer of the shadow maps;
the `terrain` and `instances` rows read `shadows.maps`. A cascade that is not due still opens its
pass (read-only, nothing drawn), so the recorder's GPU times keep their order.

## GPU-driven instances

Every repeated thing (trees, rocks, props, carriers, crowd members, gate poles) is an
**instance family**: one storage buffer of instances in SoA layout (position, rotation,
scale, kind, flags), a table of meshes with their LOD ranges and bounding spheres, and
the indirect draw arguments for each (mesh, LOD) bucket. The CPU never walks the
instances in a frame. It writes only the instances that moved.

Culling is a compute pass over every family for every view:

1. **Phase one:** test each instance against the view's frustum and last frame's depth
   pyramid, choose its LOD from its projected size (with a dithered cross-fade band
   between LODs, so a change never pops), and append the visible ones to a compacted
   list for each bucket. Atomics or a subgroup prefix sum give each instance its slot,
   and the bucket's instance count is written into its indirect arguments.
2. **Phase two**, after the depth prepass: retest the instances that phase one rejected
   by occlusion against this frame's pyramid, and draw the ones that are visible now.
   This two-phase scheme removes the one-frame-late artefacts of plain reprojected
   occlusion culling when the camera turns fast.

Shadow views run the same pass with their own rule. An instance casts when its shadow
can reach the shadowed area, not when it is near the camera, so riding towards a wood
never switches its shadows on.

Each bucket is one indirect draw, or one multi-draw indirect call for a whole family
where the feature exists. The forest and the crowd then cost a handful of draws
whatever their size, and the CPU's frame cost no longer grows with the instance count.

**As built (M7d1)** (`render/instances.hpp`, `instances_cull.wgsl`, `instances.wgsl`). A
family's meshes are built on the CPU (`render/mesh.hpp`: boxes, cylinders and cones,
ellipsoids, faceted, wound outward) and packed to three words a vertex: the position in
16-bit steps of the mesh's box, a part (a material slot) and the normal octahedral in two
snorm16s. The vertex stage pulls them from one storage buffer, so a family is one pipeline
whatever its meshes. The instances are five streams of `vec4f` in one storage buffer
(position and material, rotation, scale and mesh, last position, last rotation, the last two
for the motion target); `set_instances` writes a range, and the CPU keeps a mirror so a write
carries the old pose into the last one. A **bucket** is one (phase, LOD) pair with its own
`DrawIndexedIndirect` arguments; portable indirect draws carry no first instance, so the
bucket rides in `base_vertex` (`bucket << 20`) and the vertex stage reads its list slot from
`vertex_index >> 20` and `instance_index`.

The cull is one thread an instance. **Phase one** drops what lies outside the frustum, what
is under 0.35 px of radius, and (with a last frame) what the last frame's pyramid hides,
seen through the last frame's camera; that last kind is marked. **Phase two**, after the
pyramid, retests the marked ones against this frame's. The test projects the bounding
sphere's box, widens its rectangle by a render pixel (the last frame was drawn under a
jitter this camera does not have), reads the mip where the rectangle spans at most three
texels a side and compares the box's nearest depth with the farthest there. A mip's size is
taken from the pyramid's halving, never from `textureDimensions(hiz, mip)`, which the
software adapter answers wrongly when the level differs between threads. The **LOD** is
picked by the radius on screen against each LOD's least size, with a band of ±15 % round
each threshold where the instance is appended to both LODs and each discards its share of
pixels by a dithered threshold in the depth prepass only (the colour pass tests `equal` and
never discards); `pick_lod` is the CPU twin. The lit draw shades by the sun, the ground's own
horizon shadow at the instance's point (`terrain_common.wgsl`'s `ground_shadow`, shared with
the terrain) and the sky's light, hazed like the ground. Back faces are culled: the world is
left-handed and the view a reflection, so a front face is clockwise on screen.

The **cull check**: the `world` lab draws every view a second time with occlusion culling
off and counts the pixels that differ, which must be none (`cull-faults`);
`tests/render_instances_test.cpp` holds the phases' counts on a wall with boxes behind it.
On seed 38 at 640 × 360 a run's moment draws about 3,000 trees of 21,000 and the pyramid
hides about 9,000; the forest is twenty meshes (five forms, four variants each) at three LODs
each (`plugins/mountain/app/trees.hpp`), 9 to 384 triangles a tree.

**Seeing through (M26g)** (`set_see_through`, `SeeThrough`). A follow camera in a wood sees
crowns, not its subject. A family may be told the point the camera frames; every upright
instance whose crown holds the eye or crosses the line from the eye to that point (stopped
0.6 m short of it, so a tree beside the subject and not before him stays whole) keeps only a
dithered share of its pixels in the depth prepass, easing from all of them 1 m off the line
to 15 % on it. An instance stands as its axis (its position up its rotated +y by its y
scale) and its crown (the larger of its x and z scales), which is what a unit-sized upright
mesh at its true size is. The share is computed per instance in the vertex stage (a branch
on a uniform when the family is not seen through), uses the LOD band's dither, so in a band
the two LODs together keep just that share, and moves smoothly with the line, so nothing
pops; under TAA the screen door reads as a ghost of the tree. The shadow maps draw it whole,
so its shadow stays on the snow. `see_through_keep` is the CPU twin; the `camera-world` lab
measures how much of the skier each outside lens sees in the densest wood of seed 38: 80 to
93 % of him, against 0 to 33 % with the trees drawn whole.

## Geometry and resources

- **Vertex pulling.** Meshes of one vertex format share one large storage buffer, and
  the vertex shader reads its vertex by index. Pipelines are keyed by material model,
  not by mesh, so there are few of them.
- **Compact vertex formats.** Positions are 16-bit normalised integers inside the mesh's
  bounds, normals and tangents are octahedral-encoded 16-bit pairs, and texture
  coordinates are 16-bit. This is about a third of the bytes of 32-bit floats, and
  vertex bandwidth is what instanced meshes spend.
- **A substitute for bindless.** Material parameters live in one storage buffer indexed
  by a material id. Textures of a size class live in one texture array indexed by a
  layer id. A draw selects both by an index, so draws of different materials do not
  change bind groups.
- **Bind group frequency.** Group 0 is per frame (camera, sun, time, the sky tables),
  group 1 is per pass (the shadow maps, the snow-mark maps), group 2 is the material
  table and texture arrays, and group 3 is per draw (instance list, bucket offset).
- **Procedural meshes are built on the CPU at load**, from the game's seeded data,
  directly into the shared buffers. Nothing is generated mid-run except the instances'
  positions.

## Authored models

Most of what the game draws is built in code at load (above). A few things are worth
authoring: the rigged, detailed objects the camera sits close to (the skis, the player's
figure, the machines). They come in as **glTF 2.0** and nothing else.

- **Loaded once, into the engine's form** (`engine/assets`, the `assets` skill). The loader
  (cgltf) fills a `Model`: every node a joint, every mesh skinned (a rigid part bound
  whole to its node's joint), the base colours as materials, the clips baked at 60 Hz. glTF's
  frame is mirrored in x into the world's, so a model faces heading 0. Nothing after the
  load reads glTF.
- **Posed on the CPU, skinned on the GPU.** A frame samples a clip (or the game's own pose)
  into the joints' locals, chains them to model space and multiplies the inverse binds into
  a bone palette (`assets/pose.hpp`; no allocation). The `skinning` compute pass
  (`render/skinning.hpp`) moves each vertex by the linear blend of its four joints and packs
  it, in the packed vertex format above, into the mesh's slot in an instance family's vertex
  buffer. Every view that draws the family then reads the posed mesh, so the cull, the LODs
  and the shadow casters need nothing new. Positions are packed into the mesh's REACH (the
  box of every pose) and the mesh's sphere is the reach's, so the cull keeps a figure in
  every pose.
- **Made in Blender, by script.** A model's source is a Blender Python builder run headless
  by `ge asset build`, fed the game's own numbers as a data set (`GE_ASSET_DATA`, compiled into
  the `assets` executable), so a skeleton or a ski's sidecut is stated once, in the code. The
  committed `.glb` is the engine's input, beside a STAMP of the hashes of everything it was
  built from and under a size BUDGET its manifest states (`engine/assets/manifest.hpp`); the
  `assets_check` test fails a model older than its sources or over its budget, and CI never
  runs Blender.

**As built (M8e1).** The loader, posing, the CPU reference and the `skinning` pass, held to
each other within one 16-bit step on the software adapter (`tests/render_skinning_test.cpp`),
and the `models` lab. **M8g** added `ge asset` and the first built model, the skier on the
reference skis (`plugins/skier/assets/skier/`), and **M8e2** its poser (`plugins/skier/app/rig_pose.hpp`: the rig posed from a `SkierState`, then `rig_locals` for the model, ready for the palette and the `skinning` pass) and the model drawn in Fall Line's frame:
the committed `skier.glb` is compiled into the game as bytes (`cmake/EmbedBinary.cmake`), the
figures' family holds one skinned mesh a racer packed into the poser's fixed reach
(`pose_reach`, held by `tests/skier_rig_pose_test.cpp` over the ride scenarios), each racer's
poser keeps its lags between frames (`FrameScene::dt`; `settle` for a still or a jump), his
palette is written before the frame is encoded and the `skinning` pass runs straight after the
sky's tables, so every view that reads the family (the culls, the prepass, the colour pass, the
figures' shadow atlas) reads the posed mesh. Each skinned vertex carries its own **motion**:
the family keeps a skinned LOD's last frame's positions after its vertices (two words a
vertex, `lod_last`; the bind's to start), the `skinning` pass copies the position it replaces
there before writing the new one, and the colour pass reads it (`Lod::motion`) for the
vertex's last place, so a limb that swings between frames is not smeared by TAA; a cut, a still
and a slot left undrawn settle the skin (`settle_skin`), so the next pose moves nothing. The
`taa` lab's `--scenario=turns` chases the skier through his turns: the ghosting against the
converged still falls from 0.45 to 0.35 levels (0.95% to 0.73% of the pixels off) with it.

**The crowd: palette families (M8e2).** Hundreds of figures of one mesh, each in a pose of its
own, are not a skin each: a **palette family** (`create_family`'s `palette_joints`) holds every
instance's palette in a storage buffer of its own, twice (this frame's half and the last
frame's, swapped by each `set_palettes`, both written after a cut). Its LODs (`palette_source`)
keep the bind pose packed into the reach box and, after the family's vertices, each vertex's
four joints as bytes and four weights as 16-bit unorms (three words a vertex). The draw's
vertex stage, shared by the prepass, the colour pass and every caster, poses each vertex by the
linear blend of its instance's palette rows, and by the last frame's rows for its motion, so no
pass runs and nothing is stored a figure: a palette costs 48 bytes a joint a frame to upload.
`pose_palette` is the CPU twin, within 1e-4 m of `skin_mesh`; `tests/render_palette_test.cpp`
draws two limbs in poses of their own as one palette family and through the `skinning` pass and
finds the same depth on every pixel either covers.

## Terrain

A generic **heightfield renderer** in `engine/render`, which a game hands a height
texture and a material function.

- **Geometry clipmap.** Concentric rings of a fixed grid follow the camera, each ring
  twice the spacing of the one inside it. The vertex shader reads the height from the
  height texture, so the mesh itself never changes.
- **Seams without `discard`.** Where a coarse ring meets a fine one, the vertex shader
  morphs the fine ring's outer vertices onto the coarse grid and sinks the coarse
  ring's covered cells under the fine one.
- **Culled on the GPU.** Each ring is split into tiles. A min/max height grid baked at
  load gives each tile a tight box, and the culling compute pass writes one indirect
  draw a ring with only the tiles in view.
- **Normals from the height texture** in the fragment shader, which keeps the vertex
  small and gives per-pixel lighting at every distance.
- **Detail near the camera** (deformed snow) is a separate fine patch whose vertex
  density follows how much the surface is disturbed, so untouched snow does not pay
  for vertices it does not need.
- **The material function** is the game's or the plugin's: the snow, its age, its
  grooves and glitter (§ Where the code goes).
- **Past the map's edge the grid is read mirrored** (height, normals and albedo alike), so
  the ground runs on to the horizon as its own reflection, with no seam at the edge,
  instead of the edge's heights stretched outward. The far ranges replace it with M8.

**As built (M7a)** (`render/terrain.hpp`): eight levels of 4 × 4 tiles of 32 × 32 quads,
level 0 at 1 m a quad, so the clipmap reaches about 8 km from the eye; each level's square
is centred on the eye snapped to twice its spacing, which puts every finer square on its
coarser neighbour's grid. Every tile is an instance of one 16-bit indexed mesh whose
vertices are pulled from the vertex index; a compute pass culls the tiles (boxes from a
min/max pyramid with a texture's mip chain) into one indirect draw. The morph runs by the
vertex's distance from the eye, from 80 % to 95 % of a level's half side, so nothing pops
as the eye moves; a coarse vertex inside the finer square sinks two spacings. The normals
are baked once into an `RGBA16Float` texture by compute. The game hands the albedo as an
sRGB texture over the same grid (Fall Line's: snow, the packed piste, steep rock).

**As built (M8a): the ground's material.** The terrain's colour pass is a material's: a
shader that includes `render/wgsl/terrain_draw.wgsl` (the bindings of groups 1 and 2, the vertex
stage, the overdraw probe and the two halves every material shares: `ground_at`, the pixel's
place, normal, albedo and the sun that reaches it, and `ground_out`, the haze, the motion and the
reactive mask) and defines `fs_main`, with a bind group of its own at group 3
(`TerrainSource::shader`, `material`; `bind_terrain_material`). The prepass, the colour pass and
the probe all run the material's module, so their depths are still the same bits. `terrain.wgsl`
is the default: the albedo lit by the sun and the sky. Fall Line's is the snow's, in
`plugins/mountain/app/snow.wgsl`, over a ground map baked once a map (the packed share, rock by
slope, the piste's direction as its doubled angle): wind swells and grain bent into the normal on
the loose snow, a broken face on rock, the groomer's corduroy (14 cm a rib, worn in patches) and
lanes (5.2 m a swath, each its own shade) across the piste's way, every term faded by the pixel's
own footprint before it aliases; the wrap's blue terminator; the glare, the sun scattered forward,
harder on the piste; and the glitter, at most one crystal a cell of world space flaring as a point
where its facet lines the sun up with the eye, written to the reactive mask so TAA-U keeps it.
On the software adapter at 1280x720 the snow costs the terrain pass about 17 % over the plain
albedo (the `profile` lab's `--snow=0`).

**As built (M8b): the snow marks.** Every skier's skis lay two capsules a frame
(`plugins/skier/app/tracks.hpp`: a stance apart, the outside ski of a turn the deeper, a carve
narrower, a skid wider and shallower, a scuff on the piste and a hand's furrow in loose snow),
kept in two RGBA8 maps in `plugins/mountain/app/trails.hpp`: a coarse one over the whole map
(about 1.5 m a texel) and a fine window round the camera (96 m at 4.7 cm a texel). Each texel holds
the groove's depth and the berm's height, each as the root of its share of the channel's full
scale (so a centimetre's scuff keeps some 25 levels), and the pressed floor. A render pass draws
each capsule as an instanced quad with **max blending**: a groove ridden twice is as deep as the
deeper, and overlapping stamps in one draw cannot race, which a compute pass writing a texture
could not promise without atomics. Narrower than a texel, a stamp is widened and scaled by its
coverage, so the coarse map holds a faint line. The window is not toroidal: when the camera leaves
its middle half it is **redrawn from the stamps' history**, kept on the CPU and filed in 32 m
buckets (a frame's worst CPU cost, 1.7 ms on a window move in `profile`), so a track laid outside
the window comes back as sharp as it was cut. The snow's material reads the fine map inside the
window and the coarse beyond (blended over the window's rim): the slope of berm less depth, taken
across the larger of a texel and the pixel's footprint, bent into the normal (a groove narrower
than a pixel fades rather than shimmers); the sky's light dimmed in a deep trough; the pressed
floor smooth, with no grain, glitter or comb; and no berm inside a groove (the ring round one
stamp's end crosses the next stamp's groove). A pass with nothing to stamp opens an empty compute
pass under its name rather than loading and storing its map. Fall Line's table runs the two,
`trails.fine` then `trails.coarse`, after `terrain.horizon`; the colour pass reads both. On the software adapter at 1280x720
the tracks cost the terrain pass about 6 % and the two stamp passes about 0.5 ms each (the
`profile` lab's `--trails=0`).

The deformation field (the snow standing higher and lower than the ground, moving the vertices)
is not built: the grooves are light, not geometry, and a furrow's silhouette is the case that
would need it.

## Lighting and shadows

**Forward shading with a fixed light list.** The sun or moon, the sky's light (read
from the sky's irradiance table), and up to eight point lamps whose slots are dealt by
distance each frame. With this few lights, forward shading has the lowest bandwidth,
works with MSAA and transparency, and suits tile-based GPUs. **Clustered light
culling** (a compute pass that bins lamps into a froxel grid) is the planned upgrade,
and it is built when a scene measurably needs more than eight lamps.

**Shadows use three techniques, one for each scale:**

1. **The mountain's own shadow: a horizon map.** For each texel of a grid over the
   whole map, a compute pass stores the highest elevation angle the ground reaches
   looking towards the sun's bearing. A point is lit when the sun stands above its
   horizon. The sun's elevation can change with no rebake, and only a change of bearing
   needs a new map. Two maps, one baked behind the sun's bearing and one ahead of it,
   are blended so that a shadow's edge glides with the sun. This covers kilometres of
   ridge shadow for one texture read a pixel, with no shadow acne on grazing slopes.
2. **Everything that stands still: cached cascaded shadow maps.** Two cascades round
   the camera. The far one is redrawn rarely, the near one only when the camera has
   drifted or the sun has turned past a threshold. A cascade that is not redrawn keeps
   its matrix, so its shadows stay exactly where they were. This is what makes the
   shadow pass, which in a naive renderer redraws the scene every frame, cost almost
   nothing in a typical frame.
3. **The figures: a shadow atlas.** Each figure and its vehicle gets a small map of its
   own in one depth atlas that follows it, at millimetres per texel, redrawn every frame.

Filtering is hardware comparison sampling with a small rotated Poisson kernel. A
contact-hardening (PCSS-style) blocker search is a quality-ladder option for the
figures' atlas only.

**As built (M7d2)** (`render/shadows.hpp`, `wgsl/sun_shadow.wgsl`): the cascades and the
atlas are three layers of one `Depth32Float` array (2048 a side in Fall Line): the near cascade
(48 m round the eye), the far one (400 m), and the figures' atlas of 4 × 4 tiles (1.4 m round
each figure, 128 texels across: about a centimetre a texel). Each map is an orthographic view
along the sun, 2 km deep, with depth 1 toward the sun, cleared to 0 so nothing casts; casters
draw with `greater`, and a receiver is lit where its depth is at least the map's. A cascade is
redrawn (`update_cascade`, pure CPU) only when it was never drawn, the eye has drifted a quarter
of its radius across the sun, or the sun has turned 0.25° (near) or 1° (far); its centre is
snapped to its texels across the sun, so a redraw does not swim. Seed 38's whole run from the
chase camera redraws the near cascade on 187 of 14,382 frames and the far one on 22 (the
`world` lab's `cascade-redraws`). The casters are each family's own slots
(`create_family_casters`): the forest's two (one a cascade) culled against the map's box with
no depth pyramid, the figures' and the crowd's one a tile (sixteen), each culled against its own
tile's box, so a figure is drawn only into the tiles it can shade, each tile under its own
viewport and scissor. The tiles go to the racers first, then to the amateurs nearest the eye
(`games/fall_line/app/scene.hpp`'s `shadow_centres`). A receiver (the terrain and every instance) offsets its point 1.5 texels
along its normal, adds a 5 cm bias and takes four rotated Poisson taps, each a hardware 2 × 2
comparison (interleaved gradient noise, turned every frame for the TAA to average); the near
cascade where the point falls inside it, else the far one, and the darkest of the figures'
tiles. Under the horizon nothing is drawn or read. On the software adapter a cascade's redraw
costs 2 to 30 ms (near) and 50 to 85 ms (far), which is why they are cached; the atlas costs
under 3 ms. The `wood` view (`games/fall_line/app/views.cpp`) stands across the sun from the
sunlit edge of a wood, where the trees' shadows lie on the snow.

**As built (M7b)** (`render/horizon.hpp`): the horizon map. A turn is split into 180 bearings
(2° each); the maps for the two bearings either side of the sun's sit in two `RGBA16Float`
textures, each bearing in the slot of its index's parity, so a step of the sun into the next
interval rebakes one map and leaves the other's bits alone. A map's texel is every second
sample of the field (Fall Line: 751 × 751 at 4 m over its 3 km map); the bake marches 96
samples toward the bearing, growing geometrically from one texel to 8 km, reads the field's
bilinear heights mirrored past the edge (as the terrain draws them), takes the planet's
curvature off each sample and keeps the highest tangent of elevation. The terrain's
fragment shader (`ground_shadow`, in `wgsl/terrain_common.wgsl`, which the instanced draws read too) turns each map into a soft step where the sun's tangent
crosses it (a penumbra of 0.008, the sun's disc and the grid's interpolation) and blends the
two by where the sun lies between their bearings. A bake is a whole map in one dispatch, in
its own `terrain.horizon` compute pass, only on a frame that needs one: two at load, then one
each time the sun crosses a 2° step (on seed 38, 69 bakes from 7 to 17 h, about one every
nine minutes of the day; the `shadows` lab counts them). On the software adapter a bake takes about 0.4 s; real-GPU
timings, and slicing a bake over frames if one shows as a hitch, wait for the `profile` lab
(M7d3). Past the map's edge the ground is drawn mirrored and so are its maps: the shadows
there fall at the mirrored bearing, which only the distant ground shows until the far
ranges replace it (M8).

## Sky, atmosphere and clouds

- **A physically based sky from lookup tables** (the scheme of Hillaire, 2020), all
  built by compute passes: the transmittance and multiple-scattering tables once at
  load, the sky-view table when the sun moves more than a threshold, and the
  **aerial-perspective volume** (a low-resolution froxel grid of in-scattered light and
  transmittance) every frame for the camera.
- **Haze is one function.** Every world material applies the aerial-perspective volume
  through a shared WGSL function. No material has its own fog.
- **Clouds are a raymarched volume at a quarter of the pixels.** Each frame marches a
  sixteenth of them, picked from a blue-noise sequence, and reprojects the rest from
  the previous frame, clamped by the fresh samples round each pixel. On a camera cut the
  whole buffer is marched again. The noise volumes are built by compute at load.
- **The night sky** (moon, stars, the galaxy band) is drawn into the sky's pass.

**As built (M7a)** (`render/sky.hpp`, `render/wgsl/atmosphere.wgsl`): an Earth-like air
(Rayleigh, Mie, an ozone layer) over a planet of 6360 km, the eye's altitude from the map's
own (`FrameView::altitude_m`). Tables in `RGBA16Float`: transmittance 256 × 64 and multiple
scattering 32 × 32 once; the sky view 192 × 108 (rows crowded at the horizon, columns round
the sun) and the **sky's light** (the irradiance an up-facing surface gets from the sky, and
the sun's transmittance at the eye, in a small storage buffer) when the sun turns past
half a degree or the eye moves 50 m; the aerial volume 32³ over the screen, its slices
spaced as the square of their index out to 8 km, every frame. Every luminance is per unit
of the sun's illuminance (`FrameView::sun_illuminance` scales them all). The sky pass is a
full-screen triangle at the far plane tested `equal`, with the sun's disc. `frame.wgsl`
holds the one copy of the haze, the sun's light at a point and the sky's irradiance.

**Sky types (M8f7)** (`render/sky_style.hpp`, `FrameView::style`): a world that is not the Earth
changes its AIR (scales on the Rayleigh scattering per channel, the dust's scattering,
absorption per channel, asymmetry and scale height, and the ozone), which `medium_at` reads, so
the tables are rebuilt in it and everything lit by the sky follows (the sky, the sun's colour,
the sky's light, the haze, the cloud's light). Over the sky the pass draws what an air cannot:
the sun's disc's size and tint, a second sun with a halo, a planet lit as a ball (its phase
fixed by its offset from the sun, banded, a ring whose near half crosses it, dimming half the
sky behind it), a nebula on the star sphere and the stars by day (both against the snow's
white, not physical), and a glow from under the horizon. Ten named types (`sky_types`) a game
picks by a word, knobs over them (`SkyKnobs`). The Earth's style is every scale 1 and every
extra 0, so its picture is the same to the bit.

**The weather in the picture (M8f1).** A frame is seen under a `Weather` (`render/view.hpp`,
in the frame block as `weather` and `deck`), and `wgsl/frame.wgsl` is the one place it acts, so
the ground, the trees, the figures, the particles, the flakes and the sky all see the same day:

- the cloud takes its share off the sun's direct light (`sun_light`), so the sun's shadows, the
  snow's glare and the particles' forward lobe fade with it; the glitter, the sun's disc in a
  crystal, goes faster (the pass squared);
- a DECK covers its share of the dome (`sky_luminance`): its grey at the horizon is the clear
  day's light on a level face over pi (so it follows the hour and the night) times the deck's
  tint and light, darker overhead, a pale patch where the sun is behind it; the sun's disc is
  lost behind it;
- the sky's light on a surface becomes the deck's (`sky_irradiance`: pi times its grey, the
  overhead share weighed as a level face sees it), with no direction but up: THE FLAT LIGHT;
- the air under a deck scatters the deck's grey, not the sun's (`haze`), and a FOG or a FALL
  closes every surface into its VEIL at its visual range V (`exp(-3.912 d / V)`: 2 % of a black
  target's contrast left at V). Droplets and flakes are far larger than light's wavelengths and
  scatter every colour alike, so the veil is grey: a white face's light under the sun and sky,
  dimmed to 0.85 (`veil_luminance`), never the clear air's blue. The sky is seen through a slab of
  it 800 m deep over the eye (`veil_sky`), so through a thick fall the blue overhead is lost too,
  and toward the horizon all of it;
- the exposure keys a white face under a deck to 0.6 rather than 0.78, so a grey day's snow is a
  light grey, not a white-out.

A clear `Weather` (the default) changes no bit of the picture. What the mountain's eight skies
are in these numbers is `plugins/mountain/app/weather_look.hpp`, and Fall Line's frame reads the
map's sky at the view's time (`FrameOptions::weather`). The `weather` lab draws all eight: from
seed 38's piste the clear day's picture spreads 37 levels of luma, an overcast's 15, a snow's 3
and a storm's 2. The cloud volume itself is M8f3 (below).

**The cloud in the picture (M8f3).** A frame is seen under up to three cloud layers
(`render/view.hpp`'s `Clouds`, in the frame block as `cloud_info` and `clouds`), drawn by
`render/clouds.hpp`:

- THE SHAPE is one tileable noise, a 64³ value-noise fbm made on the CPU (the same bytes on
  every machine; the frame's group 0 binds it with a repeating sampler beside the sky's tables).
  A layer reads it flat at its cell's scale for its COLUMN, the third axis its age, and stands
  where the noise passes the cut its cover sets: `cloud_cut` is the noise's own quantile, so the
  cover is a true share of the sky (the `weather` lab measures fair's dealt 0.29 as 0.28). Within
  its slab a HEAP grows from a flat base to a top the fuller the column the taller, a SHEET
  thickens about its middle, and two more reads at a quarter and an eighth of the cell eat its
  edge, harder toward the top.
- THE WIND carries each layer along its heading (the drift, kept within the noise's tile in
  double) and its shape changes as it goes (the age).
- THE LIGHT on a point of cloud is the key light through the air to its height, through the
  layers above (their shadow at its middle) and through its own cloud toward the light (four
  reads at growing steps): Beer's law for the light that has not scattered, and for the light
  scattered on (forward, so it dies at the reduced extinction σ(1 − g), g ≈ 0.85) one wide lobe
  standing for its many orders, so a thick heap is as bright as snow in the light; a powder term
  darkens a thin edge seen with the light behind the eye; the sky's light from above and the
  snow's from below. Far cloud is hazed at its mean distance and melts into the sky from 25 to
  55 km.
- THE SHADOW on everything lit (`cloud_shadow` in `wgsl/frame.wgsl`, read by `sun_light(y, xz)`)
  is each layer's column where the beam toward the key light crosses its middle, taking
  exp(−column × the layer's shadow depth) off the light: each place its own share of the sun.
- THE PASSES ("clouds.march" and "clouds", after the sky) march each ray, nearest layer first,
  up to what the depth target holds (28 steps through a heap, 10 through a sheet, a new offset
  every frame for TAA to resolve), and lay the cloud over the HDR target premultiplied. By
  default the march runs at A QUARTER OF THE PIXELS (`Clouds::quarter`): a full-screen triangle
  over a half-size target marches each two by two's ray to the FARTHEST of its four depths, so
  the cloud behind a ridge reaches the ridge's edge, and keeps the distance it marched to beside
  it (R32F). Then each pixel of the frame takes the four half-size pixels round it, each weighted
  by its share of the bilinear footprint and by exp(−40 × how far its distance misses the
  pixel's own, as a share of the nearer); a pixel that none of the four matches within 10 %
  (the near side of an edge) marches its own ray. Marching every pixel (the quality ladder's
  top) draws "clouds" alone and leaves "clouds.march" empty; with no layer both are empty. The
  ladder also takes a share of the steps (`Clouds::steps`, never under four) or draws no layer
  at all (§ The quality ladder). The `weather` lab holds the quarter to every pixel's march:
  under 1 % of the picture may differ by more than 8 levels of luma (`<sky>.quarter-misses`).
  On lavapipe at 1280 × 720, from seed 38's start looking up into a flurry's sky, every pixel's
  march costs 72 ms and the quarter's 36 (28 at half the steps): a CPU raster's numbers, where
  a full-screen pass costs about 13 ms whatever it does, so a GPU's saving is nearer the
  pixels' four to one.
- THE MARCH SPREAD OVER FRAMES (`Clouds::spread`, on by default, and only with the last frame's
  camera: `cloud_info.z`): each frame one of every two by two of the half-size pixels marches,
  in turn over four frames, and the other three take the last frame's half-size picture. The
  marching pixels have a pass of their own ("clouds.fresh", over a target a quarter of the
  half-size one), because a GPU runs a group of neighbouring pixels as long as its slowest: one
  marching pixel in every two by two of one pass makes every group march, and the first build,
  which did that, timed no cheaper than the whole march. The half-size pass ("clouds.march")
  then takes the fresh pixel where it is its turn, and elsewhere reprojects: its ray's point at
  the distance its cloud lies at (first guessed at the nearest layer's middle, then read back
  from the last frame and reprojected again) is carried through the last frame's camera, and
  the last frame's picture there is read bilinear over the taps that were written the frame
  before (a stamp, 1..4096, beside each) and marched to the same scene within 10 %; a pixel
  those taps hold under half of (off the screen, newly seen past a ridge) marches after all.
  The two half-size sets (RGBA16F cloud, RGBA32F distances and stamp) swap each frame. The
  `weather` lab holds the spread to every pixel's march as it holds the quarter, with the view
  swinging 30 degrees a second (`--turn=30`): at 1280 × 720 looking up into the sky, 0.2 % of
  the fair sky's picture and 0.27 % of the flurry's differ by more than 8 levels (0.01 % and
  0.27 % with the march whole every frame), the edges of the heaps and a pixel's dither. On
  lavapipe the half-size march falls from 12 to 16 ms to 3.4 to 4.8 for the fresh pass and 4.8
  to 5.6 for the half-size one, and the cloud's three passes from 22 to 26 ms to 17 to 19 (the
  full-size upsample, about 8, is the same either way); on a GPU, whose pass has no such floor,
  the march is nearer a quarter of its cost. In one ladder pricing run the clouds' high stop
  fell from 63 % of every pixel's march to 42 %.

What the mountain's broken skies stand is `plugins/mountain/app/cloud_field.hpp`: FAIR heaps at
1.5 to 2 km over the valley floor and a few ice streaks at 8 km, FLURRIES' taller heaps under a
rippled sheet at 4 km, a HIGH sky's streaks over a veil; every band dealt off the map's seed,
the layers carried at a quarter of the wind aloft (the map's mean wind grown and turned with
height) and living their kind's life. Where they are drawn a broken sky's mean cloud
(`weather_look`'s pass and cover) is given back, since the layers cast their own shadows; a
lid keeps its deck.

**The night in the picture (M8f2).** A frame's `Night` (`render/view.hpp`, filled by
`render/night.hpp`'s `set_night` once the game has placed the sun) carries the moon and the dark
sky; `plugins/mountain/app/night_look.hpp` places the map's moon on its date (a day of the year
read in 2026, longitude 0) by `core/moon.hpp` (M8f6):

- ONE UNIT OF LIGHT is the sun's illuminance outside the air (128 000 lx), and a night is a
  millionth of a day, under a half-float's smallest step. The frame stores its light through a
  PRE-EXPOSURE (`pre_exposure`: the estimated light on a white face brought to about a quarter
  of a unit, in `world.y`), 1 whenever the sun is up so a day's bits never move; the exposure
  divides it back out, and the eye's adaptation and a star's light read through it;
- THE MOON'S PLACE (M8f6, `core/moon.hpp`) is a truncated lunar theory (sixty terms in
  longitude and distance, sixty in latitude), the time's step from the universal hour, the
  nutation, the apparent sidereal time, the parallax of a place on the surface and the
  refraction: within 0.007 degrees of a reference ephemeris from 1990 to 2049. It gives the
  phase, the libration and the axis's tilt (the IAU's pole and prime meridian precessed to the
  date) and the Earth's shadow (its axis and the umbra's and penumbra's radii at the moon);
- the moon's light follows the phase law (a half moon a tenth of a full one) and the square of
  its distance; its disc is drawn at its true size by Lommel-Seeliger's law, scaled to that
  light (`surface_surge`), over THE REAL FACE (`render/moon.hpp`: a public-domain orbiter
  mosaic's albedo, 1024 x 512 with its mips, read at the level the disc's size asks), turned by
  the face's pole and prime meridian, with an earthlit dark part; a faint part beside a bright
  one is drawn as the eye sees it (the earthshine's ratio to the lit limb to the power 0.35,
  never above the earthshine alone), so a day moon is a pale disc in the blue and a twilight
  crescent shows its ashen light;
- AN ECLIPSE: the Earth's shadow on the disc, the penumbra by the share of the sun's disc it
  leaves and the umbra the air's refracted red (a thousandth of the sun's light at its edge, a
  ten-thousandth on its axis), its contrast against the disc's brightest point to the power
  0.5; the moonlight on the snow is dimmed by the disc's mean (`eclipse_of`, on the CPU);
- THE KEY LIGHT, the one direct light that casts shadows, is the sun until three degrees under
  the horizon, then the moon when it is up (`FrameView::sun_*` become the moon's; the real sun
  stays in `night_sun`). The sky is TWO tables at once: the sun's own sky view and a second at
  the moon's height lit by its light (`moon_view_lut`, built only at night when the moon turns
  or the eye moves), summed in `sky_luminance` and the sky's light, so the dusk's glow fades
  under the moon's sky and the handover moves the picture by a thousandth. The aerial volume is
  built for the brighter of the two skies and the other is laid over as a veil; while the sun
  still keys, the moon's direct light is spread from above (`world.z`);
- the dark sky's own glow (2e-4 cd/m2 at the zenith, brighter toward the horizon) lights the
  snow without a moon; the stars are the REAL ones (M8f5, `render/stars.hpp`: a bright-star
  catalogue's 9,096, each at its place, magnitude and B-V colour, baked into the engine as a
  generated table and cut into a cube map of 6 × 160 × 160 cells of up to three stars, the sky
  pass reading the pixel's cell and the eight round it; 19 stars of tight clusters are dropped,
  none brighter than the fifth magnitude), drawn down to the limiting magnitude the sky's
  brightness leaves (none in the day, thousands on a moonless night, few under a full moon),
  turned by the sidereal angle about the pole at the map's latitude, so the constellations stand
  where the map's place and hour put them (the catalogue's epoch's places: the precession since,
  under half a degree, is not applied), with the galaxy's band;
- THE EYE: under 14 lx on white the exposure adapts only partly (dark scenes stay dark), and
  the rods' share desaturates the picture toward a blue tint (`night_vision`, in post before
  the exposure). The share is the scene's or the pixel's own, whichever is less, so a bright
  star keeps its tint; the moon's disc, drawn far dimmer than it is but always bright enough
  for the cones (an eclipse's umbra near 1 cd/m2), keeps its own colour: the sky pass gives the
  sky in front of it to the rods and post leaves the disc's pixels alone (`on_moon`), so a full
  moon is a warm white and an eclipsed one copper red over a blue-grey night.

The `night` lab draws it on seed 38's vista: the snow's luma falls from 194 at three degrees
under to 67 at fourteen, a full moon's snow is 71, a quarter's 28 and a new moon's 10, the
handover moves the snow by 0.1 %, a moonless sky draws 319 star pixels, and the sky tables'
twilight matches `twilight_lux` within 1 % down to twelve degrees under.

## Anti-aliasing and resolution

**Temporal anti-aliasing with upscaling (TAA-U)** is the default. Each frame's
projection is jittered on a Halton sequence, a motion-vector target is written in the
depth prepass and the opaque pass, and the resolve pass blends the jittered frame into
a history at output resolution. The history is rejected by clamping it to the current
frame's neighbourhood in YCoCg space, and a reactive mask from particles and the snow's
glitter keeps them from smearing. Rendering at 50–100% of the output size, with this
pass reconstructing the rest, is the largest single lever on GPU time. It also removes
the shimmer of thin branches and glitter, which no amount of MSAA fixes.

The resolve is our own compact pass, written for this pass table. If it falls short of
the look, the open FSR 2 algorithm (MIT licence) is the reference to port, measured
against ours in the lab.

**As built (M7c)** (`render/taa.hpp`): the jitter is the Halton (2, 3) cycle, centred, of
8 points at full size and `ceil(8 / scale²)` (at most 32) below it, applied to the projection
as a sub-pixel shift (`fill_uniforms`'s `jitter_px`). The resolve is one compute pass at the
output's size. Each output pixel reconstructs the frame from the 3 × 3 render pixels nearest
it, each weighed by a Gaussian of its sample's distance with the jitter undone and by the
inverse of its exposed brightness (`frame.wgsl`'s `exposure`, now shared with post); it takes
the motion at the nearest depth of the 3 × 3, samples the history Catmull-Rom (five bilinear
taps) and clips it into the YCoCg box of mean ± 1.25 σ; it blends 10 % of the new frame,
more where the reactive mask asks, all of it off the screen or after a reset, and while the
history is young a plain running mean, so a still converges in one cycle. Two histories
ping-pong; post reads the one written. On a cut the game calls `reset_taa` and drops its
`FrameHistory` (`fall_line::app::cut`). The `taa` lab flies a camera and measures the moving
frame against the converged still (on seed 38's vista at 40 m/s turning 20°/s: 0.45 levels
mean, 0.9 % of pixels off by more than 8, thin lines along moving ridges) and a single
jittered frame against it (0.78 levels, 2.3 %). At 50 % scale the `world` lab's views are
hard to tell from full size by eye; what each scale costs on a real GPU waits for `profile`.

**4× MSAA with no temporal pass** is the fallback for the lowest tier, where a phone's
tile memory resolves MSAA almost for free.

## Post

One full-screen fragment pass reads the resolved HDR frame and writes the swapchain image
(a fragment pass, not a compute one: a compute shader may not store to the surface's
`BGRA8Unorm` without an optional feature, and a tile GPU keeps a full-screen fragment pass
in tile memory):

- exposure from the sky's brightness, not from a histogram, so it never pumps;
- bloom from a half-resolution mip chain (dual-filter down and up), cheap and stable;
- the colour grade as a 32³ 3D lookup table the game bakes from its look;
- tone mapping, a blue-noise dither against banding, and the output transfer function.

**As built (M7a)** (`render/post.hpp`): the exposure maps a white Lambertian surface under
the sun and the sky (read from the sky's light) to 0.78; the tone curve is the Khronos PBR
neutral operator (linear to 0.76, a smooth shoulder above, so snow keeps its colour); the
dither is interleaved gradient noise until the blue-noise texture lands; the sRGB encoding
is an `override` constant off for an `*Srgb` output.

**As built (M7d3)** (`render/bloom.hpp`, `render/post.hpp`). **Bloom** is a compute pass
(`bloom`) over a chain of half-size `RGBA16Float` mips of what the post pass reads (six levels
in Fall Line: half the output's size down to a 64th). Down, each level is the 13-tap filter of
the one above (four overlapping 2 × 2 boxes and a centre one, each a bilinear read); the first
level weighs each box by 1 / (1 + its brightness), so a lone glint cannot flicker into a blob.
Up, each level adds the level below through a 3 × 3 tent to its own down level, and alpha
counts the levels summed. The post pass mixes `strength` of the average in (Fall Line: 0.04):
nothing is thresholded, so the mix conserves energy (a flat field keeps its value; the test
holds it to one 8-bit level) and only what is much brighter than its surroundings visibly
glows. **The grade** is a 32³ `RGBA16Float` lookup table read trilinearly at the tone-mapped,
sRGB-encoded colour, so its steps are spent where the eye judges colour; it starts as the
identity, and the game bakes its own (`fall_line::app::bake_grade`: a little contrast
through the middle tones with the ends held, cooler shadows, warmer highlights, 5 % more
saturation). On the software adapter at 1280 × 720 the bloom costs about 11 ms and the post
pass 7 ms (the `profile` lab); the bloom's cost on a real GPU waits for the Mac's numbers.

Linear light is used throughout, and the conversion happens here, once.

## The 2D pass

Menus, the HUD and the console are drawn by the 2D layer (`engine/ui`, `docs/architecture.md`):
an immediate-mode draw list of rounded boxes and text runs, which a CPU raster pictures headless.
The GPU draws the same list in one pass after post, blended over the display image.

**As built (M22b)** (`render/ui_pass.hpp`, `ui_pass.wgsl`):

- **One instanced draw.** Each box is one quad, and its rounded rectangle's signed distance,
  paint (solid, or a linear or radial gradient of up to eight stops), image and border are
  worked out in the fragment shader. Each glyph is one quad that reads a glyph atlas. Every
  instance is in one storage buffer and every paint in another. The draw splits only where a
  box's image changes, because each image has its own small bind group. A menu page is one draw.
- **The glyph atlas** is a 1024² `R8Unorm` texture packed in shelves. A glyph's mask is copied
  in the first time a list draws it, and kept under the font's cached mask. A full atlas is
  emptied and refilled from the list being prepared.
- **Images** are uploaded the first time a box names them. A texture the GPU already has, such
  as the live frame under a menu, is bound to a key with `ui_texture`.
- **The CPU raster's pixels.** The pass uses the same signed distance and coverage at the
  pixel's centre, the same clip rule, and text set by the same `ui::place_glyphs` from the same
  masks. It samples images bilinearly at the same texel centres. It blends straight
  sRGB-encoded colours, folding a fill and its border into one premultiplied colour, so the
  output must be a UNORM target: an `*Srgb` one would blend in linear light. Gradients get half
  a level of dither against banding.
- **Measured.** The `menu-gpu` lab holds every page of the `menu` lab to the raster: on lavapipe,
  none is more than 3 levels off at any pixel. Preparing a 1280 × 720 page costs about 1.7 ms of
  CPU, and the software adapter draws it in 19 ms. The real GPU's cost waits for the Mac.

## Particles and figures

- **Particles are simulated in compute** in fixed pools: emission appends to a ring, the
  simulation integrates and kills, and a compaction step writes the indirect draw count.
  They are drawn as camera-facing quads, softened against the depth buffer. Sorting is
  avoided by blend choice where possible; where a pool needs it, a bitonic sort in
  compute orders it.

**As built (M8d1).** `engine/render/particles.hpp` is one pool of 16384 slots of 64 bytes, a
ring: the game hands each frame's births (`emit_particles`) and `prepare_particles` writes
them into the next slots, so a full pool lets its oldest go first. There is no compaction or
indirect count yet: `particles.sim` (one thread a slot) skips a dead slot, and `particles`
draws one quad a slot with a dead one folded away in the vertex stage. The flight is gravity
times the particle's `fall` (1 a grain, about a tenth a fine that drifts), an exponential drag
toward the still air, and the ground off the terrain's heights: a grain coming down on the
snow is gone, a fine one settles and fades, and one under the snow and rising flies on (a ski
sinks into soft snow, and what it throws starts there). `step_particle` is the flight's CPU
twin, and `tests/render_particles_test.cpp` holds the pass to it. A particle is drawn as a
streak along its motion over `streak` s (a camera's shutter sees spray as strokes), thinned by
its length, faded in and out over its life, softened over `soft_m` against the depth, lit as
the instances are (the sun past the horizon and the shadow maps, the sky) with a forward lobe
toward the sun, hazed, blended premultiplied and unsorted, and it writes the motion target's
reactive channel so TAA does not smear it. What fills the pool is the game's: Fall Line
throws each racer's spray (`plugins/skier/app/spray.hpp`) over the time between two frames'
`time_s`. On the software adapter the pass pair costs about 0.6 + 0.13 ms (the `profile`
lab), the flight of all 16384 slots the larger half.

**As built (M8d2).** The drag pulls a particle toward the AIR, not still air: the frame hands the
pool the map's wind at the view's time (`plugins/mountain/air/air.hpp`), so a powder cloud (the
spray's big thin puffs, hanging three to five seconds) drifts off downwind. THE FALLING SNOW is
`engine/render/snowfall.hpp`: 24576 flakes in a 36 m cube centred on the eye, no births and no
deaths. `snowfall.sim` moves each at the wind plus its own fall (0.6 to 1.4 m/s by its index)
and a flutter on its phase, and wraps its offset from the eye back into the cube, so the eye
always stands in a full fall however fast it moves. `snowfall` draws the first `fall` share of
them (the air's fall at the view's time: a flurry a scatter, a storm the whole box) as streaks
over a 1/60 s shutter, at least a pixel and a half wide (a far flake widened and thinned as much,
so it keeps its light and does not shimmer), thinned by the root of the streak's length, faded
toward the cube's faces and at the lens, lit by the sun past the horizon and the sky, hazed and
softened against the depth. When nothing falls the pass is an empty compute pass under its name
(an empty render pass still loads and stores its targets). The flakes are lit as the day is: under
a clear sky that is a sunny fall; under a lid they are lit as its flat grey (M8f1, above).
- **Skinning** uses bone palettes in a storage buffer. A figure drawn in more than one
  view (the player in the main view, the sun's cascade and the shadow atlas) is skinned
  once by a compute pass into a vertex buffer that every view reads. Crowd members,
  drawn at lower detail and in larger numbers, are skinned in the vertex shader from
  their instance's palette.

**As built (M7d1, then M8e2).** Fall Line draws every racer as the skier model
(`plugins/skier/assets/skier/`), one skinned mesh a racer in one instance family
(`games/fall_line/app/scene.hpp`), posed every frame from his `SkierState` by
`plugins/skier/app/rig_pose.hpp` and placed at his figure frame (`skier/app/figure.hpp`'s
`figure_frame`), a jacket colour a racer; a cut writes the instances fresh and settles every
poser, so no motion is carried across it. The **placeholder figure** (`figure.hpp`'s 17 rigid
pieces, each a unit box, cylinder or ball posed from the `SkierState`) is no longer drawn; it
stays as the `anim` lab's comparison. The free ride's **crowd** (`sim/crowd.hpp`) is the same
model as one palette family (`scene.hpp`'s `crowd_meshes`, `crowd_instance`): an instance an
amateur at his id, each posed by his own poser from the amateur read as a skier (`crowd_skier`:
his place, heading, lean, crouch, down), sized by his body and in one of ten jackets; one on a
lift's carrier or up a lift unseen is drawn at a scale of nought, which the cull drops. The
amateurs nearest the eye cast into the figures' atlas, a tile each after the racers'; each tile
culls the crowd by its own box, so the atlas costs the figures near the tiles, not the crowd
times the tiles (on the software adapter, 2 ms of `shadows.figures` with one racer, 12 ms with
fifteen amateurs; drawing every figure into every tile had cost 385 ms). The crowd draws the
model at three **levels of detail** (`scene.hpp`'s `skier_lods`: the meshes `skier`,
`skier.lod1` and `skier.lod2` the builder makes off the same rig, 1808, 698 and 344 triangles)
by its radius on screen (`kSkierLodPx`: LOD 0 down to 40 px, LOD 1 to 16 px), the heights the
`model-views` lab's `lods` sheet shows each level keeping its silhouette at; the far crowd's
vertex stage, the most of its cost, fell from about 22 ms to 9 ms of `instances` on the software
adapter (crowd-look, three runs each, the medians). An atlas tile's casters pick their LOD at a
quarter of the tile's texels a metre (`ShadowSettings::figure_lod_share`), since a shadow needs
the silhouette, not the folds. The `crowd-look` lab
films a lift's queue, a busy run, a group and one of them down at `--hour` (11: the sun on the
runs), each with the crowd and without.

## Shaders

- **WGSL**, one file per pass, beside the pass's C++. A small build step resolves
  `#include` lines and embeds the result as a string. The engine never reads a shader
  file at run time.
- **Validated at build time.** `make shaders-check` compiles every WGSL file with Tint
  offline, so a shader error fails CI with a file and line, without a GPU.
- **Few permutations.** Quality switches are WGSL `override` constants set at pipeline
  creation, not text variants of a shader. Every combination the game uses is in the
  pipeline manifest.
- **One shared library** of WGSL functions for what many passes need: the haze, the
  sun's light, shadow lookups, octahedral decoding, blue noise. Each function exists
  once.

## The quality ladder

The picture's settings are one table of ROWS in the game (Fall Line's:
`games/fall_line/app/ladder.hpp`), each a ladder of STOPS, cheapest first. A stop maps to the
frame's parameters (`FrameOptions`: the render scale, the shadow maps' size, the trail maps'
texels, the flakes, the cloud march's share of its steps, whether it marches a quarter of the
pixels and whether that march is spread over frames, the picture's reach), never to a code path
of its own. Each row is its
own bill, because a machine can be short of pixels and rich in passes, or the other way round.

- **The price table.** Every stop carries a COST, the ms it adds to a frame over its row's
  cheapest on the table's reference machine, and a WORTH, what it adds to the look on one
  0–100 scale for every row (a judgement, argued beside the row in `ladder.cpp`). The `ladder`
  lab's `--price` fills the costs: the top picture, every row lowered alone to each of its stops,
  and the floor (every row at its cheapest), each timed under a flurry (the dearest sky: heaps
  under a sheet, and the fall) from seed 38's start looking 15 degrees up, so the sky fills the
  top of the frame and every row draws something (`--view=chase` times the camera behind him,
  where the clouds' march is a sliver of the frame). On a software adapter each picture is timed
  in rounds, keeping its least median.
- **The fit** (`render/ladder.hpp`, generic). `fit_ladder` takes the picture of most worth whose
  cost fits the budget over the floor: a knapsack with a choice in each row, solved exactly in
  hundredths of a millisecond. A greedy walk would hide a dear stop that matters behind a cheap
  one that does not, and could hand a slower machine more of the look than a faster one. A row
  marked to go first (Fall Line's RESOLUTION, since a softer picture loses nothing in it) steps
  down before any other row loses a stop.
- **One machine is the reference scaled.** `machine_scale` reads a machine as the reference made
  faster or slower: its timed frame at the picture it drew, over what the table says that
  picture costs. The automatic preset (`auto_picture`) fits, applies, times again and fits again,
  so the second reading is of the neighbourhood the answer lies in.
- **The presets** (low, medium, high, top) are fixed picks a player can choose by name; the
  automatic preset is the fit at the display's budget.

The table's costs today are the software adapter's (lavapipe at 1280 × 720), whose ratios are a
CPU raster's: the reference machine's prices (the Mac, § Where it is developed) replace them.
- **The reach and its mist.** `FrameView::reach_m` is how far a picture draws. The ground's
  clipmap draws only the levels that cover it (`clipmap_levels_to`: 1, 2 or 4 km round the eye
  where all eight reach 8 km), the instances' cull drops every one past it (counted with the
  frustum's), and the haze closes every surface but the cloud into the sky behind its pixel from
  half the reach to all of it (`haze` in `wgsl/frame.wgsl`; the cloud's own colour goes through
  `haze_air`, the air alone, since it is sky). While there is a reach the sky under the horizon is
  drawn as the horizon's, so the land past it is lost in a bright haze, never seen as an edge or a
  dark sea beyond it. Fall Line's DISTANCE row is priced where it is seen, from over the summit
  on a clear day (`--view=vista --lift=0 --fall=0`): there the woods past 2 km are most of the
  frame's cost on the software adapter.

The table's costs today are the software adapter's (lavapipe at 1280 × 720), whose ratios are a
CPU raster's: the reference machine's prices (the Mac, § Where it is developed) replace them.
What the ladder does not have yet: the cloud's march spread over frames with reprojection, and
changing a stop without making the frame again (the reach already can: it is a field of the
view, not of the frame).

## Measurement

- Every pass in the table is wrapped in a timestamp query pair, so GPU time per pass is
  always available when the adapter supports it.
- Each frame keeps counters: passes, draws, dispatches, culled and drawn instances
  (read back from the indirect buffers a frame late), bytes uploaded, and pipelines
  created. The developer UI and the lab tests read the same struct.
- Each pass's CPU time is kept beside its GPU time: the recorder times the encoding from one
  pass's open to the next's, and the prepare before the first pass (`FrameStats::prepare_ms`).
  The prepare's own parts are timing zones (`engine/prof`, `GE_ZONE`), which the game's
  `profile` lab folds per frame with the simulation's zones between frames; `ge profile
  --render` runs it.
- **Budgets.** On the reference desktop GPU (the M3 Max), the top of the picture ladder
  at 1440p output fits in 8.3 ms of GPU (120 Hz). On a recent phone or tablet, the
  middle of the ladder fits in 12 ms of GPU, leaving thermal headroom under a 16.6 ms
  (60 Hz) frame. The renderer's CPU half
  (every `prepare` and `encode`) stays under 1 ms on one core, whatever the instance
  counts. The `perf` skill says how a claim against these budgets is proven.

## Where it is developed

**The main development environment is a Linux container with no GPU.** Every pass must
be buildable, runnable and verifiable by screenshot there. A secondary environment is
an Apple M3 Max laptop on macOS (Metal), used only for what the container cannot do.

**In the Linux container:**

- Dawn draws on a **software Vulkan adapter: Mesa's lavapipe** (it names itself
  `llvmpipe`), from Ubuntu 24.04's `mesa-vulkan-drivers` package, the same package the
  development container and CI's Linux x64 rows install, so a golden image made in one
  matches the other. Lavapipe was chosen over SwiftShader because it is a system package
  (no extra build of Dawn) and has the optional features the tiers test (timestamps,
  subgroups, f16). `gpu::create_device` prints the adapter a run used
  (`gpu::adapter_line`), and the golden tests compare pixels only on it.
- Software rendering is one to two orders of magnitude slower than a GPU, and the
  container has a few cores. Labs therefore render at a small default size (640×360),
  with the fewest settle frames that make a still repeatable, and a lab can name the
  views and passes it needs, so it does not draw the whole frame for one target.
- Every feature tier's fallback path runs here, since a software adapter lacks some
  optional features. A lab can also force a feature off, so both paths stay covered.
- **What it can verify:** that a pass builds and validates, every pipeline in the
  manifest, every named target, and the picture (by capture and golden comparison). It
  can count draws, instances and triangles, and check that nothing is created or
  allocated mid-run.
- **What it cannot verify:** speed. GPU milliseconds from a software adapter say
  nothing about a real GPU, so no performance claim is made from them.

**What needs the Mac**, and nothing else:

| Task | Why the container cannot do it |
| --- | --- |
| A GPU performance claim: per-pass ms, the frame budget, the quality ladder's price table, the automatic preset | Software timings do not predict a real GPU |
| A bug that shows only on Metal, or only on a real GPU (precision, filtering, driver behaviour) | The container has neither |
| The final look sign-off at full resolution and the top of the ladder, where a software render is too slow to iterate | Practical only on real hardware |
| Running the desktop app interactively (input feel, vsync, frame pacing) | No display, no GPU |
| Building, signing and running the iOS, iPadOS and Mac App Store builds on hardware | Needs Xcode and Apple devices (CI's macOS runner can build, not run on a device) |

When a task needs the Mac, the agent says so to the person, and opens a GitHub issue
labelled `needs-mac` with the exact commands to run, what to look at, and what to paste
back. Speed alone is not a reason: work that is only slower in the container stays in
the container. A check that needs a physical phone or tablet (Android or iOS) is
handled the same way, with an issue labelled `needs-device`.

## Lab hooks

The renderer is built to be photographed by the engine's labs. The labs own the
contract (scenes, captions, sheets). The renderer's half is:

- **Offscreen frames.** A frame can render into an offscreen target of a given size,
  with no window and no surface, so a lab or a test runs headless.
- **Named targets.** Every target in the pass table has a name (`final`, `depth`,
  `motion`, `shadow.atlas`, `sky.view`, `trails.fine`, …). `capture(name)` reads one back
  to the CPU after the frame, and a debug view can show any of them on screen.
- **Repeatable pictures.** The renderer reads time only from the `FrameView`, never a
  clock. TAA's jitter is indexed by frame number. `reset_history()` drops every temporal
  buffer (TAA, clouds) at a cut, and a capture renders a fixed number of settle frames
  first. The same scene, settings and frame count give the same picture on the same
  driver.
- **Distance with every capture.** `read_distance` (`render/frame.hpp`) reads the frame's
  depth back and unprojects each pixel to how far from the eye it lies (`depth_distance` in
  `render/view.hpp`: the near plane's point through the pixel, scaled by 1 / depth; infinity
  for the sky). A lab hands it with the shot, and the picture diff (`engine/lab/diff.hpp`,
  `docs/labs.md` § The picture diff) judges a change band by band, so far scenery made cheaper
  is held to a looser floor than what is at the skis.
- **The wire probe.** With `TerrainSource::wire_probe` and `create_instance_pipelines`'
  `wire_probe`, the terrain and every instance family also get a line-list pipeline over the
  same vertex stage and line index buffers made once: `encode_terrain_wire` and
  `encode_instances_wire` draw the last frame's culled tiles and lists as their triangles'
  edges into a `kWireFormat` target, their instance counts copied from the cull's indirect
  arguments, tested against the frame's depth (read only) with each fragment's depth pushed a
  hair nearer (`frag_depth`: line topologies take no depth bias), so the ground hides what is
  behind it. The ground is banded by distance in doublings from 32 m, so the clipmap's rings
  show; the instances are one colour. It is what `ge shot --gpu --mode=wireframe` draws.
- **Per-pass stats with every capture**: GPU ms, draws, instances and triangles, so a
  lab sheet can show what a picture costs beside the picture.
- **Golden captures on the software adapter.** The development container and CI's
  Linux x64 row run the same pinned software adapter, so a golden image regenerated in
  the container is the one CI compares against. The comparison is within a tolerance:
  a per-pixel delta and a cap on the share of pixels that exceed it. Pictures from
  different drivers (the Mac's Metal included) are never compared bit for bit.

## Where the code goes

| Home | Holds |
| --- | --- |
| `engine/gpu/` | The `webgpu.h` wrapper: the device and its feature tiers, RAII handles, the pipeline manifest and creation counter, timestamp queries, staging uploads, readback. The only directory that includes `webgpu.h` |
| `engine/render/` | Mechanisms any game could use: the pass table and named targets, the frame's stats, GPU-driven instance families and their culling, the depth pyramid, the heightfield clipmap renderer, the horizon map, cascaded shadows and the shadow atlas, the sky tables and aerial perspective, the cloud volume, TAA-U, post, the 2D pass, compute particles, skinning, and the shared WGSL library |
| `plugins/<name>/app/` | A plugin's presentation half, shared by every game that links the plugin: `mountain/app` holds the snow material, the trail maps and their stamping, and the procedural tree shapes; `skier/app` holds the figure, the tracks' prints and the spray emitters (`docs/architecture.md` § Plugins) |
| `games/fall_line/app/` | The game's own: its pass table, its quality ladder's values, its grade, and whatever draws only this game has |

A piece goes in `engine/render` only if it carries no genre's words. A heightfield, an
instance family and a cloud volume are generic. Snow, a piste and a ski are not.

## Dependencies

Each library is added in the milestone that first needs it, pinned to a tag.

| Library | Licence | For | Milestone |
| --- | --- | --- | --- |
| Dawn | BSD-3-Clause | The GPU API on every target (prebuilt on the desktop, from source for iOS, iPadOS and Android); Tint for `make shaders-check` | M6 |
| SDL3 | zlib | The window and the surface (already the platform layer) | M6 |
| cgltf | MIT | Loading authored glTF models (`cmake/Cgltf.cmake`: one header, pinned by commit and SHA-256) | M8e1 |
| Dear ImGui | MIT | The developer UI, behind `GE_DEV_UI` | M10 |

Data baked into the engine as generated tables:

| Data | Licence | For | Milestone |
| --- | --- | --- | --- |
| The lunar orbiter's wide-angle camera colour mosaic, as NASA's Scientific Visualization Studio publishes it in its CGI Moon Kit (<https://svs.gsfc.nasa.gov/4720>) | Public domain; credit NASA's Scientific Visualization Studio | The moon's albedo (`engine/render/moon_data.cpp`) | M8f6 |

Considered and not taken now: meshoptimizer (added if an authored mesh needs LOD
simplification or vertex-cache ordering that the procedural builders cannot do
themselves), Tracy (the engine's own zones, `engine/prof`, answer through `ge` headless and
need no viewer), the FidelityFX SDK (its algorithms are a reference, not a dependency),
any render-graph or scene library, and any shader cross-compiler.

## Considered and not taken

| Technique | Why not, for this workload |
| --- | --- |
| Deferred shading | A handful of lights does not repay a G-buffer's bandwidth, which tile-based GPUs pay most for; forward also keeps MSAA and transparency simple |
| Visibility buffer | Its benefit over a depth prepass with an `equal` test is small for this scene's few materials, and it needs 64-bit atomics or a large render target |
| Virtual shadow maps | They need a page table, sparse allocation and feedback; the horizon map plus cached cascades plus the figures' atlas give the same sharpness where it is seen, at a fraction of the complexity |
| Meshlet (cluster) culling | Instances are many and small, so instance-level culling removes the work; clusters would pay off only for large unique meshes, which the game does not have |
| Screen-space GI or reflections | Snow under open sky is lit by the sun and the sky's irradiance table; the look does not call for them |
| Async compute | Dawn's API has one queue |

Each of these can be revisited when a measurement on this workload shows the current
technique is the bottleneck. Record the measurement in the PR that does it.
