# Privilege separation on Linux — a polkit helper (planned for 1.3.0)

## The problem

On Linux the whole application runs as root. `sudo ./usb-restoration-tool`
starts a Qt GUI — widgets, theming, a QSettings store, an SVG renderer, a
handful of image format plugins, a font stack — as uid 0, so that roughly two
hundred lines of it can call `open()`, `ioctl()` and `umount2()`.

That is a poor trade. The privileged work is small, well bounded, and already
isolated in `src/linux/`; everything else is an ordinary desktop app that has
no business holding root. Concretely, running the GUI as root means:

- Every Qt plugin the app loads runs as root, including ones it never uses
  deliberately. An input-method or image-format plugin is a lot of attack
  surface for a tool whose privileged needs are four syscalls.
- `QSettings` writes to root's configuration directory, so the theme and layout
  preferences are stored per-invocation-of-sudo rather than per-user. This is
  already a small visible wart.
- Files the app creates belong to root. `hardenProcessStartup()` currently sets
  a restrictive umask to limit the damage, which is a mitigation for a problem
  that would not exist under privilege separation.
- The desktop entry cannot usefully launch it. `Exec=usb-restoration-tool` from
  a menu produces the "run me with sudo" dialog, which is why the README tells
  people to use a terminal.

Fedora Media Writer solves this by splitting the privileged work into a
separate binary (`src/helper/linux/`) that the unprivileged GUI invokes. That
is the design worth copying — it is the one place where their architecture is
cleanly better than ours.

## The shape

Two binaries:

- `usb-restoration-tool` — the existing Qt GUI, unprivileged. Enumerates disks
  and evaluates every safety rule exactly as it does now; all of that is
  read-only and needs no privilege.
- `usb-restoration-helper` — a small non-GUI binary, installed to `libexecdir`,
  that performs one restore and exits. Links `src/core` and `src/linux`, and
  nothing from `src/gui` or Qt Widgets.

polkit authorises the transition. The GUI runs the helper via `pkexec`, the
user authenticates once in their desktop's own dialog, and the helper runs as
root for the duration of one restore.

### What crosses the boundary

The helper must not trust anything it is told. Everything the GUI sends is a
*request*, and the helper re-derives the facts itself:

```
GUI  -> helper   device path, partition style, volume label
helper           re-enumerates the disk from sysfs
helper           re-evaluates isSafeRestoreTarget() against its own RestoreGuard
helper           re-runs isSameRestoreTarget() against what it found
helper           opens O_EXCL and verifies identity through the descriptor
helper -> GUI    progress lines on stdout, errors on stderr, exit code
```

This is the part to get right. A helper that erases whatever path it is handed
is a privilege escalation with extra steps: any local process could invoke it.
Re-running the full safety evaluation inside the helper is what makes the
boundary meaningful, and it is nearly free — `src/core` is already pure and
already does exactly this.

The protocol should be line-oriented and dull: progress as `step/total message`
on stdout, one error line on stderr, and an exit code. Media Writer's helper
prints bare percentages; ours already has a step model worth preserving.
`RestoreReporter` is the existing seam — the helper implements it by writing
lines, and the GUI's `RestoreWorker` parses them back into the same signals it
emits today, so `MainWindow` does not change at all.

### The polkit policy

A `.policy` file installed to `/usr/share/polkit-1/actions/`:

```xml
<action id="dev.karoqdave.usb-restoration-tool.restore">
  <description>Restore a USB drive</description>
  <message>Authentication is required to erase and restore a USB drive</message>
  <defaults>
    <allow_any>auth_admin</allow_any>
    <allow_inactive>auth_admin</allow_inactive>
    <allow_active>auth_admin_keep</allow_active>
  </defaults>
  <annotate key="org.freedesktop.policykit.exec.path">/usr/libexec/usb-restoration-helper</annotate>
  <annotate key="org.freedesktop.policykit.exec.allow_gui">true</annotate>
</action>
```

`auth_admin_keep` rather than `auth_admin` so a user restoring several sticks
in a row authenticates once rather than once per drive. `yes` would be wrong
here — this action destroys data.

## Consequences worth accepting up front

**The AppImage stops being self-contained.** polkit reads its policies from
system directories, so an AppImage cannot register one; `pkexec` on a binary
inside a squashfs mount is also not something to encourage. The realistic
outcomes are that the AppImage keeps today's run-as-root behaviour as a
fallback, and that properly separated privileges only come with a real install
(`.deb`, `.rpm`, an AUR package). That is a packaging project in itself, and it
is the main reason this is 1.3.0 and not 1.2.0.

**Two binaries to keep in step.** The helper and the GUI must agree on the
protocol, and a version mismatch has to be detected rather than guessed at. A
`--protocol-version` handshake on startup is the cheap answer.

**`pkexec` sanitises the environment**, which is the point, but it also means
the helper cannot rely on anything inherited. It needs no display, so this is
mostly a matter of not assuming `$PATH` — which `findSystemTool()` already does
not.

## Rough order of work

1. Extract the restore sequence in `LinuxDiskService::restore()` behind a
   `RestoreReporter` that writes protocol lines, so it runs unchanged in either
   process. Little more than a rename today.
2. Add the `usb-restoration-helper` target: `main.cpp`, argument parsing, the
   re-validation described above, protocol output. No Qt Widgets.
3. Teach the Linux `DiskService` to spawn the helper through `pkexec` and parse
   its output back into `RestoreReporter` calls.
4. Install the `.policy` file and the helper to `libexecdir` from CMake.
5. Drop the root check from the GUI on Linux; keep it in the helper, where a
   non-root invocation is now genuinely an error.
6. Keep the AppImage on the run-as-root path, and say which is which in the
   README.

## Not in scope

Windows does not get an equivalent. UAC already gates the process at launch,
the manifest already declares it, and there is no Windows analogue worth
building here — splitting it would mean a service or a COM elevation moniker,
both of which are more attack surface than they remove.

## Before this

`src/linux` has still never performed a restore against a physical USB stick.
Splitting a code path that has never run is a good way to end up debugging two
problems at once. Verify a real GPT and a real MBR restore on hardware first.
