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

PVOID FASTCALL LepGetNamedLocaleHashNode(PWSTR LocaleName, LANGID LangId)
{
    PLDR_MODULE Kernel;
    API_POINTER(LCIDToLocaleName) LCIDToLocaleName;
    PLepGlobalData GlobalData = LepGetGlobalData();

    Kernel = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel == nullptr)
        Kernel = GetKernel32Ldr();

    if (!IN_RANGE((ULONG_PTR)Kernel->DllBase, (ULONG_PTR)LocaleName, (ULONG_PTR)PtrAdd(Kernel->DllBase, Kernel->SizeOfImage)))
    {
        return StubGetNamedLocaleHashNode(LocaleName, LangId);
    }

    *(PVOID *)&LCIDToLocaleName = LookupExportTable(Kernel->DllBase, KERNEL32_LCIDToLocaleName);

    LocaleName[-1] = (USHORT)(LCIDToLocaleName(GlobalData->GetLepb()->LocaleID, LocaleName, 0xAC, 0) - 1);

    return StubGetNamedLocaleHashNode(LocaleName, LangId);
}

NTSTATUS LepGlobalData::HackUserDefaultLCID2(PVOID Kernel32)
{
    PVOID GetNLSVersionEx, GetNamedLocaleHashNode;
    PLDR_MODULE Kernel;
    API_POINTER(GetUserDefaultLCID) GetUserDefaultLCID;

    Kernel = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel == nullptr)
        Kernel = FindLdrModuleByHandle(Kernel32);

    *(PVOID *)&GetNLSVersionEx = LookupExportTable(Kernel->DllBase, KERNEL32_GetNLSVersionEx);

    GetNamedLocaleHashNode = nullptr;

    WalkOpCodeT(GetNLSVersionEx, 0x20,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            if (Buffer[0] != CALL)
                return STATUS_NOT_FOUND;

            *(PVOID *)&GetNamedLocaleHashNode = GetCallDestination(Buffer);
            return STATUS_SUCCESS;
        }
    );

    if (GetNamedLocaleHashNode == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    PATCH_MEMORY_DATA p[] =
    {
        FunctionJumpVa(GetNamedLocaleHashNode, LepGetNamedLocaleHashNode, &StubGetNamedLocaleHashNode, LEP_FUNCTION_JUMP_OP),
    };

    PatchMemory(p, countof(p));

    *(PVOID *)&GetUserDefaultLCID = LookupExportTable(Kernel32, KERNEL32_GetUserDefaultLCID);
    GetUserDefaultLCID();

    RestoreMemory(StubGetNamedLocaleHashNode);

    return STATUS_SUCCESS;
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

NTSTATUS LepSetupAnsiOemCodeHashNodes() {

    RTL_OSVERSIONINFOW osvi;

    ZeroMemory(&osvi, sizeof(RTL_OSVERSIONINFOW));
    osvi.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);

    RtlGetVersion(&osvi);
    if (osvi.dwMajorVersion < 10 || osvi.dwBuildNumber < 19042)
        return STATUS_SUCCESS; // does not need this trick for older versions.


    PLDR_MODULE Kernel = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    void* pKernelBaseBaseDllInitialize = GetKthCallTarget(Kernel->EntryPoint, 0x30, 1);
    if (pKernelBaseBaseDllInitialize == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    WriteLog(L"KernelBaseBaseDllInitialize: %p\n", pKernelBaseBaseDllInitialize);

    void* pKernelBaseBaseDllInitializeInternal = GetKthCallOrJumpTarget(pKernelBaseBaseDllInitialize, 0x80, 2);
    if (pKernelBaseBaseDllInitializeInternal == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    WriteLog(L"KernelBaseBaseDllInitializeInternal: %p\n", pKernelBaseBaseDllInitializeInternal);

    void* pBaseNlsDllInitialize = nullptr;
    WalkOpCodeT(pKernelBaseBaseDllInitializeInternal, 0x800,
        WalkOpCodeM(Buffer, OpLength, Ret)
    {
        if (Buffer[0] != 0xB8 || *((DWORD*)&Buffer[1]) != 0x190) {
            // locate first `mov eax, 0x190`
            return STATUS_NOT_FOUND;
        }
        pBaseNlsDllInitialize = GetKthCallTarget(Buffer, 0x30, 1);
        return STATUS_SUCCESS;
    }
    );
    if (pBaseNlsDllInitialize == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    WriteLog(L"BaseNlsDllInitialize: %p\n", pBaseNlsDllInitialize);

    void* pNlsProcessInitialize = GetKthCallTarget(pBaseNlsDllInitialize, 0x30, 1);
    if (pNlsProcessInitialize == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    WriteLog(L"NlsProcessInitialize: %p\n", pNlsProcessInitialize);

    auto the_func = (pSetupAnsiOemCodeHashNodes)GetKthCallTarget(pNlsProcessInitialize, 0x30, 3);
    if (the_func == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    WriteLog(L"SetupAnsiOemCodeHashNodes: %p\n", the_func);

    the_func();
    return STATUS_SUCCESS;
}

NTSTATUS LepGlobalData::HackAnsiOemCodeHashNodes() {
    PLepGlobalData GlobalData = LepGetGlobalData();
    LepNlsDiag(L"HackAnsiOemCodeHashNodes entry target=%u/%u", GlobalData->GetLepb()->AnsiCodePage, GlobalData->GetLepb()->OemCodePage);

#if ML_AMD64
    PPEB_BASE Peb = CurrentPeb();

    *(PUSHORT)PtrAdd(Peb, 0x34C) = (USHORT)GlobalData->GetLepb()->AnsiCodePage;
    *(PUSHORT)PtrAdd(Peb, 0x34E) = (USHORT)GlobalData->GetLepb()->OemCodePage;

    return LepSetupAnsiOemCodeHashNodes();
#else
    unsigned char* pTeb = (unsigned char*)(__readfsdword(48));
    *(short*)(pTeb + 0x228) = GlobalData->GetLepb()->AnsiCodePage;
    *(short*)(pTeb + 0x22a) = GlobalData->GetLepb()->OemCodePage;

    return LepSetupAnsiOemCodeHashNodes();
#endif
}

NTSTATUS LepGlobalData::HookKernel32Routines(PVOID Kernel32)
{
    PVOID GetCurrentNlsCache;
    NTSTATUS Status;

    LepNlsDiag(L"HookKernel32Routines entry Kernel32=%p", Kernel32);

    Status = this->HackUserDefaultLCID2(Kernel32);
    LepNlsDiag(L"HackUserDefaultLCID2 status=%08X", Status);

    Status = this->HackAnsiOemCodeHashNodes();
    LepNlsDiag(L"HackAnsiOemCodeHashNodes status=%08X", Status);

    WriteLog(L"hook k32: %p", Status);

    return Status;
}

NTSTATUS LepGlobalData::UnHookKernel32Routines()
{
    return 0;
}
