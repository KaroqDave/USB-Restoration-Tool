# CLAUDE.md

**`AGENTS.md` is the guidance for this repository. Read it before changing
anything.** It covers the module layout, the build and test commands, the
coding style, what a change to a safety rule owes in tests, and the reasoning
behind the privilege split. This file exists so the instructions are not
written twice — it adds nothing to AGENTS.md except the reminder below.

## The rules that are absolute

These are restated here, not relocated. AGENTS.md is still where they are
explained; a restatement is cheap and the cost of missing one is not.

- **No AI attribution anywhere in this repository.** No `Co-Authored-By:`
  trailer naming an assistant, no "Generated with" footer, no tool byline — in
  commits, pull requests, generated files, code comments or release notes. The
  author of a commit is the person who owns the work. Assistant settings are
  not checked in, so nothing in the repository turns the trailers off for you:
  set `attribution.commit` and `attribution.pr` to empty strings in your own
  configuration, and check the message before committing regardless — a
  setting is not a substitute for looking.
- **Do not weaken the destructive path.** No bypass for the acknowledgement
  dialog or the identity check, no GUI command-line flag that starts a restore,
  no widening of the USB bus-type check.
- **`usb-restoration-helper` may only refuse on its arguments.** It re-derives
  every fact it acts on. A change that lets an argument decide something —
  above all the bus type — removes the reason the binary is allowed to exist.
- **A safety-rule change is not done until `tests/test_core.cpp` covers the
  case it now refuses**, and a new partition layout is not done until
  `scripts/verify-partition-tables.sh` has a case for it.

## Working notes

`vault/TODO.md` is the task list — read it when planning, update it when a task
is added, finished or blocked. Log physical restores in
`vault/Hardware Testing.md`. Repo files stay the published source of truth; do
not copy README, SECURITY.md or `docs/` into the vault.
