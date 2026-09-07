#ifndef _LocaleEmulatorPlus_H_cd444a0d_c7f9_44b2_aac8_8107e9a07ca2_
#define _LocaleEmulatorPlus_H_cd444a0d_c7f9_44b2_aac8_8107e9a07ca2_

#include "ml.h"


#define ROOTDIR_SYSTEM32            L"\\SystemRoot\\system32\\"

#define REGKEY_ROOT                 HKEY_LOCAL_MACHINE

#define REGPATH_CODEPAGE            L"System\\CurrentControlSet\\Control\\Nls\\CodePage"
#define REGPATH_LANGUAGE            L"System\\CurrentControlSet\\Control\\Nls\\Language"
#define REGKEY_ACP                  L"ACP"
#define REGKEY_OEMCP                L"OEMCP"
#define REGKEY_DEFAULT_LANGUAGE     L"Default"

#define PROP_WINDOW_ANSI_PROC       L"Asuna"

#define FORMAT_LOCALE_EMULATOR_PLUS_PROCESS_ENVIRONMENT_BLOCK   L"Local\\LOCALE_EMULATOR_PLUS_PROCESS_ENVIRONMENT_BLOCK_SECTION_%p"


#if ML_AMD64
#define LEP_FUNCTION_JUMP_OP                     Mp::OpJumpIndirect
#define LEP_FUNCTION_SHORT_JUMP_OP               Mp::OpJump
#define LEP_FUNCTION_NO_ABSOLUTE_JUMP_OP         (Mp::OpJumpIndirect | Mp::NoAbsoluteJump)
#else
#define LEP_FUNCTION_JUMP_OP                     Mp::OpJump
#define LEP_FUNCTION_SHORT_JUMP_OP               Mp::OpJump
#define LEP_FUNCTION_NO_ABSOLUTE_JUMP_OP         Mp::OpJump
#endif

#define LepHookFromEAT(_Base, _Prefix, _Name)    Mp::FunctionJumpVa(LookupExportTable(_Base, _Prefix##_##_Name), Lep##_Name, &HookStub.Stub##_Name, LEP_FUNCTION_JUMP_OP)
#define LepHookFromEATOp(_Base, _Prefix, _Name, _Op) Mp::FunctionJumpVa(LookupExportTable(_Base, _Prefix##_##_Name), Lep##_Name, &HookStub.Stub##_Name, _Op)
#define LepHookFromEAT2(_Base, _Prefix, _Name)   Mp::FunctionJumpVa(LookupExportTable(_Base, _Prefix##_##_Name), Lep##_Name, nullptr, LEP_FUNCTION_JUMP_OP)
#define LepFunctionJump(_Name)                   Mp::FunctionJumpVa(_Name, Lep##_Name, &HookStub.Stub##_Name, LEP_FUNCTION_JUMP_OP)
#define LepFunctionCall(_Name)                   Mp::FunctionCallVa(_Name, Lep##_Name, &HookStub.Stub##_Name)

class LepGlobalData;
typedef LepGlobalData* PLepGlobalData;

typedef ULONG (NTAPI *PLEP_QUERY_FONT_ASSOC_STATUS)();
typedef LANGID (WINAPI *PLEP_GET_DEFAULT_UI_LANGUAGE)();

VOID LepNlsDiag(PCWSTR Format, ...);
VOID LepSyncUser32ClientCodePage();

#if ML_AMD64
static const ULONG_PTR LEP_TEB_WIN32_CLIENT_INFO_OFFSET = 0x800;
static const ULONG_PTR LEP_PEB_NLS_CODE_PAGE_PAIR_OFFSET = 0x34C;
#else
static const ULONG_PTR LEP_X86_PEB_NLS_CODE_PAGE_PAIR_OFFSET = 0x228;
#endif
static const ULONG LEP_WIN32_CLIENT_INFO_CODE_PAGE_INDEX = 19;

ForceInline PPEB_BASE LepCurrentPeb()
{
    return CurrentPeb();
}

ForceInline PTEB_BASE LepCurrentTeb()
{
    return CurrentTeb();
}

ForceInline PULONG_PTR LepGetWin32ClientInfo()
{
#if ML_AMD64
    return (PULONG_PTR)PtrAdd(LepCurrentTeb(), LEP_TEB_WIN32_CLIENT_INFO_OFFSET);
#else
    return (PULONG_PTR)LepCurrentTeb()->User32Reserved;
#endif
}

ForceInline USHORT LepGetUser32ClientCodePage()
{
    return (USHORT)LepGetWin32ClientInfo()[LEP_WIN32_CLIENT_INFO_CODE_PAGE_INDEX];
}

#if ML_AMD64
ForceInline PULONG LepGetProcessCodePagePair()
{
    return (PULONG)PtrAdd(LepCurrentPeb(), LEP_PEB_NLS_CODE_PAGE_PAIR_OFFSET);
}

ForceInline USHORT LepGetProcessAnsiCodePage()
{
    return LOWORD(*LepGetProcessCodePagePair());
}

ForceInline USHORT LepGetProcessOemCodePage()
{
    return HIWORD(*LepGetProcessCodePagePair());
}

ForceInline VOID LepSetProcessCodePagePair(USHORT AnsiCodePage, USHORT OemCodePage)
{
    *LepGetProcessCodePagePair() = MAKELONG(AnsiCodePage, OemCodePage);
}
#else

ForceInline PUSHORT LepGetProcessCodePagePair()
{
    return (PUSHORT)PtrAdd(LepCurrentPeb(), LEP_X86_PEB_NLS_CODE_PAGE_PAIR_OFFSET);
}

ForceInline VOID LepSetProcessCodePagePair(USHORT AnsiCodePage, USHORT OemCodePage)
{
    PUSHORT CodePagePair = LepGetProcessCodePagePair();

    CodePagePair[0] = AnsiCodePage;
    CodePagePair[1] = OemCodePage;
}

#endif

#define THREAD_LOCAL_BUFFER_CONTEXT TAG4('LTLB')
#define LEP_LOADER_PROCESS           TAG4('LepL')

#if LEP_DIAG_INIT
#define LEP_DIAG_HEADER(_stage) ExceptionBox(_stage, L"LEP modern init diag")
#define LEP_DIAG_HEADER_IF(_condition, _stage) \
    do { if (_condition) ExceptionBox(_stage, L"LEP modern init diag"); } while (0)
#else
#define LEP_DIAG_HEADER(_stage)
#define LEP_DIAG_HEADER_IF(_condition, _stage)
#endif

inline BOOL IsLepLoader()
{
    return FindThreadFrame(LEP_LOADER_PROCESS) != nullptr;
}

typedef struct THREAD_LOCAL_BUFFER : public TEB_ACTIVE_FRAME
{
    BYTE Buffer[MEMORY_PAGE_SIZE * 2];

    THREAD_LOCAL_BUFFER()
    {
        this->Context = THREAD_LOCAL_BUFFER_CONTEXT;
    }

    PVOID GetBuffer()
    {
        return this == nullptr ? nullptr : &Buffer;
    }

    NoInline static THREAD_LOCAL_BUFFER* GetTlb(BOOL Allocate)
    {
        PTHREAD_LOCAL_BUFFER Tlb;

        Tlb = (PTHREAD_LOCAL_BUFFER)FindThreadFrame(THREAD_LOCAL_BUFFER_CONTEXT);

        if (Tlb != nullptr || Allocate == FALSE)
            return Tlb;

        Tlb = new THREAD_LOCAL_BUFFER;
        if (Tlb != nullptr)
        {
            Tlb->Push();
        }

        return Tlb;
    }

    NoInline static VOID ReleaseTlb()
    {
        delete GetTlb(FALSE);
    }

} THREAD_LOCAL_BUFFER, *PTHREAD_LOCAL_BUFFER;

typedef struct
{
    HDC                 DC;
    HFONT               Font;
    HFONT               OldFont;
    ULONG_PTR           FontType;
    LPENUMLOGFONTEXW    EnumLogFontEx;

} ADJUST_FONT_DATA, *PADJUST_FONT_DATA;

typedef struct TEXT_METRIC_INTERNAL
{
    ULONG       Magic;
    BOOL        Filled;
    TEXTMETRICA TextMetricA;
    TEXTMETRICW TextMetricW;

    TEXT_METRIC_INTERNAL()
    {
        this->Magic = TAG4('TMIN');
        this->Filled = FALSE;
    }

    BOOL VerifyMagic()
    {
        return this->Magic == TAG4('TMIN');
    }

} TEXT_METRIC_INTERNAL, *PTEXT_METRIC_INTERNAL;

typedef struct
{
    ULONG64             Root;
    UNICODE_STRING64    SubKey;
    UNICODE_STRING64    ValueName;
    ULONG               DataType;
    PVOID64             Data;
    ULONG64             DataSize;

} REGISTRY_ENTRY64;

typedef struct
{
    REGISTRY_ENTRY64 Original;
    REGISTRY_ENTRY64 Redirected;

} REGISTRY_REDIRECTION_ENTRY64, *PREGISTRY_REDIRECTION_ENTRY64;

typedef struct
{
    ULONG                           AnsiCodePage;
    ULONG                           OemCodePage;
    ULONG                           LocaleID;
    ULONG                           DefaultCharset;
    ULONG                           HookUILanguageApi;
    WCHAR                           DefaultFaceName[LF_FACESIZE];
    RTL_TIME_ZONE_INFORMATION       Timezone;
    ULONG64                         NumberOfRegistryRedirectionEntries;
    REGISTRY_REDIRECTION_ENTRY64    RegistryReplacement[1];

} LOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK, *PLOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK, LEPB, *PLEPB;

#if ML_AMD64
#define LDR_LOAD_DLL_BACKUP_SIZE 14
#else
#define LDR_LOAD_DLL_BACKUP_SIZE 5
#endif

enum LEP_CHILD_INJECTION_FLAGS
{
    LEP_INJECT_WRITE_SHADOW       = 0x00000001,
    LEP_INJECT_PATCH_LDR_LOAD_DLL = 0x00000004,
    // Diagnostic bootstrap: restore LdrLoadDll, then return without Initialize.
    LEP_INJECT_RESTORE_ONLY      = 0x00000008,
    LEP_INJECT_FULL = LEP_INJECT_WRITE_SHADOW |
                      LEP_INJECT_PATCH_LDR_LOAD_DLL
};

#define LEP_FIRST_DLL_BOOTSTRAP_MAGIC TAG4('L1DB')
#define LEP_FIRST_DLL_BOOTSTRAP_EXPORT "LepFirstDllBootstrapData"
#define LEP_BOOTSTRAP_PAYLOAD_MAGIC TAG4('LBP1')
#define LEP_BOOTSTRAP_PAYLOAD_VERSION 1
#define LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE 0x1000000
#define LEP_BOOTSTRAP_IMAGE_MAX_SIZE 0x10000000

typedef struct
{
    ULONG       Magic;
    ULONG       InjectionFlags;
    ULONG       BackupSize;
    ULONG       OriginalProtect;
    PVOID       LdrLoadDllAddress;
    ULONG       PayloadOffset;
    ULONG       PayloadSize;
    BYTE        Backup[LDR_LOAD_DLL_BACKUP_SIZE];
} LEP_FIRST_DLL_BOOTSTRAP_DATA, *PLEP_FIRST_DLL_BOOTSTRAP_DATA;

typedef struct
{
    ULONG Magic;
    ULONG Version;
    ULONG HeaderSize;
    ULONG TotalSize;
    ULONG EnvironmentOffset;
    ULONG EnvironmentSize;
    ULONG LepDllFullPathOffset;
    ULONG LepDllFullPathLength;
    ULONG LepDllDirPathOffset;
    ULONG LepDllDirPathLength;
} LEP_BOOTSTRAP_PAYLOAD, *PLEP_BOOTSTRAP_PAYLOAD;

enum LEP_BOOTSTRAP_STAGE
{
    LepBootstrapStageNone = 0,
    LepBootstrapStageValidateMetadata,
    LepBootstrapStageRestoreLdrLoadDll,
    LepBootstrapStageRestoreProtection,
    LepBootstrapStageInitialize,
    LepBootstrapStageComplete
};

#pragma warning(push)
#pragma warning(disable:4324)

typedef struct DECL_ALIGN(16) REGISTRY_ENTRY
{
    HKEY            Root;
    ml::String      SubKey;
    ml::String      ValueName;
    ULONG_PTR       DataType;
    PVOID           Data;
    ULONG_PTR       DataSize;
    ml::String      FullPath;

    REGISTRY_ENTRY()
    {
        Data = nullptr;
    }

    ~REGISTRY_ENTRY()
    {
        FreeMemoryP(this->Data);
        this->Data = nullptr;
    }

private:
    REGISTRY_ENTRY(const REGISTRY_ENTRY&);

} REGISTRY_ENTRY, *PREGISTRY_ENTRY;

#pragma warning(pop)

typedef struct
{
    REGISTRY_ENTRY Original;
    REGISTRY_ENTRY Redirected;

} REGISTRY_REDIRECTION_ENTRY, *PREGISTRY_REDIRECTION_ENTRY;

typedef struct
{
    ULONG_PTR   OriginalCharset;
    ULONG_PTR   OriginalLocaleID;
    CHAR        ScriptNameA[LF_FACESIZE];
    WCHAR       ScriptNameW[LF_FACESIZE];
    PVOID       LdrLoadDllAddress;
} LEP_RUNTIME_STATE, *PLEP_RUNTIME_STATE;

inline BOOL LepPayloadRangeValid(ULONG Offset, ULONG Size, ULONG TotalSize)
{
    return Offset <= TotalSize && Size <= TotalSize - Offset;
}

inline BOOL LepValidateBootstrapPayload(PLEP_BOOTSTRAP_PAYLOAD Payload)
{
    if (Payload == nullptr ||
        Payload->Magic != LEP_BOOTSTRAP_PAYLOAD_MAGIC ||
        Payload->Version != LEP_BOOTSTRAP_PAYLOAD_VERSION ||
        Payload->HeaderSize != sizeof(*Payload) ||
        Payload->TotalSize < Payload->HeaderSize ||
        Payload->TotalSize > LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE ||
        Payload->EnvironmentOffset < Payload->HeaderSize ||
        Payload->LepDllFullPathOffset < Payload->HeaderSize ||
        Payload->LepDllDirPathOffset < Payload->HeaderSize ||
        Payload->LepDllFullPathLength > Payload->TotalSize - sizeof(WCHAR) ||
        Payload->LepDllDirPathLength > Payload->TotalSize - sizeof(WCHAR) ||
        !LepPayloadRangeValid(Payload->EnvironmentOffset, Payload->EnvironmentSize, Payload->TotalSize) ||
        Payload->EnvironmentSize < FIELD_OFFSET(LEPB, RegistryReplacement) ||
        !LepPayloadRangeValid(Payload->LepDllFullPathOffset, Payload->LepDllFullPathLength + sizeof(WCHAR), Payload->TotalSize) ||
        !LepPayloadRangeValid(Payload->LepDllDirPathOffset, Payload->LepDllDirPathLength + sizeof(WCHAR), Payload->TotalSize) ||
        (Payload->LepDllFullPathLength & (sizeof(WCHAR) - 1)) != 0 ||
        (Payload->LepDllDirPathLength & (sizeof(WCHAR) - 1)) != 0)
    {
        return FALSE;
    }

    PLEPB Environment = (PLEPB)PtrAdd(Payload, Payload->EnvironmentOffset);
    ULONG64 Count = Environment->NumberOfRegistryRedirectionEntries;
    ULONG64 Required = FIELD_OFFSET(LEPB, RegistryReplacement) +
                       Count * sizeof(REGISTRY_REDIRECTION_ENTRY64);
    if (Count > 0x10000 || Required > Payload->EnvironmentSize)
        return FALSE;

    auto EnvironmentRangeValid = [Payload] (ULONG64 Offset, ULONG64 Size) -> BOOL
    {
        if (Offset == 0 && Size == 0)
            return TRUE;
        return Offset <= Payload->EnvironmentSize &&
               Size <= Payload->EnvironmentSize - Offset;
    };
    auto EnvironmentStringValid = [Environment, &EnvironmentRangeValid] (UNICODE_STRING64& String) -> BOOL
    {
        ULONG64 Offset = (ULONG64)String.Buffer;
        if (String.Length > String.MaximumLength || (String.Length & 1) != 0)
            return FALSE;
        if (Offset == 0 && String.MaximumLength == 0)
            return TRUE;
        if (!EnvironmentRangeValid(Offset, (ULONG64)String.MaximumLength + sizeof(WCHAR)))
            return FALSE;
        return *(PWCHAR)PtrAdd(Environment, Offset + String.MaximumLength) == 0;
    };
    PREGISTRY_REDIRECTION_ENTRY64 Entry = Environment->RegistryReplacement;
    for (ULONG64 Index = 0; Index != Count; ++Index, ++Entry)
    {
        if (!EnvironmentStringValid(Entry->Original.SubKey) ||
            !EnvironmentStringValid(Entry->Original.ValueName) ||
            !EnvironmentRangeValid((ULONG64)Entry->Original.Data, Entry->Original.DataSize) ||
            !EnvironmentStringValid(Entry->Redirected.SubKey) ||
            !EnvironmentStringValid(Entry->Redirected.ValueName) ||
            !EnvironmentRangeValid((ULONG64)Entry->Redirected.Data, Entry->Redirected.DataSize))
        {
            return FALSE;
        }
    }

    PCWSTR FullPath = (PCWSTR)PtrAdd(Payload, Payload->LepDllFullPathOffset);
    PCWSTR DirPath = (PCWSTR)PtrAdd(Payload, Payload->LepDllDirPathOffset);
    return FullPath[Payload->LepDllFullPathLength / sizeof(WCHAR)] == 0 &&
           DirPath[Payload->LepDllDirPathLength / sizeof(WCHAR)] == 0;
}

inline PLEPB LepBootstrapEnvironment(PLEP_BOOTSTRAP_PAYLOAD Payload)
{
    return LepValidateBootstrapPayload(Payload)
        ? (PLEPB)PtrAdd(Payload, Payload->EnvironmentOffset)
        : nullptr;
}

inline PCWSTR LepBootstrapDllFullPath(PLEP_BOOTSTRAP_PAYLOAD Payload)
{
    return LepValidateBootstrapPayload(Payload)
        ? (PCWSTR)PtrAdd(Payload, Payload->LepDllFullPathOffset)
        : nullptr;
}

inline PCWSTR LepBootstrapDllDirPath(PLEP_BOOTSTRAP_PAYLOAD Payload)
{
    return LepValidateBootstrapPayload(Payload)
        ? (PCWSTR)PtrAdd(Payload, Payload->LepDllDirPathOffset)
        : nullptr;
}


inline ULONG_PTR LepStringLengthW(PCWSTR String)
{
    PCWSTR Current = String;

    while (*Current != 0)
        ++Current;

    return Current - String;
}

inline ULONG LepBootstrapPayloadSize(ULONG EnvironmentSize, PCWSTR FullPath, PCWSTR DirPath)
{
    ULONG64 Size = ROUND_UP(sizeof(LEP_BOOTSTRAP_PAYLOAD), 8) + EnvironmentSize;
    Size = ROUND_UP(Size, sizeof(WCHAR));
    Size += (LepStringLengthW(FullPath) + 1) * sizeof(WCHAR);
    Size += (LepStringLengthW(DirPath) + 1) * sizeof(WCHAR);
    return Size <= LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE ? (ULONG)Size : 0;
}

inline NTSTATUS LepBuildBootstrapPayload(
    PLEP_BOOTSTRAP_PAYLOAD Payload,
    ULONG Capacity,
    PLEPB Environment,
    ULONG EnvironmentSize,
    PCWSTR FullPath,
    PCWSTR DirPath
)
{
    if (Payload == nullptr || Environment == nullptr || FullPath == nullptr || DirPath == nullptr)
        return STATUS_INVALID_PARAMETER;

    ULONG TotalSize = LepBootstrapPayloadSize(EnvironmentSize, FullPath, DirPath);
    if (TotalSize == 0 || Capacity < TotalSize)
        return STATUS_BUFFER_TOO_SMALL;

    ZeroMemory(Payload, TotalSize);
    Payload->Magic = LEP_BOOTSTRAP_PAYLOAD_MAGIC;
    Payload->Version = LEP_BOOTSTRAP_PAYLOAD_VERSION;
    Payload->HeaderSize = sizeof(*Payload);
    Payload->TotalSize = TotalSize;

    ULONG Offset = ROUND_UP(sizeof(*Payload), 8);
    Payload->EnvironmentOffset = Offset;
    Payload->EnvironmentSize = EnvironmentSize;
    CopyMemory(PtrAdd(Payload, Offset), Environment, EnvironmentSize);
    Offset = ROUND_UP(Offset + EnvironmentSize, sizeof(WCHAR));

    Payload->LepDllFullPathOffset = Offset;
    Payload->LepDllFullPathLength = (ULONG)(LepStringLengthW(FullPath) * sizeof(WCHAR));
    CopyMemory(PtrAdd(Payload, Offset), FullPath, Payload->LepDllFullPathLength + sizeof(WCHAR));
    Offset += Payload->LepDllFullPathLength + sizeof(WCHAR);

    Payload->LepDllDirPathOffset = Offset;
    Payload->LepDllDirPathLength = (ULONG)(LepStringLengthW(DirPath) * sizeof(WCHAR));
    CopyMemory(PtrAdd(Payload, Offset), DirPath, Payload->LepDllDirPathLength + sizeof(WCHAR));

    return LepValidateBootstrapPayload(Payload) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

inline ULONG_PTR FormatLepUIntDecimal(PWSTR Buffer, ULONG_PTR Value)
{
    WCHAR Digits[20];
    ULONG_PTR Length;

    Length = 0;
    do
    {
        Digits[Length++] = (WCHAR)(L'0' + Value % 10);
        Value /= 10;
    } while (Value != 0);

    for (ULONG_PTR i = 0; i != Length; ++i)
        Buffer[i] = Digits[Length - i - 1];

    Buffer[Length] = 0;
    return Length;
}

inline ULONG_PTR FormatLepUIntHex(PWSTR Buffer, ULONG_PTR Value)
{
    static const WCHAR Hex[] = L"0123456789abcdef";
    ULONG_PTR Length;
    BOOL LeadingZero;

    Length = 0;
    LeadingZero = TRUE;

    for (LONG_PTR Shift = bitsof(Value) - 4; Shift >= 0; Shift -= 4)
    {
        ULONG_PTR Digit = (Value >> Shift) & 0xF;

        if (Digit == 0 && LeadingZero && Shift != 0)
            continue;

        LeadingZero = FALSE;
        Buffer[Length++] = Hex[Digit];
    }

    Buffer[Length] = 0;
    return Length;
}

inline ULONG_PTR FormatLepUIntHex4(PWSTR Buffer, ULONG_PTR Value)
{
    static const WCHAR Hex[] = L"0123456789abcdef";

    for (ULONG_PTR i = 0; i != 4; ++i)
        Buffer[i] = Hex[(Value >> ((3 - i) * 4)) & 0xF];

    Buffer[4] = 0;
    return 4;
}

// Diagnostic build: keep initialization (including NLS/PEB changes) and
// NtCreateUserProcess propagation, but install no other runtime hooks.
// Build both architectures with the same value; this is not a payload flag.
#ifndef LEP_DIAG_PROCESS_ONLY
#define LEP_DIAG_PROCESS_ONLY 0
#endif

// Second isolation pass: prepare tables/config as before, but never apply
// the emulated NLS tables/globals/PEB pointers to the current process.
#ifndef LEP_DIAG_SKIP_NLS_APPLY
#define LEP_DIAG_SKIP_NLS_APPLY 0
#endif

#if LEP_DIAG_SKIP_NLS_APPLY && !LEP_DIAG_PROCESS_ONLY
#error LEP_DIAG_SKIP_NLS_APPLY requires LEP_DIAG_PROCESS_ONLY
#endif

// Third isolation pass: stop propagation at the chat browser -> child edge.
// Keep the same process creation/suspension path as the second pass.
#ifndef LEP_DIAG_SKIP_CHAT_CHILD_INJECTION
#define LEP_DIAG_SKIP_CHAT_CHILD_INJECTION 0
#endif
#if LEP_DIAG_SKIP_CHAT_CHILD_INJECTION && (!LEP_DIAG_PROCESS_ONLY || !LEP_DIAG_SKIP_NLS_APPLY)
#error LEP_DIAG_SKIP_CHAT_CHILD_INJECTION requires the no-NLS-apply diagnostic mode
#endif

#ifndef LEP_DIAG_CHAT_CHILD_RESTORE_ONLY
#define LEP_DIAG_CHAT_CHILD_RESTORE_ONLY 0
#endif
#if LEP_DIAG_CHAT_CHILD_RESTORE_ONLY && (!LEP_DIAG_PROCESS_ONLY || !LEP_DIAG_SKIP_NLS_APPLY || LEP_DIAG_SKIP_CHAT_CHILD_INJECTION)
#error Chat restore-only requires no-NLS-apply mode and child injection enabled
#endif

// Write the chat child's shadow image and metadata payload without executing it.
#ifndef LEP_DIAG_CHAT_CHILD_NO_LDR_PATCH
#define LEP_DIAG_CHAT_CHILD_NO_LDR_PATCH 0
#endif
#if LEP_DIAG_CHAT_CHILD_NO_LDR_PATCH && (!LEP_DIAG_PROCESS_ONLY || !LEP_DIAG_SKIP_NLS_APPLY || LEP_DIAG_SKIP_CHAT_CHILD_INJECTION || LEP_DIAG_CHAT_CHILD_RESTORE_ONLY)
#error Chat no-Ldr-patch requires no-NLS-apply mode and no other chat injection diagnostic
#endif

#ifndef ENABLE_LOG
#define ENABLE_LOG 1
#endif

#if ENABLE_LOG

inline VOID InitLog(NtFileDisk &LogFile, PLEP_BOOTSTRAP_PAYLOAD BootstrapPayload = nullptr)
{
    WCHAR LogFilePath[MAX_NTPATH];
    WCHAR NtLogFilePath[MAX_NTPATH + 4];
    UNICODE_STRING SelfPath;
    UNICODE_STRING NtLogFileName;
    PLDR_MODULE Self, Target;
    NTSTATUS Status;
    ULONG_PTR Offset;
    ULONG_PTR Length;
    ULONG_PTR ProcessId;

    Target = FindLdrModuleByHandle(nullptr);
    Self = FindLdrModuleByHandle(&__ImageBase);

    if (!Target)
    {
        LogFile = 0;
        return;
    }

    if (Self)
    {
        SelfPath = Self->FullDllName;
        SelfPath.Length -= Self->BaseDllName.Length;
    }
    else
    {
        PCWSTR Directory = LepBootstrapDllDirPath(BootstrapPayload);
        if (Directory == nullptr)
        {
            LogFile = 0;
            return;
        }
        RtlInitUnicodeString(&SelfPath, Directory);
    }

    Offset = 0;
    Length = ML_MIN(SelfPath.Length, sizeof(LogFilePath) - sizeof(WCHAR));
    CopyMemory(LogFilePath, SelfPath.Buffer, Length);
    Offset += Length / sizeof(WCHAR);

    if (Offset != 0 && LogFilePath[Offset - 1] != L'\\')
        LogFilePath[Offset++] = L'\\';

    Length = ML_MIN(Target->BaseDllName.Length, sizeof(LogFilePath) - (Offset + 1) * sizeof(WCHAR));
    CopyMemory(&LogFilePath[Offset], Target->BaseDllName.Buffer, Length);
    Offset += Length / sizeof(WCHAR);

    LogFilePath[Offset++] = L'.';
    ProcessId = CurrentPid();

    for (LONG_PTR Shift = bitsof(ProcessId) - 4; Shift >= 0; Shift -= 4)
    {
        ULONG_PTR Digit = (ProcessId >> Shift) & 0xF;

        if (Digit == 0 && LogFilePath[Offset - 1] == L'.' && Shift != 0)
            continue;

        LogFilePath[Offset++] = (WCHAR)(Digit < 10 ? L'0' + Digit : L'A' + Digit - 10);
    }

    static const WCHAR LogSuffix[] = L".log.txt";
    Length = ML_MIN(sizeof(LogSuffix), sizeof(LogFilePath) - Offset * sizeof(WCHAR));
    CopyMemory(&LogFilePath[Offset], LogSuffix, Length);

    static const WCHAR DosDevicesPrefix[] = L"\\??\\";
    CopyMemory(NtLogFilePath, DosDevicesPrefix, sizeof(DosDevicesPrefix) - sizeof(WCHAR));
    RtlInitUnicodeString(&NtLogFileName, LogFilePath);
    Length = ML_MIN(NtLogFileName.Length + sizeof(WCHAR), sizeof(NtLogFilePath) - sizeof(DosDevicesPrefix) + sizeof(WCHAR));
    CopyMemory(&NtLogFilePath[countof(DosDevicesPrefix) - 1], LogFilePath, Length);
    RtlInitUnicodeString(&NtLogFileName, NtLogFilePath);

    ULONG BOM = BOM_UTF16_LE;
    Status = LogFile.Create(
        NtLogFileName.Buffer,
        NFD_NOT_RESOLVE_PATH,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        GENERIC_WRITE,
        FILE_OVERWRITE_IF,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SYNCHRONOUS_IO_NONALERT
    );
    if (NT_FAILED(Status))
    {
        LogFile = 0;
        return;
    }

    Status = LogFile.Write(&BOM, 2);
    if (NT_FAILED(Status))
    {
        LogFile = 0;
        return;
    }

    PROCESS_IMAGE_FILE_NAME2 proc;
    //NtQueryInformationProcess(CurrentProcess, ProcessImageFileName, &proc, sizeof(proc), NULL);
    proc.ImageFileName = Target->FullDllName;
    LogFile.Write(proc.ImageFileName.Buffer, proc.ImageFileName.Length);
    LogFile.Write((PVOID)L"\r\n", 4);
}

#define WriteLog(...) { if (LepGetGlobalData() != nullptr && LepGetGlobalData()->LogFile) LepGetGlobalData()->LogFile.Print(NULL, __VA_ARGS__), LepGetGlobalData()->LogFile.Print(NULL, L"\r\n"); }

#else

inline VOID InitLog(...) {}

#define WriteLog(...)

#endif // ENABLE_LOG

class LepGlobalData
{
protected:
    BOOLEAN Wow64 : 1;
    BOOLEAN HasWin32U : 1; //windows 10.0.14295 or higher

    LEP_RUNTIME_STATE RuntimeState;
    PLEP_BOOTSTRAP_PAYLOAD BootstrapPayload;
    BOOLEAN OwnBootstrapPayload : 1;

    ml::GrowableArray<REGISTRY_REDIRECTION_ENTRY> RegistryRedirectionEntry;
    ml::HashTableT<TEXT_METRIC_INTERNAL> TextMetricCache;

public:
    PVOID CodePageMapView;
    ULONG_PTR AnsiCodePageOffset, OemCodePageOffset, UnicodeCaseTableOffset;

protected:
    PVOID DllNotificationCookie;

    UNICODE_STRING SystemDirectory;

#if ENABLE_LOG

public:
    NtFileDisk LogFile;

#endif // log

public:

    struct
    {
        API_POINTER(RtlKnownExceptionFilter)    StubRtlKnownExceptionFilter;
        API_POINTER(NtContinue)                 StubLdrInitNtContinue;
        API_POINTER(LdrResSearchResource)       StubLdrResSearchResource;
        API_POINTER(RtlCustomCPToUnicodeN)      StubRtlCustomCPToUnicodeN;
        PLEP_GET_DEFAULT_UI_LANGUAGE     StubGetSystemDefaultUILanguage;
        PLEP_GET_DEFAULT_UI_LANGUAGE     StubGetUserDefaultUILanguage;

        API_POINTER(NtUserMessageCall)          StubNtUserMessageCall;
        API_POINTER(NtUserDefSetText)           StubNtUserDefSetText;
        API_POINTER(SetWindowLongA)             StubSetWindowLongA;
        API_POINTER(GetWindowLongA)             StubGetWindowLongA;
#if ML_AMD64
        API_POINTER(SetWindowLongPtrA)          StubSetWindowLongPtrA;
        API_POINTER(GetWindowLongPtrA)          StubGetWindowLongPtrA;
#endif
        API_POINTER(IsWindowUnicode)            StubIsWindowUnicode;
        API_POINTER(GetClipboardData)           StubGetClipboardData;
        API_POINTER(SetClipboardData)           StubSetClipboardData;
        API_POINTER(SystemParametersInfoA)      StubSystemParametersInfoA;
        API_POINTER(SystemParametersInfoW)      StubSystemParametersInfoW;
        API_POINTER(GetDC)                      StubGetDC;
        API_POINTER(GetDCEx)                    StubGetDCEx;
        API_POINTER(GetWindowDC)                StubGetWindowDC;
        API_POINTER(BeginPaint)                 StubBeginPaint;

        union
        {
            PVOID                                       StubNtUserCreateWindowEx;
            API_POINTER(::NtUserCreateWindowEx_Win7)    StubNtUserCreateWindowEx_Win7;
            API_POINTER(::NtUserCreateWindowEx_Win8)    StubNtUserCreateWindowEx_Win8;
        };

        API_POINTER(GetStockObject)             StubGetStockObject;
        API_POINTER(DeleteObject)               StubDeleteObject;
        API_POINTER(CreateFontIndirectExW)      StubCreateFontIndirectExW;
        API_POINTER(NtGdiHfontCreate)           StubNtGdiHfontCreate;
        PLEP_QUERY_FONT_ASSOC_STATUS            StubQueryFontAssocStatus;
        API_POINTER(CreateCompatibleDC)         StubCreateCompatibleDC;
        API_POINTER(EnumFontsA)                 StubEnumFontsA;
        API_POINTER(EnumFontsW)                 StubEnumFontsW;
        API_POINTER(EnumFontFamiliesA)          StubEnumFontFamiliesA;
        API_POINTER(EnumFontFamiliesW)          StubEnumFontFamiliesW;
        API_POINTER(EnumFontFamiliesExA)        StubEnumFontFamiliesExA;
        API_POINTER(EnumFontFamiliesExW)        StubEnumFontFamiliesExW;
    } HookStub;

    ATOM AtomAnsiProc; //, AtomUnicodeProc;

    struct HookRoutineData
    {
        ~HookRoutineData()
        {
            RtlFreeUnicodeString(&Ntdll.CodePageKey);
            RtlFreeUnicodeString(&Ntdll.LanguageKey);
#if !LEP_DIAG_PROCESS_ONLY
            RtlDeleteCriticalSection(&Gdi32.GdiLock);
            RtlDeleteCriticalSection(&Ntdll.NtLock);
#endif
        }

        struct
        {
            UNICODE_STRING CodePageKey;
            UNICODE_STRING LanguageKey;

            RTL_CRITICAL_SECTION NtLock;

        } Ntdll;

        struct
        {
        } User32;

        struct
        {
            // maybe set up a flag for each kind of object?
            //BOOLEAN StockObjectInitialized : 1;

            RTL_CRITICAL_SECTION GdiLock;

            HGDIOBJ StockObject[STOCK_LAST + 1];

        } Gdi32;

    } HookRoutineData;

public:
    LepGlobalData()
    {
        ZeroMemory(this, sizeof(*this));

        new (&this->TextMetricCache) TYPE_OF(this->TextMetricCache);
    }

    ~LepGlobalData()
    {
        UnInitialize();
    }

    PLEP_RUNTIME_STATE GetRuntimeState()
    {
        return &RuntimeState;
    }

    PLEPB GetLepb()
    {
        return BootstrapPayload == nullptr
            ? nullptr
            : (PLEPB)PtrAdd(BootstrapPayload, BootstrapPayload->EnvironmentOffset);
    }

    PCWSTR GetLepDllFullPath()
    {
        return BootstrapPayload == nullptr
            ? nullptr
            : (PCWSTR)PtrAdd(BootstrapPayload, BootstrapPayload->LepDllFullPathOffset);
    }

    PCWSTR GetLepDllDirPath()
    {
        return BootstrapPayload == nullptr
            ? nullptr
            : (PCWSTR)PtrAdd(BootstrapPayload, BootstrapPayload->LepDllDirPathOffset);
    }

    VOID InitFontCharsetInfo()
    {
        HDC DC;
        LOGFONTW lf;

        DC = HookStub.StubGetDC == nullptr ? ::GetDC(nullptr) : this->GetDC(nullptr);
        GetRuntimeState()->OriginalCharset = GetTextCharset(DC);

        lf.lfCharSet = GetLepb()->DefaultCharset;
        lf.lfFaceName[0] = 0;

        auto EnumFontCallback = [] (CONST LOGFONTW *lf, CONST TEXTMETRICW *, DWORD, LPARAM Param)
            {
                LepGlobalData *GlobalData = (LepGlobalData *)Param;
                LPENUMLOGFONTEXW elf = (LPENUMLOGFONTEXW)lf;

                CopyStruct(GlobalData->GetRuntimeState()->ScriptNameW, elf->elfScript, sizeof(elf->elfScript));
                UnicodeToAnsi(GlobalData->GetRuntimeState()->ScriptNameA, countof(GlobalData->GetRuntimeState()->ScriptNameA), GlobalData->GetRuntimeState()->ScriptNameW);

                return FALSE;
            };

        if (HookStub.StubEnumFontFamiliesExW == nullptr)
        {
            ::EnumFontFamiliesExW(DC, &lf, EnumFontCallback, (LPARAM)this, 0);
        }
        else
        {
            EnumFontFamiliesExW(DC, &lf, EnumFontCallback, (LPARAM)this, 0);
        }

        ReleaseDC(nullptr, DC);
    }

    NTSTATUS Initialize(PLEP_BOOTSTRAP_PAYLOAD Payload, BOOL OwnPayload);
    NTSTATUS UnInitialize();
    NTSTATUS InitRegistryRedirection(PREGISTRY_REDIRECTION_ENTRY64 Entry64, ULONG_PTR Count, PVOID BaseAddress, ULONG_PTR BaseSize = 0);
    NTSTATUS InitDefaultRegistryRedirection();
    NTSTATUS BuildBootstrapPayload(PCWSTR FullPath, PLEP_BOOTSTRAP_PAYLOAD* Payload, PULONG PayloadSize);

    VOID DllNotification(ULONG NotificationReason, PCLDR_DLL_NOTIFICATION_DATA NotificationData);
    VOID HookModule(PVOID DllBase, PCUNICODE_STRING DllName, BOOL DllLoad);

    NTSTATUS HookUser32Routines(PVOID User32);
    NTSTATUS UnHookUser32Routines();

    NTSTATUS HookGdi32Routines(PVOID Gdi32);
    NTSTATUS UnHookGdi32Routines();

    NTSTATUS HookNtdllRoutines(PVOID Ntdll);
    NTSTATUS UnHookNtdllRoutines();

    NTSTATUS HookKernel32Routines(PVOID Kernel32);
    NTSTATUS UnHookKernel32Routines();

    NTSTATUS
    LookupRegistryRedirectionEntry(
        HANDLE                          KeyHandle,
        PUNICODE_STRING                 ValueName,
        PREGISTRY_REDIRECTION_ENTRY*    RedirectionEntry
    );

    NTSTATUS HackUserDefaultLCID(PVOID Kernel32);
    NTSTATUS HackUserDefaultLCID2(PVOID Kernel32);
    NTSTATUS HackAnsiOemCodeHashNodes();
    NTSTATUS InjectSelfToChildProcess(HANDLE Process, PCLIENT_ID Cid, ULONG InjectionFlags = LEP_INJECT_FULL);
    NTSTATUS InjectCrossArchitecture(BOOL CurrentWow64, PCLIENT_ID Cid, BOOL TargetWow64, ULONG InjectionFlags);

    /************************************************************************
      helper func
    ************************************************************************/

    /************************************************************************
      ntdll
    ************************************************************************/

    LONG RtlKnownExceptionFilter(PEXCEPTION_POINTERS ExceptionPointers)
    {
        return HookStub.StubRtlKnownExceptionFilter == nullptr ?
            ::RtlKnownExceptionFilter(ExceptionPointers) :
            HookStub.StubRtlKnownExceptionFilter(ExceptionPointers);
    }

    /************************************************************************
      kernelbase
    ************************************************************************/

    /************************************************************************
      user32
    ************************************************************************/

    PVOID GetWindowDataA(HWND Window)
    {
        return GetPropW(Window, (PCWSTR)AtomAnsiProc);
    }

    BOOL SetWindowDataA(HWND Window, PVOID Data)
    {
        return SetPropW(Window, (PCWSTR)AtomAnsiProc, Data);
    }
/*
    PVOID GetWindowDataW(HWND Window)
    {
        return GetPropW(Window, (PCWSTR)AtomUnicodeProc);
    }

    BOOL SetWindowDataW(HWND Window, PVOID Data)
    {
        return SetPropW(Window, (PCWSTR)AtomUnicodeProc, Data);
    }
*/
    LONG_PTR GetWindowLongA(HWND hWnd, int Index)
    {
        return HookStub.StubGetWindowLongA(hWnd, Index);
    }

    LONG_PTR SetWindowLongA(HWND hWnd, int Index, LONG_PTR NewLong)
    {
        return HookStub.StubSetWindowLongA(hWnd, Index, NewLong);
    }

#if ML_AMD64
    LONG_PTR GetWindowLongPtrA(HWND hWnd, int Index)
    {
        return HookStub.StubGetWindowLongPtrA(hWnd, Index);
    }

    LONG_PTR SetWindowLongPtrA(HWND hWnd, int Index, LONG_PTR NewLong)
    {
        return HookStub.StubSetWindowLongPtrA(hWnd, Index, NewLong);
    }
#endif

    BOOL IsWindowUnicode(HWND hWnd)
    {
        return HookStub.StubIsWindowUnicode(hWnd);
    }

    HANDLE SetClipboardData(UINT Format, HANDLE Memory)
    {
        return HookStub.StubSetClipboardData(Format, Memory);
    }

    HANDLE GetClipboardData(UINT Format)
    {
        return HookStub.StubGetClipboardData(Format);
    }

    BOOL SystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
    {
        return HookStub.StubSystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);
    }

    BOOL SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
    {
        return HookStub.StubSystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni);
    }

    HDC GetDC(HWND hWnd)
    {
        return HookStub.StubGetDC(hWnd);
    }

    HDC GetDCEx(HWND hWnd, HRGN hrgnClip, DWORD flags)
    {
        return HookStub.StubGetDCEx(hWnd, hrgnClip, flags);
    }

    HDC GetWindowDC(HWND hWnd)
    {
        return HookStub.StubGetWindowDC(hWnd);
    }

    HDC BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
    {
        return HookStub.StubBeginPaint(hWnd, lpPaint);
    }

#if ML_AMD64
    PVOID GetNtUserSystemCallOriginal(ULONG RoutineHash);
#endif

    LRESULT NtUserMessageCall(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam, ULONG_PTR xParam, DWORD xpfnProc, ULONG Flags)
    {
#if ML_AMD64
        typedef LRESULT (NTAPI *PFN)(HWND, UINT, WPARAM, LPARAM, ULONG_PTR, DWORD, ULONG);
        PVOID Original = GetNtUserSystemCallOriginal(WIN32K_NtUserMessageCall);
        return Original == nullptr ? 0 : ((PFN)Original)(hWnd, Message, wParam, lParam, xParam, xpfnProc, Flags);
#else
        return HookStub.StubNtUserMessageCall(hWnd, Message, wParam, lParam, xParam, xpfnProc, Flags);
#endif
    }

    HWND NtUserCreateWindowEx_Win7(ULONG ExStyle, PLARGE_UNICODE_STRING ClassName, PLARGE_UNICODE_STRING ClassVersion, PLARGE_UNICODE_STRING WindowName, ULONG Style, LONG X, LONG Y, LONG Width, LONG Height, HWND ParentWnd, HMENU Menu, PVOID Instance, LPVOID Param, ULONG ShowMode, ULONG_PTR Unknown)
    {
#if ML_AMD64
        typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG_PTR);
        PVOID Original = GetNtUserSystemCallOriginal(WIN32K_NtUserCreateWindowEx);
        return Original == nullptr ? nullptr : ((PFN)Original)(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown);
#else
        return HookStub.StubNtUserCreateWindowEx_Win7(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown);
#endif
    }

    HWND NtUserCreateWindowEx_Win8(ULONG ExStyle, PLARGE_UNICODE_STRING ClassName, PLARGE_UNICODE_STRING ClassVersion, PLARGE_UNICODE_STRING WindowName, ULONG Style, LONG X, LONG Y, LONG Width, LONG Height, HWND ParentWnd, HMENU Menu, PVOID Instance, LPVOID Param, ULONG ShowMode, ULONG Unknown, ULONG_PTR Unknown2)
    {
#if ML_AMD64
        typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG, ULONG_PTR);
        PVOID Original = GetNtUserSystemCallOriginal(WIN32K_NtUserCreateWindowEx);
        return Original == nullptr ? nullptr : ((PFN)Original)(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown, Unknown2);
#else
        return HookStub.StubNtUserCreateWindowEx_Win8(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown, Unknown2);
#endif
    }

    BOOL NtUserSetDefText(HWND hWnd, PLARGE_UNICODE_STRING Text)
    {
#if ML_AMD64
        typedef BOOL (NTAPI *PFN)(HWND, PLARGE_UNICODE_STRING);
        PVOID Original = GetNtUserSystemCallOriginal(WIN32K_NtUserDefSetText);
        return Original == nullptr ? FALSE : ((PFN)Original)(hWnd, Text);
#else
        return HookStub.StubNtUserDefSetText(hWnd, Text);
#endif
    }

    /************************************************************************
      gdi32
    ************************************************************************/

    INT FmsEnumFontFamiliesEx(HDC hDC, PLOGFONTW Logfont, FONTENUMPROCW Proc, LPARAM Parameter, ULONG Flags);

    NTSTATUS AdjustFontData(HDC DC, LPENUMLOGFONTEXW EnumLogFontEx, PTEXT_METRIC_INTERNAL TextMetric, ULONG_PTR FontType);
    NTSTATUS AdjustFontDataInternal(PADJUST_FONT_DATA AdjustData);
    NTSTATUS GetNameRecordFromNameTable(PVOID TableBuffer, ULONG_PTR TableSize, ULONG_PTR NameID, ULONG_PTR LanguageID, PUNICODE_STRING Name);

    VOID GetTextMetricsAFromLogFont(PTEXTMETRICA TextMetricA, CONST LOGFONTW *LogFont);
    VOID GetTextMetricsWFromLogFont(PTEXTMETRICW TextMetricW, CONST LOGFONTW *LogFont);

    PTEXT_METRIC_INTERNAL GetTextMetricFromCache(LPENUMLOGFONTEXW LogFont);
    VOID AddTextMetricToCache(LPENUMLOGFONTEXW LogFont, PTEXT_METRIC_INTERNAL TextMetric);

    HGDIOBJ GetStockObject(LONG Object)
    {
        return HookStub.StubGetStockObject(Object);
    }

    BOOL DeleteObject(HGDIOBJ GdiObject)
    {
        return HookStub.StubDeleteObject(GdiObject);
    }

    HDC CreateCompatibleDC(HDC hDC)
    {
        return HookStub.StubCreateCompatibleDC(hDC);
    }

    int EnumFontsA(HDC hdc, PCSTR lpFaceName, FONTENUMPROCA lpFontFunc, LPARAM lParam)
    {
        return HookStub.StubEnumFontsA(hdc, lpFaceName, lpFontFunc, lParam);
    }

    int EnumFontsW(HDC hdc, PCWSTR lpFaceName, FONTENUMPROCW lpFontFunc, LPARAM lParam)
    {
        return HookStub.StubEnumFontsW(hdc, lpFaceName, lpFontFunc, lParam);
    }

    int EnumFontFamiliesA(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
    {
        return HookStub.StubEnumFontFamiliesA(hdc, lpFaceName, lpProc, lParam);
    }

    int EnumFontFamiliesW(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
    {
        return HookStub.StubEnumFontFamiliesW(hdc, lpFaceName, lpProc, lParam);
    }

    int EnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
    {
        return HookStub.StubEnumFontFamiliesExA(hdc, lpLogfont, lpProc, lParam, dwFlags);
    }

    int EnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags)
    {
        return HookStub.StubEnumFontFamiliesExW(hdc, lpLogfont, lpProc, lParam, dwFlags);
    }

};

ForceInline PLepGlobalData LepGetGlobalData()
{
    extern PLepGlobalData g_GlobalData;
    return g_GlobalData;
}

VOID LepSyncNtdllNlsGlobals(USHORT AnsiCodePage, BOOLEAN AnsiDbcsCodePage, BOOLEAN OemDbcsCodePage);

// The custom Print formatter does not support %.*ws. Write counted UTF-16
// directly, without assuming a terminating NUL or allocating in the hook.
inline VOID LepLogUnicodeString(PCWSTR Label, const UNICODE_STRING& Value)
{
#if ENABLE_LOG
    PLepGlobalData Data = LepGetGlobalData();
    if (Data == nullptr || !Data->LogFile)
        return;
    WriteLog(L"%ws", Label);
    if (Value.Buffer != nullptr && Value.Length != 0)
        Data->LogFile.Write(Value.Buffer, Value.Length);
    Data->LogFile.Write((PVOID)L"\r\n", 2 * sizeof(WCHAR));
#endif
}

#endif // _LocaleEmulatorPlus_H_cd444a0d_c7f9_44b2_aac8_8107e9a07ca2_
