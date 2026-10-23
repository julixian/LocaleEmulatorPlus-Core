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

#endif
