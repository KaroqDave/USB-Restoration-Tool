# Repository Guidelines

## Project Structure & Module Organization

USB Restoration Tool is a Qt 6 desktop app for Windows and Linux that restores
an ISO-written USB drive to one GPT or MBR partition formatted exFAT. Source
lives in `src/`:

- `src/core/` — `disk.*` (the `DiskInfo` model and formatting), `safety.*`
  (every rule that decides whether a disk may be erased, plus the layout
  arithmetic), `partition_table.*` (GPT and MBR bytes, including GPT's CRC-32)
  and `restore_protocol.*` (the GUI-to-helper line protocol, whose argument half
  is a trust boundary). **No OS headers.** This is the half that is tested, and
  new safety rules belong here rather than inline in a backend.
- `src/platform/` — `disk_service.h`, the interface both backends implement;
  `restore_worker.*`, which adapts a backend's progress calls into Qt signals;
  `logger.*`; `startup.h`.
- `src/win/` — WMI (`wmi.*`), `DeviceIoControl` on `\\.\PhysicalDriveN`
  (`raw_disk.*`), enumeration, volumes, and `windows_disk_service.*`.
- `src/linux/` — sysfs and `/proc/self/mountinfo` (`sysfs.*`), raw block I/O
  (`block_device.*`), read-only enumeration (`linux_enumerate.*`), the
  privileged restore sequence (`linux_restore.*`) and the thin `DiskService`
  over both (`linux_disk_service.*`). The split is so the helper can link the
  restore without the service; keep it that way.
- `src/helper/` — `main.cpp` for `usb-restoration-helper`, the Linux-only
  privileged binary. Qt Core only, no widgets, and nothing it is told is
  trusted. See `docs/polkit-helper.md`.
- `src/gui/` — `main.cpp`, `main_window.*`, `theme.*`, `app_settings.*`.

Tests live in `tests/test_core.cpp`. `tools/partition_dump.cpp` backs
`scripts/verify-partition-tables.sh`. Docs beyond the README go in `docs/`.
Working notes (the task list, hardware log, decisions) live in `vault/`.

## Build, Test, and Development Commands

- `cmake --preset windows` / `--preset linux` — configure. Set
  `CMAKE_PREFIX_PATH` if Qt is not found.
- `cmake --build --preset windows` — build Release; `windeployqt` runs after on
  Windows.
- `ctest --preset windows` — run the `src/core` tests.
- `./scripts/verify-partition-tables.sh build-linux` — hand the partition tables
  to `sgdisk` and `fdisk`. Needs the `usb-partition-dump` target built.
- `./scripts/build-standalone.ps1` / `./scripts/build-appimage.sh` — package.
- `clang-format -i src/**/*.cpp src/**/*.h` — the repo ships a `.clang-format`.

## Coding Style & Naming Conventions

Four-space indent, 120-column limit, `.clang-format` is authoritative. Files are
`snake_case.cpp` / `snake_case.h`; types are `PascalCase`; functions and
variables are `camelCase`; members carry an `m_` prefix. Everything lives in
`namespace usbrestore`.

Platform calls report failure through a `QString *error` out-parameter and a
`bool` return, never an exception. Error text is written for the person holding
the USB stick, not for a stack trace: say which disk, which operation, and what
to do next.

Comment the *why*. A comment that restates the call below it is noise; a comment
explaining that a `4096` return value means "queued, not done", or that procfs
reports every file as zero bytes so `QTextStream::atEnd()` lies, is the reason
the next reader does not reintroduce the bug.

## Testing Guidelines

Tests use `QtTest` and live in `tests/test_core.cpp`, named after the behaviour
they pin down. Every change to a safety rule needs a test, including the case it
now refuses that it previously allowed — several existing tests exist purely to
document a rule that used to be too permissive, and their comments say so.

`src/core/partition_table.cpp` gets checked twice: against the specification in
the unit tests, and against `sgdisk` and `fdisk` in
`scripts/verify-partition-tables.sh`. Both matter — the bytes can satisfy the
spec and still be rejected by the tools that have to read the disk. Add a case
to the script for any new layout the tool can produce.

The backends drive real hardware and have no automated coverage. Never test a
restore on a drive whose contents matter. Verify by hand on a disposable stick:
an ISO-written drive, a drive with several partitions, and a drive with no
partition table at all — on both platforms and both partition styles.

## Commit & Pull Request Guidelines

History uses Conventional Commit prefixes (`feat:`, `fix:`, `docs:`, `chore:`).
Keep commits scoped. Pull requests should state the problem, summarise the
change, and report test results — including which physical drives a restore was
verified on, when the change touches a backend.

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
design and carries the status — as of 1.3.0 the whole path is implemented and
none of it has been exercised on hardware. Read it before restructuring
`src/linux`.

`PROJECT_VERSION` in `CMakeLists.txt` is the single source of the version.
Tagging `vX.Y.Z` triggers `release.yml`, which fails unless the tag matches that
version and the README has a `## What's New in X.Y.Z` section to use as the
release body.

## Working notes (Obsidian)

The task list and hardware-test log live in `vault/` at the repository root.
Read `vault/TODO.md` when planning work; update it when a task is added,
finished, or blocked. Log physical restores in `vault/Hardware Testing.md`.
Repo files remain the published source of truth — do not duplicate README,
SECURITY.md, or `docs/` into the vault. Details:
`.cursor/rules/obsidian-vault.mdc`.
