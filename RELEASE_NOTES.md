The compositor ran on real hardware for the first time, and stretched the
image. This release fixes that.

## What this release fixes

A 3440x1440 desktop resolved onto a 2560x1440 panel came out visibly squeezed.
The resolve maps each axis independently, so it was compressing horizontally by
1.34x and not at all vertically: every proportion in the image was wrong.

The compositor now fits the source inside the panel and paints black bars
around it. `--stretch` keeps the old behaviour, which is correct only when the
two aspect ratios already match.

**This does not change the driver path.** A 4K virtual display on a 1440p panel
is 16:9 onto 16:9, so it still fills the screen with no bars. There is a test
asserting exactly that.

## What the first real run established

Seeing a squeezed image rather than a black screen proved several things that
had never been exercised:

- The HLSL compiles on a real GPU. The compositor builds it at runtime, so
  until now no shader compiler had ever seen it.
- Desktop Duplication acquires frames, all three compute passes run, and the
  swap chain presents.
- `visual4k-host --list-displays` enumerates correctly, and `edid_selftest`
  passes every check on Windows.

## Still not established

**The driver has never been installed.** It compiles and links, and its INF has
been corrected by inspection, but nothing has loaded it and no virtual display
has appeared.

**Windows will not load it unless test signing is enabled** -- a boot setting
that lowers the machine's security while on. On a BitLocker machine, changing
boot settings can trigger a recovery-key prompt; have your key first. Both
`manage-bde -status C:` and `Confirm-SecureBootUEFI` need an elevated
PowerShell, which the guide now says.

## Installing

```powershell
# Extract, then open PowerShell in that folder
Get-ChildItem -Recurse | Unblock-File

.\visual4k-host.exe --list-displays
.\edid_selftest.exe

# Then, in an ELEVATED PowerShell:
.\install-driver.ps1 -WhatIf
.\install-driver.ps1
```

With a second display larger than your main one, the compositor can be tried
without the driver at all:

```powershell
.\visual4k-host.exe --source \\.\DISPLAY1
```

Quit from anywhere with Ctrl+Alt+F12.
