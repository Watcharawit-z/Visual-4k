The seven-version sweep in v0.2.1 found the cause, and it was not the version.

## What the sweep showed

Every shipped package failed identically:

    IddCx 1.10 .. 1.3    problem code 31, at every single version

That uniformity is the finding. If the class extension version were the
problem, the versions would behave differently -- and one of them does. IddCx
1.2, tested in earlier releases, *accepted* the driver's configuration and ran
on to fail later at the adapter init. Everything from 1.3 up refuses the
configuration outright, and the driver's own code never runs at all.

Something changes between 1.2 and 1.3 that decides whether the configuration is
accepted. What changes is which callbacks `IddCxDeviceInitConfig` treats as
mandatory.

## The cause

Comparing this driver's configuration against Microsoft's own indirect display
sample, line by line, there is exactly one difference:

    the sample sets 8 callbacks, including EvtIddCxDeviceIoControl
    this driver set 7, and never that one

It is set now. It refuses every request, which is what the sample's does and
all this driver needs -- it exposes no control interface of its own.

## If it is still wrong, the next run says so precisely

The sweep printed problem code 31 seven times, which concealed the only thing
that distinguished the attempts: how far each one got before failing.

Each candidate now prints what the driver itself recorded:

    IddCx 1.9:
      not this one (problem code 31)
      reached IddCxDeviceInitConfig, 0xC000000D

So a failure names the refusing call and its status, for every version, in a
single pass.

## Installing

Straight over the previous version. Extract the zip, double-click
**Visual4k-Setup.exe**, choose **1**.

It still tries the packages newest first, so failures scrolling past before one
succeeds are expected rather than errors.

## The sequence so far

    31, EvtDeviceAdd failed        UMDF version mismatch, fixed
    10, EvtDeviceAdd complete      something after it
    10, IddCxAdapterInitAsync      invalid parameter, at IddCx 1.2
    31, no record at all           IddCx 1.9 not present on the machine
    31, at all seven versions      not the version: a missing callback

## Also in here

`--subpixel` on the compositor: resolves each colour channel at its own
emitter's position, recovering about 3 dB of the horizontal detail in text that
an ordinary resolve averages away. RGB-stripe panels only, off by default.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source and of the
driver's diagnostics, a parse of every XML file, and a per-package check that
each shipped driver carries its DLL, INF and signed catalog.
