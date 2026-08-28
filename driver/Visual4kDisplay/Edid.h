// EDID generation for the virtual monitor.
//
// Kept free of Windows headers so it can be compiled and validated on any
// machine -- see tools/edid_selftest.cpp. That matters more than it sounds:
// Windows validates the checksum and the detailed timing block but reports a
// failure only as "monitor arrived with no supported modes", which is a
// miserable thing to debug on a live driver.

#pragma once

#include <cstdint>
#include <vector>

namespace visual4k {

// Builds a 128-byte EDID 1.4 block describing a digital monitor whose
// preferred timing is width x height @ refreshHz.
//
// The detailed timing descriptor uses CVT reduced-blanking, which is what a
// real DisplayPort 4K panel reports and what keeps the pixel clock inside the
// 655.35 MHz that EDID's 16-bit field can encode. Modes too fast to encode
// here (5120x2880 among them) are still offered -- they come from the driver's
// QueryTargetModes callback, which has no such limit.
std::vector<uint8_t> BuildEdid(uint32_t width, uint32_t height,
                               uint32_t refreshHz);

// True when the block is a structurally valid EDID: header, version, and
// checksum. Used by the self-test and by the driver's own startup assertion.
bool ValidateEdid(const std::vector<uint8_t>& edid);

}  // namespace visual4k
