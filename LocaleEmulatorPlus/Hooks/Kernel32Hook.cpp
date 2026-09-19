#include "stdafx.h"

using namespace Mp;

//
// LocaleName
// sCountry
// sList
// sDecimal
// sThousand
//

LCID NTAPI LepGetUserDefaultLCID()
{
    return LepGetGlobalData()->GetLepb()->LocaleID;
}

LANGID WINAPI LepGetSystemDefaultUILanguage()
{
    return (LANGID)LepGetGlobalData()->GetLepb()->LocaleID;
}

LANGID WINAPI LepGetUserDefaultUILanguage()
{
    return (LANGID)LepGetGlobalData()->GetLepb()->LocaleID;
}

NTSTATUS LepGlobalData::HackUserDefaultLCID(PVOID Kernel32)
{
    LCID        Lcid;
    PVOID       gNlsProcessLocalCache;
    PLDR_MODULE Kernel;
    API_POINTER(GetUserDefaultLCID) GetUserDefaultLCID;

    *(PVOID *)&GetUserDefaultLCID = LookupExportTable(Kernel32, KERNEL32_GetUserDefaultLCID);
    Lcid = GetUserDefaultLCID();

    Kernel = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel == nullptr)
        Kernel = FindLdrModuleByHandle(Kernel32);

    gNlsProcessLocalCache = nullptr;

    WalkRelocTableT(Kernel->DllBase,
        WalkRelocCallbackM(ImageBase, RelocationEntry, Offset, Context)
        {
            SEH_TRY
            {
                PULONG_PTR Memory = *(PULONG_PTR *)PtrAdd(ImageBase, RelocationEntry->VirtualAddress + Offset->Offset);
                if (*(PLCID)Memory[2] == Lcid)
                {
                    gNlsProcessLocalCache = Memory;
                    return STATUS_UNSUCCESSFUL;
                }
            }
            SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            return STATUS_SUCCESS;
        }
    );

    //if (gNlsProcessLocalCache != nullptr)
        //*(PLCID)(((PULONG_PTR)gNlsProcessLocalCache)[2]) = GetLepb()->LocaleID;

    return STATUS_SUCCESS;
}

PVOID (FASTCALL *StubGetNamedLocaleHashNode)(PWSTR LocaleName, LANGID Lcid);
static volatile LONG LepNamedLocalePrewarmHits;
static volatile LONG LepNamedLocalePrewarmRewrites;

PVOID FASTCALL LepGetNamedLocaleHashNode(PWSTR LocaleName, LANGID LangId)
{
    PLDR_MODULE Kernel;
    API_POINTER(LCIDToLocaleName) LCIDToLocaleName;
    PLepGlobalData GlobalData = LepGetGlobalData();
    INT LocaleNameLength;

    Kernel = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel == nullptr)
        Kernel = GetKernel32Ldr();

    if (Kernel == nullptr || LocaleName == nullptr ||
        !IN_RANGE(
            (ULONG_PTR)PtrAdd(Kernel->DllBase, sizeof(WCHAR)),
            (ULONG_PTR)LocaleName,
            (ULONG_PTR)PtrAdd(Kernel->DllBase, Kernel->SizeOfImage)
        ))
    {
        return StubGetNamedLocaleHashNode(LocaleName, LangId);
    }

    _InterlockedIncrement(&LepNamedLocalePrewarmHits);

    *(PVOID *)&LCIDToLocaleName = LookupExportTable(Kernel->DllBase, KERNEL32_LCIDToLocaleName);
    if (LCIDToLocaleName == nullptr)
        return StubGetNamedLocaleHashNode(LocaleName, LangId);

    LocaleNameLength = LCIDToLocaleName(
        GlobalData->GetLepb()->LocaleID,
        LocaleName,
        0xAC,
        0
    );
    if (LocaleNameLength > 0)
    {
        LocaleName[-1] = (USHORT)(LocaleNameLength - 1);
        _InterlockedIncrement(&LepNamedLocalePrewarmRewrites);
    }

    return StubGetNamedLocaleHashNode(LocaleName, LangId);
}

NTSTATUS LepGlobalData::HackUserDefaultLCID2(PVOID Kernel32)
{
    PVOID GetNamedLocaleHashNode;
    PLDR_MODULE Kernel;
    API_POINTER(GetUserDefaultLCID) GetUserDefaultLCID;
    LCID LocaleId;
    LONG Hits;
    LONG Rewrites;
    NTSTATUS Status;
    NTSTATUS RestoreStatus;

    Kernel = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel == nullptr)
        Kernel = FindLdrModuleByHandle(Kernel32);
    if (Kernel == nullptr)
        return STATUS_DLL_NOT_FOUND;

    GetNamedLocaleHashNode = GetRoutineAddress(
        Kernel->DllBase,
        "GetNamedLocaleHashNode"
    );
    if (GetNamedLocaleHashNode == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    *(PVOID *)&GetUserDefaultLCID = LookupExportTable(
        Kernel->DllBase,
        KERNEL32_GetUserDefaultLCID
    );
    if (GetUserDefaultLCID == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    _InterlockedExchange(&LepNamedLocalePrewarmHits, 0);
    _InterlockedExchange(&LepNamedLocalePrewarmRewrites, 0);

    PATCH_MEMORY_DATA p[] =
    {
        FunctionJumpVa(GetNamedLocaleHashNode, LepGetNamedLocaleHashNode, &StubGetNamedLocaleHashNode, LEP_FUNCTION_JUMP_OP),
    };

    Status = PatchMemory(p, countof(p));
    if (NT_FAILED(Status))
    {
        WriteLog(L"kernelbase named-locale prewarm: export=%p patch=%08X",
            GetNamedLocaleHashNode,
            Status);
        return Status;
    }

    LocaleId = GetUserDefaultLCID();
    Hits = _InterlockedCompareExchange(&LepNamedLocalePrewarmHits, 0, 0);
    Rewrites = _InterlockedCompareExchange(&LepNamedLocalePrewarmRewrites, 0, 0);
    RestoreStatus = RestoreMemory(StubGetNamedLocaleHashNode);

    WriteLog(
        L"kernelbase named-locale prewarm: export=%p patch=%08X restore=%08X hits=%u rewrites=%u locale=%04X",
        GetNamedLocaleHashNode,
        Status,
        RestoreStatus,
        (ULONG)Hits,
        (ULONG)Rewrites,
        LocaleId
    );

    return RestoreStatus;
}
void* GetFirstCallTarget(void* start_offset, DWORD parse_range, void **next) {
    void* res = nullptr;
    WalkOpCodeT(start_offset, parse_range,
        WalkOpCodeM(Buffer, OpLength, Ret)
    {
        if (Buffer[0] != CALL) {
            return STATUS_NOT_FOUND;
        }

        res = GetCallDestination(Buffer);
        *next = &Buffer[OpLength];
        return STATUS_SUCCESS;
    }
    );
    return res;
}

void* GetKthCallOrJumpTarget(void* start_offset, DWORD parse_range, int K) {
// returns nullptr if K == 0
    if (start_offset == nullptr)
        return nullptr;

    void* res = nullptr;
    WalkOpCodeT(start_offset, parse_range,
        WalkOpCodeM(Buffer, OpLength, Ret)
    {
        if (Buffer[0] != CALL && Buffer[0] != JUMP) {
            return STATUS_NOT_FOUND;
        }

        --K;
        if (K != 0) {
            return STATUS_NOT_FOUND;
        }

        res = GetCallDestination(Buffer);
        return STATUS_SUCCESS;
    }
    );
    return res;
}

void* GetKthCallTarget(void* start_offset, DWORD parse_range_each, int K) {
// returns nullptr if K == 0
    if (start_offset == nullptr)
        return nullptr;
    void *next, *res = nullptr;
    while (K >= 1) {
        res = GetFirstCallTarget(start_offset, parse_range_each, &next);
        if (res == nullptr)
            return nullptr;
        start_offset = next;
        --K;
    }
    return res;
}

typedef DWORD(__stdcall* pSetupAnsiOemCodeHashNodes)();

VOID LepSyncUser32ClientCodePage()
{
#if ML_AMD64
    PLepGlobalData GlobalData;
    ULONG_PTR Value;

    GlobalData = LepGetGlobalData();
    if (GlobalData == nullptr)
        return;

    Value = LepGetWin32ClientInfo()[LEP_WIN32_CLIENT_INFO_CODE_PAGE_INDEX];
    LepGetWin32ClientInfo()[LEP_WIN32_CLIENT_INFO_CODE_PAGE_INDEX] =
        (Value & ~0xFFFFull) | (USHORT)GlobalData->GetLepb()->AnsiCodePage;
#endif
}

static VOID LepProbeRtlAnsiTable(PCWSTR Tag, PCPTABLEINFO TargetTable)
{
#if ML_AMD64 && ENABLE_LOG
    WCHAR Sample[] = { 0xFF24, 0xFF49, 0xFF52, 0xFF45, 0xFF43, 0xFF54, 0xFF38, 0x3000, 0 };
    CHAR DefaultAnsi[32];
    CHAR CustomAnsi[32];
    ULONG DefaultBytes, CustomBytes;
    NTSTATUS DefaultStatus, CustomStatus;

    ZeroMemory(DefaultAnsi, sizeof(DefaultAnsi));
    ZeroMemory(CustomAnsi, sizeof(CustomAnsi));
    DefaultBytes = 0;
    CustomBytes = 0;

    DefaultStatus = RtlUnicodeToMultiByteN(DefaultAnsi, sizeof(DefaultAnsi), &DefaultBytes, Sample, (countof(Sample) - 1) * sizeof(WCHAR));
    CustomStatus = RtlUnicodeToCustomCPN(TargetTable, CustomAnsi, sizeof(CustomAnsi), &CustomBytes, Sample, (countof(Sample) - 1) * sizeof(WCHAR));

    WriteLog(L"nls rtl probe %s ntdll=%u mb=%u table=%u dbcs=%u def=%08X/%u %02X %02X %02X %02X %02X %02X %02X %02X custom=%08X/%u %02X %02X %02X %02X %02X %02X %02X %02X",
        Tag,
        NlsAnsiCodePage,
        NlsMbCodePageTag,
        TargetTable->CodePage,
        TargetTable->DBCSCodePage,
        DefaultStatus,
        DefaultBytes,
        (ULONG)(UCHAR)DefaultAnsi[0],
        (ULONG)(UCHAR)DefaultAnsi[1],
        (ULONG)(UCHAR)DefaultAnsi[2],
        (ULONG)(UCHAR)DefaultAnsi[3],
        (ULONG)(UCHAR)DefaultAnsi[4],
        (ULONG)(UCHAR)DefaultAnsi[5],
        (ULONG)(UCHAR)DefaultAnsi[6],
        (ULONG)(UCHAR)DefaultAnsi[7],
        CustomStatus,
        CustomBytes,
        (ULONG)(UCHAR)CustomAnsi[0],
        (ULONG)(UCHAR)CustomAnsi[1],
        (ULONG)(UCHAR)CustomAnsi[2],
        (ULONG)(UCHAR)CustomAnsi[3],
        (ULONG)(UCHAR)CustomAnsi[4],
        (ULONG)(UCHAR)CustomAnsi[5],
        (ULONG)(UCHAR)CustomAnsi[6],
        (ULONG)(UCHAR)CustomAnsi[7]);
#else
    UNREFERENCED_PARAMETER(Tag);
    UNREFERENCED_PARAMETER(TargetTable);
#endif
}

#if ML_AMD64
typedef NTSTATUS(NTAPI* pRtlpInitCodePageTables)(USHORT AnsiCodePage, USHORT OemCodePage);
#else
typedef NTSTATUS(__fastcall* pRtlpInitCodePageTables)(USHORT AnsiCodePage, USHORT OemCodePage);
#endif

static BOOL GetImageTextRange(PVOID Module, PBYTE* TextStart, PBYTE* TextEnd)
{
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;

    if (Module == nullptr || TextStart == nullptr || TextEnd == nullptr)
        return FALSE;

    DosHeader = (PIMAGE_DOS_HEADER)Module;
    NtHeaders = (PIMAGE_NT_HEADERS)PtrAdd(Module, DosHeader->e_lfanew);
    Section = IMAGE_FIRST_SECTION(NtHeaders);
    for (ULONG Index = 0; Index != NtHeaders->FileHeader.NumberOfSections; ++Index, ++Section)
    {
        ULONG TextSize;

        if (memcmp(Section->Name, ".text", 5) != 0)
            continue;

        TextSize = Section->Misc.VirtualSize != 0 ? Section->Misc.VirtualSize : Section->SizeOfRawData;
        if (TextSize == 0)
            return FALSE;

        *TextStart = (PBYTE)PtrAdd(Module, Section->VirtualAddress);
        *TextEnd = PtrAdd(*TextStart, TextSize);

        return TRUE;
    }

    return FALSE;
}

static BOOL IsCodePadding(BYTE Value)
{
    return Value == 0xCC || Value == 0x90;
}

static PVOID FindFunctionStartByPadding(PBYTE CallSite, PBYTE TextStart)
{
    PBYTE Limit;
    ULONG_PTR BackwardRange;

    BackwardRange = PtrOffset(CallSite, TextStart);
    BackwardRange = ML_MIN(BackwardRange, 0x120);
    Limit = PtrSub(CallSite, BackwardRange);
    for (PBYTE Buffer = PtrSub(CallSite, 1); Buffer > Limit; --Buffer)
    {
        PBYTE PaddingStart, PaddingEnd;

        if (!IsCodePadding(Buffer[0]))
            continue;

        PaddingEnd = PtrAdd(Buffer, 1);
        PaddingStart = Buffer;
        while (PaddingStart > Limit && IsCodePadding(PaddingStart[-1]))
            --PaddingStart;

        if (PtrOffset(PaddingEnd, PaddingStart) >= 4)
            return PaddingEnd;

        Buffer = PaddingStart;
    }

    return nullptr;
}

static BOOL IsCallSiteNearFunction(PBYTE CallSite, PBYTE Function, ULONG_PTR Range)
{
    if (CallSite < Function)
        return FALSE;

    return PtrOffset(CallSite, Function) < Range;
}

PVOID FindRtlpInitCodePageTables()
{
    PBYTE RtlInitCodePageTableRoutine;
    PBYTE TextStart;
    PBYTE TextEnd;
    PVOID Ntdll;
    PBYTE RtlInitNlsTablesRoutine;

    Ntdll = GetNtdllHandle();
    RtlInitCodePageTableRoutine = (PBYTE)::RtlInitCodePageTable;
    RtlInitNlsTablesRoutine = (PBYTE)::RtlInitNlsTables;
    if (!GetImageTextRange(Ntdll, &TextStart, &TextEnd))
        return nullptr;

    for (PBYTE Buffer = TextStart; Buffer + 5 < TextEnd; ++Buffer)
    {
        PVOID Destination;
        PVOID FunctionStart;
        BOOL HasSecondInitCall;

        if (Buffer[0] != CALL)
            continue;

        SEH_TRY
        {
            Destination = GetCallDestination(Buffer);
        }
        SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            continue;
        }

        if ((PBYTE)Destination != RtlInitCodePageTableRoutine)
            continue;

        if (IsCallSiteNearFunction(Buffer, RtlInitNlsTablesRoutine, 0x80))
            continue;

        FunctionStart = FindFunctionStartByPadding(Buffer, TextStart);
        if (FunctionStart == nullptr)
            continue;

        HasSecondInitCall = FALSE;
        for (PBYTE Next = Buffer + 5; Next + 5 < Buffer + 0x100 && Next + 5 < TextEnd; ++Next)
        {
            if (Next[0] != CALL)
                continue;

            SEH_TRY
            {
                Destination = GetCallDestination(Next);
            }
            SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                continue;
            }

            if ((PBYTE)Destination == RtlInitCodePageTableRoutine)
            {
                HasSecondInitCall = TRUE;
                break;
            }
        }
        if (!HasSecondInitCall)
            continue;

        return FunctionStart;
    }

    return nullptr;
}

NTSTATUS LepInitNtdllCodePageTables(USHORT AnsiCodePage, USHORT OemCodePage)
{
    pRtlpInitCodePageTables RtlpInitCodePageTables;

    *(PVOID *)&RtlpInitCodePageTables = FindRtlpInitCodePageTables();
    if (RtlpInitCodePageTables == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    WriteLog(L"RtlpInitCodePageTables: %p", RtlpInitCodePageTables);
    return RtlpInitCodePageTables(AnsiCodePage, OemCodePage);
}

#if ML_AMD64
#define LEP_NLS_DIAG 1
#else
#define LEP_NLS_DIAG 0
#endif

#if LEP_NLS_DIAG
VOID LepNlsDiag(PCWSTR Format, ...)
{
    UNREFERENCED_PARAMETER(Format);
}
#else
VOID LepNlsDiag(PCWSTR Format, ...)
{
    UNREFERENCED_PARAMETER(Format);
}
#endif

PVOID FindSetupAnsiOemCodeHashNodes(PLDR_MODULE Kernel)
{
    PBYTE ImageBase, ImageEnd;
    PVOID KernelBaseBaseDllInitialize;
    PVOID KernelBaseBaseDllInitializeInternal;
    PVOID BaseNlsDllInitialize;
    PVOID NlsProcessInitialize;
    PVOID SetupAnsiOemCodeHashNodes;

    ImageBase = (PBYTE)Kernel->DllBase;
    ImageEnd = ImageBase + Kernel->SizeOfImage;

    KernelBaseBaseDllInitialize = GetKthCallTarget(Kernel->EntryPoint, 0x40, 1);
    if (!IN_RANGEEX(ImageBase, (PBYTE)KernelBaseBaseDllInitialize, ImageEnd))
    {
        WriteLog(L"find kbase nls: KernelBaseBaseDllInitialize failed: %p", KernelBaseBaseDllInitialize);
        return nullptr;
    }
    WriteLog(L"KernelBaseBaseDllInitialize: %p", KernelBaseBaseDllInitialize);

    KernelBaseBaseDllInitializeInternal = GetKthCallOrJumpTarget(KernelBaseBaseDllInitialize, 0x100, 2);
    if (!IN_RANGEEX(ImageBase, (PBYTE)KernelBaseBaseDllInitializeInternal, ImageEnd))
    {
        WriteLog(L"find kbase nls: _KernelBaseBaseDllInitialize failed: %p", KernelBaseBaseDllInitializeInternal);
        return nullptr;
    }
    WriteLog(L"KernelBaseBaseDllInitializeInternal: %p", KernelBaseBaseDllInitializeInternal);

    BaseNlsDllInitialize = nullptr;
    WalkOpCodeT(KernelBaseBaseDllInitializeInternal, 0x800,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            UNREFERENCED_PARAMETER(OpLength);
            UNREFERENCED_PARAMETER(Ret);

            if (Buffer[0] != 0xB8 || *((DWORD *)&Buffer[1]) != 0x190)
                return STATUS_NOT_FOUND;

            BaseNlsDllInitialize = GetKthCallTarget(Buffer, 0x30, 1);
            return STATUS_SUCCESS;
        }
    );

    if (!IN_RANGEEX(ImageBase, (PBYTE)BaseNlsDllInitialize, ImageEnd))
    {
        WriteLog(L"find kbase nls: BaseNlsDllInitialize failed: %p", BaseNlsDllInitialize);
        return nullptr;
    }
    WriteLog(L"BaseNlsDllInitialize: %p", BaseNlsDllInitialize);

    NlsProcessInitialize = GetKthCallTarget(BaseNlsDllInitialize, 0x30, 1);
    if (!IN_RANGEEX(ImageBase, (PBYTE)NlsProcessInitialize, ImageEnd))
    {
        WriteLog(L"find kbase nls: NlsProcessInitialize failed: %p", NlsProcessInitialize);
        return nullptr;
    }
    WriteLog(L"NlsProcessInitialize: %p", NlsProcessInitialize);

    SetupAnsiOemCodeHashNodes = GetKthCallTarget(NlsProcessInitialize, 0x30, 3);
    if (!IN_RANGEEX(ImageBase, (PBYTE)SetupAnsiOemCodeHashNodes, ImageEnd))
    {
        WriteLog(L"find kbase nls: SetupAnsiOemCodeHashNodes failed: %p", SetupAnsiOemCodeHashNodes);
        return nullptr;
    }
    WriteLog(L"SetupAnsiOemCodeHashNodes: %p", SetupAnsiOemCodeHashNodes);

    return SetupAnsiOemCodeHashNodes;
}

NTSTATUS LepSetupAnsiOemCodeHashNodes() {

    RTL_OSVERSIONINFOW VersionInfo;
    NTSTATUS Status;

    Status = Nt_QueryOsVersion(&VersionInfo);
    FAIL_RETURN(Status);
    if (VersionInfo.dwMajorVersion < 10 || VersionInfo.dwBuildNumber < 19042)
        return STATUS_SUCCESS; // does not need this trick for older versions.


    PLDR_MODULE Kernel = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    auto the_func = (pSetupAnsiOemCodeHashNodes)FindSetupAnsiOemCodeHashNodes(Kernel);
    if (the_func == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    the_func();
    return STATUS_SUCCESS;
}

NTSTATUS LepGlobalData::HackAnsiOemCodeHashNodes() {
    PLepGlobalData GlobalData = LepGetGlobalData();
    LepNlsDiag(L"HackAnsiOemCodeHashNodes entry target=%u/%u", GlobalData->GetLepb()->AnsiCodePage, GlobalData->GetLepb()->OemCodePage);

    NLSTABLEINFO NlsTableInfo;
    CPTABLEINFO AnsiTableInfo, OemTableInfo;
    NTSTATUS Status, NtdllStatus;

    RtlInitCodePageTable((PUSHORT)PtrAdd(GlobalData->CodePageMapView, GlobalData->AnsiCodePageOffset), &AnsiTableInfo);
    RtlInitCodePageTable((PUSHORT)PtrAdd(GlobalData->CodePageMapView, GlobalData->OemCodePageOffset), &OemTableInfo);

#if ML_AMD64
    WriteLog(L"nls sync before peb=%u/%u ntdll=%u mb=%u oemmb=%u table=%u/%u",
        LepGetProcessAnsiCodePage(),
        LepGetProcessOemCodePage(),
        NlsAnsiCodePage,
        NlsMbCodePageTag,
        NlsMbOemCodePageTag,
        AnsiTableInfo.CodePage,
        OemTableInfo.CodePage);

    LepProbeRtlAnsiTable(L"before", &AnsiTableInfo);

    LepSetProcessCodePagePair(
        (USHORT)GlobalData->GetLepb()->AnsiCodePage,
        (USHORT)GlobalData->GetLepb()->OemCodePage);
    LepSyncUser32ClientCodePage();
#else
    PUSHORT ProcessCodePagePair = LepGetProcessCodePagePair();

    WriteLog(L"nls sync before peb=%u/%u ntdll=%u mb=%u oemmb=%u table=%u/%u",
        ProcessCodePagePair[0],
        ProcessCodePagePair[1],
        NlsAnsiCodePage,
        NlsMbCodePageTag,
        NlsMbOemCodePageTag,
        AnsiTableInfo.CodePage,
        OemTableInfo.CodePage);

    LepSetProcessCodePagePair(
        (USHORT)GlobalData->GetLepb()->AnsiCodePage,
        (USHORT)GlobalData->GetLepb()->OemCodePage);
#endif

    RtlInitNlsTables(
        (PUSHORT)PtrAdd(GlobalData->CodePageMapView, GlobalData->AnsiCodePageOffset),
        (PUSHORT)PtrAdd(GlobalData->CodePageMapView, GlobalData->OemCodePageOffset),
        (PUSHORT)PtrAdd(GlobalData->CodePageMapView, GlobalData->UnicodeCaseTableOffset),
        &NlsTableInfo);
    RtlResetRtlTranslations(&NlsTableInfo);
    LepSyncNtdllNlsGlobals(
        (USHORT)GlobalData->GetLepb()->AnsiCodePage,
        (BOOLEAN)(AnsiTableInfo.DBCSCodePage != 0),
        (BOOLEAN)(OemTableInfo.DBCSCodePage != 0));
    LepProbeRtlAnsiTable(L"after-reset", &AnsiTableInfo);

    NtdllStatus = LepInitNtdllCodePageTables(
        (USHORT)GlobalData->GetLepb()->AnsiCodePage,
        (USHORT)GlobalData->GetLepb()->OemCodePage);
    if (NT_FAILED(NtdllStatus))
    {
#if ENABLE_LOG
        WriteLog(L"nls sync ignores RtlpInitCodePageTables status=%08X", NtdllStatus);
#endif
    }

    // Keep the kernelbase private refresh implementation available for
    // diagnostics, but skip it in the normal path. Process/ntdll/user32 cache
    // sync is enough, and this private routine is fragile across builds.
    Status = STATUS_SUCCESS;
#if ENABLE_LOG
    WriteLog(L"skip SetupAnsiOemCodeHashNodes");
#endif

#if ML_AMD64
    LepProbeRtlAnsiTable(L"after-kbase", &AnsiTableInfo);

    WriteLog(L"nls sync after peb=%u/%u ntdll=%u mb=%u oemmb=%u user32cp=%u ntdllStatus=%08X status=%08X",
        LepGetProcessAnsiCodePage(),
        LepGetProcessOemCodePage(),
        NlsAnsiCodePage,
        NlsMbCodePageTag,
        NlsMbOemCodePageTag,
        LepGetUser32ClientCodePage(),
        NtdllStatus,
        Status);
#else
    ProcessCodePagePair = LepGetProcessCodePagePair();
    WriteLog(L"nls sync after peb=%u/%u ntdll=%u mb=%u oemmb=%u ntdllStatus=%08X status=%08X",
        ProcessCodePagePair[0],
        ProcessCodePagePair[1],
        NlsAnsiCodePage,
        NlsMbCodePageTag,
        NlsMbOemCodePageTag,
        NtdllStatus,
        Status);
#endif

    return Status;
}

NTSTATUS LepGlobalData::HookKernel32Routines(PVOID Kernel32)
{
    PLDR_MODULE HookedModule;
    NTSTATUS Status;

    LepNlsDiag(L"HookKernel32Routines entry Kernel32=%p", Kernel32);

    HookedModule = FindLdrModuleByHandle(Kernel32);
    if (HookedModule != nullptr &&
        RtlEqualUnicodeString(&HookedModule->BaseDllName, &USTR(L"KERNELBASE.dll"), TRUE))
    {
        // This routine is also entered once for kernel32.dll.  The named-locale
        // cache being prewarmed belongs to KernelBase, so patch it exactly once
        // and keep the hit/rewrite counters unambiguous.
        Status = this->HackUserDefaultLCID2(Kernel32);
        LepNlsDiag(L"HackUserDefaultLCID2 status=%08X", Status);

    }

    Status = this->HackAnsiOemCodeHashNodes();
    LepNlsDiag(L"HackAnsiOemCodeHashNodes status=%08X", Status);

    WriteLog(L"hook k32: %p", Status);

    if (GetLepb()->HookUILanguageMode == 1 &&
        HookedModule != nullptr &&
        RtlEqualUnicodeString(&HookedModule->BaseDllName, &USTR(L"KERNEL32.dll"), TRUE))
    {
        PVOID SystemApi = GetRoutineAddress(Kernel32, "GetSystemDefaultUILanguage");
        PVOID UserApi = GetRoutineAddress(Kernel32, "GetUserDefaultUILanguage");
        if (SystemApi == nullptr || UserApi == nullptr)
            return STATUS_PROCEDURE_NOT_FOUND;

        Mp::PATCH_MEMORY_DATA Patches[] =
        {
            Mp::FunctionJumpVa(SystemApi, LepGetSystemDefaultUILanguage,
                &HookStub.StubGetSystemDefaultUILanguage, LEP_FUNCTION_JUMP_OP),
            Mp::FunctionJumpVa(UserApi, LepGetUserDefaultUILanguage,
                &HookStub.StubGetUserDefaultUILanguage, LEP_FUNCTION_JUMP_OP),
        };
        Status = Mp::PatchMemory(Patches, countof(Patches));
        WriteLog(L"hook Win32 UI language APIs: %08X", Status);
        FAIL_RETURN(Status);
    }

    return Status;
}

NTSTATUS LepGlobalData::UnHookKernel32Routines()
{
    Mp::RestoreMemory(HookStub.StubGetSystemDefaultUILanguage);
    Mp::RestoreMemory(HookStub.StubGetUserDefaultUILanguage);
    return 0;
}
