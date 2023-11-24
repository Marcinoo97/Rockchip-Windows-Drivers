#include "precomp.h"
#include "adapter.h"

struct AdapterContext
{
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AdapterContext, AdapterGetContext)

static EVT_NET_ADAPTER_CREATE_TXQUEUE AdapterCreateTxQueue;
__declspec(code_seg("PAGE"))
NTSTATUS
AdapterCreateTxQueue(
    _In_ NETADAPTER adapter,
    _Inout_ NETTXQUEUE_INIT* queueInit)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(queueInit);
    return STATUS_NOT_IMPLEMENTED;
}

static EVT_NET_ADAPTER_CREATE_RXQUEUE AdapterCreateRxQueue;
__declspec(code_seg("PAGE"))
NTSTATUS
AdapterCreateRxQueue(
    _In_ NETADAPTER adapter,
    _Inout_ NETRXQUEUE_INIT* queueInit)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(queueInit);
    return STATUS_NOT_IMPLEMENTED;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterPrepareHardware(
    _In_ NETADAPTER adapter,
    _In_ WDFCMRESLIST resourcesRaw,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(resourcesRaw);
    UNREFERENCED_PARAMETER(resourcesTranslated);
    return STATUS_NOT_IMPLEMENTED;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterReleaseHardware(
    _In_ NETADAPTER adapter,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(resourcesTranslated);
    return STATUS_NOT_IMPLEMENTED;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterD0Entry(
    _In_ NETADAPTER adapter,
    _In_ WDF_POWER_DEVICE_STATE targetState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(targetState);
    return STATUS_NOT_IMPLEMENTED;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterD0Exit(
    _In_ NETADAPTER adapter,
    _In_ WDF_POWER_DEVICE_STATE targetState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(targetState);
    return STATUS_NOT_IMPLEMENTED;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterAdd(
    _Inout_ WDFDEVICE device,
    _Out_ NETADAPTER* pAdapter)
{
    NTSTATUS status;
    NETADAPTER adapter = nullptr;

    auto const adapterInit = NetAdapterInitAllocate(device);
    if (adapterInit == nullptr)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    NET_ADAPTER_DATAPATH_CALLBACKS datapathCallbacks;
    NET_ADAPTER_DATAPATH_CALLBACKS_INIT(&datapathCallbacks, &AdapterCreateTxQueue, &AdapterCreateRxQueue);
    NetAdapterInitSetDatapathCallbacks(adapterInit, &datapathCallbacks);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, AdapterContext);

    status = NetAdapterCreate(adapterInit, &attributes, &adapter);
    NetAdapterInitFree(adapterInit);
    if (!NT_SUCCESS(status))
    {
        goto Done;
    }

Done:

    *pAdapter = adapter;
    return status;
}
