#include "Duplicator.h"

#include <algorithm>
#include <utility>

namespace visual4k {

HRESULT Duplicator::EnumerateOutputs(std::vector<OutputInfo>* outputs)
{
    if (outputs == nullptr) return E_INVALIDARG;
    outputs->clear();

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    for (UINT a = 0; ; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) ==
            DXGI_ERROR_NOT_FOUND)
            break;

        for (UINT o = 0; ; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) ==
                DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_OUTPUT_DESC desc = {};
            if (FAILED(output->GetDesc(&desc)))
                continue;

            OutputInfo info;
            info.deviceName = desc.DeviceName;
            info.width = static_cast<uint32_t>(desc.DesktopCoordinates.right -
                                               desc.DesktopCoordinates.left);
            info.height = static_cast<uint32_t>(desc.DesktopCoordinates.bottom -
                                                desc.DesktopCoordinates.top);
            info.attachedToDesktop = desc.AttachedToDesktop != FALSE;
            outputs->push_back(info);
        }
    }

    return outputs->empty() ? DXGI_ERROR_NOT_FOUND : S_OK;
}

HRESULT Duplicator::Initialize(ID3D11Device* device, const std::wstring& deviceName)
{
    if (device == nullptr) return E_INVALIDARG;

    device_ = device;
    duplication_.Reset();
    frameHeld_ = false;

    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return hr;

    // Duplication must be created on the adapter that owns the output. The
    // virtual display is normally on the same adapter as the panel, but on a
    // laptop with switchable graphics it may not be, so this is checked rather
    // than assumed.
    for (UINT o = 0; ; ++o) {
        ComPtr<IDXGIOutput> output;
        if (adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) ==
            DXGI_ERROR_NOT_FOUND)
            break;

        DXGI_OUTPUT_DESC desc = {};
        if (FAILED(output->GetDesc(&desc)) || desc.AttachedToDesktop == FALSE)
            continue;

        if (!deviceName.empty() && deviceName != desc.DeviceName)
            continue;

        ComPtr<IDXGIOutput1> output1;
        if (FAILED(output.As(&output1)))
            continue;

        hr = output1->DuplicateOutput(device_.Get(),
                                      duplication_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE means the per-session limit on
            // duplications is already used up -- usually another capture tool.
            if (!deviceName.empty()) return hr;
            continue;
        }

        deviceName_ = desc.DeviceName;
        width_ = static_cast<uint32_t>(desc.DesktopCoordinates.right -
                                       desc.DesktopCoordinates.left);
        height_ = static_cast<uint32_t>(desc.DesktopCoordinates.bottom -
                                        desc.DesktopCoordinates.top);
        return S_OK;
    }

    return DXGI_ERROR_NOT_FOUND;
}

HRESULT Duplicator::UpdatePointer(const DXGI_OUTDUPL_FRAME_INFO& info)
{
    // Visibility and position arrive on every frame that touched the pointer.
    // LastMouseUpdateTime of 0 means it did not move, so the previous position
    // still stands.
    if (info.LastMouseUpdateTime.QuadPart != 0) {
        pointer_.visible = info.PointerPosition.Visible != FALSE;
        pointer_.x = info.PointerPosition.Position.x;
        pointer_.y = info.PointerPosition.Position.y;
    }

    // The shape only comes down when it changes, which is why it is cached.
    if (info.PointerShapeBufferSize == 0)
        return S_OK;

    if (shapeBuffer_.size() < info.PointerShapeBufferSize)
        shapeBuffer_.resize(info.PointerShapeBufferSize);

    DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo = {};
    UINT required = 0;
    HRESULT hr = duplication_->GetFramePointerShape(
        static_cast<UINT>(shapeBuffer_.size()), shapeBuffer_.data(), &required,
        &shapeInfo);
    if (FAILED(hr))
        return hr;

    auto decoded = DecodePointerShape(
        static_cast<PointerShapeType>(shapeInfo.Type), shapeBuffer_.data(),
        required, shapeInfo.Width, shapeInfo.Height, shapeInfo.Pitch);

    if (decoded.Empty()) {
        // An unknown or malformed shape should leave the previous cursor in
        // place rather than blanking it -- a stale cursor beats none.
        return S_OK;
    }

    pointer_.shape = std::move(decoded);
    pointer_.shapeGeneration++;
    return S_OK;
}

HRESULT Duplicator::AcquireFrame(uint32_t timeoutMs, ID3D11Texture2D** frame,
                                 DXGI_OUTDUPL_FRAME_INFO* info)
{
    if (!duplication_ || frame == nullptr || info == nullptr)
        return E_INVALIDARG;

    // Holding two frames at once is an API violation; release defensively so a
    // caller that skipped ReleaseFrame() on an error path cannot deadlock us.
    if (frameHeld_)
        ReleaseFrame();

    ComPtr<IDXGIResource> resource;
    HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, info,
                                                resource.GetAddressOf());
    if (FAILED(hr))
        return hr;

    frameHeld_ = true;

    // A pointer update can arrive on a frame with no image change at all, so
    // this runs before the image is unpacked and its failure is not fatal.
    UpdatePointer(*info);

    hr = resource.As(&acquired_);
    if (FAILED(hr)) {
        ReleaseFrame();
        return hr;
    }

    *frame = acquired_.Get();
    return S_OK;
}

void Duplicator::ReleaseFrame()
{
    if (!frameHeld_)
        return;

    acquired_.Reset();
    if (duplication_)
        duplication_->ReleaseFrame();
    frameHeld_ = false;
}

}  // namespace visual4k
