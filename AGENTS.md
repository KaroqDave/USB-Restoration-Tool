# Repository Guidelines

## Project Structure & Module Organization

USB Restoration Tool is a Windows-only Qt 6 desktop app that restores an
ISO-written USB drive to one GPT + exFAT volume. Source lives in `src/`:

- `src/core/` — `disk.*` (the `DiskInfo` model, byte and enum formatting) and
  `safety.*` (every rule that decides whether a disk may be erased, plus the
  GPT layout arithmetic). **No Windows headers.** This is the half that is
  unit-tested, and new safety rules belong here rather than inline in a caller.
- `src/win/` — everything that touches the machine: `raw_disk.*`
  (`DeviceIoControl` on `\\.\PhysicalDriveN`), `wmi.*` (the `MSFT_Storage`
  wrapper), `disk_enumerator.*`, `volume_manager.*`, `restore_worker.*` (the
  ordered restore sequence), `logger.*`, `admin.*`, `windows_util.*`.
- `src/gui/` — `main.cpp`, `main_window.*`, `theme.*`, `app_settings.*`.

Tests live in `tests/test_core.cpp`. Packaging lives in `scripts/`. Docs beyond
the README go in `docs/`.

## Build, Test, and Development Commands

- `cmake --preset windows` — configure. Set `CMAKE_PREFIX_PATH` to your Qt kit
  if it is not under `C:\Qt`.
- `cmake --build --preset windows` — build Release; `windeployqt` runs after.
- `ctest --preset windows` — run the `src/core` tests.
- `./scripts/build-standalone.ps1` — export a self-contained bundle with
  `SHA256SUMS.txt` and zip it for a release.
- `clang-format -i src/**/*.cpp src/**/*.h` — the repo ships a `.clang-format`.

## Coding Style & Naming Conventions

Four-space indent, 120-column limit, `.clang-format` is authoritative. Files are
`snake_case.cpp` / `snake_case.h`; types are `PascalCase`; functions and
variables are `camelCase`; members carry an `m_` prefix. Everything lives in
`namespace usbrestore`.

Windows API calls report failure through a `QString *error` out-parameter and a
`bool` return, never an exception. Error text is written for the person holding
the USB stick, not for a stack trace: say which disk, which operation, and what
to do next.

Comment the *why*. A comment that restates the call on the line below it is
noise; a comment explaining that a `4096` return value means "queued, not done"
is the reason the next reader does not reintroduce a race.

## Testing Guidelines

Tests use `QtTest` and live in `tests/test_core.cpp`, named after the behaviour
they pin down. Every change to a safety rule needs a test, including the case it
now refuses that it previously allowed — several existing tests exist purely to
document a rule that used to be too permissive, and their comments say so.

`src/win` drives real hardware and has no automated coverage. Never test a
restore on a drive whose contents matter. Verify by hand on a disposable stick:
an ISO-written drive, a drive with several partitions, and a drive with no
partition table at all.

## Commit & Pull Request Guidelines

History uses Conventional Commit prefixes (`feat:`, `fix:`, `docs:`, `chore:`).
Keep commits scoped. Pull requests should state the problem, summarise the
change, and report test results — including which physical drives a restore was
verified on, when the change touches `src/win`.

## Safety & Release Notes

The destructive path is gated by the confirmation phrase, the summary dialog,
and an identity check on the open device handle. Do not add a way to bypass any
of the three, do not add a command-line flag that starts a restore, and do not
widen the bus-type check. Read `SECURITY.md` before changing anything under
`src/win`.

`PROJECT_VERSION` in `CMakeLists.txt` is the single source of the version.
Tagging `vX.Y.Z` triggers `release.yml`, which fails unless the tag matches that
version and the README has a `## What's New in X.Y.Z` section to use as the
release body.
