The virtual display device installed correctly in v0.1.4 and v0.1.5, and then
failed to start. This release fixes the reason.

## What was wrong

Reported from a real machine, and every step of the install had succeeded:

    == Installing the virtual display
      trusted in LocalMachine\Root
      trusted in LocalMachine\TrustedPublisher
      driver package staged in the driver store
      device node created
      driver bound to the device

    == Setting up the displays
    The driver installed, but no virtual display appeared.

Windows knew exactly why:

    Status  : Error
    Problem : CM_PROB_FAILED_ADD

`FAILED_ADD` means the driver *loaded* and its `EvtDeviceAdd` returned a
failure. That rules out the signature, the INF, and test signing, and points
squarely at the driver's own code.

The build was compiling the driver for whichever IddCx version the build
machine happened to have installed -- 1.9 -- while the INF asked Windows to
load `IddCx0102`, version 1.2. Those version macros decide the size and layout
of `IDD_CX_CLIENT_CONFIG`. So the driver handed the 1.2 class extension a
structure it did not recognise, `IddCxDeviceInitConfig` refused it, and
`EvtDeviceAdd` returned that refusal.

Both halves were individually correct. Nothing in the build could see the
disagreement, which is why every release so far was green.

The version is now pinned to match the INF, and a check fails the build if the
two ever diverge again.

## Setup now says why, itself

The message above sent you to Device Manager to find a code Windows had
already recorded. Setup now reads it through `CM_Get_DevNode_Status` and prints
what it means -- that a driver refused its own device, that a signature was
rejected because test signing is not really on, that the registry entry is
damaged and needs option 3 first. Had it done that from the start, diagnosing
this would have been one message instead of three.

## If you installed v0.1.4 or v0.1.5

Remove the broken device first: run the setup program you already have, choose
**3**, and answer **n** when it offers to turn test signing off, since you will
want it again in a moment. Then run this version and choose **1**.

## What the setup program does

Extract the zip, double-click **Visual4k-Setup.exe**, choose 1.

- Asks for administrator rights itself; a double-click raises the UAC prompt.
- Trusts the build's signing certificate in both machine stores.
- Explains what test signing costs, asks, and offers the restart. Run setup
  again afterwards and it carries on from where it stopped.
- Stages the driver and creates the device, through the same SetupAPI calls
  devcon makes -- no WDK, no Device Manager wizard.
- Sets the virtual display to 3840x2160, starts the compositor, and only then
  moves the desktop onto it, so your panel is never showing nothing. If the
  compositor fails to start, the desktop does not move at all.
- Counts down 20 seconds and puts the displays back if you do not confirm.
  Windows' own 15-second revert is a feature of the Settings app, not of the
  API a program calls, so that safety net had to be built rather than assumed.
- Removes all of it again, test signing included, from the same menu.

## Still true

**Nobody has completed an install yet.** But the failure has moved: the device
now installs, binds, and is expected to start, where before it was refused at
the first call into the display stack. That is a different and much later
failure than any previous release reached.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source, a parse of
every XML file, a check that the compiled IddCx version matches the INF, and a
check that the setup program's elevation manifest really embedded. The driver
compiles clean against IddCx 1.2, which also confirms every callback it uses
exists in that version.
