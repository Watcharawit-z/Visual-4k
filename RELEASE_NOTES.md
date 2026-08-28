First build that produces installable binaries. Both Windows components now
compile from source on every push, and this release carries them together with
the install scripts and the setup guide.

## What is in the zip

- `visual4k-host.exe` and `shaders/` — the compositor
- `driver/` — the Visual4kDisplay virtual display driver and its INF
- `install-driver.ps1`, `uninstall-driver.ps1`
- `GETTING-STARTED.md` — the step-by-step guide, including the warnings below

## What is established

- The resampling core, its quality claims, the cursor decoder and the virtual
  monitor's EDID are covered by 77 tests that run on every push.
- The C++ and Python tap tables agree to float32 precision (worst delta 3e-8),
  checked on Windows against the binary being shipped.
- `visual4k-host` compiles on MSVC with the Windows SDK; `Visual4kDisplay`
  compiles and links against the WDK.
- Measured: resolving a 4K render to 1440p is +5.0 dB PSNR over a native 1440p
  render, with 3.7x less aliasing. At a 2.0x ratio, +9.7 dB and 12x less.
  Reproduce with `python reference/bench_supersample.py`.

## What is not

**Nothing here has been installed or run.** The driver has never loaded, the
virtual display has never appeared in Windows, and the HLSL has never been
through a shader compiler, because the compositor compiles it at runtime on a
GPU. Compiling is not running. Expect to find problems.

**Windows will not load the driver unless test signing is enabled.** That is a
boot setting, it needs a reboot, and it lowers the security of the whole
machine while it is on. On a BitLocker machine, changing boot settings can
trigger a recovery-key prompt — have your key before you start. Read
`GETTING-STARTED.md` first; it covers this and the Secure Boot interaction.

**Rendering at 4K costs 2.25x the pixels.** This trades frame rate for image
quality. Latency has not been measured, so no figure is quoted.

## Trying it without installing anything

`tools/visual4k.py` runs the same pipeline on image files with only numpy and
pillow, on any OS:

```
pip install numpy pillow
python tools/visual4k.py compare your-4k-shot.png comparison.png
```
