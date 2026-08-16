---
tags:
  - meta
---

# Vault

This Obsidian vault lives in `vault/` at the repository root. Open that folder as a vault in Obsidian.

Published docs stay in the repo (`README.md`, `SECURITY.md`, `AGENTS.md`, `docs/`). Notes here are tasks, hardware logs, and decisions. Do not duplicate the README.

## Layout

| Note | Role |
|---|---|
| [[Home]] | Dashboard and map of content |
| [[TODO]] | The task list |
| [[Architecture]] [[Safety]] [[Windows]] [[Linux]] | How it is built and what it will not do |
| [[Development]] [[Releases]] | How to build and ship |
| [[Decisions]] | Why |
| [[Hardware Testing]] | Disposable-stick log |
| [[Inbox]] | Capture; file later |
| `Templates/` | Daily, decision, hardware test, task |
| `Daily/` | Daily notes |

## For Cursor / other agents

`.cursor/rules/obsidian-vault.mdc` points here. Paths are repository-relative (`vault/TODO.md`), never machine-specific.

When working on the project:

1. Read [[TODO]] if the work might already be listed
2. Check off or add items when the task list changes
3. Log hardware restores in [[Hardware Testing]]
4. Add a [[Decisions]] entry only for a choice that is not already there
5. Keep wiki links (`[[Note]]`) so the graph stays useful
