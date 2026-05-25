param(
    [string] $QtDir = $env:Qt6_DIR,
    [switch] $Sign,
    [switch] $RequireSignature,
    [string] $CertificateThumbprint,
    [string] $TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Version = '0.1.0'
$Exe = Join-Path $Root 'build\Release\USBRestorationTool.exe'
$Stage = Join-Path $Root 'dist\USBRestorationTool'
$ReleaseDir = Join-Path $Root 'release'
$Archive = Join-Path $ReleaseDir "USB-Restoration-Tool-v$Version-windows-x64.zip"
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

function Get-VsInstallPath {
    if (-not (Test-Path -LiteralPath $VsWhere)) {
        throw "vswhere.exe was not found at $VsWhere"
    }

    $path = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $path) {
        throw 'Visual Studio Build Tools with MSVC x64 were not found.'
    }
    return $path.Trim()
}

function Copy-MsvcRuntime {
    param(
        [Parameter(Mandatory = $true)][string] $VsPath,
        [Parameter(Mandatory = $true)][string] $Destination
    )

    $redistRoot = Join-Path $VsPath 'VC\Redist\MSVC'
    if (-not (Test-Path -LiteralPath $redistRoot)) {
        throw "MSVC redist folder was not found at $redistRoot"
    }

    $crtDir = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object {
            Get-ChildItem -LiteralPath (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue
        } |
        Select-Object -First 1

    if (-not $crtDir) {
        throw "MSVC x64 CRT runtime folder was not found under $redistRoot"
    }

    Get-ChildItem -LiteralPath $crtDir.FullName -Filter '*.dll' |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $Destination -Force
        }
}

if (-not $QtDir) {
    throw 'Pass -QtDir, for example: .\scripts\package-release.ps1 -QtDir "C:\Qt\6.7.3\msvc2022_64"'
}

$QtDir = (Resolve-Path -LiteralPath $QtDir).Path
$VsPath = Get-VsInstallPath
$VcVars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'

if (-not (Test-Path -LiteralPath $VcVars)) {
    throw "vcvars64.bat was not found at $VcVars"
}

& (Join-Path $Root 'scripts\build-release.ps1') -QtDir $QtDir

if (Test-Path -LiteralPath $Stage) {
    Remove-Item -LiteralPath $Stage -Recurse -Force
}
New-Item -ItemType Directory -Path $Stage | Out-Null
New-Item -ItemType Directory -Path $ReleaseDir -Force | Out-Null

Copy-Item -LiteralPath $Exe -Destination $Stage
Copy-Item -LiteralPath (Join-Path $Root 'RELEASE_README.txt') -Destination (Join-Path $Stage 'README.txt')

$WinDeployQt = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $WinDeployQt)) {
    throw "windeployqt.exe was not found at $WinDeployQt"
}
$deployCommand = "`"$VcVars`" >nul && `"$WinDeployQt`" --release --no-translations `"$((Join-Path $Stage 'USBRestorationTool.exe'))`""
cmd.exe /d /s /c $deployCommand
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Get-ChildItem -LiteralPath $Stage -Filter 'vc_redist*.exe' -ErrorAction SilentlyContinue |
    Remove-Item -Force
Copy-MsvcRuntime -VsPath $VsPath -Destination $Stage

if ($Sign) {
    if (-not $CertificateThumbprint) {
        throw '-Sign requires -CertificateThumbprint.'
    }
    signtool sign /fd SHA256 /td SHA256 /tr $TimestampUrl /sha1 $CertificateThumbprint (Join-Path $Stage 'USBRestorationTool.exe')
}

$signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $Stage 'USBRestorationTool.exe')
if ($signature.Status -ne 'Valid') {
    $message = "USBRestorationTool.exe is not signed with a trusted certificate. Status: $($signature.Status)"
    if ($RequireSignature) {
        throw $message
    }
    Write-Warning $message
}

$ExeHash = Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Stage 'USBRestorationTool.exe')
$hashLines = Get-ChildItem -LiteralPath $Stage -File -Recurse |
    Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($Stage.Length + 1).Replace('\', '/')
        $fileHash = Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName
        "$($fileHash.Hash.ToLowerInvariant())  $relative"
    }
$hashLines | Set-Content -Encoding ASCII -Path (Join-Path $Stage 'SHA256SUMS.txt')

if (Test-Path -LiteralPath $Archive) {
    Remove-Item -LiteralPath $Archive -Force
}
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $Archive

Write-Host "Release archive: $Archive"
Write-Host "SHA256: $($ExeHash.Hash.ToLowerInvariant())"
