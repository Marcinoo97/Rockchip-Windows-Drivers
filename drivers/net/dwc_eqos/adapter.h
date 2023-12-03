#pragma once

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterD0Entry(
    _In_ NETADAPTER adapter,
    _In_ WDF_POWER_DEVICE_STATE targetState);

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterD0Exit(
    _In_ NETADAPTER adapter,
    _In_ WDF_POWER_DEVICE_STATE targetState);

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterPrepareHardware(
    _In_ NETADAPTER adapter,
    _In_ WDFCMRESLIST resourcesRaw,
    _In_ WDFCMRESLIST resourcesTranslated);

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterReleaseHardware(
    _In_ NETADAPTER adapter,
    _In_ WDFCMRESLIST resourcesTranslated);

__declspec(code_seg("PAGE"))
NTSTATUS
AdapterAdd(
    _Inout_ WDFDEVICE device,
    _Out_ NETADAPTER* pAdapter);
