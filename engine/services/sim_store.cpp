// The simulated store's calls: the store's side (players, licences, the connection, staged
// failures, what a player does in the client) and the Backend each player gets, every answer
// queued on the player's clock and checked against the catalog.
//
// ge:summary The simulated store's calls and its per-player Backend, queued on a virtual clock
// ge:keywords services sim store backend queue latency commit store_stats leaderboard rank
// ge:keywords keep_best upload download around_user friends cloud quota presence invite overlay
// ge:keywords result_name event_name
// ge:skill services
// ge:test tests/services_sim_store_test.cpp
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "services/sim_store.hpp"
#include "services/sim_store_state.hpp"

namespace ge::services {

std::string_view result_name(Result r) {
  switch (r) {
    case Result::ok:
      return "ok";
    case Result::offline:
      return "offline";
    case Result::unknown_id:
      return "unknown_id";
    case Result::invalid:
      return "invalid";
    case Result::not_found:
      return "not_found";
    case Result::over_quota:
      return "over_quota";
    case Result::rate_limited:
      return "rate_limited";
    case Result::unsupported:
      return "unsupported";
    case Result::failed:
      return "failed";
  }
  return "?";
}

std::string_view event_name(EventKind k) {
  switch (k) {
    case EventKind::stats_stored:
      return "stats_stored";
    case EventKind::achievement_unlocked:
      return "achievement_unlocked";
    case EventKind::score_uploaded:
      return "score_uploaded";
    case EventKind::scores_downloaded:
      return "scores_downloaded";
    case EventKind::blob_downloaded:
      return "blob_downloaded";
    case EventKind::overlay_shown:
      return "overlay_shown";
    case EventKind::overlay_hidden:
      return "overlay_hidden";
    case EventKind::invite_received:
      return "invite_received";
    case EventKind::join_requested:
      return "join_requested";
    case EventKind::presence_changed:
      return "presence_changed";
    case EventKind::screenshot_requested:
      return "screenshot_requested";
    case EventKind::connection_lost:
      return "connection_lost";
    case EventKind::connection_restored:
      return "connection_restored";
  }
  return "?";
}

SimPlayer* SimStore::Impl::find(UserId id) {
  for (SimPlayer& p : players)
    if (p.id == id) return &p;
  return nullptr;
}
const SimPlayer* SimStore::Impl::find(UserId id) const {
  for (const SimPlayer& p : players)
    if (p.id == id) return &p;
  return nullptr;
}

void SimStore::Impl::fresh_progress(SimPlayer& p) const {
  p.stats.assign(catalog.stats.size(), 0.0);
  for (size_t i = 0; i < catalog.stats.size(); i++) p.stats[i] = catalog.stats[i].initial;
  p.stats_stored = p.stats;
  p.achieved.assign(catalog.achievements.size(), false);
  p.achieved_stored = p.achieved;
}

void SimStore::Impl::save_if_asked() {
  if (config.save_dir.empty()) return;
  // A save that fails leaves the last good one in place; the failure is in the journal and in
  // `save_error` for the owner to report.
  if (!save_store(*this, config.save_dir, &save_error))
    journal.push_back("          store save failed: " + save_error);
}

namespace {

constexpr size_t kMaxPresenceKeys = 20;

bool cloud_name_ok(std::string_view name) {
  if (name.empty() || name.size() > 255 || name.front() == '/' || name.back() == '/') return false;
  if (name.find("..") != std::string_view::npos || name.find("//") != std::string_view::npos)
    return false;
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-' || c == '/';
    if (!ok) return false;
  }
  return true;
}

std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

// Ranks a board: better scores first, the earlier of two equal ones first.
void rank_board(std::vector<SimEntry>& board, BoardOrder order) {
  std::sort(board.begin(), board.end(), [order](const SimEntry& a, const SimEntry& b) {
    if (a.score != b.score)
      return order == BoardOrder::ascending ? a.score < b.score : a.score > b.score;
    return a.seq < b.seq;
  });
}

int32_t rank_of(const std::vector<SimEntry>& ranked, UserId user) {
  for (size_t i = 0; i < ranked.size(); i++)
    if (ranked[i].user == user) return static_cast<int32_t>(i + 1);
  return 0;
}

class SimBackend final : public Backend {
 public:
  SimBackend(SimStore::Impl& s, UserId user) : s_(s), user_(user) {}

  std::string_view name() const override { return s_.config.save_dir.empty() ? "sim" : "offline"; }

  void poll(int64_t now_us, std::vector<Event>& out) override {
    SimPlayer& p = me();
    p.now_us = std::max(p.now_us, now_us);
    std::stable_sort(p.queue.begin(), p.queue.end(), [](const SimPending& a, const SimPending& b) {
      return a.due_us != b.due_us ? a.due_us < b.due_us : a.seq < b.seq;
    });
    size_t n = 0;
    while (n < p.queue.size() && p.queue[n].due_us <= p.now_us) n++;
    for (size_t i = 0; i < n; i++) out.push_back(std::move(p.queue[i].event));
    p.queue.erase(p.queue.begin(), p.queue.begin() + static_cast<std::ptrdiff_t>(n));
  }

  UserInfo user() const override {
    const SimPlayer& p = me();
    return {p.id, p.name, p.language, p.country};
  }
  AppId app() const override { return s_.catalog.app; }
  bool online() const override { return me().online; }
  Environment environment() const override { return me().environment; }

  Result unlock(std::string_view id) override {
    const int a = find_achievement(s_.catalog, id);
    if (a < 0) return log("unlock", id, Result::unknown_id);
    me().achieved[static_cast<size_t>(a)] = true;
    return log("unlock", id, Result::ok);
  }
  Result achieved(std::string_view id, bool& out) const override {
    const int a = find_achievement(s_.catalog, id);
    if (a < 0) return Result::unknown_id;
    out = me().achieved[static_cast<size_t>(a)];
    return Result::ok;
  }
  Result show_progress(std::string_view id, uint32_t current, uint32_t max) override {
    const int a = find_achievement(s_.catalog, id);
    if (a < 0) return log("show_progress", id, Result::unknown_id);
    if (max == 0 || current > max) return log("show_progress", id, Result::invalid);
    return log("show_progress",
               std::string(id) + " " + std::to_string(current) + "/" + std::to_string(max),
               Result::ok);
  }

  Result set_stat(std::string_view id, double value) override {
    const int i = find_stat(s_.catalog, id);
    if (i < 0) return log("set_stat", id, Result::unknown_id);
    const StatDef& def = s_.catalog.stats[static_cast<size_t>(i)];
    double& v = me().stats[static_cast<size_t>(i)];
    const bool whole =
        def.kind != StatKind::integer || value == static_cast<double>(static_cast<int64_t>(value));
    if (!(value >= def.min && value <= def.max) || !whole || (def.increment_only && value < v))
      return log("set_stat", std::string(id) + "=" + num(value), Result::invalid);
    v = value;
    return log("set_stat", std::string(id) + "=" + num(value), Result::ok);
  }
  Result add_stat(std::string_view id, double delta) override {
    const int i = find_stat(s_.catalog, id);
    if (i < 0) return log("add_stat", id, Result::unknown_id);
    return set_stat(id, me().stats[static_cast<size_t>(i)] + delta);
  }
  Result stat(std::string_view id, double& out) const override {
    const int i = find_stat(s_.catalog, id);
    if (i < 0) return Result::unknown_id;
    out = me().stats[static_cast<size_t>(i)];
    return Result::ok;
  }

  RequestId store_stats() override {
    SimPlayer& p = me();
    const RequestId r = p.next_request++;
    Event e;
    e.kind = EventKind::stats_stored;
    e.request = r;
    e.result = server_result(p);
    if (e.result == Result::ok) {
      for (size_t i = 0; i < s_.catalog.stats.size(); i++) {
        const StatDef& def = s_.catalog.stats[i];
        if (def.max_change > 0 && std::abs(p.stats[i] - p.stats_stored[i]) > def.max_change)
          e.result = Result::invalid;
      }
      if (e.result == Result::invalid) p.stats = p.stats_stored;  // the store throws the jump out
    }
    std::vector<Event> unlocked;
    if (e.result == Result::ok) {
      p.stats_stored = p.stats;
      for (size_t a = 0; a < s_.catalog.achievements.size(); a++) {
        const AchievementDef& def = s_.catalog.achievements[a];
        if (!def.stat.empty()) {
          const int si = find_stat(s_.catalog, def.stat);
          if (si >= 0 && p.stats[static_cast<size_t>(si)] >= def.threshold) p.achieved[a] = true;
        }
        if (p.achieved[a] && !p.achieved_stored[a]) {
          p.achieved_stored[a] = true;
          Event u;
          u.kind = EventKind::achievement_unlocked;
          u.request = r;
          u.id = std::string(def.id);
          unlocked.push_back(std::move(u));
        }
      }
      s_.save_if_asked();
    }
    log("store_stats", "#" + std::to_string(r), e.result);
    const int64_t due = answer_time();
    queue(p, std::move(e), due);
    for (Event& u : unlocked) queue(p, std::move(u), due);
    return r;
  }

  Result reset_stats(bool achievements_too) override {
    SimPlayer& p = me();
    const std::vector<bool> keep = p.achieved_stored;
    s_.fresh_progress(p);
    if (!achievements_too) p.achieved = p.achieved_stored = keep;
    s_.save_if_asked();
    return log("reset_stats", achievements_too ? "with achievements" : "stats", Result::ok);
  }

  RequestId upload_score(std::string_view board, int32_t score, std::span<const int32_t> details,
                         UploadMode mode, std::span<const uint8_t> blob) override {
    SimPlayer& p = me();
    Event e;
    e.kind = EventKind::score_uploaded;
    e.request = p.next_request++;
    e.id = std::string(board);
    e.score = score;
    const int b = find_board(s_.catalog, board);
    if (b < 0)
      e.result = Result::unknown_id;
    else if (details.size() > kMaxScoreDetails)
      e.result = Result::invalid;
    else
      e.result = server_result(p);
    if (e.result == Result::ok) {
      const BoardOrder order = s_.catalog.boards[static_cast<size_t>(b)].order;
      std::vector<SimEntry>& entries = s_.boards[static_cast<size_t>(b)];
      rank_board(entries, order);
      e.previous_rank = rank_of(entries, p.id);
      SimEntry* mine = nullptr;
      for (SimEntry& en : entries)
        if (en.user == p.id) mine = &en;
      const bool better = mine == nullptr || (order == BoardOrder::ascending ? score < mine->score
                                                                             : score > mine->score);
      e.changed = mode == UploadMode::force || better;
      if (e.changed) {
        if (mine == nullptr) {
          entries.push_back({});
          mine = &entries.back();
        }
        mine->user = p.id;
        mine->score = score;
        mine->seq = s_.next_seq++;
        mine->details.assign(details.begin(), details.end());
        mine->blob = 0;
        if (!blob.empty()) {
          mine->blob = s_.next_blob++;
          s_.blobs[mine->blob].assign(blob.begin(), blob.end());
        }
        e.blob = mine->blob;
        rank_board(entries, order);
        s_.save_if_asked();
      }
      e.rank = rank_of(entries, p.id);
      e.total = static_cast<int32_t>(entries.size());
    }
    log("upload_score", e.id + " " + std::to_string(score) + " rank " + std::to_string(e.rank),
        e.result);
    const RequestId r = e.request;
    queue(p, std::move(e), answer_time());
    return r;
  }

  RequestId download_scores(std::string_view board, BoardRange range, int32_t first,
                            int32_t last) override {
    SimPlayer& p = me();
    Event e;
    e.kind = EventKind::scores_downloaded;
    e.request = p.next_request++;
    e.id = std::string(board);
    const int b = find_board(s_.catalog, board);
    if (b < 0)
      e.result = Result::unknown_id;
    else if (range != BoardRange::friends && first > last)
      e.result = Result::invalid;
    else
      e.result = server_result(p);
    if (e.result == Result::ok) {
      std::vector<SimEntry>& entries = s_.boards[static_cast<size_t>(b)];
      rank_board(entries, s_.catalog.boards[static_cast<size_t>(b)].order);
      e.total = static_cast<int32_t>(entries.size());
      int32_t lo = first, hi = last;
      if (range == BoardRange::around_user) {
        const int32_t mine = rank_of(entries, p.id);
        lo = mine == 0 ? 1 : mine + first;
        hi = mine == 0 ? 0 : mine + last;
      }
      for (size_t i = 0; i < entries.size(); i++) {
        const SimEntry& en = entries[i];
        const int32_t rank = static_cast<int32_t>(i + 1);
        bool take = rank >= lo && rank <= hi;
        if (range == BoardRange::friends)
          take = en.user == p.id ||
                 std::find(p.friends.begin(), p.friends.end(), en.user) != p.friends.end();
        if (!take) continue;
        const SimPlayer* who = s_.find(en.user);
        e.entries.push_back(
            {en.user, who ? who->name : std::string(), rank, en.score, en.details, en.blob});
      }
    }
    log("download_scores", e.id + " " + std::to_string(e.entries.size()) + " entries", e.result);
    const RequestId r = e.request;
    queue(p, std::move(e), answer_time());
    return r;
  }

  RequestId download_blob(BlobId blob) override {
    SimPlayer& p = me();
    Event e;
    e.kind = EventKind::blob_downloaded;
    e.request = p.next_request++;
    e.blob = blob;
    e.result = server_result(p);
    if (e.result == Result::ok) {
      const auto it = s_.blobs.find(blob);
      if (it == s_.blobs.end())
        e.result = Result::not_found;
      else
        e.bytes = it->second;
    }
    log("download_blob", std::to_string(blob), e.result);
    const RequestId r = e.request;
    queue(p, std::move(e), answer_time());
    return r;
  }

  Result cloud_write(std::string_view name, std::span<const uint8_t> bytes) override {
    SimPlayer& p = me();
    if (!cloud_name_ok(name)) return log("cloud_write", name, Result::invalid);
    uint64_t used = bytes.size();
    for (const auto& [n, b] : p.cloud)
      if (n != name) used += b.size();
    if (used > s_.config.cloud_quota_bytes) return log("cloud_write", name, Result::over_quota);
    p.cloud[std::string(name)].assign(bytes.begin(), bytes.end());
    s_.save_if_asked();
    return log("cloud_write", std::string(name) + " " + std::to_string(bytes.size()) + " B",
               Result::ok);
  }
  Result cloud_read(std::string_view name, std::vector<uint8_t>& out) const override {
    const SimPlayer& p = me();
    const auto it = p.cloud.find(std::string(name));
    if (it == p.cloud.end()) return Result::not_found;
    out = it->second;
    return Result::ok;
  }
  Result cloud_delete(std::string_view name) override {
    SimPlayer& p = me();
    if (p.cloud.erase(std::string(name)) == 0) return log("cloud_delete", name, Result::not_found);
    s_.save_if_asked();
    return log("cloud_delete", name, Result::ok);
  }
  void cloud_files(std::vector<CloudFile>& out) const override {
    out.clear();
    for (const auto& [n, b] : me().cloud) out.push_back({n, b.size()});
  }
  void cloud_quota(uint64_t& total, uint64_t& available) const override {
    uint64_t used = 0;
    for (const auto& [n, b] : me().cloud) used += b.size();
    total = s_.config.cloud_quota_bytes;
    available = total - std::min(total, used);
  }

  Result set_presence(std::string_view key, std::string_view value) override {
    SimPlayer& p = me();
    if (key.empty() || key.size() > 64 || value.size() > 256)
      return log("set_presence", key, Result::invalid);
    const std::string k(key);
    if (value.empty())
      p.presence.erase(k);
    else if (p.presence.count(k) == 0 && p.presence.size() >= kMaxPresenceKeys)
      return log("set_presence", key, Result::invalid);
    else
      p.presence[k] = std::string(value);
    tell_friends(p);
    return log("set_presence", k + "=" + std::string(value), Result::ok);
  }
  void clear_presence() override {
    SimPlayer& p = me();
    p.presence.clear();
    tell_friends(p);
    log("clear_presence", "", Result::ok);
  }
  std::string friend_presence(UserId user, std::string_view key) const override {
    const SimPlayer& p = me();
    if (user != p.id && std::find(p.friends.begin(), p.friends.end(), user) == p.friends.end())
      return {};
    const SimPlayer* f = s_.find(user);
    if (f == nullptr || !f->online) return {};
    const auto it = f->presence.find(std::string(key));
    return it == f->presence.end() ? std::string() : it->second;
  }

  void friends(std::vector<Friend>& out) const override {
    out.clear();
    for (const UserId id : me().friends) {
      const SimPlayer* f = s_.find(id);
      if (f == nullptr) continue;
      out.push_back({f->id, f->name, f->online ? Presence::online : Presence::offline, f->online});
    }
  }
  Result invite(UserId user, std::string_view connect) override {
    SimPlayer& p = me();
    if (std::find(p.friends.begin(), p.friends.end(), user) == p.friends.end())
      return log("invite", std::to_string(user), Result::not_found);
    if (!p.online) return log("invite", std::to_string(user), Result::offline);
    SimPlayer* f = s_.find(user);
    f->invites.push_back({p.id, std::string(connect)});
    Event e;
    e.kind = EventKind::invite_received;
    e.user = p.id;
    e.text = std::string(connect);
    queue(*f, std::move(e), answer_time());
    return log("invite", f->name + " " + std::string(connect), Result::ok);
  }
  Result open_overlay(OverlayPage page, std::string_view url, AppId app) override {
    SimPlayer& p = me();
    static constexpr const char* kPages[] = {"friends",   "players",  "achievements", "stats",
                                             "community", "settings", "store",        "url"};
    std::string what = kPages[static_cast<size_t>(page)];
    if (page == OverlayPage::url) what += " " + std::string(url);
    if (page == OverlayPage::store) what += " " + std::to_string(app ? app : s_.catalog.app);
    if (!p.environment.overlay) return log("open_overlay", what, Result::unsupported);
    if (page == OverlayPage::url && url.empty()) return log("open_overlay", what, Result::invalid);
    Event shown;
    shown.kind = EventKind::overlay_shown;
    queue(p, std::move(shown), p.now_us);
    Event hidden;
    hidden.kind = EventKind::overlay_hidden;
    queue(p, std::move(hidden), p.now_us + s_.config.overlay_us);
    return log("open_overlay", what, Result::ok);
  }

  bool owns(AppId app) const override { return me().licences.count(app) != 0; }
  bool installed(AppId dlc) const override {
    const auto it = me().licences.find(dlc);
    return it != me().licences.end() && it->second;
  }

  Result add_screenshot(std::span<const uint8_t> rgb, int width, int height,
                        std::string_view caption) override {
    if (width <= 0 || height <= 0 ||
        rgb.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 3)
      return log("add_screenshot", caption, Result::invalid);
    me().screenshots++;
    return log("add_screenshot",
               std::to_string(width) + "x" + std::to_string(height) + " " + std::string(caption),
               Result::ok);
  }
  void hook_screenshots(bool hook) override {
    me().hooked = hook;
    log("hook_screenshots", hook ? "on" : "off", Result::ok);
  }

 private:
  SimPlayer& me() { return *s_.find(user_); }
  const SimPlayer& me() const { return *s_.find(user_); }

  // What a call to the store's servers answers now: offline, a staged failure, or ok.
  Result server_result(SimPlayer& p) {
    if (!p.online) return Result::offline;
    const Result staged = p.fail_next;
    p.fail_next = Result::ok;
    return staged;
  }
  int64_t answer_time() {
    int64_t t = me().now_us + s_.config.latency_us;
    if (s_.config.jitter_us > 0)
      t += static_cast<int64_t>(core::next(s_.rng) * static_cast<double>(s_.config.jitter_us));
    return t;
  }
  void queue(SimPlayer& to, Event e, int64_t due) {
    to.queue.push_back({due, s_.queued++, std::move(e)});
  }
  void tell_friends(const SimPlayer& p) {
    for (const UserId id : p.friends) {
      SimPlayer* f = s_.find(id);
      if (f == nullptr) continue;
      Event e;
      e.kind = EventKind::presence_changed;
      e.user = p.id;
      queue(*f, std::move(e), answer_time());
    }
  }
  Result log(std::string_view call, std::string_view what, Result r) const {
    const SimPlayer& p = me();
    char t[32];
    std::snprintf(t, sizeof t, "%9.3f s ", static_cast<double>(p.now_us) / 1e6);
    s_.journal.push_back(std::string(t) + p.name + " " + std::string(call) + " " +
                         std::string(what) + " -> " + std::string(result_name(r)));
    return r;
  }

  SimStore::Impl& s_;
  UserId user_;
};

}  // namespace

SimStore::SimStore(const Catalog& catalog, SimConfig config) : impl_(std::make_unique<Impl>()) {
  impl_->catalog = catalog;
  impl_->config = std::move(config);
  impl_->rng = core::make_rng(impl_->config.seed);
  impl_->boards.resize(catalog.boards.size());
}
SimStore::~SimStore() = default;

const Catalog& SimStore::catalog() const { return impl_->catalog; }
const SimConfig& SimStore::config() const { return impl_->config; }

UserId SimStore::add_user(std::string name, std::string language, std::string country) {
  SimPlayer p;
  p.id = impl_->next_user++;
  p.name = std::move(name);
  p.language = std::move(language);
  p.country = std::move(country);
  impl_->fresh_progress(p);
  p.licences[impl_->catalog.app] = true;
  impl_->players.push_back(std::move(p));
  return impl_->players.back().id;
}

void SimStore::befriend(UserId a, UserId b) {
  SimPlayer* pa = impl_->find(a);
  SimPlayer* pb = impl_->find(b);
  if (pa == nullptr || pb == nullptr || a == b) return;
  if (std::find(pa->friends.begin(), pa->friends.end(), b) == pa->friends.end())
    pa->friends.push_back(b);
  if (std::find(pb->friends.begin(), pb->friends.end(), a) == pb->friends.end())
    pb->friends.push_back(a);
}

void SimStore::grant(UserId user, AppId app, bool installed) {
  if (SimPlayer* p = impl_->find(user)) p->licences[app] = installed;
}

void SimStore::set_environment(UserId user, Environment environment) {
  if (SimPlayer* p = impl_->find(user)) p->environment = environment;
}

void SimStore::set_online(UserId user, bool online) {
  SimPlayer* p = impl_->find(user);
  if (p == nullptr || p->online == online) return;
  p->online = online;
  Event e;
  e.kind = online ? EventKind::connection_restored : EventKind::connection_lost;
  p->queue.push_back({p->now_us, impl_->queued++, std::move(e)});
  impl_->journal.push_back("          store " + p->name + (online ? " back online" : " cut off"));
}

void SimStore::fail_next(UserId user, Result result) {
  if (SimPlayer* p = impl_->find(user)) p->fail_next = result;
}

void SimStore::press_screenshot_key(UserId user) {
  SimPlayer* p = impl_->find(user);
  if (p == nullptr) return;
  if (p->hooked) {
    Event e;
    e.kind = EventKind::screenshot_requested;
    p->queue.push_back({p->now_us, impl_->queued++, std::move(e)});
  } else {
    p->screenshots++;  // the store's client captured the frame itself
  }
  impl_->journal.push_back("          store " + p->name + " pressed the screenshot key");
}

bool SimStore::accept_invite(UserId user, UserId inviter) {
  SimPlayer* p = impl_->find(user);
  if (p == nullptr) return false;
  for (auto it = p->invites.rbegin(); it != p->invites.rend(); ++it) {
    if (it->from != inviter) continue;
    Event e;
    e.kind = EventKind::join_requested;
    e.user = inviter;
    e.text = it->connect;
    p->queue.push_back({p->now_us, impl_->queued++, std::move(e)});
    impl_->journal.push_back("          store " + p->name + " accepted an invite: " + it->connect);
    return true;
  }
  return false;
}

bool SimStore::join_friend(UserId user, UserId friend_id) {
  SimPlayer* p = impl_->find(user);
  const SimPlayer* f = impl_->find(friend_id);
  if (p == nullptr || f == nullptr || !f->online) return false;
  if (std::find(p->friends.begin(), p->friends.end(), friend_id) == p->friends.end()) return false;
  const auto it = f->presence.find("connect");
  if (it == f->presence.end()) return false;
  Event e;
  e.kind = EventKind::join_requested;
  e.user = friend_id;
  e.text = it->second;
  p->queue.push_back({p->now_us, impl_->queued++, std::move(e)});
  impl_->journal.push_back("          store " + p->name + " joined " + f->name + ": " + it->second);
  return true;
}

std::unique_ptr<Backend> SimStore::backend(UserId user) {
  if (impl_->find(user) == nullptr) return nullptr;
  return std::make_unique<SimBackend>(*impl_, user);
}

std::vector<UserId> SimStore::users() const {
  std::vector<UserId> out;
  for (const SimPlayer& p : impl_->players) out.push_back(p.id);
  return out;
}

int SimStore::screenshots(UserId user) const {
  const SimPlayer* p = impl_->find(user);
  return p ? p->screenshots : 0;
}

const std::vector<std::string>& SimStore::journal() const { return impl_->journal; }

}  // namespace ge::services
