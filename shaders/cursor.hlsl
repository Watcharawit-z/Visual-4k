// Composites the mouse pointer onto the resolved frame.
//
// Desktop Duplication reports the pointer separately from the desktop image,
// so without this pass the output has no cursor at all.
//
// It runs after the resolve rather than before it, on a cursor already scaled
// by the resolve ratio. Compositing into the 4K frame first would supersample
// the cursor along with everything else, which is marginally more correct, but
// it costs a full 4K copy every frame because the duplicated surface cannot be
// written to. A 32x32 cursor scaled to 21x21 is not where the image quality
// budget belongs.
//
// The invert mask exists because two of Windows' three cursor formats can XOR
// themselves with the screen -- that is how the text I-beam stays visible over
// any background. One RGBA image cannot express it, so the decoder emits a
// second mask and this pass applies it.

#include "common.hlsli"

cbuffer CursorConstants : register(b0)
{
    int2  gDestOrigin;    // top-left of the cursor, in target pixels
    uint2 gDestSize;      // cursor footprint on the target, after scaling
    uint2 gTargetSize;
    float2 gInvDestSize;
};

Texture2D<float4> gCursor : register(t0);
Texture2D<float>  gInvert : register(t1);
SamplerState      gLinearSampler : register(s0);

RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gDestSize.x || tid.y >= gDestSize.y)
        return;

    const int2 p = gDestOrigin + int2(tid.xy);

    // The pointer is routinely half off-screen at the edges of the desktop.
    if (p.x < 0 || p.y < 0 ||
        p.x >= int(gTargetSize.x) || p.y >= int(gTargetSize.y))
        return;

    const float2 uv = (float2(tid.xy) + 0.5f) * gInvDestSize;

    const float4 cursor = gCursor.SampleLevel(gLinearSampler, uv, 0);
    const float  invert = gInvert.SampleLevel(gLinearSampler, uv, 0);

    const float4 dst = gOutput[uint2(p)];

    float3 rgb = lerp(dst.rgb, cursor.rgb, cursor.a);
    rgb = lerp(rgb, 1.0f - dst.rgb, invert);

    gOutput[uint2(p)] = float4(saturate(rgb), dst.a);
}
