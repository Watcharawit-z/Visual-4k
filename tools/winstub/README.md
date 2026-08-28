# Windows stub headers

**These are not a Windows SDK.** They are a hand-written minimal subset of the
Windows, D3D11, DXGI and D3DCompiler declarations that `visual4k-host` uses,
just complete enough for a compiler to parse and type-check the host sources on
a non-Windows machine.

## What a clean build here does and does not prove

Proves: the host sources are syntactically valid C++17, every identifier
resolves, our own class members and function signatures are self-consistent,
argument counts and types line up, and no include is missing.

Does **not** prove: that the real API signatures match these stubs, that COM
lifetimes are handled correctly, or that the pipeline produces a correct image.
A stub encodes the author's belief about an API, so it can only ever catch
errors in *our* code, never a misremembered API.

Real validation is a build against the actual Windows SDK, plus running it.

## Usage

    tools/check-host-compiles.sh

The numerics that image quality depends on are validated properly elsewhere:
`reference/tests/test_cpp_parity.py` diffs the real tap-table code against the
Python reference, and neither of those needs Windows.
