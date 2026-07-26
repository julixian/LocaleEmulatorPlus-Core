#ifndef _HOOKPORT_H_19ab81ea_402e_4f99_8d93_44baae1cc16b_
#define _HOOKPORT_H_19ab81ea_402e_4f99_8d93_44baae1cc16b_

#include "ml.h"
#include "HandleTable.h"

_ML_C_HEAD_

#pragma pack(1)

/*++

NTSTATUS
HPCALL
NtCloseFilter(
    HPARGS
    HANDLE Handle
)
{
    if (xxx)
        *Action = SYSTEM_CALL_BLOCK;
    else if (yyy)
        *Action = SYSTEM_CALL_CONTINUE;
    return STATUS_SUCCESS;      // return value will be ignored when *Action == ContinueSystemCall
}

--*/

#define HP_MAX_SERVICE_INDEX           0x300
#define HP_MAX_WIN32K_SERVICE_INDEX    0x500

#define HP_MAX_SERVICE_TABLE_COUNT      2
#define HP_NTKRNL_SERVICE_INDEX         0
#define HP_WIN32K_SERVICE_INDEX         1

#define MAX_FILTER_NUMBER           bitsof(((PSYSCALL_INFO)0)->FilterBitmap)


#define HOOKPORT_CALLTYPE   FASTCALL
#define HPCALL              HOOKPORT_CALLTYPE


#define HPARG_FLTINFO  FltInfo
#define HPARG_SYSCALL  SysCallInfo

#define HPARG_INVOKE    HPARG_SYSCALL, HPARG_FLTINFO

#define HPARGSN     PSYSCALL_INFO HPARG_SYSCALL, PSYSCALL_FILTER_INFO HPARG_FLTINFO
#define HPARGS      HPARGSN,

#define HpSetFilterAction(_action) HPARG_FLTINFO->Action = _action
#define HpGetFilterContext() HPARG_FLTINFO->FilterContext

typedef enum
{
    ContinueSystemCall,               // call next filter if exists, default, and must be zero
    BlockSystemCall,                  // Block the system call and return

} SystemCallFilterAction;

typedef struct SYSCALL_FILTER_INFO
{
    SystemCallFilterAction  Action;
    PVOID                   FilterContext;

    SYSCALL_FILTER_INFO()
    {
        Action = ContinueSystemCall;
    }

} SYSCALL_FILTER_INFO, *PSYSCALL_FILTER_INFO;

enum
{
    SystemCallFilterEnable      = 0x00000001,
    SystemCallHasInt2ESelector  = 0x00000004,
};

enum
{
    HookPortStatusFirst = DEFINE_NTSTATUS(STATUS_SEVERITY_ERROR, 0x100),

    STATUS_HOOK_PORT_NOT_INITIALIZED,
    STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM,
    STATUS_HOOK_PORT_ENTRY_NOT_FOUND,
    STATUS_HOOK_PORT_FILTER_NOT_FOUND,
    STATUS_HOOK_PORT_TOO_MANY_FILTERS,
    HookPortStatusLast,
};

typedef struct
{
    PVOID Callback;
    PVOID Context;
} SYSTEM_CALL_FILTER, *PSYSTEM_CALL_FILTER;

typedef struct
{
    USHORT              Flags;

    union
    {
        struct
        {
            USHORT ServiceIndex : 12;
            USHORT ServiceTableIndex : 4;
            USHORT ServiceFlags;
        };

        ULONG ServiceData;
    };

    ULONG               FilterBitmap;
    PSYSTEM_CALL_FILTER FilterCallbacks;
    PVOID               FunctionAddress;
    ULONG               NameHash;

} SYSCALL_INFO, *PSYSCALL_INFO;

typedef struct
{
    ULONG           Hash;
    PSYSCALL_INFO   Entry;

} SYSTEM_CALL_HASH, *PSYSTEM_CALL_HASH;

typedef struct
{
    ULONG_PTR           Count;
    PSYSTEM_CALL_HASH   Entry;

} SYSTEM_CALL_HASH_TABLE, *PSYSTEM_CALL_HASH_TABLE;

#pragma pack()

typedef struct
{
    LONG_PTR                    RefCount;
    SYSTEM_CALL_HASH_TABLE      HashTable;
    PSYSCALL_INFO               SystemCallInfo[HP_MAX_SERVICE_TABLE_COUNT];
    ULONG_PTR                   MaxSystemCallCount[HP_MAX_SERVICE_TABLE_COUNT];

} HOOK_PORT_GLOBAL_INFO, *PHOOK_PORT_GLOBAL_INFO;

NTSTATUS
InstallHookPort(
    PLDR_MODULE SysCallModule = NULL,
    ULONG       Flags = 0
);

NTSTATUS
UnInstallHookPort(
    VOID
);

NTSTATUS
HpAddSystemCallByRoutine(
    PVOID Routine,
    ULONG RoutineHash
);

NTSTATUS
HpAddSystemCallByRoutineRange(
    PVOID*      Routine,
    PULONG      RoutineHash,
    ULONG_PTR   Count
);

NTSTATUS
HpAddSystemCall(
    PSYSCALL_INFO   SystemCall,
    ULONG_PTR       Count = 1
);

NTSTATUS
HpAddSystemCallFilter(
    ULONG RoutineHash,
    PVOID Routine,
    PVOID Context = NULL
);

NTSTATUS
HpRemoveSystemCallFilter(
    ULONG RoutineHash,
    PVOID Routine
);

PSYSCALL_INFO
HpLookupSystemCall(
    ULONG_PTR SystemCallHash
);

PVOID
HpGetSystemCallOriginal(
    ULONG RoutineHash
);

_ML_C_TAIL_

#if SUPPORT_VA_ARGS_MACRO


#define CallSysCall(_Routine, _SysCallInfo, ...) \
            CallFuncPtr( \
                _Routine, \
                HpGetSystemCallOriginal((_SysCallInfo)->NameHash), \
                __VA_ARGS__ \
            )

#define HpCallSysCall(_Routine, ...) \
            CallFuncPtr( \
                _Routine, \
                HpGetSystemCallOriginal(HPARG_SYSCALL->NameHash), \
                __VA_ARGS__ \
            )


#define ADD_FILTER_(routine, HookRoutine, ...) \
            HpAddSystemCallFilter( \
                NTDLL_##routine, \
                HookRoutine, \
                __VA_ARGS__ \
            )

#define ADD_W32_FILTER_(routine, HookRoutine, ...) \
            HpAddSystemCallFilter( \
                WIN32K_##routine, \
                HookRoutine, \
                __VA_ARGS__ \
            )

#define DEL_FILTER_(routine, HookRoutine, ...) \
            HpRemoveSystemCallFilter( \
                NTDLL_##routine, \
                HookRoutine \
            )

#define DEL_W32_FILTER_(routine, HookRoutine, ...) \
            HpRemoveSystemCallFilter( \
                WIN32K_##routine, \
                HookRoutine \
            )

#define ADD_FILTER(routine, ...) ADD_FILTER_(routine, Hook##routine, __VA_ARGS__)
#define ADD_W32_FILTER(routine, ...) ADD_FILTER_(routine, Hook##routine, __VA_ARGS__)

#define DEL_FILTER(routine) DEL_FILTER_(routine, Hook##routine)
#define DEL_W32_FILTER(routine) DEL_W32_FILTER_(routine, Hook##routine)

#endif // SUPPORT_VA_ARGS_MACRO

#endif // _HOOKPORT_H_19ab81ea_402e_4f99_8d93_44baae1cc16b_
