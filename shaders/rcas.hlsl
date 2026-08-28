// RCAS -- Robust Contrast Adaptive Sharpening, after AMD FidelityFX FSR 1.0.
//
// Transcribed from visual4k_ref.rcas, which is in turn a reimplementation of
// FsrRcasF in ffx_fsr1.h.  The resolve pass is a low-pass filter, so its
// output is correctly a little soft; this pass buys that sharpness back
// without the halos an unsharp mask would produce, because the filter weight
// is bounded by the local neighbourhood's own min and max.
//
// Colour space: run this on gamma-encoded values.  In linear light it
// over-sharpens shadows, which reads as crawling noise in dark UI chrome.

#include "common.hlsli"

cbuffer SharpenConstants : register(b0)
{
    uint2 gSize;
    float gSharpness;   // exp2(-stops); 1.0 is maximum, 0.5 is one stop down
    uint  gDenoise;     // 1 = enable FSR's noise-aware attenuation
};

Texture2D<float4>   gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

// Reciprocal of the maximum permitted lobe strength; from ffx_fsr1.h.
static const float kRcasLimit = 0.25f - (1.0f / 16.0f);

float3 LoadClamped(int2 p)
{
    p = clamp(p, int2(0, 0), int2(gSize) - 1);
    return gSource.Load(int3(p, 0)).rgb;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gSize.x || tid.y >= gSize.y)
        return;

    const int2 p = int2(tid.xy);

    //     b
    //   d e f
    //     h
    const float3 b = LoadClamped(p + int2( 0, -1));
    const float3 d = LoadClamped(p + int2(-1,  0));
    const float4 eFull = gSource.Load(int3(p, 0));
    const float3 e = eFull.rgb;
    const float3 f = LoadClamped(p + int2( 1,  0));
    const float3 h = LoadClamped(p + int2( 0,  1));

    const float3 mn4 = min(min(b, d), min(f, h));
    const float3 mx4 = max(max(b, d), max(f, h));

    // How far the filter may pull this pixel before it would undershoot black
    // (hitMin) or overshoot the 1.0 peak (hitMax), expressed as lobe weights.
    const float eps = 1e-12f;
    const float3 hitMin = min(mn4, e) / (4.0f * mx4 + eps);
    const float3 hitMax = (1.0f - max(mx4, e)) / (4.0f * mn4 - 4.0f + eps);

    const float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b);
    lobe = clamp(lobe, -kRcasLimit, 0.0f) * gSharpness;

    if (gDenoise != 0)
    {
        const float bL = LumaTimes2(b);
        const float dL = LumaTimes2(d);
        const float eL = LumaTimes2(e);
        const float fL = LumaTimes2(f);
        const float hL = LumaTimes2(h);

        const float ringMax = max(max(max(bL, dL), max(eL, fL)), hL);
        const float ringMin = min(min(min(bL, dL), min(eL, fL)), hL);

        // High when the centre pixel is an outlier against its ring, i.e. when
        // the local contrast is noise rather than an edge.
        float nz = 0.25f * (bL + dL + fL + hL) - eL;
        nz = saturate(abs(nz) / (ringMax - ringMin + eps));
        lobe *= 1.0f - 0.5f * nz;
    }

    const float3 rgb = saturate((lobe * (b + d + f + h) + e) / (4.0f * lobe + 1.0f));
    gOutput[tid.xy] = float4(rgb, eFull.a);
}
