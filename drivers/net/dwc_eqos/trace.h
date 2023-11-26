#pragma once

// *dwc_eqos = {5d8331d3-70b3-5620-5664-db28f48a4b79}
TRACELOGGING_DECLARE_PROVIDER(TraceProvider);

#define LEVEL_CRITICAL TraceLoggingLevel(WINEVENT_LEVEL_CRITICAL)
#define LEVEL_ERROR    TraceLoggingLevel(WINEVENT_LEVEL_ERROR)
#define LEVEL_WARNING  TraceLoggingLevel(WINEVENT_LEVEL_WARNING)
#define LEVEL_INFO     TraceLoggingLevel(WINEVENT_LEVEL_INFO)
#define LEVEL_VERBOSE  TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE)

#define KW_GENERAL    TraceLoggingKeyword(0x1) // All events should have at least one keyword.
#define KW_ENTRY_EXIT TraceLoggingKeyword(0x2)

// TraceLoggingWrite(eventName, KW_GENERAL, ...)
#define TraceWrite(eventName, ...) \
    TraceLoggingWrite(TraceProvider, eventName, KW_GENERAL, __VA_ARGS__)

// TraceLoggingWrite("funcName-Entry", KW_ENTRY_EXIT, INFO, ...)
#define TraceEntry(funcName, ...) \
    TraceWrite(#funcName "-Entry", \
        KW_ENTRY_EXIT, LEVEL_INFO, \
        __VA_ARGS__)

// TraceLoggingWrite("funcName-Exit", KW_ENTRY_EXIT, INFO, ...)
#define TraceExit(funcName, ...) \
    TraceWrite(#funcName "-Exit", \
        KW_ENTRY_EXIT, LEVEL_INFO, \
        __VA_ARGS__) \

// TraceLoggingWrite("funcName-Exit", KW_ENTRY_EXIT, INFO/ERROR, status, ...)
#define TraceExitWithStatus(funcName, status, ...) \
    if (NTSTATUS const _status = (status); NT_SUCCESS(_status)) { \
        TraceExit(funcName, LEVEL_INFO, TraceLoggingNTStatus(_status, "status"), __VA_ARGS__); \
    } else { \
        TraceExit(funcName, LEVEL_ERROR, TraceLoggingNTStatus(_status, "status"), __VA_ARGS__); \
    } \
