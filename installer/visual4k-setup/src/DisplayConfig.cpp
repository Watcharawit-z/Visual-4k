#include "DisplayConfig.h"

#include <windows.h>

#include <cstring>
#include <cwchar>

#include "Ui.h"

namespace visual4k {
namespace {

// Matches the DeviceName string in Visual4kDisplay.inf. Compared as a prefix
// because Windows appends its own text to some adapter descriptions.
const wchar_t kVirtualDisplayMarker[] = L"Visual-4k";

bool CurrentMode(const std::wstring& deviceName, DEVMODEW* mode)
{
    std::memset(mode, 0, sizeof(*mode));
    mode->dmSize = sizeof(*mode);
    return EnumDisplaySettingsExW(deviceName.c_str(), ENUM_CURRENT_SETTINGS,
                                  mode, 0) != FALSE;
}

}  // namespace

std::vector<DisplayInfo> EnumerateDisplays()
{
    std::vector<DisplayInfo> displays;

    DISPLAY_DEVICEW device = {};
    device.cb = sizeof(device);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i) {
        DisplayInfo info;
        info.deviceName = device.DeviceName;
        info.description = device.DeviceString;
        info.attached =
            (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
        info.primary =
            (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

        // A detached display has no meaningful mode; asking for one returns
        // whatever it last used, which would be reported as its current size.
        if (info.attached) {
            DEVMODEW mode;
            if (CurrentMode(info.deviceName, &mode)) {
                info.width = mode.dmPelsWidth;
                info.height = mode.dmPelsHeight;
                info.x = mode.dmPosition.x;
                info.y = mode.dmPosition.y;
            }
        }
        displays.push_back(info);

        device = {};
        device.cb = sizeof(device);
    }
    return displays;
}

bool FindVirtualDisplay(DisplayInfo* out)
{
    for (const auto& display : EnumerateDisplays()) {
        if (!display.attached)
            continue;
        if (display.description.compare(0, wcslen(kVirtualDisplayMarker),
                                        kVirtualDisplayMarker) == 0) {
            *out = display;
            return true;
        }
    }
    return false;
}

bool FindPresentationPanel(const std::wstring& virtualDeviceName,
                           DisplayInfo* out)
{
    const std::vector<DisplayInfo> displays = EnumerateDisplays();

    for (const auto& display : displays) {
        if (display.attached && display.primary &&
            display.deviceName != virtualDeviceName) {
            *out = display;
            return true;
        }
    }
    for (const auto& display : displays) {
        if (display.attached && display.deviceName != virtualDeviceName) {
            *out = display;
            return true;
        }
    }
    return false;
}

SavedLayout CaptureLayout()
{
    SavedLayout layout;
    for (const auto& display : EnumerateDisplays()) {
        if (!display.attached)
            continue;
        DEVMODEW mode;
        if (!CurrentMode(display.deviceName, &mode))
            continue;

        layout.deviceNames.push_back(display.deviceName);
        const auto* bytes = reinterpret_cast<const uint8_t*>(&mode);
        layout.modes.emplace_back(bytes, bytes + sizeof(mode));
    }
    return layout;
}

bool RestoreLayout(const SavedLayout& layout)
{
    if (layout.deviceNames.empty())
        return false;

    bool ok = true;
    for (size_t i = 0; i < layout.deviceNames.size(); ++i) {
        DEVMODEW mode;
        std::memcpy(&mode, layout.modes[i].data(), sizeof(mode));
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_POSITION |
                        DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

        DWORD flags = CDS_UPDATEREGISTRY | CDS_NORESET;
        // The display that was at the origin is by definition the one that was
        // primary, and restoring the positions without restoring that would
        // leave the desktop laid out correctly around the wrong screen.
        if (mode.dmPosition.x == 0 && mode.dmPosition.y == 0)
            flags |= CDS_SET_PRIMARY;

        if (ChangeDisplaySettingsExW(layout.deviceNames[i].c_str(), &mode,
                                     nullptr, flags,
                                     nullptr) != DISP_CHANGE_SUCCESSFUL)
            ok = false;
    }

    // Nothing above took effect until this: CDS_NORESET writes the registry and
    // defers, and one apply for the whole set avoids the desktop passing
    // through half-applied arrangements.
    ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    return ok;
}

bool SetResolution(const std::wstring& deviceName, uint32_t width,
                   uint32_t height)
{
    DEVMODEW mode;
    if (!CurrentMode(deviceName, &mode))
        return false;

    if (mode.dmPelsWidth == width && mode.dmPelsHeight == height)
        return true;

    mode.dmPelsWidth = width;
    mode.dmPelsHeight = height;
    mode.dmBitsPerPel = 32;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

    const LONG result = ChangeDisplaySettingsExW(
        deviceName.c_str(), &mode, nullptr, CDS_UPDATEREGISTRY, nullptr);
    return result == DISP_CHANGE_SUCCESSFUL;
}

bool MakePrimary(const std::wstring& deviceName)
{
    DEVMODEW target;
    if (!CurrentMode(deviceName, &target))
        return false;

    // Primary is not a flag a display carries; it is whichever display sits at
    // the virtual desktop's origin. So making one primary means translating
    // every display by the negative of the target's position.
    const LONG shiftX = target.dmPosition.x;
    const LONG shiftY = target.dmPosition.y;

    bool ok = true;
    for (const auto& display : EnumerateDisplays()) {
        if (!display.attached)
            continue;

        DEVMODEW mode;
        if (!CurrentMode(display.deviceName, &mode))
            continue;

        mode.dmPosition.x -= shiftX;
        mode.dmPosition.y -= shiftY;
        mode.dmFields = DM_POSITION;

        DWORD flags = CDS_UPDATEREGISTRY | CDS_NORESET;
        if (display.deviceName == deviceName)
            flags |= CDS_SET_PRIMARY;

        if (ChangeDisplaySettingsExW(display.deviceName.c_str(), &mode, nullptr,
                                     flags, nullptr) != DISP_CHANGE_SUCCESSFUL)
            ok = false;
    }

    const LONG applied =
        ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    return ok && applied == DISP_CHANGE_SUCCESSFUL;
}

}  // namespace visual4k
