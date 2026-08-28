// Minimal D3D11 declarations -- see tools/winstub/README.md.
#pragma once

#include <windows.h>
#include <dxgi.h>

enum D3D_FEATURE_LEVEL {
    D3D_FEATURE_LEVEL_11_0 = 0xb000,
    D3D_FEATURE_LEVEL_11_1 = 0xb100,
};

enum D3D_DRIVER_TYPE {
    D3D_DRIVER_TYPE_UNKNOWN = 0,
    D3D_DRIVER_TYPE_HARDWARE = 1,
};

enum D3D11_USAGE {
    D3D11_USAGE_DEFAULT = 0,
    D3D11_USAGE_IMMUTABLE = 1,
    D3D11_USAGE_DYNAMIC = 2,
};

enum D3D11_MAP { D3D11_MAP_WRITE_DISCARD = 4 };

enum D3D11_SRV_DIMENSION { D3D11_SRV_DIMENSION_BUFFER = 1 };

enum D3D11_FILTER { D3D11_FILTER_MIN_MAG_MIP_LINEAR = 0x15 };
enum D3D11_TEXTURE_ADDRESS_MODE { D3D11_TEXTURE_ADDRESS_CLAMP = 3 };
enum D3D11_COMPARISON_FUNC { D3D11_COMPARISON_NEVER = 1 };

struct D3D11_SAMPLER_DESC {
    D3D11_FILTER Filter;
    D3D11_TEXTURE_ADDRESS_MODE AddressU, AddressV, AddressW;
    float MipLODBias; UINT MaxAnisotropy;
    D3D11_COMPARISON_FUNC ComparisonFunc;
    float BorderColor[4]; float MinLOD; float MaxLOD;
};

#define D3D11_BIND_SHADER_RESOURCE   0x8
#define D3D11_BIND_CONSTANT_BUFFER   0x4
#define D3D11_BIND_UNORDERED_ACCESS  0x80
#define D3D11_CPU_ACCESS_WRITE       0x10000
#define D3D11_RESOURCE_MISC_BUFFER_STRUCTURED 0x40
#define D3D11_CREATE_DEVICE_DEBUG        0x2
#define D3D11_CREATE_DEVICE_BGRA_SUPPORT 0x20
#define D3D11_SDK_VERSION 7

struct D3D11_BUFFER_DESC {
    UINT ByteWidth; D3D11_USAGE Usage; UINT BindFlags;
    UINT CPUAccessFlags; UINT MiscFlags; UINT StructureByteStride;
};

struct D3D11_TEXTURE2D_DESC {
    UINT Width; UINT Height; UINT MipLevels; UINT ArraySize;
    DXGI_FORMAT Format; DXGI_SAMPLE_DESC SampleDesc; D3D11_USAGE Usage;
    UINT BindFlags; UINT CPUAccessFlags; UINT MiscFlags;
};

struct D3D11_SUBRESOURCE_DATA {
    LPCVOID pSysMem; UINT SysMemPitch; UINT SysMemSlicePitch;
};

struct D3D11_MAPPED_SUBRESOURCE {
    void* pData; UINT RowPitch; UINT DepthPitch;
};

struct D3D11_BUFFER_SRV { UINT FirstElement; UINT NumElements; };

struct D3D11_SHADER_RESOURCE_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D11_SRV_DIMENSION ViewDimension;
    D3D11_BUFFER_SRV Buffer;
};

struct ID3D11DeviceChild : public IUnknown {};
struct ID3D11Resource : public ID3D11DeviceChild {};
struct ID3D11Buffer : public ID3D11Resource {};
struct ID3D11ComputeShader : public ID3D11DeviceChild {};
struct ID3D11View : public ID3D11DeviceChild {};
struct ID3D11SamplerState : public ID3D11DeviceChild {};
struct ID3D11ShaderResourceView : public ID3D11View {};
struct ID3D11UnorderedAccessView : public ID3D11View {};

struct ID3D11Texture2D : public ID3D11Resource {
    virtual void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE2D_DESC*) = 0;
};

struct ID3D11Device : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateBuffer(
        const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTexture2D(
        const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateShaderResourceView(
        ID3D11Resource*, const D3D11_SHADER_RESOURCE_VIEW_DESC*,
        ID3D11ShaderResourceView**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
        ID3D11Resource*, const void*, ID3D11UnorderedAccessView**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateComputeShader(
        const void*, SIZE_T, void*, ID3D11ComputeShader**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSamplerState(
        const D3D11_SAMPLER_DESC*, ID3D11SamplerState**) = 0;
};

struct ID3D11DeviceContext : public ID3D11DeviceChild {
    virtual HRESULT STDMETHODCALLTYPE Map(
        ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*) = 0;
    virtual void STDMETHODCALLTYPE Unmap(ID3D11Resource*, UINT) = 0;
    virtual void STDMETHODCALLTYPE CSSetShader(
        ID3D11ComputeShader*, void* const*, UINT) = 0;
    virtual void STDMETHODCALLTYPE CSSetConstantBuffers(
        UINT, UINT, ID3D11Buffer* const*) = 0;
    virtual void STDMETHODCALLTYPE CSSetShaderResources(
        UINT, UINT, ID3D11ShaderResourceView* const*) = 0;
    virtual void STDMETHODCALLTYPE CSSetUnorderedAccessViews(
        UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) = 0;
    virtual void STDMETHODCALLTYPE CSSetSamplers(
        UINT, UINT, ID3D11SamplerState* const*) = 0;
    virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(
        ID3D11UnorderedAccessView*, const float[4]) = 0;
    virtual void STDMETHODCALLTYPE Dispatch(UINT, UINT, UINT) = 0;
};

extern "C" HRESULT D3D11CreateDevice(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
