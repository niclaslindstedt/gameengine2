---
name: save
description: "Use for anything about SAVES in gameengine2: engine/save (the save file's header, chunks, directory and XXH64 checksums; the state described once by describe(io, T&) for the Writer and the Reader; since and until; GE_SAVE_PLAIN), a game's or a plugin's state made savable, the save-file lab, `ge save inspect`, and the M25 rows (the run saved and loaded on the play contract, slots and the store, pictures, characters, cloud saves, the content cache)."
---

# Saves

`docs/saves.md` is the design and the plan; the rows are M25a–M25h in `docs/goals.md`. This skill
is the procedure. Read its lessons first: `make lessons ARGS=save`.

## The rules

- **A save is the state, never the inputs.** It holds what a step changes; what the setup
  rebuilds (the map, the indices over it) is shared and never saved. The meta's `setup` says
  what reopens it.
- **Describe a type once.** One `template <class Io> void describe(Io& io, T& v)` beside the
  type, in its namespace (argument-dependent lookup finds it). Never a separate save and load.
  A field a step writes and the describe leaves out is the classic save bug: the run saved and
  loaded parts from the run never saved at the first step that reads it.
- **Change a describe with a version.** Bump the chunk's version, read a new field with
  `io.since(v, field, fallback)`, an old one with `io.until(v, scratch)`; a bigger change
  branches on `io.version()`. Never change what an existing version reads.
- **Bulk where it is honest.** Numbers, enums and their vectors and arrays are one memcpy. A
  struct may be `GE_SAVE_PLAIN(T)` only when `plain_problem<T>()` is empty (no padding, every
  byte a described field, in order); put that check in the type's test.
- **No pointers in a save.** Refer by index; the shared, immutable parts are rebuilt by the setup.
- **Refusals are prompts.** A reader never throws and never reads past its bytes: it keeps the
  first problem with its byte, and `FileReader` names the file, the chunk and the next action.

## Making a type savable

1. Write its `describe` beside it (a plugin's type in its plugin, a game's in the game).
2. Pick its chunk: a four-letter `tag("XXXX")`, an instance (one per racer, per machine), a
   version starting at 1.
3. Test it: a value written and read back writes the same bytes (`save_io_test.cpp` is the
   pattern), and a plain type's `plain_problem` is empty.
4. For a run: saved, loaded and stepped on, the digest equals the run never saved (M25b's lab).

## Checking a file

```sh
make lab LAB=save-file                      # round trip, every cut and flip refused, versions, speeds
make lab LAB=save-file ARGS="--racers=256"  # a bigger state; time it on PRESET=release
build/debug/ge save inspect <file>          # the header, the meta and every chunk, or why it is refused
```
