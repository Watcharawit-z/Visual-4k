#include "Driver.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>

#include "Diagnostics.h"

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

// Container ID for the virtual monitor.
//
// This groups the monitor under one device in Settings, and reusing a fixed
// value keeps the user's per-monitor arrangement stable across reboots rather
// than scattering a new display every time. Windows treats it as a real
// identity, so it has to be set: the code that this comment used to sit above
// described exactly this and then never assigned the field, leaving it as a
// null GUID.
constexpr GUID kMonitorContainerId = {
    0x9b2e5a41, 0x7c3d, 0x4f18,
    {0xa5, 0x6b, 0x2d, 0x8e, 0x11, 0x4c, 0x9f, 0x03}};

NTSTATUS CreateMonitor(DeviceContext* ctx)
{
    IDDCX_MONITOR_INFO info = {};
    info.Size = sizeof(info);
    info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL;
    info.ConnectorIndex = 0;
    info.MonitorContainerId = kMonitorContainerId;

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
    RecordStage(L"IddCxMonitorCreate", status);
    if (!NT_SUCCESS(status))
        return status;

    ctx->monitor = createOut.MonitorObject;
    GetContextWrapper(createOut.MonitorObject)->device = ctx;

    IDARG_OUT_MONITORARRIVAL arrivalOut = {};
    status = IddCxMonitorArrival(ctx->monitor, &arrivalOut);
    RecordStage(L"IddCxMonitorArrival", status);
    return status;
}

// ---------------------------------------------------------------------------
// IddCx callbacks
// ---------------------------------------------------------------------------

NTSTATUS EvtAdapterInitFinished(IDDCX_ADAPTER adapter,
                                const IDARG_IN_ADAPTER_INIT_FINISHED* args)
{
    RecordStage(L"EvtAdapterInitFinished entered", args->AdapterInitStatus);
    if (!NT_SUCCESS(args->AdapterInitStatus))
        return args->AdapterInitStatus;

    const NTSTATUS status = CreateMonitor(GetContextWrapper(adapter)->device);
    RecordStage(L"EvtAdapterInitFinished complete", status);
    return status;
}

NTSTATUS EvtParseMonitorDescription(
    const IDARG_IN_PARSEMONITORDESCRIPTION* in,
    IDARG_OUT_PARSEMONITORDESCRIPTION* out)
{
    RecordStage(L"EvtParseMonitorDescription", STATUS_SUCCESS);
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

// Brings the adapter up.
//
// This runs from D0Entry rather than PrepareHardware because that is where
// Microsoft's indirect display sample does it, and this driver was written
// from memory of that sample rather than from the sample. Reading the actual
// source turned up three differences, all of them here or next door, and every
// one of them a plausible cause of the adapter init being refused.
NTSTATUS InitAdapter(DeviceContext* ctx, WDFDEVICE device)
{
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

    // These two were left null, and null is not something the class extension
    // accepts here. That is very likely the whole of what
    // IddCxAdapterInitAsync was refusing with STATUS_INVALID_PARAMETER: the
    // call names no parameter, and a missing required pointer looks from
    // outside exactly like a wrong structure size, which is what the last few
    // attempts went hunting for instead.
    IDDCX_ENDPOINT_VERSION version = {};
    version.Size = sizeof(version);
    version.MajorVer = 1;
    caps.EndPointDiagnostics.pFirmwareVersion = &version;
    caps.EndPointDiagnostics.pHardwareVersion = &version;

    WDF_OBJECT_ATTRIBUTES adapterAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&adapterAttributes, ContextWrapper);

    IDARG_IN_ADAPTER_INIT init = {};
    init.WdfDevice = device;
    init.pCaps = &caps;
    init.ObjectAttributes = &adapterAttributes;

    IDARG_OUT_ADAPTER_INIT initOut = {};
    const NTSTATUS status = IddCxAdapterInitAsync(&init, &initOut);
    RecordStage(L"IddCxAdapterInitAsync", status);
    if (!NT_SUCCESS(status))
        return status;

    ctx->adapter = initOut.AdapterObject;
    GetContextWrapper(initOut.AdapterObject)->device = ctx;
    return STATUS_SUCCESS;
}

NTSTATUS EvtDeviceD0Entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE /*previous*/)
{
    auto* ctx = GetDeviceContext(device);
    ctx->processor.reset();

    const NTSTATUS status = InitAdapter(ctx, device);
    RecordStage(L"EvtDeviceD0Entry", status);
    return status;
}

void EvtDeviceContextCleanup(WDFOBJECT object)
{
    GetDeviceContext(static_cast<WDFDEVICE>(object))->~DeviceContext();
}

NTSTATUS EvtDeviceAdd(WDFDRIVER /*driver*/, PWDFDEVICE_INIT deviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS power;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
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

    // Each step records its outcome. Windows reports a failed EvtDeviceAdd as
    // problem code 31 and nothing else, which names the callback but not the
    // call inside it, and there are three candidates. Two rounds were spent
    // guessing at that from the outside before this was added.
    NTSTATUS status = IddCxDeviceInitConfig(deviceInit, &config);
    RecordStage(L"IddCxDeviceInitConfig", status);
    if (!NT_SUCCESS(status))
        return status;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DeviceContext);
    // The context holds a std::unique_ptr, so its constructor and destructor
    // have to actually run -- WDF only zeroes the memory.
    attributes.EvtCleanupCallback = EvtDeviceContextCleanup;

    WDFDEVICE device = nullptr;
    status = WdfDeviceCreate(&deviceInit, &attributes, &device);
    RecordStage(L"WdfDeviceCreate", status);
    if (!NT_SUCCESS(status))
        return status;

    new (GetDeviceContext(device)) DeviceContext{};

    status = IddCxDeviceInitialize(device);
    RecordStage(L"IddCxDeviceInitialize", status);
    if (!NT_SUCCESS(status))
        return status;

    RecordStage(L"EvtDeviceAdd complete", STATUS_SUCCESS);
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
