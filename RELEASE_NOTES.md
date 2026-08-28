Preparation for the first driver install. The compositor had a bug that would
have made that install look like a total failure, and it is fixed here.

## The bug you would have hit

The output window was created at (0,0) sized to the primary display, so it
always landed on the primary.

That is exactly backwards for the setup the driver exists to create. The
virtual 4K display has to be the *primary* one, because the primary is where
Windows lays out windows, text and games -- laying that out at 4K is the whole
point of the driver. So the compositor would have drawn the resolved image back
onto the virtual display it had just captured, and left the physical panel
dark. The natural conclusion would have been "the driver does not work".

The compositor now picks a panel that is not the source, and positions the
window at that display's own origin instead of at (0,0) -- which is not where a
monitor to the left of or above the primary lives.

Two new ways to be explicit, which matter as soon as a third display is
attached:

    visual4k-host.exe --source \\.\DISPLAYn --output \\.\DISPLAYm

`--list-displays` now prints each display's position alongside its size, so
those names can be read straight off it.

With nothing specified, `--source` now defaults to the primary display rather
than to the first one that happens to be capturable.

## Also fixed

The mouse pointer was clipped against the whole panel rather than against the
area the desktop actually occupies, so with letterboxing a cursor resting on
the source's bottom edge had its lower half drawn onto the black bar. No effect
on the driver path, where the desktop fills the panel.

## Still not established

**The driver has never been installed.** It compiles, links, and is signed by
the build, and its INF has been corrected by inspection -- but nothing has ever
loaded it, and no virtual display has ever appeared. Everything about the
install is untested in the strict sense.

**Windows will not load it unless test signing is enabled**, a boot setting
that lowers the machine's security while it is on. Turn it off when finished:

    bcdedit /set testsigning off

Before touching boot settings, check `manage-bde -status C:` (a BitLocker
machine can demand a recovery key on the next boot) and `Confirm-SecureBootUEFI`
(Secure Boot makes the test-signing flag succeed silently and do nothing). Both
need an elevated PowerShell; the guide now explains how to get one and how to
confirm you have one.

## Verified

78 reference tests, 4 self-tests (tap-table parity against the Python
reference, EDID, cursor decode, aspect fit), and a type-check of every host
source. All green.
