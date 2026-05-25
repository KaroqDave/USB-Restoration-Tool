# Security Policy

USB Restoration Tool performs destructive disk operations and always requires Administrator permission.

## Supported Versions

| Version | Supported |
| --- | --- |
| 0.1.x | Yes |

## Safety Model

- Only USB bus disks are listed.
- Boot/system disks and disks containing `C:` are refused.
- Offline and read-only disks are refused.
- The selected disk is revalidated immediately before destructive work.
- The restored volume is revalidated before formatting.
- Restore requires typing the confirmation phrase shown by the app.

## Reporting a Vulnerability

Please open a GitHub security advisory or private issue with:

- Windows version
- Tool version
- The exact disk layout and USB model if relevant
- `USBRestorationTool.log` with any private paths or serials redacted

Do not publish a working destructive bypass until there is a fixed release.
