# Linux restore helper

An installed build runs the GUI unprivileged and starts `usb-restoration-helper`
through `pkexec` for one restore. The desktop handles authentication. The helper
links Qt Core only and exits when the operation finishes.

The AppImage runs the whole app as root. It cannot install the system polkit
policy needed by a separate helper.

## Code boundaries

- `linux_enumerate.*`: read-only disk discovery.
- `linux_restore.*`: privileged restore sequence, shared by both execution modes.
- `linux_disk_service.*`: selects the in-process or helper route.
- `helper_client.*`: starts the helper and interprets its output.
- `src/helper/main.cpp`: validates requests and reports progress.
- `src/core/restore_protocol.*`: shared argument and message parsing.
- `src/core/restore_steps.*`: the restore step plan both sides number steps by.

Keep enumeration and restore separate so the helper does not need the GUI or
`LinuxDiskService`.

## Request validation

Any local process can invoke the helper. Caller-supplied identity fields are
claims to check, never authoritative device facts. Partition style, filesystem
and label express the requested operation and are validated before use.

Before restoring, the helper:

1. Parses and validates the arguments.
2. Enumerates the disk itself and builds its own `RestoreGuard`.
3. Runs `isSafeRestoreTarget()` against those fresh facts.
4. Runs `isSameRestoreTarget()` against the identity shown by the GUI.
5. Opens the device exclusively and verifies its identity through the descriptor,
   again immediately before destructive work.

The safety check protects against malicious requests. The identity comparison
catches a drive being replaced while an honest user authenticates. The caller
cannot supply bus type or override either check.

Linux requires kernel 5.15 or later, disk sequence support and the loop driver.
It uses `O_EXCL` so mounted or claimed devices cannot be opened for restore.
Formatters are resolved from fixed system directories and run with argument
lists, never through a shell. A temporary loop device maps only the partition
range of the already verified, exclusively held disk descriptor. The kernel
retains that backing file even if the physical device pathname changes. The
mapping is established before erasing, with an exclusive claim preventing an
old filesystem from being mounted. That claim is released for `mkfs`, while the
mapping and physical disk remain held. Formatting finishes before the physical
partition table is reread; the mapping is flushed and detached first. See [SECURITY.md](../SECURITY.md) for the full policy.

## Protocol and cancellation

The GUI probes `--protocol-version` before authentication. The helper also emits
a version line when it starts a restore. The line names the protocol version,
the helper's application version, its step count and its first destructive
step, for example `version 4 app 1.7.0 steps 10 destructive 5`. The GUI numbers
the helper's steps and decides when unplugging is still safe from its own copy
of the plan, so it rejects a different protocol or plan, naming both
application versions. If the mismatch is detected during the run, the GUI sends
a cancellation request immediately. Application versions alone may differ.

The optional `--expect-sequence` argument carries the GUI's observed disk
instance and can only cause refusal. A missing sequence from the actual device
always refuses the restore.

Standard output carries `version`, `step`, `detail` and `result` lines. Control
characters are stripped before encoding messages. Errors use standard error.
The constants and exact encoding live in `src/core/restore_protocol.*`.

| Exit code | Meaning |
| --- | --- |
| 0 | Restored |
| 1 | Refused or failed |
| 2 | Invalid arguments |
| 3 | Cancelled before destructive work |

The GUI sends `cancel` on standard input. Cancellation is honored only at
checkpoints before destructive work. End of input is not a cancellation:
if the GUI exits during a write, the helper finishes the restore. It catches
`SIGPIPE` and `SIGHUP` so that writing a progress line to a closed pipe, or the
GUI's terminal closing, does not stop it between the partition table and `mkfs`.

A failed attempt to kill `pkexec` does not prove cancellation, because the
process may already be elevated. The GUI judges the actual protocol output and
exit status through `judgeHelperRun()`.

## Installation

The helper path is fixed at configure time and shared by the GUI, helper install
location and polkit policy. Install the helper and policy together. Configure
`CMAKE_INSTALL_PREFIX` for the final destination and use `DESTDIR` for staging;
do not relocate only one part. See [Development](development.md#install-and-package).

The policy requires administrator authentication and allows a cached grant for
an active session (`auth_admin_keep`). The helper needs no display or inherited
shell environment. `USBRESTORE_INSTALL_HELPER=OFF` omits it and its policy for
AppImage builds.

Unprivileged enumeration can use udev's partition-style information when it
cannot read the device directly. This is display information only; no safety
rule relies on it.

## Not in scope

Windows remains elevated through UAC at launch. A separate Windows service or
COM elevation component is not planned. AppImage privilege separation would
require a system installation, so it is not part of the portable bundle.
