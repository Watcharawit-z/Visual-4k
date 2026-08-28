# Algorithms, and the measurements behind them

Every default in this project is a measurement, not a preference. This
document records what was measured, how, and what it implies — so that
changing a default means re-running a benchmark rather than arguing.

Reproduce everything here with:

```bash
python3 reference/bench_supersample.py
```

## Why an analytic scene

Comparing resampling filters on a photograph cannot answer the question we
care about, because a photograph is already band-limited by the camera that
took it: there is no ground truth above its Nyquist rate to recover or lose.

`bench_supersample.py` instead defines the scene as a function of continuous
coordinates — a zone plate whose local frequency grows without bound, a fan of
thin radial lines standing in for UI hairlines and text stems, and a
converging checkerboard standing in for texture minification. A scene that can
be sampled at *any* rate lets us build the reference by brute force: 6×6
samples per panel pixel, box-averaged. That reference is what a physically
perfect display pipeline would show, and every rendering is scored against it.

## The core result

Panel 1280×720, oversampled 1920×1080 (the same 1.50× linear ratio as
3840×2160 onto a 2560×1440 panel):

| rendering | PSNR | SSIM | aliasing error | sharpness vs truth |
|---|---:|---:|---:|---:|
| native, 1 sample/pixel | 20.32 dB | 0.9609 | 5505 | 1.66× |
| triangle | 25.71 dB | 0.9866 | 1284 | 0.87× |
| catrom | 25.26 dB | 0.9864 | 1493 | 1.05× |
| mitchell | 25.71 dB | 0.9870 | 1266 | 0.91× |
| **lanczos2** | **25.31 dB** | **0.9865** | **1470** | **1.05×** |
| lanczos3 | 25.07 dB | 0.9851 | 1590 | 1.08× |
| lanczos4 | 24.99 dB | 0.9841 | 1641 | 1.09× |

At a 2.00× ratio (5120×2880 onto 2560×1440) the gap widens: lanczos2 reaches
30.00 dB with an aliasing error of 459, against the native render's 20.32 dB
and 5505 — **+9.7 dB and 12× less aliasing.**

### Reading the sharpness column

`gradient_energy` relative to ground truth: 1.00× is correct. The native
render scores **1.66×** — it looks *sharper* than reality. That is not detail,
it is aliasing: energy folded down from frequencies the sample grid could not
represent, landing in the image as structure that was never in the scene. This
is why sharpness must always be read next to the aliasing column, and why
"turn up the sharpening until it looks crisp" is a trap.

## Why lanczos2 is the default, not lanczos3

Lanczos-3 is the reflexive choice for high-quality resampling, and it is the
wrong one here. It loses to Lanczos-2 at **every** ratio the compositor
actually runs at (1.25×, 1.5×, 2.0×), on PSNR and SSIM alike.

The reason is that a wider windowed sinc buys stopband suppression, and at
1.5× there is barely any stopband to suppress — the source is only modestly
above the destination's Nyquist rate. What the extra lobes do deliver is
ringing, visible as a dark halo hugging high-contrast text edges. Lanczos-3
only starts to pay for itself well above a 2× ratio, which costs 4× the pixels
and is offered as a mode rather than a default.

Catmull-Rom scores within noise of Lanczos-2 and is cheaper (4 taps against
7 at 1.5×). It is a defensible default on a bandwidth-limited GPU.

The magnification path is a different regime and does keep lanczos3: with the
source *below* the destination Nyquist rate, the wider kernel's better
interpolation dominates and there is no aliasing for its lobes to amplify.

## RCAS, and why not an unsharp mask

The resolve is a low-pass filter, so its output is correctly a little soft and
wants sharpening back. An unsharp mask would do it — and would also amplify
the ringing the resolve's own lobes left behind, and clip specular highlights
to white.

RCAS (from AMD FidelityFX FSR 1.0, reimplemented in `reference/visual4k_ref/rcas.py`
and `shaders/rcas.hlsl`) derives its per-pixel filter weight from the local
minimum and maximum of a 5-tap cross, so the result provably cannot leave the
range its own neighbourhood already spans. No halos, no clipping — asserted by
`test_no_halo_on_a_step_edge`.

Sharpness is expressed in *stops*: 0.0 is maximum, each +1.0 halves it
(the shader constant is `exp2(-stops)`). Recommended:

| content | stops | denoise | linear |
|---|---|---|---|
| desktop, text | 0.0 | off | off |
| games | 0.25 | off | off |
| video, film | 0.5 | on | on |

## Colour space

Averaging pixels is only physically correct in linear light, and
`--linear` does exactly that. It is nonetheless **off by default for the
desktop**, deliberately.

Desktop content — antialiased text above all — is authored by people looking
at gamma-space compositing. Resolving white-on-black text in linear light
makes the glyph stems measurably thinner than the same text rendered natively
on a real 4K panel, because the correct average of a half-covered pixel is
darker than the one the font hinter assumed. The physically correct answer
looks wrong next to the thing it is imitating.

Video has no such constraint and should use `--linear`.

RCAS always runs in gamma space, whatever the resolve did. In linear light it
over-sharpens shadows, which reads as crawling noise in dark UI chrome.

## Half-pixel alignment

Destination pixel `j` samples the source at `(j + 0.5) * src_len / dst_len`.
Getting this off by half a pixel produces an image that is slightly blurry and
slightly shifted, and it is remarkably easy to ship, because it still looks
approximately right.

`test_no_half_pixel_shift` puts a symmetric impulse straddling the exact image
centre and asserts the output centroid to 1e-9. It runs for every kernel.

## Precision

The intermediate between the horizontal and vertical resolve passes is
`R16G16B16A16_FLOAT`. Eight bits there would quantise twice — once after each
weighted sum — and show as banding in dark gradients. Thirty-two bits doubles
the bandwidth of the busiest pass in the pipeline for no visible gain.

The tap weights are `float32`, which sets the tolerance in the C++/Python
parity test. Measured worst-case disagreement across nine geometries,
including deliberately awkward non-integer ratios: **3.0e-8**.
