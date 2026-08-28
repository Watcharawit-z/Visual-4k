#include "TapTable.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace visual4k {
namespace {

constexpr double kPi = 3.14159265358979323846;

double Sinc(double x)
{
    if (x == 0.0) return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

double Cubic(double x, double b, double c)
{
    x = std::fabs(x);
    const double x2 = x * x;
    const double x3 = x2 * x;

    if (x < 1.0) {
        return ((12.0 - 9.0 * b - 6.0 * c) * x3 +
                (-18.0 + 12.0 * b + 6.0 * c) * x2 +
                (6.0 - 2.0 * b)) / 6.0;
    }
    if (x < 2.0) {
        return ((-b - 6.0 * c) * x3 +
                (6.0 * b + 30.0 * c) * x2 +
                (-12.0 * b - 48.0 * c) * x +
                (8.0 * b + 24.0 * c)) / 6.0;
    }
    return 0.0;
}

double LanczosAt(double x, int a)
{
    if (std::fabs(x) >= static_cast<double>(a)) return 0.0;
    return Sinc(x) * Sinc(x / static_cast<double>(a));
}

}  // namespace

Kernel KernelFromName(const std::string& name)
{
    if (name == "triangle" || name == "bilinear") return Kernel::Triangle;
    if (name == "catrom")                          return Kernel::CatmullRom;
    if (name == "mitchell")                        return Kernel::Mitchell;
    if (name == "lanczos2")                        return Kernel::Lanczos2;
    if (name == "lanczos3")                        return Kernel::Lanczos3;
    if (name == "lanczos4")                        return Kernel::Lanczos4;
    if (name == "gaussian" || name.rfind("gaussian:", 0) == 0) return Kernel::Gaussian;
    throw std::invalid_argument("unknown kernel: " + name);
}

const char* KernelName(Kernel k)
{
    switch (k) {
        case Kernel::Triangle:   return "triangle";
        case Kernel::CatmullRom: return "catrom";
        case Kernel::Mitchell:   return "mitchell";
        case Kernel::Lanczos2:   return "lanczos2";
        case Kernel::Lanczos3:   return "lanczos3";
        case Kernel::Lanczos4:   return "lanczos4";
        case Kernel::Gaussian:   return "gaussian";
    }
    return "unknown";
}

double KernelSupport(Kernel k, double gaussianSigma)
{
    switch (k) {
        case Kernel::Triangle:   return 1.0;
        case Kernel::CatmullRom: return 2.0;
        case Kernel::Mitchell:   return 2.0;
        case Kernel::Lanczos2:   return 2.0;
        case Kernel::Lanczos3:   return 3.0;
        case Kernel::Lanczos4:   return 4.0;
        case Kernel::Gaussian:   return 3.25 * gaussianSigma;
    }
    return 1.0;
}

double EvaluateKernel(Kernel k, double x, double gaussianSigma)
{
    switch (k) {
        case Kernel::Triangle: {
            const double ax = std::fabs(x);
            return ax < 1.0 ? 1.0 - ax : 0.0;
        }
        case Kernel::CatmullRom: return Cubic(x, 0.0, 0.5);
        case Kernel::Mitchell:   return Cubic(x, 1.0 / 3.0, 1.0 / 3.0);
        case Kernel::Lanczos2:   return LanczosAt(x, 2);
        case Kernel::Lanczos3:   return LanczosAt(x, 3);
        case Kernel::Lanczos4:   return LanczosAt(x, 4);
        case Kernel::Gaussian: {
            const double support = 3.25 * gaussianSigma;
            if (std::fabs(x) >= support) return 0.0;
            return std::exp(-(x * x) / (2.0 * gaussianSigma * gaussianSigma));
        }
    }
    return 0.0;
}

double DsrSmoothnessToSigma(double smoothnessPercent)
{
    const double s = std::clamp(smoothnessPercent, 0.0, 100.0) / 100.0;
    return 0.25 + 0.75 * s;
}

FitRect FitPreservingAspect(uint32_t srcWidth, uint32_t srcHeight,
                            uint32_t dstWidth, uint32_t dstHeight)
{
    FitRect fit;
    if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0)
        return fit;

    // Compare aspect ratios by cross-multiplying rather than dividing, in
    // 64-bit: 3840 * 2160 already exceeds what a signed 32-bit multiply can
    // hold on the way to the comparison.
    const uint64_t srcAspect = static_cast<uint64_t>(srcWidth) * dstHeight;
    const uint64_t dstAspect = static_cast<uint64_t>(dstWidth) * srcHeight;

    if (srcAspect > dstAspect) {
        // Source is proportionally wider: fill the width, bars top and bottom.
        fit.width = dstWidth;
        fit.height = static_cast<uint32_t>(
            (static_cast<uint64_t>(dstWidth) * srcHeight + srcWidth / 2) / srcWidth);
    } else {
        // Source is proportionally taller, or the ratios match exactly.
        fit.height = dstHeight;
        fit.width = static_cast<uint32_t>(
            (static_cast<uint64_t>(dstHeight) * srcWidth + srcHeight / 2) / srcHeight);
    }

    fit.width = std::clamp<uint32_t>(fit.width, 1, dstWidth);
    fit.height = std::clamp<uint32_t>(fit.height, 1, dstHeight);
    fit.x = static_cast<int32_t>((dstWidth - fit.width) / 2);
    fit.y = static_cast<int32_t>((dstHeight - fit.height) / 2);
    return fit;
}

TapTable BuildTapTable(uint32_t srcLength, uint32_t dstLength, Kernel kernel,
                       double gaussianSigma)
{
    if (srcLength == 0 || dstLength == 0)
        throw std::invalid_argument("BuildTapTable: lengths must be >= 1");

    const double scale = static_cast<double>(srcLength) / static_cast<double>(dstLength);
    const double filterScale = std::max(scale, 1.0);   // widen only when minifying
    const double support = KernelSupport(kernel, gaussianSigma);
    const double kSupport = support * filterScale;

    std::vector<double> centres(dstLength);
    std::vector<int64_t> lo(dstLength), hi(dstLength);
    int64_t maxTaps = 0;

    for (uint32_t d = 0; d < dstLength; ++d) {
        // Pixel d covers [d, d+1) and is centred at d + 0.5, so it samples the
        // source at (d + 0.5) * scale in source *edge* coordinates. Half a
        // pixel of error here is the classic "sharp but subtly shifted" bug.
        centres[d] = (static_cast<double>(d) + 0.5) * scale;
        lo[d] = static_cast<int64_t>(std::floor(centres[d] - kSupport + 0.5));
        hi[d] = static_cast<int64_t>(std::ceil(centres[d] + kSupport + 0.5));
        maxTaps = std::max(maxTaps, hi[d] - lo[d]);
    }

    TapTable table;
    table.srcLength = srcLength;
    table.dstLength = dstLength;
    table.tapCount = static_cast<uint32_t>(maxTaps);
    table.firstTap.resize(dstLength);
    table.weights.assign(static_cast<size_t>(dstLength) * table.tapCount, 0.0f);

    const int64_t lastSrc = static_cast<int64_t>(srcLength) - 1;

    for (uint32_t d = 0; d < dstLength; ++d) {
        std::vector<double> row(table.tapCount, 0.0);
        double total = 0.0;

        for (uint32_t t = 0; t < table.tapCount; ++t) {
            const int64_t s = lo[d] + static_cast<int64_t>(t);
            if (s >= hi[d]) break;               // this row needs fewer taps
            // Tap s covers the source pixel centred at s + 0.5.
            const double arg = (static_cast<double>(s) + 0.5 - centres[d]) / filterScale;
            const double w = EvaluateKernel(kernel, arg, gaussianSigma);
            row[t] = w;
            total += w;
        }

        if (total == 0.0) total = 1.0;
        for (uint32_t t = 0; t < table.tapCount; ++t)
            table.weights[static_cast<size_t>(d) * table.tapCount + t] =
                static_cast<float>(row[t] / total);

        // The shader clamps each tap itself, but clamping the base index too
        // keeps the buffer contents meaningful when dumped for debugging.
        table.firstTap[d] = static_cast<int32_t>(std::clamp<int64_t>(lo[d], -lastSrc, lastSrc));
    }

    return table;
}

}  // namespace visual4k
