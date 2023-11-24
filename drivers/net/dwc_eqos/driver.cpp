#include "precomp.h"
#include "driver.h"
#include "device.h"

TRACELOGGING_DEFINE_PROVIDER(
    TraceProvider,
    "dwc_eqos",
    // *dwc_eqos = {5d8331d3-70b3-5620-5664-db28f48a4b79}
    (0x5d8331d3, 0x70b3, 0x5620, 0x56, 0x64, 0xdb, 0x28, 0xf4, 0x8a, 0x4b, 0x79));

static EVT_WDF_DRIVER_DEVICE_ADD DriverDeviceAdd;
__declspec(code_seg("PAGE"))
NTSTATUS
DriverDeviceAdd(
    _In_ WDFDRIVER,
    _Inout_ WDFDEVICE_INIT* deviceInit)
{
    PAGED_CODE();
    return DeviceAdd(deviceInit);
}

static EVT_WDF_DRIVER_UNLOAD DriverUnload;
__declspec(code_seg("PAGE"))
void
DriverUnload(_In_ WDFDRIVER)
{
    PAGED_CODE();
    TraceLoggingWrite(TraceProvider, "DriverUnload");
    TraceLoggingUnregister(TraceProvider);
}

extern "C" DRIVER_INITIALIZE DriverEntry;
__declspec(code_seg("INIT"))
NTSTATUS
DriverEntry(
    _In_ DRIVER_OBJECT* driverObject,
    _In_ UNICODE_STRING* registryPath)
{
    NTSTATUS status;

    TraceLoggingRegister(TraceProvider);

    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, DriverDeviceAdd);
    config.EvtDriverUnload = DriverUnload;
    config.DriverPoolTag = 'dwcE';

    status = WdfDriverCreate(
        driverObject,
        registryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);

    TraceLoggingWrite(TraceProvider, "DriverEntry",
        TraceLoggingNTStatus(status),
        TraceLoggingUnicodeString(registryPath));

    if (!NT_SUCCESS(status))
    {
        TraceLoggingUnregister(TraceProvider);
    }

    return status;
}
