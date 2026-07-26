#include "stdafx.h"

namespace
{
#if ML_AMD64
    enum
    {
        X64_SYSCALL_STUB_SIZE = 0x20,
        X64_SYSCALL_RELATIVE_JUMP_SIZE = 5,
        SYSCALL_PATCH_SIZE = 12,
    };

    struct SYSCALL_PATCH
    {
        PSYSCALL_INFO SysCall;
        PVOID Target;
        PVOID Original;
        PVOID Relay;
        BYTE Backup[SYSCALL_PATCH_SIZE];
    };
#else
    struct SYSCALL_PATCH
    {
        PSYSCALL_INFO SysCall;
        PVOID Target;
        PVOID Original;
    };
#endif

    PHOOK_PORT_GLOBAL_INFO g_GlobalInfo;
    SYSCALL_PATCH* g_Patches;
    ULONG_PTR g_PatchCount;
    ULONG_PTR g_PatchCapacity;

    ForceInline PHOOK_PORT_GLOBAL_INFO HppGetGlobalInfo()
    {
        return g_GlobalInfo;
    }

    ForceInline PHOOK_PORT_GLOBAL_INFO HppSetGlobalInfo(PHOOK_PORT_GLOBAL_INFO Info)
    {
        g_GlobalInfo = Info;
        return Info;
    }

    PVOID HpAlloc(ULONG_PTR Size, ULONG Flags = 0)
    {
        return AllocateMemory(Size, Flags);
    }

    BOOL HpFree(PVOID Memory)
    {
        return FreeMemory(Memory);
    }

    NTSTATUS HpAllocateVirtualMemory(PVOID* Address, ULONG_PTR Size)
    {
        *Address = nullptr;
        return Mm::AllocVirtualMemory(Address, Size);
    }

    NTSTATUS HpFreeVirtualMemory(PVOID Address)
    {
        return Mm::FreeVirtualMemory(Address);
    }

    ForceInline ULONG HashNativeZwAPI(PCChar Name)
    {
        ULONG Hash = 0x0009C074;

        Name += 2;

        while (*(PByte)Name)
        {
            Hash = _rotl(Hash, 0x0D) ^ *(PByte)Name++;
        }

        return Hash;
    }

    BOOL ParseSystemCallStub(
        PBYTE Function,
        PULONG ServiceIndex,
        PVOID* ReturnOpAddress,
        PBOOLEAN HasInt2ESelector = nullptr
    )
    {
#if ML_AMD64
        const static BYTE SystemCallSelector[] =
        {
            0xF6, 0x04, 0x25, 0x08, 0x03, 0xFE, 0x7F, 0x01,
            0x75, 0x03,
        };
        const static BYTE Int2EReturn[] = { 0xCD, 0x2E, 0xC3 };

        if (HasInt2ESelector != nullptr)
            *HasInt2ESelector = FALSE;

        for (ULONG_PTR i = 0; i != 8; ++i)
        {
            if (Function[i + 0] == 0x4C &&
                Function[i + 1] == 0x8B &&
                Function[i + 2] == 0xD1 &&
                Function[i + 3] == 0xB8)
            {
                for (ULONG_PTR j = i + 8; j + 2 < X64_SYSCALL_STUB_SIZE; ++j)
                {
                    if (Function[j + 0] == 0x0F &&
                        Function[j + 1] == 0x05 &&
                        Function[j + 2] == 0xC3)
                    {
                        *ServiceIndex = *(PULONG)&Function[i + 4];
                        *ReturnOpAddress = &Function[j + 2];

                        if (
                             HasInt2ESelector != nullptr &&
                             j >= countof(SystemCallSelector) &&
                             j + 5 < X64_SYSCALL_STUB_SIZE &&
                             RtlEqualMemory(&Function[j - countof(SystemCallSelector)], SystemCallSelector, sizeof(SystemCallSelector)) &&
                             RtlEqualMemory(&Function[j + 3], Int2EReturn, sizeof(Int2EReturn))
                           )
                        {
                            *HasInt2ESelector = TRUE;
                        }

                        return TRUE;
                    }
                }
            }
        }

        return FALSE;
#else
        if (HasInt2ESelector != nullptr)
            *HasInt2ESelector = FALSE;

        if (Function[0] != 0xB8)
            return FALSE;

        *ServiceIndex = *(PULONG)&Function[1];
        *ReturnOpAddress = nullptr;
        return TRUE;
#endif
    }

    NTSTATUS HppInitializeSystemCallByRoutine(PSYSCALL_INFO SysCall, PVOID Routine, ULONG RoutineHash)
    {
        ULONG ServiceIndex;
        PVOID ReturnOpAddress;
        BOOLEAN HasInt2ESelector;

        ZeroMemory(SysCall, sizeof(*SysCall));

        if (!ParseSystemCallStub((PBYTE)Routine, &ServiceIndex, &ReturnOpAddress, &HasInt2ESelector))
            return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;

        SysCall->NameHash = RoutineHash;
        SysCall->ServiceData = ServiceIndex;
        SysCall->FunctionAddress = Routine;
        if (HasInt2ESelector)
            SET_FLAG(SysCall->Flags, SystemCallHasInt2ESelector);

        return STATUS_SUCCESS;
    }

    VOID HppDestroyHashTable(PSYSTEM_CALL_HASH_TABLE Table)
    {
        if (Table->Entry != nullptr)
            HpFreeVirtualMemory(Table->Entry);
    }

    VOID HppSortHashTable(PSYSTEM_CALL_HASH_TABLE HashTable)
    {
        PSYSTEM_CALL_HASH Entry = HashTable->Entry;

        for (ULONG i = (ULONG)HashTable->Count; i; --i)
        {
            for (ULONG j = 0; j != i - 1; ++j)
            {
                if (Entry[j].Hash > Entry[j + 1].Hash)
                    Swap(Entry[j], Entry[j + 1]);
            }
        }
    }

    PSYSTEM_CALL_HASH HpFindHashTableEntry(SYSTEM_CALL_HASH_TABLE* HashTable, ULONG Hash)
    {
        ULONG Lepft, Right, Middle;
        PSYSTEM_CALL_HASH Entry;

        if (HashTable->Count == 0)
            return nullptr;

        Lepft = 0;
        Right = (ULONG)HashTable->Count - 1;

        if (Hash < HashTable->Entry[Lepft].Hash || Hash > HashTable->Entry[Right].Hash)
            return nullptr;

        if (Hash == HashTable->Entry[Right].Hash)
            return &HashTable->Entry[Right];

        while (Lepft < Right)
        {
            Middle = (Right - Lepft) / 2 + Lepft;
            Entry = &HashTable->Entry[Middle];

            if (Entry->Hash == Hash)
                return Entry;

            if (Entry->Hash < Hash)
                Lepft = Middle + 1;
            else
                Right = Middle - 1;
        }

        Entry = &HashTable->Entry[Lepft];
        return Entry->Hash == Hash ? Entry : nullptr;
    }

    PSYSCALL_INFO HppLookupSystemCall(PHOOK_PORT_GLOBAL_INFO Info, ULONG_PTR SystemCallHash)
    {
        PSYSTEM_CALL_HASH Entry;

        if (Info == nullptr)
            return nullptr;

        Entry = HpFindHashTableEntry(&Info->HashTable, (ULONG)SystemCallHash);
        if (Entry == nullptr)
            return nullptr;

        return Entry->Entry;
    }

    PSYSTEM_CALL_FILTER HpAllocateCallbackArray()
    {
        return (PSYSTEM_CALL_FILTER)HpAlloc(MAX_FILTER_NUMBER * sizeof(SYSTEM_CALL_FILTER), HEAP_ZERO_MEMORY);
    }

    BOOL HpFreeCallbackArray(PSYSTEM_CALL_FILTER CallbackFilters)
    {
        return HpFree(CallbackFilters);
    }

#if ML_AMD64
    PVOID AllocateSyscallStub(PSYSCALL_INFO SysCall)
    {
        const static BYTE DirectTail[] =
        {
            0x0F, 0x05, 0xC3,
        };
        const static BYTE SelectorTail[] =
        {
            0xF6, 0x04, 0x25, 0x08, 0x03, 0xFE, 0x7F, 0x01,
            0x75, 0x03,
            0x0F, 0x05, 0xC3,
            0xCD, 0x2E, 0xC3,
        };
        PBYTE Stub = nullptr;
        NTSTATUS Status;

        Status = AllocVirtualMemory((PVOID*)&Stub, X64_SYSCALL_STUB_SIZE, PAGE_EXECUTE_READWRITE);
        if (NT_FAILED(Status))
            return nullptr;

        Stub[0] = 0x4C;
        Stub[1] = 0x8B;
        Stub[2] = 0xD1;
        Stub[3] = 0xB8;
        *(PULONG)&Stub[4] = SysCall->ServiceData;
        if (FLAG_ON(SysCall->Flags, SystemCallHasInt2ESelector))
            CopyMemory(&Stub[8], SelectorTail, sizeof(SelectorTail));
        else
            CopyMemory(&Stub[8], DirectTail, sizeof(DirectTail));
        NtFlushInstructionCache(CurrentProcess, Stub, X64_SYSCALL_STUB_SIZE);

        return Stub;
    }

    BOOL GetRelativeJumpOffset(PVOID Address, PVOID Target, PLONG RelativeOffset)
    {
        LONGLONG Offset;

        Offset = (LONGLONG)(ULONG_PTR)Target -
                 (LONGLONG)((ULONG_PTR)Address + X64_SYSCALL_RELATIVE_JUMP_SIZE);
        if (Offset < -0x80000000LL || Offset > 0x7FFFFFFFLL)
            return FALSE;

        *RelativeOffset = (LONG)Offset;
        return TRUE;
    }

    VOID WriteAbsoluteJumpCode(PBYTE Code, PVOID Target)
    {
        Code[0] = 0x48;
        Code[1] = 0xB8;
        *(PVOID*)&Code[2] = Target;
        *(PUSHORT)&Code[10] = 0xE0FF;
    }

    ULONG_PTR AlignDownTo(ULONG_PTR Value, ULONG_PTR Alignment)
    {
        return Value & ~(Alignment - 1);
    }

    ULONG_PTR AlignUpTo(ULONG_PTR Value, ULONG_PTR Alignment)
    {
        return AlignDownTo(Value + Alignment - 1, Alignment);
    }

    BOOL MatchX64Nop(PBYTE Address, PBYTE End, PULONG_PTR Length)
    {
        static const BYTE Nop2[] = { 0x66, 0x90 };
        static const BYTE Nop3[] = { 0x0F, 0x1F, 0x00 };
        static const BYTE Nop4[] = { 0x0F, 0x1F, 0x40, 0x00 };
        static const BYTE Nop5[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
        static const BYTE Nop6[] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
        static const BYTE Nop7[] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };
        static const BYTE Nop8[] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
        static const BYTE Nop9[] = { 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };

        struct NOP_PATTERN
        {
            const BYTE* Bytes;
            ULONG_PTR Length;
        };

        static const NOP_PATTERN Patterns[] =
        {
            { Nop9, sizeof(Nop9) },
            { Nop8, sizeof(Nop8) },
            { Nop7, sizeof(Nop7) },
            { Nop6, sizeof(Nop6) },
            { Nop5, sizeof(Nop5) },
            { Nop4, sizeof(Nop4) },
            { Nop3, sizeof(Nop3) },
            { Nop2, sizeof(Nop2) },
        };

        for (ULONG_PTR i = 0; i != countof(Patterns); ++i)
        {
            if (Address + Patterns[i].Length <= End &&
                RtlEqualMemory(Address, Patterns[i].Bytes, Patterns[i].Length))
            {
                *Length = Patterns[i].Length;
                return TRUE;
            }
        }

        return FALSE;
    }

    BOOL IsX64PaddingRange(PBYTE Begin, PBYTE End)
    {
        while (Begin < End)
        {
            ULONG_PTR NopLength;

            if (Begin[0] == 0x90 || Begin[0] == 0xCC)
            {
                ++Begin;
                continue;
            }

            if (MatchX64Nop(Begin, End, &NopLength))
            {
                Begin += NopLength;
                continue;
            }

            return FALSE;
        }

        return TRUE;
    }

    BOOL CanUseAbsoluteJumpPatch(PBYTE Address)
    {
        ULONG ServiceIndex;
        PVOID ReturnOpAddress;
        PBYTE AfterReturn;

        if (!ParseSystemCallStub(Address, &ServiceIndex, &ReturnOpAddress))
            return FALSE;

        if ((ULONG_PTR)ReturnOpAddress >= (ULONG_PTR)Address + SYSCALL_PATCH_SIZE - 1)
            return TRUE;

        AfterReturn = (PBYTE)PtrAdd(ReturnOpAddress, 1);
        return IsX64PaddingRange(AfterReturn, PtrAdd(Address, SYSCALL_PATCH_SIZE));
    }

    NTSTATUS TryAllocateRelayStub(PVOID EntryAddress, PVOID CandidateBase, PVOID Target, PVOID* Relay)
    {
        PVOID Base;
        SIZE_T Size;
        NTSTATUS Status;
        LONG RelativeOffset;

        Base = CandidateBase;
        Size = X64_SYSCALL_STUB_SIZE;
        Status = NtAllocateVirtualMemory(CurrentProcess, &Base, 0, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (NT_FAILED(Status))
            return Status;

        if (!GetRelativeJumpOffset(EntryAddress, Base, &RelativeOffset))
        {
            Mm::FreeVirtualMemory(Base);
            return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
        }

        WriteAbsoluteJumpCode((PBYTE)Base, Target);
        NtFlushInstructionCache(CurrentProcess, Base, SYSCALL_PATCH_SIZE);
        *Relay = Base;

        return STATUS_SUCCESS;
    }

    NTSTATUS AllocateRelayStub(PVOID Address, PVOID Target, PVOID* Relay)
    {
        ULONG_PTR Near, Low, High, Query, Next;
        MEMORY_BASIC_INFORMATION MemoryInfo;
        NTSTATUS Status;

        static const ULONG_PTR AllocationGranularity = 0x10000;
        static const ULONG_PTR MinUserAddress = 0x10000;
        static const ULONG_PTR MaxRange = 0x7FFFFFFF;

        Near = (ULONG_PTR)Address;
        Low = Near > MaxRange ? Near - MaxRange : MinUserAddress;
        if (Low < MinUserAddress)
            Low = MinUserAddress;

        High = Near + MaxRange;
        if (High < Near)
            High = MAXULONG_PTR;

        Query = AlignDownTo(Near, AllocationGranularity);
        while (Query > Low)
        {
            Status = NtQueryVirtualMemory(CurrentProcess, (PVOID)Query, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), nullptr);
            if (NT_FAILED(Status))
            {
                Query -= Query >= AllocationGranularity ? AllocationGranularity : Query;
                continue;
            }

            if (MemoryInfo.State == MEM_FREE)
            {
                Status = TryAllocateRelayStub(Address, (PVOID)Query, Target, Relay);
                if (NT_SUCCESS(Status))
                    return Status;
            }

            if ((ULONG_PTR)MemoryInfo.BaseAddress <= Low)
                break;

            Query = AlignDownTo((ULONG_PTR)MemoryInfo.BaseAddress - 1, AllocationGranularity);
        }

        Query = AlignUpTo(Near, AllocationGranularity);
        while (Query < High)
        {
            Status = NtQueryVirtualMemory(CurrentProcess, (PVOID)Query, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), nullptr);
            if (NT_FAILED(Status))
            {
                Query += AllocationGranularity;
                continue;
            }

            if (MemoryInfo.State == MEM_FREE)
            {
                Status = TryAllocateRelayStub(Address, (PVOID)Query, Target, Relay);
                if (NT_SUCCESS(Status))
                    return Status;
            }

            Next = (ULONG_PTR)MemoryInfo.BaseAddress + MemoryInfo.RegionSize;
            if (Next <= Query)
                Query += AllocationGranularity;
            else
                Query = AlignUpTo(Next, AllocationGranularity);
        }

        return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
    }

    NTSTATUS WriteAbsoluteJump(PVOID Address, PVOID Target)
    {
        BYTE Code[SYSCALL_PATCH_SIZE];
        ULONG Protect;
        NTSTATUS Status;

        WriteAbsoluteJumpCode(Code, Target);

        Status = ProtectVirtualMemory(Address, SYSCALL_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &Protect);
        FAIL_RETURN(Status);

        CopyMemory(Address, Code, sizeof(Code));
        NtFlushInstructionCache(CurrentProcess, Address, SYSCALL_PATCH_SIZE);

        if (Protect != PAGE_EXECUTE_READWRITE)
            ProtectVirtualMemory(Address, SYSCALL_PATCH_SIZE, Protect, &Protect);

        return STATUS_SUCCESS;
    }

    NTSTATUS WriteRelativeJump(PVOID Address, PVOID Target, SYSCALL_PATCH* Patch)
    {
        BYTE Code[X64_SYSCALL_RELATIVE_JUMP_SIZE];
        PVOID JumpTarget;
        LONG RelativeOffset;
        ULONG Protect;
        NTSTATUS Status;

        JumpTarget = Target;
        if (!GetRelativeJumpOffset(Address, JumpTarget, &RelativeOffset))
        {
            Status = AllocateRelayStub(Address, Target, &Patch->Relay);
            FAIL_RETURN(Status);

            JumpTarget = Patch->Relay;
            if (!GetRelativeJumpOffset(Address, JumpTarget, &RelativeOffset))
                return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
        }

        Code[0] = 0xE9;
        *(PLONG)&Code[1] = RelativeOffset;

        Status = ProtectVirtualMemory(Address, X64_SYSCALL_RELATIVE_JUMP_SIZE, PAGE_EXECUTE_READWRITE, &Protect);
        FAIL_RETURN(Status);

        CopyMemory(Address, Code, sizeof(Code));
        NtFlushInstructionCache(CurrentProcess, Address, X64_SYSCALL_RELATIVE_JUMP_SIZE);

        if (Protect != PAGE_EXECUTE_READWRITE)
            ProtectVirtualMemory(Address, X64_SYSCALL_RELATIVE_JUMP_SIZE, Protect, &Protect);

        return STATUS_SUCCESS;
    }

    NTSTATUS WriteSystemCallJump(PVOID Address, PVOID Target, SYSCALL_PATCH* Patch)
    {
        NTSTATUS Status;

        CopyMemory(Patch->Backup, Address, SYSCALL_PATCH_SIZE);

        Status = WriteRelativeJump(Address, Target, Patch);
        if (NT_SUCCESS(Status))
            return Status;

        if (CanUseAbsoluteJumpPatch((PBYTE)Address))
            return WriteAbsoluteJump(Address, Target);

        return Status;
    }
#endif

    NTSTATUS RestorePatch(SYSCALL_PATCH* Patch)
    {
#if ML_AMD64
        ULONG Protect;
        NTSTATUS Status;

        if (Patch == nullptr || Patch->SysCall == nullptr)
            return STATUS_SUCCESS;

        Status = ProtectVirtualMemory(Patch->Target, SYSCALL_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &Protect);
        FAIL_RETURN(Status);

        CopyMemory(Patch->Target, Patch->Backup, SYSCALL_PATCH_SIZE);
        NtFlushInstructionCache(CurrentProcess, Patch->Target, SYSCALL_PATCH_SIZE);

        if (Protect != PAGE_EXECUTE_READWRITE)
            ProtectVirtualMemory(Patch->Target, SYSCALL_PATCH_SIZE, Protect, &Protect);

        if (Patch->Original != nullptr)
            Mm::FreeVirtualMemory(Patch->Original);

        if (Patch->Relay != nullptr)
            Mm::FreeVirtualMemory(Patch->Relay);

        ZeroMemory(Patch, sizeof(*Patch));
        return STATUS_SUCCESS;
#else
        if (Patch == nullptr || Patch->SysCall == nullptr)
            return STATUS_SUCCESS;

        NTSTATUS Status = Mp::RestoreMemory(Patch->Original);
        if (NT_SUCCESS(Status))
            ZeroMemory(Patch, sizeof(*Patch));

        return Status;
#endif
    }

    SYSCALL_PATCH* FindPatch(PSYSCALL_INFO SysCall)
    {
        for (ULONG_PTR i = 0; i != g_PatchCount; ++i)
        {
            if (g_Patches[i].SysCall == SysCall)
                return &g_Patches[i];
        }

        return nullptr;
    }

    NTSTATUS AddPatch(PSYSCALL_INFO SysCall, PVOID Target, PVOID Original)
    {
        SYSCALL_PATCH* NewPatches;

        if (g_PatchCount == g_PatchCapacity)
        {
            ULONG_PTR NewCapacity = g_PatchCapacity == 0 ? 0x20 : g_PatchCapacity * 2;

            NewPatches = (SYSCALL_PATCH*)HpAlloc(NewCapacity * sizeof(*NewPatches), HEAP_ZERO_MEMORY);
            if (NewPatches == nullptr)
                return STATUS_NO_MEMORY;

            if (g_Patches != nullptr)
            {
                CopyMemory(NewPatches, g_Patches, g_PatchCount * sizeof(*g_Patches));
                HpFree(g_Patches);
            }

            g_Patches = NewPatches;
            g_PatchCapacity = NewCapacity;
        }

        g_Patches[g_PatchCount].SysCall = SysCall;
        g_Patches[g_PatchCount].Target = Target;
        g_Patches[g_PatchCount].Original = Original;
        ++g_PatchCount;

        return STATUS_SUCCESS;
    }

    template<class Function, class Invoker>
    auto DispatchTypedFilter(PSYSCALL_INFO SysCallInfo, Function Original, Invoker InvokeFilter) -> decltype(Original())
    {
        typedef decltype(Original()) RETURN_TYPE;
        ULONG_PTR FilterBitmap;
        RETURN_TYPE ReturnValue;
        SYSCALL_FILTER_INFO FltInfo;
        PSYSTEM_CALL_FILTER Filters;

        if (SysCallInfo == nullptr)
            return Original();

        Filters = SysCallInfo->FilterCallbacks;
        if (Filters == nullptr || FLAG_OFF(SysCallInfo->Flags, SystemCallFilterEnable))
            return Original();

        FltInfo.Action = ContinueSystemCall;
        FltInfo.FilterContext = nullptr;
        ReturnValue = 0;

        FilterBitmap = SysCallInfo->FilterBitmap;
        for (; FltInfo.Action == ContinueSystemCall; ++Filters)
        {
            ULONG Index;

            if (MlBitScanForwardPtr(&Index, FilterBitmap) == FALSE)
                break;

            FilterBitmap >>= Index + 1;
            Filters += Index;

            if (Filters->Callback == nullptr)
                continue;

            FltInfo.FilterContext = Filters->Context;
            ReturnValue = InvokeFilter(Filters->Callback, SysCallInfo, &FltInfo);
        }

        if (FltInfo.Action == BlockSystemCall)
            return ReturnValue;

        return Original();
    }

    PVOID LookupSyscallOriginal(ULONG RoutineHash)
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), RoutineHash);
        SYSCALL_PATCH* Patch = FindPatch(SysCall);

        return Patch != nullptr ? Patch->Original : nullptr;
    }

    NTSTATUS NTAPI HpNtCreateUserProcess(
        PHANDLE                         ProcessHandle,
        PHANDLE                         ThreadHandle,
        ACCESS_MASK                     ProcessDesiredAccess,
        ACCESS_MASK                     ThreadDesiredAccess,
        POBJECT_ATTRIBUTES              ProcessObjectAttributes,
        POBJECT_ATTRIBUTES              ThreadObjectAttributes,
        ULONG                           ProcessFlags,
        ULONG                           ThreadFlags,
        PRTL_USER_PROCESS_PARAMETERS    ProcessParameters,
        PPS_CREATE_INFO                 CreateInfo,
        PPS_ATTRIBUTE_LIST              AttributeList
    )
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtCreateUserProcess);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtCreateUserProcess);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(
                NtCreateUserProcess,
                Original,
                ProcessHandle,
                ThreadHandle,
                ProcessDesiredAccess,
                ThreadDesiredAccess,
                ProcessObjectAttributes,
                ThreadObjectAttributes,
                ProcessFlags,
                ThreadFlags,
                ProcessParameters,
                CreateInfo,
                AttributeList
            );
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(
                    HPARGS
                    PHANDLE,
                    PHANDLE,
                    ACCESS_MASK,
                    ACCESS_MASK,
                    POBJECT_ATTRIBUTES,
                    POBJECT_ATTRIBUTES,
                    ULONG,
                    ULONG,
                    PRTL_USER_PROCESS_PARAMETERS,
                    PPS_CREATE_INFO,
                    PPS_ATTRIBUTE_LIST
                );

                return ((FILTER)Callback)(
                    Info,
                    FltInfo,
                    ProcessHandle,
                    ThreadHandle,
                    ProcessDesiredAccess,
                    ThreadDesiredAccess,
                    ProcessObjectAttributes,
                    ThreadObjectAttributes,
                    ProcessFlags,
                    ThreadFlags,
                    ProcessParameters,
                    CreateInfo,
                    AttributeList
                );
            });
    }

    NTSTATUS NTAPI HpNtQuerySystemInformation(
        SYSTEM_INFORMATION_CLASS SystemInformationClass,
        PVOID                    SystemInformation,
        ULONG_PTR                SystemInformationLength,
        PULONG                   ReturnLength
    )
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtQuerySystemInformation);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtQuerySystemInformation);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(NtQuerySystemInformation, Original, SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(HPARGS SYSTEM_INFORMATION_CLASS, PVOID, ULONG_PTR, PULONG);
                return ((FILTER)Callback)(Info, FltInfo, SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
            });
    }

    NTSTATUS NTAPI HpNtQueryValueKey(
        HANDLE                      KeyHandle,
        PUNICODE_STRING             ValueName,
        KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
        PVOID                       KeyValueInformation,
        ULONG                       Length,
        PULONG                      ResultLength
    )
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtQueryValueKey);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtQueryValueKey);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(NtQueryValueKey, Original, KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(HPARGS HANDLE, PUNICODE_STRING, KEY_VALUE_INFORMATION_CLASS, PVOID, ULONG, PULONG);
                return ((FILTER)Callback)(Info, FltInfo, KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
            });
    }

    NTSTATUS NTAPI HpNtInitializeNlsFiles(
        PVOID*          BaseAddress,
        PLCID           DefaultLocaleId,
        PLARGE_INTEGER  DefaultCasingTableSize
    )
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtInitializeNlsFiles);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtInitializeNlsFiles);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(NtInitializeNlsFiles, Original, BaseAddress, DefaultLocaleId, DefaultCasingTableSize);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(HPARGS PVOID*, PLCID, PLARGE_INTEGER);
                return ((FILTER)Callback)(Info, FltInfo, BaseAddress, DefaultLocaleId, DefaultCasingTableSize);
            });
    }

    NTSTATUS NTAPI HpNtQueryDefaultLocale(BOOLEAN UserProfile, PLCID DefaultLocaleId)
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtQueryDefaultLocale);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtQueryDefaultLocale);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(NtQueryDefaultLocale, Original, UserProfile, DefaultLocaleId);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(HPARGS BOOLEAN, PLCID);
                return ((FILTER)Callback)(Info, FltInfo, UserProfile, DefaultLocaleId);
            });
    }

    NTSTATUS NTAPI HpNtQueryDefaultUILanguage(LANGID* DefaultUILanguageId)
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtQueryDefaultUILanguage);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtQueryDefaultUILanguage);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(NtQueryDefaultUILanguage, Original, DefaultUILanguageId);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(HPARGS LANGID*);
                return ((FILTER)Callback)(Info, FltInfo, DefaultUILanguageId);
            });
    }

    NTSTATUS NTAPI HpNtQueryInstallUILanguage(LANGID* InstallUILanguageId)
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtQueryInstallUILanguage);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtQueryInstallUILanguage);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(NtQueryInstallUILanguage, Original, InstallUILanguageId);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(HPARGS LANGID*);
                return ((FILTER)Callback)(Info, FltInfo, InstallUILanguageId);
            });
    }

    NTSTATUS NTAPI HpNtContinue(PCONTEXT Context, BOOLEAN TestAlert)
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), NTDLL_NtContinue);
        PVOID Original = LookupSyscallOriginal(NTDLL_NtContinue);

        auto CallOriginal = [&]() -> NTSTATUS
        {
            return CallFuncPtr(NtContinue, Original, Context, TestAlert);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> NTSTATUS
            {
                typedef NTSTATUS (HPCALL *FILTER)(HPARGS PCONTEXT, BOOLEAN);
                return ((FILTER)Callback)(Info, FltInfo, Context, TestAlert);
            });
    }

    LRESULT NTAPI HpNtUserMessageCall(
        HWND         Window,
        UINT         Message,
        WPARAM       wParam,
        LPARAM       lParam,
        ULONG_PTR    xParam,
        ULONG        xpfnProc,
        ULONG        Flags
    )
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtUserMessageCall);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtUserMessageCall);

        auto CallOriginal = [&]() -> LRESULT
        {
            return CallFuncPtr(NtUserMessageCall, Original, Window, Message, wParam, lParam, xParam, xpfnProc, Flags);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> LRESULT
            {
                typedef LRESULT (HPCALL *FILTER)(HPARGS HWND, UINT, WPARAM, LPARAM, ULONG_PTR, ULONG, ULONG);
                return ((FILTER)Callback)(Info, FltInfo, Window, Message, wParam, lParam, xParam, xpfnProc, Flags);
            });
    }

    BOOL NTAPI HpNtUserDefSetText(HWND hWnd, PLARGE_UNICODE_STRING Text)
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtUserDefSetText);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtUserDefSetText);

        auto CallOriginal = [&]() -> BOOL
        {
            return CallFuncPtr(NtUserDefSetText, Original, hWnd, Text);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> BOOL
            {
                typedef BOOL (HPCALL *FILTER)(HPARGS HWND, PLARGE_UNICODE_STRING);
                return ((FILTER)Callback)(Info, FltInfo, hWnd, Text);
            });
    }

    HDC NTAPI HpNtUserGetDC(HWND hWnd)
    {
        typedef HDC (NTAPI *PFN)(HWND);

        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtUserGetDC);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtUserGetDC);

        auto CallOriginal = [&]() -> HDC
        {
            return ((PFN)Original)(hWnd);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> HDC
            {
                typedef HDC (HPCALL *FILTER)(HPARGS HWND);
                return ((FILTER)Callback)(Info, FltInfo, hWnd);
            });
    }

    HDC NTAPI HpNtUserGetDCEx(HWND hWnd, HRGN hrgnClip, DWORD flags)
    {
        typedef HDC (NTAPI *PFN)(HWND, HRGN, DWORD);

        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtUserGetDCEx);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtUserGetDCEx);

        auto CallOriginal = [&]() -> HDC
        {
            return ((PFN)Original)(hWnd, hrgnClip, flags);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> HDC
            {
                typedef HDC (HPCALL *FILTER)(HPARGS HWND, HRGN, DWORD);
                return ((FILTER)Callback)(Info, FltInfo, hWnd, hrgnClip, flags);
            });
    }

    HDC NTAPI HpNtUserGetWindowDC(HWND hWnd)
    {
        typedef HDC (NTAPI *PFN)(HWND);

        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtUserGetWindowDC);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtUserGetWindowDC);

        auto CallOriginal = [&]() -> HDC
        {
            return ((PFN)Original)(hWnd);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> HDC
            {
                typedef HDC (HPCALL *FILTER)(HPARGS HWND);
                return ((FILTER)Callback)(Info, FltInfo, hWnd);
            });
    }

    HDC NTAPI HpNtUserBeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
    {
        typedef HDC (NTAPI *PFN)(HWND, LPPAINTSTRUCT);

        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtUserBeginPaint);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtUserBeginPaint);

        auto CallOriginal = [&]() -> HDC
        {
            return ((PFN)Original)(hWnd, lpPaint);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> HDC
            {
                typedef HDC (HPCALL *FILTER)(HPARGS HWND, LPPAINTSTRUCT);
                return ((FILTER)Callback)(Info, FltInfo, hWnd, lpPaint);
            });
    }

    HWND DispatchHpNtUserCreateWindowEx(
        ULONG                   ArgumentCount,
        ULONG                   ExStyle,
        PLARGE_UNICODE_STRING   ClassName,
        PLARGE_UNICODE_STRING   ClassVersion,
        PLARGE_UNICODE_STRING   WindowName,
        ULONG                   Style,
        LONG                    X,
        LONG                    Y,
        LONG                    Width,
        LONG                    Height,
        HWND                    ParentWnd,
        HMENU                   Menu,
        PVOID                   Instance,
        LPVOID                  Param,
        ULONG                   ShowMode,
        ULONG_PTR               Unknown1,
        ULONG_PTR               Unknown2,
        ULONG_PTR               Unknown3
    )
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtUserCreateWindowEx);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtUserCreateWindowEx);

        auto CallOriginal = [&]() -> HWND
        {
            if (ArgumentCount == 15)
            {
                typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG_PTR);
                return ((PFN)Original)(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown1);
            }

            if (ArgumentCount == 16)
            {
                typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG, ULONG_PTR);
                return ((PFN)Original)(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown1, Unknown2);
            }

            typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG, ULONG, ULONG_PTR);
            return ((PFN)Original)(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown1, Unknown2, Unknown3);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> HWND
            {
                typedef HWND (HPCALL *FILTER)(
                    HPARGS
                    ULONG,
                    PLARGE_UNICODE_STRING,
                    PLARGE_UNICODE_STRING,
                    PLARGE_UNICODE_STRING,
                    ULONG,
                    LONG,
                    LONG,
                    LONG,
                    LONG,
                    HWND,
                    HMENU,
                    PVOID,
                    LPVOID,
                    ULONG,
                    ULONG_PTR,
                    ULONG_PTR,
                    ULONG_PTR
                );

                return ((FILTER)Callback)(Info, FltInfo, ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown1, Unknown2, Unknown3);
            });
    }

    HWND NTAPI HpNtUserCreateWindowEx_Win7(
        ULONG                   ExStyle,
        PLARGE_UNICODE_STRING   ClassName,
        PLARGE_UNICODE_STRING   ClassVersion,
        PLARGE_UNICODE_STRING   WindowName,
        ULONG                   Style,
        LONG                    X,
        LONG                    Y,
        LONG                    Width,
        LONG                    Height,
        HWND                    ParentWnd,
        HMENU                   Menu,
        PVOID                   Instance,
        LPVOID                  Param,
        ULONG                   ShowMode,
        ULONG_PTR               Unknown1
    )
    {
        return DispatchHpNtUserCreateWindowEx(15, ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown1, 0, 0);
    }

    HWND NTAPI HpNtUserCreateWindowEx_Win8(
        ULONG                   ExStyle,
        PLARGE_UNICODE_STRING   ClassName,
        PLARGE_UNICODE_STRING   ClassVersion,
        PLARGE_UNICODE_STRING   WindowName,
        ULONG                   Style,
        LONG                    X,
        LONG                    Y,
        LONG                    Width,
        LONG                    Height,
        HWND                    ParentWnd,
        HMENU                   Menu,
        PVOID                   Instance,
        LPVOID                  Param,
        ULONG                   ShowMode,
        ULONG                   Unknown1,
        ULONG_PTR               Unknown2
    )
    {
        return DispatchHpNtUserCreateWindowEx(16, ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown1, Unknown2, 0);
    }

    HWND NTAPI HpNtUserCreateWindowEx_Win10(
        ULONG                   ExStyle,
        PLARGE_UNICODE_STRING   ClassName,
        PLARGE_UNICODE_STRING   ClassVersion,
        PLARGE_UNICODE_STRING   WindowName,
        ULONG                   Style,
        LONG                    X,
        LONG                    Y,
        LONG                    Width,
        LONG                    Height,
        HWND                    ParentWnd,
        HMENU                   Menu,
        PVOID                   Instance,
        LPVOID                  Param,
        ULONG                   ShowMode,
        ULONG                   Unknown1,
        ULONG                   Unknown2,
        ULONG_PTR               Unknown3
    )
    {
        return DispatchHpNtUserCreateWindowEx(17, ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown1, Unknown2, Unknown3);
    }

    HFONT NTAPI HpNtGdiHfontCreate(
        PENUMLOGFONTEXDVW   EnumLogFont,
        ULONG               SizeOfEnumLogFont,
        LONG                LogFontType,
        LONG                Unknown,
        PVOID               FreeListLocalFont
    )
    {
        PSYSCALL_INFO SysCall = HppLookupSystemCall(HppGetGlobalInfo(), WIN32K_NtGdiHfontCreate);
        PVOID Original = LookupSyscallOriginal(WIN32K_NtGdiHfontCreate);

        auto CallOriginal = [&]() -> HFONT
        {
            return CallFuncPtr(NtGdiHfontCreate, Original, EnumLogFont, SizeOfEnumLogFont, LogFontType, Unknown, FreeListLocalFont);
        };

        return DispatchTypedFilter(SysCall, CallOriginal,
            [&] (PVOID Callback, PSYSCALL_INFO Info, PSYSCALL_FILTER_INFO FltInfo) -> HFONT
            {
                typedef HFONT (HPCALL *FILTER)(HPARGS PENUMLOGFONTEXDVW, ULONG, LONG, LONG, PVOID);
                return ((FILTER)Callback)(Info, FltInfo, EnumLogFont, SizeOfEnumLogFont, LogFontType, Unknown, FreeListLocalFont);
            });
    }

    PVOID FindWrapperByHash(ULONG RoutineHash)
    {
        RTL_OSVERSIONINFOW VersionInfo;

        switch (RoutineHash)
        {
            case NTDLL_NtCreateUserProcess:       return HpNtCreateUserProcess;
            case NTDLL_NtQuerySystemInformation:  return HpNtQuerySystemInformation;
            case NTDLL_NtQueryValueKey:           return HpNtQueryValueKey;
            case NTDLL_NtInitializeNlsFiles:      return HpNtInitializeNlsFiles;
            case NTDLL_NtQueryDefaultLocale:      return HpNtQueryDefaultLocale;
            case NTDLL_NtQueryDefaultUILanguage:  return HpNtQueryDefaultUILanguage;
            case NTDLL_NtQueryInstallUILanguage:  return HpNtQueryInstallUILanguage;
            case NTDLL_NtContinue:                return HpNtContinue;
            case WIN32K_NtUserCreateWindowEx:
                if (!NT_SUCCESS(Nt_QueryOsVersion(&VersionInfo)))
                    return nullptr;
                if (VersionInfo.dwMajorVersion == 6 && VersionInfo.dwMinorVersion < 2)
                    return HpNtUserCreateWindowEx_Win7;
                if (VersionInfo.dwMajorVersion == 6)
                    return HpNtUserCreateWindowEx_Win8;
                return HpNtUserCreateWindowEx_Win10;
            case WIN32K_NtUserMessageCall:        return HpNtUserMessageCall;
            case WIN32K_NtUserDefSetText:         return HpNtUserDefSetText;
            case WIN32K_NtUserGetDC:              return HpNtUserGetDC;
            case WIN32K_NtUserGetDCEx:            return HpNtUserGetDCEx;
            case WIN32K_NtUserGetWindowDC:        return HpNtUserGetWindowDC;
            case WIN32K_NtUserBeginPaint:         return HpNtUserBeginPaint;
            case WIN32K_NtGdiHfontCreate:         return HpNtGdiHfontCreate;
        }

        return nullptr;
    }

    NTSTATUS PatchSystemCall(PSYSCALL_INFO SysCall)
    {
        PVOID Original, Target, Wrapper;
        NTSTATUS Status;

        if (FindPatch(SysCall) != nullptr)
            return STATUS_SUCCESS;

        Wrapper = FindWrapperByHash(SysCall->NameHash);
        if (Wrapper == nullptr)
            return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;

        Target = SysCall->FunctionAddress;

#if ML_AMD64
        Original = AllocateSyscallStub(SysCall);
        if (Original == nullptr)
            return STATUS_NO_MEMORY;

        Status = AddPatch(SysCall, Target, Original);
        if (NT_FAILED(Status))
        {
            Mm::FreeVirtualMemory(Original);
            return Status;
        }

        SysCall->FunctionAddress = Original;
        Status = WriteSystemCallJump(Target, Wrapper, &g_Patches[g_PatchCount - 1]);
        if (NT_FAILED(Status))
        {
            SysCall->FunctionAddress = Target;
            RestorePatch(&g_Patches[g_PatchCount - 1]);
            --g_PatchCount;
            return Status;
        }
#else
        Status = AddPatch(SysCall, Target, nullptr);
        if (NT_FAILED(Status))
            return Status;

        Mp::PATCH_MEMORY_DATA Patch = Mp::FunctionJumpVa(
            Target,
            Wrapper,
            &g_Patches[g_PatchCount - 1].Original,
            LEP_FUNCTION_JUMP_OP
        );

        Status = Mp::PatchMemory(&Patch, 1);
        if (NT_FAILED(Status))
        {
            if (*(PBYTE)Target == 0xE9)
                Mp::RestoreMemory(g_Patches[g_PatchCount - 1].Original);
            ZeroMemory(&g_Patches[g_PatchCount - 1], sizeof(*g_Patches));
            --g_PatchCount;
            return Status;
        }

        Original = g_Patches[g_PatchCount - 1].Original;
        SysCall->FunctionAddress = Original;
#endif

        return STATUS_SUCCESS;
    }
}

NTSTATUS InstallHookPort(PLDR_MODULE SysCallModule, ULONG Flags)
{
    NTSTATUS                    Status;
    PVOID                       NtdllModule;
    PBYTE                       Function;
    PSTR                        FunctionName;
    ULONG_PTR                   HashTableCount;
    PULONG                      AddressOfNames, AddressOfFunctions;
    PUSHORT                     AddressOfNameOrdinals;
    PIMAGE_DOS_HEADER           DosHeader;
    PIMAGE_NT_HEADERS           NtHeader;
    PIMAGE_EXPORT_DIRECTORY     ExportDirectory;
    PSYSCALL_INFO               FilterEntry;
    PSYSTEM_CALL_HASH           SysCallHash;
    PHOOK_PORT_GLOBAL_INFO      GlobalInfo;

    UNREFERENCED_PARAMETER(SysCallModule);
    UNREFERENCED_PARAMETER(Flags);

    FAIL_RETURN(ml::MlInitialize());

    GlobalInfo = HppGetGlobalInfo();
    if (GlobalInfo != nullptr)
    {
        _InterlockedIncrementPtr(&GlobalInfo->RefCount);
        return STATUS_SUCCESS;
    }

    GlobalInfo = (HOOK_PORT_GLOBAL_INFO*)HpAlloc(sizeof(*GlobalInfo), HEAP_ZERO_MEMORY);
    if (GlobalInfo == nullptr)
        return STATUS_NO_MEMORY;

    GlobalInfo->MaxSystemCallCount[HP_NTKRNL_SERVICE_INDEX] = HP_MAX_SERVICE_INDEX;
    GlobalInfo->MaxSystemCallCount[HP_WIN32K_SERVICE_INDEX] = HP_MAX_WIN32K_SERVICE_INDEX;

    Status = HpAllocateVirtualMemory((PVOID*)&GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX], HP_MAX_SERVICE_INDEX * sizeof(*GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX]));
    if (NT_FAILED(Status))
        goto InstallHookPort_Failure;

    Status = HpAllocateVirtualMemory((PVOID*)&GlobalInfo->HashTable.Entry, HP_MAX_SERVICE_INDEX * sizeof(*GlobalInfo->HashTable.Entry));
    if (NT_FAILED(Status))
        goto InstallHookPort_Failure;

    NtdllModule = GetNtdllHandle();
    DosHeader = (PIMAGE_DOS_HEADER)NtdllModule;
    NtHeader = (PIMAGE_NT_HEADERS)((ULONG_PTR)DosHeader + DosHeader->e_lfanew);
    ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)(NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress + (ULONG_PTR)NtdllModule);
    AddressOfNames = (PULONG)(ExportDirectory->AddressOfNames + (ULONG_PTR)NtdllModule);
    AddressOfFunctions = (PULONG)(ExportDirectory->AddressOfFunctions + (ULONG_PTR)NtdllModule);
    AddressOfNameOrdinals = (PUSHORT)(ExportDirectory->AddressOfNameOrdinals + (ULONG_PTR)NtdllModule);

    SysCallHash = GlobalInfo->HashTable.Entry;
    for (ULONG_PTR Count = ExportDirectory->NumberOfNames; Count; ++AddressOfNames, ++AddressOfNameOrdinals, --Count)
    {
        ULONG ServiceIndex, Hash;
        PVOID ReturnOpAddress;

        FunctionName = (PSTR)(*AddressOfNames + (ULONG_PTR)NtdllModule);
        if (FunctionName[0] != 'Z' || FunctionName[1] != 'w')
            continue;

        Function = (PBYTE)(AddressOfFunctions[*AddressOfNameOrdinals] + (ULONG_PTR)NtdllModule);
        if (!ParseSystemCallStub(Function, &ServiceIndex, &ReturnOpAddress))
            continue;

        if ((ServiceIndex & 0xFFFF) >= HP_MAX_SERVICE_INDEX)
            continue;

        FilterEntry = &GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX][ServiceIndex & 0xFFFF];
        Hash = HashNativeZwAPI(FunctionName);

        Status = HppInitializeSystemCallByRoutine(FilterEntry, Function, Hash);
        if (NT_FAILED(Status))
            goto InstallHookPort_Failure;

        SysCallHash->Entry = FilterEntry;
        SysCallHash->Hash = Hash;
        ++SysCallHash;
    }

    HashTableCount = SysCallHash - GlobalInfo->HashTable.Entry;
    GlobalInfo->HashTable.Count = HashTableCount;
    HppSortHashTable(&GlobalInfo->HashTable);

    HppSetGlobalInfo(GlobalInfo);
    _InterlockedIncrementPtr(&GlobalInfo->RefCount);

    return STATUS_SUCCESS;

InstallHookPort_Failure:
    HppSetGlobalInfo(nullptr);
    if (GlobalInfo != nullptr)
    {
        if (GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX] != nullptr)
            HpFreeVirtualMemory(GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX]);

        HppDestroyHashTable(&GlobalInfo->HashTable);
        HpFree(GlobalInfo);
    }

    return Status;
}

NTSTATUS UnInstallHookPort(VOID)
{
    ULONG RefCount;
    PHOOK_PORT_GLOBAL_INFO Info;

    Info = HppGetGlobalInfo();
    if (Info == nullptr)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    RefCount = _InterlockedDecrementPtr(&Info->RefCount);
    if (RefCount != 0)
        return RefCount;

    HppSetGlobalInfo(nullptr);

    for (ULONG_PTR i = g_PatchCount; i != 0; --i)
        RestorePatch(&g_Patches[i - 1]);

    HpFree(g_Patches);
    g_Patches = nullptr;
    g_PatchCount = 0;
    g_PatchCapacity = 0;

    for (ULONG TableIndex = 0; TableIndex != HP_MAX_SERVICE_TABLE_COUNT; ++TableIndex)
    {
        PSYSCALL_INFO SysCall = Info->SystemCallInfo[TableIndex];
        if (SysCall == nullptr)
            continue;

        for (ULONG Count = (ULONG)Info->MaxSystemCallCount[TableIndex]; Count; --Count, ++SysCall)
        {
            if (SysCall->FilterCallbacks != nullptr)
                HpFreeCallbackArray(SysCall->FilterCallbacks);
        }

        HpFreeVirtualMemory(Info->SystemCallInfo[TableIndex]);
    }

    HppDestroyHashTable(&Info->HashTable);
    HpFree(Info);

    return STATUS_SUCCESS;
}

NTSTATUS HpAddSystemCallFilter(ULONG RoutineHash, PVOID Routine, PVOID Context)
{
    ULONG                   EmptyPos, Bitmap;
    PSYSTEM_CALL_FILTER     FilterCallbacks;
    PSYSCALL_INFO           Filter;
    NTSTATUS                Status;

    if (HppGetGlobalInfo() == nullptr)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    if (Routine == nullptr)
        return STATUS_INVALID_PARAMETER;

    Filter = HppLookupSystemCall(HppGetGlobalInfo(), RoutineHash);
    if (Filter == nullptr)
        return STATUS_HOOK_PORT_ENTRY_NOT_FOUND;

    FilterCallbacks = Filter->FilterCallbacks;
    if (FilterCallbacks == nullptr)
    {
        Filter->FilterBitmap = 0;
        FilterCallbacks = HpAllocateCallbackArray();
        if (FilterCallbacks == nullptr)
            return STATUS_NO_MEMORY;

        Filter->FilterCallbacks = FilterCallbacks;
    }

    Bitmap = Filter->FilterBitmap;
    if (Bitmap == -1)
        return STATUS_HOOK_PORT_TOO_MANY_FILTERS;

    EmptyPos = _BitScanForward(&EmptyPos, ~Bitmap) ? EmptyPos : 0;

    FilterCallbacks += EmptyPos;
    FilterCallbacks->Callback = Routine;
    FilterCallbacks->Context = Context;

    SET_FLAG(Filter->Flags, SystemCallFilterEnable);
    SET_FLAG(Filter->FilterBitmap, 1 << EmptyPos);

    Status = PatchSystemCall(Filter);
    if (NT_FAILED(Status))
    {
        CLEAR_FLAG(Filter->FilterBitmap, 1 << EmptyPos);
        FilterCallbacks->Callback = nullptr;
        FilterCallbacks->Context = nullptr;
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS HpRemoveSystemCallFilter(ULONG RoutineHash, PVOID Routine)
{
    ULONG                   Bitmap, Index;
    PSYSTEM_CALL_FILTER     FilterCallbacks;
    PSYSCALL_INFO           Filter;

    Filter = HppLookupSystemCall(HppGetGlobalInfo(), RoutineHash);
    if (Filter == nullptr)
        return STATUS_HOOK_PORT_ENTRY_NOT_FOUND;

    FilterCallbacks = Filter->FilterCallbacks;
    if (FilterCallbacks == nullptr)
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

        FilterCallbacks->Callback = nullptr;
        CLEAR_FLAG(Filter->FilterBitmap, 1 << (FilterCallbacks - Filter->FilterCallbacks));

        if (Filter->FilterBitmap == 0)
            CLEAR_FLAG(Filter->Flags, SystemCallFilterEnable);

        return STATUS_SUCCESS;
    }

    return STATUS_HOOK_PORT_FILTER_NOT_FOUND;
}

PSYSCALL_INFO HpLookupSystemCall(ULONG_PTR SystemCallHash)
{
    return HppLookupSystemCall(HppGetGlobalInfo(), SystemCallHash);
}

PVOID HpGetSystemCallOriginal(ULONG RoutineHash)
{
    return LookupSyscallOriginal(RoutineHash);
}

NTSTATUS HpAddSystemCallByRoutine(PVOID Routine, ULONG RoutineHash)
{
    return HpAddSystemCallByRoutineRange(&Routine, &RoutineHash, 1);
}

NTSTATUS HpAddSystemCallByRoutineRange(PVOID* Routine, PULONG RoutineHash, ULONG_PTR Count)
{
    SYSCALL_INFO LocalBuffer[0x20];
    PSYSCALL_INFO SysCallBase, SysCall;
    NTSTATUS Status;

    if (HppGetGlobalInfo() == nullptr)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    if (Routine == nullptr || RoutineHash == nullptr || Count == 0)
        return STATUS_INVALID_PARAMETER;

    if (Count <= countof(LocalBuffer))
    {
        SysCallBase = LocalBuffer;
    }
    else
    {
        SysCallBase = (PSYSCALL_INFO)HpAlloc(Count * sizeof(*SysCallBase), HEAP_ZERO_MEMORY);
        if (SysCallBase == nullptr)
            return STATUS_NO_MEMORY;
    }

    SysCall = SysCallBase;
    for (ULONG_PTR i = 0; i != Count; ++i)
    {
        Status = HppInitializeSystemCallByRoutine(SysCall, Routine[i], RoutineHash[i]);
        if (NT_FAILED(Status))
            goto Exit;

        ++SysCall;
    }

    Status = HpAddSystemCall(SysCallBase, Count);

Exit:
    if (SysCallBase != LocalBuffer)
        HpFree(SysCallBase);

    return Status;
}

NTSTATUS HpAddSystemCall(PSYSCALL_INFO SystemCall, ULONG_PTR Count)
{
    PHOOK_PORT_GLOBAL_INFO Info;
    SYSTEM_CALL_HASH_TABLE NewTable, OldTable;
    PSYSTEM_CALL_HASH TableEntry;
    ULONG_PTR EntryCount, AddedCount;
    NTSTATUS Status;

    Info = HppGetGlobalInfo();
    if (Info == nullptr)
        return STATUS_HOOK_PORT_NOT_INITIALIZED;

    if (SystemCall == nullptr || Count == 0)
        return STATUS_INVALID_PARAMETER;

    EntryCount = Info->HashTable.Count;
    Status = HpAllocateVirtualMemory((PVOID*)&NewTable.Entry, (EntryCount + Count) * sizeof(*NewTable.Entry));
    FAIL_RETURN(Status);

    CopyMemory(NewTable.Entry, Info->HashTable.Entry, EntryCount * sizeof(*NewTable.Entry));
    TableEntry = &NewTable.Entry[EntryCount];
    AddedCount = 0;

    for (ULONG_PTR i = 0; i != Count; ++i)
    {
        PSYSCALL_INFO Source, Target;
        ULONG TableIndex, ServiceIndex;

        Source = &SystemCall[i];

        if (HppLookupSystemCall(Info, Source->NameHash) != nullptr)
            continue;

        TableIndex = Source->ServiceTableIndex;
        ServiceIndex = Source->ServiceIndex;

        if (TableIndex >= HP_MAX_SERVICE_TABLE_COUNT)
        {
            Status = STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
            goto Failure;
        }

        if (ServiceIndex >= Info->MaxSystemCallCount[TableIndex])
        {
            Status = STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;
            goto Failure;
        }

        if (Info->SystemCallInfo[TableIndex] == nullptr)
        {
            Status = HpAllocateVirtualMemory(
                        (PVOID*)&Info->SystemCallInfo[TableIndex],
                        Info->MaxSystemCallCount[TableIndex] * sizeof(*Info->SystemCallInfo[TableIndex])
                    );
            if (NT_FAILED(Status))
                goto Failure;
        }

        Target = &Info->SystemCallInfo[TableIndex][ServiceIndex];
        *Target = *Source;

        TableEntry->Entry = Target;
        TableEntry->Hash = Source->NameHash;
        ++TableEntry;
        ++AddedCount;
    }

    if (AddedCount == 0)
    {
        HpFreeVirtualMemory(NewTable.Entry);
        return STATUS_SUCCESS;
    }

    NewTable.Count = EntryCount + AddedCount;
    HppSortHashTable(&NewTable);

    OldTable = Info->HashTable;
    Info->HashTable = NewTable;
    HppDestroyHashTable(&OldTable);

    return STATUS_SUCCESS;

Failure:
    HpFreeVirtualMemory(NewTable.Entry);
    return Status;
}
