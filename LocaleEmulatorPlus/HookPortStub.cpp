#include "stdafx.h"

#if ML_AMD64

namespace
{
    enum
    {
        X64_SYSCALL_STUB_SIZE = 0x20,
        X64_SYSCALL_PATCH_SIZE = 12,
    };

    struct X64_SYSCALL_PATCH
    {
        PSYSCALL_INFO SysCall;
        PVOID Target;
        PVOID Original;
        BYTE Backup[X64_SYSCALL_PATCH_SIZE];
    };

    PHOOK_PORT_GLOBAL_INFO g_GlobalInfo;
    X64_SYSCALL_PATCH* g_Patches;
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

    BOOL ParseX64SysCallStub(PBYTE Function, PULONG ServiceIndex)
    {
        for (ULONG_PTR i = 0; i != 8; ++i)
        {
            if (Function[i + 0] == 0x4C &&
                Function[i + 1] == 0x8B &&
                Function[i + 2] == 0xD1 &&
                Function[i + 3] == 0xB8)
            {
                *ServiceIndex = *(PULONG)&Function[i + 4];
                return TRUE;
            }
        }

        return FALSE;
    }

    NTSTATUS HppInitializeSystemCallByRoutine(PSYSCALL_INFO SysCall, PVOID Routine, ULONG RoutineHash)
    {
        ULONG ServiceIndex;

        ZeroMemory(SysCall, sizeof(*SysCall));

        if (!ParseX64SysCallStub((PBYTE)Routine, &ServiceIndex))
            return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;

        SysCall->NameHash = RoutineHash;
        SysCall->ServiceData = ServiceIndex;
        SysCall->FunctionAddress = Routine;
        SysCall->ReturnOpAddress = PtrAdd(Routine, X64_SYSCALL_PATCH_SIZE);
        SysCall->ArgumentSize = 0;

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

    BOOL HpIsCurrentCallSkip(PSYSCALL_INFO SysCallInfo)
    {
        SYSCALL_FILTER_SKIP_INFO* SkipInfo;

        SkipInfo = (SYSCALL_FILTER_SKIP_INFO*)FindThreadFrame(SYSCALL_SKIP_MAGIC);
        return SkipInfo != nullptr &&
               (
                SkipInfo->ServiceIndex == ALL_SERVICE_INDEX ||
                SysCallInfo->ServiceData == SkipInfo->ServiceIndex
               );
    }

    PSYSTEM_CALL_FILTER HpAllocateCallbackArray()
    {
        return (PSYSTEM_CALL_FILTER)HpAlloc(MAX_FILTER_NUMBER * sizeof(SYSTEM_CALL_FILTER), HEAP_ZERO_MEMORY);
    }

    BOOL HpFreeCallbackArray(PSYSTEM_CALL_FILTER CallbackFilters)
    {
        return HpFree(CallbackFilters);
    }

    PVOID AllocateSyscallStub(PSYSCALL_INFO SysCall)
    {
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
        Stub[8] = 0x0F;
        Stub[9] = 0x05;
        Stub[10] = 0xC3;
        NtFlushInstructionCache(CurrentProcess, Stub, X64_SYSCALL_STUB_SIZE);

        return Stub;
    }

    NTSTATUS WriteAbsoluteJump(PVOID Address, PVOID Target, BYTE* Backup)
    {
        BYTE Code[X64_SYSCALL_PATCH_SIZE];
        ULONG Protect;
        NTSTATUS Status;

        CopyMemory(Backup, Address, sizeof(Code));

        Code[0] = 0x48;
        Code[1] = 0xB8;
        *(PVOID*)&Code[2] = Target;
        *(PUSHORT)&Code[10] = 0xE0FF;

        Status = ProtectVirtualMemory(Address, sizeof(Code), PAGE_EXECUTE_READWRITE, &Protect);
        FAIL_RETURN(Status);

        CopyMemory(Address, Code, sizeof(Code));
        NtFlushInstructionCache(CurrentProcess, Address, sizeof(Code));

        if (Protect != PAGE_EXECUTE_READWRITE)
            ProtectVirtualMemory(Address, sizeof(Code), Protect, &Protect);

        return STATUS_SUCCESS;
    }

    NTSTATUS RestorePatch(X64_SYSCALL_PATCH* Patch)
    {
        ULONG Protect;
        NTSTATUS Status;

        if (Patch == nullptr || Patch->SysCall == nullptr)
            return STATUS_SUCCESS;

        Status = ProtectVirtualMemory(Patch->Target, X64_SYSCALL_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &Protect);
        FAIL_RETURN(Status);

        CopyMemory(Patch->Target, Patch->Backup, X64_SYSCALL_PATCH_SIZE);
        NtFlushInstructionCache(CurrentProcess, Patch->Target, X64_SYSCALL_PATCH_SIZE);

        if (Protect != PAGE_EXECUTE_READWRITE)
            ProtectVirtualMemory(Patch->Target, X64_SYSCALL_PATCH_SIZE, Protect, &Protect);

        if (Patch->Original != nullptr)
            Mm::FreeVirtualMemory(Patch->Original);

        ZeroMemory(Patch, sizeof(*Patch));
        return STATUS_SUCCESS;
    }

    X64_SYSCALL_PATCH* FindPatch(PSYSCALL_INFO SysCall)
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
        X64_SYSCALL_PATCH* NewPatches;

        if (g_PatchCount == g_PatchCapacity)
        {
            ULONG_PTR NewCapacity = g_PatchCapacity == 0 ? 0x20 : g_PatchCapacity * 2;

            NewPatches = (X64_SYSCALL_PATCH*)HpAlloc(NewCapacity * sizeof(*NewPatches), HEAP_ZERO_MEMORY);
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
    NTSTATUS DispatchTypedFilter(PSYSCALL_INFO SysCallInfo, Function Original, Invoker InvokeFilter)
    {
        ULONG_PTR FilterBitmap;
        NTSTATUS ReturnValue;
        SYSCALL_FILTER_INFO FltInfo;
        PSYSTEM_CALL_FILTER Filters;

        if (SysCallInfo == nullptr || HpIsCurrentCallSkip(SysCallInfo))
            return Original();

        Filters = SysCallInfo->FilterCallbacks;
        if (Filters == nullptr || FLAG_OFF(SysCallInfo->Flags, SystemCallFilterEnable))
            return Original();

        FltInfo.Action = ContinueSystemCall;
        FltInfo.FilterContext = nullptr;
        FltInfo.SsdtRoutine = SysCallInfo->FunctionAddress;
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
        X64_SYSCALL_PATCH* Patch = FindPatch(SysCall);

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

    PVOID FindWrapperByHash(ULONG RoutineHash)
    {
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
        }

        return nullptr;
    }

    NTSTATUS PatchSystemCall(PSYSCALL_INFO SysCall)
    {
        PVOID Original, Wrapper;
        NTSTATUS Status;

        if (FindPatch(SysCall) != nullptr)
            return STATUS_SUCCESS;

        Wrapper = FindWrapperByHash(SysCall->NameHash);
        if (Wrapper == nullptr)
            return STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM;

        Original = AllocateSyscallStub(SysCall);
        if (Original == nullptr)
            return STATUS_NO_MEMORY;

        Status = AddPatch(SysCall, SysCall->FunctionAddress, Original);
        if (NT_FAILED(Status))
        {
            Mm::FreeVirtualMemory(Original);
            return Status;
        }

        Status = WriteAbsoluteJump(SysCall->FunctionAddress, Wrapper, &g_Patches[g_PatchCount - 1].Backup[0]);
        if (NT_FAILED(Status))
        {
            RestorePatch(&g_Patches[g_PatchCount - 1]);
            --g_PatchCount;
            return Status;
        }

        SysCall->FunctionAddress = Original;
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

        FunctionName = (PSTR)(*AddressOfNames + (ULONG_PTR)NtdllModule);
        if (FunctionName[0] != 'Z' || FunctionName[1] != 'w')
            continue;

        Function = (PBYTE)(AddressOfFunctions[*AddressOfNameOrdinals] + (ULONG_PTR)NtdllModule);
        if (!ParseX64SysCallStub(Function, &ServiceIndex))
            continue;

        if ((ServiceIndex & 0xFFFF) >= HP_MAX_SERVICE_INDEX)
            continue;

        FilterEntry = &GlobalInfo->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX][ServiceIndex & 0xFFFF];
        Hash = HashNativeZwAPI(FunctionName);

        Status = HppInitializeSystemCallByRoutine(FilterEntry, Function, Hash);
        if (NT_FAILED(Status))
            goto InstallHookPort_Failure;

        FilterEntry->FunctionName = FunctionName;
        SysCallHash->Entry = FilterEntry;
        SysCallHash->Hash = Hash;
        ++SysCallHash;
    }

    HashTableCount = SysCallHash - GlobalInfo->HashTable.Entry;
    GlobalInfo->HashTable.Count = HashTableCount;
    GlobalInfo->SystemCallCount[HP_NTKRNL_SERVICE_INDEX] = HashTableCount;

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

    PSYSCALL_INFO SysCall = Info->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX];
    if (SysCall != nullptr)
    {
        for (ULONG Count = (ULONG)Info->MaxSystemCallCount[HP_NTKRNL_SERVICE_INDEX]; Count; --Count, ++SysCall)
        {
            if (SysCall->FilterCallbacks != nullptr)
                HpFreeCallbackArray(SysCall->FilterCallbacks);
        }

        HpFreeVirtualMemory(Info->SystemCallInfo[HP_NTKRNL_SERVICE_INDEX]);
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

NTSTATUS HpAddSystemCallByRoutine(PVOID Routine, ULONG RoutineHash)
{
    UNREFERENCED_PARAMETER(Routine);
    UNREFERENCED_PARAMETER(RoutineHash);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS HpAddSystemCallByRoutineRange(PVOID* Routine, PULONG RoutineHash, ULONG_PTR Count)
{
    UNREFERENCED_PARAMETER(Routine);
    UNREFERENCED_PARAMETER(RoutineHash);
    UNREFERENCED_PARAMETER(Count);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS HpAddSystemCall(PSYSCALL_INFO SystemCall, ULONG_PTR Count)
{
    UNREFERENCED_PARAMETER(SystemCall);
    UNREFERENCED_PARAMETER(Count);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS HpAddSystemServiceTable(PHP_SYSTEM_SERVICE_TABLE ServiceTable)
{
    UNREFERENCED_PARAMETER(ServiceTable);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS HpInitializeWin32kPort(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

PVOID HpDuplicateHookPort(PVOID NtBase)
{
    UNREFERENCED_PARAMETER(NtBase);
    return nullptr;
}

NTSTATUS HpSetGlobalFilter(PHP_GLOBAL_FILTER NewFilter, PHP_GLOBAL_FILTER OldFilter)
{
    UNREFERENCED_PARAMETER(NewFilter);
    UNREFERENCED_PARAMETER(OldFilter);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpQueryValue(HpValueType Type, PVOID* Value)
{
    UNREFERENCED_PARAMETER(Type);
    *Value = nullptr;
    return STATUS_NOT_SUPPORTED;
}

#endif
