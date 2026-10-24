// GPU TIME PER PASS: a pair of timestamp queries around each pass, resolved after the passes
// and read back a frame or more later (docs/rendering.md § Measurement). Without the
// `timestamp-query` tier the timer is inert: `writes` hands back nullptr, so a pass descriptor
// takes no timestamps, and every reading is NaN (the frame's stats print "gpu ms: n/a").
//
// The readback's callback can run after the timer is gone (the device's teardown completes any
// map still in flight), so it writes only into state it shares, never into the timer itself.
//
// Times from a software adapter say nothing about a real GPU: never make a performance
// claim from them (perf skill).
//
// ge:summary GPU time per pass from timestamp query pairs, read back a frame late
// ge:keywords gpu timer timestamp timestamp-query query-set resolve gpu-ms per-pass profile
// ge:keywords pass-timestamp-writes measurement start_ms gpu-timeline gaps
// ge:skill gpu perf
// ge:test tests/gpu_capture_test.cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "gpu/device.hpp"

namespace ge::gpu {

class PassTimer {
 public:
  // Room for `passes` passes. False (and inert) without the timestamp-query tier.
  bool init(Device& device, std::string_view label, uint32_t passes);
  bool active() const { return static_cast<bool>(queries_); }

  // The timestamp writes for pass `i`, to set on its pass descriptor; nullptr when inert.
  const wgpu::PassTimestampWrites* writes(uint32_t i) const;
  // After the passes, in the same encoder: resolve the queries and copy them out.
  void resolve(wgpu::CommandEncoder& encoder);
  // After the submit: start reading them back.
  void request(Device& device);
  // Whether the readings have arrived (never blocks); `wait` blocks (tests and labs only).
  bool ready(Device& device);
  void wait(Device& device);

  // The last readings' time for pass `i`, ms; NaN when inert or not yet read.
  double ms(uint32_t i) const;
  // When pass `i` began, ms after the first pass began, on the GPU's own clock (its gaps are the
  // GPU's idle between passes; no offset to the CPU's clock is known); NaN as `ms`.
  double start_ms(uint32_t i) const;

 private:
  wgpu::QuerySet queries_;
  wgpu::Buffer resolved_, readback_;
  std::vector<wgpu::PassTimestampWrites> writes_;
  // What the readback's callback writes: shared with it, so it outlives the timer.
  struct Readings {
    std::vector<double> ms;
    std::vector<double> start_ms;
    bool pending = false;
  };
  std::shared_ptr<Readings> readings_ = std::make_shared<Readings>();
  wgpu::Future future_{};
};

}  // namespace ge::gpu
