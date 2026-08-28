# Architecture

```
  ┌──────────────────────────────────────┐
  │  Visual4kDisplay  (IddCx, UMDF)      │  virtual 3840×2160 monitor
  │  driver/Visual4kDisplay/             │  Windows lays out the desktop at 4K
  └──────────────────┬───────────────────┘  every application renders at 4K
                     │  frames, via the composed desktop
                     ▼
  ┌──────────────────────────────────────┐
  │  DXGI Desktop Duplication            │  windows + cursor + DWM effects
  │  host/…/Duplicator.cpp               │
  └──────────────────┬───────────────────┘
                     ▼
  ┌──────────────────────────────────────┐
  │  Renderer  (D3D11 compute)           │  horizontal resolve  3840 → 2560
  │  host/…/Renderer.cpp                 │  vertical resolve    2160 → 1440
  │  shaders/downsample.hlsl, rcas.hlsl  │  RCAS sharpen
  └──────────────────┬───────────────────┘
                     ▼
              physical 2560×1440 panel
```

## Why the driver does not touch pixels

The obvious design puts the resolve inside the driver's swap-chain processor:
the frames are right there, and it saves a copy.

It does not work, for two reasons. An IddCx swap-chain processor has no
legitimate way to present to a *different* monitor — it is the consumer for
one virtual output, not a compositor. And the frames it receives are already
composed, so a user-mode consumer reading the same desktop through Desktop
Duplication gets identical pixels.

So the driver's entire job is to exist: to make Windows believe a 4K monitor
is attached. `SwapChainProcessor` acquires each frame and immediately retires
it, purely so DWM's composition of the virtual desktop is never throttled by
an unconsumed buffer.

The payoff is that the part of the system most likely to change — the filter —
lives in a shader file that is compiled at runtime. Tuning the resolve never
requires re-signing a driver.

## Why Desktop Duplication for capture

It is not the lowest-latency capture path available; a per-application hook or
`IDXGIOutputDuplication`'s newer siblings can beat it. It is the only API that
sees the *composed* desktop, which is precisely what the user asked to see
supersampled. A faster path that misses the desktop itself solves a different
problem.

Duplication runs against the virtual output, never against the physical panel.
Duplicating the panel the compositor is drawing to would be a feedback loop.

## Failure modes the code handles explicitly

| condition | handling |
|---|---|
| `DXGI_ERROR_WAIT_TIMEOUT` | nothing changed on the virtual desktop; the presented frame is still correct, so nothing is redrawn |
| `DXGI_ERROR_ACCESS_LOST` | a mode change or a fullscreen app took the duplication; rebuild it — expected, not an error |
| `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` | the per-session duplication limit is used up, usually by another capture tool |
| swap chain reassigned without unassign | the old `SwapChainProcessor` is dropped first, so the two cannot race |
| UAV still bound on the next pass | bindings are explicitly cleared; D3D silently NULLs a conflicting binding, which appears as a black frame rather than an error |

## The correctness chain

Image quality depends on a filter, and a filter is easy to get subtly wrong in
a way nobody notices until someone compares screenshots. So the numerics are
pinned at three levels:

1. **`reference/visual4k_ref/`** is the normative CPU model, covered by 69
   tests asserting DC preservation, identity, sub-nanopixel centring, energy
   conservation at borders, RCAS's no-overshoot guarantee, and the project's
   headline quality claim.
2. **`host/…/TapTable.cpp`** is deliberately free of Windows and D3D headers
   so it builds anywhere, and `tools/compare_taptable.py` diffs its output
   against the Python reference across nine geometries. Worst disagreement:
   3.0e-8, which is float32 storage precision.
3. **`shaders/*.hlsl`** consume that table verbatim. A shader bug can
   therefore always be isolated by running the Python reference on the same
   frame and diffing.

`driver/Visual4kDisplay/Edid.{h,cpp}` is separated from the Windows headers
for the same reason: `tools/edid_selftest.cpp` validates the checksum and the
detailed timing block on any machine. Windows reports a malformed EDID only as
"the monitor arrived with no supported modes", which is a miserable thing to
debug against a live driver.

## What is not built yet

- **HDR.** The pipeline is 8-bit sRGB end to end. An HDR path needs
  scRGB or HDR10 surfaces and a different transfer function in the resolve.
- **Multi-monitor.** One virtual display, one panel.
- **VRR passthrough.** The compositor presents on the panel's own cadence; the
  virtual display's refresh is fixed at 60 Hz.
- **Latency measurement.** The pipeline adds at least one frame. It has not
  been measured, so no figure is quoted.
