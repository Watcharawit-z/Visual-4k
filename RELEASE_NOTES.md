This release stops guessing which version of the display class extension your
Windows provides, and finds out on your machine instead.

## What v0.2.0 actually showed

It failed with problem code 31 and this record:

    at        : 2026-09-01 08:31:08Z
    built with: iddcx=1.2 ...

Both lines are from the *previous* run. v0.2.0 was built for IddCx 1.9 and
wrote nothing at all, which means `EvtDeviceAdd` was never entered: Windows
could not load the 1.9 class extension.

So both guesses were wrong in opposite directions. 1.2 loaded but was too old
for the structures the header emits, and the adapter init was refused. 1.9 is
too new for this machine to provide, and the driver's own code never ran.

A stale record complete with its own timestamp and build version looks exactly
like a fresh answer, and it nearly sent the next fix in the wrong direction.
Setup now clears the record before every attempt.

## What this release does instead

The build compiles **one driver package per IddCx version the kit supports**,
signs each with its own catalog, and ships them all -- seven of them in this
archive.

Setup installs them newest first and keeps the one whose device actually
starts, removing each failure before trying the next so the following attempt
is a clean create rather than an update of a broken node.

Success is judged by the device starting, never by the install reporting
success. Every failure in this sequence reported success at every install step.

You will see it work through them:

    7 driver packages available; trying newest first

      IddCx 1.9:
        not this one (problem code 31)
      IddCx 1.8:
        ...

The machine has the answer; there is no reason for me to keep guessing at it
from a build server.

## Installing

Straight over the previous version. Extract the zip, double-click
**Visual4k-Setup.exe**, choose **1**.

The install takes longer than before -- it may try several packages -- and the
per-attempt failures scrolling past are expected, not errors.

## Where this sequence has got to

    31, EvtDeviceAdd failed        UMDF version mismatch, fixed
    10, EvtDeviceAdd complete      something after it
    10, IddCxAdapterInitAsync      invalid parameter, at IddCx 1.2
    31, no record at all           IddCx 1.9 not present on the machine
    ->                             every version, tried on the machine

## Also in here

`--subpixel` on the compositor: resolves each colour channel at its own
emitter's position, recovering about 3 dB of the horizontal detail in text that
an ordinary resolve averages away. RGB-stripe panels only, off by default.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source and of the
driver's diagnostics, a parse of every XML file, and a per-package check that
each shipped driver carries its DLL, INF and signed catalog.
