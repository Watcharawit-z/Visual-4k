This fixes the failure that has blocked every release since the driver first
installed, and it fixes a mistake I introduced in v0.1.6.

## What the numbers said

v0.1.10 collected the one fact that could not be guessed at:

    last step  : IddCxAdapterInitAsync
    status     : 0xC000000D
    built with : iddcx=1.2 umdf=2.33 caps=88 diag=56 init=24
                 monitorInfo=56 clientConfig=168

`clientConfig=168` was **accepted** by `IddCxDeviceInitConfig`. So the IddCx 1.2
class extension had loaded and was validating structure sizes correctly.

`caps=88` was refused. Its fields account for 64 of those bytes -- `Size`,
`MaxMonitorsSupported`, and a 56-byte `EndPointDiagnostics`. The remaining 24
belong to a later version of the structure: fields the driver never touches and
the 1.2 extension never expected to receive.

The two structures behave differently because the header treats them
differently. Callback tables are gated on the version macros; plain data
structures are not. Building for 1.2 shrinks `IDD_CX_CLIENT_CONFIG` correctly
and leaves `IDDCX_ADAPTER_CAPS` at its newest layout, so the first sails
through and the second is rejected as an invalid parameter -- with no
indication of which parameter, which is why three attempts to infer it from
outside got nowhere.

## The mistake was mine, and the timeline shows it

v0.1.6 moved IddCx down to 1.2, on the reasoning that the oldest surface is the
most widely available. The failure did not move at all: still problem code 31.

What actually fixed `CM_PROB_FAILED_ADD` was the UMDF version pin in v0.1.8.
IddCx 1.2 was never load-bearing. It sat there for four releases contributing
nothing but this.

## The fix

The driver is built for the IddCx version the kit ships, and CI writes that one
discovery into **both** the compiler macros and the INF's `UmdfExtensions`.

Having that version in two places caused the original mismatch. Pinning both by
hand caused this one. There is now a single source, and the consistency check
runs after stamping, against the files about to be shipped rather than what is
committed.

The cost is that the driver now needs a Windows with IddCx 1.9 rather than the
1.2 every IddCx-capable Windows has. That compatibility was the entire argument
for 1.2, and it bought nothing.

## What this means for the install

The failure has moved four times across this sequence, each time later than the
last:

    31, EvtDeviceAdd failed        UMDF version mismatch
    10, EvtDeviceAdd complete      something after it
    10, IddCxAdapterInitAsync      this call, invalid parameter
    ->                             the parameter, named by its size

This is the first release where there is a reasoned expectation of the device
starting rather than a hope. If it does not, the driver's own record will name
the next step precisely, as it has every time since v0.1.8.

## Installing

Straight over the previous version. Extract, double-click **Visual4k-Setup.exe**,
choose **1**.

## Also in here

`--subpixel` on the compositor: resolves each colour channel at its own
emitter's position, recovering about 3 dB of the horizontal detail in text that
an ordinary resolve averages away. RGB-stripe panels only, off by default.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source and of the
driver's diagnostics, a parse of every XML file, and a check that both declared
framework versions match the INF -- run after stamping, on the package being
built.
