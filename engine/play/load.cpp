// The load run's body (load.hpp): the replay under the timeline and the sampler, the two
// phases' analyses, the files, and the JSON.
//
// ge:summary The load run's body: the recorded replay, the phases, the charts and the JSON
// ge:keywords play load replay timeline sampler paced charts json
// ge:skill agent-interface perf
// ge:test tests/cli_perf_test.cpp
#include "play/load.hpp"

#include <chrono>
#include <fstream>
#include <thread>

#include "core/num_text.hpp"
#include "core/thread_name.hpp"
#include "lab/load_chart.hpp"
#include "lab/plot.hpp"
#include "lab/png.hpp"
#include "play/session.hpp"

namespace ge::play {

namespace fs = std::filesystem;

namespace {

std::string fixed(double v, int places) { return core::num::to_fixed(v, places); }

bool write_file(const fs::path& path, const std::string& bytes, std::vector<std::string>& files) {
  std::ofstream f(path, std::ios::binary);
  f << bytes;
  if (!f) return false;
  files.push_back(path.generic_string());
  return true;
}

void write_chart(const fs::path& dir, const std::string& stem, const std::string& id,
                 const lab::Chart& chart, std::vector<std::string>& files) {
  write_file(dir / (stem + ".load-" + id + ".svg"), lab::to_svg(chart.plot), files);
  const fs::path png = dir / (stem + ".load-" + id + ".png");
  if (lab::write_png(png.string(), lab::to_picture(chart.plot)))
    files.push_back(png.generic_string());
}

}  // namespace

std::optional<LoadRun> load_run(const GameSpec& spec, const InputLog& log, const LoadRunOptions& o,
                                const fs::path& dir, std::string& error) {
  Setup setup;
  if (!setup_of(spec, log, setup, error)) return std::nullopt;
  const int64_t steps = o.most_steps < 0 ? log.steps() : std::min(o.most_steps, log.steps());
  const size_t axes = spec.axes.size();
  std::vector<double> in(axes);
  core::name_this_thread("main", false);
  prof::CpuSampler sampler;
  prof::timeline_start(static_cast<size_t>(std::max<int64_t>(4096, steps * 8)));
  const int64_t origin = prof::timeline_origin_ns();
  sampler.start(o.sample_us, origin);
  std::unique_ptr<Run> run;
  {
    GE_WORK("open");
    run = spec.open(setup, error);
  }
  const int64_t open_end = prof::now_ns() - origin;
  std::vector<prof::FrameWindow> slots;
  const int64_t slot_ns = static_cast<int64_t>(1e9 / spec.hz);
  const int64_t steps_from = prof::now_ns();
  if (run) {
    for (int64_t i = 0; i < steps; i++) {
      const int64_t at = steps_from + i * slot_ns;
      if (o.paced) {
        const int64_t wait = at - prof::now_ns();
        if (wait > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
      }
      const int64_t start = prof::now_ns();
      const std::span<const double> raw = log.at(i);
      for (size_t k = 0; k < axes; k++) in[k] = snap_axis(raw[k], spec.axes[k].kind);
      {
        GE_WORK("step");
        run->step(in);
      }
      if (o.paced) {
        slots.push_back({std::max(start, at) - origin, at + slot_ns - origin});
      } else {
        slots.push_back({start - origin, prof::now_ns() - origin});
      }
    }
  }
  const int64_t steps_end = prof::now_ns() - origin;
  prof::timeline_stop();
  sampler.stop();
  if (!run) return std::nullopt;

  LoadRun r;
  r.steps = steps;
  r.digest = run->digest();
  if (steps == log.steps() && log.digest) r.same_digest = r.digest == *log.digest;
  r.open_ms = static_cast<double>(open_end) / 1e6;
  r.steps_ms = static_cast<double>(steps_end - open_end) / 1e6;
  const std::vector<prof::Lane> lanes = prof::timeline_lanes();
  prof::LoadOptions lo;
  lo.sampler_os_id = sampler.os_id();
  lo.bin_ns = std::max<int64_t>(1, o.bin_ms) * 1000000;
  lo.to_ns = open_end;
  r.open = prof::analyze_load(lanes, sampler.samples(), {}, lo);
  lo.from_ns = open_end;
  lo.to_ns = std::max(steps_end, open_end + 1);
  r.run = prof::analyze_load(lanes, sampler.samples(), slots, lo);

  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string stem = std::string(spec.name) + "-" + core::num::to_string(log.seed);
  write_file(dir / (stem + ".lanes.tsv"), lab::write_series(lab::lanes_spans(lanes)), r.files);
  write_file(dir / (stem + ".load-open.tsv"), lab::write_series(lab::load_series(r.open)), r.files);
  write_file(dir / (stem + ".load-steps.tsv"), lab::write_series(lab::load_series(r.run)), r.files);
  const std::string what = std::string(spec.name) + ", seed " + core::num::to_string(log.seed);
  write_chart(dir, stem, "open-lanes",
              lab::lanes_chart({"EVERY THREAD, THE OPEN", what, "t (ms)", "", 1280, 420}, lanes,
                               r.open, r.open.from_ns, r.open.to_ns, {}),
              r.files);
  // The steps' lanes over their first tenth of a second (a dozen slots): a whole run is a
  // smear at a pixel a slot.
  const int64_t zoom_to = std::min(r.run.to_ns, r.run.from_ns + 100'000'000);
  write_chart(dir, stem, "steps-lanes",
              lab::lanes_chart({"EVERY THREAD, THE FIRST STEPS",
                                what + (o.paced ? ", paced at " : ", back to back at ") +
                                    core::num::to_string(spec.hz) + " Hz",
                                "t (ms)", "", 1280, 420},
                               lanes, r.run, r.run.from_ns, zoom_to, slots),
              r.files);
  write_chart(dir, stem, "open-busy",
              lab::busy_chart({"BUSY SHARE, THE OPEN", what, "t (ms)", "", 1280, 420}, r.open),
              r.files);
  write_chart(dir, stem, "steps-busy",
              lab::busy_chart({"BUSY SHARE, THE STEPS", what, "t (ms)", "", 1280, 420}, r.run),
              r.files);
  write_chart(
      dir, stem, "open-working",
      lab::working_chart({"THREADS WORKING AT ONCE, THE OPEN", what, "", "", 960, 360}, r.open),
      r.files);
  write_chart(
      dir, stem, "steps-working",
      lab::working_chart({"THREADS WORKING AT ONCE, THE STEPS", what, "", "", 960, 360}, r.run),
      r.files);
  return r;
}

std::string load_json(const prof::Load& L) {
  std::string lanes;
  for (const prof::LaneLoad& l : L.lanes) {
    const double w = static_cast<double>(std::max<int64_t>(1, L.to_ns - L.from_ns));
    lanes += std::string(lanes.empty() ? "" : ",") + "{\"name\":" + quote(l.name) +
             ",\"gpu\":" + (l.gpu ? "true" : "false") + ",\"work\":" + fixed(l.work_share, 4) +
             ",\"spin\":" + fixed(static_cast<double>(l.spin_ns) / w, 4) +
             ",\"sleep\":" + fixed(static_cast<double>(l.sleep_ns) / w, 4) +
             ",\"cpu\":" + fixed(l.cpu_share, 4) +
             ",\"top\":" + quote(l.work_by_name.empty() ? "" : l.work_by_name[0].first) + "}";
  }
  std::string threads;
  for (size_t i = 0; i < L.os_threads.size() && i < 12; i++) {
    const prof::OsThreadLoad& t = L.os_threads[i];
    threads += std::string(threads.empty() ? "" : ",") + "{\"name\":" + quote(t.name) +
               ",\"cpu\":" + fixed(t.share, 4) + ",\"marked\":" + (t.lane >= 0 ? "true" : "false") +
               "}";
  }
  std::string cores;
  for (const double c : L.cores_busy) cores += std::string(cores.empty() ? "" : ",") + fixed(c, 4);
  std::string findings;
  for (const prof::LoadFinding& f : L.findings) {
    findings += std::string(findings.empty() ? "" : ",") + "{\"what\":" + quote(f.what) +
                ",\"next\":" + quote(f.next) + "}";
  }
  std::vector<int64_t> dur, own;
  for (const prof::FrameLoad& f : L.frames) {
    dur.push_back(f.dur_ns);
    own.push_back(f.main_work_ns);
  }
  std::sort(dur.begin(), dur.end());
  std::sort(own.begin(), own.end());
  const auto mid = [](const std::vector<int64_t>& v) {
    return v.empty() ? 0.0 : static_cast<double>(v[v.size() / 2]) / 1e6;
  };
  return "{\"ms\":" + fixed(static_cast<double>(L.to_ns - L.from_ns) / 1e6, 3) +
         ",\"hardware_threads\":" + std::to_string(L.hardware_threads) +
         ",\"efficiency\":" + fixed(L.efficiency, 4) +
         ",\"os_efficiency\":" + fixed(L.os_efficiency, 4) +
         ",\"mean_working\":" + fixed(L.mean_working, 3) + ",\"lanes\":[" + lanes +
         "],\"threads\":[" + threads + "],\"cores\":[" + cores +
         "],\"frames\":" + std::to_string(L.frames.size()) +
         ",\"frame_ms_p50\":" + fixed(mid(dur), 3) + ",\"main_work_ms_p50\":" + fixed(mid(own), 3) +
         ",\"findings\":[" + findings + "],\"text\":" + quote(prof::load_text(L)) + "}";
}

}  // namespace ge::play
