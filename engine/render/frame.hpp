// THE FRAME'S SHARED HALF: what every pass of a frame reads and the order they run in.
//
// - `FrameUniforms`: the per-frame block (camera, sun, size, time, frustum), the C++ twin of
//   `struct Frame` in render/wgsl/frame_types.wgsl. Change both together; `static_assert`s
//   below hold the size.
// - THE NAMED TARGETS of a frame (`FrameTargets`): `hdr` (linear light, RGBA16F), `depth`
//   (reversed-z Depth32Float) and `motion` (RGBA16F: rg the screen motion since the last frame
//   in uv, b the reactive mask), at the render size. The output (`final`) is the caller's: an
//   offscreen target or the surface's texture.
// - THE TEMPORAL HALF: `FrameHistory` carries the last frame's unjittered camera, so the block
//   holds this frame's sub-pixel jitter and the previous camera every motion vector is measured
//   against (render/taa.hpp).
// - THE PASS TABLE: a game lists its passes in order as `PassRow`s, each naming the targets it
//   reads and writes. `check_pass_table` says what is wrong with an order (a read before any
//   write) with no GPU, so a test holds the game's table. Dawn places the barriers; the table is
//   the frame's graph, written where an agent can read it (docs/rendering.md § The frame).
// - `FrameStats`: what a frame cost, per pass: GPU ms (timestamp queries; NaN without the
//   tier or on the first frame), CPU ms encoding it (from its open to the next pass's), draws
//   and dispatches; and the CPU ms of the frame's prepare, before the first pass. The lab sheets
//   and the dev UI read it.
// - Bind group layouts by a short list of slot kinds (`make_layout`): binding i is the i-th
//   slot.
//
// ge:summary The frame's shared half: uniforms, named targets, the pass table and its stats
// ge:keywords render frame uniforms frame-uniforms targets hdr depth reverse-z pass-table
// ge:keywords pass-row named-targets check_pass_table frame-stats gpu-ms draws dispatches
// ge:keywords cpu-ms prepare-ms
// ge:keywords bind-group-layout make_layout slot fill_uniforms create_targets
// ge:skill render gpu
// ge:test tests/render_frame_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gpu/device.hpp"
#include "gpu/timer.hpp"
#include "render/view.hpp"

namespace ge::render {

// render/wgsl/frame_types.wgsl's `struct Frame`, field for field.
struct FrameUniforms {
  float view_proj[16];       // camera-relative world -> clip, jittered
  float inv_view_proj[16];   // clip -> camera-relative world, jittered
  float prev_view_proj[16];  // the last frame's camera-relative world -> clip, unjittered
  float eye[4];              // world xyz (m); w: the eye's altitude over the sea, km
  float sun[4];              // unit direction toward the key light; w: its illuminance, pre-exposed
  float size[4];             // render width, height, 1/width, 1/height
  float time[4];             // seconds, frame number, 0, 0
  float world[4];       // y = 0's altitude over the sea (km), the pre-exposure, the moon's direct
                        // light while the sun keys, 1 when the haze is the moon's
  float planes[6][4];   // the frustum (view.hpp's five), the sixth all zero
  float eye_delta[4];   // this eye minus the last frame's, m
  float jitter[4];      // the sub-pixel offset: render pixels (x right, y down), then uv
  float weather[4];     // the sun's pass, the deck's cover, the fog's extinction (1/m), the hidden
  float deck[4];        // the deck's tint times its light, its grey overhead over the horizon's
  float moon[4];        // unit direction toward the moon; w: its illuminance, pre-exposed
  float moon_light[4];  // lit surface's, earthlit part's luminance; angular radius; 1 if the key
  float night_sun[4];   // unit direction toward the real sun; w: its illuminance, pre-exposed
  float night[4];       // the dark sky's zenith luminance, its light, the sidereal angle, latitude
  float cloud_info[4];  // the cloud's layers drawn, the share of the march's steps, 1 if spread,
                        // the stamp
  // Three a layer (render/clouds.hpp's `cloud_rows`): base, top (world m), the noise's cut, one
  // over the cell (1/m); the wind's heading (cos, sin), the drift and the age in the noise's
  // tile (0..1); one over the stretch, the dome, the density (1/m), the shadow's optical depth.
  float clouds[kCloudLayers * 3][4];
  float moon_pole[4];    // the moon's north pole (world); w: the umbra's angular radius
  float moon_prime[4];   // toward its 0 N 0 E; w: the penumbra's angular radius
  float moon_east[4];    // toward its 0 N 90 E; w: the disc's brightest light under the shadow
  float moon_shadow[4];  // toward the Earth's shadow's axis; w: the albedo map's scale
  float mist[4];         // the reach's mist: where it begins and where it closes, m (0: none), 0, 0
};
static_assert(sizeof(FrameUniforms) ==
                  64 * 3 + 16 * 5 + 16 * 6 + 16 * 9 + 16 * 3 * kCloudLayers + 16 * 5,
              "match frame_types.wgsl");

// The last frame's unjittered camera. Invalid on the first frame and after a cut, when the
// "previous" camera is this one (no motion).
struct FrameHistory {
  bool valid = false;
  Mat4 view_proj{};
  double eye[3] = {0, 0, 0};
};

// The block for `view` at width x height. With `history`, the previous camera comes from it and
// it is advanced to this frame's; `jitter_px` shifts the projection by a sub-pixel offset (render
// pixels, x right, y down) the motion vectors and the TAA undo.
void fill_uniforms(FrameUniforms& u, const FrameView& view, uint32_t width, uint32_t height,
                   FrameHistory* history = nullptr, const double* jitter_px = nullptr);

inline constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
inline constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth32Float;
inline constexpr wgpu::TextureFormat kMotionFormat = wgpu::TextureFormat::RGBA16Float;
// The wire probes' target (render/terrain.hpp, render/instances.hpp): display colours, read back
// as they are.
inline constexpr wgpu::TextureFormat kWireFormat = wgpu::TextureFormat::RGBA8Unorm;

struct FrameTargets {
  uint32_t width = 0, height = 0;
  wgpu::Texture hdr, depth, motion;
  wgpu::TextureView hdr_view, depth_view, motion_view;
};
FrameTargets create_targets(gpu::Device& device, std::string_view label, uint32_t width,
                            uint32_t height);

// ── The pass table ──

// A row: the pass's name (also its timestamp's and stats' name) and the targets it reads and
// writes, each list space-separated ("terrain.tiles sky.aerial").
struct PassRow {
  std::string_view name;
  std::string_view reads;
  std::string_view writes;
};

// Empty when the order holds: every target a pass reads is written by an earlier pass or is
// in `inputs` (space-separated, made at load: a height texture). Otherwise one
// agent-actionable line per fault.
std::string check_pass_table(std::span<const PassRow> rows, std::string_view inputs);

inline constexpr int kMaxPasses = 32;

struct PassStat {
  std::string_view name;
  double gpu_ms = 0;        // NaN when not measured
  double gpu_start_ms = 0;  // when it began on the GPU, after the frame's first pass; NaN as gpu_ms
  double cpu_ms = 0;        // encoding it: from its open to the next pass's (or the frame's end)
  uint32_t draws = 0;
  uint32_t dispatches = 0;
};

struct FrameStats {
  std::array<PassStat, kMaxPasses> passes{};
  int count = 0;
  double cpu_ms = 0;      // every prepare and encode of the frame, wall clock
  double prepare_ms = 0;  // from begin_frame to the first pass's open: the frame's decisions
  uint32_t draws() const;
  uint32_t dispatches() const;
};

// THE RECORDER a frame's passes are opened through: each pass gets its timestamp pair (when
// the tier is on) and its row in the stats, in the order the pass table runs them.
struct FrameRecorder {
  gpu::PassTimer timer;
  FrameStats stats;
  bool timed = false;
  int64_t mark_ns = 0;    // when the last pass opened (begin_frame's time before the first)
  int64_t submit_ns = 0;  // the steady clock at after_submit: the frame's work handed to the GPU

  // Room for `passes` passes a frame.
  void init(gpu::Device& device, std::string_view label, int passes);
  // Start a frame's stats (the GPU times are the last readings that came back).
  void begin_frame(gpu::Device& device);
  wgpu::ComputePassEncoder compute(wgpu::CommandEncoder& encoder, std::string_view name);
  // Part `part` of `parts` compute passes timed and counted as ONE pass: the first opens the
  // stat and its start time, the last its end. For a chain whose every step reads what the step
  // before wrote, where a pass boundary is the barrier every backend keeps.
  wgpu::ComputePassEncoder compute_part(wgpu::CommandEncoder& encoder, std::string_view name,
                                        int part, int parts);
  wgpu::RenderPassEncoder render(wgpu::CommandEncoder& encoder, wgpu::RenderPassDescriptor desc,
                                 std::string_view name);
  // The pass opened last: count its draws and dispatches here.
  PassStat& current() { return stats.passes[static_cast<size_t>(stats.count - 1)]; }
  // After the last pass, in the same encoder; then after the submit.
  void end_frame(wgpu::CommandEncoder& encoder);
  void after_submit(gpu::Device& device);
  // Labs and tests: wait for this frame's GPU times.
  void wait(gpu::Device& device);
};

// ── Bind group layouts ──

enum class Slot : uint8_t {
  uniform,        // a uniform buffer
  storage_read,   // a read-only storage buffer
  storage,        // a read-write storage buffer
  sampler,        // a filtering sampler
  texture,        // a filterable float 2D texture
  texture_raw,    // an unfilterable float 2D texture (textureLoad only: R32F, RG32F)
  texture_3d,     // a filterable float 3D texture
  store_2d,       // a write-only RGBA16F 2D storage texture
  store_3d,       // a write-only RGBA16F 3D storage texture
  depth,          // a depth texture (textureLoad only)
  store_r32,      // a write-only R32F 2D storage texture
  depth_array,    // a depth 2D-array texture (comparison sampling)
  compare,        // a comparison sampler
  texture_array,  // a filterable float 2D-array texture
};

wgpu::BindGroupLayout make_layout(gpu::Device& device, std::string_view label,
                                  std::initializer_list<Slot> slots, wgpu::ShaderStage visibility);
wgpu::PipelineLayout make_pipeline_layout(gpu::Device& device, std::string_view label,
                                          std::initializer_list<wgpu::BindGroupLayout> groups);

// One entry of a bind group: a buffer, a texture view or a sampler, at binding = its position.
struct Bind {
  wgpu::Buffer buffer = nullptr;
  wgpu::TextureView view = nullptr;
  wgpu::Sampler sampler = nullptr;
  Bind(wgpu::Buffer b) : buffer(std::move(b)) {}     // NOLINT(google-explicit-constructor)
  Bind(wgpu::TextureView v) : view(std::move(v)) {}  // NOLINT(google-explicit-constructor)
  Bind(wgpu::Sampler s) : sampler(std::move(s)) {}   // NOLINT(google-explicit-constructor)
};
wgpu::BindGroup make_group(gpu::Device& device, std::string_view label,
                           const wgpu::BindGroupLayout& layout, std::initializer_list<Bind> binds);

// A texture, labelled; `depth` > 1 makes it 3D, `mips` > 1 gives it a mip chain.
wgpu::Texture make_texture(gpu::Device& device, std::string_view label, uint32_t width,
                           uint32_t height, uint32_t depth, wgpu::TextureFormat format,
                           wgpu::TextureUsage usage, uint32_t mips = 1);

// Labs and tests (blocks): channel 0 of one mip of a texture, row-major, as floats. `format` is
// the texture's: R32Float, Depth32Float (the depth aspect), R16Float or RGBA16Float.
std::vector<float> read_red(gpu::Device& device, const wgpu::Texture& texture, uint32_t mip,
                            uint32_t width, uint32_t height, wgpu::TextureFormat format);
// Labs and tests (blocks): how far from the eye, m, each pixel of `targets`' depth lies, seen by
// `view`'s camera (depth_distance), row-major from the top; infinity where nothing was drawn.
std::vector<float> read_distance(gpu::Device& device, const FrameTargets& targets,
                                 const FrameView& view);
float half_to_float(uint16_t h);
// The nearest half (rounding to even; overflow to infinity), for uploads of RGBA16F data.
uint16_t float_to_half(float f);

// A buffer of `size` bytes with `usage` (CopyDst added), labelled.
wgpu::Buffer make_buffer(gpu::Device& device, std::string_view label, uint64_t size,
                         wgpu::BufferUsage usage);

}  // namespace ge::render
