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

#include <cstdint>
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

// What Windows thinks of the device once it exists.
//
// "The driver installed but no display appeared" is not a diagnosis, and
// sending someone to Device Manager to find the real one costs a round trip
// and assumes they know where to look. Windows already recorded exactly why;
// this reads it.
struct DeviceStatus {
    bool present = false;
    bool started = false;
    uint32_t problemCode = 0;
    std::wstring explanation;   // what the code means, and what to do
};

DeviceStatus QueryVirtualDisplayStatus();

// Creates the key the driver records its progress into, with an ACL that lets
// the driver's host process write to it.
//
// The driver runs in a service host, not as the user, so it cannot create a
// key under HKLM\SOFTWARE itself. Setup can, and does it before the device is
// created so that the very first start attempt is already being recorded.
bool PrepareDiagnosticsKey();

// Erases the previous attempt's record.
//
// Without this a failure that never reaches the driver at all leaves the
// record from an earlier run sitting there, complete with its own timestamp
// and the version it was built with, looking exactly like a fresh answer. That
// happened, and it very nearly sent the next fix in the wrong direction.
void ClearDriverRecord();

// What the driver last recorded, empty if it recorded nothing. "Nothing" is
// itself informative: it means the driver's EvtDeviceAdd was never entered.
struct DriverRecord {
    bool present = false;
    std::wstring stage;
    uint32_t status = 0;
    std::wstring time;
    // Structure sizes and framework versions the driver was compiled with,
    // recorded before the call that rejects them.
    std::wstring compiledSizes;
};

DriverRecord ReadDriverRecord();

// Removes every device with our hardware ID, then deletes the package from the
// driver store.
Result RemoveVirtualDisplay(const std::wstring& infPath, bool* rebootRequired);

}  // namespace visual4k
