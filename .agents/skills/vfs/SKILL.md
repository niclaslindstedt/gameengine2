---
name: vfs
description: "Use for engine/vfs in gameengine2: content files and archives, today the pack archive (pak: its checked directory, a file as a view into the archive, the fixture writer); later search paths, zip archives (pk3), directories and palette pictures (docs/goals.md M18a). Load it to read a file out of an archive, add an archive kind, or when a pack is refused."
---

# Content files

`engine/vfs/` (`ge_vfs`, namespace `ge::vfs`) finds a game's content files where they are
shipped. Today it reads the classic pack archive; the rest of the virtual file system (search
paths in order, zip archives, directories, lookup that does not allocate) is roadmap row M18a,
whose status file (`docs/goals/M18a.md`) says what has landed.

| File | What |
| --- | --- |
| `vfs/pak.hpp/.cpp` | The pack archive: `read_pak` (the directory, every entry checked), `pak_find`, `pak_file` (a view into the archive, never a copy), `write_pak` (fixtures) |

## Rules

- **Check the whole directory before handing anything back.** An archive is untrusted input: the
  magic, the directory inside the file, whole entries, terminated paths, and every file inside the
  archive are checked in `read_pak`, so nothing after it can read out of range. A refusal names
  the entry and the byte offset and says what to do (`docs/architecture.md` § Diagnostics).
- **A file is a view.** `pak_file` returns a span into the archive's bytes; the caller keeps the
  archive alive. Copy only when the bytes must outlive it.
- **No content in the tree.** Tests and labs make their own archives with `write_pak`. A lab may
  open an archive the user passes at run time (`labs bsp --pak=<file> --map=maps/<name>.bsp`);
  nothing from such an archive is committed, and no doc or lesson names the game it came from.

## Tests and labs

`tests/vfs_pak_test.cpp` (a round trip, every damage refused). The `bsp` lab's `--pak` reads its
maps and the palette out of a pack (`brush` skill).
