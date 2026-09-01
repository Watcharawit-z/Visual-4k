Two things the last sweep proved, and the release that acts on both.

## What v0.2.2 showed

    IddCx 1.10 .. 1.3    problem code 31
                         the driver's own code did not run

Not one of the seven entered `EvtDeviceAdd`. That rules out the missing
callback from v0.2.2, and rules out configuration validation entirely: the DLL
is not being loaded at all. Every package above 1.2 declares its own version in
its INF and links that version's stub, and none of them load on this machine.

**IddCx 1.2 is the only version that has ever run here -- and the sweep never
shipped it.** The kit provides no 1.2 import library, so the version loop
skipped it. The earlier releases that *did* run were built with 1.2 macros, 1.2
in the INF, and whatever stub the kit happened to provide. The sweep swept past
the one answer it was looking for.

That combination is now built explicitly and shipped alongside the rest.

## The failure 1.2 actually had

`IddCxAdapterInitAsync` refusing `IDDCX_ADAPTER_CAPS` with
`STATUS_INVALID_PARAMETER`, naming no parameter, with every field matching
Microsoft's sample. The one thing a sample cannot disagree about is the layout
the header emits, and the class extension validates `Size` against the version
it is operating as.

So the size is **offered rather than asserted**: the full compiled size first,
then the size implied by the fields this driver actually sets, taking the first
the extension accepts. Every attempt is recorded, so even total failure says
what was tried and what each was refused with.

## Why this release should behave differently

Both remaining unknowns were things only your machine could answer, and both
were being guessed at from a build server:

    which extension version this Windows provides  ->  ship them all, 1.2 included
    which structure layout the extension expects   ->  offer each, take the first accepted

Neither is a guess any more.

## Installing

Straight over the previous version. Extract the zip, double-click
**Visual4k-Setup.exe**, choose **1**.

Failures scrolling past as it works through the packages are expected. Watch
for `the display came up`.

## The whole sequence

    31, EvtDeviceAdd failed        UMDF version mismatch, fixed
    10, EvtDeviceAdd complete      something after it
    10, IddCxAdapterInitAsync      invalid parameter, at IddCx 1.2
    31, no record at all           IddCx 1.9 not present on the machine
    31, at all seven versions      the DLL never loaded: 1.2 was missing
    ->                             1.2 shipped, layout negotiated

## Also in here

`--subpixel` on the compositor: resolves each colour channel at its own
emitter's position, recovering about 3 dB of the horizontal detail in text that
an ordinary resolve averages away. RGB-stripe panels only, off by default.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source and of the
driver's diagnostics, a parse of every XML file, and a per-package check that
each shipped driver carries its DLL, INF and signed catalog.
