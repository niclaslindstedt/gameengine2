// The input log's writer and parser (inputs.hpp), and the axes' grids (game.hpp).
//
// ge:summary The input log's text writer and parser; snap_axis, make_setup, the game registry
// ge:keywords inputs parse to_text snap_axis make_setup registry hex64
// ge:skill agent-interface
// ge:test tests/play_inputs_test.cpp
#include "play/inputs.hpp"

#include <cstdlib>

#include "core/math.hpp"
#include "core/num.hpp"
#include "core/num_text.hpp"

namespace ge::play {

namespace {

const GameSpec*& slot() {
  static const GameSpec* spec = nullptr;
  return spec;
}

const RendererSpec*& renderer_slot() {
  static const RendererSpec* spec = nullptr;
  return spec;
}

const SoundSpec*& sound_slot() {
  static const SoundSpec* spec = nullptr;
  return spec;
}

bool read_number(std::string_view word, double& out) {
  const std::string s(word);
  if (s.empty()) return false;
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end == s.c_str() + s.size() && out == out;
}

std::vector<std::string_view> words_of(std::string_view line) {
  std::vector<std::string_view> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    const size_t from = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') i++;
    if (i > from) out.push_back(line.substr(from, i - from));
  }
  return out;
}

}  // namespace

double snap_axis(double v, AxisKind kind) {
  if (v != v) return 0;
  if (kind == AxisKind::toggle || kind == AxisKind::press) return v >= 0.5 ? 1 : 0;
  const double steps = kind == AxisKind::signed_unit ? kSignedSteps : kLeverSteps;
  const double lo = kind == AxisKind::signed_unit ? -1 : 0;
  const double index = core::num::round(core::clamp(v, lo, 1) * steps);
  return index == 0 ? 0.0 : index / steps;
}

double Setup::number(const std::string& name) const {
  const auto it = options.find(name);
  double v = 0;
  if (it != options.end() && read_number(it->second, v)) return v;
  return 0;
}

bool make_setup(const GameSpec& spec, double seed, const std::map<std::string, std::string>& given,
                Setup& out, std::string& error) {
  out = Setup{};
  out.seed = seed;
  for (const Option& o : spec.options) out.options[o.name] = o.fallback;
  for (const auto& [name, value] : given) {
    if (!out.options.contains(name)) {
      std::string known;
      for (const Option& o : spec.options) known += std::string(known.empty() ? "" : ", ") + o.name;
      error = "the game " + std::string(spec.name) + " has no option " + name +
              (known.empty() ? " (it takes none)" : " (it takes " + known + ")");
      return false;
    }
    out.options[name] = value;
  }
  return true;
}

const GameSpec* registered_game() { return slot(); }

GameRegister::GameRegister(const GameSpec& spec) { slot() = &spec; }

const RendererSpec* registered_renderer() { return renderer_slot(); }

RendererRegister::RendererRegister(const RendererSpec& spec) { renderer_slot() = &spec; }

const SoundSpec* registered_sound() { return sound_slot(); }

SoundRegister::SoundRegister(const SoundSpec& spec) { sound_slot() = &spec; }

int64_t InputLog::steps() const {
  int64_t n = 0;
  for (const InputRun& r : runs) n += r.count;
  return n;
}

std::span<const double> InputLog::at(int64_t i) const {
  for (const InputRun& r : runs) {
    if (i < r.count) return r.values;
    i -= r.count;
  }
  return {};
}

void InputRecorder::record(std::span<const double> values) {
  if (!log_.runs.empty()) {
    InputRun& last = log_.runs.back();
    bool same = last.values.size() == values.size();
    for (size_t i = 0; same && i < values.size(); i++) same = last.values[i] == values[i];
    if (same) {
      last.count++;
      return;
    }
  }
  log_.runs.push_back({1, std::vector<double>(values.begin(), values.end())});
}

InputLog new_log(const GameSpec& spec, const Setup& setup) {
  InputLog log;
  log.game = spec.name;
  log.seed = setup.seed;
  for (const auto& [name, value] : setup.options) log.options.emplace_back(name, value);
  log.hz = spec.hz;
  for (const Axis& a : spec.axes) log.axes.emplace_back(a.name);
  return log;
}

std::string hex64(uint64_t v) {
  static const char* kDigits = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--, v >>= 4) s[static_cast<size_t>(i)] = kDigits[v & 15];
  return s;
}

std::optional<uint64_t> parse_hex64(std::string_view s) {
  if (s.size() != 16) return std::nullopt;
  uint64_t v = 0;
  for (const char c : s) {
    const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (d < 0) return std::nullopt;
    v = (v << 4) | static_cast<uint64_t>(d);
  }
  return v;
}

std::string to_text(const InputLog& log) {
  using core::num::to_string;
  std::string out = "ge-inputs " + std::to_string(kInputsVersion) + "\n";
  out += "game " + log.game + "\n";
  out += "seed " + to_string(log.seed) + "\n";
  for (const auto& [name, value] : log.options) {
    out += "option ";
    out += name;
    out += " ";
    out += value;
    out += "\n";
  }
  out += "hz " + to_string(log.hz) + "\n";
  out += "axes";
  for (const std::string& a : log.axes) out += " " + a;
  out += "\nsteps " + std::to_string(log.steps()) + "\n";
  if (log.digest) out += "digest " + hex64(*log.digest) + "\n";
  for (const InputRun& r : log.runs) {
    out += std::to_string(r.count);
    for (const double v : r.values) {
      out += ' ';
      out += to_string(v);
    }
    out += '\n';
  }
  return out;
}

std::optional<InputLog> parse_inputs(std::string_view text, std::string& error) {
  InputLog log;
  bool versioned = false;
  bool has_axes = false;
  std::optional<int64_t> steps;
  int line_no = 0;
  size_t i = 0;
  auto fail = [&](const std::string& what) {
    error = "line " + std::to_string(line_no) + ": " + what;
    return std::nullopt;
  };
  while (i <= text.size()) {
    const size_t end = text.find('\n', i);
    std::string_view line =
        text.substr(i, end == std::string_view::npos ? text.size() - i : end - i);
    i = end == std::string_view::npos ? text.size() + 1 : end + 1;
    line_no++;
    if (const size_t hash = line.find('#'); hash != std::string_view::npos)
      line = line.substr(0, hash);
    const std::vector<std::string_view> w = words_of(line);
    if (w.empty()) continue;
    const std::string_view key = w[0];
    if (!versioned) {
      if (key != "ge-inputs" || w.size() != 2 || w[1] != std::to_string(kInputsVersion)) {
        return fail("expected `ge-inputs " + std::to_string(kInputsVersion) +
                    "` first: this is not an input log, or a newer engine wrote it");
      }
      versioned = true;
      continue;
    }
    double number = 0;
    if (key == "game" && w.size() == 2) {
      log.game = w[1];
    } else if (key == "seed" && w.size() == 2 && read_number(w[1], number)) {
      log.seed = number;
    } else if (key == "option" && w.size() == 3) {
      log.options.emplace_back(w[1], w[2]);
    } else if (key == "hz" && w.size() == 2 && read_number(w[1], number)) {
      log.hz = number;
    } else if (key == "axes" && w.size() >= 2) {
      for (size_t k = 1; k < w.size(); k++) log.axes.emplace_back(w[k]);
      has_axes = true;
    } else if (key == "steps" && w.size() == 2 && read_number(w[1], number) && number >= 0) {
      steps = static_cast<int64_t>(number);
    } else if (key == "digest" && w.size() == 2) {
      log.digest = parse_hex64(w[1]);
      if (!log.digest) return fail("the digest is not sixteen lower-case hex digits");
    } else if (read_number(key, number)) {
      if (!has_axes) return fail("a run of inputs before the `axes` line");
      if (w.size() != log.axes.size() + 1) {
        return fail("a run holds " + std::to_string(w.size() - 1) + " values; the log has " +
                    std::to_string(log.axes.size()) + " axes");
      }
      if (number < 1 || number != core::num::round(number)) {
        return fail("a run's count must be a whole number of steps, 1 or more");
      }
      InputRun r{static_cast<int64_t>(number), {}};
      for (size_t k = 1; k < w.size(); k++) {
        double v = 0;
        if (!read_number(w[k], v)) return fail("`" + std::string(w[k]) + "` is not a number");
        r.values.push_back(v);
      }
      log.runs.push_back(std::move(r));
    } else {
      return fail("unreadable line `" + std::string(line) + "`");
    }
  }
  if (!versioned) {
    line_no = 1;
    return fail("empty: an input log starts with `ge-inputs " + std::to_string(kInputsVersion) +
                "`");
  }
  if (log.game.empty() || !has_axes) {
    error = "the header lacks its `game` or `axes` line";
    return std::nullopt;
  }
  if (steps && *steps != log.steps()) {
    error = "the header says " + std::to_string(*steps) + " steps; the runs hold " +
            std::to_string(log.steps()) + " (fix or drop the `steps` line)";
    return std::nullopt;
  }
  return log;
}

bool fits(const InputLog& log, const GameSpec& spec, std::string& error) {
  if (log.game != spec.name) {
    error = "the log is for the game " + log.game + ", this executable plays " + spec.name;
    return false;
  }
  std::string want;
  for (const Axis& a : spec.axes) want += std::string(want.empty() ? "" : " ") + a.name;
  std::string got;
  for (const std::string& a : log.axes) got += std::string(got.empty() ? "" : " ") + a;
  if (got != want) {
    error = "the log's axes are `" + got + "`, the game's are `" + want + "`";
    return false;
  }
  return true;
}

}  // namespace ge::play
