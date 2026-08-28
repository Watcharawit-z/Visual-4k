// Separable polyphase resolve: oversampled frame -> panel resolution.
//
// This is the pass that turns a genuinely 4K-rasterised frame into 1440p
// pixels that carry sub-pixel information.  It is a straight transcription of
// visual4k_ref.resample; the weight table is built on the CPU by the exact
// same code path (host/visual4k-host/src/TapTable.cpp) so that a shader bug
// can always be isolated by re-running the Python reference on the same frame.
//
// Run it as two dispatches:
//   pass 0 (horizontal): (SrcW x SrcH) -> (DstW x SrcH)
//   pass 1 (vertical)  : (DstW x SrcH) -> (DstW x DstH)
// Doing the axis with the larger reduction first shrinks the working set
// before the second pass runs, which is worth 10-15% of the pass on a 4K frame.

#include "common.hlsli"

cbuffer ResolveConstants : register(b0)
{
    uint2 gSrcSize;      // dimensions of the input to *this* pass
    uint2 gDstSize;      // dimensions of the output of *this* pass
    uint  gTapCount;     // taps per destination pixel, uniform across the axis
    uint  gAxis;         // 0 = horizontal, 1 = vertical
    uint  gLinearize;    // 1 = decode sRGB before averaging, encode after
    uint  gPad;
    // Where in the output this pass's (0,0) lands. Non-zero only on the final
    // pass when the source is being letterboxed into a differently-shaped
    // panel; the intermediate always starts at the origin.
    int2  gOutputOffset;
    uint2 gPad2;
};

// gFirstTap[d] is the source index of tap 0 for destination pixel d.
// gWeights[d * gTapCount + t] is that tap's weight; each row sums to 1.
StructuredBuffer<int>   gFirstTap : register(t0);
StructuredBuffer<float> gWeights  : register(t1);

Texture2D<float4>   gSource : register(t2);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gDstSize.x || tid.y >= gDstSize.y)
        return;

    // The moving coordinate is the one we are filtering along; the other is
    // just carried through.
    const uint dstIndex = (gAxis == 0) ? tid.x : tid.y;
    const int  first    = gFirstTap[dstIndex];
    const uint wBase    = dstIndex * gTapCount;
    const int  maxSrc   = int(((gAxis == 0) ? gSrcSize.x : gSrcSize.y)) - 1;

    float3 acc = 0.0f;
    float  alpha = 0.0f;

    [loop]
    for (uint t = 0; t < gTapCount; ++t)
    {
        // Clamp-to-edge addressing, matching build_taps()'s np.clip.  Doing it
        // here rather than with a sampler keeps the shader exact at the border,
        // where a wrap or border-colour mode would darken the outermost row.
        const int s = clamp(first + int(t), 0, maxSrc);
        const uint2 coord = (gAxis == 0) ? uint2(uint(s), tid.y)
                                         : uint2(tid.x, uint(s));

        float4 texel = gSource.Load(int3(coord, 0));
        float3 rgb = (gLinearize != 0) ? SrgbToLinear(texel.rgb) : texel.rgb;

        const float w = gWeights[wBase + t];
        acc += rgb * w;
        alpha += texel.a * w;
    }

    // Encode only on the final pass; running the intermediate through the
    // transfer function twice would compress the midtones.
    if (gLinearize != 0 && gAxis == 1)
        acc = LinearToSrgb(acc);

    gOutput[uint2(int2(tid.xy) + gOutputOffset)] = float4(acc, alpha);
}
