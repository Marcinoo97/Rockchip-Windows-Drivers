#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <netadaptercx.h>
#include <initguid.h>
#include <TraceLoggingProvider.h>

// *dwc_eqos = {5d8331d3-70b3-5620-5664-db28f48a4b79}
TRACELOGGING_DECLARE_PROVIDER(TraceProvider);
