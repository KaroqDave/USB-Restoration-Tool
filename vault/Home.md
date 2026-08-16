---
tags:
  - moc
  - project
---

# USB Restoration Tool

A Qt 6 desktop app that restores an ISO-written USB drive to **one GPT or MBR partition formatted exFAT**, labelled `USB`. It talks to each platform's storage stack directly — no `diskpart`, no PowerShell, no `sfdisk`.

| | |
|---|---|
| Version | **1.3.0** |
| Platforms | Windows, Linux |
| License | MIT |
| Notes | `vault/` in this repository |
| GitHub | [KaroqDave/USB-Restoration-Tool](https://github.com/KaroqDave/USB-Restoration-Tool) |

Restoring **erases the selected disk**. Only USB disks are listed; boot, system, offline, and read-only disks are refused. The drive you pick is still wiped completely.

## Status

- **Windows** restore path has been verified on hardware.
- **Linux** has never restored a physical USB stick. The polkit helper path has not been exercised at all. Treat it as unverified. See [[Hardware Testing]] and [[TODO]].

## Map of content

- [[TODO]] — what to do next
- [[Architecture]] — modules, `DiskService`, the helper split
- [[Safety]] — refusals, identity checks, trust boundary
- [[Windows]] — WMI, `DeviceIoControl`, UAC
- [[Linux]] — sysfs, the helper, AppImage vs install
- [[Development]] — build, test, style
- [[Releases]] — versioning, tags, release notes
- [[Decisions]] — why the design is the way it is
- [[Hardware Testing]] — disposable-stick matrix
- [[Inbox]] — scratch
- [[Vault]] — how this vault relates to the repo

## For agents

Repo docs (`README.md`, `SECURITY.md`, `AGENTS.md`, `docs/`) are the published source of truth. This vault is working memory: tasks, hardware logs, decisions.

When you finish work, update [[TODO]]. When you make a design choice that is not already in [[Decisions]], add it. Do not copy the README into these notes.
