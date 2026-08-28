// Pulls frames off the virtual 4K display with DXGI Desktop Duplication.
//
// Desktop Duplication is the right capture path here even though it is not
// the lowest-latency one available: it is the only API that sees the composed
// desktop -- windows, cursor, and DWM effects -- which is exactly what the
// user is asking to see supersampled. A per-application hook would be faster
// but would miss the desktop itself, which is the main use case.
//
// The duplication runs against the *virtual* adapter output created by the
// Visual4kDisplay driver, never against the physical panel; duplicating the
// panel the compositor is drawing to would be a feedback loop.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "CursorDecoder.h"

namespace visual4k {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

struct OutputInfo {
    std::wstring deviceName;      // e.g. "\\\\.\\DISPLAY3"
    uint32_t width = 0;
    uint32_t height = 0;
    bool attachedToDesktop = false;
};

// Where the pointer is and what it looks like.
//
// Desktop Duplication reports the shape only when it *changes*, so the decoded
// shape has to be held across frames; a caller that reads it fresh each frame
// sees a cursor that vanishes as soon as it stops changing.
struct PointerState {
    // Top-left of the shape in source pixels. DXGI already applies the hot
    // spot, so subtracting it again shifts the cursor by its own offset --
    // a small, plausible-looking, very confusing bug.
    int32_t x = 0;
    int32_t y = 0;
    bool visible = false;
    DecodedCursor shape;
    // Bumped whenever `shape` changes, so the renderer can skip re-uploading
    // an unchanged bitmap on every frame.
    uint64_t shapeGeneration = 0;
};

class Duplicator {
public:
    // Binds to the output whose device name matches, or -- when deviceName is
    // empty -- to the first output whose size differs from the physical panel,
    // which is how the virtual display is auto-detected on a normal setup.
    HRESULT Initialize(ID3D11Device* device, const std::wstring& deviceName);

    // Blocks for up to timeoutMs for a new frame.
    //
    // Returns:
    //   S_OK              a new frame is in `frame`; call ReleaseFrame() after
    //   DXGI_ERROR_WAIT_TIMEOUT   nothing changed on screen; reuse the last frame
    //   DXGI_ERROR_ACCESS_LOST    mode change or a fullscreen app took over;
    //                             the caller must call Initialize() again
    HRESULT AcquireFrame(uint32_t timeoutMs, ID3D11Texture2D** frame,
                         DXGI_OUTDUPL_FRAME_INFO* info);
    void ReleaseFrame();

    const PointerState& Pointer() const { return pointer_; }

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    const std::wstring& DeviceName() const { return deviceName_; }

    // Enumerates every output on every adapter; used by --list-displays and by
    // the auto-detection above.
    static HRESULT EnumerateOutputs(std::vector<OutputInfo>* outputs);

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> acquired_;
    std::wstring deviceName_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool frameHeld_ = false;

    HRESULT UpdatePointer(const DXGI_OUTDUPL_FRAME_INFO& info);

    PointerState pointer_;
    std::vector<uint8_t> shapeBuffer_;
};

}  // namespace visual4k
