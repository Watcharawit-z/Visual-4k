<#
.SYNOPSIS
    Removes the Visual-4k virtual display driver and its test certificate.

.DESCRIPTION
    Reverses tools/install-driver.ps1. It does NOT turn test signing back off,
    because that needs a reboot and you may still be using it for something
    else. When you are finished with it:
        bcdedit /set testsigning off
    and reboot. Leaving test signing on lowers the machine's security.
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$CertSubject = 'CN=Visual-4k Test Certificate',
    [switch]$KeepCertificate
)

$ErrorActionPreference = 'Continue'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: run this in an elevated PowerShell." -ForegroundColor Red
    exit 1
}

Write-Host "`n==> Removing the device" -ForegroundColor Cyan
$devcon = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter devcon.exe `
              -Recurse -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
if ($devcon -and $PSCmdlet.ShouldProcess('Root\Visual4kDisplay', 'Remove device')) {
    & $devcon.FullName remove 'Root\Visual4kDisplay'
} else {
    Write-Warning "devcon not found; remove 'Visual-4k Virtual Display' from Device Manager by hand."
}

Write-Host "`n==> Removing the driver package" -ForegroundColor Cyan
$packages = & pnputil /enum-drivers | Out-String
$matches = [regex]::Matches($packages,
    'Published Name:\s+(oem\d+\.inf)[\s\S]*?Original Name:\s+Visual4kDisplay\.inf')

if ($matches.Count -eq 0) {
    Write-Host "  no Visual4kDisplay package is staged"
}
foreach ($m in $matches) {
    $oem = $m.Groups[1].Value
    if ($PSCmdlet.ShouldProcess($oem, 'Delete driver package')) {
        & pnputil /delete-driver $oem /uninstall /force
    }
}

if (-not $KeepCertificate) {
    Write-Host "`n==> Removing the test certificate" -ForegroundColor Cyan
    foreach ($path in 'Cert:\CurrentUser\My', 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher') {
        Get-ChildItem $path -ErrorAction SilentlyContinue |
            Where-Object { $_.Subject -eq $CertSubject } |
            ForEach-Object {
                if ($PSCmdlet.ShouldProcess("$path\$($_.Thumbprint)", 'Remove certificate')) {
                    Remove-Item $_.PSPath -Force
                }
            }
    }
}

Write-Host @"

Done.

Test signing is still enabled. If you are finished experimenting, turn it back
off -- it lowers this machine's security while it is on:
    bcdedit /set testsigning off
then reboot.
"@ -ForegroundColor Green
