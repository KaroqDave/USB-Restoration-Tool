#!/usr/bin/env bash
#
# Checks the partition tables this tool writes against the tools that will have
# to read them back: sgdisk validates GPT headers and both CRCs, fdisk reads
# the MBR and the 4 Kn GPT.
#
# The unit tests already check the bytes against what the specification says.
# This checks them against what Linux itself accepts, which is the question
# that decides whether a restored stick mounts. Nothing here touches a real
# disk; every image is a sparse file.
#
# Usage:
#   ./scripts/verify-partition-tables.sh [build-dir]
#
# Requires: gdisk (sgdisk) and util-linux (fdisk).

set -euo pipefail

# Every check below greps sgdisk and fdisk output for English phrases, so a
# localised machine would fail all of them for the wrong reason.
export LC_ALL=C

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BuildDir="${1:-$RepoRoot/build-linux}"
WorkDir="$(mktemp -d)"
trap 'rm -rf "$WorkDir"' EXIT

Dump="$BuildDir/usb-partition-dump"
if [[ ! -x "$Dump" ]]; then
    echo "error: $Dump not found. Build the usb-partition-dump target first:" >&2
    echo "    cmake --build $BuildDir --target usb-partition-dump" >&2
    exit 1
fi

# sgdisk and fdisk live in /sbin on some distributions and /usr/bin on others.
find_tool() {
    local name="$1"
    local candidate
    for candidate in "$(command -v "$name" 2>/dev/null || true)" "/sbin/$name" "/usr/sbin/$name"; do
        if [[ -n "$candidate" && -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "error: $name not found. Install gdisk and util-linux." >&2
    exit 1
}

Sgdisk="$(find_tool sgdisk)"
Fdisk="$(find_tool fdisk)"

failures=0

check() {
    local description="$1"
    shift
    if "$@"; then
        echo "  ok: $description"
    else
        echo "  FAIL: $description" >&2
        failures=$((failures + 1))
    fi
}

echo "==> GPT, 512-byte sectors"
"$Dump" 16000000000 512 gpt "$WorkDir/gpt512.img"
GptReport="$("$Sgdisk" -v "$WorkDir/gpt512.img")"
echo "$GptReport" | sed 's/^/    /'
# sgdisk reports every problem it finds, and says so explicitly when it finds
# none. A caution about alignment counts as a problem here: it means the layout
# is not what every other partitioning tool would have produced.
check "sgdisk finds no problems" grep -q "No problems found" <<< "$GptReport"
check "sgdisk raises no caution" bash -c '! grep -qi "caution" <<< "$1"' _ "$GptReport"

GptTable="$("$Fdisk" -l "$WorkDir/gpt512.img")"
check "fdisk reads a GPT label" grep -q "Disklabel type: gpt" <<< "$GptTable"
check "the partition is Microsoft basic data" grep -q "Microsoft basic data" <<< "$GptTable"
check "the partition starts at 1 MiB" grep -qE "img1[[:space:]]+2048[[:space:]]" <<< "$GptTable"

echo
echo "==> GPT, 4096-byte sectors"
"$Dump" 34359738368 4096 gpt "$WorkDir/gpt4k.img"
# sgdisk assumes 512-byte sectors for a plain file and cannot be told otherwise,
# so the 4 Kn image is read with fdisk, which can.
Gpt4kTable="$("$Fdisk" -b 4096 -l "$WorkDir/gpt4k.img")"
echo "$Gpt4kTable" | sed 's/^/    /'
check "fdisk reads a GPT label" grep -q "Disklabel type: gpt" <<< "$Gpt4kTable"
check "the partition is Microsoft basic data" grep -q "Microsoft basic data" <<< "$Gpt4kTable"
check "the partition starts at 1 MiB" grep -qE "img1[[:space:]]+256[[:space:]]" <<< "$Gpt4kTable"

echo
echo "==> MBR, 512-byte sectors"
"$Dump" 16000000000 512 mbr "$WorkDir/mbr512.img"
MbrTable="$("$Fdisk" -l "$WorkDir/mbr512.img")"
echo "$MbrTable" | sed 's/^/    /'
check "fdisk reads an MBR label" grep -q "Disklabel type: dos" <<< "$MbrTable"
# Type 0x07 is what a BIOS-era device reads to recognise an exFAT stick.
check "the partition type is exFAT/NTFS" grep -q "HPFS/NTFS/exFAT" <<< "$MbrTable"
check "the partition starts at 1 MiB" grep -qE "img1[[:space:]]+2048[[:space:]]" <<< "$MbrTable"

echo
echo "==> MBR FAT32, 512-byte sectors"
"$Dump" 16000000000 512 mbr "$WorkDir/mbr512fat32.img" fat32
Fat32Table="$("$Fdisk" -l "$WorkDir/mbr512fat32.img")"
echo "$Fat32Table" | sed 's/^/    /'
check "fdisk reads an MBR label" grep -q "Disklabel type: dos" <<< "$Fat32Table"
check "the partition type is FAT32 LBA" grep -q "W95 FAT32 (LBA)" <<< "$Fat32Table"

echo
echo "==> GPT ext4, 512-byte sectors"
"$Dump" 16000000000 512 gpt "$WorkDir/gptext4.img" ext4
Ext4Table="$("$Fdisk" -l "$WorkDir/gptext4.img")"
echo "$Ext4Table" | sed 's/^/    /'
check "fdisk reads a GPT label" grep -q "Disklabel type: gpt" <<< "$Ext4Table"
check "the partition is Linux filesystem" grep -q "Linux filesystem" <<< "$Ext4Table"

echo
echo "==> MBR past its addressing limit"
# 4 TB is more than MBR's 32-bit sector count can describe. Being refused is the
# correct outcome; silently truncating the partition would not be.
if "$Dump" 4398046511104 512 mbr "$WorkDir/toobig.img" 2>/dev/null; then
    echo "  FAIL: a 4 TB MBR layout was accepted" >&2
    failures=$((failures + 1))
else
    echo "  ok: refused, as it should be"
fi

echo
if [[ "$failures" -gt 0 ]]; then
    echo "$failures check(s) failed" >&2
    exit 1
fi
echo "All partition table checks passed."
