#include "precomp.h"
#include "device.h"
#include "adapter.h"
#include "trace.h"

struct DeviceContext
{
    NETADAPTER Adapter;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceContext, DeviceGetContext)

static EVT_WDF_DEVICE_PREPARE_HARDWARE DevicePrepareHardware;
__declspec(code_seg("PAGE"))
NTSTATUS
DevicePrepareHardware(
    _In_ WDFDEVICE device,
    _In_ WDFCMRESLIST resourcesRaw,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    PAGED_CODE();
    return AdapterPrepareHardware(DeviceGetContext(device)->Adapter, resourcesRaw, resourcesTranslated);
}

static EVT_WDF_DEVICE_RELEASE_HARDWARE DeviceReleaseHardware;
__declspec(code_seg("PAGE"))
NTSTATUS
DeviceReleaseHardware(
    _In_ WDFDEVICE device,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    PAGED_CODE();
    return AdapterReleaseHardware(DeviceGetContext(device)->Adapter, resourcesTranslated);
}

static EVT_WDF_DEVICE_D0_ENTRY DeviceD0Entry;
__declspec(code_seg("PAGE"))
NTSTATUS
DeviceD0Entry(
    _In_ WDFDEVICE device,
    _In_ WDF_POWER_DEVICE_STATE targetState)
{
    PAGED_CODE();
    return AdapterD0Entry(DeviceGetContext(device)->Adapter, targetState);
}

static EVT_WDF_DEVICE_D0_ENTRY DeviceD0Exit;
__declspec(code_seg("PAGE"))
NTSTATUS
DeviceD0Exit(
    _In_ WDFDEVICE device,
    _In_ WDF_POWER_DEVICE_STATE targetState)
{
    PAGED_CODE();
    return AdapterD0Exit(DeviceGetContext(device)->Adapter, targetState);
}

__declspec(code_seg("PAGE"))
NTSTATUS
DeviceAdd(_Inout_ WDFDEVICE_INIT* deviceInit)
{
    PAGED_CODE();

    NTSTATUS status;
    TraceEntry(DeviceAdd);

    // Configure deviceInit

    status = NetDeviceInitConfig(deviceInit);
    if (!NT_SUCCESS(status))
    {
        goto Done;
    }

    {
        WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

        pnpPowerCallbacks.EvtDevicePrepareHardware = &DevicePrepareHardware;
        pnpPowerCallbacks.EvtDeviceReleaseHardware = &DeviceReleaseHardware;
        pnpPowerCallbacks.EvtDeviceD0Entry = &DeviceD0Entry;
        pnpPowerCallbacks.EvtDeviceD0Exit = &DeviceD0Exit;

        WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnpPowerCallbacks);
    }

    // Create device.

    WDFDEVICE device;
    {
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DeviceContext);

        status = WdfDeviceCreate(&deviceInit, &attributes, &device);
        if (!NT_SUCCESS(status))
        {
            goto Done;
        }

        WdfDeviceSetAlignmentRequirement(device, FILE_BYTE_ALIGNMENT);
    }

    // Create adapter.

    status = AdapterAdd(device, &DeviceGetContext(device)->Adapter);

Done:

    TraceExitWithStatus(DeviceAdd, status);
    return status;
}
