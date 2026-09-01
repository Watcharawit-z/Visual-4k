#include "DeviceInstall.h"

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <sddl.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "Ui.h"

namespace visual4k {
namespace {

// Display class. Spelled out rather than taken from devguid.h so that this
// file needs nothing beyond the base SDK headers.
const GUID kDisplayClass = {0x4d36e968, 0xe325, 0x11ce,
                            {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

// Must match the model line in Visual4kDisplay.inf exactly; the INF is what
// binds this string to the driver.
const wchar_t kHardwareId[] = L"Root\\Visual4kDisplay";

// SPDRP_HARDWAREID is a REG_MULTI_SZ, so the value written has to carry its
// own terminating empty string. Getting this wrong produces a device whose
// hardware ID looks right in Device Manager and matches nothing.
std::vector<wchar_t> HardwareIdMultiSz()
{
    const size_t length = wcslen(kHardwareId);
    std::vector<wchar_t> value(length + 2, L'\0');
    std::copy(kHardwareId, kHardwareId + length, value.begin());
    return value;
}

bool DeviceHasOurHardwareId(HDEVINFO set, SP_DEVINFO_DATA* data)
{
    // Two buffers' worth is plenty for one short ID, but the call is asked for
    // its size anyway rather than assuming.
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, data, SPDRP_HARDWAREID, nullptr,
                                      nullptr, 0, &required);
    if (required == 0)
        return false;

    std::vector<BYTE> buffer(required + sizeof(wchar_t) * 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, data, SPDRP_HARDWAREID, nullptr,
                                           buffer.data(), required, nullptr))
        return false;

    // REG_MULTI_SZ: walk the strings until the empty one.
    const wchar_t* id = reinterpret_cast<const wchar_t*>(buffer.data());
    while (*id != L'\0') {
        if (_wcsicmp(id, kHardwareId) == 0)
            return true;
        id += wcslen(id) + 1;
    }
    return false;
}

}  // namespace

bool VirtualDisplayDevicePresent()
{
    HDEVINFO set = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr,
                                        DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    SP_DEVINFO_DATA data = {};
    data.cbSize = sizeof(data);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        if (DeviceHasOurHardwareId(set, &data)) {
            found = true;
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

namespace {

// The problem codes a driver install can realistically produce, in the words
// that say what to do about them. Windows' own text is written for a user
// looking at hardware they bought, and says "contact the manufacturer".
std::wstring ExplainProblem(uint32_t code)
{
    switch (code) {
        case 0:
            return L"";
        case 31:  // CM_PROB_FAILED_ADD
            return L"The driver loaded, and then refused the device.\n"
                   L"  This is a fault in the driver itself rather than in the "
                   L"install:\n"
                   L"  its EvtDeviceAdd callback returned a failure. The usual "
                   L"cause is\n"
                   L"  an IddCx version mismatch between the driver binary and "
                   L"the INF.";
        case 52:  // CM_PROB_UNSIGNED_DRIVER
            return L"Windows refused the driver's signature.\n"
                   L"  Test signing is not actually on. Turning it on needs a "
                   L"restart, and\n"
                   L"  Secure Boot overrides it entirely -- the setting then "
                   L"succeeds and\n"
                   L"  does nothing.";
        case 28:  // CM_PROB_FAILED_INSTALL
            return L"No driver is installed for the device.\n"
                   L"  The device node was created but nothing bound to it.";
        case 39:  // CM_PROB_DRIVER_FAILED_LOAD
            return L"Windows could not load the driver binary.\n"
                   L"  It is missing, unreadable, or corrupt in the driver "
                   L"store.";
        case 37:  // CM_PROB_FAILED_DRIVER_ENTRY
            return L"The driver's entry point returned a failure.";
        case 10:  // CM_PROB_FAILED_START
            return L"The device failed to start.";
        case 18:  // CM_PROB_REINSTALL
            return L"The driver needs reinstalling.";
        case 19:  // CM_PROB_REGISTRY
            return L"The device's registry configuration is damaged.\n"
                   L"  Remove it with option 3 and install again.";
        default:
            return L"Windows reported a problem with the device.";
    }
}

}  // namespace

DeviceStatus QueryVirtualDisplayStatus()
{
    DeviceStatus status;

    // DIGCF_PRESENT is deliberately absent: a device that failed to start is
    // exactly the case worth reporting, and filtering to present devices can
    // hide it.
    HDEVINFO set = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr, 0);
    if (set == INVALID_HANDLE_VALUE)
        return status;

    SP_DEVINFO_DATA data = {};
    for (DWORD i = 0;; ++i) {
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(set, i, &data))
            break;
        if (!DeviceHasOurHardwareId(set, &data))
            continue;

        status.present = true;

        ULONG state = 0;
        ULONG problem = 0;
        if (CM_Get_DevNode_Status(&state, &problem, data.DevInst, 0) == CR_SUCCESS) {
            status.problemCode = static_cast<uint32_t>(problem);
            status.started = (state & DN_STARTED) != 0 && problem == 0;
            status.explanation = ExplainProblem(status.problemCode);
        }
        break;
    }

    SetupDiDestroyDeviceInfoList(set);
    return status;
}

// Must match Diagnostics.h in the driver.
const wchar_t kDiagnosticsKey[] = L"SOFTWARE\\Visual-4k";

bool PrepareDiagnosticsKey()
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kDiagnosticsKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | WRITE_DAC,
                        nullptr, &key, &disposition) != ERROR_SUCCESS)
        return false;

    // Full access for SYSTEM and Administrators, and for the two service
    // accounts a UMDF host can run under. Scoped to this one key, which holds
    // nothing but a failure message.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const wchar_t* sddl = L"D:(A;;KA;;;SY)(A;;KA;;;BA)(A;;KA;;;LS)(A;;KA;;;NS)";
    bool ok = false;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &descriptor, nullptr)) {
        BOOL present = FALSE;
        BOOL defaulted = FALSE;
        PACL acl = nullptr;
        if (GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) &&
            present) {
            ok = RegSetKeySecurity(key, DACL_SECURITY_INFORMATION,
                                   descriptor) == ERROR_SUCCESS;
        }
        LocalFree(descriptor);
    }

    RegCloseKey(key);
    return ok;
}

DriverRecord ReadDriverRecord()
{
    DriverRecord record;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kDiagnosticsKey, 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS)
        return record;

    wchar_t text[256] = {};
    DWORD size = sizeof(text);
    DWORD type = 0;
    if (RegQueryValueExW(key, L"LastStage", nullptr, &type,
                         reinterpret_cast<LPBYTE>(text), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        record.present = true;
        record.stage = text;
    }

    DWORD status = 0;
    size = sizeof(status);
    if (RegQueryValueExW(key, L"LastStatus", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&status), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        record.status = status;
    }

    size = sizeof(text);
    if (RegQueryValueExW(key, L"LastTime", nullptr, &type,
                         reinterpret_cast<LPBYTE>(text), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        record.time = text;
    }

    size = sizeof(text);
    if (RegQueryValueExW(key, L"CompiledSizes", nullptr, &type,
                         reinterpret_cast<LPBYTE>(text), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        record.compiledSizes = text;
    }

    RegCloseKey(key);
    return record;
}

Result InstallVirtualDisplay(const std::wstring& infPath, bool* rebootRequired)
{
    if (rebootRequired != nullptr)
        *rebootRequired = false;

    // Stage the package first. Without this the driver lives only in the folder
    // the user extracted, and a device created now would bind to a driver that
    // disappears the moment that folder is deleted.
    BOOL stagingNeedsReboot = FALSE;
    if (!DiInstallDriverW(nullptr, infPath.c_str(), DIIRFLAG_FORCE_INF,
                          &stagingNeedsReboot)) {
        const DWORD error = GetLastError();
        // Already in the store, with nothing newer to add. Not a failure.
        if (error != ERROR_NO_MORE_ITEMS) {
            return Result::Failure(
                L"could not add the driver to the driver store: " +
                DescribeError(error) +
                L"\n  This usually means the signature was rejected. Check that "
                L"the restart after enabling test signing actually happened.");
        }
    }
    if (stagingNeedsReboot && rebootRequired != nullptr)
        *rebootRequired = true;
    Line(L"  driver package staged in the driver store");

    const bool alreadyPresent = VirtualDisplayDevicePresent();
    SP_DEVINFO_DATA data = {};
    HDEVINFO set = INVALID_HANDLE_VALUE;

    if (!alreadyPresent) {
        set = SetupDiCreateDeviceInfoList(&kDisplayClass, nullptr);
        if (set == INVALID_HANDLE_VALUE) {
            return Result::Failure(L"could not start creating the device: " +
                                   DescribeError(GetLastError()));
        }

        data.cbSize = sizeof(data);
        if (!SetupDiCreateDeviceInfoW(set, L"Display", &kDisplayClass, nullptr,
                                      nullptr, DICD_GENERATE_ID, &data)) {
            const DWORD error = GetLastError();
            SetupDiDestroyDeviceInfoList(set);
            return Result::Failure(L"could not create the device node: " +
                                   DescribeError(error));
        }

        std::vector<wchar_t> hardwareId = HardwareIdMultiSz();
        if (!SetupDiSetDeviceRegistryPropertyW(
                set, &data, SPDRP_HARDWAREID,
                reinterpret_cast<const BYTE*>(hardwareId.data()),
                static_cast<DWORD>(hardwareId.size() * sizeof(wchar_t)))) {
            const DWORD error = GetLastError();
            SetupDiDestroyDeviceInfoList(set);
            return Result::Failure(L"could not set the device's hardware ID: " +
                                   DescribeError(error));
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &data)) {
            const DWORD error = GetLastError();
            SetupDiDestroyDeviceInfoList(set);
            return Result::Failure(L"could not register the device: " +
                                   DescribeError(error));
        }
        Line(L"  device node created");
    } else {
        Line(L"  device already exists; updating its driver");
    }

    BOOL bindNeedsReboot = FALSE;
    const BOOL bound = UpdateDriverForPlugAndPlayDevicesW(
        nullptr, kHardwareId, infPath.c_str(), INSTALLFLAG_FORCE,
        &bindNeedsReboot);
    const DWORD bindError = GetLastError();

    if (!bound && !alreadyPresent) {
        // Leaving a registered device with no driver behind would show up as a
        // broken entry in Device Manager and would make the next run take the
        // "already exists" path into the same failure.
        SetupDiCallClassInstaller(DIF_REMOVE, set, &data);
    }
    if (set != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(set);

    if (!bound) {
        return Result::Failure(
            L"the device was created but Windows would not bind the driver to "
            L"it: " + DescribeError(bindError));
    }
    if (bindNeedsReboot && rebootRequired != nullptr)
        *rebootRequired = true;

    Line(L"  driver bound to the device", Tone::Good);
    return Result::Success();
}

Result RemoveVirtualDisplay(const std::wstring& infPath, bool* rebootRequired)
{
    if (rebootRequired != nullptr)
        *rebootRequired = false;

    HDEVINFO set = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr,
                                        DIGCF_PRESENT);
    if (set != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA data = {};
        data.cbSize = sizeof(data);
        int removed = 0;
        // Enumeration indices shift as devices are removed, so this restarts
        // the scan after each removal rather than continuing through it.
        bool again = true;
        while (again) {
            again = false;
            data.cbSize = sizeof(data);
            for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
                if (!DeviceHasOurHardwareId(set, &data))
                    continue;
                if (SetupDiCallClassInstaller(DIF_REMOVE, set, &data)) {
                    ++removed;
                    again = true;
                }
                break;
            }
        }
        SetupDiDestroyDeviceInfoList(set);
        Line(L"  removed " + std::to_wstring(removed) + L" device(s)");
    }

    BOOL needsReboot = FALSE;
    if (!DiUninstallDriverW(nullptr, infPath.c_str(), 0, &needsReboot)) {
        const DWORD error = GetLastError();
        // Not in the store is the state we were trying to reach.
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_ITEMS) {
            return Result::Failure(
                L"could not remove the driver package: " + DescribeError(error));
        }
    }
    if (needsReboot && rebootRequired != nullptr)
        *rebootRequired = true;

    Line(L"  driver package removed from the driver store");
    return Result::Success();
}

}  // namespace visual4k
