// Validates the virtual monitor's EDID without needing Windows or a driver
// install. A malformed EDID surfaces on a real machine only as "the monitor
// appeared but has no modes", so it is worth catching here.

#include "Edid.h"

#include <cstdio>
#include <vector>

using namespace visual4k;

namespace {

int failures = 0;

void Check(bool condition, const char* what)
{
    std::printf("%-52s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition) ++failures;
}

uint32_t Field12(uint8_t lo, uint8_t hiNibble)
{
    return (static_cast<uint32_t>(hiNibble) << 8) | lo;
}

}  // namespace

int main()
{
    const auto edid = BuildEdid(3840, 2160, 60);

    Check(edid.size() == 128, "EDID is one 128-byte block");
    Check(ValidateEdid(edid), "header, version and checksum validate");

    uint32_t sum = 0;
    for (uint8_t b : edid) sum += b;
    Check((sum & 0xFF) == 0, "checksum sums the block to zero mod 256");

    Check(edid[18] == 1 && edid[19] == 4, "reports EDID 1.4");
    Check((edid[20] & 0x80) != 0, "input is flagged digital");

    const uint8_t* d = &edid[54];
    const uint32_t clock10k = d[0] | (d[1] << 8);
    Check(clock10k > 0 && clock10k <= 0xFFFF,
          "pixel clock fits EDID's 16-bit field");

    const uint32_t hActive = Field12(d[2], d[4] >> 4);
    const uint32_t hBlank  = Field12(d[3], d[4] & 0x0F);
    const uint32_t vActive = Field12(d[5], d[7] >> 4);
    const uint32_t vBlank  = Field12(d[6], d[7] & 0x0F);

    Check(hActive == 3840, "detailed timing carries 3840 active pixels");
    Check(vActive == 2160, "detailed timing carries 2160 active lines");
    Check(hBlank == 160, "CVT reduced blanking (160px) is used");

    const uint64_t hTotal = hActive + hBlank;
    const uint64_t vTotal = vActive + vBlank;
    const double refresh = (clock10k * 10000.0) / (hTotal * vTotal);
    std::printf("  timing: %llux%llu total, %.0f kHz clock, %.2f Hz\n",
                static_cast<unsigned long long>(hTotal),
                static_cast<unsigned long long>(vTotal),
                clock10k * 10.0, refresh);
    Check(refresh > 59.0 && refresh < 61.0, "encoded refresh rate is 60 Hz");

    Check(edid[72 + 3] == 0xFC, "descriptor 2 is the monitor name");
    Check(edid[126] == 0, "no extension blocks are declared");

    // A mode too fast for the 16-bit clock field must clamp, not wrap.
    const auto big = BuildEdid(5120, 2880, 60);
    const uint32_t bigClock = big[54] | (big[55] << 8);
    Check(bigClock == 0xFFFF, "over-fast modes clamp the clock instead of wrapping");
    Check(ValidateEdid(big), "clamped EDID still validates");

    std::printf("\n%s\n", failures == 0 ? "all EDID checks passed"
                                        : "EDID SELF-TEST FAILED");
    return failures == 0 ? 0 : 1;
}
