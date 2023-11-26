#pragma once

#include <ntddk.h>

#pragma warning(push)
#pragma warning(disable: 4471) // forward declaration of an unscoped enumeration
#include <wdf.h>
#pragma warning(pop)

#include <netadaptercx.h>
#include <initguid.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>
