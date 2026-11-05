# Development

## Build

Requirements: CMake 3.21+, Qt 6.4+ (`Core`, `Widgets`, `LinguistTools`, and `Test`
for tests), the German Qt base translation catalog, and a C++20 compiler. Use
MSVC / Visual Studio 2022+ on Windows or GCC / Clang on Linux.

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

`usb-gui-tests` runs offscreen through ctest. An example `DiskService` checks the
single-window erase acknowledgement, worker progress and all three outcomes
without accessing disks. It checks that dismissal cannot start a restore,
the worker uses the request shown in the confirmation, and each outcome leaves
the main window reflecting a fresh scan. Retrying requires a new erase
acknowledgement. Widget checks cover both
step plans and themes, cancellation, elapsed time, window dismissal and long
errors. To save previews on Linux:

```bash
USBRESTORE_GUI_SNAPSHOTS=build-linux/ui-previews ./build-linux/usb-gui-tests rendersBothPlansAndThemes -platform offscreen
```

The README's interface previews come from the same test binary:

```bash
USBRESTORE_GUI_SNAPSHOTS=docs/images ./build-linux/usb-gui-tests rendersTheMainWindow rendersTheConfirmationDialog -platform offscreen
```

That writes `app-light.png`, `app-dark.png`, `confirm-light.png` and
`confirm-dark.png` with first-run settings. Review all four, and regenerate them
whenever the main window or the confirmation dialog changes. It also writes
`app-light-de.png` and `app-dark-de.png` for reviewing the German layout; the
README does not use them, so do not commit them.

The restore animation, `restore-light.gif` and `restore-dark.gif`, is recorded in
real time and encoded with ffmpeg:

```bash
./scripts/render-restore-animation.sh build-linux
```

It takes about 25 seconds. Regenerate it whenever the restore dialog changes.

Set `QT_SCALE_FACTOR=2` to check 200% scaling.

The restore-dialog rendering matrix covers English and German, both step plans,
both themes and every outcome. Main-window previews also cover both languages
and themes. ctest runs these and the German About/activity/scan-error checks at
200% scaling as `usb-gui-tests-scaled`. German snapshots have a `-de` filename suffix. The German
error and progress examples in this matrix are supplied by the test, not by a
translated backend. Other wording tests fix their default locale to C.

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

Linux's `usb-linux-tests` also exercises formatter mappings on disposable sparse
image files. Ordinary `ctest` runs the nonprivileged checks and skips loop tests;
the Debian and sanitizer CI jobs run all four formatters with 512-byte and
4096-byte sectors:

```bash
sudo apt install exfatprogs dosfstools ntfs-3g e2fsprogs util-linux
sudo env USBRESTORE_TEST_LOOP=1 ./build-linux/usb-linux-tests
```

These tests require root, kernel 5.15+ and `/dev/loop-control`. They cover both
regular-file backing and an image-backed block device held exclusively. They
replace the backing image pathname after mapping, verify that only the original file's
partition range changes, check filesystem recognition and confirm geometry.
Cleanup tests exit at the cancellation checkpoint, run a formatter that fails,
and attempt to start a missing formatter. They check that mappings detach,
exclusive claims can be acquired again and image contents stay unchanged.
A partition-scanning loop image checks that the kernel drops the old partition
whether or not mkfs succeeds, and that the new partition matches the disk sequence
read after the re-read. Another test holds the free loop node exclusively and
checks that the formatter mapping claims a different one.
They never enumerate or restore physical disks.

The platform restore sequences have no automated hardware coverage. Use only
disposable media for manual restore checks. A per-device test log is not required.

The application log records setup time, each numbered restore step and the total
attempt time in milliseconds, including failed or cancelled attempts. Use these
to identify slow stages when checking performance on disposable media. Helper
setup includes the time spent approving the authentication prompt.

## Translations

The GUI loads the saved language at startup, before any window.
`AppSettings::language` stores `system`, `en` or `de`; missing and unknown values
follow the system's preferred UI languages, falling back to English. The main
window cycles those three choices and saves them. It does not switch language
while a restore is running. `Translations` owns the Qt translators without
changing the default locale. A catalog that fails to load leaves English in
place and the application still opens.

`qt_add_translations` compiles `translations/usbrestore_de.ts`. The application
catalog and the Qt kit's `qtbase_de.qm` are embedded in the GUI, the helper and
translation tests; loading never searches user-writable directories or
environment-supplied paths. Update the catalog after marking more UI text for translation:

```bash
cmake --build build-linux --target update_translations
```

Review the new translations before building; unfinished entries fall back to
English. Both platform source sets are listed explicitly for extraction on Qt 6.4.
`RestoreMessage` carries English log text and translated display text; nested
messages format each half separately and raw diagnostics populate both unchanged.
Backend `QString *error` outputs remain English. Optional `displayError` outputs
carry UI explanations, and `RestoreWorker` emits them only through progress and
failure signals. Log-only reporter details never pass through a translator.

The helper embeds the same catalogs. The GUI passes the resolved `en` or `de`
choice as `--language`, because pkexec clears the environment. Progress lines
and stderr stay English for the log; a translated refusal travels on a `failure`
line. Launch and protocol errors are still translated by the client. A missing
catalog leaves the helper in English and the restore still runs. The language
never decides whether a restore is allowed or which device is opened.

Shared core descriptions, safety reasons and layout errors accept `TextMode::Display`; their default is English
for backend and log callers. The catalog supplies the display number locale,
without changing the process-wide locale. Device IDs and protocol tokens are
never translated. Language must never decide whether a restore is allowed or
which device is opened.

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
| Fedora package | `./scripts/build-rpm.sh` | `.rpm` and checksum under `standalone/` |

The package scripts run `--show-translation en` and `--show-translation de` on the
binary that performs the restore. That is the helper in the Debian and Fedora
packages, and the application itself in the AppImage and Windows bundle. The
probe prints one line and exits before opening a disk. German must print
`Erhöhte Berechtigung erforderlich`. The Windows script runs the probe with
`__COMPAT_LAYER=RunAsInvoker`, so it needs no elevated shell.

The Windows script needs PowerShell 7. Run it with `pwsh`, not the Windows
PowerShell 5.1 that ships with Windows; under 5.1 it stops before building.

The AppImage script needs `curl` and FUSE support for its build tools. It disables
the installed helper; the resulting app runs as root. Windows signing options
remain available in the packaging script's help.

AppImage packaging also runs a backend-free probe against the bundled Qt
libraries, platform plugin and SVG support. The probe is linked without an
RPATH or RUNPATH, and the script refuses to run it if either is present. It
accepts the run only when every loaded `libQt6*` library resolves inside the
AppDir's library directory, including libraries loaded by plugins. A missing
library or a symlink to host Qt fails validation. Linux GUI tests check a complete
bundle, missing QtGui and QtWidgets, and a symlink to a host library in disposable
directories. The probe never enumerates or restores disks. Some patchelf versions
corrupt tightly packed Qt plugins while rewriting their library paths
([upstream issue](https://github.com/NixOS/patchelf/issues/639)). If the probe
fails for that reason, set `PATCHELF=/path/to/a/fixed/patchelf` when rebuilding;
linuxdeploy uses that instead of its bundled version.

The `.deb` is built by CPack from the `install()` rules, with the prefix fixed at
`/usr`. Build it on the oldest release it should install on (CI uses Ubuntu 24.04),
against the distribution's Qt:

```bash
sudo apt install cmake g++ qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-translations-l10n dpkg-dev
./scripts/build-deb.sh
```

Leave `CMAKE_PREFIX_PATH` unset so CMake finds the system Qt; `dpkg-shlibdeps`
fails on a Qt kit no package owns. The script checks that the package contains
the helper at `/usr/libexec/usb-restoration-helper`, a polkit action naming that
path, and the license at `/usr/share/doc/usb-restoration-tool/copyright`. The
same install also ships `/usr/share/licenses/usb-restoration-tool/LICENSE` for
the RPM. `cpack` refuses any prefix other than `/usr`, since polkit reads
actions only from `/usr/share/polkit-1/actions`. Both native package formats are
non-relocatable for the same reason.

Build the RPM on Fedora 43 or later against the distribution's Qt (CI and
releases use Fedora 43):

```bash
sudo dnf install cmake gcc-c++ make qt6-qtbase-devel qt6-qttools-devel qt6-linguist qt6-qttranslations rpm-build binutils cpio
./scripts/build-rpm.sh
ctest --test-dir build-rpm --output-on-failure
```

Leave `CMAKE_PREFIX_PATH` unset for this build too. Set `Qt6LinguistTools_DIR`
when LinguistTools is unpacked but not installed; the Qt libraries themselves
must still come from Fedora packages. The script refuses Qt libraries that
are not owned by an RPM, verifies the helper/policy/GUI paths,
file ownership and permissions, runtime dependencies and helper version, and
writes a checksum. RPM's library dependency generator supplements explicit
requirements for Qt plugins, polkit and the formatters; Fedora's NTFS formatter
package is `ntfsprogs`, not `ntfs-3g`. It does not install anything on the host.
`BUILD_DIR` overrides the build directory; `CMAKE_BUILD_PARALLEL_LEVEL` sets
parallelism (default: 2).

The Fedora container image keeps only `en_US` files. CI sets `%_install_langs`
to `all` before installing packages so the German `qtbase_de.qm` is unpacked.
The shared Fedora CI/release workflow also checks an install, upgrade and
removal in a disposable container. It installs a release-0.1 baseline built from
the same binaries, upgrades it to the real release-1 RPM, verifies package
integrity, probes the helper and Qt platform/SVG loading as an unprivileged user,
loads the installed polkit action through an isolated D-Bus, then checks that
removal leaves no package files. It never starts a restore. To repeat this
**only in a clean disposable Fedora environment**, after building:

```bash
dnf install -y dbus-daemon polkit glib2 libxml2 util-linux
USBRESTORE_TEST_RPM=1 ./scripts/test-rpm-install.sh build-rpm standalone/*.rpm
```

This check requires root and refuses an existing application installation.
Desktop authentication prompts and restores on disposable USB media still need
manual validation.

`PROJECT_VERSION` in `CMakeLists.txt` is the version source. Before tagging
`vX.Y.Z`, update the changelog and the README's `## What's New in X.Y.Z` section.
The release workflow checks the version and extracts that README section as the
release body.

## Project structure

| Directory | Responsibility |
| --- | --- |
| `src/core` | No OS headers. `disk.*` (the `DiskInfo` model), `safety.*` (every erase rule and the layout arithmetic), `partition_table.*` (GPT and MBR bytes, GPT's CRC-32), `restore_message.*` (bilingual messages, and each backend's step plan, which sets the step count and first destructive step) and `restore_protocol.*` (the GUI-to-helper protocol; its argument half is a trust boundary) |
| `src/platform` | `disk_service.h`, the interface both backends implement; `restore_worker.*`, which turns progress calls into Qt signals; `logger.*`; `startup.h` |
| `src/win` | WMI (`wmi.*`), `DeviceIoControl` on `\\.\PhysicalDriveN` (`raw_disk.*`), volumes and drive letters (`volume_manager.*`), enumeration and the `DiskService` (`windows_disk_service.*`), error text and process helpers (`windows_util.*`) |
| `src/linux` | sysfs and mountinfo (`sysfs.*`), raw block I/O and the formatter's loop mapping (`block_device.*`), read-only enumeration (`linux_enumerate.*`), the privileged restore (`linux_restore.*`) and the `DiskService` over both (`linux_disk_service.*`) |
| `src/helper` | `usb-restoration-helper`, the privileged Linux executable; Qt Core only, and nothing it is told is trusted |
| `src/gui` | `main_window.*`, `restore_dialog.*` (confirmation, progress and result in one window), `theme.*`, `app_settings.*` |
| `tests` | `test_core.cpp` (`src/core`), `test_gui.cpp` (offscreen widgets and an example disk service), `test_wmi.cpp` (Windows WMI encoding and job monitoring), `test_linux.cpp` (formatter mappings and loop-image re-reads); `fuzz_core.cpp` fuzzes the parsers and tables and replays `fuzz-corpus` |
| `tools` | `partition_dump.cpp` for `scripts/verify-partition-tables.sh`; `qt_bundle_probe.cpp`, which checks a built AppImage's bundled Qt plugins load; `export_branding.cpp` |
| `scripts` | Packaging and partition verification |

Backends supply OS facts and perform I/O. New safety rules belong in core, with
tests. Add platforms through `DiskService`; keep the GUI and core independent of
platform details. Filesystem choices also come from the service.

Windows uses WMI for partitioning and formatting and `DeviceIoControl` for raw
access. WMI result `4096` means a job was queued and must be awaited. Linux keeps
read-only enumeration separate from privileged restore so the helper can link
only what it needs.

## Contributing

The icon sources, palette and export instructions are in [Branding](branding.md).

Coding style, naming and commit conventions are in [AGENTS.md](../AGENTS.md).
CI fails if any tracked `.cpp` or `.h` file is not formatted with clang-format
22.1.8. Other releases lay some code out differently, so use that version on every
platform, then format before committing:

```sh
python -m pip install clang-format==22.1.8
git ls-files '*.cpp' '*.h' | xargs clang-format -i
```

On Windows, run the second line from Git Bash, or in PowerShell use
`git ls-files '*.cpp' '*.h' | ForEach-Object { clang-format -i $_ }`.

Keep open work in [TODO.md](TODO.md) and release history in
[CHANGELOG.md](../CHANGELOG.md).
