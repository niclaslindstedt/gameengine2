// THE PLATFORM SERVICES: what a store's client gives a game (who is playing, achievements,
// stats, leaderboards, cloud files, rich presence, friends and their invites, the store's
// overlay, ownership of the game and its DLC, screenshots) as ONE interface, whatever store is
// behind it. A game is written against `Backend` once; which backend it gets is decided at
// start-up: a store's (Steam's, `engine/steam`, docs/goals.md M20b), the offline store
// (sim_store.hpp's `open_offline`: one player's progress kept in a directory, for a build sold
// outside a store) or the simulated store (`SimStore`: many players on a virtual clock, for
// tests and labs). docs/services.md is the design, with every call's store mapping.
//
// THE SHAPE. A call that a store answers at once returns a `Result`; a call that goes to the
// store's servers returns a `RequestId` and its answer arrives later as an `Event` carrying
// that id. So does everything the store starts on its own (an invite, the overlay opening, the
// screenshot key). `poll` is the one pump: call it once a frame on the main thread with the
// frame's time, and act on the events it appends. Nothing is delivered from another thread and
// nothing arrives between polls, so a session over the simulated store is a pure function of its
// script and its clock.
//
// STATS AND ACHIEVEMENTS are changed locally (`unlock`, `set_stat`, `add_stat`: the new value
// reads back at once) and committed by `store_stats`, whose `stats_stored` event says whether
// the store took them; an achievement the commit unlocked (by `unlock` or by its stat reaching
// its threshold) arrives as an `achievement_unlocked` event with it. Commit at a natural pause
// (a run's end), not every frame: stores rate-limit it.
//
// ge:summary The platform services interface: identity, achievements, stats, boards, cloud, friends
// ge:keywords services platform-services store backend steam steamworks game-center play-games
// ge:keywords achievement unlock stat store_stats leaderboard board score upload download blob
// ge:keywords ghost cloud cloud-save remote-storage quota rich-presence presence friends invite
// ge:keywords join overlay dlc ownership screenshot deck big-picture event poll request callback
// ge:skill services
// ge:test tests/services_sim_store_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "services/catalog.hpp"

namespace ge::services {

using UserId = uint64_t;     // the store's number for a player
using RequestId = uint32_t;  // a call's answer carries it; 0 is never a request
using BlobId = uint64_t;     // a blob kept with a leaderboard entry (a ghost); 0 is none

enum class Result : uint8_t {
  ok,
  offline,       // the store cannot be reached: try again later
  unknown_id,    // the id is not in the catalog
  invalid,       // a value the store refuses: out of range, lowered, too long, a bad name
  not_found,     // no such file, entry or blob
  over_quota,    // the cloud is full
  rate_limited,  // too many calls; try again later
  unsupported,   // this backend has no such service
  failed,        // the store answered with an error
};
std::string_view result_name(Result r);

// A friend's state as the store shows it.
enum class Presence : uint8_t { offline, online, away, busy };

struct UserInfo {
  UserId id = 0;
  std::string name;      // the player's display name
  std::string language;  // the store's language for the game ("english"), the game's default first
  std::string country;   // ISO 3166-1 alpha-2 of where the player is, "" when unknown
};

struct Friend {
  UserId id = 0;
  std::string name;
  Presence state = Presence::offline;
  bool in_this_game = false;
};

// Where the game runs, as the store's client knows it.
struct Environment {
  bool handheld = false;     // a handheld PC the store sells for (the Steam Deck)
  bool big_picture = false;  // the client's controller-first, full-screen mode
  bool overlay = true;       // the overlay can open over the game
  bool cloud = true;         // the player lets this game use the cloud
};

// Which entries a download asks for. `global` is ranks [first, last] (1-based); `around_user`
// is offsets from the player's own rank ([-4, 5] is four above to five below); `friends` is the
// player and every friend with a score, and ignores the range.
enum class BoardRange : uint8_t { global, around_user, friends };
// `keep_best` replaces the player's entry only with a better score; `force` always replaces it.
enum class UploadMode : uint8_t { keep_best, force };

inline constexpr size_t kMaxScoreDetails = 64;  // int32 values an entry may carry

struct BoardEntry {
  UserId user = 0;
  std::string name;
  int32_t rank = 0;  // 1-based
  int32_t score = 0;
  std::vector<int32_t> details;
  BlobId blob = 0;  // download it with `download_blob`
};

struct CloudFile {
  std::string name;
  uint64_t size = 0;
};

// A page of the store's overlay. `store` opens the given app's store page (the game's when 0);
// `url` opens a web page in the overlay's browser.
enum class OverlayPage : uint8_t {
  friends,
  players,
  achievements,
  stats,
  community,
  settings,
  store,
  url
};

enum class EventKind : uint8_t {
  stats_stored,          // store_stats answered (request, result)
  achievement_unlocked,  // the store took an unlock (id)
  score_uploaded,        // upload_score answered (id, score, rank, previous_rank, changed, blob)
  scores_downloaded,     // download_scores answered (id, entries, total)
  blob_downloaded,       // download_blob answered (blob, bytes)
  overlay_shown,         // the overlay opened: pause the game
  overlay_hidden,        // it closed: resume
  invite_received,       // a friend invited the player (user, text: the connect string)
  join_requested,        // the player chose to join a friend or accepted an invite (user, text)
  presence_changed,      // a friend's rich presence changed (user)
  screenshot_requested,  // the store's screenshot key, when the game hooks it
  connection_lost,       // the store went offline
  connection_restored,   // and came back
};
std::string_view event_name(EventKind k);

// One answer or notice. Only the fields its kind names are set.
struct Event {
  EventKind kind = EventKind::stats_stored;
  RequestId request = 0;
  Result result = Result::ok;
  UserId user = 0;
  std::string id;    // an achievement's or a board's id
  std::string text;  // a connect string
  int32_t score = 0;
  int32_t rank = 0;
  int32_t previous_rank = 0;
  int32_t total = 0;     // the board's entry count
  bool changed = false;  // the upload changed the player's entry
  BlobId blob = 0;
  std::vector<BoardEntry> entries;
  std::vector<uint8_t> bytes;
};

class Backend {
 public:
  virtual ~Backend() = default;

  // "sim", "offline" or a store's name ("steam").
  virtual std::string_view name() const = 0;
  // The pump: deliver every event due by `now_us` (the frame's time, microseconds, never going
  // back) into `out`, appended. Calls between two polls happen at the last poll's time.
  virtual void poll(int64_t now_us, std::vector<Event>& out) = 0;

  // ---- who and where
  virtual UserInfo user() const = 0;
  virtual AppId app() const = 0;
  virtual bool online() const = 0;
  virtual Environment environment() const = 0;

  // ---- achievements and stats (see the header: local at once, committed by store_stats)
  virtual Result unlock(std::string_view achievement) = 0;
  virtual Result achieved(std::string_view achievement, bool& out) const = 0;
  // The store's progress notice ("12 / 50") for a stat-driven achievement; nothing is stored.
  virtual Result show_progress(std::string_view achievement, uint32_t current, uint32_t max) = 0;
  virtual Result set_stat(std::string_view stat, double value) = 0;
  virtual Result add_stat(std::string_view stat, double delta) = 0;
  virtual Result stat(std::string_view stat, double& out) const = 0;
  virtual RequestId store_stats() = 0;
  // Every stat back to its initial value (and every achievement locked when asked): a developer's
  // reset, committed at once.
  virtual Result reset_stats(bool achievements_too) = 0;

  // ---- leaderboards. A blob (a ghost's tape, a replay) may ride with the entry; it is kept by
  // the store and fetched by anyone who downloads the entry.
  virtual RequestId upload_score(std::string_view board, int32_t score,
                                 std::span<const int32_t> details, UploadMode mode,
                                 std::span<const uint8_t> blob) = 0;
  virtual RequestId download_scores(std::string_view board, BoardRange range, int32_t first,
                                    int32_t last) = 0;
  virtual RequestId download_blob(BlobId blob) = 0;

  // ---- cloud files: the player's own, carried between their machines. Names are relative
  // paths of ASCII letters, digits and "._-/" (no "..", at most 255 characters).
  virtual Result cloud_write(std::string_view name, std::span<const uint8_t> bytes) = 0;
  virtual Result cloud_read(std::string_view name, std::vector<uint8_t>& out) const = 0;
  virtual Result cloud_delete(std::string_view name) = 0;
  virtual void cloud_files(std::vector<CloudFile>& out) const = 0;
  virtual void cloud_quota(uint64_t& total_bytes, uint64_t& available_bytes) const = 0;

  // ---- rich presence: key-value pairs friends see. Up to 20 keys; a key at most 64
  // characters, a value at most 256. "status" is the line the friends list shows; "connect" is
  // what a friend's join hands this game (a join_requested event's text).
  virtual Result set_presence(std::string_view key, std::string_view value) = 0;
  virtual void clear_presence() = 0;
  virtual std::string friend_presence(UserId user, std::string_view key) const = 0;

  // ---- friends, invites and the overlay
  virtual void friends(std::vector<Friend>& out) const = 0;
  virtual Result invite(UserId user, std::string_view connect) = 0;
  virtual Result open_overlay(OverlayPage page, std::string_view url = {}, AppId app = 0) = 0;

  // ---- ownership: the game and its DLC
  virtual bool owns(AppId app) const = 0;       // a licence (the game's own number, or a DLC's)
  virtual bool installed(AppId dlc) const = 0;  // its content is on this machine

  // ---- screenshots. `add_screenshot` files a picture with the store's library (rows of
  // `width` RGB bytes from the top). `hook_screenshots(true)` makes the store's screenshot key
  // a `screenshot_requested` event instead of the store's own capture, so the game shoots its
  // picture without the HUD.
  virtual Result add_screenshot(std::span<const uint8_t> rgb, int width, int height,
                                std::string_view caption) = 0;
  virtual void hook_screenshots(bool hook) = 0;
};

}  // namespace ge::services
