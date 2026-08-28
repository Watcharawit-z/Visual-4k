// Decodes DXGI Desktop Duplication pointer shapes into a plain RGBA bitmap.
//
// Desktop Duplication does not draw the mouse into the frames it hands you --
// it reports the pointer's position and shape separately, and leaves the
// compositing to the caller. Skipping that step gives you a desktop with no
// cursor, which is not a usable desktop.
//
// Windows still describes cursors in the three formats it has used since the
// 1980s, two of which combine with whatever is already on screen:
//
//   COLOR         straight BGRA. The easy case.
//   MONOCHROME    two stacked 1-bit masks, AND then XOR. The four combinations
//                 mean black, white, transparent, and *invert the screen*.
//   MASKED_COLOR  BGRA where alpha is only ever 0 or 255: 0 means draw this
//                 colour, 255 means XOR it with the screen.
//
// Because two of the three can invert the screen, one RGBA image is not enough
// to describe the result. This decoder emits an RGBA bitmap plus a parallel
// one-byte-per-pixel invert mask, and the shader applies both.
//
// Deliberately free of Windows headers so it can be tested anywhere; see
// reference/tests/test_cursor_decode.py.

#pragma once

#include <cstdint>
#include <vector>

namespace visual4k {

// Values match DXGI_OUTDUPL_POINTER_SHAPE_TYPE.
enum class PointerShapeType : uint32_t {
    Monochrome = 0x1,
    Color = 0x2,
    MaskedColor = 0x4,
};

struct DecodedCursor {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;     // width * height * 4, straight alpha
    std::vector<uint8_t> invert;   // width * height, 255 = XOR with the screen

    bool Empty() const { return width == 0 || height == 0; }
};

// Decodes one pointer shape.
//
// `data`/`pitch`/`width`/`height` come straight from
// IDXGIOutputDuplication::GetFramePointerShape and its
// DXGI_OUTDUPL_POINTER_SHAPE_INFO. For MONOCHROME, `height` is twice the
// cursor's real height, because the two masks are stacked; the returned
// DecodedCursor has the real height.
//
// Returns an empty DecodedCursor if the buffer is too small for the described
// geometry, rather than reading past it.
DecodedCursor DecodePointerShape(PointerShapeType type, const uint8_t* data,
                                 size_t dataSize, uint32_t width,
                                 uint32_t height, uint32_t pitch);

}  // namespace visual4k
