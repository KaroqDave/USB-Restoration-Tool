# Privilege separation on Linux — a polkit helper (for 1.3.0)

> **Status.** All six steps below are implemented. An installed build runs the
> GUI unprivileged and the restore in `usb-restoration-helper` through pkexec;
> the AppImage keeps the run-as-root path. None of it has been exercised against
> a physical USB stick, and the pkexec round trip has not been exercised at all
> — there is no polkit in the environment this was written in. Treat the whole
> path as unverified until someone has restored a real drive both ways. What
> "Before this" at the bottom says still stands.

## The problem

On Linux the whole application ran as root. `sudo ./usb-restoration-tool`
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
GUI  -> helper   device path, partition style, volume label,
                 and the identity the user was shown: size, sector size,
                 serial, unique id, sysfs path, model name
helper           re-enumerates the disk from sysfs
helper           re-evaluates isSafeRestoreTarget() against its own RestoreGuard
helper           re-runs isSameRestoreTarget() against what it found
helper           opens O_EXCL and verifies identity through the descriptor
helper -> GUI    progress lines on stdout, errors on stderr, exit code
```

More crosses than the three fields this document first assumed, because
`isSameRestoreTarget()` compares seven. Sending them costs nothing in safety:
each one is a claim the helper can only *refuse* on, never act on, so an extra
field can only narrow what gets through. The one field deliberately not sent is
the bus type — the caller does not get to assert that its target is USB, and a
`DiskInfo` assembled from arguments alone fails `isSafeRestoreTarget()` for
exactly that reason.

The two checks defend against different things, and it is worth being precise
about which:

- `isSafeRestoreTarget()`, run against what sysfs says right now, is the defence
  against a *malicious* caller. It is the only one. Any local process can invoke
  the helper, so this is what stops one asking for the system disk.
- `isSameRestoreTarget()` is the defence against an *honest* caller whose disk
  changed underneath it. The polkit prompt takes seconds — long enough to unplug
  one stick and plug in another. It is no defence against a malicious caller,
  who could simply describe the disk accurately.

Confusing the two would lead to the mistake of trusting the identity fields
because they "were verified".

The protocol is line-oriented and dull: `version <n>` first, then
`step <n>/<total> <message>` and `detail <message>` on stdout, one error line on
stderr, and an exit code — 0 restored, 1 refused or failed, 2 the two binaries
disagreeing about their arguments. Media Writer's helper prints bare
percentages; ours already has a step model worth preserving. `RestoreReporter`
is the existing seam — the helper implements it by writing lines, and the GUI's
`RestoreWorker` parses them back into the same signals it emits today, so
`MainWindow` does not change at all.

Message text is stripped of control characters before it is written. mkfs
output goes into the log verbatim, and a newline in it would arrive at the GUI
as a line the helper never sent — a `result` line, say. Each source line
becomes its own `detail` line instead.

Cancellation travels the other way, as the word `cancel` on the helper's stdin,
honoured only where an in-process cancel would be. End of input is not a
cancel: if the GUI dies mid-restore, finishing is better than leaving a
half-written partition table behind.

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

1. **Done.** Extract the restore sequence in `LinuxDiskService::restore()`
   behind a `RestoreReporter` that writes protocol lines, so it runs unchanged
   in either process. Little more than a rename today.

   It was. `src/linux/linux_enumerate.*` holds the read-only half and
   `src/linux/linux_restore.*` the privileged sequence, both moved verbatim;
   `LinuxDiskService` is now a hundred lines of adapter over them. The protocol
   itself went to `src/core/restore_protocol.*` rather than `src/linux`, because
   the argument half is a safety rule and belongs where the tests are.
2. **Done.** Add the `usb-restoration-helper` target: `main.cpp`, argument
   parsing, the re-validation described above, protocol output. No Qt Widgets.

   `ldd` on the result lists `libQt6Core` and nothing else, against the GUI's
   Core, Gui, Widgets and DBus. That is the entire point of the exercise,
   visible in one command.
3. **Done.** Teach the Linux `DiskService` to spawn the helper through `pkexec`
   and parse its output back into `RestoreReporter` calls.

   `src/linux/helper_client.*`. `restore()` picks a route: already root means
   sudo or the AppImage and runs in-process, otherwise the helper. The version
   handshake happens *before* pkexec — the probe needs no privilege — so a
   mismatched pair is caught without making the user type a password first.
4. **Done.** Install the `.policy` file from CMake, alongside the helper in
   `libexecdir`. The exec path is one CMake variable that is compiled into the
   GUI, written into the action, and used as the install destination; pkexec
   authorises exactly one absolute path, so all three have to be the same
   string. The two files install together or not at all — a helper without the
   policy cannot obtain privilege, and a policy without the helper points at
   nothing.
5. **Done.** Drop the root check from the GUI on Linux; keep it in the helper,
   where a non-root invocation is now genuinely an error.

   `isPrivileged()` stopped meaning "am I root" and started meaning "can a
   restore happen at all": root, or an installed helper and a pkexec to reach
   it. `main.cpp` did not change.
6. **Done.** Keep the AppImage on the run-as-root path, and say which is which
   in the README. `USBRESTORE_INSTALL_HELPER=OFF` is how the AppImage build
   leaves the helper and the policy out.

### One thing this cost

`detectPartitionStyle()` read the first sectors of the device, which an
unprivileged GUI cannot open, so every disk in the list would have shown
"RAW / unknown". It now falls back to `ID_PART_TABLE_TYPE` from udev's
database, which is world-readable. Second-hand and possibly stale, so it is
consulted only when the authoritative read is unavailable — and nothing but the
label in the list depends on it. No safety rule reads `partitionStyle`.

## Not in scope

Windows does not get an equivalent. UAC already gates the process at launch,
the manifest already declares it, and there is no Windows analogue worth
building here — splitting it would mean a service or a COM elevation moniker,
both of which are more attack surface than they remove.

## Before this

`src/linux` has still never performed a restore against a physical USB stick.
Splitting a code path that has never run is a good way to end up debugging two
problems at once. Verify a real GPT and a real MBR restore on hardware first.
