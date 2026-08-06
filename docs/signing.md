# Code Signing

USB Restoration Tool requests Administrator permission, so Windows shows a UAC
prompt naming the publisher. On an unsigned build that publisher reads
"Unknown", and SmartScreen may add a warning of its own. Signing replaces both
with a real identity.

Signing is optional: unsigned builds are reproducible from source and ship with
`SHA256SUMS.txt` covering every file in the bundle.

## Find a code-signing certificate

```powershell
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert
Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert
```

Note the thumbprint of the certificate you want to use.

## Build a signed release

```powershell
./scripts/build-standalone.ps1 `
  -Sign `
  -CertificateThumbprint "YOUR_CERT_THUMBPRINT" `
  -RequireSignature
```

The script signs `usb-restoration-tool.exe` with SHA-256 and an RFC 3161
timestamp, verifies the resulting Authenticode status, deploys the Qt runtime
beside it, and writes `SHA256SUMS.txt` for every file in the bundle.

`-RequireSignature` makes the build fail rather than warn when the signature is
not valid. Use it in any pipeline that must not publish unsigned output.

## Verify a signature

```powershell
Get-AuthenticodeSignature .\usb-restoration-tool.exe | Format-List Status, SignerCertificate
```

`Status` must read `Valid`. Anything else means the file is unsigned, the
certificate is untrusted on this machine, or the file has been modified since
it was signed.

## Timestamping

The default timestamp URL is `http://timestamp.digicert.com`, overridable with
`-TimestampUrl`. Timestamping matters: without it the signature stops
validating the day the certificate expires, and with it the signature stays
valid for the certificate's lifetime at the moment of signing.
