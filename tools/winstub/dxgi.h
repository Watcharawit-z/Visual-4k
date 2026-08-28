// Minimal DXGI declarations -- see tools/winstub/README.md.
#pragma once

#include <windows.h>

enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    DXGI_FORMAT_B8G8R8A8_UNORM = 87,
};

enum DXGI_MODE_SCANLINE_ORDER { DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED = 0 };
enum DXGI_MODE_ROTATION { DXGI_MODE_ROTATION_IDENTITY = 1 };
enum DXGI_ALPHA_MODE { DXGI_ALPHA_MODE_IGNORE = 3 };
enum DXGI_SWAP_EFFECT { DXGI_SWAP_EFFECT_FLIP_DISCARD = 4 };

#define DXGI_USAGE_RENDER_TARGET_OUTPUT 0x20UL
#define DXGI_USAGE_UNORDERED_ACCESS     0x100UL
#define DXGI_MWA_NO_ALT_ENTER           0x2

#define DXGI_ERROR_NOT_FOUND               ((HRESULT)0x887A0002L)
#define DXGI_ERROR_WAIT_TIMEOUT            ((HRESULT)0x887A0027L)
#define DXGI_ERROR_ACCESS_LOST             ((HRESULT)0x887A0026L)
#define DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ((HRESULT)0x887A0022L)

struct DXGI_SAMPLE_DESC { UINT Count; UINT Quality; };
struct DXGI_RATIONAL { UINT Numerator; UINT Denominator; };

struct DXGI_OUTPUT_DESC {
    WCHAR DeviceName[32];
    RECT DesktopCoordinates;
    BOOL AttachedToDesktop;
    DXGI_MODE_ROTATION Rotation;
    HANDLE Monitor;
};

struct DXGI_SWAP_CHAIN_DESC1 {
    UINT Width; UINT Height; DXGI_FORMAT Format; BOOL Stereo;
    DXGI_SAMPLE_DESC SampleDesc; DWORD BufferUsage; UINT BufferCount;
    UINT Scaling; DXGI_SWAP_EFFECT SwapEffect; DXGI_ALPHA_MODE AlphaMode;
    UINT Flags;
};

struct DXGI_OUTDUPL_FRAME_INFO {
    LONGLONG LastPresentTime;
    LONGLONG LastMouseUpdateTime;
    UINT AccumulatedFrames;
    BOOL RectsCoalesced;
    BOOL ProtectedContentMaskedOut;
    UINT TotalMetadataBufferSize;
    UINT PointerShapeBufferSize;
};

struct DXGI_ADAPTER_DESC1 { WCHAR Description[128]; UINT VendorId; LUID AdapterLuid; };

struct IDXGIObject : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) = 0;
};

struct IDXGIResource;
struct IDXGIOutput;

struct IDXGIDeviceSubObject : public IDXGIObject {};

struct IDXGIResource : public IDXGIDeviceSubObject {};

struct IDXGIOutputDuplication : public IDXGIObject {
    virtual HRESULT STDMETHODCALLTYPE AcquireNextFrame(
        UINT, DXGI_OUTDUPL_FRAME_INFO*, IDXGIResource**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReleaseFrame() = 0;
};

struct IDXGIOutput : public IDXGIObject {
    virtual HRESULT STDMETHODCALLTYPE GetDesc(DXGI_OUTPUT_DESC*) = 0;
};

struct IDXGIOutput1 : public IDXGIOutput {
    virtual HRESULT STDMETHODCALLTYPE DuplicateOutput(
        IUnknown*, IDXGIOutputDuplication**) = 0;
};

struct IDXGIAdapter : public IDXGIObject {
    virtual HRESULT STDMETHODCALLTYPE EnumOutputs(UINT, IDXGIOutput**) = 0;
};

struct IDXGIAdapter1 : public IDXGIAdapter {
    virtual HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1*) = 0;
};

struct IDXGIDevice : public IDXGIObject {
    virtual HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter**) = 0;
};

struct IDXGISwapChain1 : public IDXGIDeviceSubObject {
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT, REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Present(UINT, UINT) = 0;
};

struct IDXGIFactory1 : public IDXGIObject {
    virtual HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT, IDXGIAdapter1**) = 0;
};

struct IDXGIFactory2 : public IDXGIFactory1 {
    virtual HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(
        IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const void*,
        IDXGIOutput*, IDXGISwapChain1**) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND, UINT) = 0;
};

extern "C" {
HRESULT CreateDXGIFactory1(REFIID, void**);
HRESULT CreateDXGIFactory2(UINT, REFIID, void**);
}
