// Polyphase tap table construction -- the CPU half of shaders/downsample.hlsl.
//
// Deliberately free of Windows and D3D dependencies so it can be built and
// diff-tested against reference/visual4k_ref/resample.py on any machine; see
// tools/taptable_selftest.cpp.  The shader is only ever as correct as this
// table, so this file carries the numerics and nothing else.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace visual4k {

enum class Kernel {
    Triangle,   // bilinear
    CatmullRom,
    Mitchell,
    Lanczos2,   // shipping default for the resolve; see docs/ALGORITHMS.md
    Lanczos3,   // shipping default for magnification
    Lanczos4,
    Gaussian,   // DSR-style; width comes from the smoothness setting
};

// Parses "lanczos2", "catrom", "gaussian:0.6", ... Throws std::invalid_argument.
Kernel KernelFromName(const std::string& name);
const char* KernelName(Kernel k);

// Support radius in destination pixels. Gaussian's depends on its sigma.
double KernelSupport(Kernel k, double gaussianSigma = 0.5);

// Evaluates the kernel at x, measured in destination pixels from the centre.
double EvaluateKernel(Kernel k, double x, double gaussianSigma = 0.5);

// Maps a DSR-style smoothness percentage (0..100) onto a Gaussian sigma.
double DsrSmoothnessToSigma(double smoothnessPercent);

// The table the shader consumes. firstTap[d] is the source index of tap 0 for
// destination pixel d; weights[d * tapCount + t] is that tap's weight. Every
// row sums to 1, and out-of-range taps are folded onto the border pixel
// (clamp-to-edge), which is what keeps the outermost row from darkening.
struct TapTable {
    std::vector<int32_t> firstTap;
    std::vector<float>   weights;
    uint32_t srcLength = 0;
    uint32_t dstLength = 0;
    uint32_t tapCount  = 0;

    size_t WeightBytes() const { return weights.size() * sizeof(float); }
    size_t IndexBytes() const { return firstTap.size() * sizeof(int32_t); }
};

// Builds the table for one axis. When minifying, the kernel is widened by the
// scale ratio: the filter has to be low-pass for the *destination* grid, and
// forgetting that is what turns a 4K resolve back into an aliased mess.
TapTable BuildTapTable(uint32_t srcLength, uint32_t dstLength, Kernel kernel,
                       double gaussianSigma = 0.5);

}  // namespace visual4k
