#include "stdafx.h"

ML_OVERLOAD_NEW

#pragma comment(linker, "/ENTRY:DllMain")
#pragma comment(linker, "/SECTION:.text,ERW /MERGE:.rdata=.text /MERGE:.data=.text")
#pragma comment(linker, "/SECTION:.Asuna,ERW /MERGE:.text=.Asuna")
#if ML_AMD64
    #pragma comment(linker, "/EXPORT:GetFileAttributesA=FuckStupid2DJGame")
#else
    #pragma comment(linker, "/EXPORT:GetFileAttributesA=_FuckStupid2DJGame@4")
#endif

EXTC ULONG NTAPI FuckStupid2DJGame(PCSTR FileName)
{
    return FILE_ATTRIBUTE_NORMAL;
/*
    PVOID Rubbish;
    PLDR_MODULE Garbage;

    Garbage = FindLdrModuleByHandle(nullptr);
    Rubbish = _ReturnAddress();

    if (PtrOffset(Rubbish, Garbage->EntryPoint) < 100 && StrCompareA(FileName, "2djgame.txt") == 0)
        return FILE_ATTRIBUTE_NORMAL;

    return Io::QueryFileAttributes(ml::String::Decode(FileName, StrLengthA(FileName), CP_ACP));
*/
}

PLepGlobalData g_GlobalData;

VOID LepSyncNtdllNlsGlobals(USHORT AnsiCodePage, BOOLEAN AnsiDbcsCodePage, BOOLEAN OemDbcsCodePage)
{
    NlsAnsiCodePage = AnsiCodePage;
    NlsMbCodePageTag = AnsiDbcsCodePage;
    NlsMbOemCodePageTag = OemDbcsCodePage;
}

#if LEP_DIAG_INIT

static VOID LepDiagStatus(PCWSTR Stage, NTSTATUS Status)
{
    static const WCHAR Hex[] = L"0123456789ABCDEF";
    WCHAR Buffer[128];
    PWSTR p = Buffer;

    while (*Stage != 0 && p < Buffer + countof(Buffer) - 16)
        *p++ = *Stage++;

    *p++ = L':';
    *p++ = L' ';
    *p++ = L'0';
    *p++ = L'x';

    for (LONG_PTR Shift = 28; Shift >= 0; Shift -= 4)
        *p++ = Hex[((ULONG)Status >> Shift) & 0xF];

    *p = 0;

    ExceptionBox(Buffer, L"LEP modern init diag");
}

#define LEP_DIAG_HERE(_stage) ExceptionBox(_stage, L"LEP modern init diag")
#define LEP_DIAG_HERE_IF(_condition, _stage) \
    do { if (_condition) ExceptionBox(_stage, L"LEP modern init diag"); } while (0)
#define LEP_DIAG_FAIL_RETURN(_stage, _expr) \
    do { Status = (_expr); if (NT_FAILED(Status)) { LepDiagStatus(_stage, Status); return Status; } } while (0)

#else

#define LEP_DIAG_HERE(_stage)
#define LEP_DIAG_HERE_IF(_condition, _stage)
#define LEP_DIAG_FAIL_RETURN(_stage, _expr) \
    do { Status = (_expr); FAIL_RETURN(Status); } while (0)

#endif

ForceInline VOID LepSetGlobalData(PLepGlobalData GlobalData)
{
    g_GlobalData = GlobalData;
}

ForceInline VOID LepReleaseGlobalData()
{
    SafeDeleteT(g_GlobalData);
}

BOOL NTAPI DelayInitDllEntry(PVOID BaseAddress, ULONG Reason, PVOID Reserved)
{
    BOOL Success;
    PLDR_MODULE Module = FindLdrModuleByHandle(BaseAddress);
    PIMAGE_NT_HEADERS NtHeaders;

    NtHeaders = PtrAdd((PIMAGE_NT_HEADERS)BaseAddress, ((PIMAGE_DOS_HEADER)BaseAddress)->e_lfanew);
    Module->EntryPoint = PtrAdd(BaseAddress, NtHeaders->OptionalHeader.AddressOfEntryPoint);

    Success = ((API_POINTER(DelayInitDllEntry))Module->EntryPoint)(BaseAddress, Reason, Reserved);

    if (Reason == DLL_PROCESS_ATTACH && !Success)
        return Success;

    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            if (!Success)
                return Success;
        case DLL_PROCESS_DETACH:
            break;

        default:
            return Success;
    }

    LepGetGlobalData()->HookModule(BaseAddress, &Module->BaseDllName, Reason == DLL_PROCESS_ATTACH);

    return Success;
}

NTSTATUS GetNlsFile(PUNICODE_STRING NlsFile, ULONG NlsIndex, PCWSTR SubKey)
{
    BOOL        Success;
    WCHAR       NlsIndexBuffer[16];
    NTSTATUS    Status;
    PKEY_VALUE_PARTIAL_INFORMATION FileName;

    FormatLepUIntDecimal(NlsIndexBuffer, NlsIndex);

    Status = GetKeyValue(REGKEY_ROOT, SubKey, NlsIndexBuffer, &FileName);
    FAIL_RETURN(Status);

    Success = RtlCreateUnicodeString(NlsFile, (PWSTR)FileName->Data);
    FreeMemoryP(FileName);

    return Success ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

NTSTATUS GetLangFile(PUNICODE_STRING LangFile, ULONG LangIndex, PCWSTR SubKey)
{
    BOOL        Success;
    WCHAR       LangIndexBuffer[16];
    NTSTATUS    Status;
    PKEY_VALUE_PARTIAL_INFORMATION FileName;

    FormatLepUIntHex4(LangIndexBuffer, LangIndex);

    Status = GetKeyValue(REGKEY_ROOT, SubKey, LangIndexBuffer, &FileName);
    FAIL_RETURN(Status);

    Success = RtlCreateUnicodeString(LangFile, (PWSTR)FileName->Data);
    FreeMemoryP(FileName);

    return Success ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

NTSTATUS ReadFileInSystemDirectory(NtFileMemory &File, PUNICODE_STRING Path)
{
    PWSTR       Buffer;
    ULONG_PTR   Length;
    NTSTATUS    Status;

    Length = sizeof(ROOTDIR_SYSTEM32) + Path->Length + sizeof(WCHAR);
    Buffer = (PWSTR)AllocateMemoryP(Length);
    if (Buffer == nullptr)
        return STATUS_NO_MEMORY;

    Length = CONST_STRLEN(ROOTDIR_SYSTEM32);
    CopyMemory(Buffer, ROOTDIR_SYSTEM32, Length * sizeof(WCHAR));
    CopyMemory(Buffer + Length, Path->Buffer, Path->Length);
    Buffer[Length + Path->Length / sizeof(WCHAR)] = 0;

    Status = File.Open(Buffer, NFD_NOT_RESOLVE_PATH);

    FreeMemoryP(Buffer);

    return Status;
}

NTSTATUS LepGlobalData::Initialize()
{
    BOOL            IsLoader;
    BOOL            LepPebMapped;
    PLEPPEB          LEPPEB;
    PLDR_MODULE     Ntdll;
    PPEB_BASE       ProcessEnvironment;
    NTSTATUS        Status;
    BOOL            DiagVerbose;
    NLSTABLEINFO    NlsTableInfo;
    UNICODE_STRING  SystemDirectory, NlsFileName, OemNlsFileName, LangFileName, Win32U;
    PKEY_VALUE_PARTIAL_INFORMATION IndexValue;

#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
    if (LEP_X64_CRASH_PROBE == 0)
        return STATUS_DLL_INIT_FAILED;
#endif

    IsLoader = IsLepLoader();
    DiagVerbose = !IsLoader;
    LEP_DIAG_HERE_IF(DiagVerbose, L"LepGlobalData::Initialize entry");
    LepPebMapped = FALSE;
#if ML_AMD64 && defined(LEP_X64_ATTACH_WAIT)
    if (!IsLoader)
    {
        WCHAR Buffer[0x100];
        PWSTR BufferEnd;

        static const WCHAR Prefix[] = L"Attach x64dbg to PID 0x";
        static const WCHAR Suffix[] = L", then press OK.";

        CopyMemory(Buffer, Prefix, sizeof(Prefix) - sizeof(WCHAR));
        BufferEnd = Buffer + CONST_STRLEN(Prefix);
        BufferEnd += FormatLepUIntHex(BufferEnd, CurrentPid());
        CopyMemory(BufferEnd, Suffix, sizeof(Suffix));

        ExceptionBox(Buffer, L"LEP x64 attach wait");
    }
#endif
#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
    if (LEP_X64_CRASH_PROBE == 10 && IsLoader)
        return STATUS_DLL_INIT_FAILED;
    if (LEP_X64_CRASH_PROBE == 11 && !IsLoader)
        return STATUS_DLL_INIT_FAILED;
#endif
    LEP_DIAG_HERE_IF(DiagVerbose, IsLoader ? L"IsLepLoader: true" : L"IsLepLoader: false");

    Wow64 = Ps::IsWow64Process();
    LEP_DIAG_HERE_IF(DiagVerbose, Wow64 ? L"Ps::IsWow64Process: true" : L"Ps::IsWow64Process: false");

    Ntdll = GetNtdllLdrModule();
    if (Ntdll == nullptr)
    {
#if LEP_DIAG_INIT
        LepDiagStatus(L"GetNtdllLdrModule", STATUS_DLL_INIT_FAILED);
#endif
        return STATUS_DLL_INIT_FAILED;
    }
    LEP_DIAG_HERE_IF(DiagVerbose, L"GetNtdllLdrModule ok");

    LOOP_ONCE
    {
        LEP_DIAG_HERE_IF(DiagVerbose, L"before OpenOrCreateLepPeb");
        LEPPEB = OpenOrCreateLepPeb();
        LEP_DIAG_HERE_IF(DiagVerbose, LEPPEB == nullptr ? L"OpenOrCreateLepPeb: null" : L"OpenOrCreateLepPeb: mapped");
        LepPebMapped = LEPPEB != nullptr;
        if (LEPPEB == nullptr)
        {
            ULONG_PTR       DefaultACPLength, DefaultLCIDLength, DefaultOEMCPLength;
            WCHAR           DefaultACP[0x20], DefaultOEMCP[0x20], DefaultLCID[0x20];
            PVOID           ReloadedNtdll;
            PUNICODE_STRING FullDllName;
            PLDR_MODULE     Lepdll;

            LEPPEB = GetLepPeb();
            LEP_DIAG_HERE_IF(DiagVerbose, L"GetLepPeb ok");

            InitDefaultLeb(&LEPPEB->LEPB);
            LEP_DIAG_HERE_IF(DiagVerbose, L"InitDefaultLeb ok");

            Lepdll = FindLdrModuleByHandle(&__ImageBase);
            LEP_DIAG_HERE_IF(DiagVerbose, Lepdll == nullptr ? L"FindLdrModuleByHandle(self): null" : L"FindLdrModuleByHandle(self): ok");
            if (Lepdll)
            {
                FullDllName = &Lepdll->FullDllName;
                CopyMemory(LEPPEB->LepDllFullPath, FullDllName->Buffer, FullDllName->Length + sizeof(WCHAR));
                CopyMemory(LEPPEB->LepDllDirPath, FullDllName->Buffer,
                    FullDllName->Length + sizeof(WCHAR) - Lepdll->BaseDllName.Length);
                LEPPEB->LepDllDirPath[(FullDllName->Length - Lepdll->BaseDllName.Length) / sizeof(WCHAR)] = 0;
            }

            LEP_DIAG_HERE_IF(DiagVerbose, L"before LoadPeImage(ntdll)");
            Status = LoadPeImage(Ntdll->FullDllName.Buffer, &ReloadedNtdll, nullptr, LOAD_PE_IGNORE_RELOC);
#if LEP_DIAG_INIT
            if (DiagVerbose || NT_FAILED(Status))
                LepDiagStatus(L"LoadPeImage(ntdll)", Status);
#endif
            if (NT_SUCCESS(Status))
            {
                PVOID LdrLoadDllAddress;

                LdrLoadDllAddress = LookupExportTable(ReloadedNtdll, NTDLL_LdrLoadDll);
                LEP_DIAG_HERE_IF(DiagVerbose, LdrLoadDllAddress == nullptr ? L"Lookup LdrLoadDll: null" : L"Lookup LdrLoadDll: ok");
                LEPPEB->LdrLoadDllAddress = PtrAdd(LdrLoadDllAddress, PtrOffset(Ntdll->DllBase, ReloadedNtdll));
                CopyMemory(LEPPEB->LdrLoadDllBackup, LdrLoadDllAddress, LDR_LOAD_DLL_BACKUP_SIZE);
                LEPPEB->LdrLoadDllBackupSize = LDR_LOAD_DLL_BACKUP_SIZE;

                UnloadPeImage(ReloadedNtdll);
                LEP_DIAG_HERE_IF(DiagVerbose, L"UnloadPeImage(ntdll) ok");
            }

            LEP_DIAG_HERE_IF(DiagVerbose, L"before format defaults");
            DefaultACPLength    = (FormatLepUIntDecimal(DefaultACP, LEPPEB->LEPB.AnsiCodePage) + 1) * sizeof(WCHAR);
            DefaultOEMCPLength  = (FormatLepUIntDecimal(DefaultOEMCP, LEPPEB->LEPB.OemCodePage) + 1) * sizeof(WCHAR);
            DefaultLCIDLength   = (FormatLepUIntDecimal(DefaultLCID, LEPPEB->LEPB.LocaleID) + 1) * sizeof(WCHAR);
            LEP_DIAG_HERE_IF(DiagVerbose, L"format defaults ok");

            REGISTRY_REDIRECTION_ENTRY64 *Entry, Entries[] =
            {
                {
                    { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_ACP), REG_SZ, },
                    { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_ACP), REG_SZ, DefaultACP, DefaultACPLength },
                },
                {
                    { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_OEMCP), REG_SZ, },
                    { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_OEMCP), REG_SZ, DefaultOEMCP, DefaultOEMCPLength },
                },
                {
                    { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_LANGUAGE), USTR64(REGKEY_DEFAULT_LANGUAGE), REG_SZ, },
                    { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_LANGUAGE), USTR64(REGKEY_DEFAULT_LANGUAGE), REG_SZ, DefaultLCID, DefaultLCIDLength },
                },
            };

            Status = this->InitRegistryRedirection(Entries, countof(Entries), nullptr);
        }
        else
        {
            *GetLepPeb() = *LEPPEB;
            Status = this->InitRegistryRedirection(LEPPEB->LEPB.RegistryReplacement, LEPPEB->LEPB.NumberOfRegistryRedirectionEntries, &LEPPEB->LEPB);
            if (this->RegistryRedirectionEntry.GetSize() == 0)
                Status = this->InitDefaultRegistryRedirection();
#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
            if (LEP_X64_CRASH_PROBE == 20 && this->RegistryRedirectionEntry.GetSize() == 0)
                return STATUS_DLL_INIT_FAILED;
            if (LEP_X64_CRASH_PROBE == 21 && this->RegistryRedirectionEntry.GetSize() != 0)
                return STATUS_DLL_INIT_FAILED;
#endif

            NtClose(LEPPEB->Section);
            CloseLepPeb(LEPPEB);
        }

        if (IsLoader && !LepPebMapped)
        {
#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
            if (LEP_X64_CRASH_PROBE == 12)
                return STATUS_DLL_INIT_FAILED;
#endif
            break;
        }

        LEP_DIAG_FAIL_RETURN(L"TextMetricCache.Initialize", this->TextMetricCache.Initialize());

        PVOID           NlsBaseAddress;
        LCID            DefaultLocaleID;
        LARGE_INTEGER   DefaultCasingTableSize;

        LEP_DIAG_FAIL_RETURN(L"NtInitializeNlsFiles", NtInitializeNlsFiles(&NlsBaseAddress, &DefaultLocaleID, &DefaultCasingTableSize));

        this->GetLepPeb()->OriginalLocaleID = DefaultLocaleID;

        NtUnmapViewOfSection(CurrentProcess, NlsBaseAddress);

        WriteLog(L"init LEPB %s", GetLepPeb()->LepDllFullPath);
        WriteLog(L"LEP image base: %p", &__ImageBase);

        SystemDirectory = Ntdll->FullDllName;
        SystemDirectory.Length -= Ntdll->BaseDllName.Length;

        LEP_DIAG_FAIL_RETURN(L"RtlDuplicateUnicodeString(SystemDirectory)", RtlDuplicateUnicodeString(RTL_DUPSTR_ADD_NULL, &SystemDirectory, &this->SystemDirectory));

        ml::String Win32U_Path(SystemDirectory);
        Win32U_Path += L"\\win32u.dll";
        this->HasWin32U = Nt_GetFileAttributes(Win32U_Path.GetBuffer()) != INVALID_FILE_ATTRIBUTES;
        WriteLog(L"win32u: %s, has:%d", Win32U_Path.GetBuffer(), this->HasWin32U);

        RtlInitEmptyString(&NlsFileName, nullptr, 0);
        RtlInitEmptyString(&OemNlsFileName, nullptr, 0);
        RtlInitEmptyString(&LangFileName, nullptr, 0);

        SCOPE_EXIT
        {
            RtlFreeUnicodeString(&NlsFileName);
            RtlFreeUnicodeString(&OemNlsFileName);
            RtlFreeUnicodeString(&LangFileName);
        }
        SCOPE_EXIT_END;

        LEP_DIAG_FAIL_RETURN(L"GetNlsFile(ACP)", GetNlsFile(&NlsFileName, GetLepb()->AnsiCodePage, REGPATH_CODEPAGE));

        LEP_DIAG_FAIL_RETURN(L"GetNlsFile(OEMCP)", GetNlsFile(&OemNlsFileName, GetLepb()->OemCodePage, REGPATH_CODEPAGE));

        // Windows 10 1803 removed all keys under "HKLM\SYSTEM\CurrentControlSet\Control\Nls\Language".
        // Since the value of all keys are "l_intl.nls" since Windows XP, 
        // we suppose that MS is not going to change it in the near future.
        // 
        //Status = GetLangFile(&LangFileName, GetLepb()->LocaleID, REGPATH_LANGUAGE);
        //FAIL_RETURN(Status);
        RtlCreateUnicodeString(&LangFileName, L"l_intl.nls");

        NtFileMemory AnsiFile, OemFile, LangFile;

        LEP_DIAG_FAIL_RETURN(L"ReadFileInSystemDirectory(ACP)", ReadFileInSystemDirectory(AnsiFile, &NlsFileName));

        LEP_DIAG_FAIL_RETURN(L"ReadFileInSystemDirectory(OEMCP)", ReadFileInSystemDirectory(OemFile, &OemNlsFileName));

        LEP_DIAG_FAIL_RETURN(L"ReadFileInSystemDirectory(Lang)", ReadFileInSystemDirectory(LangFile, &LangFileName));

        AnsiCodePageOffset      = 0;
        OemCodePageOffset       = ROUND_UP(AnsiFile.GetSize32(), 16);
        UnicodeCaseTableOffset  = OemCodePageOffset + ROUND_UP(OemFile.GetSize32(), 16);

        LEP_DIAG_FAIL_RETURN(L"AllocVirtualMemory(CodePageMapView)", AllocVirtualMemory(&CodePageMapView, UnicodeCaseTableOffset + LangFile.GetSize32(), PAGE_READWRITE, MEM_COMMIT | MEM_TOP_DOWN));

        CopyMemory(PtrAdd(CodePageMapView, AnsiCodePageOffset),     AnsiFile.GetBuffer(),   AnsiFile.GetSize32());
        CopyMemory(PtrAdd(CodePageMapView, OemCodePageOffset),      OemFile.GetBuffer(),    OemFile.GetSize32());
        CopyMemory(PtrAdd(CodePageMapView, UnicodeCaseTableOffset), LangFile.GetBuffer(),   LangFile.GetSize32());

        ProtectVirtualMemory(CodePageMapView, UnicodeCaseTableOffset + LangFile.GetSize32(), PAGE_READONLY);

        RtlInitNlsTables(
            (PUSHORT)PtrAdd(CodePageMapView, AnsiCodePageOffset),
            (PUSHORT)PtrAdd(CodePageMapView, OemCodePageOffset),
            (PUSHORT)PtrAdd(CodePageMapView, UnicodeCaseTableOffset),
            &NlsTableInfo
        );

        RtlResetRtlTranslations(&NlsTableInfo);

        LepSyncNtdllNlsGlobals(
            (USHORT)GetLepb()->AnsiCodePage,
            (BOOLEAN)(NlsTableInfo.AnsiTableInfo.DBCSCodePage != 0),
            (BOOLEAN)(NlsTableInfo.OemTableInfo.DBCSCodePage != 0));
        WriteLog(L"reset nls acp=%u ntdll=%u mb=%u oemmb=%u table=%u/%u",
            GetLepb()->AnsiCodePage,
            NlsAnsiCodePage,
            NlsMbCodePageTag,
            NlsMbOemCodePageTag,
            NlsTableInfo.AnsiTableInfo.CodePage,
            NlsTableInfo.OemTableInfo.CodePage);

        ProcessEnvironment = LepCurrentPeb();

        ProcessEnvironment->AnsiCodePageData       = (PUSHORT)PtrAdd(CodePageMapView, AnsiCodePageOffset);
        ProcessEnvironment->OemCodePageData        = (PUSHORT)PtrAdd(CodePageMapView, OemCodePageOffset);
        ProcessEnvironment->UnicodeCaseTableData   = (PUSHORT)PtrAdd(CodePageMapView, UnicodeCaseTableOffset);

        // LdrInitShimEngineDynamic(&__ImageBase);

        LdrRegisterDllNotification(0,
            [] (ULONG NotificationReason, PCLDR_DLL_NOTIFICATION_DATA NotificationData, PVOID Context)
            {
                return ((PLepGlobalData)Context)->DllNotification(NotificationReason, NotificationData);
            },
            this,
            &DllNotificationCookie
        );
    }

    Status = InstallHookPort();
    WriteLog(L"inst hp: %08X", Status);
    LEP_DIAG_FAIL_RETURN(L"InstallHookPort", Status);

    Status = HookNtdllRoutines(Ntdll->DllBase);
#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
    if (LEP_X64_CRASH_PROBE == 13)
        return STATUS_DLL_INIT_FAILED;
#endif

    WriteLog(L"hook ntdll: %08X", Status);
    LEP_DIAG_FAIL_RETURN(L"HookNtdllRoutines", Status);

    if (IsLoader && !LepPebMapped)
    {
#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
        if (LEP_X64_CRASH_PROBE == 14)
            return STATUS_DLL_INIT_FAILED;
#endif
        return Status;
    }

    HackAnsiOemCodeHashNodes();

    PLDR_MODULE Kernel32Ldr;

    Kernel32Ldr = FindLdrModuleByName(&USTR(L"KERNELBASE.dll"));
    if (Kernel32Ldr != nullptr)
    {
        if (FLAG_ON(Kernel32Ldr->Flags, LDRP_PROCESS_ATTACH_CALLED))
            HookKernel32Routines(Kernel32Ldr->DllBase);
        else
            Kernel32Ldr->EntryPoint = DelayInitDllEntry;
    }

    Kernel32Ldr = GetKernel32Ldr();
    if (Kernel32Ldr != nullptr)
    {
        if (FLAG_ON(Kernel32Ldr->Flags, LDRP_PROCESS_ATTACH_CALLED))
            HookKernel32Routines(Kernel32Ldr->DllBase);
        else
            Kernel32Ldr->EntryPoint = DelayInitDllEntry;
    }

    WriteLog(L"init %p", Status);

    return Status;
}

NTSTATUS LepGlobalData::InitRegistryRedirection(PREGISTRY_REDIRECTION_ENTRY64 Entry64, ULONG_PTR Count, PVOID BaseAddress)
{
    NTSTATUS    Status;
    PLEPPEB      LEPPEB;
    PREGISTRY_REDIRECTION_ENTRY Entry;

    if (Count == 0)
        return STATUS_NO_MORE_ENTRIES;

    LEPPEB = this->GetLepPeb();

#pragma push_macro("USTR64ToUSTR")
#undef USTR64ToUSTR
#define USTR64ToUSTR(ustr64) UNICODE_STRING({ ustr64.Length, ustr64.MaximumLength, PtrAdd(ustr64.Buffer, BaseAddress) });

    REGISTRY_REDIRECTION_ENTRY LocalEntry;

    FOR_EACH(Entry64, Entry64, Count)
    {
        ULONG_PTR       LastIndex;
        HANDLE          OriginalKey, RedirectedKey;
        UNICODE_STRING  KeyFullPath;

        OriginalKey     = nullptr;
        RedirectedKey   = nullptr;

        Status = Reg::OpenKey(&OriginalKey, (HANDLE)Entry64->Original.Root, KEY_QUERY_VALUE, PtrAdd(Entry64->Original.SubKey.Buffer, BaseAddress));
        FAIL_CONTINUE(Status);

        if (Entry64->Redirected.Root != NULL)
        {
            Status = Reg::OpenKey(&RedirectedKey, (HANDLE)Entry64->Redirected.Root, KEY_QUERY_VALUE, PtrAdd(Entry64->Redirected.SubKey.Buffer, BaseAddress));
            if (NT_FAILED(Status))
            {
                Reg::CloseKeyHandle(OriginalKey);
                continue;
            }
        }

        this->RegistryRedirectionEntry.Add(LocalEntry);
        LastIndex = this->RegistryRedirectionEntry.GetSize() - 1;
        Entry = &this->RegistryRedirectionEntry[LastIndex];

        Status = QueryRegKeyFullPath(OriginalKey, &KeyFullPath);
        Reg::CloseKeyHandle(OriginalKey);
        if (NT_FAILED(Status))
        {
            Reg::CloseKeyHandle(RedirectedKey);
            this->RegistryRedirectionEntry.Remove(LastIndex);
            continue;
        }

        Entry->Original.FullPath = KeyFullPath;
        RtlFreeUnicodeString(&KeyFullPath);

        if (RedirectedKey != nullptr)
        {
            Status = QueryRegKeyFullPath(RedirectedKey, &KeyFullPath);
            Reg::CloseKeyHandle(RedirectedKey);
            if (NT_FAILED(Status))
            {
                this->RegistryRedirectionEntry.Remove(LastIndex);
                continue;
            }

            Entry->Redirected.FullPath = KeyFullPath;
            RtlFreeUnicodeString(&KeyFullPath);
        }

        Entry->Original.Root        = (HKEY)Entry64->Original.Root;
        Entry->Original.SubKey      = USTR64ToUSTR(Entry64->Original.SubKey);
        Entry->Original.ValueName   = USTR64ToUSTR(Entry64->Original.ValueName);
        Entry->Original.DataType    = Entry64->Original.DataType;
        Entry->Original.Data        = nullptr;
        Entry->Original.DataSize    = 0;

        Entry->Redirected.Root      = (HKEY)Entry64->Redirected.Root;
        Entry->Redirected.SubKey    = USTR64ToUSTR(Entry64->Redirected.SubKey);
        Entry->Redirected.ValueName = USTR64ToUSTR(Entry64->Redirected.ValueName);
        Entry->Redirected.DataType  = Entry64->Redirected.DataType;
        Entry->Redirected.Data      = nullptr;
        Entry->Redirected.DataSize  = 0;

        if (Entry64->Redirected.Data != nullptr && Entry64->Redirected.DataSize != 0)
        {
            Entry->Redirected.DataSize = (ULONG_PTR)Entry64->Redirected.DataSize;
            Entry->Redirected.Data = AllocateMemoryP(Entry->Redirected.DataSize);
            if (Entry->Redirected.Data == nullptr)
            {
                this->RegistryRedirectionEntry.Remove(LastIndex);
                continue;
            }

            CopyMemory(Entry->Redirected.Data, PtrAdd(Entry64->Redirected.Data, BaseAddress), Entry->Redirected.DataSize);
        }
    }

#pragma pop_macro("USTR64ToUSTR")

    return STATUS_SUCCESS;
}

NTSTATUS LepGlobalData::InitDefaultRegistryRedirection()
{
    ULONG_PTR DefaultACPLength, DefaultLCIDLength, DefaultOEMCPLength;
    WCHAR DefaultACP[0x20], DefaultOEMCP[0x20], DefaultLCID[0x20];

    DefaultACPLength    = (FormatLepUIntDecimal(DefaultACP, GetLepb()->AnsiCodePage) + 1) * sizeof(WCHAR);
    DefaultOEMCPLength  = (FormatLepUIntDecimal(DefaultOEMCP, GetLepb()->OemCodePage) + 1) * sizeof(WCHAR);
    DefaultLCIDLength   = (FormatLepUIntDecimal(DefaultLCID, GetLepb()->LocaleID) + 1) * sizeof(WCHAR);

    REGISTRY_REDIRECTION_ENTRY64 Entries[] =
    {
        {
            { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_ACP), REG_SZ, },
            { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_ACP), REG_SZ, DefaultACP, DefaultACPLength },
        },
        {
            { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_OEMCP), REG_SZ, },
            { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_CODEPAGE), USTR64(REGKEY_OEMCP), REG_SZ, DefaultOEMCP, DefaultOEMCPLength },
        },
        {
            { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_LANGUAGE), USTR64(REGKEY_DEFAULT_LANGUAGE), REG_SZ, },
            { (ULONG64)HKEY_LOCAL_MACHINE, USTR64(REGPATH_LANGUAGE), USTR64(REGKEY_DEFAULT_LANGUAGE), REG_SZ, DefaultLCID, DefaultLCIDLength },
        },
    };

    return InitRegistryRedirection(Entries, countof(Entries), nullptr);
}

typedef struct
{
    UNICODE_STRING DllName;
    TYPE_OF(&LepGlobalData::HookNtdllRoutines)     HookRoutine;
    TYPE_OF(&LepGlobalData::UnHookNtdllRoutines)   UnHookRoutine;

} DLL_HOOK_ENTRY, *PDLL_HOOK_ENTRY;

PDLL_HOOK_ENTRY LookupDllHookEntry(PCUNICODE_STRING BaseDllName)
{
    static DLL_HOOK_ENTRY DllHookEntries[] =
    {
        { RTL_CONSTANT_STRING(L"USER32.dll"),       &LepGlobalData::HookUser32Routines,      &LepGlobalData::UnHookUser32Routines },
        { RTL_CONSTANT_STRING(L"GDI32.dll"),        &LepGlobalData::HookGdi32Routines,       &LepGlobalData::UnHookGdi32Routines },
        { RTL_CONSTANT_STRING(L"KERNELBASE.dll"),   &LepGlobalData::HookKernel32Routines,    &LepGlobalData::UnHookKernel32Routines },
        { RTL_CONSTANT_STRING(L"KERNEL32.dll"),     &LepGlobalData::HookKernel32Routines,    &LepGlobalData::UnHookKernel32Routines },
    };

    PDLL_HOOK_ENTRY Entry;

    FOR_EACH_ARRAY(Entry, DllHookEntries)
    {
        if (!RtlEqualUnicodeString(BaseDllName, &Entry->DllName, TRUE))
            continue;

        return Entry;
    }

    return nullptr;
}

VOID LepGlobalData::HookModule(PVOID DllBase, PCUNICODE_STRING DllName, BOOL DllLoad)
{
    PDLL_HOOK_ENTRY Entry;

    Entry = LookupDllHookEntry(DllName);
    if (Entry == nullptr)
        return;

    NTSTATUS Status = DllLoad ? (this->*Entry->HookRoutine)(DllBase) : (this->*Entry->UnHookRoutine)();

    UNREFERENCED_PARAMETER(Status);
    WriteLog(L"hook %s: %p", Entry->DllName.Buffer, Status);
}

VOID LepGlobalData::DllNotification(ULONG NotificationReason, PCLDR_DLL_NOTIFICATION_DATA NotificationData)
{
    NTSTATUS            Status;
    PVOID               DllBase;
    ULONG_PTR           Length;
    PLDR_MODULE         Module;
    UNICODE_STRING      DllPath;
    PCUNICODE_STRING    DllName;

    switch (NotificationReason)
    {
        case LDR_DLL_NOTIFICATION_REASON_LOADED:
            DllName = NotificationData->Loaded.BaseDllName;
            DllBase = NotificationData->Loaded.DllBase;
            DllPath = *NotificationData->Loaded.FullDllName;
            DllPath.Length -= DllName->Length;
            break;

        case LDR_DLL_NOTIFICATION_REASON_UNLOADED:
            DllName = NotificationData->Unloaded.BaseDllName;
            DllBase = NotificationData->Unloaded.DllBase;
            DllPath = *NotificationData->Unloaded.FullDllName;
            DllPath.Length -= DllName->Length;
            break;

        default:
            return;
    }

    //if (!RtlEqualUnicodeString(&SystemDirectory, &DllPath, TRUE))
    //    return;

    if (LookupDllHookEntry(DllName) == nullptr)
        return;

    Module = FindLdrModuleByHandle(DllBase);
    if (!FLAG_ON(Module->Flags, LDRP_PROCESS_ATTACH_CALLED))
    {
        Module->EntryPoint = DelayInitDllEntry;
    }
    else
    {
        HookModule(DllBase, DllName, NotificationReason == LDR_DLL_NOTIFICATION_REASON_LOADED);
    }
}

NTSTATUS LepGlobalData::UnInitialize()
{
    if (DllNotificationCookie != nullptr)
    {
        LdrUnregisterDllNotification(DllNotificationCookie);
        DllNotificationCookie = nullptr;
    }

    UnHookGdi32Routines();
    UnHookUser32Routines();
    UnHookKernel32Routines();
    UnHookNtdllRoutines();

    UnInstallHookPort();

    RtlFreeUnicodeString(&SystemDirectory);

    return 0;
}

inline BOOL UnInitialize(PVOID BaseAddress)
{
    LepReleaseGlobalData();
    ml::MlUnInitialize();
    return FALSE;
}

VOID GenerateModuleList(ml::String &ModuleNames)
{
    PVOID                               Address, BaseAddress, LastAllocationBase;
    ULONG_PTR                           Size, Need, BufferSize;
    NTSTATUS                            Status;
    SYSTEM_BASIC_INFORMATION            SystemBasic;
    MEMORY_BASIC_INFORMATION            MemoryBasic;

    union
    {
        MEMORY_MAPPED_FILENAME_INFORMATION2 MappedFile;
        BYTE MappedFileBuffer[0x2000];
    };

    auto AppendHexPointer = [] (ml::String &String, ULONG_PTR Value)
    {
        BOOL LeadingZero = TRUE;
        WCHAR Buffer[bitsof(Value) / 4];
        ULONG_PTR Count = 0;

        for (LONG_PTR Shift = bitsof(Value) - 4; Shift >= 0; Shift -= 4)
        {
            ULONG_PTR Digit = (Value >> Shift) & 0xF;

            if (Digit == 0 && LeadingZero && Shift != 0)
                continue;

            LeadingZero = FALSE;
            Buffer[Count++] = (WCHAR)(Digit < 10 ? L'0' + Digit : L'A' + Digit - 10);
        }

        String.Concat(Buffer, Count);
    };

    Status = NtQuerySystemInformation(SystemBasicInformation, &SystemBasic, sizeof(SystemBasic), nullptr);
    if (!NT_SUCCESS(Status))
        return;

    LastAllocationBase = IMAGE_INVALID_VA;
    BaseAddress = (PVOID)SystemBasic.MinimumUserModeAddress;
    BaseAddress = nullptr;

    for (; (ULONG_PTR)BaseAddress < SystemBasic.MaximumUserModeAddress; BaseAddress = PtrAdd(BaseAddress, MemoryBasic.RegionSize))
    {
        MemoryBasic.RegionSize = MEMORY_PAGE_SIZE;
        Status = NtQueryVirtualMemory(CurrentProcess, BaseAddress, MemoryBasicInformation, &MemoryBasic, sizeof(MemoryBasic), nullptr);
        FAIL_CONTINUE(Status);

        BaseAddress = MemoryBasic.BaseAddress;

        if (MemoryBasic.Type != MEM_IMAGE || MemoryBasic.AllocationBase == LastAllocationBase)
            continue;

        LastAllocationBase = MemoryBasic.AllocationBase;

        Status = NtQueryVirtualMemory(CurrentProcess, BaseAddress, MemoryMappedFilenameInformation, &MappedFile, sizeof(MappedFileBuffer), nullptr);
        if (NT_FAILED(Status) || MappedFile.Name.Length == 0)
            continue;

        UNICODE_STRING DosPath;

        Status = Io::QueryDosPathFromNtDeviceName(&DosPath, &MappedFile.Name);

        ModuleNames += L"0x";
        AppendHexPointer(ModuleNames, (ULONG_PTR)BaseAddress);
        ModuleNames += L": ";
        if (NT_SUCCESS(Status))
            ModuleNames.Concat(DosPath.Buffer, DosPath.Length / sizeof(WCHAR));
        else
            ModuleNames.Concat(MappedFile.Name.Buffer, MappedFile.Name.Length / sizeof(WCHAR));
        ModuleNames += L"\n";

        RtlFreeUnicodeString(&DosPath);
    }
}

BOOL Initialize(PVOID BaseAddress)
{
    NTSTATUS            Status;
    PLDR_MODULE         Kernel32;
    PLepGlobalData       GlobalData;
    BOOL                IsLoader;

    ml::MlInitialize();

    IsLoader = FindThreadFrame(LEP_LOADER_PROCESS) != nullptr;
    LEP_DIAG_HERE_IF(!IsLoader, L"Initialize entry");

    if (!IsLoader)
    {
        Kernel32 = GetKernel32Ldr();
        if (Kernel32 != nullptr && FLAG_ON(Kernel32->Flags, LDRP_PROCESS_ATTACH_CALLED))
        {
            ml::String ModuleList = L"kernel32 has been loaded before the initialization of LEP\n\nModule list:\n\n";
            GenerateModuleList(ModuleList);
            ExceptionBox(ModuleList);
            LEP_DIAG_HERE(L"kernel32 loaded before LEP");
            return FALSE;
        }
    }

    LdrDisableThreadCalloutsForDll(BaseAddress);

    GlobalData = new LepGlobalData;
    if (GlobalData == nullptr)
    {
        LEP_DIAG_HERE(L"new LepGlobalData failed");
        return FALSE;
    }

    LepSetGlobalData(GlobalData);

#if ENABLE_LOG
    InitLog(GlobalData->LogFile);
#endif

    Status = GlobalData->Initialize();
    if (NT_FAILED(Status))
    {
#if LEP_DIAG_INIT
        LepDiagStatus(L"GlobalData.Initialize", Status);
#endif
        return FALSE;
    }

    WriteLog(L"init ret");

    return TRUE;
}

BOOL NTAPI DllMain(PVOID BaseAddress, ULONG Reason, PVOID Reserved)
{
    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            return Initialize(BaseAddress) || UnInitialize(BaseAddress);

        case DLL_PROCESS_DETACH:
            UnInitialize(BaseAddress);
            break;
    }

    return TRUE;
}
