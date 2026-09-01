Two fixes to the install that failed last time, and one new thing that gives
back the sharpness supersampling takes from text.

## The install should actually update now

v0.1.6 carried a fix for the IddCx version mismatch, and there is a good chance
Windows never loaded it. The setup program reported:

    device already exists; updating its driver
    driver bound to the device

and the device stayed broken in exactly its old way.

`DriverVer` was the literal string `01/01/2025,1.0.0.0` in every build ever
made. Windows ranks driver packages, and one that is not newer than what the
driver store already holds does not have to win -- so installing over a
previous version could leave the earlier binary bound to the device. An update
that updates nothing, reported as success.

Every build now stamps its own version.

**If you have a previous version installed, remove it first**: run the setup
program you already have, choose **3**, answer **n** when it offers to turn
test signing off. Then install this one with **1**.

## Setup diagnoses before it asks

When no display appeared, v0.1.6 offered the list of attached displays and
asked which was the virtual one -- putting your own two monitors in front of
you and asking which was the one that was not there. Windows had recorded the
actual reason the whole time.

It now reads the device's problem code first and says what it means, and offers
the list only when the driver is genuinely running, which is the one case a
person can settle and the program cannot.

## Subpixel resolve: --subpixel

An LCD pixel is three emitters side by side, red a third of a pixel left of
centre and blue a third right. ClearType makes small text legible by computing
how much of a glyph covers each *emitter* rather than each pixel, which is
three times the horizontal resolution for free.

Every ordinary resolve averages that away: one value per output pixel, sent to
all three emitters. That is the specific reason a supersampled desktop softens
text that native rendering keeps crisp, and NVIDIA's DSR does not address it
either.

So sample each channel where its emitter actually is. Measured on vertical
stems at glyph widths and sub-pixel phases, with coverage computed analytically
so the ground truth carries no sampling error at all:

                                    subpixel PSNR    luma PSNR
    native 1440p, greyscale AA           17.69 dB          inf
    4K -> plain resolve                  17.46 dB     30.22 dB
    4K -> subpixel resolve               20.53 dB     26.68 dB

+3.07 dB of effective horizontal resolution, which is seven times what the
entire choice of filter was worth. The cost in luminance accuracy is real and
is the known trade of subpixel rendering.

Five fringe-reduction filters were tried and every one scored worse. That
result is in the benchmark with a warning attached: the ground truth there is
ideal per-emitter coverage, so an unfiltered resolve is closest to it by
construction, and whether a coloured edge is objectionable to *look at* is not
something the measurement can see.

**Off by default**, because it buys resolution with colour. A panel whose
subpixels are not in RGB vertical stripes gets fringing instead of detail.

## What was measured about NVIDIA DSR along the way

Worth stating, since it decides whether any of this is worth installing:

- The DSR smoothness default of 33% renders about 23% softer than ground truth.
  Lower it. Lower is sharper, in both DSR and DLDSR.
- **The oversampling ratio matters roughly ten times more than the filter.**
  1.50x to 2.00x is worth +4.3 dB; every filter question put together is worth
  about 0.4 dB. On a 1440p panel that means DSR 4.00x (5120x2880), which DLDSR
  cannot reach -- it offers only 1.33x and 1.5x.
- At 1.50x, a well-tuned Gaussian slightly beats this project's own filter.
  There is no headroom left in filter design at that ratio.

Subpixel resolve is the one thing here that a display driver's resolve does not
already do.

## Still true

**Nobody has completed an install yet.** The device installs and binds; whether
it starts is what this release makes testable for the first time, since until
now the fix for it may never have been loaded.

## Verified

78 reference tests, 4 self-tests, a type-check of every host source, a parse of
every XML file, a check that the compiled IddCx version matches the INF, and a
check that the setup program's elevation manifest really embedded.
