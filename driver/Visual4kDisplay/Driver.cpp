#include "Driver.h"

#include <algorithm>
#include <cstring>

namespace visual4k {
namespace {

// The modes the virtual monitor advertises, preferred first.
//
// 3840x2160 is the point of the exercise: at a 1.50x linear ratio onto a
// 2560x1440 panel it is the largest mode that stays inside the memory and fill
// budget of a mid-range GPU while still carrying real sub-pixel detail.
// 5120x2880 (a 2.00x ratio) resolves measurably better -- see
// docs/ALGORITHMS.md -- but costs 4x the pixels, so it is offered, not default.
constexpr VirtualMode kModes[] = {
    {3840, 2160, 60},
    {5120, 2880, 60},
    {3200, 1800, 60},
    {2560, 1440, 60},   // 1:1 passthrough, for A/B comparison
};

constexpr VirtualMode kPreferredMode = kModes[0];

// D3DKMDT_VSS_OTHER. The constant is declared in d3dkmdt.h, which is a
// kernel-mode header this user-mode driver has no business including, and the
// field it feeds is a plain UINT32 bitfield.
constexpr UINT32 kVideoSignalStandardOther = 255;

NTSTATUS CreateMonitor(DeviceContext* ctx)
{
    IDDCX_MONITOR_INFO info = {};
    info.Size = sizeof(info);
    info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL;
    // Container ID groups the virtual monitor under one device in Settings.
    // Reusing a fixed GUID keeps the user's per-monitor arrangement stable
    // across reboots instead of scattering a new display every time.
    info.ConnectorIndex = 0;

    const std::vector<uint8_t> edid = BuildEdid(kPreferredMode.width,
                                              kPreferredMode.height,
                                              kPreferredMode.verticalSyncNumerator);
    info.MonitorDescription.Size = sizeof(info.MonitorDescription);
    info.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    info.MonitorDescription.DataSize = static_cast<UINT>(edid.size());
    info.MonitorDescription.pData = const_cast<uint8_t*>(edid.data());

    WDF_OBJECT_ATTRIBUTES monitorAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&monitorAttributes, ContextWrapper);

    IDARG_IN_MONITORCREATE createIn = {};
    createIn.ObjectAttributes = &monitorAttributes;
    createIn.pMonitorInfo = &info;

    IDARG_OUT_MONITORCREATE createOut = {};
    NTSTATUS status = IddCxMonitorCreate(ctx->adapter, &createIn, &createOut);
    if (!NT_SUCCESS(status))
        return status;

    ctx->monitor = createOut.MonitorObject;
    GetContextWrapper(createOut.MonitorObject)->device = ctx;

    IDARG_OUT_MONITORARRIVAL arrivalOut = {};
    return IddCxMonitorArrival(ctx->monitor, &arrivalOut);
}

// ---------------------------------------------------------------------------
// IddCx callbacks
// ---------------------------------------------------------------------------

NTSTATUS EvtAdapterInitFinished(IDDCX_ADAPTER adapter,
                                const IDARG_IN_ADAPTER_INIT_FINISHED* args)
{
    if (!NT_SUCCESS(args->AdapterInitStatus))
        return args->AdapterInitStatus;

    return CreateMonitor(GetContextWrapper(adapter)->device);
}

NTSTATUS EvtParseMonitorDescription(
    const IDARG_IN_PARSEMONITORDESCRIPTION* in,
    IDARG_OUT_PARSEMONITORDESCRIPTION* out)
{
    out->MonitorModeBufferOutputCount = ARRAYSIZE(kModes);

    // IddCx calls this once with a null buffer to ask for the count, then
    // again with a buffer to fill. Returning the wrong count on the first call
    // is the usual cause of a monitor that arrives with no supported modes.
    if (in->pMonitorModes == nullptr)
        return STATUS_SUCCESS;

    if (in->MonitorModeBufferInputCount < ARRAYSIZE(kModes))
        return STATUS_BUFFER_TOO_SMALL;

    for (DWORD i = 0; i < ARRAYSIZE(kModes); ++i) {
        auto& mode = in->pMonitorModes[i];
        mode.Size = sizeof(IDDCX_MONITOR_MODE);
        mode.Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER;
        mode.MonitorVideoSignalInfo.totalSize = {kModes[i].width, kModes[i].height};
        mode.MonitorVideoSignalInfo.activeSize =
            mode.MonitorVideoSignalInfo.totalSize;
        mode.MonitorVideoSignalInfo.vSyncFreq = {
            kModes[i].verticalSyncNumerator, 1};
        mode.MonitorVideoSignalInfo.hSyncFreq = {
            kModes[i].verticalSyncNumerator * kModes[i].height, 1};
        mode.MonitorVideoSignalInfo.pixelRate =
            static_cast<UINT64>(kModes[i].width) * kModes[i].height *
            kModes[i].verticalSyncNumerator;
        mode.MonitorVideoSignalInfo.scanLineOrdering =
            DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        mode.MonitorVideoSignalInfo.videoStandard = kVideoSignalStandardOther;
    }

    // The first entry is the one Windows picks by default.
    out->PreferredMonitorModeIdx = 0;
    return STATUS_SUCCESS;
}

NTSTATUS EvtMonitorGetDefaultModes(
    IDDCX_MONITOR /*monitor*/,
    const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* /*in*/,
    IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* out)
{
    // Only reached when the EDID failed to parse. Advertising nothing here is
    // correct: it makes an EDID bug loud instead of silently falling back to a
    // mode the compositor was not configured for.
    out->DefaultMonitorModeBufferOutputCount = 0;
    return STATUS_SUCCESS;
}

NTSTATUS EvtMonitorQueryTargetModes(
    IDDCX_MONITOR /*monitor*/,
    const IDARG_IN_QUERYTARGETMODES* in,
    IDARG_OUT_QUERYTARGETMODES* out)
{
    out->TargetModeBufferOutputCount = ARRAYSIZE(kModes);

    if (in->pTargetModes == nullptr)
        return STATUS_SUCCESS;

    if (in->TargetModeBufferInputCount < ARRAYSIZE(kModes))
        return STATUS_BUFFER_TOO_SMALL;

    for (DWORD i = 0; i < ARRAYSIZE(kModes); ++i) {
        auto& mode = in->pTargetModes[i];
        mode.Size = sizeof(IDDCX_TARGET_MODE);
        mode.TargetVideoSignalInfo.targetVideoSignalInfo.totalSize = {
            kModes[i].width, kModes[i].height};
        mode.TargetVideoSignalInfo.targetVideoSignalInfo.activeSize =
            mode.TargetVideoSignalInfo.targetVideoSignalInfo.totalSize;
        mode.TargetVideoSignalInfo.targetVideoSignalInfo.vSyncFreq = {
            kModes[i].verticalSyncNumerator, 1};
        mode.TargetVideoSignalInfo.targetVideoSignalInfo.hSyncFreq = {
            kModes[i].verticalSyncNumerator * kModes[i].height, 1};
        mode.TargetVideoSignalInfo.targetVideoSignalInfo.pixelRate =
            static_cast<UINT64>(kModes[i].width) * kModes[i].height *
            kModes[i].verticalSyncNumerator;
        mode.TargetVideoSignalInfo.targetVideoSignalInfo.scanLineOrdering =
            DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS EvtMonitorAssignSwapChain(IDDCX_MONITOR monitor,
                                   const IDARG_IN_SETSWAPCHAIN* in)
{
    auto* ctx = GetContextWrapper(monitor)->device;

    // Windows may reassign without unassigning first; dropping the old
    // processor here is what stops the two racing over the same swap chain.
    ctx->processor.reset();
    ctx->processor = std::make_unique<SwapChainProcessor>(
        in->hSwapChain, in->RenderAdapterLuid, in->hNextSurfaceAvailable);

    return STATUS_SUCCESS;
}

NTSTATUS EvtMonitorUnassignSwapChain(IDDCX_MONITOR monitor)
{
    GetContextWrapper(monitor)->device->processor.reset();
    return STATUS_SUCCESS;
}

NTSTATUS EvtAdapterCommitModes(IDDCX_ADAPTER /*adapter*/,
                               const IDARG_IN_COMMITMODES* /*in*/)
{
    // Nothing to reconfigure: the driver owns no scanout hardware, and the
    // host discovers geometry changes through Desktop Duplication's
    // ACCESS_LOST path.
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// WDF plumbing
// ---------------------------------------------------------------------------

NTSTATUS EvtDeviceD0Entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE /*previous*/)
{
    auto* ctx = GetDeviceContext(device);
    ctx->processor.reset();
    return STATUS_SUCCESS;
}

NTSTATUS EvtDevicePrepareHardware(WDFDEVICE device,
                                  WDFCMRESLIST /*raw*/, WDFCMRESLIST /*translated*/)
{
    auto* ctx = GetDeviceContext(device);

    IDDCX_ADAPTER_CAPS caps = {};
    caps.Size = sizeof(caps);
    caps.MaxMonitorsSupported = 1;
    caps.EndPointDiagnostics.Size = sizeof(caps.EndPointDiagnostics);
    caps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    caps.EndPointDiagnostics.TransmissionType =
        IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    caps.EndPointDiagnostics.pEndPointFriendlyName = L"Visual-4k Virtual Display";
    caps.EndPointDiagnostics.pEndPointManufacturerName = L"Visual-4k";
    caps.EndPointDiagnostics.pEndPointModelName = L"Supersampling Source";

    WDF_OBJECT_ATTRIBUTES adapterAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&adapterAttributes, ContextWrapper);

    IDARG_IN_ADAPTER_INIT init = {};
    init.WdfDevice = device;
    init.pCaps = &caps;
    init.ObjectAttributes = &adapterAttributes;

    IDARG_OUT_ADAPTER_INIT initOut = {};
    NTSTATUS status = IddCxAdapterInitAsync(&init, &initOut);
    if (!NT_SUCCESS(status))
        return status;

    ctx->adapter = initOut.AdapterObject;
    GetContextWrapper(initOut.AdapterObject)->device = ctx;
    return STATUS_SUCCESS;
}

void EvtDeviceContextCleanup(WDFOBJECT object)
{
    GetDeviceContext(static_cast<WDFDEVICE>(object))->~DeviceContext();
}

NTSTATUS EvtDeviceAdd(WDFDRIVER /*driver*/, PWDFDEVICE_INIT deviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS power;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
    power.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    power.EvtDeviceD0Entry = EvtDeviceD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &power);

    IDD_CX_CLIENT_CONFIG config;
    IDD_CX_CLIENT_CONFIG_INIT(&config);
    config.EvtIddCxAdapterInitFinished = EvtAdapterInitFinished;
    config.EvtIddCxParseMonitorDescription = EvtParseMonitorDescription;
    config.EvtIddCxMonitorGetDefaultDescriptionModes = EvtMonitorGetDefaultModes;
    config.EvtIddCxMonitorQueryTargetModes = EvtMonitorQueryTargetModes;
    config.EvtIddCxMonitorAssignSwapChain = EvtMonitorAssignSwapChain;
    config.EvtIddCxMonitorUnassignSwapChain = EvtMonitorUnassignSwapChain;
    config.EvtIddCxAdapterCommitModes = EvtAdapterCommitModes;

    NTSTATUS status = IddCxDeviceInitConfig(deviceInit, &config);
    if (!NT_SUCCESS(status))
        return status;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DeviceContext);
    // The context holds a std::unique_ptr, so its constructor and destructor
    // have to actually run -- WDF only zeroes the memory.
    attributes.EvtCleanupCallback = EvtDeviceContextCleanup;

    WDFDEVICE device = nullptr;
    status = WdfDeviceCreate(&deviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
        return status;

    new (GetDeviceContext(device)) DeviceContext{};

    status = IddCxDeviceInitialize(device);
    if (!NT_SUCCESS(status))
        return status;

    return STATUS_SUCCESS;
}

}  // namespace
}  // namespace visual4k

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,
                                PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, visual4k::EvtDeviceAdd);
    config.DriverPoolTag = 'k4sV';

    return WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES,
                           &config, WDF_NO_HANDLE);
}
