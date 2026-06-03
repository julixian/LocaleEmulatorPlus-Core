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
#define LEP_NTUSER_SYSCALL_CONTEXT   TAG4('UNSC')

#if ML_AMD64
struct LEP_NTUSER_SYSCALL_FRAME : public TEB_ACTIVE_FRAME
{
    PVOID NtUserCreateWindowEx;
    PVOID NtUserMessageCall;
    PVOID NtUserDefSetText;

    LEP_NTUSER_SYSCALL_FRAME()
    {
        this->Context = LEP_NTUSER_SYSCALL_CONTEXT;
        NtUserCreateWindowEx = nullptr;
        NtUserMessageCall = nullptr;
        NtUserDefSetText = nullptr;
    }

    static LEP_NTUSER_SYSCALL_FRAME* Current()
    {
        return (LEP_NTUSER_SYSCALL_FRAME*)FindThreadFrame(LEP_NTUSER_SYSCALL_CONTEXT);
    }
};
#endif

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

    HANDLE      Section;
    PVOID       LdrLoadDllAddress;
    ULONG_PTR   LdrLoadDllBackupSize;
    BYTE        LdrLoadDllBackup[16];
    WCHAR       LepDllFullPath[MAX_NTPATH];
    WCHAR       LepDllDirPath[MAX_NTPATH];
    LEPB         LEPB;

} LOCALE_EMULATOR_PLUS_PROCESS_ENVIRONMENT_BLOCK, *PLOCALE_EMULATOR_PLUS_PROCESS_ENVIRONMENT_BLOCK, LEPPEB, *PLEPPEB;

inline
ULONG_PTR
GetLepPebSectionName(
    PWSTR       Buffer,
    ULONG_PTR   ProcessId
)
{
    static const WCHAR Prefix[] = L"Local\\LOCALE_EMULATOR_PLUS_PROCESS_ENVIRONMENT_BLOCK_SECTION_";
    ULONG_PTR Length;
    ULONG_PTR Shift;
    BOOL Started;

    CopyMemory(Buffer, Prefix, sizeof(Prefix) - sizeof(WCHAR));
    Length = CONST_STRLEN(Prefix);

    Started = FALSE;
    for (Shift = sizeof(ProcessId) * 8; Shift != 0; )
    {
        ULONG_PTR Digit;

        Shift -= 4;
        Digit = (ProcessId >> Shift) & 0xF;
        if (Digit != 0 || Started || Shift == 0)
        {
            Started = TRUE;
            Buffer[Length++] = (WCHAR)(Digit < 10 ? L'0' + Digit : L'A' + Digit - 10);
        }
    }

    Buffer[Length] = 0;
    return Length;
}

inline NTSTATUS CloseLepPeb(PLEPPEB LEPPEB)
{
    return LEPPEB == nullptr ? STATUS_INVALID_PARAMETER : NtUnmapViewOfSection(CurrentProcess, LEPPEB);
}

inline VOID InitDefaultLeb(PLEPB LEPB)
{
    static WCHAR StandardName[] = L"@tzres.dll,-632";
    static WCHAR DaylightName[] = L"@tzres.dll,-631";

    ZeroMemory(LEPB, sizeof(*LEPB));

#if 1

    static WCHAR FaceName[]     = L"MS Gothic";

    LEPB->AnsiCodePage      = CP_SHIFTJIS;
    LEPB->OemCodePage       = CP_SHIFTJIS;
    LEPB->LocaleID          = 0x411;
    LEPB->DefaultCharset    = SHIFTJIS_CHARSET;
	LEPB->HookUILanguageApi = 0;

#else

    static WCHAR FaceName[]     = L"Microsoft YaHei";

    LEPB->AnsiCodePage      = CP_GB2312;
    LEPB->OemCodePage       = CP_GB2312;
    LEPB->LocaleID          = 0x804;
    LEPB->DefaultCharset    = GB2312_CHARSET;
	LEPB->HookUILanguageApi = 0;

#endif

    CopyStruct(LEPB->DefaultFaceName, FaceName, sizeof(FaceName));

    NtQuerySystemInformation(SystemCurrentTimeZoneInformation, &LEPB->Timezone, sizeof(LEPB->Timezone), nullptr);

    LEPB->Timezone.Bias = -540;
    CopyStruct(LEPB->Timezone.StandardName, StandardName, sizeof(StandardName));
    CopyStruct(LEPB->Timezone.DaylightName, DaylightName, sizeof(DaylightName));
}

inline ULONG_PTR LepStringLengthW(PCWSTR String)
{
    PCWSTR Current = String;

    while (*Current != 0)
        ++Current;

    return Current - String;
}

inline
NTSTATUS
LepOpenDirectoryObject(
    PHANDLE DirectoryHandle,
    PWSTR   DirectoryNameBuffer,
    HANDLE  RootHandle
)
{
    NTSTATUS                     Status;
    OBJECT_ATTRIBUTES            ObjectAttributes;
    UNICODE_STRING               DirectoryName;

    DirectoryName.Buffer          = DirectoryNameBuffer;
    DirectoryName.Length          = LepStringLengthW(DirectoryNameBuffer) * sizeof(WCHAR);
    DirectoryName.MaximumLength   = DirectoryName.Length;

    InitializeObjectAttributes(&ObjectAttributes, &DirectoryName, OBJ_CASE_INSENSITIVE, RootHandle, nullptr);

    return NtOpenDirectoryObject(DirectoryHandle, DIRECTORY_ALL_ACCESS, &ObjectAttributes);
}

inline ULONG_PTR FormatLepBaseNamedObjectsRoot(PWSTR Buffer, ULONG_PTR SessionId)
{
    static const WCHAR Prefix[] = L"\\Sessions\\";
    static const WCHAR Suffix[] = L"\\BaseNamedObjects";
    WCHAR Digits[20];
    ULONG_PTR Length;
    ULONG_PTR Count;

    CopyMemory(Buffer, Prefix, sizeof(Prefix) - sizeof(WCHAR));
    Length = CONST_STRLEN(Prefix);

    Count = 0;
    do
    {
        Digits[Count++] = (WCHAR)(L'0' + SessionId % 10);
        SessionId /= 10;
    } while (SessionId != 0);

    do
    {
        Buffer[Length++] = Digits[--Count];
    } while (Count != 0);

    CopyMemory(Buffer + Length, Suffix, sizeof(Suffix));
    Length += CONST_STRLEN(Suffix);

    return Length;
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

inline
BOOL
LepFindSectionObject(
    PHANDLE         SectionHandle,
    PUNICODE_STRING SectionName,
    ULONG           Depth,
    HANDLE          RootHandle
)
{
    NTSTATUS                     Status;
    OBJECT_ATTRIBUTES            ObjectAttributes;
    PDIRECTORY_BASIC_INFORMATION Buffer;
    ULONG                        BufferSize;
    ULONG                        Context;
    ULONG                        RetSize;
    HANDLE                       SubDirHandle;

    if (Depth == 0)
    {
        InitializeObjectAttributes(&ObjectAttributes, SectionName, 0, RootHandle, nullptr);
        Status = NtOpenSection(SectionHandle, SECTION_ALL_ACCESS, &ObjectAttributes);
        if (NT_FAILED(Status))
            return FALSE;
        return TRUE;
    }
    else
    {
        BufferSize = 0x400;
        // XXX: Use _alloca instead of AllocStack for memory continuity
        // XXX: Also do not use AllocateMemory
        Buffer = (PDIRECTORY_BASIC_INFORMATION) _alloca(BufferSize);
        do
        {
            Buffer = (PDIRECTORY_BASIC_INFORMATION) _alloca(BufferSize);
            BufferSize *= 2;
            Status = NtQueryDirectoryObject(RootHandle, (PVOID)Buffer, BufferSize, FALSE, TRUE, &Context, &RetSize);
        } while (Status == STATUS_MORE_ENTRIES || Status == STATUS_BUFFER_TOO_SMALL);
        if (!NT_SUCCESS(Status))
            return FALSE;
        while ((Buffer->ObjectName.Length != 0) && (Buffer->ObjectTypeName.Length != 0))
        {
            if (memcmp(Buffer->ObjectTypeName.Buffer, L"Directory", Buffer->ObjectTypeName.Length))
            {
                ++Buffer;
                continue;
            }
            InitializeObjectAttributes(&ObjectAttributes, &Buffer->ObjectName, OBJ_CASE_INSENSITIVE, RootHandle, nullptr);
            Status = NtOpenDirectoryObject(&SubDirHandle, DIRECTORY_ALL_ACCESS, &ObjectAttributes);
            if (NT_FAILED(Status))
            {
                ++Buffer;
                continue;
            }
            if(LepFindSectionObject(SectionHandle, SectionName, Depth-1, SubDirHandle)) {
                NtClose(SubDirHandle);
                return TRUE;
            }
            NtClose(SubDirHandle);
            ++Buffer;
        }
    }
    return FALSE;
}

inline
PLEPPEB
NTAPI
OpenOrCreateLepPeb(
    ULONG_PTR   ProcessId   = CurrentPid(),
    BOOL        Create      = FALSE,
    ULONG_PTR   ExtraSize   = 0
)
{
    NTSTATUS            Status;
    WCHAR               RootNameBuffer[0x80];
    WCHAR               SectionNameBuffer[0x80];
    OBJECT_ATTRIBUTES   ObjectAttributes;
    UNICODE_STRING      SectionName;
    HANDLE              SectionHandle, RootHandle;
    PLEPPEB              LEPPEB;
    ULONG_PTR           ViewSize, SessionId;
    LARGE_INTEGER       MaximumSize;
#if LEP_DIAG_INIT
    BOOL                DiagVerbose = !IsLepLoader() || Create;
#endif

    // ExceptionBox(L"OpenOrCreateLepPeb");

    LEP_DIAG_HEADER_IF(DiagVerbose, L"OpenOrCreateLepPeb entry");
    SessionId = GetSessionId(ProcessId);
    LEP_DIAG_HEADER_IF(DiagVerbose, SessionId == INVALID_SESSION_ID ? L"GetSessionId invalid" : L"GetSessionId ok");
    if (SessionId == INVALID_SESSION_ID)
        return nullptr;

    LEP_DIAG_HEADER_IF(DiagVerbose, L"before section root format");
    FormatLepBaseNamedObjectsRoot(RootNameBuffer, SessionId);
    LEP_DIAG_HEADER_IF(DiagVerbose, L"section root format ok");

    LEP_DIAG_HEADER_IF(DiagVerbose, L"before LepOpenDirectoryObject(BaseNamedObjects)");
    Status = LepOpenDirectoryObject(&RootHandle, RootNameBuffer, nullptr);
    LEP_DIAG_HEADER_IF(DiagVerbose, NT_FAILED(Status) ? L"LepOpenDirectoryObject(BaseNamedObjects) failed" : L"LepOpenDirectoryObject(BaseNamedObjects) ok");
    if (NT_FAILED(Status))
        return FALSE;

    LEP_DIAG_HEADER_IF(DiagVerbose, L"before GetLepPebSectionName");
    SectionName.Length          = (USHORT) GetLepPebSectionName(SectionNameBuffer, ProcessId) * sizeof(WCHAR);
    SectionName.MaximumLength   = sizeof(SectionNameBuffer);
    SectionName.Buffer          = SectionNameBuffer;
    LEP_DIAG_HEADER_IF(DiagVerbose, L"GetLepPebSectionName ok");

    InitializeObjectAttributes(&ObjectAttributes, &SectionName, 0, RootHandle, nullptr);

    LEP_DIAG_HEADER_IF(DiagVerbose, L"before NtOpenSection");
    Status = NtOpenSection(&SectionHandle, SECTION_ALL_ACCESS, &ObjectAttributes);
    LEP_DIAG_HEADER_IF(DiagVerbose, NT_FAILED(Status) ? L"NtOpenSection failed" : L"NtOpenSection ok");
    if (NT_FAILED(Status))
    {
        if (Create)
        {
            LEP_DIAG_HEADER_IF(DiagVerbose, L"before NtCreateSection");
            MaximumSize.QuadPart = sizeof(*LEPPEB) + ExtraSize;
            Status = NtCreateSection(
                        &SectionHandle,
                        SECTION_ALL_ACCESS,
                        &ObjectAttributes,
                        &MaximumSize,
                        PAGE_READWRITE,
                        SEC_COMMIT,
                        nullptr
                    );
            LEP_DIAG_HEADER_IF(DiagVerbose, NT_FAILED(Status) ? L"NtCreateSection failed" : L"NtCreateSection ok");
            if (NT_FAILED(Status))
            {
                NtClose(RootHandle);
                return nullptr;
            }
        }
        else
        {
            NtClose(RootHandle);
            LEP_DIAG_HEADER_IF(DiagVerbose, L"before LepOpenDirectoryObject(Sandbox)");
            Status = LepOpenDirectoryObject(&RootHandle, L"\\Sandbox", nullptr);
            LEP_DIAG_HEADER_IF(DiagVerbose, NT_FAILED(Status) ? L"LepOpenDirectoryObject(Sandbox) failed" : L"LepOpenDirectoryObject(Sandbox) ok");
            if (NT_FAILED(Status))
                return FALSE;
            LEP_DIAG_HEADER_IF(DiagVerbose, L"before LepFindSectionObject");
            if (!LepFindSectionObject(&SectionHandle, &SectionName, 6, RootHandle))
            {
                LEP_DIAG_HEADER_IF(DiagVerbose, L"LepFindSectionObject failed");
                NtClose(RootHandle);
                return nullptr;
            }
            LEP_DIAG_HEADER_IF(DiagVerbose, L"LepFindSectionObject ok");
        }
    }
    NtClose(RootHandle);

    ViewSize = 0;
    LEPPEB = nullptr;
    LEP_DIAG_HEADER_IF(DiagVerbose, L"before NtMapViewOfSection");
    Status = NtMapViewOfSection(
                SectionHandle,
                CurrentProcess,
                (PVOID *)&LEPPEB,
                0,
                sizeof(*LEPPEB),
                nullptr,
                &ViewSize,
                ViewShare,
                0,
                PAGE_READWRITE
            );
    LEP_DIAG_HEADER_IF(DiagVerbose, NT_FAILED(Status) ? L"NtMapViewOfSection failed" : L"NtMapViewOfSection ok");

    if (NT_FAILED(Status))
    {
        NtClose(SectionHandle);
        return nullptr;
    }

    if (Create) LOOP_ONCE
    {
        HANDLE ProcessHandle;

        ZeroMemory(LEPPEB, ViewSize);

        Status = PidToHandleEx(&ProcessHandle, ProcessId);
        FAIL_BREAK(Status);

        Status = NtDuplicateObject(CurrentProcess, SectionHandle, ProcessHandle, &LEPPEB->Section, 0, 0, DUPLICATE_SAME_ACCESS);
        NtClose(ProcessHandle);
        FAIL_BREAK(Status);
    }

    NtClose(SectionHandle);
    if (NT_FAILED(Status))
    {
        CloseLepPeb(LEPPEB);
        return nullptr;
    }

    LEP_DIAG_HEADER_IF(DiagVerbose, L"OpenOrCreateLepPeb return");
    return LEPPEB;
}

#ifndef ENABLE_LOG
#define ENABLE_LOG 0
#endif

#if ENABLE_LOG

inline VOID InitLog(NtFileDisk &LogFile)
{
    WCHAR LogFilePath[MAX_NTPATH];
    WCHAR NtLogFilePath[MAX_NTPATH + 4];
    UNICODE_STRING SelfPath;
    UNICODE_STRING NtLogFileName;
    PLDR_MODULE Self, Target;
    PLEPPEB LEPPEB = nullptr;
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
        LEPPEB = OpenOrCreateLepPeb();
        if (LEPPEB == nullptr)
        {
            LogFile = 0;
            return;
        }
        RtlInitUnicodeString(&SelfPath, LEPPEB->LepDllDirPath);
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

    if (LEPPEB != nullptr)
    {
        CloseLepPeb(LEPPEB);
    }

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

    LEPPEB LEPPEB;

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
            RtlDeleteCriticalSection(&Gdi32.GdiLock);
            RtlDeleteCriticalSection(&Ntdll.NtLock);
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

    PLEPPEB GetLepPeb()
    {
        return &LEPPEB;
    }

    PLEPB GetLepb()
    {
        return &LEPPEB.LEPB;
    }

    VOID InitFontCharsetInfo()
    {
        HDC DC;
        LOGFONTW lf;

        DC = HookStub.StubGetDC == nullptr ? ::GetDC(nullptr) : this->GetDC(nullptr);
        GetLepPeb()->OriginalCharset = GetTextCharset(DC);

        lf.lfCharSet = GetLepb()->DefaultCharset;
        lf.lfFaceName[0] = 0;

        auto EnumFontCallback = [] (CONST LOGFONTW *lf, CONST TEXTMETRICW *, DWORD, LPARAM Param)
            {
                LepGlobalData *GlobalData = (LepGlobalData *)Param;
                LPENUMLOGFONTEXW elf = (LPENUMLOGFONTEXW)lf;

                CopyStruct(GlobalData->GetLepPeb()->ScriptNameW, elf->elfScript, sizeof(elf->elfScript));
                UnicodeToAnsi(GlobalData->GetLepPeb()->ScriptNameA, countof(GlobalData->GetLepPeb()->ScriptNameA), GlobalData->GetLepPeb()->ScriptNameW);

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

    NTSTATUS Initialize();
    NTSTATUS UnInitialize();
    NTSTATUS InitRegistryRedirection(PREGISTRY_REDIRECTION_ENTRY64 Entry64, ULONG_PTR Count, PVOID BaseAddress);
    NTSTATUS InitDefaultRegistryRedirection();

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
    NTSTATUS InjectSelfToChildProcess(HANDLE Process, PCLIENT_ID Cid);

    /************************************************************************
      helper func
    ************************************************************************/

    /************************************************************************
      ntdll
    ************************************************************************/

    LONG RtlKnownExceptionFilter(PEXCEPTION_POINTERS ExceptionPointers)
    {
        return HookStub.StubRtlKnownExceptionFilter(ExceptionPointers);
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

    LRESULT NtUserMessageCall(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam, ULONG_PTR xParam, DWORD xpfnProc, ULONG Flags)
    {
#if ML_AMD64
        LEP_NTUSER_SYSCALL_FRAME* Frame = LEP_NTUSER_SYSCALL_FRAME::Current();
        if (Frame != nullptr && Frame->NtUserMessageCall != nullptr)
        {
            typedef LRESULT (NTAPI *PFN)(HWND, UINT, WPARAM, LPARAM, ULONG_PTR, DWORD, ULONG);
            return ((PFN)Frame->NtUserMessageCall)(hWnd, Message, wParam, lParam, xParam, xpfnProc, Flags);
        }
#endif
        return HookStub.StubNtUserMessageCall(hWnd, Message, wParam, lParam, xParam, xpfnProc, Flags);
    }

    HWND NtUserCreateWindowEx_Win7(ULONG ExStyle, PLARGE_UNICODE_STRING ClassName, PLARGE_UNICODE_STRING ClassVersion, PLARGE_UNICODE_STRING WindowName, ULONG Style, LONG X, LONG Y, LONG Width, LONG Height, HWND ParentWnd, HMENU Menu, PVOID Instance, LPVOID Param, ULONG ShowMode, ULONG_PTR Unknown)
    {
#if ML_AMD64
        LEP_NTUSER_SYSCALL_FRAME* Frame = LEP_NTUSER_SYSCALL_FRAME::Current();
        if (Frame != nullptr && Frame->NtUserCreateWindowEx != nullptr)
        {
            typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG_PTR);
            return ((PFN)Frame->NtUserCreateWindowEx)(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown);
        }
#endif
        return HookStub.StubNtUserCreateWindowEx_Win7(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown);
    }

    HWND NtUserCreateWindowEx_Win8(ULONG ExStyle, PLARGE_UNICODE_STRING ClassName, PLARGE_UNICODE_STRING ClassVersion, PLARGE_UNICODE_STRING WindowName, ULONG Style, LONG X, LONG Y, LONG Width, LONG Height, HWND ParentWnd, HMENU Menu, PVOID Instance, LPVOID Param, ULONG ShowMode, ULONG Unknown, ULONG_PTR Unknown2)
    {
#if ML_AMD64
        LEP_NTUSER_SYSCALL_FRAME* Frame = LEP_NTUSER_SYSCALL_FRAME::Current();
        if (Frame != nullptr && Frame->NtUserCreateWindowEx != nullptr)
        {
            typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG, ULONG_PTR);
            return ((PFN)Frame->NtUserCreateWindowEx)(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown, Unknown2);
        }
#endif
        return HookStub.StubNtUserCreateWindowEx_Win8(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown, Unknown2);
    }

    BOOL NtUserSetDefText(HWND hWnd, PLARGE_UNICODE_STRING Text)
    {
#if ML_AMD64
        LEP_NTUSER_SYSCALL_FRAME* Frame = LEP_NTUSER_SYSCALL_FRAME::Current();
        if (Frame != nullptr && Frame->NtUserDefSetText != nullptr)
        {
            typedef BOOL (NTAPI *PFN)(HWND, PLARGE_UNICODE_STRING);
            return ((PFN)Frame->NtUserDefSetText)(hWnd, Text);
        }
#endif
        return HookStub.StubNtUserDefSetText(hWnd, Text);
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

#endif // _LocaleEmulatorPlus_H_cd444a0d_c7f9_44b2_aac8_8107e9a07ca2_
