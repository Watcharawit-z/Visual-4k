Installing this no longer requires you to be comfortable with PowerShell,
Device Manager, or display settings. Extract the zip and double-click
**Visual4k-Setup.exe**.

## Why this release exists

The previous route did not work in practice, and the reason was not subtle:
Windows' default execution policy refuses to run a downloaded `.ps1` at all.

    .\install-driver.ps1 : File ...\install-driver.ps1 cannot be loaded because
    running scripts is disabled on this system.

Past that lay the genuinely hard step. This driver is *root-enumerated*: it has
no hardware to bind to, so the device node has to be conjured before Windows
will ever load it. `pnputil` stages the package and stops there. The two
documented ways to create the node are `devcon`, which ships only inside the
90-minute WDK install, and six steps through Device Manager's "Add legacy
hardware" wizard.

## What the setup program does

One executable, no dependencies, nothing to install first:

- Asks for administrator rights itself, through an embedded manifest -- a
  double-click raises the UAC prompt rather than failing at the first step.
- Trusts the build's signing certificate in both machine stores.
- Explains what test signing costs before turning it on, asks, and offers the
  restart.
- Stages the driver **and creates the device**, through the same SetupAPI calls
  devcon makes. No WDK, no wizard.
- Sets the virtual display to 3840x2160, starts the compositor, and only then
  moves the desktop onto the virtual display.
- Removes all of it again, test signing included, from the same menu.

## The order of those last steps is the point

The compositor is drawing the virtual display onto your panel *before* the
desktop moves there, so the panel is never showing nothing.

And the confirmation countdown is the program's own, deliberately. The
15-second "Keep these changes?" revert people know from Windows is a feature of
the Settings app, not of the API a program calls: `ChangeDisplaySettingsEx` with
`CDS_UPDATEREGISTRY` takes effect permanently the moment it is applied, with no
timeout behind it. So setup captures the display layout first, and puts it back
if nobody confirms within 20 seconds. If the compositor fails to start, setup
does not move the desktop at all.

## Also in this release

The compositor drew its output window on the primary display, which is exactly
the display the driver exists to make virtual. Installing the driver and
running the compositor would have painted the resolved image back onto the
display it had just captured and left the physical panel dark. It now draws on
a display that is not the source, positioned at that display's own origin, with
`--source` and `--output` to say which when more than two are attached.

The mouse pointer was clipped against the whole panel rather than the area the
desktop occupies, so with letterboxing a cursor on the bottom edge had its
lower half drawn onto the black bar.

## Still true

**Nobody has completed an install yet, including the author.** Every piece
compiles, the numerics are tested, and the compositor has run on real hardware
-- but no virtual display has ever appeared on any machine. This release is the
first one where attempting it is reasonable rather than laborious.

Before it will work at all, test signing has to be on, which is a boot setting
that lowers the machine's security while it lasts. Setup says so and asks.

## Verified

78 reference tests, 4 self-tests (tap-table parity against the Python
reference, EDID, cursor decode, aspect fit), a type-check of every host source,
and a check that the setup program's elevation manifest really embedded --
since an exe missing it fails in a way the build cannot see.
