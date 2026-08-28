<#
.SYNOPSIS
    Downloads the latest Visual-4k build and installs it.

.DESCRIPTION
    One command instead of a toolchain. It fetches the release binaries built
    by CI, unpacks them, and hands off to install-driver.ps1.

    WHAT THIS CANNOT DO
    Windows refuses to load a driver that Microsoft has not signed, and getting
    that signature requires an EV code-signing certificate and a submission to
    the Windows Hardware Dev Center. This project has neither. So the driver
    half of the install still needs test signing turned on, which is a boot
    setting, needs a reboot, and lowers the machine's security while it is on.
    There is no way around that short of buying a certificate -- it is a
    Windows security guarantee, not a missing feature here.

    The compositor half needs none of that and runs as a normal program.

.EXAMPLE
    # Compositor only -- no driver, no reboot, no security tradeoff.
    .\Install-Visual4k.ps1 -CompositorOnly

    # Everything, once you have read the risks in docs/GETTING-STARTED.md.
    .\Install-Visual4k.ps1
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$InstallPath = "$env:LOCALAPPDATA\Visual-4k",

    # Skip the driver entirely. The compositor still works against any display
    # larger than your panel, and against nothing at all if you only want the
    # offline image tools.
    [switch]$CompositorOnly,

    # Use a local zip instead of downloading, for offline installs.
    [string]$FromZip,

    [string]$Repository = 'Watcharawit-z/Visual-4k'
)

$ErrorActionPreference = 'Stop'

function Write-Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "`nERROR: $m" -ForegroundColor Red; exit 1 }

Write-Host @"

  Visual-4k installer
  ===================
"@ -ForegroundColor Cyan

# --- checks ----------------------------------------------------------------

Write-Step "Checking this machine"

$build = [int](Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').CurrentBuildNumber
Write-Host "  Windows build $build"
if (-not $CompositorOnly -and $build -lt 16299) {
    Fail "The virtual display needs Windows 10 build 16299 or later (this is $build). Re-run with -CompositorOnly."
}

$panel = Get-CimInstance Win32_VideoController |
         Where-Object { $_.CurrentHorizontalResolution } |
         Select-Object -First 1
if ($panel) {
    Write-Host ("  panel: {0}x{1}" -f $panel.CurrentHorizontalResolution,
                                      $panel.CurrentVerticalResolution)
}

# --- fetch -----------------------------------------------------------------

$zip = $FromZip

if (-not $zip) {
    Write-Step "Finding the latest release"

    $api = "https://api.github.com/repos/$Repository/releases/latest"
    try {
        $release = Invoke-RestMethod -Uri $api -UseBasicParsing `
                                     -Headers @{ 'User-Agent' = 'visual4k-installer' }
    } catch {
        Fail @"
Could not reach the release API: $($_.Exception.Message)

If no release has been published yet, build from source instead:
  https://github.com/$Repository/blob/main/docs/GETTING-STARTED.md
"@
    }

    $asset = $release.assets | Where-Object { $_.name -like 'Visual-4k-*.zip' } |
             Select-Object -First 1
    if (-not $asset) { Fail "Release $($release.tag_name) has no Visual-4k zip attached." }

    Write-Host "  $($release.tag_name) -- $($asset.name) ($([math]::Round($asset.size/1MB,1)) MB)"

    $zip = Join-Path $env:TEMP $asset.name
    Write-Step "Downloading"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -UseBasicParsing
}

if (-not (Test-Path $zip)) { Fail "Archive not found: $zip" }

# --- unpack ----------------------------------------------------------------

Write-Step "Installing to $InstallPath"

if (-not $PSCmdlet.ShouldProcess($InstallPath, "Unpack Visual-4k")) { exit 0 }

if (Test-Path $InstallPath) {
    # Anything still running would hold the exe open and make the expand fail
    # with a confusing access error.
    Get-Process visual4k-host -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    Remove-Item -Recurse -Force $InstallPath
}
New-Item -ItemType Directory -Force -Path $InstallPath | Out-Null

$staging = Join-Path $env:TEMP "visual4k-unpack-$PID"
Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue
Expand-Archive -Path $zip -DestinationPath $staging -Force

# The zip has a single top-level folder; flatten it so paths stay predictable.
$inner = Get-ChildItem $staging -Directory | Select-Object -First 1
$source = if ($inner -and -not (Test-Path (Join-Path $staging 'visual4k-host.exe'))) {
    $inner.FullName
} else { $staging }
Copy-Item -Recurse -Force "$source\*" $InstallPath
Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue

$exe = Join-Path $InstallPath 'visual4k-host.exe'
if (-not (Test-Path $exe)) { Fail "visual4k-host.exe is missing from the archive." }
if (-not (Test-Path (Join-Path $InstallPath 'shaders'))) {
    Fail "The shaders folder is missing from the archive; the compositor would render black."
}
Write-Host "  unpacked"

# --- shortcut --------------------------------------------------------------

$shortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Visual-4k.lnk'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = $exe
$link.WorkingDirectory = $InstallPath
$link.Description = 'Visual-4k supersampling compositor'
$link.Save()
Write-Host "  desktop shortcut created"

# --- driver ----------------------------------------------------------------

if ($CompositorOnly) {
    Write-Step "Done (compositor only)"
    Write-Host @"

The compositor is installed at:
  $exe

Without the virtual display driver it can still resolve any display larger
than your panel:
  & "$exe" --list-displays
  & "$exe" --source \\.\DISPLAY2

"@ -ForegroundColor Green
    exit 0
}

Write-Step "Driver"

$installer = Join-Path $InstallPath 'install-driver.ps1'
if (-not (Test-Path $installer)) {
    Write-Warning "install-driver.ps1 is not in the archive; skipping the driver."
    exit 0
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host @"

The compositor is installed. The driver needs an elevated shell:

  Start -> right-click Windows Terminal -> Run as administrator, then:
    cd "$InstallPath"
    .\install-driver.ps1 -WhatIf      # see what it would do
    .\install-driver.ps1              # do it

Read the risks first -- BitLocker and Secure Boot can both bite here:
  $InstallPath\GETTING-STARTED.md

"@ -ForegroundColor Yellow
    exit 0
}

Write-Host @"

  About to install a kernel-adjacent display driver.

  Windows will not load it unless test signing is on, which is a boot setting
  that needs a reboot and lowers this machine's security while enabled. On a
  BitLocker machine, changing boot settings can trigger a recovery-key prompt.

  Read $InstallPath\GETTING-STARTED.md before continuing.

"@ -ForegroundColor Yellow

& $installer
