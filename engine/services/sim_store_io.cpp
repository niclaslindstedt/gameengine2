// The simulated store saved to and loaded from a directory, and the offline store built on it.
// The directory holds `store.txt` (one record a line: the players, their friends, licences,
// stored stats and achievements, every leaderboard entry), `cloud/<player id>/<name>` (each
// player's cloud files as they are) and `blobs/<id>.bin`. A save writes `store.txt.tmp` and
// renames it over the old one, so a crash mid-save leaves the last good save. Presence, invites
// and queued answers belong to a session and are not kept.
//
// ge:summary The simulated store saved to a directory and read back; the offline store over it
// ge:keywords services sim store save load persist directory store-txt offline open_offline
// ge:keywords save_store load_store cloud-files blobs atomic-rename
// ge:skill services
// ge:test tests/services_offline_test.cpp
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "services/sim_store.hpp"
#include "services/sim_store_state.hpp"

namespace ge::services {

namespace fs = std::filesystem;

namespace {

constexpr const char* kHeader = "ge-services-store 1";

std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

bool write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(f);
}

bool read_file(const fs::path& path, std::vector<uint8_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool fail(std::string* error, std::string line) {
  if (error) *error = std::move(line);
  return false;
}

}  // namespace

bool save_store(const SimStore::Impl& s, const std::string& dir, std::string* error) {
  const fs::path root(dir);
  std::error_code ec;
  fs::create_directories(root, ec);
  std::ostringstream o;
  o << kHeader << "\n";
  o << "app " << s.catalog.app << "\n";
  o << "next " << s.next_user << " " << s.next_seq << " " << s.next_blob << "\n";
  for (const SimPlayer& p : s.players) {
    o << "player " << p.id << " " << (p.online ? 1 : 0) << " " << p.language << " "
      << (p.country.empty() ? "-" : p.country) << " " << p.name << "\n";
    const Environment& e = p.environment;
    o << "env " << p.id << " " << e.handheld << " " << e.big_picture << " " << e.overlay << " "
      << e.cloud << "\n";
  }
  // Every player before anything that names one, so a load meets each id before its records.
  for (const SimPlayer& p : s.players) {
    for (const UserId f : p.friends)
      if (p.id < f) o << "friend " << p.id << " " << f << "\n";
    for (const auto& [app, installed] : p.licences)
      o << "licence " << p.id << " " << app << " " << (installed ? 1 : 0) << "\n";
    for (size_t i = 0; i < s.catalog.stats.size(); i++)
      o << "stat " << p.id << " " << s.catalog.stats[i].id << " " << num(p.stats_stored[i]) << "\n";
    for (size_t i = 0; i < s.catalog.achievements.size(); i++)
      if (p.achieved_stored[i])
        o << "achieved " << p.id << " " << s.catalog.achievements[i].id << "\n";
  }
  for (size_t b = 0; b < s.boards.size(); b++)
    for (const SimEntry& en : s.boards[b]) {
      o << "entry " << s.catalog.boards[b].id << " " << en.user << " " << en.score << " " << en.seq
        << " " << en.blob << " " << en.details.size();
      for (const int32_t d : en.details) o << " " << d;
      o << "\n";
    }

  // The cloud files and the blobs first, so a store.txt never names what is not on disk.
  fs::remove_all(root / "cloud", ec);
  for (const SimPlayer& p : s.players)
    for (const auto& [name, bytes] : p.cloud)
      if (!write_file(root / "cloud" / std::to_string(p.id) / name, bytes))
        return fail(error, std::string("services: offline store cannot write cloud file '")
                               .append(name)
                               .append("' under ")
                               .append(dir)
                               .append("; next: check the directory is writable and has room"));
  for (const auto& [id, bytes] : s.blobs) {
    const fs::path path = root / "blobs" / (std::to_string(id) + ".bin");
    if (fs::exists(path, ec)) continue;  // a blob never changes once kept
    if (!write_file(path, bytes))
      return fail(error, "services: offline store cannot write blob " + std::to_string(id) +
                             " under " + dir + "; next: check the directory is writable");
  }
  const std::string text = o.str();
  const fs::path tmp = root / "store.txt.tmp";
  if (!write_file(tmp, std::vector<uint8_t>(text.begin(), text.end())))
    return fail(error, "services: offline store cannot write " + tmp.string() +
                           "; next: check the directory is writable and has room");
  fs::rename(tmp, root / "store.txt", ec);
  if (ec)
    return fail(error, "services: offline store cannot replace " + (root / "store.txt").string() +
                           " (" + ec.message() + "); next: check nothing holds the file open");
  return true;
}

bool load_store(SimStore::Impl& s, const std::string& dir, std::string* error) {
  const fs::path root(dir);
  const fs::path path = root / "store.txt";
  std::ifstream f(path);
  if (!f)
    return fail(error, "services: no store at " + path.string() +
                           "; next: save one first, or open the directory with open_offline");
  std::string line;
  int n = 0;
  const auto bad = [&](const std::string& what) {
    return fail(error, "services: store " + path.string() + " line " + std::to_string(n) + ": " +
                           what +
                           "; next: fix the line, or delete the directory to start afresh "
                           "(what it kept is lost)");
  };
  s.players.clear();
  s.blobs.clear();
  for (auto& b : s.boards) b.clear();
  while (std::getline(f, line)) {
    n++;
    if (n == 1) {
      if (line != kHeader) return bad("not a store file (expected '" + std::string(kHeader) + "')");
      continue;
    }
    std::istringstream in(line);
    std::string tag;
    in >> tag;
    if (tag == "app") {
      AppId app = 0;
      in >> app;
      if (app != s.catalog.app)
        return bad("saved for app " + std::to_string(app) + ", the catalog is app " +
                   std::to_string(s.catalog.app));
    } else if (tag == "next") {
      in >> s.next_user >> s.next_seq >> s.next_blob;
    } else if (tag == "player") {
      SimPlayer p;
      int online = 1;
      in >> p.id >> online >> p.language >> p.country;
      std::getline(in >> std::ws, p.name);
      if (!in && p.name.empty())
        return bad("a player line needs an id, online, language, country, name");
      if (p.country == "-") p.country.clear();
      p.online = online != 0;
      s.fresh_progress(p);
      s.players.push_back(std::move(p));
    } else if (tag == "env") {
      UserId id = 0;
      Environment e;
      in >> id >> e.handheld >> e.big_picture >> e.overlay >> e.cloud;
      if (SimPlayer* p = s.find(id))
        p->environment = e;
      else
        return bad("env for an unknown player");
    } else if (tag == "friend") {
      UserId a = 0, b = 0;
      in >> a >> b;
      SimPlayer* pa = s.find(a);
      SimPlayer* pb = s.find(b);
      if (!pa || !pb) return bad("a friend of an unknown player");
      pa->friends.push_back(b);
      pb->friends.push_back(a);
    } else if (tag == "licence") {
      UserId id = 0;
      AppId app = 0;
      int installed = 0;
      in >> id >> app >> installed;
      SimPlayer* p = s.find(id);
      if (!p) return bad("a licence of an unknown player");
      p->licences[app] = installed != 0;
    } else if (tag == "stat" || tag == "achieved") {
      UserId id = 0;
      std::string name, value;
      in >> id >> name >> value;
      SimPlayer* p = s.find(id);
      if (!p) return bad(tag + " of an unknown player");
      if (tag == "stat") {
        const int i = find_stat(s.catalog, name);
        if (i < 0) continue;  // a stat the catalog dropped
        p->stats[static_cast<size_t>(i)] = p->stats_stored[static_cast<size_t>(i)] =
            std::strtod(value.c_str(), nullptr);
      } else {
        const int i = find_achievement(s.catalog, name);
        if (i < 0) continue;
        p->achieved[static_cast<size_t>(i)] = p->achieved_stored[static_cast<size_t>(i)] = true;
      }
    } else if (tag == "entry") {
      std::string board;
      SimEntry en;
      size_t count = 0;
      in >> board >> en.user >> en.score >> en.seq >> en.blob >> count;
      if (!in || count > kMaxScoreDetails)
        return bad("an entry needs board, user, score, seq, blob, details");
      en.details.resize(count);
      for (int32_t& d : en.details) in >> d;
      const int b = find_board(s.catalog, board);
      if (b < 0) continue;  // a board the catalog dropped
      if (en.blob != 0 &&
          !read_file(root / "blobs" / (std::to_string(en.blob) + ".bin"), s.blobs[en.blob]))
        return bad("blob " + std::to_string(en.blob) + " is missing from blobs/");
      s.boards[static_cast<size_t>(b)].push_back(std::move(en));
    } else if (!tag.empty()) {
      return bad("unknown record '" + tag + "'");
    }
  }
  std::error_code ec;
  for (SimPlayer& p : s.players) {
    const fs::path cloud = root / "cloud" / std::to_string(p.id);
    if (!fs::is_directory(cloud, ec)) continue;
    for (const auto& file : fs::recursive_directory_iterator(cloud, ec)) {
      if (!file.is_regular_file()) continue;
      std::vector<uint8_t> bytes;
      if (!read_file(file.path(), bytes))
        return fail(error, "services: cannot read cloud file " + file.path().string() +
                               "; next: check its permissions");
      p.cloud[fs::relative(file.path(), cloud).generic_string()] = std::move(bytes);
    }
  }
  return true;
}

bool SimStore::save(const std::string& dir, std::string* error) const {
  return save_store(*impl_, dir, error);
}

bool SimStore::load(const std::string& dir, std::string* error) {
  return load_store(*impl_, dir, error);
}

Offline open_offline(const Catalog& catalog, const std::string& dir, std::string player_name,
                     std::string* error) {
  SimConfig config;
  config.latency_us = 0;
  config.save_dir = dir;
  Offline out;
  out.store = std::make_unique<SimStore>(catalog, config);
  std::error_code ec;
  if (fs::exists(fs::path(dir) / "store.txt", ec)) {
    if (!out.store->load(dir, error)) {
      out.store.reset();
      return out;
    }
  }
  if (out.store->users().empty()) {
    out.store->add_user(std::move(player_name));
    if (!out.store->save(dir, error)) {
      out.store.reset();
      return out;
    }
  }
  out.backend = out.store->backend(out.store->users().front());
  return out;
}

}  // namespace ge::services
