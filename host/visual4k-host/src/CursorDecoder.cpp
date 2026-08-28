#include "CursorDecoder.h"

namespace visual4k {
namespace {

// Monochrome masks are 1 bit per pixel, most significant bit leftmost.
inline bool TestBit(const uint8_t* row, uint32_t x)
{
    return (row[x / 8] & (0x80u >> (x % 8))) != 0;
}

DecodedCursor DecodeMonochrome(const uint8_t* data, size_t dataSize,
                               uint32_t width, uint32_t height, uint32_t pitch)
{
    // The AND mask is stacked directly on top of the XOR mask, so the reported
    // height is twice the cursor's own.
    const uint32_t realHeight = height / 2;
    if (realHeight == 0) return {};

    const size_t needed = static_cast<size_t>(pitch) * height;
    if (dataSize < needed) return {};

    DecodedCursor out;
    out.width = width;
    out.height = realHeight;
    out.rgba.assign(static_cast<size_t>(width) * realHeight * 4, 0);
    out.invert.assign(static_cast<size_t>(width) * realHeight, 0);

    for (uint32_t y = 0; y < realHeight; ++y) {
        const uint8_t* andRow = data + static_cast<size_t>(y) * pitch;
        const uint8_t* xorRow = data + static_cast<size_t>(y + realHeight) * pitch;

        for (uint32_t x = 0; x < width; ++x) {
            const bool andBit = TestBit(andRow, x);
            const bool xorBit = TestBit(xorRow, x);

            const size_t i = (static_cast<size_t>(y) * width + x);
            uint8_t* px = &out.rgba[i * 4];

            if (!andBit && !xorBit) {            // opaque black
                px[0] = px[1] = px[2] = 0;
                px[3] = 255;
            } else if (!andBit && xorBit) {      // opaque white
                px[0] = px[1] = px[2] = 255;
                px[3] = 255;
            } else if (andBit && !xorBit) {      // transparent
                px[3] = 0;
            } else {                             // invert whatever is behind
                px[3] = 0;
                out.invert[i] = 255;
            }
        }
    }

    return out;
}

DecodedCursor DecodeColor(const uint8_t* data, size_t dataSize, uint32_t width,
                          uint32_t height, uint32_t pitch)
{
    const size_t needed = static_cast<size_t>(pitch) * height;
    if (dataSize < needed || height == 0) return {};

    DecodedCursor out;
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    out.invert.assign(static_cast<size_t>(width) * height, 0);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = data + static_cast<size_t>(y) * pitch;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* src = row + static_cast<size_t>(x) * 4;
            uint8_t* dst = &out.rgba[(static_cast<size_t>(y) * width + x) * 4];
            // Source is BGRA; the compositor wants RGBA.
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }

    return out;
}

DecodedCursor DecodeMaskedColor(const uint8_t* data, size_t dataSize,
                                uint32_t width, uint32_t height, uint32_t pitch)
{
    const size_t needed = static_cast<size_t>(pitch) * height;
    if (dataSize < needed || height == 0) return {};

    DecodedCursor out;
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    out.invert.assign(static_cast<size_t>(width) * height, 0);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = data + static_cast<size_t>(y) * pitch;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* src = row + static_cast<size_t>(x) * 4;
            const size_t i = static_cast<size_t>(y) * width + x;
            uint8_t* dst = &out.rgba[i * 4];

            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];

            // Alpha is a two-state flag here, not a coverage value: 0 means
            // "replace the screen with this colour", 255 means "XOR it".
            if (src[3] == 0) {
                dst[3] = 255;
            } else {
                dst[3] = 0;
                out.invert[i] = 255;
            }
        }
    }

    return out;
}

}  // namespace

DecodedCursor DecodePointerShape(PointerShapeType type, const uint8_t* data,
                                 size_t dataSize, uint32_t width,
                                 uint32_t height, uint32_t pitch)
{
    if (data == nullptr || width == 0 || height == 0 || pitch == 0)
        return {};

    switch (type) {
        case PointerShapeType::Monochrome:
            return DecodeMonochrome(data, dataSize, width, height, pitch);
        case PointerShapeType::Color:
            return DecodeColor(data, dataSize, width, height, pitch);
        case PointerShapeType::MaskedColor:
            return DecodeMaskedColor(data, dataSize, width, height, pitch);
    }
    return {};
}

}  // namespace visual4k
