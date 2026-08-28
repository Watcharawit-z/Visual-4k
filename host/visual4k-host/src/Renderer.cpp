#include "Renderer.h"

#include <d3dcompiler.h>

#include <cmath>
#include <string>
#include <vector>

namespace visual4k {
namespace {

constexpr uint32_t kThreadGroupSize = 8;   // must match [numthreads] in the HLSL

uint32_t DispatchCount(uint32_t extent)
{
    return (extent + kThreadGroupSize - 1) / kThreadGroupSize;
}

}  // namespace

HRESULT Renderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                             const std::wstring& shaderDir)
{
    if (device == nullptr || context == nullptr)
        return E_INVALIDARG;

    device_ = device;
    context_ = context;
    shaderDir_ = shaderDir;

    HRESULT hr = CompileShader(shaderDir_ + L"\\downsample.hlsl", "CSMain",
                               resolveCs_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    hr = CompileShader(shaderDir_ + L"\\rcas.hlsl", "CSMain",
                       sharpenCs_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC cb = {};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cb.ByteWidth = sizeof(ResolveConstants);
    hr = device_->CreateBuffer(&cb, nullptr, resolveCb_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    cb.ByteWidth = sizeof(SharpenConstants);
    hr = device_->CreateBuffer(&cb, nullptr, sharpenCb_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    return S_OK;
}

HRESULT Renderer::CompileShader(const std::wstring& path, const char* entry,
                                ID3D11ComputeShader** out)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;

    // D3D_COMPILE_STANDARD_FILE_INCLUDE resolves common.hlsli relative to the
    // shader being compiled, which is why shaderDir_ is passed as a full path.
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr,
                                    D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, "cs_5_0", flags, 0,
                                    code.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors)
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        return hr;
    }

    return device_->CreateComputeShader(code->GetBufferPointer(),
                                        code->GetBufferSize(), nullptr, out);
}

void Renderer::SetSettings(const RendererSettings& settings)
{
    const bool kernelChanged = settings.kernel != settings_.kernel ||
                               settings.gaussianSigma != settings_.gaussianSigma;
    settings_ = settings;
    if (kernelChanged)
        tablesDirty_ = true;
}

void Renderer::RebuildTapTables()
{
    horizontalTaps_ = BuildTapTable(srcWidth_, dstWidth_, settings_.kernel,
                                    settings_.gaussianSigma);
    verticalTaps_ = BuildTapTable(srcHeight_, dstHeight_, settings_.kernel,
                                  settings_.gaussianSigma);
}

HRESULT Renderer::UploadTapTable(const TapTable& table,
                                 ComPtr<ID3D11ShaderResourceView>& firstTapSrv,
                                 ComPtr<ID3D11ShaderResourceView>& weightsSrv)
{
    // Immutable: the table only changes when the geometry or kernel does, and
    // both paths recreate these buffers rather than mapping them.
    auto makeStructured = [&](const void* data, UINT stride, UINT count,
                              ComPtr<ID3D11ShaderResourceView>& srv) -> HRESULT {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = stride * count;
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = stride;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = data;

        ComPtr<ID3D11Buffer> buffer;
        HRESULT hr = device_->CreateBuffer(&bd, &init, buffer.GetAddressOf());
        if (FAILED(hr)) return hr;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;          // required for structured buffers
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements = count;

        return device_->CreateShaderResourceView(buffer.Get(), &sd,
                                                 srv.ReleaseAndGetAddressOf());
    };

    HRESULT hr = makeStructured(table.firstTap.data(), sizeof(int32_t),
                                static_cast<UINT>(table.firstTap.size()),
                                firstTapSrv);
    if (FAILED(hr)) return hr;

    return makeStructured(table.weights.data(), sizeof(float),
                          static_cast<UINT>(table.weights.size()), weightsSrv);
}

HRESULT Renderer::CreateIntermediates()
{
    auto makeTexture = [&](uint32_t w, uint32_t h, DXGI_FORMAT format,
                           ComPtr<ID3D11Texture2D>& tex,
                           ComPtr<ID3D11ShaderResourceView>& srv,
                           ComPtr<ID3D11UnorderedAccessView>& uav) -> HRESULT {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        HRESULT hr = device_->CreateTexture2D(&td, nullptr,
                                              tex.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;

        hr = device_->CreateShaderResourceView(tex.Get(), nullptr,
                                               srv.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;

        return device_->CreateUnorderedAccessView(tex.Get(), nullptr,
                                                  uav.ReleaseAndGetAddressOf());
    };

    // Half-float between the two resolve passes: the horizontal pass has
    // already spent its precision budget on a weighted sum, and re-quantising
    // to 8 bits here shows up as banding in dark gradients.
    HRESULT hr = makeTexture(dstWidth_, srcHeight_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                             intermediateH_, intermediateHSrv_, intermediateHUav_);
    if (FAILED(hr)) return hr;

    return makeTexture(dstWidth_, dstHeight_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                       resolved_, resolvedSrv_, resolvedUav_);
}

HRESULT Renderer::Resize(uint32_t srcWidth, uint32_t srcHeight,
                         uint32_t dstWidth, uint32_t dstHeight)
{
    if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0)
        return E_INVALIDARG;

    const bool geometrySame = srcWidth == srcWidth_ && srcHeight == srcHeight_ &&
                              dstWidth == dstWidth_ && dstHeight == dstHeight_;
    if (geometrySame && !tablesDirty_)
        return S_OK;

    const bool needIntermediates = !geometrySame;

    srcWidth_ = srcWidth;
    srcHeight_ = srcHeight;
    dstWidth_ = dstWidth;
    dstHeight_ = dstHeight;

    RebuildTapTables();

    HRESULT hr = UploadTapTable(horizontalTaps_, hFirstTapSrv_, hWeightsSrv_);
    if (FAILED(hr)) return hr;

    hr = UploadTapTable(verticalTaps_, vFirstTapSrv_, vWeightsSrv_);
    if (FAILED(hr)) return hr;

    if (needIntermediates) {
        hr = CreateIntermediates();
        if (FAILED(hr)) return hr;
    }

    tablesDirty_ = false;
    return S_OK;
}

void Renderer::UnbindComputeStage()
{
    // A UAV left bound cannot be read as an SRV on the next pass, and D3D
    // silently NULLs the conflicting binding instead of failing -- which shows
    // up as a black frame rather than an error. Unbind explicitly.
    ID3D11ShaderResourceView* nullSrvs[3] = {nullptr, nullptr, nullptr};
    ID3D11UnorderedAccessView* nullUav[1] = {nullptr};
    context_->CSSetShaderResources(0, 3, nullSrvs);
    context_->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
}

HRESULT Renderer::RunResolvePass(uint32_t axis, ID3D11ShaderResourceView* srcSrv,
                                 ID3D11UnorderedAccessView* dstUav,
                                 uint32_t srcW, uint32_t srcH,
                                 uint32_t dstW, uint32_t dstH,
                                 ID3D11ShaderResourceView* firstTapSrv,
                                 ID3D11ShaderResourceView* weightsSrv,
                                 uint32_t tapCount)
{
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context_->Map(resolveCb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                               &mapped);
    if (FAILED(hr)) return hr;

    auto* c = static_cast<ResolveConstants*>(mapped.pData);
    c->srcSize[0] = srcW;
    c->srcSize[1] = srcH;
    c->dstSize[0] = dstW;
    c->dstSize[1] = dstH;
    c->tapCount = tapCount;
    c->axis = axis;
    c->linearize = settings_.linearResolve ? 1u : 0u;
    c->pad = 0;
    context_->Unmap(resolveCb_.Get(), 0);

    ID3D11ShaderResourceView* srvs[3] = {firstTapSrv, weightsSrv, srcSrv};
    ID3D11Buffer* cbs[1] = {resolveCb_.Get()};
    ID3D11UnorderedAccessView* uavs[1] = {dstUav};

    context_->CSSetShader(resolveCs_.Get(), nullptr, 0);
    context_->CSSetConstantBuffers(0, 1, cbs);
    context_->CSSetShaderResources(0, 3, srvs);
    context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    context_->Dispatch(DispatchCount(dstW), DispatchCount(dstH), 1);

    UnbindComputeStage();
    return S_OK;
}

HRESULT Renderer::Render(ID3D11Texture2D* source, ID3D11Texture2D* target)
{
    if (source == nullptr || target == nullptr)
        return E_INVALIDARG;
    if (!resolveCs_ || !sharpenCs_)
        return E_NOT_VALID_STATE;

    D3D11_TEXTURE2D_DESC srcDesc = {};
    source->GetDesc(&srcDesc);
    D3D11_TEXTURE2D_DESC dstDesc = {};
    target->GetDesc(&dstDesc);

    HRESULT hr = Resize(srcDesc.Width, srcDesc.Height, dstDesc.Width, dstDesc.Height);
    if (FAILED(hr)) return hr;

    ComPtr<ID3D11ShaderResourceView> sourceSrv;
    hr = device_->CreateShaderResourceView(source, nullptr,
                                           sourceSrv.GetAddressOf());
    if (FAILED(hr)) return hr;

    // Horizontal first: it is the axis with the larger reduction on every
    // 16:9 source, so it shrinks the working set before the vertical pass runs.
    hr = RunResolvePass(0, sourceSrv.Get(), intermediateHUav_.Get(),
                        srcWidth_, srcHeight_, dstWidth_, srcHeight_,
                        hFirstTapSrv_.Get(), hWeightsSrv_.Get(),
                        horizontalTaps_.tapCount);
    if (FAILED(hr)) return hr;

    const bool sharpen = settings_.sharpnessStops >= 0.0f;

    ComPtr<ID3D11UnorderedAccessView> targetUav;
    hr = device_->CreateUnorderedAccessView(target, nullptr,
                                            targetUav.GetAddressOf());
    if (FAILED(hr)) return hr;

    // With sharpening off the vertical pass can write straight to the target,
    // saving a full-resolution round trip through memory.
    hr = RunResolvePass(1, intermediateHSrv_.Get(),
                        sharpen ? resolvedUav_.Get() : targetUav.Get(),
                        dstWidth_, srcHeight_, dstWidth_, dstHeight_,
                        vFirstTapSrv_.Get(), vWeightsSrv_.Get(),
                        verticalTaps_.tapCount);
    if (FAILED(hr)) return hr;

    if (!sharpen)
        return S_OK;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context_->Map(sharpenCb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return hr;

    auto* c = static_cast<SharpenConstants*>(mapped.pData);
    c->size[0] = dstWidth_;
    c->size[1] = dstHeight_;
    // FSR's convention: the shader constant is exp2(-stops), so 0 stops is
    // maximum sharpening and each additional stop halves it.
    c->sharpness = std::exp2f(-settings_.sharpnessStops);
    c->denoise = settings_.denoise ? 1u : 0u;
    context_->Unmap(sharpenCb_.Get(), 0);

    ID3D11ShaderResourceView* srvs[1] = {resolvedSrv_.Get()};
    ID3D11Buffer* cbs[1] = {sharpenCb_.Get()};
    ID3D11UnorderedAccessView* uavs[1] = {targetUav.Get()};

    context_->CSSetShader(sharpenCs_.Get(), nullptr, 0);
    context_->CSSetConstantBuffers(0, 1, cbs);
    context_->CSSetShaderResources(0, 1, srvs);
    context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    context_->Dispatch(DispatchCount(dstWidth_), DispatchCount(dstHeight_), 1);

    UnbindComputeStage();
    return S_OK;
}

}  // namespace visual4k
