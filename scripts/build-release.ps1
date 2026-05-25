param(
    [string] $QtDir = $env:Qt6_DIR
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
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

function Get-CMakeGenerator {
    param([Parameter(Mandatory = $true)][string] $VsInstallPath)

    $json = @(& $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
    $major = $null
    if ($json -and $json[0].catalog.productLineVersion) {
        $major = [int] $json[0].catalog.productLineVersion
    } elseif ($json -and $json[0].installationVersion) {
        $major = [int] (($json[0].installationVersion -split '\.')[0])
    }

    switch ($major) {
        18 { return 'Visual Studio 18 2026' }
        17 { return 'Visual Studio 17 2022' }
        default {
            if ($VsInstallPath -match '2022') { return 'Visual Studio 17 2022' }
            if ($VsInstallPath -match '\\18\\') { return 'Visual Studio 18 2026' }
            throw "Unsupported Visual Studio generator for installation at $VsInstallPath"
        }
    }
}

if (-not $QtDir) {
    throw 'Pass -QtDir, for example: .\scripts\build-release.ps1 -QtDir "C:\Qt\6.7.3\msvc2022_64"'
}

$QtDir = (Resolve-Path -LiteralPath $QtDir).Path
$VsPath = Get-VsInstallPath
$VcVars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
$CMake = Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Generator = Get-CMakeGenerator -VsInstallPath $VsPath

if (-not (Test-Path -LiteralPath $VcVars)) {
    throw "vcvars64.bat was not found at $VcVars"
}
if (-not (Test-Path -LiteralPath $CMake)) {
    throw "cmake.exe was not found at $CMake"
}

Push-Location $Root
try {
    $configure = "`"$VcVars`" >nul && `"$CMake`" -S . -B build -G `"$Generator`" -A x64 -DCMAKE_PREFIX_PATH=`"$QtDir`""
    $build = "`"$VcVars`" >nul && `"$CMake`" --build build --config Release"
    cmd.exe /d /s /c $configure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmd.exe /d /s /c $build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}
