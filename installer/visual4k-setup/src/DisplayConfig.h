// Finding, resizing and rearranging displays.
//
// The dangerous part of setup lives here. Making the virtual display primary
// moves the desktop onto a screen nobody can see until the compositor is
// mirroring it, and -- unlike the same change made from the Settings app --
// nothing reverts it on its own: ChangeDisplaySettingsEx with CDS_UPDATEREGISTRY
// is permanent the moment it is applied. So the layout is captured before any
// change and can be put back.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace visual4k {

struct DisplayInfo {
    std::wstring deviceName;   // \\.\DISPLAY1
    std::wstring description;  // adapter string, e.g. "Visual-4k Virtual Display"
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t x = 0;
    int32_t y = 0;
    bool primary = false;
    bool attached = false;
};

std::vector<DisplayInfo> EnumerateDisplays();

// Identifies the virtual display by the adapter description the INF gives it.
// Returns false when it is not attached, which is the normal state before the
// driver is installed and the signal that something went wrong after.
bool FindVirtualDisplay(DisplayInfo* out);

// The physical panel to present on: any attached display that is not the
// virtual one. Prefers the current primary, since that is the screen the user
// is looking at right now.
bool FindPresentationPanel(const std::wstring& virtualDeviceName,
                           DisplayInfo* out);

// A layout that can be restored wholesale, used as the undo for the steps
// below.
struct SavedLayout {
    std::vector<std::wstring> deviceNames;
    std::vector<std::vector<uint8_t>> modes;  // opaque DEVMODEW blobs
};

SavedLayout CaptureLayout();
bool RestoreLayout(const SavedLayout& layout);

bool SetResolution(const std::wstring& deviceName, uint32_t width,
                   uint32_t height);

// Moves the whole desktop so that `deviceName` sits at the origin and becomes
// primary, keeping every other display in the same place relative to it.
bool MakePrimary(const std::wstring& deviceName);

}  // namespace visual4k
