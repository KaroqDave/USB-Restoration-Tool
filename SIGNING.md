# Code Signing

USB Restoration Tool requests Administrator permissions. Authenticode signing is supported for release channels that require publisher identity and reduced SmartScreen friction.

## Check for a Code-Signing Certificate

```powershell
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert
Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert
```

## Build a Signed Release

```powershell
.\scripts\package-release.ps1 `
  -QtDir ".\.deps\Qt\6.10.1\msvc2022_64" `
  -Sign `
  -CertificateThumbprint "YOUR_CERT_THUMBPRINT" `
  -RequireSignature
```

The release script signs `USBRestorationTool.exe`, verifies the Authenticode status, deploys Qt/runtime dependencies, and writes `SHA256SUMS.txt` for every file in the release folder.

Unsigned builds are reproducible from source and include full-bundle hashes, but Windows may show publisher warnings.
