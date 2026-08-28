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
        uint32_t pad;
    };
    static_assert(sizeof(ResolveConstants) % 16 == 0,
                  "constant buffers must be 16-byte aligned");

    struct SharpenConstants {
        uint32_t size[2];
        float sharpness;
        uint32_t denoise;
    };
    static_assert(sizeof(SharpenConstants) % 16 == 0,
                  "constant buffers must be 16-byte aligned");

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
                           uint32_t tapCount);
    void UnbindComputeStage();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    std::wstring shaderDir_;

    ComPtr<ID3D11ComputeShader> resolveCs_;
    ComPtr<ID3D11ComputeShader> sharpenCs_;

    ComPtr<ID3D11Buffer> resolveCb_;
    ComPtr<ID3D11Buffer> sharpenCb_;

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

    ComPtr<ID3D11ShaderResourceView> hFirstTapSrv_;
    ComPtr<ID3D11ShaderResourceView> hWeightsSrv_;
    ComPtr<ID3D11ShaderResourceView> vFirstTapSrv_;
    ComPtr<ID3D11ShaderResourceView> vWeightsSrv_;

    TapTable horizontalTaps_;
    TapTable verticalTaps_;

    RendererSettings settings_;
    uint32_t srcWidth_ = 0;
    uint32_t srcHeight_ = 0;
    uint32_t dstWidth_ = 0;
    uint32_t dstHeight_ = 0;
    bool tablesDirty_ = true;
};

}  // namespace visual4k
