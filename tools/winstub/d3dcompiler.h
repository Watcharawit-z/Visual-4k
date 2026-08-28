// Minimal D3DCompiler declarations -- see tools/winstub/README.md.
#pragma once

#include <windows.h>

#define D3DCOMPILE_DEBUG              (1 << 0)
#define D3DCOMPILE_SKIP_OPTIMIZATION  (1 << 2)
#define D3DCOMPILE_ENABLE_STRICTNESS  (1 << 11)
#define D3DCOMPILE_OPTIMIZATION_LEVEL3 (1 << 15)

struct ID3DBlob : public IUnknown {
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};

struct ID3DInclude;
#define D3D_COMPILE_STANDARD_FILE_INCLUDE ((ID3DInclude*)(UINT_PTR)1)

struct D3D_SHADER_MACRO { LPCSTR Name; LPCSTR Definition; };

extern "C" HRESULT D3DCompileFromFile(
    LPCWSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
    UINT, UINT, ID3DBlob**, ID3DBlob**);
