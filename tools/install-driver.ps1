<#
.SYNOPSIS
    Installs the Visual-4k virtual display driver.

.DESCRIPTION
    Works from two layouts, and the difference decides how much you need
    installed:

    A release zip carries a driver already signed by the build, plus the
    matching public certificate. Installing it needs nothing beyond Windows:
    the certificate goes into the machine's trust stores and pnputil stages the
    package.

    A source build carries an unsigned driver, so this script has to sign it,
    which needs inf2cat and signtool from the WDK and Windows SDK.

    READ THIS BEFORE USING -EnableTestSigning:
    Windows will not load either one unless test signing is on. That is a boot
    setting. While it is on, Windows loads ANY driver signed by a certificate
    in the local trust stores, not just this one, so it lowers the security of
    the whole machine. Turn it off when you are done:
        bcdedit /set testsigning off
    On a BitLocker machine, changing boot settings can trigger a recovery-key
    prompt. Have your key before you start.

    This script never enables test signing on its own. It reports the state and
    stops; enabling it is a separate, explicit decision.

.EXAMPLE
    # See what would happen, change nothing
    .\install-driver.ps1 -WhatIf

    # Install (test signing must already be on)
    .\install-driver.ps1
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

# --- locate the driver package ---------------------------------------------
#
# The release zip and a source tree put the driver in different places, and
# guessing only the source layout is why v0.1.0 could not install from its own
# release archive.
$parent = Split-Path -Parent $PSScriptRoot
$candidates = @(
    (Join-Path $PSScriptRoot 'driver'),
    (Join-Path $parent "driver\Visual4kDisplay\x64\$Configuration\Visual4kDisplay"),
    (Join-Path $parent "driver\Visual4kDisplay\x64\$Configuration")
)

$driverDir = $candidates |
             Where-Object { Test-Path (Join-Path $_ 'Visual4kDisplay.dll') } |
             Select-Object -First 1

if (-not $driverDir) {
    Fail @"
Visual4kDisplay.dll was not found. Looked in:
$($candidates | ForEach-Object { "  $_" } | Out-String)
If this is a release zip, extract all of it and run the script from the folder
it came in. If this is a source tree, build it first: .\tools\build.ps1
"@
}

$dll = Join-Path $driverDir 'Visual4kDisplay.dll'
$inf = Join-Path $driverDir 'Visual4kDisplay.inf'
$cat = Join-Path $driverDir 'Visual4kDisplay.cat'
if (-not (Test-Path $inf)) { Fail "Visual4kDisplay.inf is missing from $driverDir" }

Write-Host "  driver package: $driverDir"

# A catalog plus a certificate means the build already signed this; anything
# else has to be signed here, which is what needs the WDK.
$shippedCert = Get-ChildItem $driverDir -Filter '*.cer' -ErrorAction SilentlyContinue |
               Select-Object -First 1
$prepackaged = (Test-Path $cat) -and ($null -ne $shippedCert)

if ($prepackaged) {
    Write-Host "  pre-signed by the build; no WDK needed" -ForegroundColor Green
} else {
    Write-Host "  unsigned; this script will sign it (needs the WDK)"
}

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

  If BitLocker is enabled, this boot change can trigger a recovery-key prompt
  on the next start. Check with: manage-bde -status C:

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
         .\install-driver.ps1 -EnableTestSigning
     then reboot and run this script again. Read the security note in
     'Get-Help .\install-driver.ps1 -Full' before you do.
  2. A production signature: an EV code-signing certificate and an attestation
     signature from the Windows Hardware Dev Center. This is the right route if
     anyone other than you will install it.
"@
}

# --- trust the signing certificate -----------------------------------------

if ($prepackaged) {
    Write-Step "Trusting the build's certificate"

    Write-Host @"
  $($shippedCert.Name) was generated by the build that produced this driver.
  Its private key existed only inside that build and was never exported, so
  nothing can sign new drivers with it. Trusting it lets Windows accept this
  driver and nothing else that is not already signed by it.

"@

    if (-not $PSCmdlet.ShouldProcess($shippedCert.FullName, "Trust this code signing certificate")) { exit 0 }

    foreach ($store in 'Root', 'TrustedPublisher') {
        Import-Certificate -FilePath $shippedCert.FullName `
                           -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
    }
    Write-Host "  trusted in LocalMachine\Root and LocalMachine\TrustedPublisher"

} else {
    # --- WDK tooling -------------------------------------------------------

    function Find-KitTool($name) {
        # Prefer x64, but fall back to whatever the kit ships: inf2cat.exe is
        # x86-only, so an x64-only filter finds signtool and then reports the
        # WDK as missing.
        $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")
        $all = @()
        foreach ($r in $roots) {
            if (-not (Test-Path $r)) { continue }
            $all += Get-ChildItem -Path $r -Filter $name -Recurse -ErrorAction SilentlyContinue
        }
        if (-not $all) { return $null }
        $x64 = $all | Where-Object { $_.FullName -match '\\x64\\' } |
               Sort-Object FullName -Descending | Select-Object -First 1
        if ($x64) { return $x64.FullName }
        return ($all | Sort-Object FullName -Descending | Select-Object -First 1).FullName
    }

    $signtool = Find-KitTool 'signtool.exe'
    $inf2cat  = Find-KitTool 'inf2cat.exe'
    if (-not $signtool) { Fail "signtool.exe not found. Install the Windows SDK, or use a release zip, which ships pre-signed." }
    if (-not $inf2cat)  { Fail "inf2cat.exe not found. Install the WDK, or use a release zip, which ships pre-signed." }

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

    Write-Step "Building and signing the driver catalog"

    if (-not $PSCmdlet.ShouldProcess($driverDir, "Generate and sign the driver catalog")) { exit 0 }

    & $inf2cat /driver:$driverDir /os:10_X64 /uselocaltime
    if ($LASTEXITCODE -ne 0) {
        Fail "inf2cat failed. This is almost always a problem in Visual4kDisplay.inf; its output above names the section."
    }

    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /t http://timestamp.digicert.com $cat
    if ($LASTEXITCODE -ne 0) { Fail "signing the catalog failed" }

    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /t http://timestamp.digicert.com $dll
    if ($LASTEXITCODE -ne 0) { Fail "signing the driver binary failed" }

    Write-Host "  catalog and binary signed"
}

# --- install ---------------------------------------------------------------

Write-Step "Installing the driver"

if (-not $PSCmdlet.ShouldProcess("Root\Visual4kDisplay", "Install the virtual display driver")) { exit 0 }

& pnputil /add-driver $inf /install
if ($LASTEXITCODE -ne 0) {
    Fail "pnputil failed to stage the driver package. Check the log at %windir%\INF\setupapi.dev.log."
}

# pnputil stages the package but does not create a root-enumerated device.
# devcon does, and it ships with the WDK, so a release-zip install usually
# has to create the device by hand.
$devcon = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter devcon.exe `
              -Recurse -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1

$deviceCreated = $false
if ($devcon) {
    & $devcon.FullName install $inf 'Root\Visual4kDisplay'
    $deviceCreated = ($LASTEXITCODE -eq 0)
    if (-not $deviceCreated) { Write-Warning "devcon could not create the device." }
}

Write-Step "Done"

if ($deviceCreated) {
    Write-Host @"
The virtual display device was created.
"@ -ForegroundColor Green
} else {
    Write-Host @"
The driver is staged, but no device exists yet. devcon ships with the WDK and
is not present here, so create the device by hand -- it takes about a minute:

  1. Press Win+X, choose Device Manager
  2. Action -> Add legacy hardware -> Next
  3. "Install the hardware that I manually select from a list" -> Next
  4. Choose "Display adapters" -> Next
  5. Have Disk... -> Browse to:
     $inf
  6. Select "Visual-4k Virtual Display" -> Next -> Finish

"@ -ForegroundColor Yellow
}

Write-Host @"
Then:
  1. Settings -> System -> Display. A monitor named "Visual-4k Virtual Display"
     should be listed. Set it to 3840x2160 and make it the primary display.
  2. Set your real panel to Extend (not Duplicate -- the compositor draws on it).
  3. Run the compositor:
       .\visual4k-host.exe

  If the screen goes black, wait 15 seconds: Windows reverts a display change
  you do not confirm. Quit the compositor from anywhere with Ctrl+Alt+F12.

If no monitor appeared, run .\edid_selftest.exe to rule out the EDID, then
check Device Manager for an error code on the device.
"@ -ForegroundColor Green
