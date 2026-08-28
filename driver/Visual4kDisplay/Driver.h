// Visual4kDisplay -- an IddCx indirect display driver that presents a virtual
// 4K monitor to Windows.
//
// Design note, because it is the thing people get wrong when they build this:
// this driver does NOT process pixels. Its entire job is to make Windows
// believe a 3840x2160 monitor is attached, so that DWM lays out the desktop at
// 4K and every application renders at 4K. The frames are then picked up by
// visual4k-host through Desktop Duplication, resolved on the GPU, and shown on
// the real panel.
//
// Doing the resolve inside the driver would be faster by one copy, but an IddCx
// swap-chain processor has no legitimate way to present to another monitor, and
// the frames it receives are already post-composition -- so there is nothing to
// gain and a signed kernel-adjacent component to maintain. Keeping the driver
// dumb also means shader changes never require re-signing anything.

#pragma once

#include <windows.h>
#include <wdf.h>
#include <iddcx.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <memory>
#include <thread>
#include <vector>

#include "Edid.h"

namespace visual4k {

// A mode the virtual monitor advertises. The point of the driver is the 4K
// entry; the others exist so the user can fall back without uninstalling.
struct VirtualMode {
    UINT width;
    UINT height;
    UINT verticalSyncNumerator;      // Hz numerator; denominator is always 1
};

// Keeps the IddCx swap chain draining.
//
// Windows will stall composition on the virtual display if nobody consumes its
// buffers, so this thread acquires and immediately retires every frame. The
// pixels are read separately, by visual4k-host, via Desktop Duplication.
class SwapChainProcessor {
public:
    SwapChainProcessor(IDDCX_SWAPCHAIN swapChain, LUID renderAdapter,
                       HANDLE newFrameEvent);
    ~SwapChainProcessor();

    SwapChainProcessor(const SwapChainProcessor&) = delete;
    SwapChainProcessor& operator=(const SwapChainProcessor&) = delete;

private:
    void Run();
    void RunCore();

    IDDCX_SWAPCHAIN swapChain_;
    LUID renderAdapter_;
    HANDLE newFrameEvent_;
    HANDLE terminateEvent_;
    std::thread thread_;
};

// Per-adapter state, attached to the WDFDEVICE via a WDF context.
struct DeviceContext {
    IDDCX_ADAPTER adapter;
    IDDCX_MONITOR monitor;
    std::unique_ptr<SwapChainProcessor> processor;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceContext, GetDeviceContext);

}  // namespace visual4k

extern "C" DRIVER_INITIALIZE DriverEntry;
