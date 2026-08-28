// Creates the virtual display device, which is the part no ordinary tool does
// for you.
//
// The driver has no hardware to bind to: it is root-enumerated, meaning the
// device node has to be conjured before Windows will ever load the driver.
// pnputil stages the package but stops there, and the two documented ways to
// create the node are devcon -- which ships only inside the WDK -- and six
// manual steps through Device Manager's "Add legacy hardware" wizard.
//
// This does what devcon does, through the same SetupAPI calls, so that neither
// is needed.

#pragma once

#include <string>

#include "CertTrust.h"  // Result

namespace visual4k {

// True when a device with our hardware ID is already present, whatever state
// it is in. Setup uses this to update rather than duplicate.
bool VirtualDisplayDevicePresent();

// Stages the package into the driver store, creates the device node, and binds
// the driver to it. Removes the node again if the binding fails, so a failed
// run does not leave a dead device behind for the next one to trip over.
Result InstallVirtualDisplay(const std::wstring& infPath, bool* rebootRequired);

// Removes every device with our hardware ID, then deletes the package from the
// driver store.
Result RemoveVirtualDisplay(const std::wstring& infPath, bool* rebootRequired);

}  // namespace visual4k
