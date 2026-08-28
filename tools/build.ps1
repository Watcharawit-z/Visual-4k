<#
.SYNOPSIS
    Builds the Visual-4k compositor and, when the WDK is present, the driver.

.DESCRIPTION
    Run from anywhere; paths are resolved relative to the repository root.
    The compositor and the driver are independent: the compositor is useful on
    its own against any display larger than your panel (an NVIDIA DSR or AMD VSR
    mode, or a second monitor), so a missing WDK is reported and skipped rather
    than treated as a failure.

.EXAMPLE
    .\tools\build.ps1
    .\tools\build.ps1 -Configuration Debug
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [switch]$SkipDriver
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Write-Step($message) {
    Write-Host "`n==> $message" -ForegroundColor Cyan
}

# --- compositor ------------------------------------------------------------

Write-Step "Building visual4k-host ($Configuration)"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found on PATH. Install CMake 3.20+ or use the one bundled with Visual Studio."
}

$buildDir = Join-Path $root 'build'
& cmake -B $buildDir -S $root -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$hostExe = Join-Path $buildDir "host\visual4k-host\$Configuration\visual4k-host.exe"
if (Test-Path $hostExe) {
    Write-Host "  compositor: $hostExe" -ForegroundColor Green
} else {
    Write-Warning "  build reported success but $hostExe is missing"
}

# --- self-tests ------------------------------------------------------------

Write-Step "Running the portable self-tests"
Push-Location $root
try {
    & ctest --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "self-tests reported failures; the numerics may be wrong"
    }
} finally {
    Pop-Location
}

# --- driver ----------------------------------------------------------------

if ($SkipDriver) {
    Write-Step "Skipping the driver (-SkipDriver)"
    return
}

Write-Step "Building Visual4kDisplay"

$msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
if (-not $msbuild) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) { $msbuild = $candidate }
        }
    }
}

if (-not $msbuild) {
    Write-Warning @"
MSBuild not found; skipping the driver.
The compositor above is still usable on its own -- point it at any display
larger than your panel with --source. See docs/BUILD-WINDOWS.md.
"@
    return
}

$proj = Join-Path $root 'driver\Visual4kDisplay\Visual4kDisplay.vcxproj'
& $msbuild $proj /p:Configuration=$Configuration /p:Platform=x64 /v:minimal

if ($LASTEXITCODE -ne 0) {
    Write-Warning @"
Driver build failed. The usual cause is a missing Windows Driver Kit: the
WindowsUserModeDriver10.0 platform toolset and the IddCx headers ship with it,
not with Visual Studio.
  https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk

The compositor built above is unaffected and can be used without the driver.
"@
    return
}

Write-Host "`n  driver built. Install it with: .\tools\install-driver.ps1" -ForegroundColor Green
