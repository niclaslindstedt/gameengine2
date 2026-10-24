// The frame's uniforms filled from a FrameView, its targets made, the pass table checked, and
// the layout, group and buffer helpers.
//
// ge:summary Frame uniforms, targets, the pass table's check and the binding helpers
// ge:keywords render uniforms targets pass-table bind-group layout buffer
// ge:skill render
// ge:test tests/render_frame_test.cpp
#include "render/frame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include "render/clouds.hpp"
#include "render/night.hpp"

namespace ge::render {

void fill_uniforms(FrameUniforms& u, const FrameView& view, uint32_t width, uint32_t height,
                   FrameHistory* history, const double* jitter_px) {
  std::memset(&u, 0, sizeof u);
  const CameraMatrices cam =
      camera_matrices(view, static_cast<double>(width) / static_cast<double>(height));
  // The jitter moves every point's NDC by a constant: clip.xy += offset * clip.w.
  const double jx = jitter_px ? jitter_px[0] : 0.0, jy = jitter_px ? jitter_px[1] : 0.0;
  Mat4 shift{};
  shift.m[0] = shift.m[5] = shift.m[10] = shift.m[15] = 1;
  shift.m[12] = static_cast<float>(2.0 * jx / width);
  shift.m[13] = static_cast<float>(-2.0 * jy / height);
  const Mat4 jittered = mul(shift, cam.view_proj);
  const Mat4 inv = jitter_px ? inverse(jittered) : cam.inv_view_proj;
  std::memcpy(u.view_proj, jittered.m, sizeof u.view_proj);
  std::memcpy(u.inv_view_proj, inv.m, sizeof u.inv_view_proj);
  u.jitter[0] = static_cast<float>(jx);
  u.jitter[1] = static_cast<float>(jy);
  u.jitter[2] = static_cast<float>(jx / width);
  u.jitter[3] = static_cast<float>(jy / height);
  const bool had_history = history && history->valid;
  if (had_history) {
    std::memcpy(u.prev_view_proj, history->view_proj.m, sizeof u.prev_view_proj);
    for (int i = 0; i < 3; ++i) u.eye_delta[i] = static_cast<float>(view.eye[i] - history->eye[i]);
  } else {
    std::memcpy(u.prev_view_proj, cam.view_proj.m, sizeof u.prev_view_proj);
  }
  if (history) {
    history->valid = true;
    history->view_proj = cam.view_proj;
    for (int i = 0; i < 3; ++i) history->eye[i] = view.eye[i];
  }
  for (int i = 0; i < 3; ++i) u.eye[i] = static_cast<float>(view.eye[i]);
  u.eye[3] = static_cast<float>((view.altitude_m + view.eye[1]) * 1e-3);
  double s[3];
  sun_direction(view, s);
  for (int i = 0; i < 3; ++i) u.sun[i] = static_cast<float>(s[i]);
  // Light is stored at the pre-exposure's scale (render/night.hpp): 1 by day.
  const double pre = pre_exposure(view);
  u.sun[3] = static_cast<float>(view.sun_illuminance * pre);
  u.size[0] = static_cast<float>(width);
  u.size[1] = static_cast<float>(height);
  u.size[2] = 1.0f / static_cast<float>(width);
  u.size[3] = 1.0f / static_cast<float>(height);
  u.time[0] = static_cast<float>(view.time_s);
  u.time[1] = static_cast<float>(view.frame);
  u.world[0] = static_cast<float>(view.altitude_m * 1e-3);
  std::memcpy(u.planes, cam.planes, sizeof cam.planes);
  // THE WEATHER: a visual range V closes the view as exp(-3.912 d / V) (the contrast a black
  // target keeps against the sky at V is 2 %).
  const Weather& w = view.weather;
  const auto unit = [](double x) { return static_cast<float>(std::clamp(x, 0.0, 1.0)); };
  u.weather[0] = unit(w.sun_pass);
  u.weather[1] = unit(w.cover);
  u.weather[2] = w.visibility_m > 0 ? static_cast<float>(3.912 / w.visibility_m) : 0.0f;
  u.weather[3] = unit(w.hidden);
  for (int i = 0; i < 3; ++i) u.deck[i] = static_cast<float>(w.deck_tint[i] * w.deck_light);
  u.deck[3] = unit(w.deck_zenith);
  // THE NIGHT (render/night.hpp): the pre-exposure, the moon's direct light while the sun keys,
  // which sky the haze is; the moon, its light and disc; the real sun; the dark sky.
  const Night& n = view.night;
  u.world[1] = static_cast<float>(pre);
  u.world[2] = static_cast<float>(n.other_light * pre);
  u.world[3] = n.moon_haze ? 1.0f : 0.0f;
  double d[3];
  look_dir(n.moon_azimuth, n.moon_elevation, d);
  for (int i = 0; i < 3; ++i) u.moon[i] = static_cast<float>(d[i]);
  u.moon[3] = static_cast<float>(n.moon_illuminance * pre);
  u.moon_light[0] = static_cast<float>(n.moon_surface * pre);
  u.moon_light[1] = static_cast<float>(n.earthshine * pre);
  u.moon_light[2] = static_cast<float>(n.moon_radius);
  u.moon_light[3] = n.moon_key ? 1.0f : 0.0f;
  real_sun_direction(view, d);
  for (int i = 0; i < 3; ++i) u.night_sun[i] = static_cast<float>(d[i]);
  u.night_sun[3] =
      static_cast<float>((n.moon_key ? n.sun_illuminance : view.sun_illuminance) * pre);
  u.night[0] = static_cast<float>(n.dark_sky * pre);
  u.night[1] = static_cast<float>(n.dark_light * pre);
  u.night[2] = static_cast<float>(n.sidereal);
  u.night[3] = static_cast<float>(n.latitude);
  // THE CLOUD (render/clouds.hpp).
  cloud_rows(view.clouds, u.cloud_info, u.clouds);
  // Spread over frames only with the last frame's camera to reproject by; the stamp is 1..4096.
  u.cloud_info[2] = view.clouds.quarter && view.clouds.spread && had_history ? 1.0f : 0.0f;
  u.cloud_info[3] = static_cast<float>(view.frame % kCloudStamps + 1);
  // THE MOON'S FACE AND THE EARTH'S SHADOW ON IT (render/night.hpp).
  for (int i = 0; i < 3; ++i) {
    u.moon_pole[i] = static_cast<float>(n.moon_pole[i]);
    u.moon_prime[i] = static_cast<float>(n.moon_prime[i]);
    u.moon_east[i] = static_cast<float>(n.moon_east[i]);
    u.moon_shadow[i] = static_cast<float>(n.shadow[i]);
  }
  u.moon_pole[3] = static_cast<float>(n.umbra);
  u.moon_prime[3] = static_cast<float>(n.penumbra);
  u.moon_east[3] = static_cast<float>(n.eclipse_peak);
  u.moon_shadow[3] = static_cast<float>(n.albedo_scale);
  // THE REACH: the mist closes every surface into the sky from half the reach to all of it.
  u.mist[0] = static_cast<float>(view.reach_m * kMistBegins);
  u.mist[1] = static_cast<float>(view.reach_m);
}

FrameTargets create_targets(gpu::Device& device, std::string_view label, uint32_t width,
                            uint32_t height) {
  FrameTargets t;
  t.width = width;
  t.height = height;
  auto make = [&](std::string_view name, wgpu::TextureFormat format, wgpu::TextureUsage usage) {
    const std::string l = std::string(label) + "." + std::string(name);
    wgpu::TextureDescriptor desc{};
    desc.label = wgpu::StringView(l.data(), l.size());
    desc.usage = usage;
    desc.size = {width, height, 1};
    desc.format = format;
    return device.device.CreateTexture(&desc);
  };
  t.hdr = make("hdr", kHdrFormat,
               wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
  // CopySrc: the labs and tests read the depth back (render/hiz.hpp's read_depth).
  t.depth = make("depth", kDepthFormat,
                 wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                     wgpu::TextureUsage::CopySrc);
  t.motion = make("motion", kMotionFormat,
                  wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
  t.hdr_view = t.hdr.CreateView();
  t.depth_view = t.depth.CreateView();
  t.motion_view = t.motion.CreateView();
  return t;
}

namespace {

// The words of a space-separated list.
std::vector<std::string_view> words(std::string_view list) {
  std::vector<std::string_view> out;
  size_t i = 0;
  while (i < list.size()) {
    while (i < list.size() && list[i] == ' ') ++i;
    const size_t j = list.find(' ', i);
    const size_t end = j == std::string_view::npos ? list.size() : j;
    if (end > i) out.push_back(list.substr(i, end - i));
    i = end;
  }
  return out;
}

}  // namespace

std::string check_pass_table(std::span<const PassRow> rows, std::string_view inputs) {
  std::vector<std::string_view> written = words(inputs);
  std::string faults;
  std::vector<std::string_view> names;
  for (const PassRow& row : rows) {
    if (std::find(names.begin(), names.end(), row.name) != names.end()) {
      faults += "render: pass '" + std::string(row.name) +
                "' is listed twice in the pass table; give each row its own name (skill: "
                "render, § A pass).\n";
    }
    names.push_back(row.name);
    for (std::string_view r : words(row.reads)) {
      if (std::find(written.begin(), written.end(), r) == written.end()) {
        faults += "render: pass '" + std::string(row.name) + "' reads '" + std::string(r) +
                  "' before any pass writes it; move the writer above it in the pass table, or "
                  "list the target as made at load (skill: render, § A pass).\n";
      }
    }
    for (std::string_view w : words(row.writes)) written.push_back(w);
  }
  if (names.size() > static_cast<size_t>(kMaxPasses)) {
    faults += "render: the pass table has " + std::to_string(names.size()) +
              " passes, over kMaxPasses (" + std::to_string(kMaxPasses) +
              "); raise it in render/frame.hpp.\n";
  }
  return faults;
}

uint32_t FrameStats::draws() const {
  uint32_t n = 0;
  for (int i = 0; i < count; ++i) n += passes[static_cast<size_t>(i)].draws;
  return n;
}

uint32_t FrameStats::dispatches() const {
  uint32_t n = 0;
  for (int i = 0; i < count; ++i) n += passes[static_cast<size_t>(i)].dispatches;
  return n;
}

static int64_t steady_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void FrameRecorder::init(gpu::Device& device, std::string_view label, int passes) {
  timed = timer.init(device, label, static_cast<uint32_t>(passes));
}

void FrameRecorder::begin_frame(gpu::Device& device) {
  if (timed) timer.ready(device);
  for (int i = 0; i < stats.count; ++i) {
    stats.passes[static_cast<size_t>(i)].gpu_ms = timer.ms(static_cast<uint32_t>(i));
    stats.passes[static_cast<size_t>(i)].gpu_start_ms = timer.start_ms(static_cast<uint32_t>(i));
  }
  stats.count = 0;
  mark_ns = steady_ns();
}

// The CPU time since the last mark goes to the pass opened last, or to the prepare before the
// first.
static void close_cpu(FrameStats& stats, int64_t& mark_ns) {
  const int64_t now = steady_ns();
  const double ms = static_cast<double>(now - mark_ns) / 1e6;
  if (stats.count == 0) {
    stats.prepare_ms = ms;
  } else {
    stats.passes[static_cast<size_t>(stats.count - 1)].cpu_ms = ms;
  }
  mark_ns = now;
}

static PassStat& open_stat(FrameStats& stats, int64_t& mark_ns, std::string_view name) {
  close_cpu(stats, mark_ns);
  if (stats.count >= kMaxPasses) stats.count = kMaxPasses - 1;  // check_pass_table reports it
  PassStat& p = stats.passes[static_cast<size_t>(stats.count++)];
  p.name = name;
  p.cpu_ms = 0;
  p.draws = 0;
  p.dispatches = 0;
  return p;
}

wgpu::ComputePassEncoder FrameRecorder::compute(wgpu::CommandEncoder& encoder,
                                                std::string_view name) {
  const uint32_t i = static_cast<uint32_t>(stats.count);
  open_stat(stats, mark_ns, name);
  wgpu::ComputePassDescriptor desc{};
  desc.label = wgpu::StringView(name.data(), name.size());
  desc.timestampWrites = timed ? timer.writes(i) : nullptr;
  return encoder.BeginComputePass(&desc);
}

wgpu::ComputePassEncoder FrameRecorder::compute_part(wgpu::CommandEncoder& encoder,
                                                     std::string_view name, int part, int parts) {
  if (part == 0) open_stat(stats, mark_ns, name);
  const uint32_t i = static_cast<uint32_t>(stats.count - 1);
  const wgpu::PassTimestampWrites* all = timed ? timer.writes(i) : nullptr;
  wgpu::PassTimestampWrites w{};
  if (all) {
    w = *all;
    if (part != 0) w.beginningOfPassWriteIndex = wgpu::kQuerySetIndexUndefined;
    if (part != parts - 1) w.endOfPassWriteIndex = wgpu::kQuerySetIndexUndefined;
  }
  wgpu::ComputePassDescriptor desc{};
  desc.label = wgpu::StringView(name.data(), name.size());
  const bool stamps = all && (part == 0 || part == parts - 1);
  desc.timestampWrites = stamps ? &w : nullptr;
  return encoder.BeginComputePass(&desc);
}

wgpu::RenderPassEncoder FrameRecorder::render(wgpu::CommandEncoder& encoder,
                                              wgpu::RenderPassDescriptor desc,
                                              std::string_view name) {
  const uint32_t i = static_cast<uint32_t>(stats.count);
  open_stat(stats, mark_ns, name);
  desc.label = wgpu::StringView(name.data(), name.size());
  desc.timestampWrites = timed ? timer.writes(i) : nullptr;
  return encoder.BeginRenderPass(&desc);
}

void FrameRecorder::end_frame(wgpu::CommandEncoder& encoder) {
  close_cpu(stats, mark_ns);
  if (timed) timer.resolve(encoder);
}

void FrameRecorder::after_submit(gpu::Device& device) {
  submit_ns = steady_ns();
  if (timed) timer.request(device);
}

void FrameRecorder::wait(gpu::Device& device) {
  if (!timed) return;
  timer.wait(device);
  for (int i = 0; i < stats.count; ++i) {
    stats.passes[static_cast<size_t>(i)].gpu_ms = timer.ms(static_cast<uint32_t>(i));
    stats.passes[static_cast<size_t>(i)].gpu_start_ms = timer.start_ms(static_cast<uint32_t>(i));
  }
}

wgpu::BindGroupLayout make_layout(gpu::Device& device, std::string_view label,
                                  std::initializer_list<Slot> slots, wgpu::ShaderStage visibility) {
  std::vector<wgpu::BindGroupLayoutEntry> entries;
  uint32_t i = 0;
  for (Slot s : slots) {
    wgpu::BindGroupLayoutEntry e{};
    e.binding = i++;
    e.visibility = visibility;
    switch (s) {
      case Slot::uniform:
        e.buffer.type = wgpu::BufferBindingType::Uniform;
        break;
      case Slot::storage_read:
        e.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        break;
      case Slot::storage:
        e.buffer.type = wgpu::BufferBindingType::Storage;
        // A writable storage buffer is not allowed in a vertex shader.
        e.visibility = visibility & ~wgpu::ShaderStage::Vertex;
        break;
      case Slot::sampler:
        e.sampler.type = wgpu::SamplerBindingType::Filtering;
        break;
      case Slot::texture:
        e.texture.sampleType = wgpu::TextureSampleType::Float;
        e.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        break;
      case Slot::texture_raw:
        e.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        e.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        break;
      case Slot::texture_3d:
        e.texture.sampleType = wgpu::TextureSampleType::Float;
        e.texture.viewDimension = wgpu::TextureViewDimension::e3D;
        break;
      case Slot::store_2d:
      case Slot::store_3d:
        e.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
        e.storageTexture.format = wgpu::TextureFormat::RGBA16Float;
        e.storageTexture.viewDimension =
            s == Slot::store_2d ? wgpu::TextureViewDimension::e2D : wgpu::TextureViewDimension::e3D;
        e.visibility = visibility & ~wgpu::ShaderStage::Vertex;
        break;
      case Slot::depth:
        e.texture.sampleType = wgpu::TextureSampleType::Depth;
        e.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        break;
      case Slot::store_r32:
        e.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
        e.storageTexture.format = wgpu::TextureFormat::R32Float;
        e.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
        e.visibility = visibility & ~wgpu::ShaderStage::Vertex;
        break;
      case Slot::depth_array:
        e.texture.sampleType = wgpu::TextureSampleType::Depth;
        e.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
        break;
      case Slot::compare:
        e.sampler.type = wgpu::SamplerBindingType::Comparison;
        break;
      case Slot::texture_array:
        e.texture.sampleType = wgpu::TextureSampleType::Float;
        e.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
        break;
    }
    entries.push_back(e);
  }
  wgpu::BindGroupLayoutDescriptor desc{};
  desc.label = wgpu::StringView(label.data(), label.size());
  desc.entryCount = entries.size();
  desc.entries = entries.data();
  return device.device.CreateBindGroupLayout(&desc);
}

wgpu::PipelineLayout make_pipeline_layout(gpu::Device& device, std::string_view label,
                                          std::initializer_list<wgpu::BindGroupLayout> groups) {
  std::vector<wgpu::BindGroupLayout> list(groups.begin(), groups.end());
  wgpu::PipelineLayoutDescriptor desc{};
  desc.label = wgpu::StringView(label.data(), label.size());
  desc.bindGroupLayoutCount = list.size();
  desc.bindGroupLayouts = list.data();
  return device.device.CreatePipelineLayout(&desc);
}

wgpu::BindGroup make_group(gpu::Device& device, std::string_view label,
                           const wgpu::BindGroupLayout& layout, std::initializer_list<Bind> binds) {
  std::vector<wgpu::BindGroupEntry> entries;
  uint32_t i = 0;
  for (const Bind& b : binds) {
    wgpu::BindGroupEntry e{};
    e.binding = i++;
    e.buffer = b.buffer;
    e.textureView = b.view;
    e.sampler = b.sampler;
    entries.push_back(e);
  }
  wgpu::BindGroupDescriptor desc{};
  desc.label = wgpu::StringView(label.data(), label.size());
  desc.layout = layout;
  desc.entryCount = entries.size();
  desc.entries = entries.data();
  return device.device.CreateBindGroup(&desc);
}

wgpu::Buffer make_buffer(gpu::Device& device, std::string_view label, uint64_t size,
                         wgpu::BufferUsage usage) {
  wgpu::BufferDescriptor desc{};
  desc.label = wgpu::StringView(label.data(), label.size());
  desc.size = (size + 3) / 4 * 4;
  desc.usage = usage | wgpu::BufferUsage::CopyDst;
  return device.device.CreateBuffer(&desc);
}

}  // namespace ge::render

namespace ge::render {

wgpu::Texture make_texture(gpu::Device& device, std::string_view label, uint32_t width,
                           uint32_t height, uint32_t depth, wgpu::TextureFormat format,
                           wgpu::TextureUsage usage, uint32_t mips) {
  wgpu::TextureDescriptor desc{};
  desc.label = wgpu::StringView(label.data(), label.size());
  desc.usage = usage;
  desc.dimension = depth > 1 ? wgpu::TextureDimension::e3D : wgpu::TextureDimension::e2D;
  desc.size = {width, height, depth};
  desc.format = format;
  desc.mipLevelCount = mips;
  return device.device.CreateTexture(&desc);
}

std::vector<float> read_distance(gpu::Device& device, const FrameTargets& targets,
                                 const FrameView& view) {
  const uint32_t w = targets.width, h = targets.height;
  std::vector<float> depth = read_red(device, targets.depth, 0, w, h, kDepthFormat);
  const CameraMatrices m = camera_matrices(view, static_cast<double>(w) / h);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      float& d = depth[size_t{y} * w + x];
      d = static_cast<float>(depth_distance(m, (x + 0.5) / w * 2 - 1, 1 - (y + 0.5) / h * 2, d));
    }
  }
  return depth;
}

float half_to_float(uint16_t h) {
  const uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1fu, man = h & 0x3ffu;
  float v;
  if (exp == 0) {
    v = std::ldexp(static_cast<float>(man), -24);
  } else if (exp == 31) {
    v = man ? std::nanf("") : INFINITY;
  } else {
    v = std::ldexp(static_cast<float>(man | 0x400u), static_cast<int>(exp) - 25);
  }
  return sign ? -v : v;
}

uint16_t float_to_half(float f) {
  uint32_t x;
  std::memcpy(&x, &f, 4);
  const auto sign = static_cast<uint16_t>((x >> 16) & 0x8000u);
  const uint32_t exp = (x >> 23) & 0xffu;
  uint32_t man = x & 0x7fffffu;
  if (exp == 0xff) return static_cast<uint16_t>(sign | 0x7c00u | (man ? 0x200u : 0u));
  const int e = static_cast<int>(exp) - 127 + 15;
  if (e >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
  if (e <= 0) {
    // A subnormal half (or zero): the implicit bit made explicit and shifted down.
    if (e < -10) return sign;
    man |= 0x800000u;
    const auto shift = static_cast<uint32_t>(14 - e);
    uint32_t h = man >> shift;
    const uint32_t rest = man & ((1u << shift) - 1u), half = 1u << (shift - 1u);
    if (rest > half || (rest == half && (h & 1u))) ++h;
    return static_cast<uint16_t>(sign | h);
  }
  uint32_t h = (static_cast<uint32_t>(e) << 10) | (man >> 13);
  const uint32_t rest = man & 0x1fffu;
  if (rest > 0x1000u || (rest == 0x1000u && (h & 1u))) ++h;  // a carry may round up to infinity
  return static_cast<uint16_t>(sign | h);
}

std::vector<float> read_red(gpu::Device& device, const wgpu::Texture& texture, uint32_t mip,
                            uint32_t w, uint32_t h, wgpu::TextureFormat format) {
  const bool half =
      format == wgpu::TextureFormat::R16Float || format == wgpu::TextureFormat::RGBA16Float;
  const uint32_t texel = format == wgpu::TextureFormat::RGBA16Float ? 8 : half ? 2 : 4;
  const uint32_t row = (w * texel + 255) / 256 * 256;
  wgpu::BufferDescriptor bd{};
  bd.label = "render.readback";
  bd.size = uint64_t{row} * h;
  bd.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
  wgpu::Buffer buffer = device.device.CreateBuffer(&bd);
  wgpu::TexelCopyTextureInfo src{};
  src.texture = texture;
  src.mipLevel = mip;
  if (format == kDepthFormat) src.aspect = wgpu::TextureAspect::DepthOnly;
  wgpu::TexelCopyBufferInfo dst{};
  dst.buffer = buffer;
  dst.layout.bytesPerRow = row;
  dst.layout.rowsPerImage = h;
  const wgpu::Extent3D size{w, h, 1};
  wgpu::CommandEncoder encoder = device.device.CreateCommandEncoder();
  encoder.CopyTextureToBuffer(&src, &dst, &size);
  wgpu::CommandBuffer commands = encoder.Finish();
  device.queue.Submit(1, &commands);
  gpu::wait(device,
            buffer.MapAsync(wgpu::MapMode::Read, 0, bd.size, wgpu::CallbackMode::WaitAnyOnly,
                            [](wgpu::MapAsyncStatus, wgpu::StringView) {}));
  std::vector<float> out(size_t{w} * h);
  if (const void* p = buffer.GetConstMappedRange(0, bd.size)) {
    const auto* bytes = static_cast<const uint8_t*>(p);
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) {
        const uint8_t* at = bytes + size_t{y} * row + size_t{x} * texel;
        if (half) {
          uint16_t v;
          std::memcpy(&v, at, 2);
          out[size_t{y} * w + x] = half_to_float(v);
        } else {
          std::memcpy(&out[size_t{y} * w + x], at, 4);
        }
      }
    }
  }
  buffer.Unmap();
  return out;
}

}  // namespace ge::render
