// GPU side of the Visual-4k pipeline.
//
//   oversampled frame (from the virtual 4K display)
//     -> horizontal resolve  (SrcW x SrcH) -> (DstW x SrcH)
//     -> vertical resolve    (DstW x SrcH) -> (DstW x DstH)
//     -> RCAS sharpen
//     -> present on the physical panel
//
// The two resolve passes share one shader and differ only by their constant
// buffer, so the filter the GPU applies is exactly the one BuildTapTable
// produced -- and that table is diff-tested against the Python reference by
// reference/tests/test_cpp_parity.py.

#pragma once

#include <cstdint>
#include <string>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "CursorDecoder.h"
#include "TapTable.h"

namespace visual4k {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

struct RendererSettings {
    Kernel kernel = Kernel::Lanczos2;
    double gaussianSigma = 0.5;

    // RCAS strength in stops: 0 is maximum sharpening, each +1 halves it.
    // Negative disables the pass entirely.
    float sharpnessStops = 0.25f;
    bool denoise = false;

    // Averaging is only physically correct in linear light, but most desktop
    // content is authored expecting a gamma-space resolve, and linearising it
    // makes antialiased text look thinner than the same text on a real 4K
    // panel. Off for the desktop, on for video. See docs/ALGORITHMS.md.
    bool linearResolve = false;

    // Resolve each colour channel at the position of its own emitter.
    //
    // An LCD pixel is three emitters side by side, and ClearType makes small
    // text legible by computing how much of a glyph covers each one. An
    // ordinary resolve produces a single value per pixel and lights all three
    // with it, which averages that structure away -- the reason a supersampled
    // desktop softens text that native rendering keeps crisp.
    //
    // Measured on vertical stems at glyph widths this recovers about 3 dB of
    // horizontal detail, at some cost in luminance accuracy; see
    // reference/bench_subpixel.py. It buys resolution with colour, so on a
    // panel whose subpixels are not in RGB vertical stripes it produces
    // fringing instead of detail. Off by default for that reason.
    bool subpixelResolve = false;

    // Fit the source inside the panel instead of stretching it to fill.
    //
    // The resolve maps the source onto the destination axis by axis, so a
    // source whose aspect ratio differs from the panel's comes out squeezed:
    // 3440x1440 onto a 2560x1440 panel compresses horizontally by 1.34x and
    // not at all vertically, and everything looks thin. Fitting letterboxes
    // instead. Off means the old behaviour, which is right only when the two
    // aspect ratios already match -- as they do for a 4K virtual display on a
    // 1440p panel.
    bool preserveAspect = true;
};

// Owns every GPU resource whose size depends on the source or panel geometry.
// Resize() is cheap to call on every frame; it early-outs unless the geometry
// actually changed, which happens when the user switches virtual resolution.
class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // shaderDir is where downsample.hlsl / rcas.hlsl / common.hlsli live.
    // Shaders are compiled at startup so the filter can be edited without a
    // rebuild; failures surface here rather than as a black screen.
    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                       const std::wstring& shaderDir);

    // Rebuilds the tap tables and intermediates. Safe to call every frame.
    HRESULT Resize(uint32_t srcWidth, uint32_t srcHeight,
                   uint32_t dstWidth, uint32_t dstHeight);

    void SetSettings(const RendererSettings& settings);
    const RendererSettings& Settings() const { return settings_; }

    // Runs the whole pipeline. `source` must be a shader-readable texture at
    // the source geometry passed to Resize(); `target` must be a UAV-capable
    // texture at the destination geometry.
    HRESULT Render(ID3D11Texture2D* source, ID3D11Texture2D* target);

    // Composites the pointer onto `target`, scaling it by the resolve ratio so
    // it ends up the size it would be on a real 4K panel.
    //
    // `sourceX`/`sourceY` are the shape's top-left in source pixels.
    // `generation` lets the upload be skipped while the shape is unchanged,
    // which is almost every frame.
    HRESULT DrawCursor(ID3D11Texture2D* target, const DecodedCursor& shape,
                       uint64_t generation, int32_t sourceX, int32_t sourceY);

    // Set when the last Resize() rebuilt the tables; useful for logging.
    uint32_t HorizontalTapCount() const { return horizontalTaps_.tapCount; }
    uint32_t VerticalTapCount() const { return verticalTaps_.tapCount; }

private:
    struct ResolveConstants {
        uint32_t srcSize[2];
        uint32_t dstSize[2];
        uint32_t tapCount;
        uint32_t axis;
        uint32_t linearize;
        uint32_t subpixel;
        int32_t outputOffset[2];
        uint32_t pad2[2];
    };
    static_assert(sizeof(ResolveConstants) % 16 == 0,
                  "constant buffers must be 16-byte aligned");

    struct CursorConstants {
        int32_t destOrigin[2];
        uint32_t destSize[2];
        int32_t clipOrigin[2];
        uint32_t clipSize[2];
        float invDestSize[2];
        uint32_t pad[2];
    };
    static_assert(sizeof(CursorConstants) % 16 == 0,
                  "constant buffers must be 16-byte aligned");

    struct SharpenConstants {
        uint32_t size[2];
        float sharpness;
        uint32_t denoise;
        int32_t outputOffset[2];
        uint32_t pad[2];
    };
    static_assert(sizeof(SharpenConstants) % 16 == 0,
                  "constant buffers must be 16-byte aligned");

    HRESULT UploadCursorShape(const DecodedCursor& shape);
    HRESULT CompileShader(const std::wstring& path, const char* entry,
                          ID3D11ComputeShader** out);
    HRESULT UploadTapTable(const TapTable& table,
                           ComPtr<ID3D11ShaderResourceView>& firstTapSrv,
                           ComPtr<ID3D11ShaderResourceView>& weightsSrv);
    HRESULT CreateIntermediates();
    void RebuildTapTables();
    HRESULT RunResolvePass(uint32_t axis, ID3D11ShaderResourceView* srcSrv,
                           ID3D11UnorderedAccessView* dstUav,
                           uint32_t srcW, uint32_t srcH,
                           uint32_t dstW, uint32_t dstH,
                           ID3D11ShaderResourceView* firstTapSrv,
                           ID3D11ShaderResourceView* weightsSrv,
                           uint32_t tapCount,
                           int32_t offsetX = 0, int32_t offsetY = 0);
    void UnbindComputeStage();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    std::wstring shaderDir_;

    ComPtr<ID3D11ComputeShader> resolveCs_;
    ComPtr<ID3D11ComputeShader> sharpenCs_;
    ComPtr<ID3D11ComputeShader> cursorCs_;

    ComPtr<ID3D11Buffer> resolveCb_;
    ComPtr<ID3D11Buffer> sharpenCb_;
    ComPtr<ID3D11Buffer> cursorCb_;

    ComPtr<ID3D11SamplerState> linearSampler_;
    ComPtr<ID3D11ShaderResourceView> cursorSrv_;
    ComPtr<ID3D11ShaderResourceView> cursorInvertSrv_;
    uint64_t cursorGeneration_ = 0;
    uint32_t cursorWidth_ = 0;
    uint32_t cursorHeight_ = 0;

    // (DstW x SrcH) after the horizontal pass, then (DstW x DstH) after the
    // vertical one. Kept at 16-bit float: 8-bit here would quantise twice and
    // show as banding in smooth gradients, and 32-bit doubles the bandwidth of
    // the busiest pass for no visible gain.
    ComPtr<ID3D11Texture2D> intermediateH_;
    ComPtr<ID3D11ShaderResourceView> intermediateHSrv_;
    ComPtr<ID3D11UnorderedAccessView> intermediateHUav_;

    ComPtr<ID3D11Texture2D> resolved_;
    ComPtr<ID3D11ShaderResourceView> resolvedSrv_;
    ComPtr<ID3D11UnorderedAccessView> resolvedUav_;

    // Desktop Duplication and the swap chain both hand back the same few
    // textures frame after frame, so the views are cached on the texture
    // pointer rather than recreated in the hot path.
    ComPtr<ID3D11ShaderResourceView> sourceSrv_;
    ID3D11Texture2D* cachedSource_ = nullptr;
    ComPtr<ID3D11UnorderedAccessView> targetUav_;
    ID3D11Texture2D* cachedTarget_ = nullptr;

    ComPtr<ID3D11ShaderResourceView> hFirstTapSrv_;
    ComPtr<ID3D11ShaderResourceView> hWeightsSrv_;
    // The horizontal tables again, shifted to the red and blue emitters. Built
    // only for the subpixel resolve; null otherwise.
    ComPtr<ID3D11ShaderResourceView> hFirstTapRedSrv_;
    ComPtr<ID3D11ShaderResourceView> hWeightsRedSrv_;
    ComPtr<ID3D11ShaderResourceView> hFirstTapBlueSrv_;
    ComPtr<ID3D11ShaderResourceView> hWeightsBlueSrv_;
    ComPtr<ID3D11ShaderResourceView> vFirstTapSrv_;
    ComPtr<ID3D11ShaderResourceView> vWeightsSrv_;

    TapTable horizontalTaps_;
    TapTable horizontalTapsRed_;
    TapTable horizontalTapsBlue_;
    TapTable verticalTaps_;

    RendererSettings settings_;
    uint32_t srcWidth_ = 0;
    uint32_t srcHeight_ = 0;
    uint32_t dstWidth_ = 0;
    uint32_t dstHeight_ = 0;

    // The rectangle inside the panel the resolve actually writes to. Equal to
    // the panel when stretching or when the aspect ratios match; smaller, and
    // centred, when letterboxing.
    uint32_t fitWidth_ = 0;
    uint32_t fitHeight_ = 0;
    int32_t offsetX_ = 0;
    int32_t offsetY_ = 0;

    bool tablesDirty_ = true;
};

}  // namespace visual4k
