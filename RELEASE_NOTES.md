The driver now tells you why it failed, instead of leaving Windows to say only
that it did. Plus a second version mismatch, of exactly the kind that was fixed
last time in only one of the two places it existed.

## Why v0.1.7 still failed

The install was clean -- `device node created` rather than `already exists`, on
a freshly stamped DriverVer -- which proves the IddCx fix from v0.1.6 was
genuinely loaded this time. It was not the cause. Problem code 31 again.

The identical mistake was sitting one line away in the same file:

    INF:  UmdfLibraryVersion = 2.33.0
    CI:   built against whatever UMDF the build machine had, which is 2.35

Same class of bug as the IddCx one, same invisibility to the build, same
symptom. It got fixed for IddCx and left in place for UMDF because only one of
the two was looked at. Both are pinned now, and `check-driver-versions.py`
checks both rather than one.

## The driver records what it does

That fix is still a guess, and it is the third. So this release stops the
guessing.

Windows reports a failed `EvtDeviceAdd` as problem code 31 and nothing more.
That names the callback but not the call inside it, and there are three
candidates: `IddCxDeviceInitConfig`, `WdfDeviceCreate`, `IddCxDeviceInitialize`.
Every round so far has been an attempt to infer from outside which one it was,
at the cost of a release and an install on someone else's machine, and two of
those inferences were wrong.

The driver now writes down each step and its status code, to the debugger
stream and to a registry value that outlives the failure. Setup creates that
key before the device exists -- with an ACL permitting the service accounts a
driver host runs under, since the driver cannot create it itself -- and prints
what it finds when no display appears:

    What the driver itself recorded:
      last step : IddCxDeviceInitConfig
      status    : 0xC000000D
      at        : 2026-09-01 06:40:12Z

Whatever happens next, it will be a fact rather than a fourth guess. Including
the case where nothing was recorded, which is also an answer: it means the
failure happens before any of the driver's own code runs.

Recording success matters as much as failure. A record ending at
`EvtDeviceAdd complete` separates a driver that never ran from one that ran
correctly and still produced no display.

## If you have a previous version installed

Remove it first: run the setup program you already have, choose **3**, answer
**n** when it offers to turn test signing off. Then install this one with **1**.

## Also still here

`--subpixel` on the compositor, which resolves each colour channel at its own
emitter's position and recovers about 3 dB of the horizontal detail in text
that an ordinary resolve averages away. RGB-stripe panels only; off by default.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source *and the
driver's diagnostics* -- that file broke the build once, on errors a Linux
compiler would have caught in seconds, so it is checked there now -- a parse of
every XML file, and a check that both declared framework versions match the
INF.

**Nobody has completed an install yet.** But for the first time, a failure will
name its own cause.
