#ifndef LEP_BROKER_PROTOCOL_H
#define LEP_BROKER_PROTOCOL_H

#include "../LocaleEmulatorPlus/LocaleEmulatorPlus.h"

#define LEP_BROKER_CONFIG_MAGIC TAG4('LBC1')
#define LEP_BROKER_CONFIG_VERSION 5

#define LEP_BROKER_PROCESS_ACCESS (PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_LIMITED_INFORMATION)

enum LEP_BROKER_STAGE
{
    LepBrokerStageNone,
    LepBrokerStageValidateConfig,
    LepBrokerStageAcceptHandle,
    LepBrokerStageOpenProcess,
    LepBrokerStageQueryAccess,
    LepBrokerStageValidateTarget,
    LepBrokerStageLoadImage,
    LepBrokerStageAllocateMemory,
    LepBrokerStageReadLdr,
    LepBrokerStageProtectLdr,
    LepBrokerStageWriteImage,
    LepBrokerStageWritePayload,
    LepBrokerStagePatchLdr,
    LepBrokerStageComplete,
};

inline PCWSTR LepBrokerStageName(ULONG Stage)
{
    switch (Stage)
    {
        case LepBrokerStageValidateConfig: return L"validate-config";
        case LepBrokerStageAcceptHandle: return L"accept-created-handle";
        case LepBrokerStageOpenProcess: return L"open-process-fallback";
        case LepBrokerStageQueryAccess: return L"query-process-access";
        case LepBrokerStageValidateTarget: return L"validate-target-pid";
        case LepBrokerStageLoadImage: return L"load-core-image";
        case LepBrokerStageAllocateMemory: return L"allocate-target-memory";
        case LepBrokerStageReadLdr: return L"read-ldr-backup";
        case LepBrokerStageProtectLdr: return L"protect-ldr";
        case LepBrokerStageWriteImage: return L"write-core-image";
        case LepBrokerStageWritePayload: return L"write-payload";
        case LepBrokerStagePatchLdr: return L"patch-ldr";
        case LepBrokerStageComplete: return L"complete";
        default: return L"none";
    }
}

typedef struct LEP_BROKER_CONFIG
{
    ULONG Magic;
    ULONG Version;
    ULONG Size;
    ULONG PayloadSize;
    ULONG Result;
    ULONG ThreadId;
    ULONG InjectionFlags;
    ULONG Stage;
    ULONGLONG ProcessHandleValue; // Handle in the broker, not in its creator.
    ULONG GrantedAccess;
    ULONG Reserved;
    BYTE Payload[1];
} LEP_BROKER_CONFIG, *PLEP_BROKER_CONFIG;

static_assert(FIELD_OFFSET(LEP_BROKER_CONFIG, ProcessHandleValue) == 32, "broker handle offset");
static_assert(FIELD_OFFSET(LEP_BROKER_CONFIG, Payload) == 48, "broker payload offset");

#define LEP_ELEVATION_CONFIG_MAGIC TAG4('LEC1')
#define LEP_ELEVATION_CONFIG_VERSION 1
#define LEP_ELEVATION_CONFIG_MAX_SIZE 0x2000000

// Restricted access rights matching AppInfo's returned handles so the
// non-elevated caller can wait, query, terminate, or resume an initially
// suspended child without gaining VM write/injection access across UAC.
#define LEP_ELEVATION_CALLER_PROCESS_ACCESS \
    (PROCESS_TERMINATE | PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | READ_CONTROL)
#define LEP_ELEVATION_CALLER_THREAD_ACCESS \
    (THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | READ_CONTROL)

enum LEP_ELEVATION_STAGE
{
    LepElevationStageNone = 0,
    LepElevationStageOpenMapping,
    LepElevationStageValidateConfig,
    LepElevationStageCreateProcess,
    LepElevationStageInjectTarget,
    LepElevationStageOpenCaller,
    LepElevationStageDuplicateHandles,
    LepElevationStageResumeTarget,
    LepElevationStageComplete,
};

inline PCWSTR LepElevationStageName(ULONG Stage)
{
    switch (Stage)
    {
        case LepElevationStageOpenMapping: return L"open-mapping";
        case LepElevationStageValidateConfig: return L"validate-config";
        case LepElevationStageCreateProcess: return L"create-process";
        case LepElevationStageInjectTarget: return L"inject-target";
        case LepElevationStageOpenCaller: return L"open-caller";
        case LepElevationStageDuplicateHandles: return L"duplicate-handles";
        case LepElevationStageResumeTarget: return L"resume-target";
        case LepElevationStageComplete: return L"complete";
        default: return L"none";
    }
}

typedef struct LEP_ELEVATION_CONFIG
{
    ULONG Magic;
    ULONG Version;
    ULONG Size;
    ULONG Stage;
    ULONG Win32Error;
    ULONG InjectionStatus;
    ULONG CallerProcessId;
    ULONG CreationFlags;
    ULONG StartupFlags;
    ULONG StartupX;
    ULONG StartupY;
    ULONG StartupXSize;
    ULONG StartupYSize;
    ULONG StartupXCountChars;
    ULONG StartupYCountChars;
    ULONG StartupFillAttribute;
    USHORT StartupShowWindow;
    USHORT Reserved0;
    ULONG Reserved1;
    ULONGLONG StartupMonitorHandle;
    ULONG ApplicationOffset;
    ULONG ApplicationLength;
    ULONG CommandLineOffset;
    ULONG CommandLineLength;
    ULONG CurrentDirectoryOffset;
    ULONG CurrentDirectoryLength;
    ULONG DesktopOffset;
    ULONG DesktopLength;
    ULONG TitleOffset;
    ULONG TitleLength;
    ULONG PayloadOffset;
    ULONG PayloadSize;
    ULONGLONG TargetProcessHandle;
    ULONGLONG TargetThreadHandle;
    ULONG TargetProcessId;
    ULONG TargetThreadId;
    ULONG CrossArchBrokerUsed;
    ULONG Reserved2;
    BYTE Data[1];
} LEP_ELEVATION_CONFIG, *PLEP_ELEVATION_CONFIG;

static_assert(FIELD_OFFSET(LEP_ELEVATION_CONFIG, StartupMonitorHandle) == 72, "elevation monitor handle offset");
static_assert(FIELD_OFFSET(LEP_ELEVATION_CONFIG, TargetProcessHandle) == 128, "elevation target handle offset");
static_assert(FIELD_OFFSET(LEP_ELEVATION_CONFIG, Data) == 160, "elevation data offset");

#endif
