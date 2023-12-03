#include "precomp.h"
#include <bcrypt.h>
#include "adapter.h"
#include "registers.h"
#include "trace.h"

/*
Lifecycle:

PrepareHardware     ReleaseHardware
D0Entry             D0Exit
(InterruptEnable)   (InterruptDisable)
CreateRxQueue       DestroyCallback
CreateTxQueue       DestroyCallback
(PacketQueueStart)  (PacketQueueCancel, PacketQueueStop)
(DisarmWake)        (ArmWake)
*/

static auto constexpr DefaultAxiMaxWriteOutstanding = 4u;
static auto constexpr DefaultAxiMaxReadOutstanding = 8u;
static auto constexpr DefaultCsrRate = 125'000'000u;
static auto constexpr BusBytes = 8u;

struct AdapterContext
{
    MacRegisters* regs;
    WDFDEVICE device;
    WDFINTERRUPT interrupt;
    WDFDMAENABLER dma;
    MacHwFeature0_t feature0;
    MacHwFeature1_t feature1;
    MacHwFeature2_t feature2;
    MacHwFeature3_t feature3;
    UINT8 permanentMacAddress[ETHERNET_LENGTH_OF_ADDRESS];
    UINT8 currentMacAddress[ETHERNET_LENGTH_OF_ADDRESS];
    //NET_PACKET_FILTER_FLAGS receiveFilterFlags; // TODO: Packet filter.
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AdapterContext, AdapterGetContext)

struct InterruptContext
{
    AdapterContext* adapterContext;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(InterruptContext, InterruptGetContext)

template<class T>
struct TypeIdentity { using type = T; };

template<class T>
T FORCEINLINE
Read32(T const& reg)
{
    static_assert(sizeof(T) == sizeof(ULONG));
    auto const val = READ_REGISTER_ULONG((ULONG*)&reg);
    return reinterpret_cast<T const&>(val);
}

template<class T>
void FORCEINLINE
Write32(_Out_ T* reg, typename TypeIdentity<T>::type val)
{
    static_assert(sizeof(T) == sizeof(ULONG));
#pragma warning(suppress: 6001) // WRITE_REGISTER_ULONG is not annotated correctly.
    WRITE_REGISTER_ULONG(reinterpret_cast<ULONG*>(reg), reinterpret_cast<ULONG const&>(val));
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

    NTSTATUS status = STATUS_NOT_IMPLEMENTED; // TODO
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

    NTSTATUS status = STATUS_NOT_IMPLEMENTED; // TODO
    TraceExitWithStatus(AdapterCreateRxQueue, status);
    return status;
}

__declspec(code_seg("PAGE"))
static void
ApplyReceiveFilter(_In_ AdapterContext const* context)
{
    PAGED_CODE();
    
    MacPacketFilter_t filter = {};
    filter.ReceiveAll = true;
    Write32(&context->regs->MacPacketFilter, filter);
}

EVT_WDF_DEVICE_D0_ENTRY AdapterD0Entry;
__declspec(code_seg("PAGE"))
NTSTATUS
AdapterD0Entry(
    _In_ NETADAPTER adapter,
    _In_ WDF_POWER_DEVICE_STATE previousState)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_NOT_IMPLEMENTED; // TODO
    auto const context = AdapterGetContext(adapter);
    ApplyReceiveFilter(context);
    //GenetMacInitialize(adapter);
    //GenetSetMacAddressFilters(adapter);
    //GenetPhyInitialize(adapter);
    //GenetMacPhyConfigure(adapter);
    TraceExitWithStatus(AdapterD0Entry, status,
        TraceLoggingUInt32(previousState));
    return status;
}

EVT_WDF_DEVICE_D0_EXIT AdapterD0Exit;
__declspec(code_seg("PAGE"))
NTSTATUS
AdapterD0Exit(
    _In_ NETADAPTER adapter,
    _In_ WDF_POWER_DEVICE_STATE targetState)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    auto const context = AdapterGetContext(adapter);
    UNREFERENCED_PARAMETER(context);
    TraceExitWithStatus(AdapterD0Exit, status,
        TraceLoggingUInt32(targetState));
    return status;
}

EVT_WDF_DEVICE_PREPARE_HARDWARE AdapterPrepareHardware;
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
    PHYSICAL_ADDRESS maxPhysicalAddress;
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

    // Configure resources

    {
        NT_ASSERT(context->regs == nullptr);
        NT_ASSERT(context->interrupt == nullptr);

        ULONG const resourcesCount = WdfCmResourceListGetCount(resourcesTranslated);
        for (ULONG i = 0; i != resourcesCount; i += 1)
        {
            auto desc = WdfCmResourceListGetDescriptor(resourcesTranslated, i);
            switch (desc->Type)
            {
            case CmResourceTypeMemory:
                if (context->regs != nullptr)
                {
                    TraceWrite("AdapterPrepareHardware-memory-unexpected", LEVEL_WARNING,
                        TraceLoggingHexInt64(desc->u.Memory.Start.QuadPart, "start"));
                }
                else if (desc->u.Memory.Length < sizeof(*context->regs))
                {
                    TraceWrite("AdapterPrepareHardware-memory-small", LEVEL_WARNING,
                        TraceLoggingHexInt64(desc->u.Memory.Start.QuadPart, "start"),
                        TraceLoggingHexInt32(desc->u.Memory.Length, "length"));
                }
                else
                {
                    TraceWrite("AdapterPrepareHardware-memory", LEVEL_INFO,
                        TraceLoggingHexInt64(desc->u.Memory.Start.QuadPart, "start"),
                        TraceLoggingHexInt32(desc->u.Memory.Length, "length"));

                    context->regs = static_cast<MacRegisters*>(
                        MmMapIoSpaceEx(desc->u.Memory.Start, sizeof(*context->regs), PAGE_READWRITE | PAGE_NOCACHE));
                    if (context->regs == nullptr)
                    {
                        TraceWrite("MmMapIoSpaceEx-failed", LEVEL_ERROR);
                        status = STATUS_INSUFFICIENT_RESOURCES;
                        goto Done;
                    }
                }
                break;

            case CmResourceTypeInterrupt:
                if (context->interrupt != nullptr)
                {
                    TraceWrite("AdapterPrepareHardware-interrupt-unexpected", LEVEL_WARNING,
                        TraceLoggingHexInt32(desc->u.Interrupt.Vector, "vector"));
                }
                else
                {
                    TraceWrite("AdapterPrepareHardware-interrupt", LEVEL_INFO,
                        TraceLoggingHexInt32(desc->u.Interrupt.Vector, "vector"));

                    WDF_INTERRUPT_CONFIG config;
                    WDF_INTERRUPT_CONFIG_INIT(&config, AdapterInterruptIsr, AdapterInterruptDpc);

                    WDF_OBJECT_ATTRIBUTES attributes;
                    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, InterruptContext);

                    status = WdfInterruptCreate(context->device, &config, &attributes, &context->interrupt);
                    if (!NT_SUCCESS(status))
                    {
                        TraceWrite("WdfInterruptCreate-failed", LEVEL_ERROR,
                            TraceLoggingNTStatus(status));
                        goto Done;
                    }

                    auto const interruptContext = InterruptGetContext(context->interrupt);
                    interruptContext->adapterContext = context;
                }
                break;

            default:
                TraceWrite("AdapterPrepareHardware-resource-unexpected", LEVEL_WARNING,
                    TraceLoggingUInt8(desc->Type, "type"));
                break;
            }
        }
    }

    if (context->regs == nullptr)
    {
        TraceWrite("AdapterPrepareHardware-no-memory", LEVEL_ERROR);
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }

    if (context->interrupt == nullptr)
    {
        TraceWrite("AdapterPrepareHardware-no-interrupt", LEVEL_ERROR);
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }

    // Set MAC address

    {
        auto const mac0hi = Read32(context->regs->MacAddress[0].High);
        auto const mac0lo = Read32(context->regs->MacAddress[0].Low);
        context->permanentMacAddress[0] = mac0lo.Addr0;
        context->permanentMacAddress[1] = mac0lo.Addr1;
        context->permanentMacAddress[2] = mac0lo.Addr2;
        context->permanentMacAddress[3] = mac0lo.Addr3;
        context->permanentMacAddress[4] = mac0hi.Addr4;
        context->permanentMacAddress[5] = mac0hi.Addr5;

        if (!configHasMacAddress)
        {
            memcpy(context->currentMacAddress, context->permanentMacAddress, sizeof(context->currentMacAddress));
            if (mac0lo.Value32 == 0xFFFFFFFF && mac0hi.Addr4 == 0xFF && mac0hi.Addr5 == 0xFF)
            {
                TraceWrite("AdapterPrepareHardware-MacAddress0-bcast", LEVEL_WARNING,
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
                TraceWrite("AdapterPrepareHardware-MacAddress0-mcast", LEVEL_WARNING,
                    TraceLoggingBinary(context->currentMacAddress, sizeof(context->currentMacAddress), "Mac0"));
                context->currentMacAddress[0] &= ~1u;
            }
        }
    }

    // Read features

    {
        auto const version = Read32(context->regs->MacVersion);
        context->feature0 = Read32(context->regs->MacHwFeature0);
        context->feature1 = Read32(context->regs->MacHwFeature1);
        context->feature2 = Read32(context->regs->MacHwFeature2);
        context->feature3 = Read32(context->regs->MacHwFeature3);
        TraceWrite("AdapterPrepareHardware-config", LEVEL_INFO,
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
            TraceWrite("AdapterPrepareHardware-RkVer-not-supported", LEVEL_ERROR,
                TraceLoggingHexInt32(version.RkVer, "RkVer"));
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Done;
        }
    }

    // Create DMA enabler

    {
        auto const profile = context->feature1.AddressWidth == AddressWidth_32
            ? WdfDmaProfileScatterGather
            : WdfDmaProfileScatterGather64;
        WDF_DMA_ENABLER_CONFIG config;
        WDF_DMA_ENABLER_CONFIG_INIT(&config, profile, 2048); // TODO: Jumbo packets.
        config.WdmDmaVersionOverride = 3;
        switch (context->feature1.AddressWidth)
        {
        case AddressWidth_32:
            config.AddressWidthOverride = 32;
            maxPhysicalAddress.QuadPart = 0xFFFFFFFF;
            break;
        case AddressWidth_40:
            config.AddressWidthOverride = 40;
            maxPhysicalAddress.QuadPart = 0xFFFFFFFFFF;
            break;
        case AddressWidth_48:
            config.AddressWidthOverride = 48;
            maxPhysicalAddress.QuadPart = 0xFFFFFFFFFFFF;
            break;
        default:
            TraceWrite("AdapterPrepareHardware-AddressWidth-unknown", LEVEL_ERROR,
                TraceLoggingHexInt32(context->feature1.AddressWidth, "AddressWidth"));
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Done;
        }

        status = WdfDmaEnablerCreate(context->device, &config, WDF_NO_OBJECT_ATTRIBUTES, &context->dma);
        if (!NT_SUCCESS(status))
        {
            TraceWrite("WdfDmaEnablerCreate-failed", LEVEL_ERROR,
                TraceLoggingNTStatus(status));
            goto Done;
        }
    }

    // Update adapter configuration.

    {
        NET_ADAPTER_LINK_LAYER_ADDRESS address;
        NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&address, sizeof(context->currentMacAddress), context->currentMacAddress);
        NetAdapterSetCurrentLinkLayerAddress(adapter, &address);
        NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&address, sizeof(context->permanentMacAddress), context->permanentMacAddress);
        NetAdapterSetPermanentLinkLayerAddress(adapter, &address);

        NET_ADAPTER_LINK_STATE linkState;
        NET_ADAPTER_LINK_STATE_INIT_DISCONNECTED(&linkState);
        NetAdapterSetLinkState(adapter, &linkState);

        NET_ADAPTER_LINK_LAYER_CAPABILITIES linkCaps;
        auto const maxSpeed = context->feature0.Gmii ? 1'000'000'000u : 100'000'000u;
        NET_ADAPTER_LINK_LAYER_CAPABILITIES_INIT(&linkCaps, maxSpeed, maxSpeed);
        NetAdapterSetLinkLayerCapabilities(adapter, &linkCaps);

        NetAdapterSetLinkLayerMtuSize(adapter, 1500); // TODO: Jumbo packets.

        NET_ADAPTER_DMA_CAPABILITIES dmaCaps;
        NET_ADAPTER_DMA_CAPABILITIES_INIT(&dmaCaps, context->dma);
        dmaCaps.MaximumPhysicalAddress = maxPhysicalAddress;

        NET_ADAPTER_TX_CAPABILITIES txCaps;
        NET_ADAPTER_TX_CAPABILITIES_INIT_FOR_DMA(&txCaps, &dmaCaps, 1);
        NET_ADAPTER_RX_CAPABILITIES rxCaps; // TODO: More efficient if driver-managed.
        NET_ADAPTER_RX_CAPABILITIES_INIT_SYSTEM_MANAGED_DMA(&rxCaps, & dmaCaps, 2048, 1); // TODO: Jumbo packets.
        NetAdapterSetDataPathCapabilities(adapter, &txCaps, &rxCaps);

        // NetAdapterSetReceiveFilterCapabilities(); TODO: Packet filter.
    }

    // Initialize adapter.

    {
        // Software Reset

        Write32(&context->regs->DmaMode, 1u);

        unsigned retry;
        for (retry = 1000; retry != 0; retry -= 1)
        {
            KeStallExecutionProcessor(20);
            auto const dmaMode = Read32(context->regs->DmaMode);
            if (dmaMode & 1)
            {
                break;
            }
        }

        if (retry == 0)
        {
            TraceWrite("AdapterPrepareHardware-reset-timeout", LEVEL_ERROR);
            status = STATUS_TIMEOUT;
            goto Done;
        }

        // Misc configuration.

        TraceWrite("PostResetConfig", LEVEL_INFO,
            TraceLoggingHexInt32(Read32(context->regs->MacAddress[0].High.Value32), "MacHi"),
            TraceLoggingHexInt32(Read32(context->regs->MacAddress[0].Low.Value32), "MacLo"));

        // TODO: use ACPI _DSD?
        auto busMode = Read32(context->regs->DmaSysBusMode);
        busMode.AxiMaxWriteOutstanding = DefaultAxiMaxWriteOutstanding - 1;
        busMode.AxiMaxReadOutstanding = DefaultAxiMaxReadOutstanding - 1;
        Write32(&context->regs->DmaSysBusMode, busMode);

        Write32(&context->regs->Mac1usTicCounter, DefaultCsrRate / 1'000'000u - 1);

        static_assert(sizeof(DmaDescriptor) % BusBytes == 0,
            "DmaDescriptor must be a multiple of bus width.");
        ChannelDmaControl_t dmaControl = {};
        dmaControl.DescriptorSkipLength = (sizeof(DmaDescriptor) - 16) / BusBytes;
        dmaControl.PblX8 = true;
        Write32(&context->regs->DmaCh[0].DmaControl, dmaControl);

        Write32(&context->regs->MmcControl, 0x9); // Reset and freeze.

        Write32(&context->regs->MacTxFlowCtrl, 0);
        Write32(&context->regs->MacRxFlowCtrl, 0);

        MacConfiguration_t macConfig = {};
        macConfig.DisableCarrierSenseDuringTransmit = true;
        macConfig.PacketBurstEnable = true;
        Write32(&context->regs->MacConfiguration, macConfig);
    }

    // Start adapter.

    status = NetAdapterStart(adapter);
    if (!NT_SUCCESS(status))
    {
        TraceWrite("NetAdapterStart-failed", LEVEL_ERROR,
            TraceLoggingNTStatus(status));
        goto Done;
    }

Done:

    TraceExitWithStatus(AdapterPrepareHardware, status);
    return status;
}

EVT_WDF_DEVICE_RELEASE_HARDWARE AdapterReleaseHardware;
__declspec(code_seg("PAGE"))
NTSTATUS
AdapterReleaseHardware(
    _In_ NETADAPTER adapter,
    _In_ WDFCMRESLIST resourcesTranslated)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(resourcesTranslated);

    auto const context = AdapterGetContext(adapter);
    if (context->regs != nullptr)
    {
        // Restore MAC address.

        MacAddressLow_t mac0lo = {};
        mac0lo.Addr0 = context->permanentMacAddress[0];
        mac0lo.Addr1 = context->permanentMacAddress[1];
        mac0lo.Addr2 = context->permanentMacAddress[2];
        mac0lo.Addr3 = context->permanentMacAddress[3];

        MacAddressHigh_t mac0hi = {};
        mac0hi.Addr4 = context->permanentMacAddress[4];
        mac0hi.Addr5 = context->permanentMacAddress[5];

        Write32(&context->regs->MacAddress[0].High, mac0hi);
        Write32(&context->regs->MacAddress[0].Low, mac0lo);

        MmUnmapIoSpace(context->regs, sizeof(*context->regs));
        context->regs = nullptr;
    }

    TraceExit(AdapterReleaseHardware);
    return STATUS_SUCCESS;
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
        TraceWrite("NetAdapterInitAllocate-failed", LEVEL_ERROR);
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
        TraceWrite("NetAdapterCreate-failed", LEVEL_ERROR,
            TraceLoggingNTStatus(status));
        goto Done;
    }
    else
    {
        auto const context = AdapterGetContext(adapter);
        NT_ASSERT(context->device == nullptr);
        context->device = device;
    }

Done:

    TraceExitWithStatus(AdapterAdd, status);
    *pAdapter = adapter;
    return status;
}
