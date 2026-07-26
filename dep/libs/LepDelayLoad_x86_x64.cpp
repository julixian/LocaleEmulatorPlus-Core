#include "../../LocaleEmulatorPlus/stdafx.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

typedef DWORD RVA;

typedef struct ImgDelayDescrV2
{
    DWORD grAttrs;
    RVA   rvaDLLName;
    RVA   rvaHmod;
    RVA   rvaIAT;
    RVA   rvaINT;
    RVA   rvaBoundIAT;
    RVA   rvaUnloadIAT;
    DWORD dwTimeStamp;
} ImgDelayDescr, *PImgDelayDescr;

typedef const ImgDelayDescr *PCImgDelayDescr;

enum
{
    dlattrRva = 1,
};

extern "C" PVOID __pfnDliNotifyHook2 = nullptr;
extern "C" PVOID __pfnDliFailureHook2 = nullptr;
extern "C" int __bChangeProtectionOfWholeDloadSection = 0;

static PVOID LepDelayFromRva(RVA Rva)
{
    return PtrAdd(&__ImageBase, Rva);
}

static VOID LepDelayInitAnsiString(PANSI_STRING String, PCSTR Buffer)
{
    USHORT Length;

    Length = 0;
    while (Buffer[Length] != 0)
        ++Length;

    String->Length = Length;
    String->MaximumLength = Length + 1;
    String->Buffer = (PSTR)Buffer;
}

static BOOL LepDelayAnsiToUnicode(PUNICODE_STRING Unicode, PCSTR Ansi, PWSTR Buffer, USHORT BufferChars)
{
    USHORT Length;

    Length = 0;
    while (Ansi[Length] != 0)
    {
        if (Length + 1 >= BufferChars)
            return FALSE;

        Buffer[Length] = (WCHAR)(UCHAR)Ansi[Length];
        ++Length;
    }

    Buffer[Length] = 0;
    Unicode->Length = Length * sizeof(WCHAR);
    Unicode->MaximumLength = BufferChars * sizeof(WCHAR);
    Unicode->Buffer = Buffer;

    return TRUE;
}

extern "C" PVOID WINAPI __delayLoadHelper2(PCImgDelayDescr Descriptor, PVOID *ThunkAddress)
{
    PVOID ModuleBase;
    PVOID Procedure;
    PCSTR DllName;
    PVOID *ModuleHandle;
    PIMAGE_THUNK_DATA Iat;
    PIMAGE_THUNK_DATA Int;
    PIMAGE_THUNK_DATA ImportName;
    ULONG_PTR Index;
    NTSTATUS Status;
    WCHAR DllNameBuffer[MAX_NTPATH];
    UNICODE_STRING UnicodeDllName;
    ANSI_STRING ProcedureName;

    if (Descriptor == nullptr || ThunkAddress == nullptr)
        return nullptr;

    if (FLAG_ON(Descriptor->grAttrs, dlattrRva))
    {
        DllName = (PCSTR)LepDelayFromRva(Descriptor->rvaDLLName);
        ModuleHandle = (PVOID *)LepDelayFromRva(Descriptor->rvaHmod);
        Iat = (PIMAGE_THUNK_DATA)LepDelayFromRva(Descriptor->rvaIAT);
        Int = (PIMAGE_THUNK_DATA)LepDelayFromRva(Descriptor->rvaINT);
    }
    else
    {
        DllName = (PCSTR)(ULONG_PTR)Descriptor->rvaDLLName;
        ModuleHandle = (PVOID *)(ULONG_PTR)Descriptor->rvaHmod;
        Iat = (PIMAGE_THUNK_DATA)(ULONG_PTR)Descriptor->rvaIAT;
        Int = (PIMAGE_THUNK_DATA)(ULONG_PTR)Descriptor->rvaINT;
    }

    if (DllName == nullptr || ModuleHandle == nullptr || Iat == nullptr || Int == nullptr)
        return nullptr;

    ModuleBase = *ModuleHandle;
    if (ModuleBase == nullptr)
    {
        if (!LepDelayAnsiToUnicode(&UnicodeDllName, DllName, DllNameBuffer, countof(DllNameBuffer)))
            return nullptr;

        Status = LdrLoadDll(nullptr, nullptr, &UnicodeDllName, &ModuleBase);
        if (NT_FAILED(Status) || ModuleBase == nullptr)
            return nullptr;

        *ModuleHandle = ModuleBase;
    }

    Index = ((PIMAGE_THUNK_DATA)ThunkAddress) - Iat;
    ImportName = &Int[Index];

    if (IMAGE_SNAP_BY_ORDINAL(ImportName->u1.Ordinal))
    {
        Status = LdrGetProcedureAddress(ModuleBase, nullptr, (USHORT)IMAGE_ORDINAL(ImportName->u1.Ordinal), &Procedure);
    }
    else
    {
        PIMAGE_IMPORT_BY_NAME Name;

        Name = (PIMAGE_IMPORT_BY_NAME)LepDelayFromRva((RVA)ImportName->u1.AddressOfData);
        LepDelayInitAnsiString(&ProcedureName, (PCSTR)Name->Name);
        Status = LdrGetProcedureAddress(ModuleBase, &ProcedureName, 0, &Procedure);
    }

    if (NT_FAILED(Status) || Procedure == nullptr)
        return nullptr;

    *ThunkAddress = Procedure;
    return Procedure;
}

extern "C" BOOL WINAPI __FUnloadDelayLoadedDLL2(PCSTR)
{
    return FALSE;
}

extern "C" HRESULT WINAPI __HrLoadAllImportsForDll(PCSTR)
{
    return S_OK;
}
