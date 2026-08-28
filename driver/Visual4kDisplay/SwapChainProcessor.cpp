#include "Driver.h"

#include <avrt.h>

namespace visual4k {

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN swapChain,
                                       LUID renderAdapter, HANDLE newFrameEvent)
    : swapChain_(swapChain),
      renderAdapter_(renderAdapter),
      newFrameEvent_(newFrameEvent),
      terminateEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr))
{
    thread_ = std::thread([this] { Run(); });
}

SwapChainProcessor::~SwapChainProcessor()
{
    if (terminateEvent_ != nullptr)
        SetEvent(terminateEvent_);

    if (thread_.joinable())
        thread_.join();

    if (terminateEvent_ != nullptr)
        CloseHandle(terminateEvent_);
}

void SwapChainProcessor::Run()
{
    // Join the Distribution MMCSS task, which is what the display stack expects
    // of a swap-chain processor. Without it this thread competes with ordinary
    // background work and the virtual display stutters under load even though
    // it does almost no work.
    DWORD taskIndex = 0;
    HANDLE avTask = AvSetMmThreadCharacteristicsW(L"Distribution", &taskIndex);

    RunCore();

    // Retiring the swap chain tells IddCx we are done with it. Skipping this
    // leaves the monitor in a state where Windows will not reassign a new
    // chain, and the display goes permanently black.
    WdfObjectDelete(reinterpret_cast<WDFOBJECT>(swapChain_));

    if (avTask != nullptr)
        AvRevertMmThreadCharacteristics(avTask);
}

void SwapChainProcessor::RunCore()
{
    // IddCx requires a D3D device on the adapter Windows chose for rendering,
    // even though this driver never touches the pixels: the device is what
    // gives the swap chain somewhere to allocate its surfaces.
    IDARG_IN_SWAPCHAINSETDEVICE setDevice = {};

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()))))
        return;

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapterByLuid(renderAdapter_,
                                          IID_PPV_ARGS(adapter.GetAddressOf()))))
        return;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_1;
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 0, &level, 1, D3D11_SDK_VERSION,
                                 device.GetAddressOf(), nullptr,
                                 context.GetAddressOf())))
        return;

    setDevice.pDevice = device.Get();
    if (!NT_SUCCESS(IddCxSwapChainSetDevice(swapChain_, &setDevice)))
        return;

    HANDLE waitOn[2] = {newFrameEvent_, terminateEvent_};

    for (;;) {
        IDARG_OUT_RELEASEANDACQUIREBUFFER buffer = {};
        NTSTATUS status = IddCxSwapChainReleaseAndAcquireBuffer(swapChain_,
                                                                &buffer);

        if (status == STATUS_PENDING) {
            // No frame ready. Block until DWM signals one or we are torn down.
            const DWORD wait = WaitForMultipleObjects(2, waitOn, FALSE, 500);
            if (wait == WAIT_OBJECT_0 + 1)
                break;                       // terminate
            continue;                        // new frame, or a timeout re-poll
        }

        if (!NT_SUCCESS(status))
            break;                           // chain is gone; the caller retries

        // The frame is deliberately not read here. visual4k-host consumes the
        // composed desktop through Desktop Duplication instead, so all this
        // thread owes IddCx is a prompt acknowledgement -- holding buffers
        // would throttle DWM's composition of the virtual desktop.
        status = IddCxSwapChainFinishedProcessingFrame(swapChain_);
        if (!NT_SUCCESS(status))
            break;

        if (WaitForSingleObject(terminateEvent_, 0) == WAIT_OBJECT_0)
            break;
    }
}

}  // namespace visual4k
