# Platform services

What a store's client gives a game, and how a game on this engine uses it: who is playing,
achievements, stats, leaderboards, cloud saves, rich presence, friends and invites, the store's
overlay, lobbies and the store's network, the workshop, the store's controller layer, ownership
of the game and its DLC, screenshots, and the handheld PC a store sells for. The design
`engine/services` is built to, the decisions behind it, how Steam maps onto it call by call, and
the order it lands in (`docs/goals.md` M20). Where a part is not built yet, this page is the
plan. Change it in the same PR that changes the plan.

## What a game asks of a store

Fall Line, the demo game, needs the store for the things a player expects of a game bought on
it, and every one of them already exists in the game in a store-less form:

| The game has | On a store it becomes |
| --- | --- |
| Medals and challenges | Achievements, some unlocked by a counter reaching a threshold |
| The statistics page (runs, metres, top speed, falls) | Stats the store keeps, feeding the achievements |
| The record book and the ghosts | Leaderboards, with the ghost's tape riding on each entry |
| Settings, the record book and the ghosts carried between devices | Cloud files, merged by the game's own rules |
| The shutter (a screenshot without the HUD) | The store's screenshot library, the store's key hooked |
| A free ride with friends (M17k) | Rich presence, invites, join from the friends list, lobbies, the store's relay |
| The pause on focus loss | The pause when the store's overlay opens |
| The keys, the gamepad | The store's controller layer and its glyphs (with `engine/input`, M19) |

So the engine owns one interface that says all of that in no store's words, and each store is a
backend behind it. A game written against the interface runs on Steam, on a build sold outside
any store (its progress kept on disk), and headless in a test, with no change.

## Decisions

- **One interface, many backends.** `ge::services::Backend` (`engine/services/backend.hpp`) is
  the whole surface. A backend is chosen once at start-up. It is an abstract class: the
  services are never on a hot path (a game calls them a few times a run), so the clarity of one
  interface is worth a virtual call.
- **Answers are events, pumped once a frame.** A call a store answers locally returns a
  `Result`; a call that goes to the store's servers returns a `RequestId`, and its answer is an
  `Event` carrying the id. So is everything the store starts on its own (an invite, the overlay
  opening, the screenshot key). `poll(now_us, events)` on the main thread is the one pump:
  nothing arrives from another thread or between polls. That is Steam's own callback model
  (`SteamAPI_RunCallbacks`) made plain data, and it is what makes a session on the simulated
  store a pure function of its script and its clock.
- **The catalog is stated once, in the game.** Every achievement, stat, leaderboard and DLC is a
  row in the game's constant tables (`services/catalog.hpp`). Every backend holds the game to
  it, and `check_catalog` names every fault before a store ever sees it. A store's partner
  configuration must name the same ids; the Steam backend checks the two agree at start-up
  (M20b).
- **Stats are local, then committed.** `unlock`, `set_stat` and `add_stat` change the local value
  at once; `store_stats` commits them and its event says whether the store took them, with an
  `achievement_unlocked` for every unlock the commit made (by the game or by a stat's
  threshold). That is how every store with progress stats behaves, and it tells a game to
  commit at a pause rather than every frame.
- **Leaderboards carry blobs.** A score may carry up to 64 integers of detail and a blob (a
  ghost's tape, a replay) that anyone who downloads the entry can fetch. On Steam that is a cloud
  file shared and attached to the entry; the game never sees those steps.
- **The simulated store is a real store, held in memory.** `SimStore` keeps many players, their
  friends, licences, stats, boards, blobs, cloud files and presence; answers arrive after a
  latency (and a jitter off a seed) on the clock the polls pass in; a connection can be cut and a
  call staged to fail with any result; what a player does in the store's client (the screenshot
  key, accepting an invite, joining from the friends list) is a call on the store. It refuses
  what a real store refuses (an unknown id, a stat out of range or lowered when it may only grow,
  a jump past its `max_change`, a cloud write over the quota, a bad file name, too many presence
  keys), so a mistake fails in a test, not on a store's servers.
- **The offline store is the simulated store, saved.** One player, no latency, written to a
  directory after every committed change (`open_offline`). A build sold outside a store keeps
  its achievements, stats, best scores and cloud files there, and a game never branches on
  whether a store is present.
- **The store's SDK never enters the tree.** A store's SDK is licensed to its developers and may
  not be redistributed as source, and this repository is public. The SDK is supplied at build
  time from a path (`-DGE_STEAMWORKS_SDK=<dir>`), never committed, and CI downloads it from a
  location kept in a GitHub secret. Everything else (the interface, the simulated and offline
  stores, every test and lab) builds and runs without it on the GPU-less Linux container. Only
  the store's runtime library is shipped, beside the game's executable, as its licence allows.
- **The engine owns the mechanism; the game owns the words.** The engine says achievement, board,
  presence; Fall Line says which medals are achievements, which record is which board, what
  its presence line reads (`games/fall_line`, M20g).
- **Engine, not plugin.** The services are what any game of any genre uses, so they live in
  `engine/services` and a store's backend in `engine/<store>` (`engine/steam`), built only when
  its SDK is supplied, the way `engine/gpu` is built only with Dawn.

## The interface

| Group | Calls | Events |
| --- | --- | --- |
| Who and where | `user`, `app`, `online`, `environment` (a handheld, big-picture mode, the overlay, the cloud) | `connection_lost`, `connection_restored` |
| Achievements and stats | `unlock`, `achieved`, `show_progress`, `set_stat`, `add_stat`, `stat`, `store_stats`, `reset_stats` | `stats_stored`, `achievement_unlocked` |
| Leaderboards | `upload_score` (details, a blob, keep-best or force), `download_scores` (global, around the player, friends), `download_blob` | `score_uploaded`, `scores_downloaded`, `blob_downloaded` |
| Cloud files | `cloud_write`, `cloud_read`, `cloud_delete`, `cloud_files`, `cloud_quota` | |
| Rich presence | `set_presence`, `clear_presence`, `friend_presence` | `presence_changed` |
| Friends and the overlay | `friends`, `invite`, `open_overlay` | `invite_received`, `join_requested`, `overlay_shown`, `overlay_hidden` |
| Ownership | `owns`, `installed` | |
| Screenshots | `add_screenshot`, `hook_screenshots` | `screenshot_requested` |

Lobbies, the store's network, the workshop and the controller layer are rows of their own
(M20c–M20e) and join the interface when they land.

## Steam, call by call

How each Steamworks interface maps onto the engine. The Steam backend (M20b onwards) is the only
code that includes Steamworks' headers.

| Steamworks | The engine | Row |
| --- | --- | --- |
| `SteamAPI_RestartAppIfNecessary`, `SteamAPI_InitFlat`, `SteamAPI_Shutdown` | The Steam backend's open and close: relaunched through Steam when started outside it, a clear line when Steam is not running | M20b |
| `SteamAPI_RunCallbacks`, call results | `Backend::poll`: every callback and call result becomes an `Event` | M20b |
| `ISteamUser` (the player's id, logged on), `ISteamFriends` persona name, `ISteamApps` language | `user`, `online` | M20b |
| `ISteamUtils` (`IsSteamRunningOnSteamDeck`, `IsSteamInBigPictureMode`, `IsOverlayEnabled`, overlay notification position), `ISteamRemoteStorage::IsCloudEnabledForAccount/App` | `environment` | M20b |
| `ISteamUserStats` achievements (`SetAchievement`, `GetAchievement`, `IndicateAchievementProgress`, `ClearAchievement`) | `unlock`, `achieved`, `show_progress`, `reset_stats` | M20b |
| `ISteamUserStats` stats (`SetStat`, `GetStat`, `StoreStats`, `ResetAllStats`; `UserStatsStored_t`, `UserAchievementStored_t`) | `set_stat`, `add_stat`, `stat`, `store_stats`; `stats_stored`, `achievement_unlocked` | M20b |
| `ISteamUserStats` leaderboards (`FindLeaderboard` at start-up, `UploadLeaderboardScore`, `DownloadLeaderboardEntries`, `GetDownloadedLeaderboardEntry`, `AttachLeaderboardUGC`) | `upload_score`, `download_scores`; the board's handle found once per catalog row | M20b |
| `ISteamRemoteStorage` (`FileWrite`, `FileRead`, `FileDelete`, `GetFileCount`/`GetFileNameAndSize`, `GetQuota`; `FileShare` and `UGCDownload` for a board's blob) | `cloud_*`, the blob behind `upload_score` and `download_blob` | M20b |
| `ISteamFriends` rich presence (`SetRichPresence`, `ClearRichPresence`, `GetFriendRichPresence`; `FriendRichPresenceUpdate_t`, `GameRichPresenceJoinRequested_t`) | `set_presence`, `clear_presence`, `friend_presence`; `presence_changed`, `join_requested` | M20b |
| `ISteamFriends` friends and overlay (`GetFriendCount`/`GetFriendByIndex`, `GetFriendPersonaState`, `GetFriendGamePlayed`, `InviteUserToGame`, `ActivateGameOverlay*`; `GameOverlayActivated_t`) | `friends`, `invite`, `open_overlay`; `overlay_shown`, `overlay_hidden` | M20b |
| `ISteamApps` (`BIsSubscribedApp`, `BIsDlcInstalled`, `GetDLCCount`/`BGetDLCDataByIndex`; `DlcInstalled_t`) | `owns`, `installed` | M20b |
| `ISteamScreenshots` (`WriteScreenshot`, `HookScreenshots`, `SetLocation`; `ScreenshotRequested_t`) | `add_screenshot`, `hook_screenshots`; `screenshot_requested` | M20b |
| `ISteamMatchmaking` lobbies (create, join, list with filters, lobby data, member data, chat, invites; `LobbyEnter_t`, `LobbyDataUpdate_t`, `LobbyChatUpdate_t`, `GameLobbyJoinRequested_t`) | A store's lobby directory under `engine/net`'s session lobby: a session found, joined and invited to through Steam, its lobby kept by `engine/net` | M20c |
| `ISteamNetworkingSockets`, `ISteamNetworkingMessages`, the Steam Datagram Relay | One more `engine/net` transport kind beside UDP, TCP and the simulated network: a peer addressed by its Steam id, reached through Steam's relay, with no NAT work of the game's own | M20c |
| `ISteamUser::GetAuthTicketForWebApi`, `BeginAuthSession` | A host trusting a player's store identity; a store account linked to the game's own account service (M17j) | M20c |
| `ISteamGameServer` | A dedicated server listed and authenticated by Steam | M20c |
| `ISteamUGC` (query, subscribe, download, install folders, create and submit an item) | The workshop: mods, maps, outfits; items mounted as the game's content | M20d |
| `ISteamInput` (action sets, digital and analog actions, origins and glyphs, rumble, the configurator) | A backend of `engine/input` (M19): its actions and action sets are the engine's action maps, its glyphs the prompts the HUD draws | M20e |
| `ISteamUtils::ShowFloatingGamepadTextInput`, the Deck's suspend and resume, `ISteamTimeline` (events and game modes on the recording timeline), `ISteamRemotePlay` | The handheld and the extras: the on-screen keyboard, the game's moments marked on Steam's recording, playing together remotely | M20f |
| `ISteamInventory`, `ISteamMicroTxn`, `ISteamHTMLSurface`, `ISteamMusic`, `ISteamParentalSettings`, `ISteamVideo` | Not planned: no game on the engine sells items or plays the player's music; a row is added when one does | none |

Other stores fit the same interface: Game Center on iOS, iPadOS and the Mac App Store, and
Google Play Games on Android (M20h, with the store builds of M11).

## The SDK, the secrets and the build

- `GE_STEAM` (a CMake option, off by default) builds `engine/steam`. It needs
  `GE_STEAMWORKS_SDK`, the path of an unpacked Steamworks SDK (the directory holding
  `public/steam` and `redistributable_bin`); a build asked for Steam without it stops with a line
  saying where to get the SDK and how to pass it.
- On CI, one job builds with Steam when the repository secret `STEAMWORKS_SDK_URL` (where a
  maintainer keeps the SDK's archive) is set, and is skipped when it is not, so a fork builds
  green. The archive is never cached into an artifact or a release.
- The runtime library (`steam_api64.dll`, `libsteam_api.so`, `libsteam_api.dylib`) is copied
  beside the executable by `ge package` (M12c) for a Steam build only. `steam_appid.txt`, which a
  developer's run outside Steam needs, is written into the build directory from the catalog's
  app id and never committed.
- Nothing about the game's Steam app (its id, its partner configuration, keys) is a secret, but
  credentials for publishing builds to Steam (the build account) are, and live only in GitHub
  secrets when a release workflow uploads to Steam.

## Testing without a store

- Every call is tested on the simulated store (`tests/services_sim_store_test.cpp`), the offline
  store's directory round trip (`tests/services_offline_test.cpp`) and the catalog's check
  (`tests/services_catalog_test.cpp`).
- The `services` lab plays a game's whole use of the services by four players on one script and
  tables every event, the store's journal and how long each kind of answer took
  (`make lab LAB=services ARGS="--latency-ms=200 --jitter-ms=100"`).
- The Steam backend is compiled on CI when the SDK secret is set. Its live check (a real Steam
  client logged in, the overlay, an achievement's pop-up) needs a machine with Steam running:
  a `needs-mac` issue with the exact commands, run on the reference Mac (Steam runs there too).

## The order it lands in

| Row | What |
| --- | --- |
| M20a | The interface, the catalog, the simulated and offline stores, the `services` lab |
| M20b | The Steam backend: the SDK supplied, every M20a call mapped, the CI job behind the secret, the live check |
| M20c | Lobbies, the store's relay as an `engine/net` transport, auth tickets, game servers (with M17) |
| M20d | The workshop |
| M20e | Steam Input as an `engine/input` backend (with M19) |
| M20f | The handheld and the extras: the on-screen keyboard, suspend and resume, the timeline, Remote Play Together |
| M20g | Fall Line on Steam: its catalog, its record book as boards with ghosts, its cloud save, its presence |
| M20h | Game Center and Google Play Games (with M11) |
