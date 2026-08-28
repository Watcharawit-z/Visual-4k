The v0.1.0 archive could not install itself. This one can.

## What was wrong with v0.1.0

- `install-driver.ps1` looked for the driver only where a source build puts
  it, so from the release folder it reported "Driver binary not found".
- It then called `inf2cat` and `signtool`, which ship with the WDK and the
  Windows SDK, so using the binaries still required the 90-minute toolchain
  they exist to avoid.
- The INF carried the WDK's `$ARCH$` and `$UMDFVERSION$` stamping tokens
  unsubstituted, never declared `UmdfExtensions` (nothing said the driver
  needs IddCx at all), had two sections that INF's case-insensitive names
  merged into one, and pointed `DestinationDirs` at a section that does not
  exist.

## What changed

The build now signs the driver and ships the public certificate beside it, so
installing needs only Windows' own `pnputil` and `Import-Certificate`. The
private key exists solely inside the build job and is never exported, so
trusting that certificate accepts this driver and nothing else.

`install-driver.ps1` finds the driver in either layout, skips the WDK path
entirely when the package is already signed, and prints the Device Manager
steps when `devcon` is absent, as it will be without the WDK.

## How to install

```powershell
# Extract, then open PowerShell in that folder
Get-ChildItem -Recurse | Unblock-File

# Check the binaries run before touching anything system-wide
.\visual4k-host.exe --list-displays
.\edid_selftest.exe

# Then, in an ELEVATED PowerShell:
.\install-driver.ps1 -WhatIf     # see what it would do
.\install-driver.ps1
```

## Still true, and it matters

**Nothing here has been installed or run.** The driver has never loaded, the
virtual display has never appeared, and the HLSL has never been through a
shader compiler, because the compositor compiles it at runtime on a GPU. The
INF in particular has been corrected by inspection, not by an install.

**Windows will not load the driver unless test signing is enabled.** That is a
boot setting, it needs a reboot, and it lowers the security of the whole
machine while it is on. On a BitLocker machine, changing boot settings can
trigger a recovery-key prompt -- have your key before you start. Read
`GETTING-STARTED.md` first.

**Rendering at 4K costs 2.25x the pixels**, trading frame rate for image
quality. Latency has not been measured, so no figure is quoted.

## Trying it without installing anything

`tools/visual4k.py` runs the same pipeline on image files with only numpy and
pillow, on any OS:

```
pip install numpy pillow
python tools/visual4k.py compare your-4k-shot.png comparison.png
```
