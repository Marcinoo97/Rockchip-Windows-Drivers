#pragma once

__declspec(code_seg("PAGE"))
NTSTATUS
DeviceAdd(_Inout_ WDFDEVICE_INIT* deviceInit);
