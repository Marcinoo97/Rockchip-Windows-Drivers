#include "precomp.h"
#include "adapter.h"
#include "registers.h"
#include "trace.h"

template<class T>
T FORCEINLINE
Read32(T const& reg)
{
    static_assert(sizeof(T) == sizeof(ULONG));
    auto const val = READ_REGISTER_ULONG((ULONG*)&reg);
    return reinterpret_cast<T const&>(val);
}

struct AdapterContext
{
    WDFDEVICE device;
    Registers* r;
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

    NTSTATUS status = STATUS_NOT_IMPLEMENTED;
    TraceExitWithStatus(AdapterCreateTxQueue, status);
    return status;
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

    NTSTATUS status = STATUS_NOT_IMPLEMENTED;
    TraceExitWithStatus(AdapterCreateRxQueue, status);
    return status;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterPrepareHardware(
    _In_ NETADAPTER adapter,
    _In_ WDFCMRESLIST resourcesRaw,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(resourcesRaw);
    UNREFERENCED_PARAMETER(resourcesTranslated);

    NTSTATUS status;
    Registers* r = nullptr;
    auto const context = AdapterGetContext(adapter);

    ULONG const resourcesCount = WdfCmResourceListGetCount(resourcesTranslated);
    for (ULONG i = 0; i != resourcesCount; i += 1)
    {
        auto desc = WdfCmResourceListGetDescriptor(resourcesTranslated, i);
        if (desc->Type == CmResourceTypeMemory && desc->u.Memory.Length >= sizeof(*r))
        {
            r = static_cast<Registers*>(MmMapIoSpaceEx(desc->u.Memory.Start, sizeof(*r), PAGE_READWRITE | PAGE_NOCACHE));
            context->r = r;
            if (r == nullptr)
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }

            break;
        }
    }

    if (r == nullptr)
    {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }

    TraceWrite("Config", LEVEL_INFO,
        TraceLoggingHexInt32(Read32(r->MacConfiguration), "Configuration"),
        TraceLoggingHexInt32(Read32(r->MacExtConfiguration), "ExtConfiguration"),
        TraceLoggingHexInt32(Read32(r->MacVersion), "Version"),
        TraceLoggingHexInt32(Read32(r->MacDebug), "Debug"),
        TraceLoggingHexInt32(Read32(r->MacHwFeature0), "HwFeature0"),
        TraceLoggingHexInt32(Read32(r->MacHwFeature1), "HwFeature1"),
        TraceLoggingHexInt32(Read32(r->MacHwFeature2), "HwFeature2"),
        TraceLoggingHexInt32(Read32(r->MacHwFeature3), "HwFeature3"));

    for (unsigned i = 0; i != ARRAYSIZE(r->MacAddress); i += 1)
    {
        TraceWrite("MacAddress", LEVEL_INFO,
            TraceLoggingHexInt32(Read32(r->MacAddress[i].High16), "MacHi"),
            TraceLoggingHexInt32(Read32(r->MacAddress[i].Low32), "MacLo"));
    };

    status = STATUS_NOT_IMPLEMENTED;

Done:

    TraceExitWithStatus(AdapterPrepareHardware, status);
    return status;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterReleaseHardware(
    _In_ NETADAPTER adapter,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(resourcesTranslated);

    auto const context = AdapterGetContext(adapter);
    if (context->r != nullptr)
    {
        MmUnmapIoSpace(context->r, sizeof(*context->r));
        context->r = nullptr;
    }

    TraceExit(AdapterReleaseHardware);
    return STATUS_SUCCESS;
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

    NTSTATUS status = STATUS_NOT_IMPLEMENTED;
    TraceExitWithStatus(AdapterD0Entry, status);
    return status;
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

    NTSTATUS status = STATUS_NOT_IMPLEMENTED;
    TraceExitWithStatus(AdapterD0Exit, status);
    return status;
}

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterAdd(
    _Inout_ WDFDEVICE device,
    _Out_ NETADAPTER* pAdapter)
{
    NTSTATUS status;
    NETADAPTER adapter = nullptr;

    // Configure adapterInit

    auto const adapterInit = NetAdapterInitAllocate(device);
    if (adapterInit == nullptr)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    {
        NET_ADAPTER_DATAPATH_CALLBACKS datapathCallbacks;
        NET_ADAPTER_DATAPATH_CALLBACKS_INIT(&datapathCallbacks, &AdapterCreateTxQueue, &AdapterCreateRxQueue);
        NetAdapterInitSetDatapathCallbacks(adapterInit, &datapathCallbacks);
    }

    // Create adapter

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, AdapterContext);

    status = NetAdapterCreate(adapterInit, &attributes, &adapter);
    NetAdapterInitFree(adapterInit);
    if (!NT_SUCCESS(status))
    {
        goto Done;
    }

    // Create adapter resources

    AdapterContext* context;
    context = AdapterGetContext(adapter);
    context->device = device;

    // TODO: Interrupt
    // TODO: DMA

Done:

    TraceExitWithStatus(AdapterAdd, status);
    *pAdapter = adapter;
    return status;
}
