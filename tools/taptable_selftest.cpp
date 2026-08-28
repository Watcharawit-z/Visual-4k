// Dumps a tap table as CSV so it can be diffed against the Python reference.
//
// Build:  g++ -O2 -std=c++17 -I host/visual4k-host/src
//              tools/taptable_selftest.cpp host/visual4k-host/src/TapTable.cpp
//              -o build/taptable_selftest
// Run:    build/taptable_selftest <srcLen> <dstLen> <kernel> [sigma]
//
// Checked by tools/compare_taptable.py, which is part of the test suite: the
// compositor and the reference must agree bit-for-bit at float32, otherwise
// "the shader looks slightly different" turns into an unfalsifiable argument.

#include "TapTable.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: %s <srcLen> <dstLen> <kernel> [gaussianSigma]\n",
                     argv[0]);
        return 2;
    }

    try {
        const auto srcLen = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
        const auto dstLen = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
        const std::string kernelName = argv[3];
        const double sigma = (argc > 4) ? std::strtod(argv[4], nullptr) : 0.5;

        const auto kernel = visual4k::KernelFromName(kernelName);
        const auto table = visual4k::BuildTapTable(srcLen, dstLen, kernel, sigma);

        std::printf("# src=%u dst=%u kernel=%s taps=%u\n",
                    table.srcLength, table.dstLength,
                    visual4k::KernelName(kernel), table.tapCount);

        for (uint32_t d = 0; d < table.dstLength; ++d) {
            std::printf("%d", table.firstTap[d]);
            for (uint32_t t = 0; t < table.tapCount; ++t)
                std::printf(",%.9g",
                            table.weights[static_cast<size_t>(d) * table.tapCount + t]);
            std::printf("\n");
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
