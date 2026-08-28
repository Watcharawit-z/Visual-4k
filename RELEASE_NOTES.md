Extract the zip and double-click **Visual4k-Setup.exe**. That is the whole
procedure.

This is v0.1.4 plus one hardening change to the step most likely to fail in a
way that misleads you.

## What changed since v0.1.4

Setup recognised the new virtual display only by its adapter description
matching "Visual-4k". Nothing guarantees Windows describes the adapter with the
name from the INF, and if it does not, setup would have reported that no
virtual display appeared. That reads as the driver having failed, when in fact
the device installed correctly and only the name was unexpected -- and it would
have sent both of us looking in entirely the wrong place.

It now identifies the display four ways, in descending confidence:

1. The adapter's description.
2. The monitor's description, which Windows reads out of the EDID the driver
   reports, and which carries the same name.
3. Whichever display is attached now but was not attached before the device was
   created. This needs no name at all, so nothing about how Windows chooses to
   describe the device can defeat it.
4. Failing all of that, it lists the attached displays and asks. Someone
   looking at their own monitors can pick out the entry they do not recognise
   in one keystroke, where the program cannot.

The failure message, if it still gets there, now names **code 52** and says
what it means: Windows refused the signature, which means test signing is not
actually on yet.

## What the setup program does

One executable, no dependencies, nothing to install first:

- Asks for administrator rights itself, through an embedded manifest -- a
  double-click raises the UAC prompt rather than failing at the first step.
- Trusts the build's signing certificate in both machine stores.
- Explains what test signing costs before turning it on, asks, and offers the
  restart. (Run setup again after the restart; it picks up where it left off.)
- Stages the driver **and creates the device**, through the same SetupAPI calls
  devcon makes. This is the step that previously required either the 90-minute
  WDK install or six passes through Device Manager's "Add legacy hardware"
  wizard, because a root-enumerated driver has no hardware to bind to and the
  device node has to be conjured before Windows will load it.
- Sets the virtual display to 3840x2160, starts the compositor, and only then
  moves the desktop onto the virtual display.
- Removes all of it again, test signing included, from the same menu.

## The order of those last steps is the point

The compositor is drawing the virtual display onto your panel *before* the
desktop moves there, so the panel is never showing nothing. If the compositor
fails to start, setup does not move the desktop at all.

And the confirmation countdown is the program's own, deliberately. The
15-second "Keep these changes?" revert people know from Windows is a feature of
the Settings app, not of the API a program calls: `ChangeDisplaySettingsEx`
with `CDS_UPDATEREGISTRY` takes effect permanently the moment it is applied,
with no timeout behind it. So setup captures the display layout first and puts
it back if nobody confirms within 20 seconds.

## Still true

**Nobody has completed an install yet, including the author.** Every piece
compiles, the numerics are tested, and the compositor has run on real hardware
-- but no virtual display has ever appeared on any machine. These last two
releases are about making the attempt cheap and its failures legible, not about
having proven it works.

Test signing must be on for Windows to load this driver at all. It is a boot
setting that lowers the machine's security while it lasts. Setup says so, asks,
and turns it back off when you remove Visual-4k.

## Verified

78 reference tests, 4 self-tests (tap-table parity against the Python
reference, EDID, cursor decode, aspect fit), a type-check of every host source,
a parse of every XML file in the tree, and a check that the setup program's
elevation manifest really embedded -- since an exe missing it fails in a way
the build cannot see.
