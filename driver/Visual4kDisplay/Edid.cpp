#include "Edid.h"

#include <cstring>

namespace visual4k {
namespace {

// CVT reduced-blanking v1 constants. These are fixed by the standard, which is
// why a 4K CVT-RB mode always has a 160-pixel horizontal blanking interval.
constexpr uint32_t kRbHBlank = 160;
constexpr uint32_t kRbHFrontPorch = 48;
constexpr uint32_t kRbHSyncWidth = 32;
constexpr uint32_t kRbVFrontPorch = 3;
constexpr uint32_t kRbVSyncWidth = 5;
constexpr uint32_t kRbMinVBlank = 460;   // microseconds, per CVT-RB

void PackManufacturerId(uint8_t* out, const char id[3])
{
    // EDID packs three uppercase letters into 5 bits each, 'A' == 1.
    const uint16_t packed =
        static_cast<uint16_t>(((id[0] - 'A' + 1) & 0x1F) << 10) |
        static_cast<uint16_t>(((id[1] - 'A' + 1) & 0x1F) << 5) |
        static_cast<uint16_t>((id[2] - 'A' + 1) & 0x1F);
    out[0] = static_cast<uint8_t>(packed >> 8);
    out[1] = static_cast<uint8_t>(packed & 0xFF);
}

void WriteTextDescriptor(uint8_t* d, uint8_t tag, const char* text)
{
    d[0] = 0x00; d[1] = 0x00; d[2] = 0x00;
    d[3] = tag;
    d[4] = 0x00;

    // 13 bytes, padded with 0x0A then 0x20 -- the padding is mandated, and
    // some display stacks show garbage in Settings if it is wrong.
    for (int i = 0; i < 13; ++i)
        d[5 + i] = 0x20;

    int i = 0;
    for (; i < 13 && text[i] != '\0'; ++i)
        d[5 + i] = static_cast<uint8_t>(text[i]);
    if (i < 13)
        d[5 + i] = 0x0A;
}

void WriteDetailedTiming(uint8_t* d, uint32_t width, uint32_t height,
                         uint32_t refreshHz)
{
    const uint32_t hTotal = width + kRbHBlank;

    // CVT-RB fixes the vertical blanking to at least kRbMinVBlank microseconds
    // of line time; solve for the line count that satisfies it.
    uint32_t vBlank = kRbVFrontPorch + kRbVSyncWidth + 6;
    {
        // One iteration is enough: the line rate depends on vTotal only weakly.
        const uint32_t approxLineRate = refreshHz * (height + vBlank);
        const uint32_t lines =
            (kRbMinVBlank * approxLineRate + 999999u) / 1000000u;
        vBlank = lines > vBlank ? lines : vBlank;
    }
    const uint32_t vTotal = height + vBlank;

    // Pixel clock in 10 kHz units, which is all EDID's 16-bit field can hold.
    const uint64_t pixelClockHz =
        static_cast<uint64_t>(hTotal) * vTotal * refreshHz;
    uint32_t clock10k = static_cast<uint32_t>((pixelClockHz + 5000) / 10000);
    if (clock10k > 0xFFFF) clock10k = 0xFFFF;

    const uint32_t vBackPorch = vBlank - kRbVFrontPorch - kRbVSyncWidth;
    (void)vBackPorch;   // implied by the other three; not encoded separately

    d[0] = static_cast<uint8_t>(clock10k & 0xFF);
    d[1] = static_cast<uint8_t>(clock10k >> 8);

    d[2] = static_cast<uint8_t>(width & 0xFF);
    d[3] = static_cast<uint8_t>(kRbHBlank & 0xFF);
    d[4] = static_cast<uint8_t>(((width >> 8) << 4) | ((kRbHBlank >> 8) & 0x0F));

    d[5] = static_cast<uint8_t>(height & 0xFF);
    d[6] = static_cast<uint8_t>(vBlank & 0xFF);
    d[7] = static_cast<uint8_t>(((height >> 8) << 4) | ((vBlank >> 8) & 0x0F));

    d[8] = static_cast<uint8_t>(kRbHFrontPorch & 0xFF);
    d[9] = static_cast<uint8_t>(kRbHSyncWidth & 0xFF);
    d[10] = static_cast<uint8_t>(((kRbVFrontPorch & 0x0F) << 4) |
                                 (kRbVSyncWidth & 0x0F));
    d[11] = static_cast<uint8_t>((((kRbHFrontPorch >> 8) & 0x03) << 6) |
                                 (((kRbHSyncWidth >> 8) & 0x03) << 4) |
                                 (((kRbVFrontPorch >> 4) & 0x03) << 2) |
                                 ((kRbVSyncWidth >> 4) & 0x03));

    // Physical size in millimetres. 16:9 at roughly 27 inches, which is what a
    // 1440p panel usually is; it only affects the DPI Windows suggests.
    const uint32_t widthMm = 597;
    const uint32_t heightMm = 336;
    d[12] = static_cast<uint8_t>(widthMm & 0xFF);
    d[13] = static_cast<uint8_t>(heightMm & 0xFF);
    d[14] = static_cast<uint8_t>(((widthMm >> 8) << 4) | ((heightMm >> 8) & 0x0F));

    d[15] = 0x00;   // horizontal border
    d[16] = 0x00;   // vertical border
    // Digital separate sync, positive vertical, negative horizontal -- the
    // polarity CVT-RB specifies.
    d[17] = 0x1E;
}

}  // namespace

std::vector<uint8_t> BuildEdid(uint32_t width, uint32_t height,
                               uint32_t refreshHz)
{
    std::vector<uint8_t> edid(128, 0);

    // Header.
    static const uint8_t kHeader[8] = {0x00, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0x00};
    std::memcpy(edid.data(), kHeader, sizeof(kHeader));

    // "VSL" is not a registered PNP vendor ID; it is deliberately outside the
    // assigned space so this virtual monitor can never be confused with real
    // hardware in a crash dump or a support log.
    PackManufacturerId(&edid[8], "VSL");
    edid[10] = 0x4B; edid[11] = 0x34;          // product code 0x344B ("4K")
    edid[12] = 0x01; edid[13] = 0x00;
    edid[14] = 0x00; edid[15] = 0x00;          // serial number
    edid[16] = 1;                              // week of manufacture
    edid[17] = 34;                             // year 1990 + 34 = 2024

    edid[18] = 1;   // EDID version 1
    edid[19] = 4;   // revision 4

    // Digital input, 8 bits per colour, DisplayPort.
    edid[20] = 0x80 | (0x01 << 4) | 0x05;

    edid[21] = 60;    // max horizontal image size, cm
    edid[22] = 34;    // max vertical image size, cm
    edid[23] = 120;   // gamma: (2.20 * 100) - 100

    // Feature support: digital display, sRGB default colour space, preferred
    // timing includes native pixel format, continuous frequency.
    edid[24] = 0x06;

    // Chromaticity for sRGB primaries.
    static const uint8_t kSrgbChromaticity[10] = {
        0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54};
    std::memcpy(&edid[25], kSrgbChromaticity, sizeof(kSrgbChromaticity));

    // No established or standard timings: everything this monitor supports is
    // a detailed or driver-supplied mode, and advertising 640x480 here would
    // let Windows fall back to a mode the compositor cannot resolve from.
    edid[35] = 0x00; edid[36] = 0x00; edid[37] = 0x00;
    for (int i = 38; i < 54; i += 2) {
        edid[i] = 0x01;      // unused standard timing marker
        edid[i + 1] = 0x01;
    }

    WriteDetailedTiming(&edid[54], width, height, refreshHz);
    WriteTextDescriptor(&edid[72], 0xFC, "Visual-4k");        // monitor name
    WriteTextDescriptor(&edid[90], 0xFF, "VSL4K0001");        // serial string
    WriteTextDescriptor(&edid[108], 0xFE, "Virtual");         // unspecified text

    edid[126] = 0;   // no extension blocks

    uint32_t sum = 0;
    for (int i = 0; i < 127; ++i)
        sum += edid[i];
    edid[127] = static_cast<uint8_t>((256 - (sum & 0xFF)) & 0xFF);

    return edid;
}

bool ValidateEdid(const std::vector<uint8_t>& edid)
{
    if (edid.size() != 128)
        return false;

    static const uint8_t kHeader[8] = {0x00, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0x00};
    if (std::memcmp(edid.data(), kHeader, sizeof(kHeader)) != 0)
        return false;

    if (edid[18] != 1)
        return false;

    uint32_t sum = 0;
    for (uint8_t b : edid)
        sum += b;
    return (sum & 0xFF) == 0;
}

}  // namespace visual4k
