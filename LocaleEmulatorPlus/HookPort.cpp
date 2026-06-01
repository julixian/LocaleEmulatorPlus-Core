#include "HookPort.h"
#include "SectionProtector.h"

VOID InterlockedExchangeDoublePointer(PVOID p1, PVOID p2)
{
#if ML_X86

    _InterlockedExchange64((PLONG64)p1, *(PLONG64)p2);

#elif ML_AMD64

    InterlockedCompare64Exchange128((PLONG64)p1, 0, 0, 0);

#else

    #error

#endif
}

#if ML_AMD64

#else // 32

PHOOK_PORT_GLOBAL_INFO g_GlobalInfo;


PVOID HpAlloc(ULONG_PTR Size, ULONG Flags = 0)
{
    return AllocateMemory(Size, Flags);
}


BOOL HpFree(PVOID Memory)
{
    return FreeMemory(Memory);
}

NTSTATUS HpAllocateVirtualMemory(PVOID *Address, ULONG_PTR Size)
{

    *Address = NULL;
    return Mm::AllocVirtualMemory(Address, Size);

}

NTSTATUS HpFreeVirtualMemory(PVOID Address)
{

    return Mm::FreeVirtualMemory(Address);

}


VOID
HppDestroyHashTable(
    PSYSTEM_CALL_HASH_TABLE Table
);

NTSTATUS
UnInstallHookPortInternal(
    PHOOK_PORT_GLOBAL_INFO GlobalInfo
);

VOID HookSysEnter_x86();
VOID HookSysEnter_Wow64();

ULONG_PTR
FASTCALL
HpServiceDispatcherInternal(
    PHOOK_PORT_GLOBAL_INFO      Info,
    PHOOK_PORT_DISPATCHER_INFO  Dispatcher,
    PULONG_PTR                  Arguments,
    PSYSCALL_FILTER_INFO        UserModeFltInfo = NULL
);

BOOL
FASTCALL
HpIsCurrentCallSkip(
    PSYSCALL_INFO SysCallInfo
);

NAKED
VOID
StubSysEnter()
{
    ASM_DUMMY_AUTO();
}

ForceInline PHOOK_PORT_GLOBAL_INFO HppGetGlobalInfo()
{
    return g_GlobalInfo;
}

ForceInline PHOOK_PORT_GLOBAL_INFO HppSetGlobalInfo(PHOOK_PORT_GLOBAL_INFO Info)
{
    g_GlobalInfo = Info;
    return Info;
}

/************************************************************************
  x86
************************************************************************/


NAKED VOID HookSysEnter_x86()
{
    INLINE_ASM
    {
        lea edx, [edx + 08h];
        jmp HookSysEnter_Wow64;
    }
}

NTSTATUS PatchNtdllSysCallStub(PHOOK_PORT_GLOBAL_INFO Info)
{
    PBYTE           Function;
    PSYSCALL_INFO   SysCall;

    FOR_EACH(SysCall, Info->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX], Info->SystemCallCount[HP_NTKRNL_SERVICE_INDEX])
    {
        Function = (PBYTE)SysCall->FunctionAddress;

        LOOP_FOREVER
        {
            if (Function[0] == 0xC2 || Function[0] == 0xC3)
                break;

            if (Function[0] != CALL)
            {
                Function += GetOpCodeSize(Function);
                continue;
            }

            Mp::PATCH_MEMORY_DATA p[] =
            {
                Mp::FunctionCallVa(Function, Info->User.KiFastSystemCall),
            };

            Mp::PatchMemory(p, countof(p));
            break;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS HookSysCall_x86(HOOK_PORT_GLOBAL_INFO *GlobalInfo)
{
    NTSTATUS    Status;
    BYTE        OpCodeBuffer[0x10];
    ULONG       OldProtect, StubSysEnterProtect, SysEnterSize;
    PBYTE       KiFastSystemCall, BaseKiFastSystemCall;
    __m128      Backup;

    KiFastSystemCall = (PBYTE)GlobalInfo->User.KiFastSystemCall;
    BaseKiFastSystemCall = KiFastSystemCall;

    if (
        (
         *(PULONG)&KiFastSystemCall[0] != 0x340FD48B ||  // mov edx, esp
                                                         // sysenter
         *(PULONG)&KiFastSystemCall[4] != 0x24A48DC3 ||  // ret
         *(PULONG)&KiFastSystemCall[8] != 0              // lea esp, dword ptr [esp]     7 bytes
        ) &&

        (
         *(PULONG)&KiFastSystemCall[0] != 0x340FD48B ||  // mov edx, esp
                                                         // sysenter
         *(PULONG)&KiFastSystemCall[4] != 0xEB24248D ||  // lea esp, dword ptr [esp]    3 bytes
         *(PULONG)&KiFastSystemCall[8] != 0xCCCCCC03     // jmp short KiFastSystemCallRet
        ) &&

        (
         *(PULONG)&KiFastSystemCall[0] != 0x340FD48B ||     // mov edx, esp
                                                            // sysenter
         *(PULONG)&KiFastSystemCall[4] != 0x0024A48D ||     // lea esp, dword ptr [esp]    7 bytes
         *(PULONG)&KiFastSystemCall[8] != 0xEB000000 ||     // jmp short KiFastSystemCallRet
         *(PULONG)&KiFastSystemCall[12] != 0xCCCCCC03       // CC CC CC
        ) &&

        (
         *(PULONG)&KiFastSystemCall[0]   != 0x000000BA  ||  // mov edx, 000000C3h
         *(PUSHORT)&KiFastSystemCall[4]  != 0xBAC3      ||  // mov edx, const
         *(PUSHORT)&KiFastSystemCall[10] != 0xE2FF          // jmp edx
        ) &&

        (
         *(PULONG)&KiFastSystemCall[0]   != 0x01EBD48B  ||  // mov edx, esp jmp @f+3
         *(PUSHORT)&KiFastSystemCall[4]  != 0x68C3      ||  // ret  push HookSysEnter_x86
         *(PUSHORT)&KiFastSystemCall[10] != 0x90C3          // lea esp, dword ptr [esp]     ; 7 bytes
        )
       )
    {
        return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
    }

    SysEnterSize = 0x10;
    Status = ProtectVirtualMemory(
                BaseKiFastSystemCall,
                SysEnterSize,
                PAGE_EXECUTE_READWRITE,
                &OldProtect
             );
    FAIL_RETURN(Status);

    Status = ProtectVirtualMemory(
                StubSysEnter,
                SysEnterSize,
                PAGE_EXECUTE_READWRITE,
                &StubSysEnterProtect
             );
    if (NT_FAILED(Status))
    {
        if (OldProtect != PAGE_EXECUTE_READWRITE)
            ProtectVirtualMemory(BaseKiFastSystemCall, 12, OldProtect, &OldProtect);

        return Status;
    }

    GlobalInfo->User.SysEnterSize = SysEnterSize;

    Backup = _mm_loadu_ps((float *)BaseKiFastSystemCall);
    _mm_storeu_ps((float *)StubSysEnter, Backup);
    _mm_storeu_ps((float *)OpCodeBuffer, Backup);

    //
    // mov  edx, 'C3' 000000
    // mov  edx, HookSysEnter_x86
    // jmp  edx
    //
    // jmp @f + 3
    // sysenter
    // ret
    // push HookSysEnter_x86
    // ret
    // nop
    //
    *(PUSHORT)&OpCodeBuffer[2]      = 0x01EB;
    OpCodeBuffer[5]                 = 0x68;
    *(PULONG_PTR)&OpCodeBuffer[6]   = (ULONG_PTR)HookSysEnter_x86;
    *(PUSHORT)&OpCodeBuffer[10]     = 0x90C3;

    _mm_storeu_ps((float *)BaseKiFastSystemCall, _mm_loadu_ps((float *)OpCodeBuffer));

    if (StubSysEnterProtect != PAGE_EXECUTE_READWRITE)
    {
        ProtectVirtualMemory(StubSysEnter, SysEnterSize, StubSysEnterProtect, &StubSysEnterProtect);
    }

    if (OldProtect != PAGE_EXECUTE_READWRITE)
    {
        ProtectVirtualMemory(BaseKiFastSystemCall, 12, OldProtect, &OldProtect);
    }

    PPEB_BASE Peb = CurrentPeb();

    switch (Peb->OSMajorVersion)
    {
        case 6:
            switch (Peb->OSMinorVersion)
            {
                case 2: // win 8
                case 3: // win 8.1
                case 4: // win 10
                    PatchNtdllSysCallStub(GlobalInfo);
                    break;
            }
            break;

        case 10: // win 10
            PatchNtdllSysCallStub(GlobalInfo);
            break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS UnHookSysCall_x86(HOOK_PORT_GLOBAL_INFO *GlobalInfo)
{
    PVOID       BaseAddress, KiFastSystemCall;
    ULONG       OldProtect;
    NTSTATUS    Status;

    KiFastSystemCall = GlobalInfo->User.KiFastSystemCall;
    BaseAddress = KiFastSystemCall;
    Status = ProtectVirtualMemory(
                KiFastSystemCall,
                12,
                PAGE_EXECUTE_READWRITE,
                &OldProtect
             );

    if (!NT_SUCCESS(Status))
        return Status;

    _mm_storeu_ps((float *)KiFastSystemCall, _mm_loadu_ps((float *)StubSysEnter));

    if (OldProtect != PAGE_EXECUTE_READWRITE)
    {
        ProtectVirtualMemory(KiFastSystemCall, 12, OldProtect, &OldProtect);
    }

    return STATUS_SUCCESS;
}


/************************************************************************
  wow64
************************************************************************/


ULONG_PTR
NTAPI
HpUserModeDispatcher(
    IN      ULONG                   ServiceIndex,
    IN OUT  PULONG_PTR              Arguments,
    IN OUT  PPUSHAD_REGISTER        RegContext,
    IN OUT  PVOID*                  AddressOfReturnAddress,
    OUT     PSYSCALL_FILTER_INFO    FltInfo
)
{
    BOOL                        HasGlobalFilter, ValidSystemCall;
    ULONG                       TableIndex;
    ULONG_PTR                   ReturnValue;
    HOOK_PORT_DISPATCHER_INFO   Dispatcher;
    PHOOK_PORT_GLOBAL_INFO      Info = HppGetGlobalInfo();

    if (Info == NULL)
        return 0;

    HasGlobalFilter = Info->GlobalFilter.Routine != NULL;
    ValidSystemCall = TRUE;

    TableIndex = HP_TABLE_INDEX(ServiceIndex);
    ServiceIndex = HP_SERVICE_INDEX(ServiceIndex);

    if (TableIndex >= HP_MAX_SERVICE_TABLE_COUNT || Info->SystemCallInfo[TableIndex] == NULL)
    {
        if (!HasGlobalFilter)
            return 0;

        ValidSystemCall = FALSE;
    }

    if (ServiceIndex >= Info->MaxSystemCallCount[TableIndex])
    {
        if (!HasGlobalFilter)
            return 0;

        ValidSystemCall = FALSE;
    }

    Dispatcher.SystemCallInfo = ValidSystemCall ? &Info->SystemCallInfo[TableIndex][ServiceIndex] : NULL;

    if (ValidSystemCall && HpIsCurrentCallSkip(Dispatcher.SystemCallInfo))
        return 0;

    Dispatcher.AddressOfReturnAddress   = AddressOfReturnAddress;
    Dispatcher.ServiceData              = 0;
    Dispatcher.ServiceIndex             = ServiceIndex;
    Dispatcher.TableIndex               = TableIndex;
    Dispatcher.OriginalRoutine          = ValidSystemCall ? Dispatcher.SystemCallInfo->FunctionAddress : NULL;

    if (Info->GlobalFilter.Routine != NULL)
    {
        SYSCALL_FILTER_INFO FltInfo;

        FltInfo.FilterContext = Info->GlobalFilter.Context;
        FltInfo.SsdtRoutine = ValidSystemCall ? Dispatcher.SystemCallInfo->FunctionAddress : NULL;
        ReturnValue = Info->GlobalFilter.Routine(
                    Dispatcher.SystemCallInfo,
                    &FltInfo,
                    Dispatcher.ServiceData,
                    &FltInfo.SsdtRoutine,
                    Arguments,
                    Dispatcher.AddressOfReturnAddress
                 );

        if (FltInfo.Action == GlobalFilterHandled || !ValidSystemCall)
            return ReturnValue;
    }
    else if (HasGlobalFilter && !ValidSystemCall)   // changed async
    {
        return 0;
    }

    if (
         Dispatcher.SystemCallInfo->FilterCallbacks == NULL ||
         FLAG_OFF(Dispatcher.SystemCallInfo->Flags, SystemCallFilterEnable)
       )
    {
        return 0;
    }

    SEH_TRY
    {
        ReturnValue = HpServiceDispatcherInternal(Info, &Dispatcher, Arguments, FltInfo);
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ReturnValue = GetExceptionCode();
    }

    switch (FltInfo->Action)
    {
        case BlockSystemCall:
            *AddressOfReturnAddress = Dispatcher.SystemCallInfo->ReturnOpAddress;
            break;
    }

    return ReturnValue;
}

NAKED VOID HookSysEnter_Wow64_Win10()
{
    INLINE_ASM
    {
        lea edx, [esp + 08h];
        jmp HookSysEnter_Wow64;
    }
}

NAKED VOID HookSysEnter_Wow64_Win8()
{
    INLINE_ASM
    {
        lea edx, [esp + 8];
        jmp HookSysEnter_Wow64;
    }
}

NAKED VOID HookSysEnter_Wow64()
{
    INLINE_ASM
    {
        push offset StubSysEnter;
        pushfd;

//        cmp     ax, HP_MAX_SERVICE_INDEX
//        jae     _EXCEED_MAX_SERVICE;

        pushad;

        lea     ecx, [esp + 28h];
        lea     esp, [esp - 10h];

        and     eax, 0FFFFh;
        and     dword ptr[esp], 0;

        push    esp;                    // FltInfo
        push    ecx;                    // addr of ret addr
        lea     ecx, [ecx - 28h];
        push    ecx;                    // reg context
        push    edx;                    // arguments
        push    eax;                    // service index
        call    HpUserModeDispatcher;

        mov     ecx, [esp]SYSCALL_FILTER_INFO.Action;
        lea     esp, [esp + 10h];

        jecxz   _SYSTEM_CALL_PERMIT;    // SYSTEM_CALL_CONTINUE
        dec     ecx;
        jnz     _SYSTEM_CALL_PERMIT;

        mov     esp, [esp]PUSHAD_REGISTER.Rsp;
        popfd;

        lea     esp, [esp + 0x04];      // pushed StubSysEnter
        ret;

_SYSTEM_CALL_PERMIT:

        popad;

// _EXCEED_MAX_SERVICE:

        popfd;
        ret;
    }
}


PVOID GetWow64SyscallJumpStub()
{
    PVOID NtCancelTimer;

    NtCancelTimer = LookupExportTable(GetNtdllHandle(), NTDLL_NtCancelTimer);

    return WalkOpCodeT(NtCancelTimer, 0x20,
                WalkOpCodeM(Buffer, OpLength, Ret)
                {
                    switch (Buffer[0])
                    {
                        case 0x64:
                            if (*(PUSHORT)&Buffer[1] == 0x15FF) // call    dword ptr fs:[0C0h]
                            {
                                Ret = (PVOID)ReadFsPtr(*(PULONG)&Buffer[3]);
                                return STATUS_SUCCESS;
                            }
                            break;

                        case 0xBA:  // mov edx, const
                            Ret = *(PVOID *)&Buffer[1];
                            break;

                        case 0xFF:
                            switch (Buffer[1])
                            {
                                case 0xD2:  // call edx
                                    return STATUS_SUCCESS;

                                case 0x12:  // call [edx]
                                    Ret = *(PVOID *)Ret;
                                    return STATUS_SUCCESS;
                            }

                        case 0xC2:
                        case 0xC3:
                            Ret = nullptr;
                            return STATUS_SUCCESS;
                    }

                    return STATUS_UNSUCCESSFUL;
                }
            );
}

NTSTATUS HookSysCall_Wow64(HOOK_PORT_GLOBAL_INFO *GlobalInfo)
{
    BOOL        HpInstalled;
    __m128      Temp;
    PBYTE       Buffer;
    ULONG       Protect, StubSysEnterProtect;
    ULONG_PTR   SrcLength, DestLength;
    PVOID       Wow64SyscallJumpStub, HookRoutine;
    NTSTATUS    Status;
    PPEB_BASE   Peb;

    const static ULONG_PTR Wow64FsC0Size = 7;

    Wow64SyscallJumpStub = GetWow64SyscallJumpStub();
    GlobalInfo->User.Wow64SyscallJumpStub = Wow64SyscallJumpStub;

    HpInstalled = *(PBYTE)Wow64SyscallJumpStub == PUSH;

    Status = ProtectVirtualMemory(
                Wow64SyscallJumpStub,
                Wow64FsC0Size,
                PAGE_EXECUTE_READWRITE,
                &Protect
             );
    FAIL_RETURN(Status);

    Status = ProtectVirtualMemory(
                StubSysEnter,
                Wow64FsC0Size,
                PAGE_EXECUTE_READWRITE,
                &StubSysEnterProtect
             );
    FAIL_RETURN(Status);

    GlobalInfo->User.SysEnterSize = ULONG_PTR_MAX;

    Temp = _mm_loadu_ps((float *)Wow64SyscallJumpStub);
    _mm_storeu_ps((float *)GlobalInfo->User.Wow64FsC0Backup, Temp);

    CopyOneOpCode(StubSysEnter, Wow64SyscallJumpStub, &DestLength, &SrcLength, 0, 0x10);
    if (HpInstalled)
    {
        CopyOneOpCode(PtrAdd(StubSysEnter, DestLength), PtrAdd(Wow64SyscallJumpStub, SrcLength), NULL, NULL, 0, 0x10 - DestLength);
    }
    else
    {
        *(PBYTE)PtrAdd(StubSysEnter, DestLength) = PUSH;
        *(PVOID *)PtrAdd(StubSysEnter, DestLength + 1) = PtrAdd(Wow64SyscallJumpStub, SrcLength);
        *(PBYTE)PtrAdd(StubSysEnter, DestLength + 1 + sizeof(PVOID)) = 0xC3;
    }

    if (SrcLength < 6)
        DebugBreakPoint();

    Buffer = (PBYTE)&Temp;

    Peb = CurrentPeb();

    if (Peb->OSMajorVersion <= 6 && Peb->OSMinorVersion <= 1)
    {
        HookRoutine = HookSysEnter_Wow64;
    }
    else if (Peb->OSMajorVersion == 6 && Peb->OSMinorVersion > 1)
    {
        HookRoutine = HookSysEnter_Wow64_Win8;
    }
    else
    {
        HookRoutine = HookSysEnter_Wow64_Win10;
    }

    Buffer[0] = PUSH;
    *(PVOID *)&Buffer[1] = HookRoutine;
    Buffer[5] = 0xC3;

    _mm_storeu_ps((float *)Wow64SyscallJumpStub, Temp);

    if (StubSysEnterProtect != PAGE_EXECUTE_READWRITE)
        ProtectVirtualMemory(StubSysEnter, Wow64FsC0Size, StubSysEnterProtect, &StubSysEnterProtect);

    if (Protect != PAGE_EXECUTE_READWRITE)
        ProtectVirtualMemory(Wow64SyscallJumpStub, Wow64FsC0Size, Protect, &Protect);

    return Status;
}

NTSTATUS UnHookSysCall_Wow64(HOOK_PORT_GLOBAL_INFO *GlobalInfo)
{
    ULONG       Protect;
    PVOID       Wow64SyscallJumpStub;
    NTSTATUS    Status;

    Wow64SyscallJumpStub = GlobalInfo->User.Wow64SyscallJumpStub;

    Status = ProtectVirtualMemory(
                Wow64SyscallJumpStub,
                0x10,
                PAGE_EXECUTE_READWRITE,
                &Protect
             );
    if (!NT_SUCCESS(Status))
        return Status;

    _mm_storeu_ps((float *)Wow64SyscallJumpStub, _mm_loadu_ps((float *)GlobalInfo->User.Wow64FsC0Backup));

    if (Protect != PAGE_EXECUTE_READWRITE)
        ProtectVirtualMemory(Wow64SyscallJumpStub, 0x10, Protect, &Protect);

    return Status;
}


/************************************************************************
  common
************************************************************************/

BOOL
FASTCALL
HpIsCurrentCallSkip(
    PSYSCALL_INFO SysCallInfo
)
{


    SYSCALL_FILTER_SKIP_INFO *SkipInfo;

    SkipInfo = (SYSCALL_FILTER_SKIP_INFO *)FindThreadFrame(SYSCALL_SKIP_MAGIC);
    return SkipInfo != NULL &&
           (
            SkipInfo->ServiceIndex == ALL_SERVICE_INDEX ||
            SysCallInfo->ServiceData == SkipInfo->ServiceIndex
           );

}

ULONG_PTR
FASTCALL
HpServiceDispatcherInternal(
    PHOOK_PORT_GLOBAL_INFO      Info,
    PHOOK_PORT_DISPATCHER_INFO  Dispatcher,
    PULONG_PTR                  Arguments,
    PSYSCALL_FILTER_INFO        UserModeFltInfo
)
{
    PVOID                   LocalArguments;
    ULONG_PTR               ReturnValue, FilterBitmap, ArgumentSize;
    NTSTATUS                Status;
    HpFilterStub            FilterStub;
    PSYSCALL_INFO           SystemCallInfo;
    SYSCALL_FILTER_INFO     FltInfo;
    PSYSTEM_CALL_FILTER     Filters;

    ReturnValue = 0;

    SystemCallInfo = Dispatcher->SystemCallInfo;

    if (SystemCallInfo == NULL)
    {
        SystemCallInfo = &Info->SystemCallInfo[Dispatcher->TableIndex][Dispatcher->ServiceIndex];
        Dispatcher->SystemCallInfo = SystemCallInfo;
    }

    ArgumentSize = SystemCallInfo->ArgumentSize;
    FltInfo.SsdtRoutine = Dispatcher->OriginalRoutine;

    Filters = SystemCallInfo->FilterCallbacks;
    if (Filters == NULL)
    {
        return ReturnValue;
    }

    FilterBitmap = SystemCallInfo->FilterBitmap;


    *UserModeFltInfo = FltInfo;
    UserModeFltInfo->Action = ContinueSystemCall;


    for (FltInfo.Action = ContinueSystemCall; FltInfo.Action == ContinueSystemCall; ++Filters)
    {
        ULONG Index;

        if (MlBitScanForwardPtr(&Index, FilterBitmap) == FALSE)
            break;

        FilterBitmap >>= Index + 1;
        Filters += Index;

        FilterStub = (HpFilterStub)Filters->Callback;
        if (FilterStub == NULL)
            continue;

        LocalArguments = _alloca(ArgumentSize);
        CopyMemory(LocalArguments, Arguments, ArgumentSize);

        UserModeFltInfo->FilterContext = Filters->Context;
        ReturnValue = FilterStub(SystemCallInfo, UserModeFltInfo);
        FltInfo.Action = UserModeFltInfo->Action;
    }

    switch (FltInfo.Action)
    {
        case ContinueSystemCall:
        case PermitSystemCall:

            break;
    }

    return ReturnValue;
}

VOID
HppSortHashTable(
    PSYSTEM_CALL_HASH_TABLE HashTable
)
{
    ULONG HashTableCount;
    PSYSTEM_CALL_HASH Entry;

    Entry = HashTable->Entry;

    for (ULONG i = HashTable->Count; i; --i)
    {
        for (ULONG j = 0; j != i - 1; ++j)
            if (Entry[j].Hash > Entry[j + 1].Hash)
                Swap(Entry[j], Entry[j + 1]);
    }
}

PSYSTEM_CALL_HASH
HpFindHashTableEntry(
    SYSTEM_CALL_HASH_TABLE *HashTable,
    ULONG                   Hash
)
{
    ULONG Lepft, Right, Middle;
    PSYSTEM_CALL_HASH Entry;

    Lepft    = 0;
    Right   = HashTable->Count - 1;

    if (Hash < HashTable->Entry[Lepft].Hash ||
        Hash > HashTable->Entry[Right].Hash)
    {
        return NULL;
    }

    if (Hash == HashTable->Entry[Right].Hash)
        return &HashTable->Entry[Right];

    while (Lepft < Right)
    {
        Middle  = (Right - Lepft) / 2 + Lepft;
        Entry   = &HashTable->Entry[Middle];

        if (Entry->Hash == Hash)
            return Entry;

        if (Entry->Hash < Hash)
        {
            Lepft = Middle + 1;
        }
        else
        {
            Right = Middle - 1;
        }
    }

    Entry = &HashTable->Entry[Lepft];

    return Entry->Hash == Hash ? Entry : NULL;
}

PSYSTEM_CALL_FILTER HpAllocateCallbackArray()
{
    return (PSYSTEM_CALL_FILTER)HpAlloc(
                MAX_FILTER_NUMBER * sizeof(SYSTEM_CALL_FILTER),
                HEAP_ZERO_MEMORY
           );
}

BOOL HpFreeCallbackArray(PSYSTEM_CALL_FILTER CallbackFilters)
{
    return HpFree(CallbackFilters);
}

NTSTATUS
HppInitializeSystemCallByEntry(
    PHOOK_PORT_GLOBAL_INFO      GlobalInfo,
    PSYSCALL_INFO               SysCall,
    PHP_SYSTEM_SERVICE_ENTRY    Entry
)
{
    ZeroMemory(SysCall, sizeof(*SysCall));

    SysCall->FunctionAddress    = NULL;
    SysCall->ArgumentSize       = Entry->ArgumentSize;
    SysCall->ReturnOpAddress    = NULL;
    SysCall->NameHash           = Entry->Hash;
    SysCall->ServiceIndex       = Entry->ServiceIndex;
    SysCall->ServiceTableIndex  = Entry->ServiceTableIndex;

    return STATUS_SUCCESS;
}

NTSTATUS
HppInitializeSystemCallByRoutine(
    PHOOK_PORT_GLOBAL_INFO  GlobalInfo,
    PSYSCALL_INFO           SysCall,
    PVOID                   Routine,
    ULONG                   RoutineHash
)
{
    PBYTE Function = (PBYTE)Routine;
    ULONG ServiceIndex;

    ZeroMemory(SysCall, sizeof(*SysCall));

    if (Function[0] != 0xB8)  // mov eax, const
        return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;

    ServiceIndex            = *(PULONG)&Function[1];
    SysCall->NameHash       = RoutineHash;
    SysCall->ServiceData    = ServiceIndex;


    SysCall->FunctionAddress = Function;

    do
    {
        Function += GetOpCodeSize(Function);

        //
        // 64 FF15 C0000000: call    dword ptr fs:[0xC0]
        //
        if (GlobalInfo->User.IsWow64 &&
            Function[0] == 0x64 &&
            *(PUSHORT)&Function[1] == 0x15FF &&
            *(PULONG)&Function[3]  != 0xC0
           )
        {
            return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
        }

    } while (Function[0] != 0xC2 && Function[0] != 0xC3);

    SysCall->ReturnOpAddress = (PVOID)Function;
    if (Function[0] == 0xC2)
        SysCall->ArgumentSize = ROUND_UP(*(PUSHORT)&Function[1], 4);


    return STATUS_SUCCESS;
}

PSYSCALL_INFO
HppLookupSystemCall(
    PHOOK_PORT_GLOBAL_INFO  Info,
    ULONG_PTR               SystemCallHash
)
{
    PSYSTEM_CALL_HASH Entry;

    if (Info == NULL)
        return NULL;

    Entry = HpFindHashTableEntry(&Info->HashTable, SystemCallHash);
    if (Entry == NULL)
        return NULL;

    return Entry->Entry;
}

NTSTATUS
HppAddSystemCall(
    PHOOK_PORT_GLOBAL_INFO  Info,
    PSYSCALL_INFO           SystemCall,
    ULONG_PTR               SystemCallCount
)
{
    ULONG               EntryCount, TableIndex;
    NTSTATUS            Status;
    PSYSCALL_INFO       SysCall, SysCallEntry;
    PSYSTEM_CALL_HASH   TableEntry;
    SYSTEM_CALL_HASH_TABLE NewTable, OldTable;

    EntryCount = Info->HashTable.Count;
    Status = HpAllocateVirtualMemory((PVOID *)&NewTable.Entry, (EntryCount + SystemCallCount) * sizeof(*NewTable.Entry));
    FAIL_RETURN(Status);

    TableEntry = &NewTable.Entry[EntryCount];
    FOR_EACH(SysCallEntry, SystemCall, SystemCallCount)
    {
        if (HppLookupSystemCall(Info, SysCallEntry->NameHash) != NULL)
        {
            --SystemCallCount;
            continue;
        }

        TableIndex = SysCallEntry->ServiceTableIndex;

        if (TableIndex == HP_WIN32K_SERVICE_INDEX && Info->SystemCallInfo[TableIndex] == NULL)
        {
            Status = HpAllocateVirtualMemory(
                        (PVOID *)&Info->SystemCallInfo[TableIndex],
                        HP_MAX_WIN32K_SERVICE_INDEX * sizeof(*SysCallEntry)
                    );
            if (NT_FAILED(Status))
            {
                HppDestroyHashTable(&NewTable);
                return Status;
            }
        }

        SysCall = &Info->SystemCallInfo[TableIndex][SysCallEntry->ServiceIndex];

        *SysCall = *SysCallEntry;

        TableEntry->Entry = SysCall;
        TableEntry->Hash = SysCallEntry->NameHash;
        ++TableEntry;

        ++Info->SystemCallCount[TableIndex];
    }

    CopyMemory(NewTable.Entry, Info->HashTable.Entry, EntryCount * sizeof(*NewTable.Entry));
    NewTable.Count = EntryCount + SystemCallCount;

    HppSortHashTable(&NewTable);

    OldTable = Info->HashTable;

#if ML_X86

    InterlockedExchangeDoublePointer(&Info->HashTable, &NewTable);

#else

    #error

#endif

    HppDestroyHashTable(&OldTable);

    return STATUS_SUCCESS;
}

NTSTATUS
HppAddSystemCallByRoutineRange(
    PHOOK_PORT_GLOBAL_INFO  Info,
    PVOID*                  Routine,
    PULONG                  RoutineHash,
    ULONG_PTR               Count
)
{
    NTSTATUS        Status;
    SYSCALL_INFO    LocalBuffer[0x20];
    PSYSCALL_INFO   SysCall, SysCallBase;

    if (Count == 0)
        return STATUS_INVALID_PARAMETER;

    if (Count < countof(LocalBuffer))
    {
        SysCallBase = LocalBuffer;
    }
    else
    {
        SysCallBase = (PSYSCALL_INFO)HpAlloc(Count * sizeof(*SysCall));
        if (SysCallBase == NULL)
            return STATUS_NO_MEMORY;
    }

    Status = STATUS_SUCCESS;

    SysCall = SysCallBase;
    FOR_EACH(Routine, Routine, Count)
    {
        Status = HppInitializeSystemCallByRoutine(Info, SysCall, *Routine, *RoutineHash);
        FAIL_BREAK(Status);

        ++SysCall;
        ++RoutineHash;
    }

    if (NT_SUCCESS(Status))
        Status = HppAddSystemCall(Info, SysCallBase, Count);

    if (SysCallBase != LocalBuffer)
        HpFree(SysCallBase);

    return Status;
}

NTSTATUS
HppAddSystemCallByRoutine(
    PHOOK_PORT_GLOBAL_INFO  Info,
    PVOID                   Routine,
    ULONG                   RoutineHash
)
{
    NTSTATUS        Status;
    SYSCALL_INFO    SysCall;

    return HppAddSystemCallByRoutineRange(Info, &Routine, &RoutineHash, 1);
}

NTSTATUS
HppAddSystemCallFilter(
    PHOOK_PORT_GLOBAL_INFO  Info,
    ULONG                   RoutineHash,
    PVOID                   Routine,
    PVOID                   Context
)
{
    ULONG                   EmptyPos, Bitmap;
    PSYSTEM_CALL_FILTER     FilterCallbacks;
    PSYSCALL_INFO           Filter;

    if (Info == NULL)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    if (Routine == NULL)
        return STATUS_INVALID_PARAMETER;

    Filter = HppLookupSystemCall(Info, RoutineHash);
    if (Filter == NULL)
        return STATUS_HOOK_PORT_ENTRY_NOT_FOUND;

    FilterCallbacks = Filter->FilterCallbacks;
    if (FilterCallbacks == NULL)
    {
        Filter->FilterBitmap = 0;
        FilterCallbacks = HpAllocateCallbackArray();
        if (FilterCallbacks == NULL)
            return STATUS_NO_MEMORY;

        Filter->FilterCallbacks = FilterCallbacks;
    }

    Bitmap = Filter->FilterBitmap;
    if (Bitmap == -1)
        return STATUS_HOOK_PORT_TOO_MANY_FILTERS;

    EmptyPos = _BitScanForward(&EmptyPos, ~Bitmap) ? EmptyPos : 0;

    FilterCallbacks += EmptyPos;
    FilterCallbacks->Callback   = Routine;
    FilterCallbacks->Context   = Context;

    SET_FLAG(Filter->Flags, SystemCallFilterEnable);
    SET_FLAG(Filter->FilterBitmap, 1 << EmptyPos);

    return STATUS_SUCCESS;
}

NTSTATUS
HppRemoveSystemCallFilter(
    PHOOK_PORT_GLOBAL_INFO  Info,
    ULONG                   RoutineHash,
    PVOID                   Routine
)
{
    ULONG                   Bitmap, Index;
    PSYSTEM_CALL_FILTER     FilterCallbacks;
    PSYSCALL_INFO           Filter;

    if (Info == NULL)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    Filter = HppLookupSystemCall(Info, RoutineHash);
    if (Filter == NULL)
        return STATUS_HOOK_PORT_ENTRY_NOT_FOUND;

    FilterCallbacks = Filter->FilterCallbacks;
    if (FilterCallbacks == NULL)
        return STATUS_HOOK_PORT_FILTER_NOT_FOUND;

    Bitmap = Filter->FilterBitmap;
    if (Bitmap == 0)
        return STATUS_HOOK_PORT_FILTER_NOT_FOUND;

    while (MlBitScanForwardPtr(&Index, Bitmap))
    {
        Bitmap >>= Index + 1;
        FilterCallbacks += Index;

        if (FilterCallbacks->Callback != Routine)
        {
            ++FilterCallbacks;
            continue;
        }

        FilterCallbacks->Callback = NULL;

        CLEAR_FLAG(Filter->FilterBitmap, 1 << (FilterCallbacks - Filter->FilterCallbacks));

        if (Filter->FilterBitmap == 0)
            CLEAR_FLAG(Filter->Flags, SystemCallFilterEnable);

        return STATUS_SUCCESS;
    }

    return STATUS_HOOK_PORT_FILTER_NOT_FOUND;
}

PSYSCALL_INFO
HpLookupSystemCall(
    ULONG_PTR SystemCallHash
)
{
    return HppLookupSystemCall(HppGetGlobalInfo(), SystemCallHash);
}

NTSTATUS
HpAddSystemCallByRoutineRange(
    PVOID*      Routine,
    PULONG      RoutineHash,
    ULONG_PTR   Count
)
{
    return HppAddSystemCallByRoutineRange(HppGetGlobalInfo(), Routine, RoutineHash, Count);
}

NTSTATUS
HpAddSystemCallByRoutine(
    PVOID Routine,
    ULONG RoutineHash
)
{
    return HppAddSystemCallByRoutine(HppGetGlobalInfo(), Routine, RoutineHash);
}

NTSTATUS
HpAddSystemCall(
    PSYSCALL_INFO   SystemCall,
    ULONG_PTR       Count
)
{
    return HppAddSystemCall(HppGetGlobalInfo(), SystemCall, Count);
}

NTSTATUS HppAddSystemServiceTable(PHOOK_PORT_GLOBAL_INFO Info, PHP_SYSTEM_SERVICE_TABLE ServiceTable)
{
    NTSTATUS                    Status;
    PSYSCALL_INFO               SysCallBase, SysCall;
    PHP_SYSTEM_SERVICE_ENTRY    Entry;

    SysCallBase = (PSYSCALL_INFO)HpAlloc(ServiceTable->Number * sizeof(*SysCallBase));
    if (SysCallBase == NULL)
        return STATUS_NO_MEMORY;

    SysCall = SysCallBase;
    FOR_EACH(Entry, ServiceTable->Entries, ServiceTable->Number)
    {
        HppInitializeSystemCallByEntry(Info, SysCall, Entry);
        ++SysCall;
    }

    Status = HppAddSystemCall(Info, SysCallBase, SysCall - SysCallBase);

    HpFree(SysCallBase);

    return Status;
}

NTSTATUS HppInitializeWin32kPort(PHOOK_PORT_GLOBAL_INFO Info)
{


    return STATUS_NOT_IMPLEMENTED;


}

NTSTATUS HpAddSystemServiceTable(PHP_SYSTEM_SERVICE_TABLE ServiceTable)
{
    return HppAddSystemServiceTable(HppGetGlobalInfo(), ServiceTable);
}

NTSTATUS HpInitializeWin32kPort()
{
    return HppInitializeWin32kPort(HppGetGlobalInfo());
}

NTSTATUS
HpAddSystemCallFilter(
    ULONG RoutineHash,
    PVOID Routine,
    PVOID Context  /* = NULL */
)
{
    return HppAddSystemCallFilter(HppGetGlobalInfo(), RoutineHash, Routine, Context);
}

NTSTATUS
HpRemoveSystemCallFilter(
    ULONG RoutineHash,
    PVOID Routine
)
{
    return HppRemoveSystemCallFilter(HppGetGlobalInfo(), RoutineHash, Routine);
}

NTSTATUS HpSetGlobalFilter(PHP_GLOBAL_FILTER NewFilter, PHP_GLOBAL_FILTER OldFilter)
{
    PHOOK_PORT_GLOBAL_INFO Info;

    Info = HppGetGlobalInfo();
    if (Info == NULL)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    if (OldFilter != NULL)
        *OldFilter = Info->GlobalFilter;

    InterlockedExchangeDoublePointer(&Info->GlobalFilter, NewFilter);

    return STATUS_SUCCESS;
}

NTSTATUS
HpQueryValue(
    HpValueType Type,
    PVOID*      Value
)
{
    PHOOK_PORT_GLOBAL_INFO Info = HppGetGlobalInfo();

    *Value = NULL;

    if (Info == NULL)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;


    return STATUS_NOT_SUPPORTED;


}

ForceInline ULONG HashNativeZwAPI(PCChar Name)
{
    ULONG Hash = 0;

//    Hash = _rotl(Hash, 0x0D) ^ 'N';
//    Hash = _rotl(Hash, 0x0D) ^ 't';
    Hash = 0x0009C074;

    Name += 2;

    while (*(PByte)Name)
    {
        Hash = _rotl(Hash, 0x0D) ^ *(PByte)Name++;
    }

    return Hash;
}


VOID HppDestroyHashTable(PSYSTEM_CALL_HASH_TABLE Table)
{
    if (Table->Entry != NULL)
        HpFreeVirtualMemory(Table->Entry);
}

NTSTATUS
InstallHookPort(
    PLDR_MODULE SysCallModule,  // = NULL
    ULONG       Flags           // = 0
)
{
    BOOL                        IsWow64;
    NTSTATUS                    Status;
    PVOID                       NtdllModule;
    PBYTE                       Function;
    PSTR                        FunctionName;
    ULONG_PTR                   HashTableCount, BaseAddress, Offset;
    PULONG_PTR                  AddressOfNames, AddressOfFunctions;
    PUSHORT                     AddressOfNameOrdinals;
    PIMAGE_DOS_HEADER           DosHeader;
    PIMAGE_NT_HEADERS           NtHeader;
    PIMAGE_EXPORT_DIRECTORY     ExportDirectory;
    PSYSCALL_INFO               SysCallInfo, FilterEntry;
    PSYSTEM_CALL_HASH           SysCallHash;
    PLDR_MODULE                 LdrModule;
    PHOOK_PORT_GLOBAL_INFO      GlobalInfo;

    FAIL_RETURN(ml::MlInitialize());


    GlobalInfo = HppGetGlobalInfo();
    if (GlobalInfo != NULL)
    {
        _InterlockedIncrementPtr(&GlobalInfo->RefCount);
        return STATUS_SUCCESS;
    }

    GlobalInfo = (HOOK_PORT_GLOBAL_INFO *)HpAlloc(sizeof(*GlobalInfo));
    if (GlobalInfo == NULL)
        return STATUS_NO_MEMORY;

    ZeroMemory(GlobalInfo, sizeof(*GlobalInfo));

    GlobalInfo->MaxSystemCallCount[HP_NTKRNL_SERVICE_INDEX] = HP_MAX_SERVICE_INDEX;
    GlobalInfo->MaxSystemCallCount[HP_WIN32K_SERVICE_INDEX] = HP_MAX_WIN32K_SERVICE_INDEX;

    BaseAddress = NULL;


    Status = HpAllocateVirtualMemory((PVOID *)&GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX], HP_MAX_SERVICE_INDEX * sizeof(*GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX]));

    if (!NT_SUCCESS(Status))
        goto InstallHookPort_Failure;

    Status = HpAllocateVirtualMemory((PVOID *)&GlobalInfo->HashTable.Entry, HP_MAX_SERVICE_INDEX * sizeof(*GlobalInfo->HashTable.Entry));

    if (!NT_SUCCESS(Status))
        goto InstallHookPort_Failure;



    LdrModule = GetNtdllLdrModule();
    NtdllModule = LdrModule->DllBase;
    Status = LoadPeImage(LdrModule->FullDllName.Buffer, (PVOID *)&BaseAddress, NtdllModule, LOAD_PE_IGNORE_IAT);
    if (!NT_SUCCESS(Status))
        goto InstallHookPort_Failure;

    GlobalInfo->User.KiFastSystemCall = EATLookupRoutineByHashPNoFix((PVOID)BaseAddress, NTDLL_KiFastSystemCall);
    GlobalInfo->User.KiFastSystemCall = PtrAdd(GlobalInfo->User.KiFastSystemCall, PtrOffset(LdrModule->DllBase, BaseAddress));

    IsWow64 = Ps::IsWow64Process();
    GlobalInfo->User.IsWow64 = IsWow64;


    Offset                  = PtrOffset(NtdllModule, BaseAddress);
    DosHeader               = (PIMAGE_DOS_HEADER)BaseAddress;
    NtHeader                = (PIMAGE_NT_HEADERS)((ULONG_PTR)DosHeader + DosHeader->e_lfanew);
    ExportDirectory         = (PIMAGE_EXPORT_DIRECTORY)(NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress + BaseAddress);
    AddressOfNames          = (PULONG_PTR)(ExportDirectory->AddressOfNames + BaseAddress);
    AddressOfFunctions      = (PULONG_PTR)(ExportDirectory->AddressOfFunctions + BaseAddress);
    AddressOfNameOrdinals   = (PUSHORT)(ExportDirectory->AddressOfNameOrdinals + BaseAddress);

    SysCallInfo = GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX];
    SysCallHash = GlobalInfo->HashTable.Entry;
    for (ULONG_PTR Count = ExportDirectory->NumberOfNames; Count; ++AddressOfNames, ++AddressOfNameOrdinals, --Count)
    {
        ULONG_PTR ServiceIndex;

        FunctionName = (LPSTR)(*AddressOfNames + BaseAddress);
        if (FunctionName[0] != 'Z' || FunctionName[1] != 'w')
            continue;

        // ignore "Zw*CHPE" functions. "CHPE" is introduced in Windows 10 Redstone 3 update, which translates ARM64 codes to amd64.
        PCSTR FunctionNameEnd = FunctionName;
        while (*FunctionNameEnd != 0)
            ++FunctionNameEnd;

        if (FunctionNameEnd - FunctionName >= 6 &&
            FunctionNameEnd[-4] == 'C' &&
            FunctionNameEnd[-3] == 'H' &&
            FunctionNameEnd[-2] == 'P' &&
            FunctionNameEnd[-1] == 'E')
        {
            continue;
        }

        Function = (PBYTE)(AddressOfFunctions[*AddressOfNameOrdinals] + BaseAddress);

        if (Function[0] != 0xB8)  // mov eax, const
        {
            Status = STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
            goto InstallHookPort_Failure;
        }

        ServiceIndex    = *(PULONG)&Function[1];
        FilterEntry     = &SysCallInfo[ServiceIndex & 0xFFFF];

        SysCallHash->Entry  = FilterEntry;
        SysCallHash->Hash   = HashNativeZwAPI(FunctionName);

        Status = HppInitializeSystemCallByRoutine(GlobalInfo, FilterEntry, Function, SysCallHash->Hash);
        if (NT_FAILED(Status))
            goto InstallHookPort_Failure;

        FilterEntry->FunctionAddress    = PtrAdd(FilterEntry->FunctionAddress, Offset);
        FilterEntry->ReturnOpAddress    = PtrAdd(FilterEntry->ReturnOpAddress, Offset);
        FilterEntry->FunctionName       = PtrAdd(FunctionName, Offset);

        ++SysCallHash;
    }

    UnloadPeImage((PVOID)BaseAddress);

    //HppInitializeWin32kPort(GlobalInfo);

    HashTableCount = SysCallHash - GlobalInfo->HashTable.Entry;
    GlobalInfo->HashTable.Count = HashTableCount;
    GlobalInfo->SystemCallCount[HP_NTKRNL_SERVICE_INDEX] = HashTableCount;

    HppSortHashTable(&GlobalInfo->HashTable);


    if (IsWow64)
    {
        Status = HookSysCall_Wow64(GlobalInfo);
    }
    else
    {
        Status = HookSysCall_x86(GlobalInfo);
    }


    if (!NT_SUCCESS(Status))
        goto InstallHookPort_Failure;

    HppSetGlobalInfo(GlobalInfo);
    _InterlockedIncrementPtr(&GlobalInfo->RefCount);

    return STATUS_SUCCESS;

InstallHookPort_Failure:

    if (BaseAddress != NULL)
        UnloadPeImage((PVOID)BaseAddress);

    UnInstallHookPortInternal(GlobalInfo);
    return Status;
}

NTSTATUS UnInstallHookPort()
{
    ULONG                   RefCount;
    HOOK_PORT_GLOBAL_INFO  *GlobalInfo;

    GlobalInfo = HppGetGlobalInfo();
    if (GlobalInfo == NULL)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    RefCount = _InterlockedDecrementPtr(&GlobalInfo->RefCount);
    if (RefCount != 0)
        return RefCount;

    HppSetGlobalInfo(NULL);

    return UnInstallHookPortInternal(GlobalInfo);
}

NTSTATUS UnInstallHookPortInternal(PHOOK_PORT_GLOBAL_INFO Info)
{

    if (Info->User.SysEnterSize == -1)
    {
        UnHookSysCall_Wow64(Info);
    }
    else if (Info->User.SysEnterSize != 0)
    {
        UnHookSysCall_x86(Info);
    }


    PSYSCALL_INFO* SysCallInfo;
    PULONG_PTR SystemCallCount = Info->MaxSystemCallCount - 1;

    FOR_EACH(SysCallInfo, Info->SystemCallInfo, countof(Info->SystemCallInfo))
    {
        ++SystemCallCount;

        PSYSCALL_INFO SysCall = *SysCallInfo;

        if (SysCall == NULL)
            continue;

        for (ULONG Count = *SystemCallCount; Count; --Count)
        {
            if (SysCall->FilterCallbacks != NULL)
                HpFreeCallbackArray(SysCall->FilterCallbacks);

            ++SysCall;
        }

        HpFreeVirtualMemory(*SysCallInfo);
    }

    HppDestroyHashTable(&Info->HashTable);

    HpFree(Info);

    return STATUS_SUCCESS;
}

#endif // amd64
