An instrumentation-only release. Nothing is fixed here; this exists to make the
next fix a deduction rather than a fourth guess.

## Where the failure is now

v0.1.9's instrumentation named the failing call exactly:

    problem code 10
    last step : IddCxAdapterInitAsync
    status    : 0xC000000D

`0xC000000D` is `STATUS_INVALID_PARAMETER`. So: the driver loads, its INF and
signature are accepted, `EvtDeviceAdd` succeeds, the device reaches
`EvtDevicePrepareHardware`, and the display class extension rejects the
arguments it is handed there.

The failure has moved three times across the last four releases, each time to a
later stage:

    31, EvtDeviceAdd failed          ->  the driver's config was refused
    10, EvtDeviceAdd complete        ->  something after it failed
    10, IddCxAdapterInitAsync        ->  this one call, with a named status

## What this release adds

The adapter capabilities are set exactly as Microsoft's own indirect display
sample sets them, field for field. That rules out the obvious reading of
"invalid parameter" and leaves the one thing a sample cannot disagree about:
the sizes of the structures.

Each of those structures carries a `Size` that the class extension validates
against the version it is operating as. A structure compiled to one version's
layout and handed to another is rejected as an invalid parameter, named as
nothing more specific -- which is exactly the shape of what we have, and
exactly what a version mismatch has already produced twice in this driver.

So the driver now records what the compiler decided, before making the call
that rejects it:

    built with: iddcx=1.2 umdf=2.33 caps=... diag=... init=...
                monitorInfo=... clientConfig=...

Those numbers settle whether the version macros change these layouts at all,
or only gate which functions get declared. If the sizes are a newer version's
while the INF asks for IddCx 1.2, that is the answer and the fix follows from
it directly. If they are 1.2's sizes, a whole class of cause is eliminated and
the search moves elsewhere.

## Why no fix in this release

Three fixes have been attempted on this failure. Two were wrong, and each cost
a release and an install. The information this release collects costs the same
one install and cannot be wrong.

## Installing

Straight over the previous version; removing it first is no longer necessary.

Extract the zip, double-click **Visual4k-Setup.exe**, choose **1**.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source and of the
driver's diagnostics, a parse of every XML file, and a check that both declared
framework versions match the INF.
