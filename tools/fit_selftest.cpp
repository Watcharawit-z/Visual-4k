// Checks the aspect-preserving fit, which decides where the resolved image
// lands on the panel.
//
// The expectations are worked out by hand rather than by a second copy of the
// same formula: getting this wrong is what made a 3440x1440 source come out
// horizontally squeezed on a 2560x1440 panel.

#include "TapTable.h"

#include <cstdio>

using namespace visual4k;

namespace {

int failures = 0;

void Check(bool condition, const char* what)
{
    std::printf("%-58s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition) ++failures;
}

void Expect(uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
            uint32_t w, uint32_t h, int32_t x, int32_t y, const char* what)
{
    const FitRect f = FitPreservingAspect(sw, sh, dw, dh);
    const bool ok = f.width == w && f.height == h && f.x == x && f.y == y;
    std::printf("%-38s -> %ux%u+%d+%d %s\n", what,
                f.width, f.height, f.x, f.y, ok ? "ok" : "FAIL");
    if (!ok) {
        std::printf("    expected %ux%u+%d+%d\n", w, h, x, y);
        ++failures;
    }
}

}  // namespace

int main()
{
    std::printf("-- matching aspect ratios keep the whole panel --\n");
    // 4K onto 1440p: both 16:9, so nothing is given up.
    Expect(3840, 2160, 2560, 1440, 2560, 1440, 0, 0, "3840x2160 -> 2560x1440");
    Expect(2560, 1440, 2560, 1440, 2560, 1440, 0, 0, "1:1 passthrough");
    Expect(5120, 2880, 2560, 1440, 2560, 1440, 0, 0, "5120x2880 -> 2560x1440");

    std::printf("\n-- a wider source is letterboxed top and bottom --\n");
    // 3440x1440 is 21.5:9. Filling 2560 wide leaves 2560 * 1440 / 3440 = 1072
    // lines (1071.6 rounded), centred with 184 above and below.
    Expect(3440, 1440, 2560, 1440, 2560, 1072, 0, 184, "3440x1440 -> 2560x1440");
    // 32:9 onto 16:9 halves the height exactly.
    Expect(3840, 1080, 1920, 1080, 1920, 540, 0, 270, "3840x1080 -> 1920x1080");

    std::printf("\n-- a taller source is pillarboxed left and right --\n");
    // 4:3 into 16:9: 1440 * 4 / 3 = 1920 wide, 320 either side.
    Expect(1600, 1200, 2560, 1440, 1920, 1440, 320, 0, "1600x1200 -> 2560x1440");
    // Portrait into landscape.
    Expect(1080, 1920, 2560, 1440, 810, 1440, 875, 0, "1080x1920 -> 2560x1440");

    std::printf("\n-- invariants --\n");
    const FitRect wide = FitPreservingAspect(3440, 1440, 2560, 1440);
    Check(wide.width <= 2560 && wide.height <= 1440,
          "never larger than the panel");
    Check(wide.x >= 0 && wide.y >= 0, "offsets are never negative");
    Check(wide.x * 2 + static_cast<int32_t>(wide.width) <= 2560 + 1,
          "centred horizontally to within a pixel");

    // A source far wider than the panel must still leave a visible row.
    const FitRect extreme = FitPreservingAspect(10000, 100, 640, 480);
    Check(extreme.height >= 1, "an extreme ratio still yields a visible row");
    Check(extreme.height <= 480, "an extreme ratio stays inside the panel");

    std::printf("\n-- degenerate input yields an empty rectangle --\n");
    Check(FitPreservingAspect(0, 1080, 1920, 1080).width == 0, "zero source width");
    Check(FitPreservingAspect(1920, 0, 1920, 1080).width == 0, "zero source height");
    Check(FitPreservingAspect(1920, 1080, 0, 1080).width == 0, "zero panel width");
    Check(FitPreservingAspect(1920, 1080, 1920, 0).width == 0, "zero panel height");

    std::printf("\n%s\n", failures == 0 ? "all fit checks passed"
                                        : "FIT SELF-TEST FAILED");
    return failures == 0 ? 0 : 1;
}
