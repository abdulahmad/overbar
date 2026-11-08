# How Overbar work is run (the harness)

This is for any agent (Claude Code, Cursor, Codex) or person working on Overbar or on a port's host shell that uses it.
It describes how the work is set up and checked, not where any one piece of work stands.

## 1. Repos

| Repo | Overbar's place | Branch that takes the work |
|---|---|---|
| overbar | the library: overlay, menu bar, picture shape, looks | `main` |
| NHL94Genesis-C-Port | submodule at `native/overbar` | `cursor/overbar-genesis-6801` |
| NHL94SNES-C-Port | submodule at `platform/overbar` | `cursor/overbar-snes-6801` |
| NHL95PC | submodule at `port/overbar` | `cursor/overbar-95pc-6801` |
| NHL96PC | submodule at `port/overbar` | `cursor/overbar-96pc-6801` |
| NHL94PC | submodule at `port/overbar` | `cursor/overbar-94pc-6801` |

## 2. Ground rules

| Rule | What it means |
|---|---|
| Merge it yourself | Work happens on the thread's own branch, then goes straight into Overbar `main` and each port's overbar branch, with no PRs. Use a merge commit when a port's branch has commits of its own |
| Bump every port | An Overbar change ends with a submodule bump commit in each port that builds against it, made only after the library's checks pass |
| PulseMan stays as it is | Never change `ext/pulseman` in the Genesis port, and never push to 1nputs/pulseman |
| Nothing generated in git | No ROMs, save states, captures, screenshots, GIFs or golden files. The README's sample pictures are the exception Abdul asked for. The MiSTer filter files live once, in Overbar's `mister/`; ports copy them at build time and keep no copy of their own |
| No line-ending rewrites | Don't commit a file whose only change is its line endings |
| Game code doesn't move | Overbar work changes host shells only. Game code (`src/`, `game/`) and frame hashes stay the same |
| Keys stay | `` ` `` and F11 open the overlay, F12 shows the stats, Alt+Enter toggles fullscreen |
| Builds work as checked out | `-Werror` is for CI only. Don't fix pre-existing warnings or the known ucrt64 failures as part of other work |
| Credit | Overbar is a derivative of 1nputs' PulseMan. A derivative of Overbar carries the credit block in the README |

## 3. Gates (programs that can fail)

A change passes on what a program says.

| Gate | Command | Fails when |
|---|---|---|
| Library | `make test` and `tests/check.sh` in overbar | a test fails, or the library stops building without SDL |
| Genesis | `make` (game target `build/nhl94.exe` on Windows); `make test` when anything outside `native/` changes | the build fails, or a frame hash changes |
| SNES | `make`; `test/check.sh ROM` when anything outside `platform/` changes | the build fails, or a frame hash changes |
| PC ports | `sh port/build-win.sh` (mingw32 for 95 and 96 PC, ucrt64 for 94 PC) | the build fails |

## 4. Checks on Abdul's machine

The cloud has no ROMs, no Wine and no display. Anything you can see or hear (menus, looks, sound, online play) is checked on Abdul's
machine through a Remote Control session. The session does these steps:

1. Pull the branch.
2. Run `git submodule update`.
3. Build.
4. Run the game.
5. Report what it saw and heard.

It stashes local changes rather than discarding them. It makes no source edits and commits nothing. A fix found there is made in
the cloud and pushed, then pulled again.

## 5. How work is cut and run

| Kind of work | Who does it |
|---|---|
| A change to Overbar's API (`include/overbar/overbar.h`, shared behaviour) | the lead, once, first |
| One port's shell wiring, after the API is in | a subagent per port, run in parallel |
| Builds and bumps for every port | in parallel, one per port |

Check the state of each repo (`git status`, `git log -1`) before redoing anything after a restart.

A unit that fails a gate goes back with the check's output. After the same failure twice, ask the advisor before the third try.

## 6. Model tiers

| Job | Model |
|---|---|
| Searching, builds, bumps, logs | Haiku or Sonnet |
| Per-port wiring, menus, settings | Sonnet |
| API design, and anything with state or timing (netplay, threads, audio) | Opus |

## 7. Approval

These steps go ahead without asking, because they can be undone:
- branches
- pushes to the branches above
- builds in the cloud
- a Remote Control session that builds and runs

These steps wait for Abdul:
- changes on his machine outside the repos
- anything sent outside the project
