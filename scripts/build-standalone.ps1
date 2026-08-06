<#
.SYNOPSIS
    Builds the Release executable and exports a self-contained, ready-to-run
    bundle into standalone/USB-Restoration-Tool, then zips it for distribution.

.DESCRIPTION
    Pipeline:
      1. Configure + build the Release executable (windeployqt runs post-build).
      2. Wipe standalone/USB-Restoration-Tool and repopulate it with the exe
         plus the full Qt runtime via `cmake --install`.
      3. Optionally Authenticode-sign the executable.
      4. Write README.txt and SHA256SUMS.txt covering every bundled file, so a
         download can be verified without trusting the archive.
      5. Zip the folder to standalone/USB-Restoration-Tool-<version>.zip.

    The standalone/ folder is gitignored; it is regenerated from scratch on each
    run and is meant to be published via Releases rather than committed.

.EXAMPLE
    ./scripts/build-standalone.ps1

.EXAMPLE
    ./scripts/build-standalone.ps1 -Sign -CertificateThumbprint ABC123... -RequireSignature
#>

[CmdletBinding()]
param(
    [string]$Config = "Release",
    [string]$BuildDir,
    [switch]$Sign,
    [switch]$RequireSignature,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}
$AppName    = "USB-Restoration-Tool"
$ExeName    = "usb-restoration-tool.exe"
$OutputRoot = Join-Path $RepoRoot "standalone"
$AppDir     = Join-Path $OutputRoot $AppName

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Get-ProjectVersion {
    $cmake = Join-Path $RepoRoot "CMakeLists.txt"
    $content = Get-Content -Raw -LiteralPath $cmake
    if ($content -match 'project\s*\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        return $Matches[1]
    }
    throw "Could not read PROJECT_VERSION from CMakeLists.txt."
}

$version = Get-ProjectVersion
Write-Host "USB Restoration Tool standalone build" -ForegroundColor Green
Write-Host "  Version    : $version"
Write-Host "  Config     : $Config"
Write-Host "  Build dir  : $BuildDir"
Write-Host "  Output     : $AppDir"

Write-Step "Configuring CMake"
& cmake -B $BuildDir -S $RepoRoot
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Step "Building ($Config)"
& cmake --build $BuildDir --config $Config --target usb-restoration-tool
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

Write-Step "Exporting standalone bundle"
if (Test-Path -LiteralPath $AppDir) {
    Remove-Item -LiteralPath $AppDir -Recurse -Force
}
New-Item -ItemType Directory -Path $AppDir -Force | Out-Null
& cmake --install $BuildDir --config $Config --prefix $AppDir
if ($LASTEXITCODE -ne 0) { throw "Install/export failed." }

# Drop debug artifacts the build may leave behind.
Get-ChildItem -Path $AppDir -Recurse -Include "*.pdb", "*.ilk", "*.exp", "*.lib" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

$exe = Join-Path $AppDir $ExeName
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Exported executable not found at '$exe'. Export likely failed."
}

if ($Sign) {
    Write-Step "Signing $ExeName"
    if (-not $CertificateThumbprint) {
        throw "-Sign requires -CertificateThumbprint."
    }
    & signtool sign /fd SHA256 /td SHA256 /tr $TimestampUrl /sha1 $CertificateThumbprint $exe
    if ($LASTEXITCODE -ne 0) { throw "Authenticode signing failed." }
}

$signature = Get-AuthenticodeSignature -LiteralPath $exe
if ($signature.Status -ne "Valid") {
    $message = "$ExeName is not signed with a trusted certificate. Status: $($signature.Status)"
    if ($RequireSignature) {
        throw $message
    }
    Write-Warning $message
}

Write-Step "Writing README.txt"
$readme = @"
USB Restoration Tool $version - standalone build

WARNING: restoring a drive erases every partition and file on it. Only continue
when you are certain the selected disk is disposable or backed up.

How to run
  1. Extract this folder anywhere.
  2. Run $ExeName and approve the Windows Administrator prompt. Raw disk
     access is not possible without it.
  3. Select the USB disk, type the confirmation phrase the app shows, and
     choose Restore USB.

The disk is restored to one GPT partition formatted exFAT and labelled USB.
Only disks Windows reports on the USB bus are listed; boot, system, offline and
read-only disks are refused.

Keep the DLLs and plugin folders (platforms, styles, tls, ...) beside the
executable when moving or sharing it. If Windows reports missing Microsoft
runtime DLLs on another PC, install the latest Microsoft Visual C++
Redistributable (x64) from Microsoft.

Verify this download against SHA256SUMS.txt before running it.

Project: https://github.com/KaroqDave/USB-Restoration-Tool
"@
Set-Content -LiteralPath (Join-Path $AppDir "README.txt") -Value $readme -Encoding UTF8

Write-Step "Hashing bundle contents"
$sumsPath = Join-Path $AppDir "SHA256SUMS.txt"
$prefixLength = $AppDir.Length + 1
$hashLines = Get-ChildItem -LiteralPath $AppDir -File -Recurse |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($prefixLength).Replace("\", "/")
        $fileHash = Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName
        "$($fileHash.Hash.ToLowerInvariant())  $relative"
    }
Set-Content -LiteralPath $sumsPath -Value $hashLines -Encoding ASCII

Write-Step "Creating release archive"
$zipPath = Join-Path $OutputRoot "$AppName-$version.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path $AppDir -DestinationPath $zipPath -CompressionLevel Optimal

Write-Step "Done"
Write-Host "Bundle  : $AppDir" -ForegroundColor Green
Write-Host "Archive : $zipPath" -ForegroundColor Green
Write-Host "Exe hash: $((Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant())" -ForegroundColor Green
