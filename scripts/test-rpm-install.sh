#!/usr/bin/env bash
#
# Install/upgrade/removal checks for a disposable Fedora container only.
# Usage: USBRESTORE_TEST_RPM=1 ./scripts/test-rpm-install.sh <build-dir> <rpm>
# The baseline uses the same binaries with RPM release 0.1; the upgrade is the
# real release-1 package, so no older application or restore is needed.

set -euo pipefail

fail() {
    echo "error: $1" >&2
    exit 1
}

[[ "${USBRESTORE_TEST_RPM:-}" == 1 && "$EUID" == 0 ]] \
    || fail "run as root with USBRESTORE_TEST_RPM=1 only in a disposable Fedora environment"
[[ $# -eq 2 ]] || fail "usage: $0 <build-dir> <rpm>"
BuildDir="$(realpath "$1")"
Package="$(realpath "$2")"
readonly Name="usb-restoration-tool"
readonly HelperPath="/usr/libexec/usb-restoration-helper"
readonly PolicyPath="/usr/share/polkit-1/actions/dev.karoqdave.usb-restoration-tool.policy"

if rpm -q "$Name" >/dev/null 2>&1; then
    fail "$Name is already installed; refusing to change an existing installation"
fi
for path in /usr/bin/usb-restoration-tool "$HelperPath" "$PolicyPath"; do
    [[ ! -e "$path" ]] || fail "$path already exists; use a clean disposable environment"
done
[[ "$(rpm -qp --qf '%{NAME}' "$Package")" == "$Name" ]] || fail "unexpected package name"
ExpectedNevra="$(rpm -qp --qf '%{NEVRA}' "$Package")"
ExpectedVersion="$(rpm -qp --qf '%{VERSION}' "$Package")"
BaselineDir="$(mktemp -d "$BuildDir/rpm-baseline.XXXXXX")"
trap 'rm -rf "$BaselineDir"' EXIT

cat > "$BaselineDir/dbus.conf" <<'EOF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>system</type>
  <user>dbus</user>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/>
    <allow own="org.freedesktop.PolicyKit1"/>
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
  </policy>
</busconfig>
EOF

# CPack treats release 0 as unset and silently replaces it with 1.
cpack -G RPM --config "$BuildDir/CPackConfig.cmake" -B "$BaselineDir" -D CPACK_RPM_PACKAGE_RELEASE=0.1
shopt -s nullglob
Baselines=("$BaselineDir"/*.rpm)
[[ ${#Baselines[@]} -eq 1 ]] || fail "expected one release-0.1 baseline RPM"
BaselineNevra="$(rpm -qp --qf '%{NEVRA}' "${Baselines[0]}")"
[[ "$BaselineNevra" != "$ExpectedNevra" ]] || fail "the upgrade RPM must differ from the release-0.1 baseline"
cmake --build "$BuildDir" --target usb-qt-bundle-probe --parallel 2

checkInstalled() {
    [[ "$(rpm -q --qf '%{NEVRA}' "$Name")" == "$1" ]] || fail "installed package is not $1"
    rpm -V "$Name"
    local protocol
    protocol="$(runuser -u nobody -- "$HelperPath" --protocol-version)"
    [[ "$protocol" == "version "* && "$protocol" == *" app $ExpectedVersion steps "* ]] \
        || fail "installed helper version mismatch"
    echo "$protocol"
    xmllint --nonet --noout "$PolicyPath"
    # Start polkit directly: its activation helper uses the fixed system-bus socket.
    dbus-run-session --config-file="$BaselineDir/dbus.conf" -- bash -e -c '
        export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
        /usr/lib/polkit-1/polkitd --no-debug &
        polkitPid=$!
        trap "kill $polkitPid 2>/dev/null || true; wait $polkitPid 2>/dev/null || true" EXIT
        gdbus wait --system --timeout 15 org.freedesktop.PolicyKit1
        pkaction --action-id dev.karoqdave.usb-restoration-tool.restore --verbose
    '
    local platform plugins
    platform="$(rpm -ql qt6-qtbase-gui | grep '/platforms/libqoffscreen.so$')"
    plugins="$(dirname "$(dirname "$platform")")"
    runuser -u nobody -- "$BuildDir/usb-qt-bundle-probe" \
        "$plugins" /usr/share/icons/hicolor/scalable/apps/usb-restoration-tool.svg
    for formatter in mkfs.exfat mkfs.vfat mkntfs mkfs.ext4; do
        [[ -x "/usr/sbin/$formatter" || -x "/usr/bin/$formatter" ]] || fail "$formatter was not installed"
    done
}

echo "==> Installing baseline $BaselineNevra"
dnf install -y "${Baselines[0]}"
checkInstalled "$BaselineNevra"

echo "==> Upgrading to $ExpectedNevra"
dnf upgrade -y "$Package"
checkInstalled "$ExpectedNevra"

echo "==> Removing $Name"
dnf remove -y "$Name"
if rpm -q "$Name" >/dev/null 2>&1; then
    fail "$Name is still registered after removal"
fi
Contents="$(rpm -qpl "$Package")"
while read -r path; do
    # Shared directories are owned by Fedora's filesystem/icon/polkit packages.
    [[ -d "$path" ]] && continue
    [[ ! -e "$path" && ! -L "$path" ]] || fail "$path remains after removal"
done <<< "$Contents"
echo "RPM installation, upgrade and removal passed. No restore performed."
