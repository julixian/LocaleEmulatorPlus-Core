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
#else
#define LEP_FUNCTION_JUMP_OP                     Mp::OpJump
#endif

#define LepHookFromEAT(_Base, _Prefix, _Name)    Mp::FunctionJumpVa(LookupExportTable(_Base, _Prefix##_##_Name), Lep##_Name, &HookStub.Stub##_Name, LEP_FUNCTION_JUMP_OP)
#define LepHookFromEAT2(_Base, _Prefix, _Name)   Mp::FunctionJumpVa(LookupExportTable(_Base, _Prefix##_##_Name), Lep##_Name, nullptr, LEP_FUNCTION_JUMP_OP)
#define LepFunctionJump(_Name)                   Mp::FunctionJumpVa(_Name, Lep##_Name, &HookStub.Stub##_Name, LEP_FUNCTION_JUMP_OP)
#define LepFunctionCall(_Name)                   Mp::FunctionCallVa(_Name, Lep##_Name, &HookStub.Stub##_Name)

class LepGlobalData;
typedef LepGlobalData* PLepGlobalData;

VOID LepNlsDiag(PCWSTR Format, ...);

#define THREAD_LOCAL_BUFFER_CONTEXT TAG4('LTLB')
#define LEP_LOADER_PROCESS           TAG4('LepL')

#if LEP_DIAG_INIT
#define LEP_DIAG_HEADER(_stage) ExceptionBox(_stage, L"LEP modern init diag")
#else
#define LEP_DIAG_HEADER(_stage)
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
    DirectoryName.Length          = wcslen(DirectoryNameBuffer) * sizeof(WCHAR);
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

    // ExceptionBox(L"OpenOrCreateLepPeb");

    LEP_DIAG_HEADER(L"OpenOrCreateLepPeb entry");
    SessionId = GetSessionId(ProcessId);
    LEP_DIAG_HEADER(SessionId == INVALID_SESSION_ID ? L"GetSessionId invalid" : L"GetSessionId ok");
    if (SessionId == INVALID_SESSION_ID)
        return nullptr;

    LEP_DIAG_HEADER(L"before section root swprintf");
    FormatLepBaseNamedObjectsRoot(RootNameBuffer, SessionId);
    LEP_DIAG_HEADER(L"section root format ok");

    LEP_DIAG_HEADER(L"before LepOpenDirectoryObject(BaseNamedObjects)");
    Status = LepOpenDirectoryObject(&RootHandle, RootNameBuffer, nullptr);
    LEP_DIAG_HEADER(NT_FAILED(Status) ? L"LepOpenDirectoryObject(BaseNamedObjects) failed" : L"LepOpenDirectoryObject(BaseNamedObjects) ok");
    if (NT_FAILED(Status))
        return FALSE;

    LEP_DIAG_HEADER(L"before GetLepPebSectionName");
    SectionName.Length          = (USHORT) GetLepPebSectionName(SectionNameBuffer, ProcessId) * sizeof(WCHAR);
    SectionName.MaximumLength   = sizeof(SectionNameBuffer);
    SectionName.Buffer          = SectionNameBuffer;
    LEP_DIAG_HEADER(L"GetLepPebSectionName ok");

    InitializeObjectAttributes(&ObjectAttributes, &SectionName, 0, RootHandle, nullptr);

    LEP_DIAG_HEADER(L"before NtOpenSection");
    Status = NtOpenSection(&SectionHandle, SECTION_ALL_ACCESS, &ObjectAttributes);
    LEP_DIAG_HEADER(NT_FAILED(Status) ? L"NtOpenSection failed" : L"NtOpenSection ok");
    if (NT_FAILED(Status))
    {
        if (Create)
        {
            LEP_DIAG_HEADER(L"before NtCreateSection");
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
            LEP_DIAG_HEADER(NT_FAILED(Status) ? L"NtCreateSection failed" : L"NtCreateSection ok");
            if (NT_FAILED(Status))
            {
                NtClose(RootHandle);
                return nullptr;
            }
        }
        else
        {
            NtClose(RootHandle);
            LEP_DIAG_HEADER(L"before LepOpenDirectoryObject(Sandbox)");
            Status = LepOpenDirectoryObject(&RootHandle, L"\\Sandbox", nullptr);
            LEP_DIAG_HEADER(NT_FAILED(Status) ? L"LepOpenDirectoryObject(Sandbox) failed" : L"LepOpenDirectoryObject(Sandbox) ok");
            if (NT_FAILED(Status))
                return FALSE;
            LEP_DIAG_HEADER(L"before LepFindSectionObject");
            if (!LepFindSectionObject(&SectionHandle, &SectionName, 6, RootHandle))
            {
                LEP_DIAG_HEADER(L"LepFindSectionObject failed");
                NtClose(RootHandle);
                return nullptr;
            }
            LEP_DIAG_HEADER(L"LepFindSectionObject ok");
        }
    }
    NtClose(RootHandle);

    ViewSize = 0;
    LEPPEB = nullptr;
    LEP_DIAG_HEADER(L"before NtMapViewOfSection");
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
    LEP_DIAG_HEADER(NT_FAILED(Status) ? L"NtMapViewOfSection failed" : L"NtMapViewOfSection ok");

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

    LEP_DIAG_HEADER(L"OpenOrCreateLepPeb return");
    return LEPPEB;
}

#ifndef ENABLE_LOG
#define ENABLE_LOG 0
#endif

#if ENABLE_LOG

inline VOID InitLog(NtFileDisk &LogFile)
{
    WCHAR LogFilePath[MAX_NTPATH];
    UNICODE_STRING SelfPath;
    PLDR_MODULE Self, Target;
    PLEPPEB LEPPEB = nullptr;

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

    swprintf(LogFilePath, L"%wZ\\%wZ.%p.log.txt", &SelfPath, &Target->BaseDllName, CurrentPid());

    if (LEPPEB != nullptr)
    {
        CloseLepPeb(LEPPEB);
    }

    ULONG BOM = BOM_UTF16_LE;
    LogFile.Create(LogFilePath);
    LogFile.Write(&BOM, 2);

    PROCESS_IMAGE_FILE_NAME2 proc;
    //NtQueryInformationProcess(CurrentProcess, ProcessImageFileName, &proc, sizeof(proc), NULL);
    proc.ImageFileName = Target->FullDllName;
    LogFile.Write(proc.ImageFileName.Buffer, proc.ImageFileName.Length);
    LogFile.Write(L"\r\n", 4);
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

        IF_EXIST(LepGlobalData::LogFile)
        {
            InitLog(this->LogFile);
        }
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
        return HookStub.StubNtUserMessageCall(hWnd, Message, wParam, lParam, xParam, xpfnProc, Flags);
    }

    HWND NtUserCreateWindowEx_Win7(ULONG ExStyle, PLARGE_UNICODE_STRING ClassName, PLARGE_UNICODE_STRING ClassVersion, PLARGE_UNICODE_STRING WindowName, ULONG Style, LONG X, LONG Y, LONG Width, LONG Height, HWND ParentWnd, HMENU Menu, PVOID Instance, LPVOID Param, ULONG ShowMode, ULONG Unknown)
    {
        return HookStub.StubNtUserCreateWindowEx_Win7(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown);
    }

    HWND NtUserCreateWindowEx_Win8(ULONG ExStyle, PLARGE_UNICODE_STRING ClassName, PLARGE_UNICODE_STRING ClassVersion, PLARGE_UNICODE_STRING WindowName, ULONG Style, LONG X, LONG Y, LONG Width, LONG Height, HWND ParentWnd, HMENU Menu, PVOID Instance, LPVOID Param, ULONG ShowMode, ULONG Unknown, ULONG Unknown2)
    {
        return HookStub.StubNtUserCreateWindowEx_Win8(ExStyle, ClassName, ClassVersion, WindowName, Style, X, Y, Width, Height, ParentWnd, Menu, Instance, Param, ShowMode, Unknown, Unknown2);
    }

    BOOL NtUserSetDefText(HWND hWnd, PLARGE_UNICODE_STRING Text)
    {
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

#endif // _LocaleEmulatorPlus_H_cd444a0d_c7f9_44b2_aac8_8107e9a07ca2_
