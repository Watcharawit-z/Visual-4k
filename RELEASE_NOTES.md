This driver was written from a recollection of Microsoft's indirect display
sample. This release compares it against the actual source, which turned up
three differences -- all of them in or beside the function that was failing.

## What v0.2.3 proved before I opened the source

    IddCx 1.2:  reached IddCxDeviceInitConfig, 0xC000000D

At 1.2 that call used to succeed. The only change since was the
`EvtIddCxDeviceIoControl` callback added in v0.2.2, on my inference that a
mandatory callback was missing. The real sample sets seven callbacks and not
that one -- exactly the seven this driver already had. The inference was wrong,
and it broke the one version that worked. Removed.

## The difference that is almost certainly the original cause

    IDDCX_ENDPOINT_VERSION Version = {};
    Version.Size = sizeof(Version);
    Version.MajorVer = 1;
    AdapterCaps.EndPointDiagnostics.pFirmwareVersion = &Version;
    AdapterCaps.EndPointDiagnostics.pHardwareVersion = &Version;

Both pointers were left null here. A required pointer left null is refused with
`STATUS_INVALID_PARAMETER`, which names no parameter -- and from outside that
is indistinguishable from a wrong structure size, which is exactly what the
last several releases went hunting for. The size-probing added for that hunt is
removed along with it.

## And one structural difference

The sample brings the adapter up in `EvtDeviceD0Entry` and sets no
`PrepareHardware` callback at all. This driver called `IddCxAdapterInitAsync`
from `PrepareHardware`. Moved.

## What this says about the last several releases

None of these three were findable by reasoning about the failure from the
outside, and reasoning about it from the outside is what I kept doing. When the
instrumentation named `IddCxAdapterInitAsync` as the failing call -- several
releases ago -- the right next step was to open the sample, not to reconstruct
it from memory and compare the code against my own reconstruction.

The instrumentation was right every time. I was slow to use it.

## Installing

Straight over the previous version. Extract the zip, double-click
**Visual4k-Setup.exe**, choose **1**.

It still tries each shipped IddCx package newest first; watch for
`the display came up`.

## The whole sequence

    31, EvtDeviceAdd failed        UMDF version mismatch, fixed
    10, EvtDeviceAdd complete      something after it
    10, IddCxAdapterInitAsync      invalid parameter, at IddCx 1.2
    31, no record at all           IddCx 1.9 not present on the machine
    31, at all seven versions      the DLL never loaded: 1.2 was missing
    31, config refused at 1.2      a callback I added that should not exist
    ->                             three differences from the real sample

## Also in here

`--subpixel` on the compositor: resolves each colour channel at its own
emitter's position, recovering about 3 dB of the horizontal detail in text that
an ordinary resolve averages away. RGB-stripe panels only, off by default.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source and of the
driver's diagnostics, a parse of every XML file, and a per-package check that
each shipped driver carries its DLL, INF and signed catalog.
