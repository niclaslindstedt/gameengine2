// THE `play` EXECUTABLE: a game's play target is its GE_GAME file, its simulation and a `main`
// that calls `play_main`. `ge play`, `ge sim`, `ge replay`, `ge state`, `ge diverge`, `ge shot`,
// `ge bench`, `ge profile`, `ge mem` and `ge load` build it and run it; it can be run by hand too:
//
//   play [--stdio] [--seed=S] [--<option>=V] [--inputs=F]   the line protocol on stdin/stdout;
//                                                           --inputs writes the log at the end
//   play describe                                           the game: axes, options, step rate
//   play sim [--seeds=A,B] [--human] [--<option>=V]         the pilot over each seed; logs in DIR;
//            [--out=DIR]                                    --human: how human each run looks
//   play replay <log>                                       the log played back to its digest
//   play state <log> [--step=N]                             the state after N steps (default all)
//   play diverge <log-a> <log-b>                            the first step two runs part
//   play bench [--seeds=A,B | <log>...] [--repeats=5]       each step timed, per zone, with its
//              [--steps=N] [--<option>=V] [--out=DIR]       allocations (bench.hpp); the series
//                                                           and spans files in DIR
//   play load [--seeds=A,B] [--steps=1200]                  how the run uses the machine
//             [--paced=1] [--bin-ms=10] [--out=DIR]         (load.hpp): every thread's lanes,
//                                                           the cores, the critical path; the
//                                                           lanes, bins and charts in DIR
//   play shot [<log> | --seed=S] [--step=N] [--view=V]      a still of the run at step N (the end
//             [--<option>=V] [--out=DIR]                    by default): the view framed with its
//                                                           caption band, a PNG in DIR
//   play why <log> [--agent=N] [--step=S] [--around=K]       why a bot did it (why.hpp): agent N's
//            [--decision=D] [--flips] [--close=X]           decisions at steps S-K..S+K; --flips
//            [--timeline] [--out=DIR]                       every change of choice (and margins
//                                                           under X); --timeline its series in DIR
//   play record [<log> | --seed=S] [--view=V]               the run as a video (record.hpp): the
//               [--from=N|Ns] [--to=N|Ns] [--fps=30]        view at each frame of --fps, the run
//               [--speed=1] [--size=WxH] [--format=mp4]     at --speed, encoded by ffmpeg (mp4,
//               [--codec=C] [--crf=N | --bitrate=4M]        webm, gif; png writes the frames) and
//               [--encoder-preset=P] [--thumbs=24]          a contact sheet of --thumbs stills,
//               [--band=0] [--<option>=V] [--out=DIR]       each with its time and step, in DIR
//   play cvar ls [<prefix>] | get <name>...                 the game's cvars (console_cmds.hpp)
//   play cfg check|exec <file>... [--server] [--cmd=C]      cfg files run against them: problems,
//   play cfg dump                                           what changed; every player setting
//
// Every command but the protocol prints exactly one JSON object on one line. A problem prints
// `{"error":...,"where":...,"next":...}`; the exit code is 0 done, 1 a problem (a replay that
// misses its digest, two runs that diverge), 2 a usage error.
//
// ge:summary The play executable's main: protocol, describe, sim, replay, state, diverge, bench
// ge:summary shot, record and why
// ge:keywords play main executable command-line describe sim replay state diverge bench
// ge:keywords play_main why human record video cvar cfg load
// ge:skill agent-interface
// ge:test tests/play_session_test.cpp
#pragma once

#include <iosfwd>

namespace ge::play {

// The `play` command line over the registered game (game.hpp's GE_GAME).
int play_main(int argc, char** argv);

// The same over explicit streams (the tests' way in).
int play_main(int argc, char** argv, std::istream& in, std::ostream& out);

}  // namespace ge::play
