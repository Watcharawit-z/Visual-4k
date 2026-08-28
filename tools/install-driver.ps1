<#
.SYNOPSIS
    Signs and installs the Visual-4k virtual display driver.

.DESCRIPTION
    Windows will not load an unsigned driver. For a machine you own, the normal
    route is test signing: a self-signed certificate the machine is told to
    trust, plus a boot flag that permits test-signed drivers.

    READ THIS BEFORE USING -EnableTestSigning:
    Test signing lowers the security of the whole machine. While it is on,
    Windows will load ANY driver signed by a certificate in the local trust
    stores, not just this one -- so a driver that arrives by some other route
    can also load. Turn it off when you are done experimenting:
        bcdedit /set testsigning off
    Do not leave it on for a machine that holds anything you care about.

    This script never enables test signing on its own. It reports the state and
    stops; enabling it is a separate, explicit decision.

.EXAMPLE
    # Check what would happen, change nothing
    .\tools\install-driver.ps1 -WhatIf

    # Sign and install (test signing must already be on)
    .\tools\install-driver.ps1
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$CertSubject = 'CN=Visual-4k Test Certificate',

    # Turns on the boot flag that permits test-signed drivers. Requires a
    # reboot, and lowers machine security until turned off again. Read the
    # description above before passing this.
    [switch]$EnableTestSigning
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Write-Step($message) { Write-Host "`n==> $message" -ForegroundColor Cyan }
function Fail($message) { Write-Host "`nERROR: $message" -ForegroundColor Red; exit 1 }

# --- preconditions ---------------------------------------------------------

Write-Step "Checking prerequisites"

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Fail "This script must run in an elevated PowerShell (Run as Administrator)."
}

$build = [int](Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').CurrentBuildNumber
if ($build -lt 16299) {
    Fail "IddCx requires Windows 10 build 16299 or later; this machine is build $build."
}
Write-Host "  Windows build $build (IddCx available)"

$driverDir = Join-Path $root "driver\Visual4kDisplay\x64\$Configuration\Visual4kDisplay"
if (-not (Test-Path $driverDir)) {
    $driverDir = Join-Path $root "driver\Visual4kDisplay\x64\$Configuration"
}
$dll = Join-Path $driverDir 'Visual4kDisplay.dll'
$inf = Join-Path $driverDir 'Visual4kDisplay.inf'

if (-not (Test-Path $dll)) {
    Fail "Driver binary not found at $dll. Build it first: .\tools\build.ps1"
}
Write-Host "  driver package: $driverDir"

# --- WDK tooling -----------------------------------------------------------

function Find-KitTool($name) {
    $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")
    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        $found = Get-ChildItem -Path $r -Filter $name -Recurse -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -match '\\x64\\' } |
                 Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

$signtool = Find-KitTool 'signtool.exe'
$inf2cat  = Find-KitTool 'inf2cat.exe'
if (-not $signtool) { Fail "signtool.exe not found. Install the Windows SDK or WDK." }
if (-not $inf2cat)  { Fail "inf2cat.exe not found. Install the WDK." }

# --- test signing state ----------------------------------------------------

Write-Step "Checking driver signature enforcement"

$bcd = & bcdedit /enum '{current}' 2>&1 | Out-String
$testSigningOn = $bcd -match 'testsigning\s+Yes'

if ($testSigningOn) {
    Write-Host "  test signing is ON" -ForegroundColor Yellow
} elseif ($EnableTestSigning) {
    Write-Host @"

  Test signing is currently OFF and you passed -EnableTestSigning.

  This lowers the security of this machine: while it is on, Windows will load
  any driver signed by a certificate in the local trust stores, not only this
  one. Turn it off when you are finished:  bcdedit /set testsigning off

"@ -ForegroundColor Yellow

    if ($PSCmdlet.ShouldProcess("this machine", "Enable test signing (requires reboot)")) {
        & bcdedit /set testsigning on
        if ($LASTEXITCODE -ne 0) {
            Fail "bcdedit failed. Secure Boot may be blocking it; disable Secure Boot in firmware, or sign the driver properly instead."
        }
        Write-Host "`n  Test signing enabled. REBOOT, then run this script again." -ForegroundColor Green
        exit 0
    }
    exit 0
} else {
    Fail @"
Test signing is OFF, so Windows will refuse to load this driver (code 52).

Two ways forward:
  1. Test signing, for a machine you own and are experimenting on:
         .\tools\install-driver.ps1 -EnableTestSigning
     then reboot and run this script again. Read the security note in
     'Get-Help .\tools\install-driver.ps1 -Full' before you do.
  2. A production signature: an EV code-signing certificate and an attestation
     signature from the Windows Hardware Dev Center. This is the right route if
     anyone other than you will install it.
"@
}

# --- certificate -----------------------------------------------------------

Write-Step "Preparing the signing certificate"

$cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $CertSubject } |
        Sort-Object NotAfter -Descending | Select-Object -First 1

if ($cert) {
    Write-Host "  reusing existing certificate, thumbprint $($cert.Thumbprint)"
} else {
    if (-not $PSCmdlet.ShouldProcess($CertSubject, "Create a self-signed code signing certificate")) { exit 0 }
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject `
                                      -CertStoreLocation Cert:\CurrentUser\My `
                                      -KeyUsage DigitalSignature `
                                      -NotAfter (Get-Date).AddYears(2)
    Write-Host "  created certificate, thumbprint $($cert.Thumbprint)"
}

# Windows checks the driver's chain against these two machine stores, so the
# test certificate has to be in both: Root to make the chain valid, and
# TrustedPublisher so the install does not prompt and then fail.
$tmpCer = Join-Path $env:TEMP 'visual4k-test.cer'
Export-Certificate -Cert $cert -FilePath $tmpCer -Force | Out-Null
foreach ($store in 'Root', 'TrustedPublisher') {
    Import-Certificate -FilePath $tmpCer -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
}
Remove-Item $tmpCer -Force
Write-Host "  certificate trusted in LocalMachine\Root and LocalMachine\TrustedPublisher"

# --- catalog and signature -------------------------------------------------

Write-Step "Building and signing the driver catalog"

if (-not $PSCmdlet.ShouldProcess($driverDir, "Generate and sign the driver catalog")) { exit 0 }

& $inf2cat /driver:$driverDir /os:10_X64 /uselocaltime
if ($LASTEXITCODE -ne 0) {
    Fail "inf2cat failed. This is almost always a problem in Visual4kDisplay.inf; its output above names the section."
}

$cat = Join-Path $driverDir 'Visual4kDisplay.cat'
& $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /t http://timestamp.digicert.com $cat
if ($LASTEXITCODE -ne 0) { Fail "signing the catalog failed" }

& $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /t http://timestamp.digicert.com $dll
if ($LASTEXITCODE -ne 0) { Fail "signing the driver binary failed" }

Write-Host "  catalog and binary signed"

# --- install ---------------------------------------------------------------

Write-Step "Installing the driver"

if (-not $PSCmdlet.ShouldProcess("Root\Visual4kDisplay", "Install the virtual display driver")) { exit 0 }

& pnputil /add-driver $inf /install
if ($LASTEXITCODE -ne 0) {
    Fail "pnputil failed to stage the driver package. Check the log at %windir%\INF\setupapi.dev.log."
}

# pnputil stages the package but does not create a root-enumerated device;
# devcon does. Without it the driver is installed but no monitor appears.
$devcon = Find-KitTool 'devcon.exe'
if ($devcon) {
    & $devcon install $inf 'Root\Visual4kDisplay'
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "devcon could not create the device; add it manually (see below)."
    }
} else {
    Write-Warning @"
devcon.exe not found, so the driver is staged but no device exists yet.
Create it by hand:
  Device Manager -> Action -> Add legacy hardware -> Install manually ->
  Display adapters -> Have Disk -> point at:
  $inf
"@
}

Write-Step "Done"
Write-Host @"
Next:
  1. Settings -> System -> Display. A monitor named "Visual-4k Virtual Display"
     should be listed. Set it to 3840x2160 and make it the primary display.
  2. Set your real panel to Extend (not Duplicate -- the compositor draws on it).
  3. Run the compositor:
       .\build\host\visual4k-host\$Configuration\visual4k-host.exe

If no monitor appeared, run:  .\build\edid_selftest.exe
to rule out the EDID, then check Device Manager for a code on the device.
"@ -ForegroundColor Green
