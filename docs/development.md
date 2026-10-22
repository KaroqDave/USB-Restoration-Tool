# Development

## Build

Requirements: CMake 3.21+, Qt 6.4+ (`Core`, `Widgets`, and `Test` for tests), and a
C++20 compiler. Use MSVC / Visual Studio 2022+ on Windows or GCC / Clang on Linux.

Set `CMAKE_PREFIX_PATH` to your Qt kit if CMake cannot find it. Windows builds
also look for an MSVC kit under `C:\Qt` when no Qt location is configured.

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

On Linux, use `linux` in all three commands. Windows output is
`build/Release/usb-restoration-tool.exe`; Linux output is
`build-linux/usb-restoration-tool`. Windows builds deploy the Qt runtime beside
the executable automatically.

To build without presets:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Set `-DUSBRESTORE_BUILD_TESTS=OFF` to omit tests.

Configure creates `compile_commands.json` for clangd and suppresses diagnostics
for the inactive platform backend. Restart clangd after configuring. Windows
uses a synthesized database because Visual Studio does not export one; Linux
links the build-tree database.

## Test

Core tests cover safety refusals, device identity, layout arithmetic, partition
table bytes and the helper protocol. Every safety-rule change needs a regression
test for a case it now refuses.

On Windows, `usb-wmi-tests` also checks numeric method parameters against
in-memory WMI objects. These tests use no storage provider and access no disks;
they run with the core tests through ctest.

Linux also checks partition tables with `sgdisk` and `fdisk`:

```bash
cmake --build --preset linux --target usb-partition-dump
./scripts/verify-partition-tables.sh build-linux
```

This requires `gdisk` and `util-linux` and uses sparse files, not physical drives.
Add a verification case for any new partition layout.

`usb-core-fuzz` feeds arbitrary bytes to the helper's argument parser, the
protocol line parser and the partition-table builder, and checks the properties
the unit tests rely on, such as no argument list ever asserting a USB bus. ctest
replays the seeds in `tests/fuzz-corpus` through it on every platform. With
Clang on Linux it can also run as a libFuzzer target under ASan and UBSan, as CI
does for two minutes on every push:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DUSBRESTORE_SANITIZE=ON -DUSBRESTORE_FUZZ=ON
cmake --build build-sanitize --target usb-restoration-tool-tests usb-partition-dump usb-core-fuzz
ctest --test-dir build-sanitize --output-on-failure
mkdir -p fuzz-work && cp tests/fuzz-corpus/* fuzz-work/ && ./build-sanitize/usb-core-fuzz -max_total_time=60 fuzz-work
```

`USBRESTORE_SANITIZE` also works on its own with GCC. Once a crash it finds is
fixed, add the input to `tests/fuzz-corpus` so every build replays it.

The platform restore sequences have no automated hardware coverage. Use only
disposable media for manual restore checks. A per-device test log is not required.

## Install and package

For an installed Linux build, configure the final prefix before building:

```bash
cmake --preset linux -DCMAKE_INSTALL_PREFIX=/usr
cmake --build --preset linux
sudo cmake --install build-linux
```

For package staging, use `DESTDIR` instead of changing the install prefix:

```bash
DESTDIR="$PWD/stage" cmake --install build-linux
```

The helper's absolute path is compiled into the GUI and polkit policy. Changing
only `cmake --install --prefix` would break that relationship. See the
[helper design](polkit-helper.md).

| Package | Command | Output |
| --- | --- | --- |
| Windows portable | `./scripts/build-standalone.ps1` | Folder, ZIP and checksums under `standalone/` |
| Linux AppImage | `./scripts/build-appimage.sh` | AppImage and checksum under `standalone/` |
| Debian package | `./scripts/build-deb.sh` | `.deb` and checksum under `standalone/` |

The AppImage script needs `curl` and FUSE support for its build tools. It disables
the installed helper; the resulting app runs as root. Windows signing options
remain available in the packaging script's help.

The `.deb` is built by CPack from the `install()` rules, with the prefix fixed at
`/usr`. Build it on the oldest release it should install on (CI uses Ubuntu 24.04),
against the distribution's Qt:

```bash
sudo apt install cmake g++ qt6-base-dev dpkg-dev
./scripts/build-deb.sh
```

Leave `CMAKE_PREFIX_PATH` unset so CMake finds the system Qt; `dpkg-shlibdeps`
fails on a Qt kit no package owns. The script checks that the package contains
the helper at `/usr/libexec/usb-restoration-helper` and a polkit action naming
that path. `cpack` refuses any prefix other than `/usr`, since polkit reads
actions only from `/usr/share/polkit-1/actions`.

`PROJECT_VERSION` in `CMakeLists.txt` is the version source. Before tagging
`vX.Y.Z`, update the changelog and the README's `## What's New in X.Y.Z` section.
The release workflow checks the version and extracts that README section as the
release body. The 1.4.x development versions were never tagged; they shipped in 1.5.0.

## Project structure

| Directory | Responsibility |
| --- | --- |
| `src/core` | Disk model, safety policy, layout, partition bytes, helper protocol; no OS headers |
| `src/platform` | `DiskService`, worker, logging and startup interface |
| `src/win` | WMI, raw disk I/O and drive letters |
| `src/linux` | sysfs, enumeration, raw block I/O and formatting |
| `src/helper` | Privileged Linux executable; Qt Core only |
| `src/gui` | Widgets, themes and settings |
| `tests` | Core QtTest suite |
| `tools` | Partition-table dump utility |
| `scripts` | Packaging and partition verification |

Backends supply OS facts and perform I/O. New safety rules belong in core, with
tests. Add platforms through `DiskService`; keep the GUI and core independent of
platform details. Filesystem choices also come from the service.

Windows uses WMI for partitioning and formatting and `DeviceIoControl` for raw
access. WMI result `4096` means a job was queued and must be awaited. Linux keeps
read-only enumeration separate from privileged restore so the helper can link
only what it needs.

## Contributing

Follow `.clang-format`: four spaces and a 120-column limit. Use `snake_case`
filenames, `PascalCase` types, `camelCase` functions and variables, and `m_` members
inside `namespace usbrestore`. Platform failures return `bool` with a
`QString *error`; messages should explain the operation and next step.

Use Conventional Commits and include relevant validation in pull requests.
Keep open work in [TODO.md](TODO.md), release history in [CHANGELOG.md](../CHANGELOG.md),
and safety guidance in [SECURITY.md](../SECURITY.md).
