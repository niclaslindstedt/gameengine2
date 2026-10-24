---
type: Added
title: ge lab publish puts a lab's run on one page to share
---

`ge lab publish <id>` writes `previews/<id>/publish.html`, and `ge lab publish <a> <b>` writes several labs one after another to `previews/publish.html`. It is one self-contained page holding each run: every picture without its caption band, shrunk to `--width` (720 px by default) and inlined as a 256-colour GIF, then its title, caption and what to look for, the metrics, the tables and the notes. With `--before=<tag>`, the kept run (`ge lab <id> --tag=<tag>`) stands beside each picture, and each metric gets its before value and its change. The page carries its own light and dark colours and needs no outside file, so it can be published as it is, and the command says when the page is past 15 MB. The GIF writer is `ge::lab::encode_gif`, with `ge::lab::shrink` for the box-filter downscale. It keeps every colour of a picture that has 256 or fewer, and median-cuts the rest.
