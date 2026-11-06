# Repository Guidelines

## Project Structure & Module Organization

USB Restoration Tool is a Qt 6 desktop app for Windows and Linux that restores
an ISO-written USB drive to one GPT or MBR partition formatted exFAT, FAT32,
NTFS, or (on Linux) ext4. Source lives in `src/`: `core/`, `platform/`, `win/`,
`linux/`, `helper/` and `gui/`.

Module responsibilities, the test layout and the build, test and packaging
commands are in [docs/development.md](docs/development.md#project-structure).
Read it before changing a boundary; this file holds the rules that apply to
every change.

## Build, Test, and Development Commands

Configure, build, test and package with the `windows` and `linux` presets, as
described in [docs/development.md](docs/development.md#build). Formatting uses
clang-format 22.1.8, the version CI checks against:

```sh
git ls-files '*.cpp' '*.h' | xargs clang-format -i
```

## Coding Style & Naming Conventions

Four-space indent, 120-column limit, `.clang-format` is authoritative. Files are
`snake_case.cpp` / `snake_case.h`; types are `PascalCase`; functions and
variables are `camelCase`; members carry an `m_` prefix. Everything lives in
`namespace usbrestore`.

Platform calls report failure through a `QString *error` out-parameter and a
`bool` return, never an exception. Error text is written for the person holding
the USB stick, not for a stack trace: say which disk, which operation, and what
to do next.

Comment sparingly, and only the *why*: a non-obvious reason, such as a `4096`
return value meaning "queued, not done", or procfs reporting every file as zero
bytes so `QTextStream::atEnd()` lies. Keep it to one or two lines. Leave out
comments that restate the code, narrate history, or repeat what a nearby
comment or the docs already say.

## Testing Guidelines

Tests use `QtTest`, are named after the behaviour they pin down, and go in the
file for the code they cover: `test_core.cpp`, `test_wmi.cpp`, `test_linux.cpp` or
`test_gui.cpp`. Every change to a safety rule needs a test, including the case it
now refuses that it previously allowed — several existing tests exist purely to
document a rule that used to be too permissive, and their comments say so.

`src/core/partition_table.cpp` gets checked twice: against the specification in
the unit tests, and against `sgdisk` and `fdisk` in
`scripts/verify-partition-tables.sh`. Both matter — the bytes can satisfy the
spec and still be rejected by the tools that have to read the disk. Add a case
to the script for any new layout the tool can produce.

The backends' WMI handling and Linux loop-image steps have automated tests, but no
test restores a physical drive. The Linux loop tests need root and
`USBRESTORE_TEST_LOOP=1`; see `docs/development.md`. Never test a restore on a
drive whose contents matter. Use disposable media for manual checks
when a backend change needs them. Do not maintain a per-device hardware-test log.

## Commit & Pull Request Guidelines

History uses Conventional Commit prefixes (`feat:`, `fix:`, `docs:`, `chore:`).
Keep commits scoped. Pull requests should state the problem, summarise the
change, and report relevant test results and any validation limits.

**No AI attribution anywhere in this repository.** Commits and pull requests
carry no `Co-Authored-By:` trailer naming an assistant, no "Generated with"
footer, and no tool byline. The author of a commit is the person who owns the
work. This applies whether or not a tool's default settings add one — check the
message before committing, and strip it if it appeared. The same goes for
generated files, code comments, and release notes.

## Safety & Release Notes

The destructive path is gated by the acknowledgement dialog and an identity
check on the open device handle. Do not add a way to bypass either, do not add a
command-line flag to the GUI that starts a restore, and do not widen the
bus-type check. Read `SECURITY.md` before changing anything under `src/win` or
`src/linux`.

`usb-restoration-helper` is the one deliberate exception to the command-line
rule, and it earns it by re-deriving every fact rather than accepting one: it
enumerates the disk itself, builds its own `RestoreGuard`, and re-runs
`isSafeRestoreTarget()` before anything is opened. Arguments are claims it may
only refuse on. Any change that lets an argument decide something instead —
above all the bus type — removes the reason the binary is allowed to exist.

Adding a platform means implementing `DiskService` and nothing else. If a change
needs the GUI or `src/core` to know which platform it is on, the abstraction is
in the wrong place.

On Linux an installed build runs the GUI unprivileged and the restore in
`usb-restoration-helper` through pkexec; the AppImage still runs everything as
root, because it can register no polkit policy. `docs/polkit-helper.md` is the
design. Both platforms have been tested across devices and systems. Read the
helper design before restructuring `src/linux`.

`PROJECT_VERSION` in `CMakeLists.txt` is the single source of the version.
Tagging `vX.Y.Z` triggers `release.yml`, which fails unless the tag matches that
version and the README has a `## What's New in X.Y.Z` section to use as the
release body.

## Documentation and tasks

Read `docs/TODO.md` when planning work; update it when a task is added, finished,
or blocked. Keep only actionable tasks and brief completion notes there.

Use README for download and usage, `docs/development.md` for build and contributor
instructions, `docs/polkit-helper.md` for the helper design, `SECURITY.md` for
safety policy, and `CHANGELOG.md` for release history. Avoid duplicating these
across files. Use ordinary relative Markdown links, not Obsidian wiki links.
Hardware-test logs and per-device matrices are no longer maintained.
