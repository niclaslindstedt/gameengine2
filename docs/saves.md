# Saves

How a game on this engine keeps a run to come back to: an autosave, a quicksave, a hundred
numbered slots, a save loaded, copied or deleted, the list of saves a menu shows with a picture
of each, and PART of a run carried somewhere else (a character taken out of one world and played
in another). The roadmap rows are M25a–M25h (`docs/goals.md`); this page is the design they build
to. Where a part is not built yet, this page is the plan. Change it in the same PR that changes
the plan.

## What a game asks of saves

| A game wants | What it is on this engine | Row |
| --- | --- | --- |
| A save file it can trust | Chunks behind a fixed header, every chunk checksummed and versioned; a damaged or newer file refused with a message naming the chunk | M25a |
| Saving without a hitch | The state described once and written to memory on the game thread in well under a frame; the disk written by a thread of its own | M25a, M25c |
| Loading in a blink | One read of the file, no parsing beyond copying fields; what the setup can rebuild (the map) is rebuilt, and then cached on disk (M25h) | M25b, M25h |
| A run saved and loaded that plays on as if never stopped | The contract every lab holds: loaded and stepped, the run has the digest of the run that never stopped | M25b |
| Autosaves, a quicksave, many slots, delete, copy | The save store: one directory, a slot a file, an autosave ring, an index so a list of a thousand saves is one small read | M25c |
| A save never lost to a crash or a full disk | Written to a temporary file, flushed, renamed over the old one; the last good save kept until the new one reads back | M25c |
| A picture of each save (optional) | A small picture chunk, from the CPU view the game draws headless or the renderer's last frame; or the save itself opened and drawn live | M25d |
| A character used in another world | A save is chunks, so a character is a chunk set in a file of its own, taken out of a run and put into another | M25e |
| Saves on every device | Each slot a cloud file of the platform services (`docs/services.md`), the index merged, a conflict kept as a copy | M25f |

## The one idea: a save is the state, never the inputs

A replay is the run's inputs (`docs/replays.md`), because the simulation is deterministic and a
few bytes a second replay any run. A save cannot be: loading a forty-hour game by stepping its
log again would take hours. So a save is the run's MUTABLE STATE written as bytes, and loading
is reading them back. Everything the setup can rebuild (the map generated from its seed, the
indices built over it, the lifts' plans) is not in the save: the save names its setup (the seed
and the options, exactly as the input log does), the loader rebuilds that, then reads the state
over it. A save is therefore small (Fall Line's whole run is a few hundred kilobytes in memory,
most of it the field's tapes), and writing one is a copy.

Because the simulation is deterministic to the bit (G1), a save has a check no ordinary save
system has: **a run saved at step s, loaded and stepped on, has at every later step the digest
of the run that was never saved.** Every lab of these rows holds it, and a field forgotten in a
save shows up as the first step where the two digests part (`ge diverge` names the field).

## The file (M25a)

Every number is little-endian and of a fixed width; a double is its IEEE 754 bits, so a save
written on one platform loads bit for bit on every other. The layout:

```
header       64 bytes: magic "GESAVE", the format's version, the file's kind (run,
             character, …), the game's name and save version, the chunk count, the
             directory's offset, the header's checksum
meta chunk   always first: what a menu lists (the title line, the place, the play time, the
             date, the setup), so listing a save reads the first few hundred bytes
picture      optional, second: the thumbnail (M25d)
chunks       the state, one chunk a part of the run (the player, the field, the crowd, …)
directory    at the end: for each chunk its tag, its instance, its version, its codec, its
             offset, its stored and its raw size, its checksum
```

- **A chunk** is a four-letter TAG (`PLYR`, `FLD_`, `CRWD`) and an instance number, so a game
  writes several of a kind (every racer, every machine) and a loader finds one without reading
  the rest. A loader skips a chunk it does not know, which is how a game that drops a system
  still loads saves that had it.
- **The checksum** is a 64-bit hash of the chunk's stored bytes (the XXH64 algorithm, written in
  the engine), checked on every load at several gigabytes a second. A file truncated, a byte
  flipped or a directory that points outside the file is refused, and the message names the
  file, the chunk and what was wrong ("chunk PLYR#0 at offset 812: checksum 9f…, expected 31…;
  the file is damaged, load the previous save").
- **A codec** per chunk: stored as is, or compressed with LZ4 (BSD-licensed, gigabytes a second
  to decode) when a measurement says the files are worth shrinking (the cloud's quota, M25f).
  M25a ships stored only and the codec field, so compression never changes the format.

## Describing the state once

A save written by one function and read by another drifts: a field added to one and not the
other is the classic save bug. So a type's fields are described ONCE, by one function template
beside the type (`engine/save/io.hpp`), and both directions walk it:

```cpp
template <class Io>
void describe(Io& io, Racer& r) {
  io(r.skier);                // a nested described type
  io(r.finished);
  io(r.gates);                // a vector: its length, then its items
  io.since(3, r.best, 0.0);   // a field added in chunk version 3; older saves read the default
  io.until(5, old_lap);       // dropped in version 5: read from older chunks, never written
}
```

The WRITER appends the fields' bytes to a buffer and the READER fills the fields from bytes, so a
field saved is a field loaded. A digest of exactly what is saved is the hash of the written
bytes. Numbers, enums, and arrays and vectors of them are copied in bulk with one `memcpy`; so
are structs a game marks `GE_SAVE_PLAIN(T)`, whose memory is exactly their described fields with
no padding (`plain_problem<T>()` checks it). Everything else is walked field by field. Nothing is
written by reflection or by name, so a save is as fast to read as the memory it fills. The
reader never reads past its bytes: a cut, a length longer than what is left or a bool that is
not 0 or 1 is its first problem, kept with the byte it happened at.

**Versions.** Each chunk carries its own version, the type's `describe` says from which version
a field exists (`since`) and until which it was read (`until`, for a field removed), and a change
too big for that branches on `io.version()`, the version of the chunk being read. A chunk NEWER
than the build is refused with both versions named. Note what a version does NOT promise: a
replay plays only on the behaviour it was recorded under, but a save loads under the new
behaviour and simply plays on from there, which is what a player of a patched game expects.

**References.** A pointer is never saved. What the setup rebuilds is shared and immutable (the
map); what refers to another part of the state refers by index, and the type's `describe` writes the
index. The engine walks vectors, strings, optionals, fixed arrays, pairs, enums and the
`core::Rng` state itself, so a game describes only its own structs.

## The run saved (M25b)

The play contract (`engine/play/game.hpp`) grows two calls beside `clone`:
`Run::save(save::Writer&) const` writes the run's chunks and `Run::load(save::Reader&)` reads
them into a run the setup has just opened; a game with no saves returns false from both. A
`GameSpec` names its save version. `ge save` drives them headless:

```sh
ge save write runs/fall_line-38.inputs --step=7200 --out=saves/at-60s.save
ge save inspect saves/at-60s.save      # the header, the meta, every chunk: tag, version, bytes, checksum
ge save load saves/at-60s.save --pilot --steps=2400   # loaded and stepped on; prints the digest
ge save diff a.save b.save             # chunk by chunk, then field by field through `state`
```

`ge bench` gains a `save` zone and a `load` zone, timed per call, so a save's cost is held in the
performance ledger (`docs/perf.md`) like a step's. Fall Line's chunks are its own (`games/fall_line`);
the state types of its plugins (the skier, the snowmobile, the helicopter, the paramotor) carry
their `describe` beside them in their plugins, so any game linking a plugin saves its state for free.

**Targets** (on the container, release build, to be measured by the lab and moved into
`docs/perf.md` when met): a whole Fall Line run saved to memory in under 1 ms; loaded, map
excluded, in under 2 ms; the file read and every checksum checked at over 2 GB/s.

## Slots and the store (M25c)

`save::Store` keeps one directory (the platform's per-user data directory, from
`engine/platform`: Application Support on Apple, `XDG_DATA_HOME` on Linux, Saved Games on
Windows, the app's sandbox on iOS and Android; any directory given, headless). A slot is a file:

- `auto-<n>`: the autosave ring, `n` of a size the game sets; a new autosave overwrites the oldest;
- `quick`: the quicksave;
- `slot-<n>`: the player's numbered slots, as many as the game allows;
- `index`: one small record a slot (the meta chunk and the picture's place), so a menu lists a
  thousand saves with one read. The index is a cache: missing or damaged, it is rebuilt from the
  slots' headers, and a slot the index does not name is still found.

Writing never risks the last good save: the bytes go to `<slot>.tmp`, are flushed to the disk,
read back and checked, then renamed over the slot (atomic on every platform), and the previous
version stays as `<slot>.bak` until the new one has read back. A crash at any byte leaves either
the old save or the new one, never half of each; the lab kills a write at every offset to prove
it. The game thread only serializes into a buffer; a writer thread does the disk, and the game
is told when the save is safe (an event the HUD turns into its "saved" mark).

Delete, copy and rename are slot operations on the store; an autosave is a store call with the
ring's next number. WHEN to autosave is the game's (Fall Line: at a run's foot, on leaving a
lift, every few minutes on the free ride); the engine gives the ring and a minimum spacing so a
burst of triggers writes once.

## Pictures of a save (M25d)

Optional, per game: a `PICT` chunk holding a small picture (a few hundred pixels wide) in QOI
(a lossless picture format with a one-page public specification, a few hundred lines to read and
write, faster than PNG and about its size). It comes from either source a game has:

- the run's CPU view (`Run::shoot`), which works headless and needs no GPU, so every lab and
  every test save has one;
- the renderer's last presented frame, read back without stalling (`engine/gpu/target.hpp`'s
  readback ring), when the game has a window.

Because a load is fast, a menu can also show a save LIVE: open it in the background, draw the
run as it stands, and let the camera drift. That is the game's choice; the engine's part is that
a load is cheap enough to do on a highlight.

## Part of a run: characters (M25e)

A save is chunks, so PART of a run is a chunk set. A game names which chunks make a character
(its stats, its outfit, its inventory, its skills) and the store keeps characters as files of
their own kind (`character` in the header), in the same format, read by the same tools:

- `save::extract`: a character's chunks copied out of a run's save (or written straight from the
  run, `Run::save_part`);
- `Run::import`: those chunks read into a run opened on ANOTHER setup (another seed, another map),
  replacing that run's own character and nothing else.

That is the shape of a game that keeps its heroes apart from its worlds: the character is a file,
the world is made new each time, and the character walks into it. Fall Line's is the skier's
profile (the outfit, the skis, the body and its injuries, the statistics), carried from one
mountain to another.

## Saves on every device (M25f)

With the platform services (M20): each slot is a cloud file of the same name, written after the
local one is safe, and the index is rebuilt on each device from the slots it has. A slot changed
on two devices since they last met is resolved by its play time and date, and the loser is kept as
`<slot>.conflict` for the player to choose from. The cloud's quota is when LZ4 earns its place.

## Fast loads: the content cache (M25h)

Reading a save is milliseconds; rebuilding its setup may not be. Fall Line's mountain is
generated from its seed, which takes far longer than reading the save. The content cache writes
what a setup builds (the compiled map and its indices) to disk once, keyed by the generator's
version and the setup, in a layout that is read with one read (or mapped) and used where it
lies: a second load of the same mountain skips the generator entirely. The cache is a cache:
missing, stale or damaged, it is rebuilt from the setup and the save still loads.

## Relation to replays and the network

- A KEYFRAME (`engine/replay/keep.hpp`) is an in-memory `clone`, cheaper than a save and never
  written; a save is the same moment as bytes. A replay file may START from a save (a replay of
  the last hour of a long game needs no log of the first forty).
- A late joiner to a spectated or networked run (M23f, M17f) needs a snapshot of the state; the
  replication may walk the same `describe` functions, so a type described for saves is described
  for the network.

## Labs

| Lab | Question | Row |
| --- | --- | --- |
| `save-file` | Does a file of every kind of field round-trip to the byte, is every truncation and flipped byte refused with the chunk named, do old chunk versions read with their defaults, and at how many GB/s are files written and read? | M25a |
| `save-run` | Saved at many steps and loaded, does Fall Line's run step on to the digests of the run never saved, and what do a save and a load cost, and how many bytes is each chunk? | M25b |
| `save-slots` | A thousand slots written, listed, copied and deleted: how long does the list take, and does a write killed at every offset leave the last good save loadable? | M25c |
| `save-picture` | A save's picture from the CPU view, headless, on a contact sheet of slots | M25d |
| `save-character` | A skier's profile taken from seed 38 into seed 7: the profile's fields equal, the rest of the run seed 7's own | M25e |
| `save-cloud` | Two devices on the simulated store: do their slots converge, and is a conflict kept? | M25f |

Every one runs headless on the GPU-less Linux container.
