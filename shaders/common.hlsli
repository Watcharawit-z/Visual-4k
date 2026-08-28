// Shared helpers for the Visual-4k compositor shaders.
//
// Every function here has a Python twin in reference/visual4k_ref/.  When the
// two disagree, the Python one is right: it is covered by reference/tests and
// this file is not directly testable on a build machine without a GPU.

#ifndef VISUAL4K_COMMON_HLSLI
#define VISUAL4K_COMMON_HLSLI

// Rec.709 luma, used for metrics and for the sharpener's noise term.
float Luma709(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// FSR's cheap luma proxy: exactly 2x the perceptual luma it approximates.
// Keep the factor of 2 -- RCAS's noise term is calibrated against it.
float LumaTimes2(float3 c)
{
    return c.r * 0.5f + c.g + c.b * 0.5f;
}

// Exact sRGB transfer functions.  The fast pow(x, 2.2) approximation is not
// good enough here: the resolve averages millions of pixels per frame and the
// error in the near-black segment accumulates into a visible black crush.
float3 SrgbToLinear(float3 c)
{
    float3 lo = c / 12.92f;
    float3 hi = pow(max(c + 0.055f, 1e-6f) / 1.055f, 2.4f);
    return lerp(hi, lo, step(c, 0.04045f));
}

float3 LinearToSrgb(float3 c)
{
    c = saturate(c);
    float3 lo = c * 12.92f;
    float3 hi = 1.055f * pow(max(c, 1e-6f), 1.0f / 2.4f) - 0.055f;
    return lerp(hi, lo, step(c, 0.0031308f));
}

#endif // VISUAL4K_COMMON_HLSLI
