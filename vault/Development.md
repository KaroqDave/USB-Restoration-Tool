---
tags:
  - development
---

# Development

Conventions are in `AGENTS.md` at the repository root; `.clang-format` is authoritative.

## Stack

- CMake 3.21+, C++20, Qt 6.4+ (`Core`, `Widgets`; `Test` for tests)
- MSVC (VS 2022+) on Windows; GCC/Clang on Linux
- Four-space indent, 120-column limit
- Files `snake_case`; types `PascalCase`; functions/variables `camelCase`; members `m_`
- Everything in `namespace usbrestore`

Platform calls return `bool` and write a `QString *error` — never exceptions. Error text is for the person holding the stick: which disk, which operation, what to do next.

Comment the *why*. A `4096` from `MSFT_Storage` means queued, not done. Procfs reports every file as zero bytes, so `QTextStream::atEnd()` lies.

## Build

```bash
cmake --preset windows   # or linux
cmake --build --preset windows
ctest --preset windows
```

Set `CMAKE_PREFIX_PATH` if Qt is not found. On Windows, a kit under `C:\Qt` is located automatically when neither `CMAKE_PREFIX_PATH` nor `Qt6_DIR` is set.

- Windows output: `build\Release\usb-restoration-tool.exe` (`windeployqt` runs after)
- Linux output: `build/usb-restoration-tool`

Package: `scripts/build-standalone.ps1` / `scripts/build-appimage.sh`. `clang-format -i src/**/*.cpp src/**/*.h`

## Tests

`tests/test_core.cpp` covers `src/core`: refusals, identity comparison, layout arithmetic, GPT/MBR bytes. Every safety-rule change needs a test, including the case it now refuses that it previously allowed.

Partition tables are checked twice: against the spec in unit tests, and against `sgdisk`/`fdisk` via `scripts/verify-partition-tables.sh`. Add a script case for any new layout the tool can produce.

```bash
cmake --build build --target usb-partition-dump
./scripts/verify-partition-tables.sh build
```

Backends have no automated coverage. Never test a restore on a drive whose contents matter. See [[Hardware Testing]].

## Commits

Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`). PRs state the problem, summarise the change, and report tests — including which physical drives, when a backend changed.

## Related

- [[Architecture]]
- [[Releases]]
- [[TODO]]
