#!/usr/bin/env bash
#
# Builds against Fedora's Qt, packages the GUI, helper and polkit action, and
# writes the RPM and checksum to standalone/.
#
# Usage: ./scripts/build-rpm.sh
# Optional: BUILD_DIR (default: build-rpm), CMAKE_BUILD_PARALLEL_LEVEL (default: 2).

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BuildDir="${BUILD_DIR:-$RepoRoot/build-rpm}"
OutputRoot="$RepoRoot/standalone"

readonly HelperPath="/usr/libexec/usb-restoration-helper"
readonly PolicyPath="/usr/share/polkit-1/actions/dev.karoqdave.usb-restoration-tool.policy"
readonly ExpectedFiles=(
    /usr/bin/usb-restoration-tool
    "$HelperPath"
    "$PolicyPath"
    /usr/share/applications/usb-restoration-tool.desktop
    /usr/share/icons/hicolor/scalable/apps/usb-restoration-tool.svg
    /usr/share/icons/hicolor/256x256/apps/usb-restoration-tool.png
    /usr/share/licenses/usb-restoration-tool/LICENSE
)
readonly ExpectedDependencies=(
    qt6-qtbase-gui qt6-qtwayland qt6-qtsvg polkit
    exfatprogs dosfstools ntfsprogs e2fsprogs
)

step() {
    echo ""
    echo "==> $1"
}

fail() {
    echo "error: $1" >&2
    exit 1
}

for tool in cmake cpack rpmbuild rpm rpm2cpio cpio strings sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool is required; see docs/development.md"
done
[[ "$(rpm -E '%{?fedora}')" =~ ^[0-9]+$ ]] || fail "build the RPM on Fedora against its system Qt"

step "Configuring CMake"
cmake -S "$RepoRoot" -B "$BuildDir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBEXECDIR=libexec \
    -DUSBRESTORE_INSTALL_HELPER=ON
BuildDir="$(cd "$BuildDir" && pwd)"

step "Building"
cmake --build "$BuildDir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"

step "Checking system Qt ownership"
Libraries="$(ldd "$BuildDir/usb-restoration-tool")"
while read -r library; do
    rpm -qf "$library" >/dev/null || fail "Qt library $library is not owned by a Fedora package"
done < <(awk '/libQt6.* => \// { print $3 }' <<< "$Libraries")
grep -qE 'libQt6Core.* => /' <<< "$Libraries" || fail "could not resolve the system Qt Core library"

PackageDir="$(mktemp -d "$BuildDir/rpm-package.XXXXXX")"
Extracted="$(mktemp -d)"
trap 'rm -rf "$PackageDir" "$Extracted"' EXIT

step "Packaging"
cpack -G RPM --config "$BuildDir/CPackConfig.cmake" -B "$PackageDir"
shopt -s nullglob
Packages=("$PackageDir"/*.rpm)
[[ ${#Packages[@]} -eq 1 ]] || fail "cpack must produce exactly one RPM"
Rpm="${Packages[0]}"

step "Checking package contents and dependencies"
rpm -qpi "$Rpm"
Contents="$(rpm -qpl "$Rpm")"
for path in "${ExpectedFiles[@]}"; do
    grep -qxF "$path" <<< "$Contents" || fail "$path is missing from $(basename "$Rpm")"
    echo "  found $path"
done
Dependencies="$(rpm -qp --requires "$Rpm")"
for dependency in "${ExpectedDependencies[@]}"; do
    grep -qxF "$dependency" <<< "$Dependencies" || fail "$dependency is missing from the RPM requirements"
done
grep -qF 'libQt6Core.so.6' <<< "$Dependencies" || fail "RPM did not discover the Qt Core dependency"
grep -qF 'libQt6Widgets.so.6' <<< "$Dependencies" || fail "RPM did not discover the Qt Widgets dependency"

[[ "$(rpm -qp --qf '%{PREFIXES}' "$Rpm")" == "(none)" ]] || fail "the RPM must not be relocatable"
Ownership="$(rpm -qp --qf '[%{FILEUSERNAME}:%{FILEGROUPNAME}\n]' "$Rpm" | sort -u)"
[[ "$Ownership" == "root:root" ]] || fail "all packaged files must belong to root:root"
( cd "$Extracted" && rpm2cpio "$Rpm" | cpio --extract --make-directories --quiet --no-absolute-filenames )
for path in /usr/bin/usb-restoration-tool "$HelperPath"; do
    [[ "$(stat -c '%a' "$Extracted$path")" == "755" ]] || fail "$path must be executable without setuid/setgid"
done
[[ "$(stat -c '%a' "$Extracted$PolicyPath")" == "644" ]] || fail "the polkit action must have mode 644"
grep -qF "<annotate key=\"org.freedesktop.policykit.exec.path\">$HelperPath</annotate>" "$Extracted$PolicyPath" \
    || fail "the polkit action does not name $HelperPath"
GuiStrings="$(strings -el "$Extracted/usr/bin/usb-restoration-tool")"
grep -qxF "$HelperPath" <<< "$GuiStrings" || fail "the GUI was not built to start $HelperPath"
Version="$(rpm -qp --qf '%{VERSION}' "$Rpm")"
Protocol="$("$Extracted$HelperPath" --protocol-version)"
[[ "$Protocol" == "version "* && "$Protocol" == *" app $Version steps "* ]] \
    || fail "the helper version does not match the RPM version $Version"
echo "  GUI, helper and polkit action agree; helper reports: $Protocol"

step "Writing package and checksum"
mkdir -p "$OutputRoot"
OutputPath="$OutputRoot/$(basename "$Rpm")"
mv -f "$Rpm" "$OutputPath"
( cd "$OutputRoot" && sha256sum "$(basename "$OutputPath")" > "$(basename "$OutputPath").sha256" )
echo "Package: $OutputPath"
echo "Install with: sudo dnf install \"$OutputPath\""
