#include "precomp.h"
#include <bcrypt.h>
#include "adapter.h"
#include "registers.h"
#include "trace.h"

struct AdapterContext
{
    Registers* r;
    WDFDEVICE device;
    WDFINTERRUPT interrupt;
    WDFDMAENABLER dma;
    MacHwFeature0_t feature0;
    MacHwFeature1_t feature1;
    MacHwFeature2_t feature2;
    MacHwFeature3_t feature3;
    UINT8 permanentMacAddress[ETHERNET_LENGTH_OF_ADDRESS];
    UINT8 currentMacAddress[ETHERNET_LENGTH_OF_ADDRESS];
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AdapterContext, AdapterGetContext)

struct InterruptContext
{
    AdapterContext* adapterContext;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(InterruptContext, InterruptGetContext)

template<class T>
T FORCEINLINE
Read32(T const& reg)
{
    static_assert(sizeof(T) == sizeof(ULONG));
    auto const val = READ_REGISTER_ULONG((ULONG*)&reg);
    return reinterpret_cast<T const&>(val);
}

static EVT_WDF_INTERRUPT_ISR AdapterInterruptIsr;
static BOOLEAN
AdapterInterruptIsr(
    _In_ WDFINTERRUPT interrupt,
    _In_ ULONG messageId)
{
    // TODO
    UNREFERENCED_PARAMETER(interrupt);
    UNREFERENCED_PARAMETER(messageId);
    return true;
}

static EVT_WDF_INTERRUPT_DPC AdapterInterruptDpc;
static void
AdapterInterruptDpc(
    _In_ WDFINTERRUPT interrupt,
    _In_ WDFOBJECT associatedObject)
{
    // TODO
    auto const context = InterruptGetContext(interrupt)->adapterContext;
    NT_ASSERT(context->device == associatedObject);
    NT_ASSERT(context->interrupt == interrupt);
}

static EVT_NET_ADAPTER_CREATE_TXQUEUE AdapterCreateTxQueue;
__declspec(code_seg("PAGE"))
static NTSTATUS
AdapterCreateTxQueue(
    _In_ NETADAPTER adapter,
    _Inout_ NETTXQUEUE_INIT* queueInit)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(queueInit);

    // TODO
    NTSTATUS status = STATUS_NOT_IMPLEMENTED;
    TraceExitWithStatus(AdapterCreateTxQueue, status);
    return status;
}

static EVT_NET_ADAPTER_CREATE_RXQUEUE AdapterCreateRxQueue;
__declspec(code_seg("PAGE"))
static NTSTATUS
AdapterCreateRxQueue(
    _In_ NETADAPTER adapter,
    _Inout_ NETRXQUEUE_INIT* queueInit)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(queueInit);

    // TODO
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

    NTSTATUS status;
    Registers* r = nullptr;
    auto const context = AdapterGetContext(adapter);
    bool configHasMacAddress = false;

    // Read configuration

    {
        NETCONFIGURATION configuration;
        status = NetAdapterOpenConfiguration(adapter, WDF_NO_OBJECT_ATTRIBUTES, &configuration);
        if (!NT_SUCCESS(status))
        {
            TraceWrite("NetAdapterOpenConfiguration-failed", LEVEL_ERROR,
                TraceLoggingNTStatus(status));
            goto Done;
        }

        NET_ADAPTER_LINK_LAYER_ADDRESS configAddress;
        status = NetConfigurationQueryLinkLayerAddress(configuration, &configAddress);
        if (!NT_SUCCESS(status))
        {
            TraceWrite("QueryLinkLayerAddress-not-found", LEVEL_INFO,
                TraceLoggingNTStatus(status));
        }
        else if (configAddress.Length != ETHERNET_LENGTH_OF_ADDRESS)
        {
            TraceWrite("QueryLinkLayerAddress-bad-length", LEVEL_WARNING,
                TraceLoggingHexInt16(configAddress.Length, "Length"));
        }
        else if (
            ETH_IS_MULTICAST(configAddress.Address) ||
            ETH_IS_BROADCAST(configAddress.Address))
        {
            TraceWrite("QueryLinkLayerAddress-bad-address", LEVEL_WARNING,
                TraceLoggingBinary(configAddress.Address, ETHERNET_LENGTH_OF_ADDRESS, "address"));
        }
        else
        {
            TraceWrite("QueryLinkLayerAddress-found", LEVEL_INFO,
                TraceLoggingBinary(configAddress.Address, ETHERNET_LENGTH_OF_ADDRESS, "address"));
            memcpy(context->currentMacAddress, configAddress.Address, sizeof(context->currentMacAddress));
            configHasMacAddress = true;
        }
    }

    // Map registers

    {
        ULONG const resourcesCount = WdfCmResourceListGetCount(resourcesTranslated);
        for (ULONG i = 0; i != resourcesCount; i += 1)
        {
            auto desc = WdfCmResourceListGetDescriptor(resourcesTranslated, i);
            if (desc->Type == CmResourceTypeMemory && desc->u.Memory.Length >= sizeof(*r))
            {
                context->r = static_cast<Registers*>(
                    MmMapIoSpaceEx(desc->u.Memory.Start, sizeof(*context->r), PAGE_READWRITE | PAGE_NOCACHE));
                if (context->r == nullptr)
                {
                    TraceWrite("MmMapIoSpaceEx-failed", LEVEL_ERROR);
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Done;
                }

                break;
            }
        }
    }

    if (context->r == nullptr)
    {
        TraceWrite("CmResourceTypeMemory-not-found", LEVEL_ERROR);
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }

    r = context->r;

    // Set MAC address

    {
        auto const mac0hi = Read32(r->MacAddress[0].High).High16;
        auto const mac0lo = Read32(r->MacAddress[0].Low32);
        context->permanentMacAddress[0] = static_cast<UINT8>(mac0lo);
        context->permanentMacAddress[1] = static_cast<UINT8>(mac0lo >> 8);
        context->permanentMacAddress[2] = static_cast<UINT8>(mac0lo >> 16);
        context->permanentMacAddress[3] = static_cast<UINT8>(mac0lo >> 24);
        context->permanentMacAddress[4] = static_cast<UINT8>(mac0hi);
        context->permanentMacAddress[5] = static_cast<UINT8>(mac0hi >> 8);

        if (!configHasMacAddress)
        {
            memcpy(context->currentMacAddress, context->permanentMacAddress, sizeof(context->currentMacAddress));
            if (mac0lo == 0xFFFFFFFF && mac0hi == 0xFFFF)
            {
                TraceWrite("MacAddress0-unconfigured", LEVEL_WARNING,
                    TraceLoggingBinary(context->currentMacAddress, sizeof(context->currentMacAddress), "Mac0"));
                context->currentMacAddress[0] = 0xF2;
                context->currentMacAddress[1] = 0x00;
                BCryptGenRandom(
                    nullptr,
                    context->currentMacAddress + 2,
                    sizeof(context->currentMacAddress) - 2,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            }
            else if (context->currentMacAddress[0] & 1u)
            {
                TraceWrite("MacAddress0-multicast", LEVEL_WARNING,
                    TraceLoggingBinary(context->currentMacAddress, sizeof(context->currentMacAddress), "Mac0"));
                context->currentMacAddress[0] &= ~1u;
            }
        }

        NET_ADAPTER_LINK_LAYER_ADDRESS address;
        NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&address, sizeof(context->permanentMacAddress), context->permanentMacAddress);
        NetAdapterSetPermanentLinkLayerAddress(adapter, &address);
        NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&address, sizeof(context->currentMacAddress), context->currentMacAddress);
        NetAdapterSetCurrentLinkLayerAddress(adapter, &address);
    }

    // Read features

    {
        auto const version = Read32(r->MacVersion);
        context->feature0 = Read32(r->MacHwFeature0);
        context->feature1 = Read32(r->MacHwFeature1);
        context->feature2 = Read32(r->MacHwFeature2);
        context->feature3 = Read32(r->MacHwFeature3);
        TraceWrite("Config", LEVEL_INFO,
            TraceLoggingHexInt32(version.RkVer, "RkVer"),
            TraceLoggingHexInt32(version.UserVer, "UserVer"),
            TraceLoggingHexInt32(context->feature0.Value32, "HwFeature0"),
            TraceLoggingHexInt32(context->feature1.Value32, "HwFeature1"),
            TraceLoggingHexInt32(context->feature2.Value32, "HwFeature2"),
            TraceLoggingHexInt32(context->feature3.Value32, "HwFeature3"),
            TraceLoggingBinary(context->permanentMacAddress, sizeof(context->permanentMacAddress), "PermanentAddr"),
            TraceLoggingBinary(context->currentMacAddress, sizeof(context->currentMacAddress), "CurrentAddr"));

        if (version.RkVer < 0x51 || version.UserVer > 0x52)
        {
            TraceWrite("RkVer-not-supported", LEVEL_ERROR,
                TraceLoggingHexInt32(version.RkVer, "RkVer"));
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Done;
        }
    }

    // Create DMA enabler

    {
        WDF_DMA_ENABLER_CONFIG config;
        WDF_DMA_ENABLER_CONFIG_INIT(&config, WdfDmaProfilePacket, 0x3FFF);
        status = WdfDmaEnablerCreate(context->device, &config, WDF_NO_OBJECT_ATTRIBUTES, &context->dma);
        if (!NT_SUCCESS(status))
        {
            TraceWrite("WdfDmaEnablerCreate-failed", LEVEL_ERROR,
                TraceLoggingNTStatus(status));
            goto Done;
        }
    }

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

    // Create adapter

    {
        auto const adapterInit = NetAdapterInitAllocate(device);
        if (adapterInit == nullptr)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }

        {
            NET_ADAPTER_DATAPATH_CALLBACKS callbacks;
            NET_ADAPTER_DATAPATH_CALLBACKS_INIT(&callbacks, &AdapterCreateTxQueue, &AdapterCreateRxQueue);
            NetAdapterInitSetDatapathCallbacks(adapterInit, &callbacks);
        }

        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, AdapterContext);

        status = NetAdapterCreate(adapterInit, &attributes, &adapter);
        NetAdapterInitFree(adapterInit);
        if (!NT_SUCCESS(status))
        {
            goto Done;
        }
    }

    AdapterContext* context;
    context = AdapterGetContext(adapter);
    NT_ASSERT(context->r == nullptr);
    context->device = device;

    // Create interrupt

    {
        WDF_INTERRUPT_CONFIG config;
        WDF_INTERRUPT_CONFIG_INIT(&config, AdapterInterruptIsr, AdapterInterruptDpc);

        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, InterruptContext);

        status = WdfInterruptCreate(device, &config, &attributes, &context->interrupt);
        if (!NT_SUCCESS(status))
        {
            goto Done;
        }

        auto const interruptContext = InterruptGetContext(context->interrupt);
        interruptContext->adapterContext = context;
    }

Done:

    TraceExitWithStatus(AdapterAdd, status);
    *pAdapter = adapter;
    return status;
}
