#!/usr/bin/env bash
#
# Builds the Release binaries against the system Qt and packages them as
# standalone/usb-restoration-tool_<version>_<arch>.deb, then checks that the
# GUI, the helper and its polkit action all agree on where the helper is.
#
# Usage:
#   ./scripts/build-deb.sh
#
# Optional environment variables:
#   BUILD_DIR  CMake build directory (default: build-deb)
#
# Needs Debian or Ubuntu with cmake, g++, qt6-base-dev and dpkg-dev. Qt has to
# be the distribution's: dpkg-shlibdeps derives the Qt dependencies from the
# packages that own the libraries, and fails on a Qt kit no package owns.

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BuildDir="${BUILD_DIR:-$RepoRoot/build-deb}"
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
)

step() {
    echo ""
    echo "==> $1"
}

fail() {
    echo "error: $1" >&2
    exit 1
}

if ! command -v dpkg-deb >/dev/null 2>&1 || ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
    fail "dpkg-deb and dpkg-shlibdeps are required; install dpkg-dev"
fi

step "Configuring CMake"
# The prefix is part of the build, not of the install: it fixes the helper path
# compiled into the GUI and written into the polkit action.
cmake -S "$RepoRoot" -B "$BuildDir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DUSBRESTORE_INSTALL_HELPER=ON

step "Building"
cmake --build "$BuildDir" --parallel

step "Packaging"
PackageDir="$BuildDir/package"
rm -rf "$PackageDir"
cpack --config "$BuildDir/CPackConfig.cmake" -B "$PackageDir"

Deb="$(find "$PackageDir" -maxdepth 1 -name '*.deb' -type f | head -1)"
[[ -n "$Deb" ]] || fail "cpack did not produce a .deb"

step "Checking package contents"
dpkg-deb --info "$Deb"
Contents="$(dpkg-deb --contents "$Deb" | awk '{ print $6 }')"
for path in "${ExpectedFiles[@]}"; do
    grep -qxF ".$path" <<< "$Contents" || fail "$path is missing from $(basename "$Deb")"
    echo "  found $path"
done

Extracted="$(mktemp -d)"
trap 'rm -rf "$Extracted"' EXIT
dpkg-deb --extract "$Deb" "$Extracted"

# pkexec authorises one absolute path; all three must name the same one.
grep -qF "<annotate key=\"org.freedesktop.policykit.exec.path\">$HelperPath</annotate>" "$Extracted$PolicyPath" \
    || fail "the polkit action does not name $HelperPath"
# QStringLiteral stores the path as UTF-16, hence -el.
strings -el "$Extracted/usr/bin/usb-restoration-tool" | grep -qxF "$HelperPath" \
    || fail "usb-restoration-tool was not built to start $HelperPath"
echo "  GUI and polkit action both name $HelperPath"

step "Writing package and checksum"
mkdir -p "$OutputRoot"
OutputPath="$OutputRoot/$(basename "$Deb")"
mv -f "$Deb" "$OutputPath"
( cd "$OutputRoot" && sha256sum "$(basename "$OutputPath")" > "$(basename "$OutputPath").sha256" )

step "Done"
echo "Package: $OutputPath"
echo "Install it with:"
echo "  sudo apt install $OutputPath"
