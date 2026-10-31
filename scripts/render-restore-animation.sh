#!/usr/bin/env bash
#
# Records the restore dialog from confirmation to result and encodes the README
# animations, docs/images/restore-light.gif and restore-dark.gif. The frames come
# from the rendersTheRestoreAnimation GUI test with an example drive; nothing
# here touches a real disk.
#
# Usage:
#   ./scripts/render-restore-animation.sh [build-dir]
#
# Requires: a built usb-gui-tests target and ffmpeg.

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BuildDir="${1:-$RepoRoot/build-linux}"
WorkDir="$(mktemp -d)"
trap 'rm -rf "$WorkDir"' EXIT

# Matches FrameMs in the test.
FramesPerSecond=20
Width=520

USBRESTORE_GUI_ANIMATION="$WorkDir" "$BuildDir/usb-gui-tests" rendersTheRestoreAnimation -platform offscreen

for Theme in light dark; do
    # One palette per animation, and only the changed rectangle per frame, keeps the GIF small.
    ffmpeg -loglevel error -y -framerate "$FramesPerSecond" -i "$WorkDir/$Theme/frame-%04d.png" \
        -filter_complex "scale=$Width:-1:flags=lanczos,split[frames][copy];[copy]palettegen=stats_mode=diff[palette];[frames][palette]paletteuse=dither=none:diff_mode=rectangle" \
        -loop 0 "$RepoRoot/docs/images/restore-$Theme.gif"
    echo "Wrote docs/images/restore-$Theme.gif"
done
