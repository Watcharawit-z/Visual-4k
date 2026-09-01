The device now gets past `EvtDeviceAdd`. This release fixes a defect found in
the code that runs after it, and instruments the rest of that path.

## What v0.1.8 established

The instrumentation earned its keep on its first run:

    problem code 10, not 31
    last step : EvtDeviceAdd complete
    status    : 0x00000000

Two facts, neither of which was available before. The UMDF version fix was
right -- `EvtDeviceAdd` now succeeds where it had been failing since the
beginning. And the failure moved to the device's *start*, in the callbacks that
run afterwards.

## The defect that was there to find

`CreateMonitor` carried this comment:

> Container ID groups the virtual monitor under one device in Settings.
> Reusing a fixed GUID keeps the user's per-monitor arrangement stable across
> reboots instead of scattering a new display every time.

The field was never assigned. It went to Windows as a null GUID, in the place
where the identity of the monitor is supposed to be.

The comment described the intent and the code skipped it, which is exactly the
kind of mistake that survives being read repeatedly: the comment reads as
though it had been done. It only came to light because the instrumentation
narrowed the search to one function.

It is now a fixed GUID, as the comment always said it should be.

## The rest of the start path reports too

Whether the container ID was the whole cause is not something to assert, so
every step after `EvtDeviceAdd` now records its outcome the same way:
`IddCxAdapterInitAsync`, `EvtDevicePrepareHardware`, `EvtDeviceD0Entry`,
`EvtAdapterInitFinished`, `IddCxMonitorCreate`, `IddCxMonitorArrival` and
`EvtParseMonitorDescription`.

If it fails again, the record will say which of those was reached and what it
returned.

## You no longer need to remove the old version first

v0.1.8 was installed over v0.1.7 without removing it -- setup reported `device
already exists; updating its driver` -- and the new binary was loaded anyway,
proven by the behaviour changing. The DriverVer stamping added in v0.1.7 works.

Installing straight over a previous version is fine from here on.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source and of the
driver's diagnostics, a parse of every XML file, and a check that both declared
framework versions match the INF.

**Nobody has completed an install yet**, but the failure has moved twice now,
each time to a later stage than the last.
