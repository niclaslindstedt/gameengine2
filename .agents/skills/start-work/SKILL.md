---
name: start-work
description: "Use before beginning any task in gameengine2, before edits. Confirms a clean tree, syncs with origin/main, sets up the porting references when the task ports, and names the docs/goals.md milestone row the task serves, then finds what already exists (make find, the skills' lessons)."
---

# Start work

Run this preflight before you start the task. You may read files to find your way
first, but make no edits until the preflight passes.

## 1. The tree

```sh
git status --short --branch
git branch --show-current
```

- A dirty tree is the user's work until you prove otherwise. Never reset, stash or
  delete it to get a clean tree. If you cannot tell who owns it, ask.
- If a merge or rebase is in progress, finish or abort it before anything else.

## 2. Sync

```sh
git fetch origin main
git rebase origin/main          # on a feature branch; on main: git merge --ff-only origin/main
```

If the branch's PR is already merged, restart the branch from `origin/main` and do not
stack new commits on merged history.

## 3. The references (porting tasks only)

A task that carries a module over, or regenerates a golden table, clones the read-only
reference sources beside the checkout: `porting/README.md` § The references has the
commands. Never commit, push or comment there.

## 4. The row

Open `docs/goals.md` and name the milestone row this task serves. If no row fits, the
task is probably out of scope (G4), so ask before you build it. A task that adds a NEW row claims
its number before anything else: `build/<preset>/ge goals claim "<what the row is>"` (`--under=M17`
for a letter under one) writes it to `docs/goals/reserved.md`, pushes it to your branch at once
and checks every branch, so two threads starting together never take the same number. Then load the skill
`AGENTS.md` routes the subject to, plus `write-code`.

## 5. What already exists

```sh
make find Q="<the task's nouns> <identifiers>"   # files, skills, lessons, the porting map
make lessons ARGS="<skill> --scope=<the dir you will edit>"     # per loaded skill
make labs                                                        # the labs of your subject
```

Run the subject's labs now with `--tag=before`, so the PR can show `--compare=before` (the
`labs` skill).

`make index` writes `docs/index.md`, every file with its summary (gitignored, so run it in your
checkout); `porting/map.md` says which reference
modules are already ported. Read the lessons of each skill you loaded before the first edit (the
`skill-reflection` skill, OPEN).

## 6. The build works before you touch it

```sh
make test
```

If it is red before your change, that red is the first thing to fix or report.
