#include "stdafx.h"
#include "../../LoaderDll/BrokerProtocol.h"

extern BOOL Initialize(PVOID BaseAddress, PLEP_BOOTSTRAP_PAYLOAD BootstrapPayload = nullptr);

typedef struct
{
    USHORT  Size;
    USHORT  Unknown;
    WCHAR   Version16Name[1];

} IMAGE_VERSION_INFO_DATA_ENTRY, *PIMAGE_VERSION_INFO_DATA_ENTRY;

EXTC __declspec(dllexport) LEP_FIRST_DLL_BOOTSTRAP_DATA LepFirstDllBootstrapData = {};

#if !ML_AMD64
PVOID SearchLdrInitNtContinue()
{
    return WalkOpCodeT(LdrInitializeThunk, 0x5B,
                WalkOpCodeM(Buffer, OpLength, Ret)
                {
                    if (Buffer[0] == CALL && (PVOID)GetCallDestination(Buffer) == NtContinue)
                    {
                        Ret = Buffer;
                        return STATUS_SUCCESS;
                    }

                    return STATUS_NOT_FOUND;
                }
    );
}
#endif

#define LEP_BROKER_LAUNCH_CONTEXT TAG4('LBRC')

static ULONG LepCurrentTimeMs()
{
    return NtGetTickCount();
}

static BOOL LepBuildArchitecturePath(PCWSTR Source, BOOL TargetWow64, PWSTR Destination, ULONG Capacity)
{
    static const WCHAR X86[] = L"_x86.dll";
    static const WCHAR X64[] = L"_x64.dll";
    PCWSTR Replacement = TargetWow64 ? X86 : X64;
    ULONG_PTR Length = StrLengthW(Source), SuffixLength = CONST_STRLEN(X86);
    ULONG_PTR Position = Length;

    if (Length + 1 > Capacity)
        return FALSE;
    CopyMemory(Destination, Source, (Length + 1) * sizeof(WCHAR));
    while (Position >= SuffixLength)
    {
        Position--;
        if (StrICompareW(&Destination[Position], X86) == 0 || StrICompareW(&Destination[Position], X64) == 0)
        {
            CopyMemory(&Destination[Position], Replacement, (SuffixLength + 1) * sizeof(WCHAR));
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL LepBuildLoaderPath(PCWSTR CorePath, BOOL TargetWow64, PWSTR Destination, ULONG Capacity)
{
    ULONG_PTR Length = StrLengthW(CorePath), End = Length;
    static const WCHAR NameX86[] = L"LoaderDll_x86.dll";
    static const WCHAR NameX64[] = L"LoaderDll_x64.dll";
    PCWSTR Name = TargetWow64 ? NameX86 : NameX64;
    ULONG_PTR NameLength = StrLengthW(Name);

    while (End != 0 && CorePath[End - 1] != L'\\')
        --End;
    if (End + NameLength + 1 > Capacity)
        return FALSE;
    CopyMemory(Destination, CorePath, End * sizeof(WCHAR));
    CopyMemory(Destination + End, Name, (NameLength + 1) * sizeof(WCHAR));
    return TRUE;
}

NTSTATUS LepGlobalData::BuildBootstrapPayload(
    PCWSTR FullPath,
    PLEP_BOOTSTRAP_PAYLOAD* Payload,
    PULONG PayloadSize
)
{
    if (FullPath == nullptr || Payload == nullptr || PayloadSize == nullptr)
        return STATUS_INVALID_PARAMETER;

    ULONG_PTR EntryCount = this->RegistryRedirectionEntry.GetSize();
    ULONG64 EnvironmentSize64 = FIELD_OFFSET(LEPB, RegistryReplacement) +
                                EntryCount * sizeof(REGISTRY_REDIRECTION_ENTRY64);
    PREGISTRY_REDIRECTION_ENTRY Entry;
    FOR_EACH_VEC(Entry, this->RegistryRedirectionEntry)
    {
        if (Entry->Original.SubKey.GetSize() > USHRT_MAX ||
            Entry->Original.ValueName.GetSize() > USHRT_MAX ||
            Entry->Redirected.SubKey.GetSize() > USHRT_MAX ||
            Entry->Redirected.ValueName.GetSize() > USHRT_MAX)
        {
            return STATUS_NAME_TOO_LONG;
        }
        EnvironmentSize64 += Entry->Original.SubKey.GetSize() + sizeof(WCHAR);
        EnvironmentSize64 += Entry->Original.ValueName.GetSize() + sizeof(WCHAR);
        EnvironmentSize64 += Entry->Redirected.SubKey.GetSize() + sizeof(WCHAR);
        EnvironmentSize64 += Entry->Redirected.ValueName.GetSize() + sizeof(WCHAR);
        EnvironmentSize64 += Entry->Redirected.DataSize;
    }
    if (EntryCount > 0x10000 || EnvironmentSize64 > LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE)
        return STATUS_BUFFER_OVERFLOW;

    ULONG EnvironmentSize = (ULONG)EnvironmentSize64;
    PLEPB Environment = (PLEPB)AllocateMemoryP(EnvironmentSize);
    if (Environment == nullptr)
        return STATUS_NO_MEMORY;
    ZeroMemory(Environment, EnvironmentSize);

    CopyMemory(Environment, GetLepb(), FIELD_OFFSET(LEPB, NumberOfRegistryRedirectionEntries));
    Environment->NumberOfRegistryRedirectionEntries = EntryCount;
    PREGISTRY_REDIRECTION_ENTRY64 Entry64 = Environment->RegistryReplacement;
    PBYTE Buffer = (PBYTE)(Entry64 + EntryCount);

    auto StringToUnicode64 = [&] (UNICODE_STRING64& Ustr64, ml::String& String)
    {
        ULONG Length = (ULONG)String.GetSize();
        Ustr64.Length = (USHORT)Length;
        Ustr64.MaximumLength = (USHORT)Length;
        Ustr64.Dummy = PtrOffset(Buffer, Environment);
        CopyMemory(Buffer, (PWSTR)String, Length);
        Buffer += Length;
        *(PWCHAR)Buffer = 0;
        Buffer += sizeof(WCHAR);
    };

    FOR_EACH_VEC(Entry, this->RegistryRedirectionEntry)
    {
        Entry64->Original.Root = (ULONG64)Entry->Original.Root;
        Entry64->Original.DataType = (ULONG)Entry->Original.DataType;
        StringToUnicode64(Entry64->Original.SubKey, Entry->Original.SubKey);
        StringToUnicode64(Entry64->Original.ValueName, Entry->Original.ValueName);

        Entry64->Redirected.Root = (ULONG64)Entry->Redirected.Root;
        Entry64->Redirected.DataType = (ULONG)Entry->Redirected.DataType;
        StringToUnicode64(Entry64->Redirected.SubKey, Entry->Redirected.SubKey);
        StringToUnicode64(Entry64->Redirected.ValueName, Entry->Redirected.ValueName);
        if (Entry->Redirected.Data != nullptr && Entry->Redirected.DataSize != 0)
        {
            Entry64->Redirected.Data = (PVOID64)PtrOffset(Buffer, Environment);
            Entry64->Redirected.DataSize = Entry->Redirected.DataSize;
            CopyMemory(Buffer, Entry->Redirected.Data, Entry->Redirected.DataSize);
            Buffer += Entry->Redirected.DataSize;
        }
        ++Entry64;
    }

    WCHAR DirPath[MAX_NTPATH];
    ULONG_PTR FullPathLength = StrLengthW(FullPath);
    if (FullPathLength + 1 > countof(DirPath))
    {
        FreeMemoryP(Environment);
        return STATUS_NAME_TOO_LONG;
    }
    CopyMemory(DirPath, FullPath, (FullPathLength + 1) * sizeof(WCHAR));
    for (ULONG_PTR Index = FullPathLength; Index != 0; --Index)
    {
        if (DirPath[Index - 1] == L'\\')
        {
            DirPath[Index] = 0;
            break;
        }
    }

    ULONG Required = LepBootstrapPayloadSize(EnvironmentSize, FullPath, DirPath);
    if (Required == 0)
    {
        FreeMemoryP(Environment);
        return STATUS_BUFFER_OVERFLOW;
    }
    PLEP_BOOTSTRAP_PAYLOAD LocalPayload = (PLEP_BOOTSTRAP_PAYLOAD)AllocateMemoryP(Required);
    if (LocalPayload == nullptr)
    {
        FreeMemoryP(Environment);
        return STATUS_NO_MEMORY;
    }

    NTSTATUS Status = LepBuildBootstrapPayload(LocalPayload, Required, Environment,
                                               EnvironmentSize, FullPath, DirPath);
    FreeMemoryP(Environment);
    if (NT_FAILED(Status))
    {
        FreeMemoryP(LocalPayload);
        return Status;
    }

    *Payload = LocalPayload;
    *PayloadSize = Required;
    return STATUS_SUCCESS;
}

NTSTATUS LepGlobalData::InjectCrossArchitecture(BOOL CurrentWow64, PCLIENT_ID Cid, BOOL TargetWow64, ULONG InjectionFlags)
{
    ULONG_PTR ConfigSize;
    ULONG PayloadSize;
    PLEP_BOOTSTRAP_PAYLOAD Payload;
    WCHAR MappingName[128], CorePath[MAX_NTPATH], LoaderPath[MAX_NTPATH], WindowsPath[MAX_NTPATH];
    WCHAR RundllPath[MAX_NTPATH], CommandLine[2048];
    HANDLE Mapping;
    PLEP_BROKER_CONFIG Config;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION NativeInfo;
    ULONG ProcessId = (ULONG)(ULONG_PTR)Cid->UniqueProcess;
    DWORD ExitCode, Error;
    BOOL Created;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    Payload = nullptr;
    PayloadSize = 0;
    WriteLog(L"cross-arch begin pid=%u currentWow64=%u targetWow64=%u flags=%08X", ProcessId, CurrentWow64, TargetWow64, InjectionFlags);
    if (!LepBuildArchitecturePath(GetLepDllFullPath(), TargetWow64, CorePath, countof(CorePath)) ||
        !LepBuildLoaderPath(CorePath, TargetWow64, LoaderPath, countof(LoaderPath)))
    {
        WriteLog(L"cross-arch path derivation failed source=%ws", GetLepDllFullPath());
        return STATUS_OBJECT_PATH_INVALID;
    }
    if (GetFileAttributesW(CorePath) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(LoaderPath) == INVALID_FILE_ATTRIBUTES)
    {
        WriteLog(L"cross-arch module missing core=%ws loader=%ws error=%u", CorePath, LoaderPath, GetLastError());
        return STATUS_DLL_NOT_FOUND;
    }

    Status = BuildBootstrapPayload(CorePath, &Payload, &PayloadSize);
    if (NT_FAILED(Status))
    {
        WriteLog(L"cross-arch payload build failed status=%08X", Status);
        return Status;
    }
    ConfigSize = FIELD_OFFSET(LEP_BROKER_CONFIG, Payload) + PayloadSize;
    if (ConfigSize > 0x7FFFFFFF)
    {
        FreeMemoryP(Payload);
        return STATUS_BUFFER_OVERFLOW;
    }

    wsprintfW(MappingName, L"Local\\LEP_BROKER_%u_%u", ProcessId, GetCurrentProcessId());
    DWORD ConfigHigh = 0;
#if ML_AMD64
    ConfigHigh = (DWORD)(ConfigSize >> 32);
#endif
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, ConfigHigh, (DWORD)ConfigSize, MappingName);
    if (Mapping == nullptr)
    {
        Error = GetLastError();
        WriteLog(L"cross-arch CreateFileMapping failed=%u", Error);
        FreeMemoryP(Payload);
        return ML_NTSTATUS_FROM_WIN32(Error);
    }
    Config = (PLEP_BROKER_CONFIG)MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, ConfigSize);
    if (Config == nullptr)
    {
        Error = GetLastError();
        WriteLog(L"cross-arch MapViewOfFile failed=%u", Error);
        CloseHandle(Mapping);
        FreeMemoryP(Payload);
        return ML_NTSTATUS_FROM_WIN32(Error);
    }
    ZeroMemory(Config, ConfigSize);
    Config->Magic = LEP_BROKER_CONFIG_MAGIC;
    Config->Version = LEP_BROKER_CONFIG_VERSION;
    Config->Size = (ULONG)ConfigSize;
    Config->PayloadSize = PayloadSize;
    Config->Result = STATUS_UNSUCCESSFUL;
    Config->ThreadId = (ULONG)(ULONG_PTR)Cid->UniqueThread;
    Config->InjectionFlags = InjectionFlags;
    CopyMemory(Config->Payload, Payload, PayloadSize);
    FreeMemoryP(Payload);
    Payload = nullptr;
    WriteLog(L"cross-arch payload serialized bytes=%u", PayloadSize);
    WriteLog(L"cross-arch mapping ready name=%ws size=%u loader=%ws core=%ws", MappingName,
             (ULONG)ConfigSize, LoaderPath, CorePath);

    if (GetWindowsDirectoryW(WindowsPath, countof(WindowsPath)) == 0)
    {
        Error = GetLastError();
        WriteLog(L"cross-arch GetWindowsDirectory failed=%u", Error);
        UnmapViewOfFile(Config);
        CloseHandle(Mapping);
        return ML_NTSTATUS_FROM_WIN32(Error);
    }
    if (TargetWow64)
        wsprintfW(RundllPath, L"%s\\SysWOW64\\rundll32.exe", WindowsPath);
    else if (CurrentWow64)
        wsprintfW(RundllPath, L"%s\\Sysnative\\rundll32.exe", WindowsPath);
    else
        wsprintfW(RundllPath, L"%s\\System32\\rundll32.exe", WindowsPath);

    wsprintfW(CommandLine, L"\"%s\" \"%s\",LepBrokerEntry %u|%s", RundllPath, LoaderPath, ProcessId, MappingName);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    ZeroMemory(&NativeInfo, sizeof(NativeInfo));
    TEB_ACTIVE_FRAME BrokerLaunchFrame(LEP_BROKER_LAUNCH_CONTEXT);
    BrokerLaunchFrame.Push();
    Created = CreateProcessW(RundllPath, CommandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &StartupInfo, &NativeInfo);
    BrokerLaunchFrame.Pop();
    if (!Created)
    {
        Error = GetLastError();
        WriteLog(L"cross-arch broker CreateProcess failed=%u cmd=%ws", Error, CommandLine);
        UnmapViewOfFile(Config);
        CloseHandle(Mapping);
        return ML_NTSTATUS_FROM_WIN32(Error);
    }
    WriteLog(L"cross-arch broker started pid=%u cmd=%ws", NativeInfo.dwProcessId, CommandLine);
    WaitForSingleObject(NativeInfo.hProcess, INFINITE);
    ExitCode = STATUS_UNSUCCESSFUL;
    GetExitCodeProcess(NativeInfo.hProcess, &ExitCode);
    Status = (NTSTATUS)Config->Result;
    WriteLog(L"cross-arch broker finished pid=%u processExit=%08X result=%08X", NativeInfo.dwProcessId, ExitCode, Status);
    CloseHandle(NativeInfo.hThread);
    CloseHandle(NativeInfo.hProcess);
    UnmapViewOfFile(Config);
    CloseHandle(Mapping);
    return Status;
}

NTSTATUS ReplaceMUIVersionLocaleInfo(PVOID& VersionData, ULONG& VersionSize)
{
    PVOID       TlbBuffer;
    PBYTE       Buffer;
    LONG_PTR    Size;
    PUSHORT     LangID, CharsetID;
    PIMAGE_VERSION_INFO_DATA_ENTRY Entry;

    static WCHAR VarFileInfo[] = L"VarFileInfo";
    static WCHAR Translation[] = L"Translation";

    TlbBuffer = (PBYTE)THREAD_LOCAL_BUFFER::GetTlb(TRUE)->GetBuffer();
    if (TlbBuffer == nullptr)
        return STATUS_NO_MEMORY;

    CopyMemory(TlbBuffer, VersionData, VersionSize);

    Entry = (PIMAGE_VERSION_INFO_DATA_ENTRY)TlbBuffer;
    Size = Entry->Size;

    if (Size < 8 || Size > 0x8000)
        return STATUS_DATA_NOT_ACCEPTED;

    // ansi version
    if (Entry->Version16Name[0] == TAG2('VS'))
        return STATUS_DATA_NOT_ACCEPTED;

    Buffer = (PBYTE)KMP(Entry, Size, VarFileInfo, sizeof(VarFileInfo));
    if (Buffer == nullptr)
        return STATUS_RESOURCE_DATA_NOT_FOUND;

    Size = Entry->Size - PtrOffset(Buffer, Entry) - sizeof(VarFileInfo);
    if (Size <= 0)
        return STATUS_RESOURCE_DATA_NOT_FOUND;

    Buffer += sizeof(VarFileInfo);

    Buffer = (PBYTE)KMP(Buffer, Size, Translation, sizeof(Translation));
    if (Buffer == nullptr)
        return STATUS_RESOURCE_DATA_NOT_FOUND;

    Size = Entry->Size - PtrOffset(Buffer, Entry) - sizeof(Translation);
    if (Size <= 0)
        return STATUS_RESOURCE_DATA_NOT_FOUND;

    Entry = (PIMAGE_VERSION_INFO_DATA_ENTRY)PtrSub(Buffer, 6);

    LangID = (PUSHORT)PtrAdd(Entry, ROUND_DOWN(CONST_STRSIZE(Translation) + 11, 4));
    CharsetID = LangID + 1;

    *LangID = LepGetGlobalData()->GetLepb()->LocaleID;

    VersionData = TlbBuffer;

    return STATUS_SUCCESS;
}

NTSTATUS QueryRegKeyFullPath(HANDLE Key, PUNICODE_STRING KeyFullPath)
{
    NTSTATUS                    Status;
    PKEY_NAME_INFORMATION       KeyFullName;
    ULONG_PTR                   MaxLength;
    ULONG                       ReturnedLength;

    KeyFullName = nullptr;
    MaxLength   = 0;

    LOOP_FOREVER
    {
        Status = NtQueryKey(Key, KeyNameInformation, KeyFullName, MaxLength, &ReturnedLength);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;

        KeyFullName = (PKEY_NAME_INFORMATION)ReAllocateMemoryP(KeyFullName, ReturnedLength + sizeof(WCHAR));
        if (KeyFullName == nullptr)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        MaxLength = ReturnedLength;
    }

    if (NT_SUCCESS(Status))
    {
        PtrAdd(KeyFullName->Name, KeyFullName->NameLength)[0] = 0;
        Status = RtlCreateUnicodeString(KeyFullPath, KeyFullName->Name) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    }

    FreeMemoryP(KeyFullName);
    return Status;
}

static const WCHAR LepMuiLanguagesKeyPath[] =
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\MUI\\UILanguages";
static const WCHAR LepMuiSystemKeyPrefix[] =
    L"\\Registry\\Machine\\System\\";
static const WCHAR LepMuiLanguagesKeySuffix[] =
    L"\\Control\\MUI\\UILanguages";
static const WCHAR LepNlsLanguageKeyPath[] =
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\NLS\\Language";
static const WCHAR LepNlsLanguageKeySuffix[] =
    L"\\Control\\NLS\\Language";

typedef BOOLEAN (NTAPI *PRTL_LCID_TO_CULTURE_NAME)(LCID LocaleId, PUNICODE_STRING CultureName);

static NTSTATUS LepReadHostInstallLanguageFallback(
    PWSTR Buffer,
    ULONG BufferCount,
    PUSHORT BufferLength
)
{
    BYTE ValueBuffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) +
                     LOCALE_NAME_MAX_LENGTH * 4 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION Information =
        (PKEY_VALUE_PARTIAL_INFORMATION)ValueBuffer;
    UNICODE_STRING KeyPath;
    UNICODE_STRING ValueName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle = nullptr;
    ULONG ResultLength = 0;
    NTSTATUS Status;

    if (Buffer == nullptr || BufferLength == nullptr || BufferCount < 2)
        return STATUS_INVALID_PARAMETER;

    Buffer[0] = 0;
    *BufferLength = 0;
    RtlInitUnicodeString(&KeyPath, LepNlsLanguageKeyPath);
    InitializeObjectAttributes(
        &ObjectAttributes,
        &KeyPath,
        OBJ_CASE_INSENSITIVE,
        nullptr,
        nullptr
    );
    Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (NT_FAILED(Status))
        return Status;

    RtlInitUnicodeString(&ValueName, L"InstallLanguageFallback");
    Status = NtQueryValueKey(
        KeyHandle,
        &ValueName,
        KeyValuePartialInformation,
        Information,
        sizeof(ValueBuffer),
        &ResultLength
    );
    NtClose(KeyHandle);
    if (NT_FAILED(Status))
        return Status;
    if (Information->Type != REG_SZ && Information->Type != REG_MULTI_SZ)
        return STATUS_OBJECT_TYPE_MISMATCH;

    ULONG CharacterCount = Information->DataLength / sizeof(WCHAR);
    PWSTR Source = (PWSTR)Information->Data;
    ULONG Length = 0;
    while (Length < CharacterCount && Source[Length] != 0)
        ++Length;
    if (Length + 1 > BufferCount)
        return STATUS_BUFFER_TOO_SMALL;

    CopyMemory(Buffer, Source, Length * sizeof(WCHAR));
    Buffer[Length] = 0;
    *BufferLength = (USHORT)(Length * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static BOOLEAN LepInitializeMuiCultureName(
    PRTL_LCID_TO_CULTURE_NAME LcidToCultureName,
    LCID LocaleId,
    PWSTR Buffer,
    ULONG BufferSize,
    PUSHORT NameLength
)
{
    UNICODE_STRING CultureName;

    CultureName.Buffer = Buffer;
    CultureName.Length = 0;
    CultureName.MaximumLength = (USHORT)BufferSize;
    if (!LcidToCultureName(LocaleId, &CultureName) ||
        CultureName.Length + sizeof(WCHAR) > CultureName.MaximumLength)
    {
        return FALSE;
    }

    CultureName.Buffer[CultureName.Length / sizeof(WCHAR)] = 0;
    *NameLength = CultureName.Length;
    return TRUE;
}

static NTSTATUS LepInitializeVirtualMuiLanguage(PLepGlobalData GlobalData, PVOID Ntdll)
{
    PRTL_LCID_TO_CULTURE_NAME LcidToCultureName;
    LANGID HostLanguageId;
    NTSTATUS Status;
    ULONG_PTR Offset;

    *(PVOID *)&LcidToCultureName = LookupExportTable(Ntdll, NTDLL_RtlLCIDToCultureName);
    if (LcidToCultureName == nullptr)
        return STATUS_PROCEDURE_NOT_FOUND;

    if (!LepInitializeMuiCultureName(
            LcidToCultureName,
            GlobalData->GetLepb()->LocaleID,
            GlobalData->HookRoutineData.Ntdll.MuiLanguageName,
            sizeof(GlobalData->HookRoutineData.Ntdll.MuiLanguageName),
            &GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength
        ))
        return STATUS_NOT_SUPPORTED;

    Status = NtQueryInstallUILanguage(&HostLanguageId);
    FAIL_RETURN(Status);
    if (!LepInitializeMuiCultureName(
            LcidToCultureName,
            HostLanguageId,
            GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName,
            sizeof(GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName),
            &GlobalData->HookRoutineData.Ntdll.MuiHostLanguageNameLength
        ))
        return STATUS_NOT_SUPPORTED;

    // Preserve the host installation's own fallback tail.  The synthetic
    // target is layered above the actually installed UI language so shell
    // components never select resources that are absent from this machine:
    // target -> host install language -> host install fallback.
    Status = LepReadHostInstallLanguageFallback(
        GlobalData->HookRoutineData.Ntdll.MuiHostFallback,
        countof(GlobalData->HookRoutineData.Ntdll.MuiHostFallback),
        &GlobalData->HookRoutineData.Ntdll.MuiHostFallbackLength
    );
    if (NT_FAILED(Status))
    {
        GlobalData->HookRoutineData.Ntdll.MuiHostFallback[0] = 0;
        GlobalData->HookRoutineData.Ntdll.MuiHostFallbackLength = 0;
    }

    Offset = 0;
    ULONG_PTR HostNameCount =
        GlobalData->HookRoutineData.Ntdll.MuiHostLanguageNameLength / sizeof(WCHAR);
    ULONG_PTR HostFallbackCount =
        GlobalData->HookRoutineData.Ntdll.MuiHostFallbackLength / sizeof(WCHAR);
    CopyMemory(
        GlobalData->HookRoutineData.Ntdll.MuiInstallFallback,
        GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName,
        GlobalData->HookRoutineData.Ntdll.MuiHostLanguageNameLength
    );
    Offset += HostNameCount;
    if (HostFallbackCount != 0)
    {
        GlobalData->HookRoutineData.Ntdll.MuiInstallFallback[Offset++] = L',';
        GlobalData->HookRoutineData.Ntdll.MuiInstallFallback[Offset++] = L' ';
        CopyMemory(
            GlobalData->HookRoutineData.Ntdll.MuiInstallFallback + Offset,
            GlobalData->HookRoutineData.Ntdll.MuiHostFallback,
            GlobalData->HookRoutineData.Ntdll.MuiHostFallbackLength
        );
        Offset += HostFallbackCount;
    }
    GlobalData->HookRoutineData.Ntdll.MuiInstallFallback[Offset] = 0;
    GlobalData->HookRoutineData.Ntdll.MuiInstallFallbackLength =
        (USHORT)(Offset * sizeof(WCHAR));

    WriteLog(L"virtual MUI installed language: %ws (%04X), host=%ws (%04X), chain=%ws",
        GlobalData->HookRoutineData.Ntdll.MuiLanguageName,
        GlobalData->GetLepb()->LocaleID,
        GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName,
        HostLanguageId,
        GlobalData->HookRoutineData.Ntdll.MuiInstallFallback);
    return STATUS_SUCCESS;
}

static VOID LepGetVirtualMuiLanguageName(PLepGlobalData GlobalData, PUNICODE_STRING LanguageName)
{
    LanguageName->Buffer = GlobalData->HookRoutineData.Ntdll.MuiLanguageName;
    LanguageName->Length = GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength;
    LanguageName->MaximumLength = sizeof(GlobalData->HookRoutineData.Ntdll.MuiLanguageName);
}

static BOOLEAN LepBuildVirtualMuiKeyPath(
    PLepGlobalData GlobalData,
    PWSTR Buffer,
    ULONG BufferCount,
    PUNICODE_STRING KeyPath
)
{
    ULONG_PTR ParentLength = CONST_STRLEN(LepMuiLanguagesKeyPath);
    ULONG_PTR LanguageLength = GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength / sizeof(WCHAR);

    if (ParentLength + 1 + LanguageLength + 1 > BufferCount)
        return FALSE;

    CopyMemory(Buffer, LepMuiLanguagesKeyPath, ParentLength * sizeof(WCHAR));
    Buffer[ParentLength] = L'\\';
    CopyMemory(
        Buffer + ParentLength + 1,
        GlobalData->HookRoutineData.Ntdll.MuiLanguageName,
        LanguageLength * sizeof(WCHAR)
    );
    Buffer[ParentLength + 1 + LanguageLength] = 0;
    RtlInitUnicodeString(KeyPath, Buffer);
    return TRUE;
}

static BOOLEAN LepIsMuiLanguagesKeyHandle(HANDLE KeyHandle)
{
    UNICODE_STRING KeyPath;
    UNICODE_STRING MuiLanguagesPath;
    UNICODE_STRING SystemKeyPrefix;
    UNICODE_STRING LanguagesKeySuffix;
    NTSTATUS Status = QueryRegKeyFullPath(KeyHandle, &KeyPath);

    if (NT_FAILED(Status))
        return FALSE;

    RtlInitUnicodeString(&MuiLanguagesPath, LepMuiLanguagesKeyPath);
    BOOLEAN Match = RtlEqualUnicodeString(&KeyPath, &MuiLanguagesPath, TRUE);
    if (!Match)
    {
        // NtQueryKey canonicalizes CurrentControlSet to ControlSet00x.  The
        // native MUI loader opens the literal CurrentControlSet path, so both
        // spellings must identify the same parent key.
        RtlInitUnicodeString(&SystemKeyPrefix, LepMuiSystemKeyPrefix);
        RtlInitUnicodeString(&LanguagesKeySuffix, LepMuiLanguagesKeySuffix);
        if (KeyPath.Length >= SystemKeyPrefix.Length + LanguagesKeySuffix.Length)
        {
            UNICODE_STRING Prefix =
            {
                SystemKeyPrefix.Length,
                SystemKeyPrefix.Length,
                KeyPath.Buffer
            };
            UNICODE_STRING Suffix =
            {
                LanguagesKeySuffix.Length,
                LanguagesKeySuffix.Length,
                KeyPath.Buffer + (KeyPath.Length - LanguagesKeySuffix.Length) / sizeof(WCHAR)
            };
            Match = RtlEqualUnicodeString(&Prefix, &SystemKeyPrefix, TRUE) &&
                    RtlEqualUnicodeString(&Suffix, &LanguagesKeySuffix, TRUE);
        }
    }
    RtlFreeUnicodeString(&KeyPath);
    return Match;
}

static BOOLEAN LepIsNlsLanguageKeyHandle(HANDLE KeyHandle)
{
    UNICODE_STRING KeyPath;
    UNICODE_STRING NlsLanguagePath;
    UNICODE_STRING SystemKeyPrefix;
    UNICODE_STRING NlsLanguageSuffix;
    NTSTATUS Status = QueryRegKeyFullPath(KeyHandle, &KeyPath);

    if (NT_FAILED(Status))
        return FALSE;

    RtlInitUnicodeString(&NlsLanguagePath, LepNlsLanguageKeyPath);
    BOOLEAN Match = RtlEqualUnicodeString(&KeyPath, &NlsLanguagePath, TRUE);
    if (!Match)
    {
        RtlInitUnicodeString(&SystemKeyPrefix, LepMuiSystemKeyPrefix);
        RtlInitUnicodeString(&NlsLanguageSuffix, LepNlsLanguageKeySuffix);
        if (KeyPath.Length >= SystemKeyPrefix.Length + NlsLanguageSuffix.Length)
        {
            UNICODE_STRING Prefix =
            {
                SystemKeyPrefix.Length,
                SystemKeyPrefix.Length,
                KeyPath.Buffer
            };
            UNICODE_STRING Suffix =
            {
                NlsLanguageSuffix.Length,
                NlsLanguageSuffix.Length,
                KeyPath.Buffer + (KeyPath.Length - NlsLanguageSuffix.Length) / sizeof(WCHAR)
            };
            Match = RtlEqualUnicodeString(&Prefix, &SystemKeyPrefix, TRUE) &&
                    RtlEqualUnicodeString(&Suffix, &NlsLanguageSuffix, TRUE);
        }
    }
    RtlFreeUnicodeString(&KeyPath);
    return Match;
}

static BOOLEAN LepIsVirtualMuiKeyHandle(PLepGlobalData GlobalData, HANDLE KeyHandle)
{
    if (KeyHandle == nullptr)
        return FALSE;

    for (ULONG_PTR Index = 0; Index != countof(GlobalData->HookRoutineData.Ntdll.VirtualMuiKeyHandles); ++Index)
    {
        PVOID Current = _InterlockedCompareExchangePointer(
            (PVOID volatile *)&GlobalData->HookRoutineData.Ntdll.VirtualMuiKeyHandles[Index],
            nullptr,
            nullptr
        );
        if (Current == KeyHandle)
            return TRUE;
    }

    return FALSE;
}

static BOOLEAN LepAddVirtualMuiKeyHandle(PLepGlobalData GlobalData, HANDLE KeyHandle)
{
    if (KeyHandle == nullptr)
        return FALSE;

    for (ULONG_PTR Index = 0; Index != countof(GlobalData->HookRoutineData.Ntdll.VirtualMuiKeyHandles); ++Index)
    {
        if (_InterlockedCompareExchangePointer(
                (PVOID volatile *)&GlobalData->HookRoutineData.Ntdll.VirtualMuiKeyHandles[Index],
                KeyHandle,
                nullptr
            ) == nullptr)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN LepRemoveVirtualMuiKeyHandle(PLepGlobalData GlobalData, HANDLE KeyHandle)
{
    if (KeyHandle == nullptr)
        return FALSE;

    for (ULONG_PTR Index = 0; Index != countof(GlobalData->HookRoutineData.Ntdll.VirtualMuiKeyHandles); ++Index)
    {
        if (_InterlockedCompareExchangePointer(
                (PVOID volatile *)&GlobalData->HookRoutineData.Ntdll.VirtualMuiKeyHandles[Index],
                nullptr,
                KeyHandle
            ) == KeyHandle)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN LepIsVirtualMuiOpenRequest(
    PLepGlobalData GlobalData,
    POBJECT_ATTRIBUTES ObjectAttributes
)
{
    OBJECT_ATTRIBUTES LocalAttributes;
    UNICODE_STRING ObjectName;
    UNICODE_STRING LanguageName;
    WCHAR FullPathBuffer[256];
    UNICODE_STRING FullPath;

    SEH_TRY
    {
        if (ObjectAttributes == nullptr || ObjectAttributes->ObjectName == nullptr)
            return FALSE;

        LocalAttributes = *ObjectAttributes;
        ObjectName = *ObjectAttributes->ObjectName;
        if (NT_FAILED(RtlValidateUnicodeString(0, &ObjectName)))
            return FALSE;

        LepGetVirtualMuiLanguageName(GlobalData, &LanguageName);
        if (LocalAttributes.RootDirectory != nullptr)
        {
            return RtlEqualUnicodeString(&ObjectName, &LanguageName, TRUE) &&
                   LepIsMuiLanguagesKeyHandle(LocalAttributes.RootDirectory);
        }

        if (!LepBuildVirtualMuiKeyPath(GlobalData, FullPathBuffer, countof(FullPathBuffer), &FullPath))
            return FALSE;

        return RtlEqualUnicodeString(&ObjectName, &FullPath, TRUE);
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
}

static NTSTATUS LepWriteVirtualKeyValue(
    PCUNICODE_STRING ValueName,
    ULONG DataType,
    LPCVOID Data,
    ULONG DataLength,
    KEY_VALUE_INFORMATION_CLASS InformationClass,
    PVOID KeyValueInformation,
    ULONG Length,
    PULONG ResultLength
)
{
    ULONG RequiredLength;
    ULONG MinimumLength;

    switch (InformationClass)
    {
        case KeyValueBasicInformation:
            MinimumLength = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name);
            RequiredLength = MinimumLength + ValueName->Length;
            break;

        case KeyValueFullInformation:
        case KeyValueFullInformationAlign64:
        {
            ULONG Alignment = InformationClass == KeyValueFullInformationAlign64 ? sizeof(ULONG64) : sizeof(ULONG);
            MinimumLength = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name);
            RequiredLength = ROUND_UP(MinimumLength + ValueName->Length, Alignment) + DataLength;
            break;
        }

        case KeyValuePartialInformation:
            MinimumLength = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
            RequiredLength = MinimumLength + DataLength;
            break;

        case KeyValuePartialInformationAlign64:
            MinimumLength = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data);
            RequiredLength = MinimumLength + DataLength;
            break;

        default:
            return STATUS_INVALID_INFO_CLASS;
    }

    if (ResultLength != nullptr)
        *ResultLength = RequiredLength;
    if (Length < MinimumLength)
        return STATUS_BUFFER_TOO_SMALL;
    if (KeyValueInformation == nullptr)
        return STATUS_ACCESS_VIOLATION;
    if (Length < RequiredLength)
        return STATUS_BUFFER_OVERFLOW;

    ZeroMemory(KeyValueInformation, RequiredLength);
    switch (InformationClass)
    {
        case KeyValueBasicInformation:
        {
            PKEY_VALUE_BASIC_INFORMATION Information =
                (PKEY_VALUE_BASIC_INFORMATION)KeyValueInformation;
            Information->Type = DataType;
            Information->NameLength = ValueName->Length;
            CopyMemory(Information->Name, ValueName->Buffer, ValueName->Length);
            break;
        }

        case KeyValueFullInformation:
        case KeyValueFullInformationAlign64:
        {
            PKEY_VALUE_FULL_INFORMATION Information =
                (PKEY_VALUE_FULL_INFORMATION)KeyValueInformation;
            ULONG Alignment = InformationClass == KeyValueFullInformationAlign64 ? sizeof(ULONG64) : sizeof(ULONG);
            Information->Type = DataType;
            Information->NameLength = ValueName->Length;
            Information->DataOffset = ROUND_UP(
                FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name) + ValueName->Length,
                Alignment
            );
            Information->DataLength = DataLength;
            CopyMemory(Information->Name, ValueName->Buffer, ValueName->Length);
            CopyMemory(PtrAdd(Information, Information->DataOffset), Data, DataLength);
            break;
        }

        case KeyValuePartialInformation:
        {
            PKEY_VALUE_PARTIAL_INFORMATION Information =
                (PKEY_VALUE_PARTIAL_INFORMATION)KeyValueInformation;
            Information->Type = DataType;
            Information->DataLength = DataLength;
            CopyMemory(Information->Data, Data, DataLength);
            break;
        }

        case KeyValuePartialInformationAlign64:
        {
            PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64 Information =
                (PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64)KeyValueInformation;
            Information->Type = DataType;
            Information->DataLength = DataLength;
            CopyMemory(Information->Data, Data, DataLength);
            break;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS LepQueryVirtualMuiKeyValue(
    PLepGlobalData GlobalData,
    PUNICODE_STRING ValueName,
    KEY_VALUE_INFORMATION_CLASS InformationClass,
    PVOID KeyValueInformation,
    ULONG Length,
    PULONG ResultLength
)
{
    ULONG LanguageType = 0x92;
    ULONG LocaleId = GlobalData->GetLepb()->LocaleID;
    UNICODE_STRING HostLanguageName;

    HostLanguageName.Buffer = GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName;
    HostLanguageName.Length = GlobalData->HookRoutineData.Ntdll.MuiHostLanguageNameLength;
    HostLanguageName.MaximumLength = sizeof(GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName);

    if (RtlEqualUnicodeString(ValueName, PUSTR(L"Type"), TRUE))
    {
        return LepWriteVirtualKeyValue(
            ValueName, REG_DWORD, &LanguageType, sizeof(LanguageType),
            InformationClass, KeyValueInformation, Length, ResultLength
        );
    }

    if (RtlEqualUnicodeString(ValueName, PUSTR(L"LCID"), TRUE))
    {
        return LepWriteVirtualKeyValue(
            ValueName, REG_DWORD, &LocaleId, sizeof(LocaleId),
            InformationClass, KeyValueInformation, Length, ResultLength
        );
    }

    if (RtlEqualUnicodeString(ValueName, PUSTR(L"DefaultFallback"), TRUE))
    {
        return LepWriteVirtualKeyValue(
            ValueName,
            REG_SZ,
            HostLanguageName.Buffer,
            HostLanguageName.Length + sizeof(WCHAR),
            InformationClass, KeyValueInformation, Length, ResultLength
        );
    }

    if (RtlEqualUnicodeString(ValueName, &HostLanguageName, TRUE))
    {
        return LepWriteVirtualKeyValue(
            ValueName,
            REG_MULTI_SZ,
            GlobalData->HookRoutineData.Ntdll.MuiHostFallback,
            GlobalData->HookRoutineData.Ntdll.MuiHostFallbackLength + 2 * sizeof(WCHAR),
            InformationClass, KeyValueInformation, Length, ResultLength
        );
    }

    if (ResultLength != nullptr)
        *ResultLength = 0;
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static NTSTATUS LepQueryVirtualInstallLanguageFallback(
    PLepGlobalData GlobalData,
    PUNICODE_STRING ValueName,
    KEY_VALUE_INFORMATION_CLASS InformationClass,
    PVOID KeyValueInformation,
    ULONG Length,
    PULONG ResultLength
)
{
    return LepWriteVirtualKeyValue(
        ValueName,
        // RtlpLoadInstallLanguageFallback requires REG_SZ and parses at
        // most two culture names separated by a comma.  REG_MULTI_SZ is
        // rejected before the fallback text is examined.
        REG_SZ,
        GlobalData->HookRoutineData.Ntdll.MuiInstallFallback,
        GlobalData->HookRoutineData.Ntdll.MuiInstallFallbackLength + sizeof(WCHAR),
        InformationClass,
        KeyValueInformation,
        Length,
        ResultLength
    );
}

static PCWSTR LepBootstrapStageName(ULONG Stage)
{
    switch (Stage)
    {
        case LepBootstrapStageValidateMetadata:  return L"validate-metadata";
        case LepBootstrapStageRestoreLdrLoadDll: return L"restore-ldr-load-dll";
        case LepBootstrapStageRestoreProtection: return L"restore-protection";
        case LepBootstrapStageInitialize:        return L"initialize";
        case LepBootstrapStageComplete:          return L"complete";
        default:                                 return L"bootstrap-entry";
    }
}

static VOID LepWriteBootstrapFailureLog(ULONG Stage, NTSTATUS Status, PCUNICODE_STRING ModuleFileName)
{
#if ENABLE_LOG
    WCHAR DosPath[MAX_NTPATH];
    WCHAR NtPath[MAX_NTPATH + 4];
    ULONG_PTR Offset, Length, ProcessId;
    NtFileDisk LogFile;

    PLEP_BOOTSTRAP_PAYLOAD Payload = nullptr;
    if (LepFirstDllBootstrapData.PayloadOffset <= LEP_BOOTSTRAP_IMAGE_MAX_SIZE &&
        LepFirstDllBootstrapData.PayloadSize >= sizeof(LEP_BOOTSTRAP_PAYLOAD) &&
        LepFirstDllBootstrapData.PayloadSize <= LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE)
    {
        Payload = (PLEP_BOOTSTRAP_PAYLOAD)PtrAdd(&__ImageBase, LepFirstDllBootstrapData.PayloadOffset);
    }
    PCWSTR Directory = LepBootstrapDllDirPath(Payload);
    if (Directory == nullptr)
        return;

    Length = ML_MIN(StrLengthW(Directory), countof(DosPath) - 1);
    CopyMemory(DosPath, Directory, Length * sizeof(WCHAR));
    Offset = Length;
    if (!LepAppendLogDirectory(DosPath, countof(DosPath), &Offset))
        return;

    static const WCHAR Prefix[] = L"LEP-bootstrap-";
    Length = ML_MIN(CONST_STRLEN(Prefix), countof(DosPath) - Offset - 1);
    CopyMemory(&DosPath[Offset], Prefix, Length * sizeof(WCHAR));
    Offset += Length;

    ProcessId = CurrentPid();
    for (LONG_PTR Shift = bitsof(ProcessId) - 4; Shift >= 0 && Offset + 1 < countof(DosPath); Shift -= 4)
    {
        ULONG_PTR Digit = (ProcessId >> Shift) & 0xF;
        if (Digit == 0 && Offset != 0 && DosPath[Offset - 1] == L'-' && Shift != 0)
            continue;
        DosPath[Offset++] = (WCHAR)(Digit < 10 ? L'0' + Digit : L'A' + Digit - 10);
    }

    static const WCHAR Suffix[] = L".log.txt";
    Length = ML_MIN(CONST_STRLEN(Suffix), countof(DosPath) - Offset - 1);
    CopyMemory(&DosPath[Offset], Suffix, Length * sizeof(WCHAR));
    Offset += Length;
    DosPath[Offset] = 0;

    static const WCHAR NtPrefix[] = L"\\??\\";
    CopyMemory(NtPath, NtPrefix, sizeof(NtPrefix) - sizeof(WCHAR));
    Length = ML_MIN((Offset + 1) * sizeof(WCHAR), sizeof(NtPath) - sizeof(NtPrefix) + sizeof(WCHAR));
    CopyMemory(&NtPath[countof(NtPrefix) - 1], DosPath, Length);

    NTSTATUS LogStatus = LogFile.Create(
        NtPath,
        NFD_NOT_RESOLVE_PATH,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        GENERIC_WRITE,
        FILE_OVERWRITE_IF,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SYNCHRONOUS_IO_NONALERT
    );
    if (NT_FAILED(LogStatus))
        return;

    ULONG Bom = BOM_UTF16_LE;
    LogFile.Write(&Bom, 2);
    LogFile.Print(nullptr, L"pid=%u stage=%ws status=%08X\r\n", (ULONG)ProcessId,
                  LepBootstrapStageName(Stage), Status);
    if (ModuleFileName != nullptr && ModuleFileName->Buffer != nullptr)
    {
        LogFile.Write((PVOID)L"module=", 7 * sizeof(WCHAR));
        LogFile.Write(ModuleFileName->Buffer, ModuleFileName->Length);
        LogFile.Write((PVOID)L"\r\n", 2 * sizeof(WCHAR));
    }
#else
    UNREFERENCED_PARAMETER(Stage);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(ModuleFileName);
#endif
}

NoInline PVOID FASTCALL LoadSelfAsFirstDll(PVOID ReturnAddress, NTSTATUS* BootstrapStatus, PULONG BootstrapStage)
{
    PVOID BaseToFree;
    PLEP_BOOTSTRAP_PAYLOAD Payload;

    BaseToFree = nullptr;
    Payload = nullptr;
    *BootstrapStage = LepBootstrapStageValidateMetadata;

    if (LepFirstDllBootstrapData.Magic != LEP_FIRST_DLL_BOOTSTRAP_MAGIC ||
        LepFirstDllBootstrapData.LdrLoadDllAddress == nullptr ||
        LepFirstDllBootstrapData.BackupSize == 0 ||
        LepFirstDllBootstrapData.BackupSize > sizeof(LepFirstDllBootstrapData.Backup) ||
        LepFirstDllBootstrapData.PayloadOffset == 0 ||
        LepFirstDllBootstrapData.PayloadOffset > LEP_BOOTSTRAP_IMAGE_MAX_SIZE ||
        LepFirstDllBootstrapData.PayloadSize < sizeof(LEP_BOOTSTRAP_PAYLOAD) ||
        LepFirstDllBootstrapData.PayloadSize > LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE)
    {
        *BootstrapStatus = STATUS_INVALID_PARAMETER;
        return nullptr;
    }

    *BootstrapStage = LepBootstrapStageRestoreLdrLoadDll;
    // The parent leaves this page writable. Restore it with ordinary CPU
    // stores before parsing the variable-length payload or initializing LEP.
    for (ULONG Index = 0; Index != LepFirstDllBootstrapData.BackupSize; ++Index)
    {
        ((volatile BYTE*)LepFirstDllBootstrapData.LdrLoadDllAddress)[Index] =
            LepFirstDllBootstrapData.Backup[Index];
    }

    ULONG IgnoredProtect;
    *BootstrapStage = LepBootstrapStageRestoreProtection;
    *BootstrapStatus = ProtectVirtualMemory(
        LepFirstDllBootstrapData.LdrLoadDllAddress,
        LepFirstDllBootstrapData.BackupSize,
        LepFirstDllBootstrapData.OriginalProtect,
        &IgnoredProtect,
        CurrentProcess
    );
    if (NT_FAILED(*BootstrapStatus) ||
        (LepFirstDllBootstrapData.InjectionFlags & LEP_INJECT_RESTORE_ONLY) != 0)
    {
        return nullptr;
    }

#if ML_AMD64 && defined(LEP_X64_ATTACH_SPIN)
    while (!CurrentPeb()->BeingDebugged)
    {
        LARGE_INTEGER Interval;
        Interval.QuadPart = -1000 * 1000;
        NtDelayExecution(FALSE, &Interval);
    }
#endif

    *BootstrapStage = LepBootstrapStageValidateMetadata;
    Payload = (PLEP_BOOTSTRAP_PAYLOAD)PtrAdd(&__ImageBase, LepFirstDllBootstrapData.PayloadOffset);
    if (LepFirstDllBootstrapData.PayloadOffset != ROUND_UP(ImageGetSizeOfImage(&__ImageBase), 16) ||
        !LepValidateBootstrapPayload(Payload) ||
        Payload->TotalSize != LepFirstDllBootstrapData.PayloadSize)
    {
        *BootstrapStatus = STATUS_INVALID_PARAMETER;
        return nullptr;
    }

    *BootstrapStage = LepBootstrapStageInitialize;
    if (!Initialize(&__ImageBase, Payload))
    {
        *BootstrapStatus = STATUS_DLL_INIT_FAILED;
        return nullptr;
    }

    *BootstrapStage = LepBootstrapStageComplete;

    return BaseToFree;
}

EXTC
NTSTATUS
NTAPI
LoadFirstDll(
    PWCHAR              PathToFile OPTIONAL,
    PULONG              DllCharacteristics OPTIONAL,
    PCUNICODE_STRING    ModuleFileName,
    PVOID*              DllHandle
)
{
    PVOID BaseToFree;

    NTSTATUS BootstrapStatus = STATUS_SUCCESS;
    ULONG BootstrapStage = LepBootstrapStageNone;

    WriteLog(L"LoadFirstDll module=%ws return=%p", ModuleFileName != nullptr ? ModuleFileName->Buffer : nullptr, _ReturnAddress());

    BaseToFree = LoadSelfAsFirstDll(_ReturnAddress(), &BootstrapStatus, &BootstrapStage);
    if (NT_FAILED(BootstrapStatus))
    {
        LepWriteBootstrapFailureLog(BootstrapStage, BootstrapStatus, ModuleFileName);
        return BootstrapStatus;
    }
    // if (BaseToFree != nullptr)
        // Mm::FreeVirtualMemory(BaseToFree);

    return LdrLoadDll(PathToFile, DllCharacteristics, ModuleFileName, DllHandle);
}

NTSTATUS
HPCALL
LepNtQuerySystemInformation(
    HPARGS
    SYSTEM_INFORMATION_CLASS    SystemInformationClass,
    PVOID                       SystemInformation,
    ULONG_PTR                   SystemInformationLength,
    PULONG                      ReturnLength OPTIONAL
)
{
    NTSTATUS Status = 0;
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();

    switch (SystemInformationClass)
    {
        case SystemCurrentTimeZoneInformation:
            if (SystemInformation == nullptr)
                break;

            if (SystemInformationLength < sizeof(GlobalData->GetLepb()->Timezone))
                break;

            *((PRTL_TIME_ZONE_INFORMATION)SystemInformation) = GlobalData->GetLepb()->Timezone;

            if (ReturnLength != nullptr)
                *ReturnLength = sizeof(GlobalData->GetLepb()->Timezone);

            HpSetFilterAction(BlockSystemCall);

            return STATUS_SUCCESS;
    }

    return Status;
}

NTSTATUS
HPCALL
LepNtQueryInformationThread(
    HPARGS
    HANDLE          ThreadHandle,
    THREADINFOCLASS ThreadInformationClass,
    PVOID           ThreadInformation,
    ULONG           ThreadInformationLength,
    PULONG          ReturnLength
)
{
    NTSTATUS Status;

    switch (ThreadInformationClass)
    {
        case ThreadTimes:
            break;

        default:
            return 0;
    }

    HpSetFilterAction(BlockSystemCall);

    Status = HpCallSysCall(NtQueryInformationThread, ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength, ReturnLength);
    FAIL_RETURN(Status);

    PKERNEL_USER_TIMES Times;

    Times = (PKERNEL_USER_TIMES)ThreadInformation;

    //Times->CreateTime.QuadPart = 0;
    Times->KernelTime.QuadPart = 0x26161;
    Times->UserTime.QuadPart = 0x26161;

    AllocConsole();
    PrintConsoleW(L"%I64X, %I64X\n", Times->KernelTime, Times->UserTime);

    return Status;
}

NTSTATUS LepGlobalData::InjectSelfToChildProcess(HANDLE Process, PCLIENT_ID Cid, ULONG InjectionFlags)
{
    if (FindThreadFrame(LEP_BROKER_LAUNCH_CONTEXT) != nullptr)
    {
        WriteLog(L"skip child injection while broker launch is active");
        return STATUS_SUCCESS;
    }

    BOOL TargetWow64 = Ps::IsWow64Process(Process);
    if (this->Wow64 != TargetWow64)
        return this->InjectCrossArchitecture(this->Wow64, Cid, TargetWow64, InjectionFlags);

    NTSTATUS Status = STATUS_SUCCESS;
    ULONG StartTime = LepCurrentTimeMs();
    PVOID SelfShadow = nullptr;
    PVOID LocalSelfShadow = nullptr;
    PLEP_BOOTSTRAP_PAYLOAD Payload = nullptr;
    ULONG PayloadSize = 0;
    ULONG_PTR ImageSize = 0;
    ULONG_PTR PayloadOffset = 0;
    PVOID LdrLoadDllAddress = GetRuntimeState()->LdrLoadDllAddress;
    ULONG LdrLoadDllProtect = 0;
    BOOL LdrLoadDllWritable = FALSE;
    BYTE Backup[LDR_LOAD_DLL_BACKUP_SIZE];

    WriteLog(L"child injection begin pid=%u tid=%u currentWow64=%u targetWow64=%u flags=%08X",
             (ULONG)(ULONG_PTR)Cid->UniqueProcess, (ULONG)(ULONG_PTR)Cid->UniqueThread,
             this->Wow64, TargetWow64, InjectionFlags);

    if ((InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) != 0)
    {
        Status = ReadMemory(Process, LdrLoadDllAddress, Backup, sizeof(Backup));
        if (NT_FAILED(Status))
            goto INJECT_FINISH_NEW;
        Status = ProtectVirtualMemory(LdrLoadDllAddress, sizeof(Backup), PAGE_EXECUTE_READWRITE,
                                      &LdrLoadDllProtect, Process);
        if (NT_FAILED(Status))
            goto INJECT_FINISH_NEW;
        LdrLoadDllWritable = TRUE;
    }

    if ((InjectionFlags & LEP_INJECT_WRITE_SHADOW) != 0)
    {
        Status = BuildBootstrapPayload(GetLepDllFullPath(), &Payload, &PayloadSize);
        if (NT_FAILED(Status))
            goto INJECT_FINISH_NEW;

        Status = LoadPeImage(GetLepDllFullPath(), &LocalSelfShadow, nullptr, 0);
        if (NT_FAILED(Status))
            goto INJECT_FINISH_NEW;

        // The shadow contents come from the DLL currently on disk.  Derive the
        // allocation and copy size from that same image: the injecting process
        // may still have an older build loaded after an in-place deployment.
        ImageSize = ImageGetSizeOfImage(LocalSelfShadow);
        if (ImageSize > LEP_BOOTSTRAP_IMAGE_MAX_SIZE)
        {
            Status = STATUS_BUFFER_OVERFLOW;
            goto INJECT_FINISH_NEW;
        }
        PayloadOffset = ROUND_UP(ImageSize, 16);
        Status = AllocVirtualMemoryEx(Process, &SelfShadow, PayloadOffset + PayloadSize);
        if (NT_FAILED(Status))
            goto INJECT_FINISH_NEW;

        RelocPeImage(LocalSelfShadow, LocalSelfShadow, nullptr, SelfShadow);

        PLEP_FIRST_DLL_BOOTSTRAP_DATA BootstrapData = (PLEP_FIRST_DLL_BOOTSTRAP_DATA)
            LookupExportTable(LocalSelfShadow, LEP_FIRST_DLL_BOOTSTRAP_EXPORT);
        if (BootstrapData == nullptr)
        {
            Status = STATUS_ENTRYPOINT_NOT_FOUND;
            goto INJECT_FINISH_NEW;
        }
        ZeroMemory(BootstrapData, sizeof(*BootstrapData));
        BootstrapData->Magic = LEP_FIRST_DLL_BOOTSTRAP_MAGIC;
        BootstrapData->InjectionFlags = InjectionFlags;
        BootstrapData->PayloadOffset = (ULONG)PayloadOffset;
        BootstrapData->PayloadSize = PayloadSize;
        if ((InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) != 0)
        {
            BootstrapData->BackupSize = sizeof(Backup);
            BootstrapData->OriginalProtect = LdrLoadDllProtect;
            BootstrapData->LdrLoadDllAddress = LdrLoadDllAddress;
            CopyMemory(BootstrapData->Backup, Backup, sizeof(Backup));
        }

        Status = WriteMemory(Process, SelfShadow, LocalSelfShadow, ImageSize);
        if (NT_SUCCESS(Status))
            Status = WriteMemory(Process, PtrAdd(SelfShadow, PayloadOffset), Payload, PayloadSize);
        if (NT_FAILED(Status))
            goto INJECT_FINISH_NEW;
    }

    if ((InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) != 0)
    {
        if (SelfShadow == nullptr)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto INJECT_FINISH_NEW;
        }

        PVOID LocalLoadFirstDll = LookupExportTable(LocalSelfShadow, "LoadFirstDll");
        if (LocalLoadFirstDll == nullptr)
        {
            Status = STATUS_ENTRYPOINT_NOT_FOUND;
            goto INJECT_FINISH_NEW;
        }
        PVOID ShadowLoadFirstDll = PtrAdd(SelfShadow, PtrOffset(LocalLoadFirstDll, LocalSelfShadow));
        BYTE Jump[16] = {};
#if ML_AMD64
        Jump[0] = 0xFF;
        Jump[1] = 0x25;
        *(PVOID*)&Jump[6] = ShadowLoadFirstDll;
#else
        Jump[0] = JUMP;
        *(PULONG)&Jump[1] = PtrOffset(ShadowLoadFirstDll, PtrAdd(LdrLoadDllAddress, 5));
#endif
        Status = WriteMemory(Process, LdrLoadDllAddress, Jump, sizeof(Backup));
    }

INJECT_FINISH_NEW:
    if (Payload != nullptr)
        FreeMemoryP(Payload);
    if (LocalSelfShadow != nullptr)
        UnloadPeImage(LocalSelfShadow);
    if (NT_FAILED(Status) && LdrLoadDllWritable)
    {
        ULONG IgnoredProtect;
        ProtectVirtualMemory(LdrLoadDllAddress, sizeof(Backup), LdrLoadDllProtect,
                             &IgnoredProtect, Process);
    }
    if (NT_FAILED(Status) && SelfShadow != nullptr)
        Mm::FreeVirtualMemory(SelfShadow, Process);

    WriteLog(L"child injection complete pid=%u status=%p payload=%u elapsed=%u",
             (ULONG)(ULONG_PTR)Cid->UniqueProcess, Status, PayloadSize,
             LepCurrentTimeMs() - StartTime);
    return Status;
}


NTSTATUS
HPCALL
LepNtCreateUserProcess(
    HPARGS
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
    NTSTATUS            Status, Status2;
    PLepGlobalData       GlobalData;
    PPS_ATTRIBUTE_LIST  LocalAttributeList;
    PPS_ATTRIBUTE       Attribute;
    CLIENT_ID           LocalCid, *Cid;
    ULONG_PTR           ReturnLength;
    LONG                Offset;
    PVOID               SelfShadow;
    BOOL                ResumeAfterInjection;
    HpSetFilterAction(BlockSystemCall);
    WriteLog(L"create proc");
    WriteLog(L"create request creatorPid=%u creatorTid=%u processOnly=%u processFlags=%08X originalThreadFlags=%08X",
             (ULONG)CurrentPid(), (ULONG)CurrentTid(), (ULONG)LEP_DIAG_PROCESS_ONLY, ProcessFlags, ThreadFlags);
    if (ProcessParameters != nullptr)
    {
        LepLogUnicodeString(L"create image:", ProcessParameters->ImagePathName);
        LepLogUnicodeString(L"create commandLine:", ProcessParameters->CommandLine);
    }
    ResumeAfterInjection = FLAG_OFF(ThreadFlags, THREAD_CREATE_FLAGS_CREATE_SUSPENDED);
    ThreadFlags |= THREAD_CREATE_FLAGS_CREATE_SUSPENDED;

    GlobalData = (PLepGlobalData)HpGetFilterContext();

    LocalAttributeList = nullptr;
    Attribute = nullptr;
    Cid = nullptr;

    if (AttributeList != nullptr)
    {
        ULONG_PTR Count;

        Attribute = AttributeList->Attributes;
        Count = (PPS_ATTRIBUTE)PtrAdd(AttributeList, AttributeList->TotalLength) - Attribute;
        FOR_EACH(Attribute, AttributeList->Attributes, Count)
        {
            if (Attribute->AttributeNumber != PsAttributeClientId)
                continue;

//            if (Attribute->AttributeFlags != PS_ATTRIBUTE_FLAG_THREAD)
//                break;

            Cid = (PCLIENT_ID)Attribute->ValuePtr;
            break;
        }
    }

    if (Cid == nullptr)
    {
        ULONG_PTR ListLength;

        ListLength = (AttributeList != nullptr ? AttributeList->TotalLength : sizeof(AttributeList->TotalLength)) + sizeof(*Attribute);
        LocalAttributeList = (PPS_ATTRIBUTE_LIST)AllocStack(ListLength);

        if (AttributeList != nullptr)
            CopyMemory(LocalAttributeList, AttributeList, AttributeList->TotalLength);

        LocalAttributeList->TotalLength = ListLength;
        Attribute = (PPS_ATTRIBUTE)PtrAdd(LocalAttributeList, LocalAttributeList->TotalLength) - 1;

        Attribute->AttributeNumber  = PsAttributeClientId;
        Attribute->AttributeFlags   = PS_ATTRIBUTE_FLAG_THREAD;
        Attribute->ValuePtr         = &LocalCid;
        Attribute->Size             = sizeof(LocalCid);
        Attribute->ReturnLength     = &ReturnLength;

        Cid = &LocalCid;

        AttributeList = LocalAttributeList;
        WriteLog(L"create proc ClientId attribute appended listLength=%p", ListLength);
    }

    Status = HpCallSysCall(
                NtCreateUserProcess,
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

    WriteLog(L"create proc syscall status=%p effectiveThreadFlags=%08X", Status, ThreadFlags);

    if (NT_FAILED(Status))
        return Status;

    WriteLog(L"create proc returned pid=%u tid=%u originalSuspended=%u",
             Cid != nullptr ? (ULONG)(ULONG_PTR)Cid->UniqueProcess : 0,
             Cid != nullptr ? (ULONG)(ULONG_PTR)Cid->UniqueThread : 0,
             !ResumeAfterInjection);

    BOOL InjectionSkipped = FALSE;
    ULONG ChildInjectionFlags = LEP_INJECT_FULL;
#if LEP_DIAG_SKIP_CHAT_CHILD_INJECTION || LEP_DIAG_CHAT_CHILD_RESTORE_ONLY || LEP_DIAG_CHAT_CHILD_NO_LDR_PATCH
    PLDR_MODULE CreatorModule = FindLdrModuleByHandle(nullptr);
    BOOL IsChatCreator = CreatorModule != nullptr &&
        RtlEqualUnicodeString(&CreatorModule->BaseDllName, &USTR(L"QQSpeedChatBrowser.exe"), TRUE);
#if LEP_DIAG_SKIP_CHAT_CHILD_INJECTION
    InjectionSkipped = IsChatCreator;
#elif LEP_DIAG_CHAT_CHILD_NO_LDR_PATCH
    if (IsChatCreator)
    {
        ChildInjectionFlags = LEP_INJECT_WRITE_SHADOW;
        WriteLog(L"diagnostic chat child no-Ldr-patch: creatorPid=%u childPid=%u flags=%08X",
                 (ULONG)CurrentPid(), (ULONG)(ULONG_PTR)Cid->UniqueProcess, ChildInjectionFlags);
    }
#else
    if (IsChatCreator)
    {
        ChildInjectionFlags |= LEP_INJECT_RESTORE_ONLY;
        WriteLog(L"diagnostic chat child restore-only: creatorPid=%u childPid=%u flags=%08X",
                 (ULONG)CurrentPid(), (ULONG)(ULONG_PTR)Cid->UniqueProcess, ChildInjectionFlags);
    }
#endif
#endif
    if (InjectionSkipped)
    {
        Status2 = STATUS_SUCCESS;
        WriteLog(L"diagnostic skip child injection: chat boundary creatorPid=%u childPid=%u childTid=%u",
                 (ULONG)CurrentPid(), (ULONG)(ULONG_PTR)Cid->UniqueProcess,
                 (ULONG)(ULONG_PTR)Cid->UniqueThread);
    }
    else
    {
        Status2 = GlobalData->InjectSelfToChildProcess(*ProcessHandle, Cid, ChildInjectionFlags);
    }

    WriteLog(L"inject %p", Status2);
    WriteLog(L"create edge creatorPid=%u creatorTid=%u childPid=%u childTid=%u createStatus=%08X injectStatus=%08X originalSuspended=%u injectionSkipped=%u",
             (ULONG)CurrentPid(), (ULONG)CurrentTid(),
             (ULONG)(ULONG_PTR)Cid->UniqueProcess, (ULONG)(ULONG_PTR)Cid->UniqueThread,
             Status, Status2, !ResumeAfterInjection, InjectionSkipped);

    if (ResumeAfterInjection)
    {
        NTSTATUS ResumeStatus = NtResumeThread(*ThreadHandle, nullptr);
        WriteLog(L"resume child status=%p pid=%u tid=%u", ResumeStatus,
                 (ULONG)(ULONG_PTR)Cid->UniqueProcess, (ULONG)(ULONG_PTR)Cid->UniqueThread);
    }
    else
    {
        WriteLog(L"resume child deferred to creator pid=%u tid=%u",
                 Cid != nullptr ? (ULONG)(ULONG_PTR)Cid->UniqueProcess : 0,
                 Cid != nullptr ? (ULONG)(ULONG_PTR)Cid->UniqueThread : 0);
    }

    return Status;
}

NTSTATUS
HPCALL
LepNtInitializeNlsFiles(
    HPARGS
    PVOID*          BaseAddress,
    PLCID           DefaultLocaleId,
    PLARGE_INTEGER  DefaultCasingTableSize
)
{
    NTSTATUS Status;
    PLepGlobalData GlobalData;

    HpSetFilterAction(BlockSystemCall);

    Status = HpCallSysCall(
                NtInitializeNlsFiles,
                BaseAddress,
                DefaultLocaleId,
                DefaultCasingTableSize
            );

    FAIL_RETURN(Status);

    GlobalData = (PLepGlobalData)HpGetFilterContext();

    *DefaultLocaleId = GlobalData->GetLepb()->LocaleID;

    return Status;
}

NTSTATUS
HPCALL
LepNtTerminateThread(
    HPARGS
    HANDLE      ThreadHandle,
    NTSTATUS    ExitStatus
)
{
    if (ThreadHandle == CurrentThread || ThreadHandle == nullptr)
    {
        THREAD_LOCAL_BUFFER::ReleaseTlb();
    }

    return 0;
}

NTSTATUS
HPCALL
LepNtClose(
    HPARGS
    HANDLE Handle
)
{
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();

    if (LepRemoveVirtualMuiKeyHandle(GlobalData, Handle))
        WriteLog(L"virtual MUI key close: %p", Handle);

    return STATUS_SUCCESS;
}

NTSTATUS
HPCALL
LepNtOpenKey(
    HPARGS
    PHANDLE KeyHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
)
{
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();
    UNICODE_STRING ParentPath;
    OBJECT_ATTRIBUTES ParentAttributes;
    NTSTATUS Status;

    if (!LepIsVirtualMuiOpenRequest(GlobalData, ObjectAttributes))
        return STATUS_SUCCESS;

    HpSetFilterAction(BlockSystemCall);
    RtlInitUnicodeString(&ParentPath, LepMuiLanguagesKeyPath);
    InitializeObjectAttributes(
        &ParentAttributes,
        &ParentPath,
        OBJ_CASE_INSENSITIVE,
        nullptr,
        nullptr
    );
    Status = HpCallSysCall(NtOpenKey, KeyHandle, DesiredAccess, &ParentAttributes);
    if (NT_FAILED(Status))
        return Status;

    if (!LepAddVirtualMuiKeyHandle(GlobalData, *KeyHandle))
    {
        NtClose(*KeyHandle);
        *KeyHandle = nullptr;
        return STATUS_NO_MEMORY;
    }

    WriteLog(L"virtual MUI key open: %ws handle=%p",
        GlobalData->HookRoutineData.Ntdll.MuiLanguageName, *KeyHandle);
    return STATUS_SUCCESS;
}

NTSTATUS
HPCALL
LepNtEnumerateKey(
    HPARGS
    HANDLE KeyHandle,
    ULONG Index,
    KEY_INFORMATION_CLASS KeyInformationClass,
    PVOID KeyInformation,
    ULONG Length,
    PULONG ResultLength
)
{
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();
    ULONG NameLength;
    ULONG RequiredLength;
    ULONG MinimumLength;

    if (!LepIsMuiLanguagesKeyHandle(KeyHandle))
        return STATUS_SUCCESS;

    HpSetFilterAction(BlockSystemCall);
    if (Index != 0)
    {
        // Present the synthetic target language in addition to the languages
        // installed on the host.  Hiding the host entries leaves fallback
        // names (for example ja-JP -> zh-CN -> en-US) dangling in ntdll's MUI
        // cache and can make activation-context resource lookup fail for every
        // system module.
        NTSTATUS Status = HpCallSysCall(
            NtEnumerateKey,
            KeyHandle,
            Index - 1,
            KeyInformationClass,
            KeyInformation,
            Length,
            ResultLength
        );
        WriteLog(L"virtual MUI host language enumerate: virtual-index=%u host-index=%u status=%08X",
            Index, Index - 1, Status);
        return Status;
    }
    if (KeyInformationClass != KeyBasicInformation)
        return STATUS_INVALID_INFO_CLASS;

    NameLength = GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength;
    MinimumLength = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name);
    RequiredLength = MinimumLength + NameLength;
    if (ResultLength != nullptr)
        *ResultLength = RequiredLength;
    if (Length < MinimumLength)
        return STATUS_BUFFER_TOO_SMALL;
    if (KeyInformation == nullptr)
        return STATUS_ACCESS_VIOLATION;
    if (Length < RequiredLength)
        return STATUS_BUFFER_OVERFLOW;

    PKEY_BASIC_INFORMATION Information = (PKEY_BASIC_INFORMATION)KeyInformation;
    ZeroMemory(Information, RequiredLength);
    Information->NameLength = NameLength;
    CopyMemory(
        Information->Name,
        GlobalData->HookRoutineData.Ntdll.MuiLanguageName,
        NameLength
    );
    WriteLog(L"virtual MUI language enumerate: index=%u name=%ws",
        Index, GlobalData->HookRoutineData.Ntdll.MuiLanguageName);
    return STATUS_SUCCESS;
}

NTSTATUS
HPCALL
LepNtQueryKey(
    HPARGS
    HANDLE KeyHandle,
    KEY_INFORMATION_CLASS KeyInformationClass,
    PVOID KeyInformation,
    ULONG Length,
    PULONG ResultLength
)
{
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();
    WCHAR FullPathBuffer[256];
    UNICODE_STRING FullPath;
    ULONG RequiredLength;
    ULONG MinimumLength;

    if (!LepIsVirtualMuiKeyHandle(GlobalData, KeyHandle))
        return STATUS_SUCCESS;

    HpSetFilterAction(BlockSystemCall);
    if (!LepBuildVirtualMuiKeyPath(GlobalData, FullPathBuffer, countof(FullPathBuffer), &FullPath))
        return STATUS_NAME_TOO_LONG;

    if (KeyInformationClass == KeyNameInformation)
    {
        MinimumLength = FIELD_OFFSET(KEY_NAME_INFORMATION, Name);
        RequiredLength = MinimumLength + FullPath.Length;
        if (ResultLength != nullptr)
            *ResultLength = RequiredLength;
        if (Length < MinimumLength)
            return STATUS_BUFFER_TOO_SMALL;
        if (KeyInformation == nullptr)
            return STATUS_ACCESS_VIOLATION;
        if (Length < RequiredLength)
            return STATUS_BUFFER_OVERFLOW;

        PKEY_NAME_INFORMATION Information = (PKEY_NAME_INFORMATION)KeyInformation;
        Information->NameLength = FullPath.Length;
        CopyMemory(Information->Name, FullPath.Buffer, FullPath.Length);
        return STATUS_SUCCESS;
    }

    if (KeyInformationClass == KeyBasicInformation)
    {
        ULONG NameLength = GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength;
        MinimumLength = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name);
        RequiredLength = MinimumLength + NameLength;
        if (ResultLength != nullptr)
            *ResultLength = RequiredLength;
        if (Length < MinimumLength)
            return STATUS_BUFFER_TOO_SMALL;
        if (KeyInformation == nullptr)
            return STATUS_ACCESS_VIOLATION;
        if (Length < RequiredLength)
            return STATUS_BUFFER_OVERFLOW;

        PKEY_BASIC_INFORMATION Information = (PKEY_BASIC_INFORMATION)KeyInformation;
        ZeroMemory(Information, RequiredLength);
        Information->NameLength = NameLength;
        CopyMemory(
            Information->Name,
            GlobalData->HookRoutineData.Ntdll.MuiLanguageName,
            NameLength
        );
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_INFO_CLASS;
}

NTSTATUS
HPCALL
LepNtEnumerateValueKey(
    HPARGS
    HANDLE                      KeyHandle,
    ULONG                       Index,
    KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    PVOID                       KeyValueInformation,
    ULONG                       Length,
    PULONG                      ResultLength
)
{
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    if (!LepIsVirtualMuiKeyHandle(GlobalData, KeyHandle))
        return STATUS_SUCCESS;

    HpSetFilterAction(BlockSystemCall);
    switch (Index)
    {
        case 0:
            RtlInitUnicodeString(&ValueName, L"DefaultFallback");
            break;

        case 1:
            ValueName.Buffer = GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName;
            ValueName.Length = GlobalData->HookRoutineData.Ntdll.MuiHostLanguageNameLength;
            ValueName.MaximumLength = sizeof(GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName);
            break;

        case 2:
            RtlInitUnicodeString(&ValueName, L"LCID");
            break;

        case 3:
            RtlInitUnicodeString(&ValueName, L"Type");
            break;

        default:
            if (ResultLength != nullptr)
                *ResultLength = 0;
            return STATUS_NO_MORE_ENTRIES;
    }

    Status = LepQueryVirtualMuiKeyValue(
        GlobalData,
        &ValueName,
        KeyValueInformationClass,
        KeyValueInformation,
        Length,
        ResultLength
    );
    WriteLog(L"virtual MUI value enumerate: index=%u name=%ws status=%08X",
        Index, ValueName.Buffer, Status);
    return Status;
}

NTSTATUS
LepGlobalData::
LookupRegistryRedirectionEntry(
    HANDLE                          KeyHandle,
    PUNICODE_STRING                 ValueName,
    PREGISTRY_REDIRECTION_ENTRY*    RedirectionEntry
)
{
    NTSTATUS                    Status;
    PREGISTRY_REDIRECTION_ENTRY Entry;
    UNICODE_STRING              KeyFullPath;

    // RtlAllocateHeap dead lock
    if (RtlEqualUnicodeString(ValueName, PUSTR(L"ResourcePolicies"), FALSE))
        return STATUS_NOT_FOUND;

    Status = QueryRegKeyFullPath(KeyHandle, &KeyFullPath);
    FAIL_RETURN(Status);

    Status = STATUS_NOT_FOUND;

    //ml::String KeyFullPathString = KeyFullPath;
    ml::String ValueNameString;

    if (ValueName != nullptr)
        ValueNameString = *ValueName;

    FOR_EACH_VEC(Entry, this->RegistryRedirectionEntry)
    {
        if (ValueName != nullptr)
        {
            if (ValueNameString.MatchExpression(Entry->Original.ValueName, TRUE) == FALSE)
                continue;
        }

        if (RtlEqualUnicodeString(&KeyFullPath, Entry->Original.FullPath, TRUE) == FALSE)
            continue;

        //if (KeyFullPathString.MatchExpression(Entry->Original.FullPath, TRUE) == FALSE)
        //    continue;

        *RedirectionEntry = Entry;
        Status = STATUS_SUCCESS;
        break;
    }

    RtlFreeUnicodeString(&KeyFullPath);

    return Status;
}

NTSTATUS
HPCALL
LepNtQueryValueKey(
    HPARGS
    HANDLE                      KeyHandle,
    PUNICODE_STRING             ValueName,
    KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    PVOID                       KeyValueInformation,
    ULONG                       Length,
    PULONG                      ResultLength
)
{
    NTSTATUS                    Status;
    ULONG_PTR                   BufferLength;
    PLepGlobalData               GlobalData;
    OBJECT_ATTRIBUTES           ObjectAttributes;
    PREGISTRY_REDIRECTION_ENTRY Entry;

    if (NT_FAILED(RtlValidateUnicodeString(0, ValueName)))
        return 0;

    GlobalData = (PLepGlobalData)HpGetFilterContext();

    if (GlobalData->GetLepb()->HookUILanguageApi == 2 &&
        LepIsVirtualMuiKeyHandle(GlobalData, KeyHandle))
    {
        HpSetFilterAction(BlockSystemCall);
        Status = LepQueryVirtualMuiKeyValue(
            GlobalData,
            ValueName,
            KeyValueInformationClass,
            KeyValueInformation,
            Length,
            ResultLength
        );
        WriteLog(L"virtual MUI value %ws: %08X", ValueName->Buffer, Status);
        return Status;
    }

    if (GlobalData->GetLepb()->HookUILanguageApi == 2 &&
        RtlEqualUnicodeString(ValueName, PUSTR(L"InstallLanguageFallback"), TRUE) &&
        LepIsNlsLanguageKeyHandle(KeyHandle))
    {
        HpSetFilterAction(BlockSystemCall);
        Status = LepQueryVirtualInstallLanguageFallback(
            GlobalData,
            ValueName,
            KeyValueInformationClass,
            KeyValueInformation,
            Length,
            ResultLength
        );
        WriteLog(L"virtual MUI install fallback %ws: %08X",
            GlobalData->HookRoutineData.Ntdll.MuiInstallFallback,
            Status);
        return Status;
    }
    
    WriteLog(L"lookup %ws", ValueName->Buffer);

    Status = GlobalData->LookupRegistryRedirectionEntry(KeyHandle, ValueName, &Entry);

    WriteLog(L"lookup %ws: %p", ValueName->Buffer, Status);

    FAIL_RETURN(Status);

    HpSetFilterAction(BlockSystemCall);

    if (Entry->Redirected.Data != nullptr)
    {
        union
        {
            PVOID                                   Information;
            PKEY_VALUE_BASIC_INFORMATION            BasicInformation;
            PKEY_VALUE_FULL_INFORMATION             FullInformation;
            PKEY_VALUE_PARTIAL_INFORMATION          PartialInformation;
            PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64  PartialInformationAlign64;
        };

        ULONG_PTR   Alignment, DataLength, DataOffset, RequiredLength, MinimumSize;
        PVOID       LocalBuffer;

        Alignment       = sizeof(ULONG_PTR);
        DataOffset      = 0;
        DataLength      = Entry->Redirected.DataSize;
        RequiredLength  = 0;
        Information     = nullptr;

        switch (KeyValueInformationClass)
        {
            default:
                return STATUS_INVALID_PARAMETER;

            case KeyValueBasicInformation:
                goto REDIRECT_ONLY;

            case KeyValueFullInformationAlign64:
                Alignment = sizeof(ULONG64);
                NO_BREAK;

            case KeyValueFullInformation:
                Alignment = sizeof(ULONG64);

                MinimumSize     = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name);
                RequiredLength  = MinimumSize + Entry->Redirected.ValueName.GetSize();
                RequiredLength  = ROUND_UP(RequiredLength, Alignment);
                DataOffset      = RequiredLength;
                RequiredLength += DataLength;
                RequiredLength  = ROUND_UP(RequiredLength, sizeof(ULONG));

                if (KeyValueInformation == nullptr || Length < MinimumSize)
                    break;

                Information = Length < RequiredLength ? AllocStack(RequiredLength) : KeyValueInformation;

                FullInformation->TitleIndex = 0;
                FullInformation->Type       = Entry->Redirected.DataType;
                FullInformation->NameLength = Entry->Redirected.ValueName.GetSize() + sizeof(WCHAR);
                FullInformation->DataOffset = DataOffset;
                FullInformation->DataLength = DataLength;

                CopyMemory(FullInformation->Name, (PWSTR)Entry->Redirected.ValueName, FullInformation->NameLength);
                CopyMemory(PtrAdd(FullInformation, DataOffset), Entry->Redirected.Data, DataLength);
                break;

            case KeyValuePartialInformationAlign64:
                Alignment = sizeof(ULONG64);

                MinimumSize = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data);
                RequiredLength = MinimumSize + DataLength;
                RequiredLength = ROUND_UP(RequiredLength, Alignment);
                if (KeyValueInformation == nullptr || Length < MinimumSize)
                    break;

                Information = Length < RequiredLength ? AllocStack(RequiredLength) : KeyValueInformation;

                PartialInformationAlign64->Type = Entry->Redirected.DataType;
                PartialInformationAlign64->DataLength = DataLength;

                CopyMemory(PartialInformationAlign64->Data, Entry->Redirected.Data, DataLength);
                break;

            case KeyValuePartialInformation:
                MinimumSize = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
                RequiredLength = MinimumSize + DataLength;
                RequiredLength = ROUND_UP(RequiredLength, Alignment);
                if (KeyValueInformation == nullptr || Length < MinimumSize)
                    break;

                Information = Length < RequiredLength ? AllocStack(RequiredLength) : KeyValueInformation;

                PartialInformation->TitleIndex  = 0;
                PartialInformation->Type        = Entry->Redirected.DataType;
                PartialInformation->DataLength  = DataLength;

                CopyMemory(PartialInformation->Data, Entry->Redirected.Data, DataLength);
                break;
        }

        if (Information != nullptr && Information != KeyValueInformation)
        {
            CopyMemory(KeyValueInformation, Information, ML_MIN(RequiredLength, Length));
        }

        if (ResultLength != nullptr)
            *ResultLength = RequiredLength;

        if (KeyValueInformation == nullptr)
            return STATUS_ACCESS_VIOLATION;

        if (Length < MinimumSize)
            return STATUS_BUFFER_TOO_SMALL;

        if (Length < RequiredLength)
            return STATUS_BUFFER_OVERFLOW;

        return STATUS_SUCCESS;
    }
    else
    {

REDIRECT_ONLY:

        InitializeObjectAttributes(&ObjectAttributes, Entry->Redirected.FullPath, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
        FAIL_RETURN(Status);

        Status = HpCallSysCall(
                    NtQueryValueKey,
                    KeyHandle,
                    ValueName,
                    KeyValueInformationClass,
                    KeyValueInformation,
                    Length,
                    ResultLength
                );

        NtClose(KeyHandle);
    }

    return Status;
}

NTSTATUS
HPCALL
LepNtQueryLicenseValue(
    HPARGS
    PUNICODE_STRING Name,
    PULONG Type,
    PVOID Buffer,
    ULONG Length,
    PULONG DataLength
)
{
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();
    UNICODE_STRING MuiLanguageAllowed;
    ULONG RequiredLength;

    RtlInitUnicodeString(&MuiLanguageAllowed, L"Kernel-MUI-Language-Allowed");
    SEH_TRY
    {
        if (Name == nullptr ||
            NT_FAILED(RtlValidateUnicodeString(0, Name)) ||
            !RtlEqualUnicodeString(Name, &MuiLanguageAllowed, TRUE))
        {
            return STATUS_SUCCESS;
        }
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        // Let the real system call validate malformed caller pointers.
        return STATUS_SUCCESS;
    }

    HpSetFilterAction(BlockSystemCall);
    RequiredLength = GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength + sizeof(WCHAR);

    SEH_TRY
    {
        if (DataLength == nullptr)
            return STATUS_INVALID_PARAMETER;

        *DataLength = RequiredLength;
        if (Type != nullptr)
            *Type = REG_SZ;

        if (Length < RequiredLength)
            return STATUS_BUFFER_TOO_SMALL;
        if (Buffer == nullptr)
            return STATUS_ACCESS_VIOLATION;

        CopyMemory(
            Buffer,
            GlobalData->HookRoutineData.Ntdll.MuiLanguageName,
            GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength
        );
        ((PWSTR)Buffer)[GlobalData->HookRoutineData.Ntdll.MuiLanguageNameLength / sizeof(WCHAR)] = 0;
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode();
    }

    WriteLog(L"virtual MUI licensed language: %ws",
        GlobalData->HookRoutineData.Ntdll.MuiLanguageName);
    return STATUS_SUCCESS;
}

NTSTATUS
HPCALL
LepNtQueryDefaultLocale(
    HPARGS
    BOOLEAN UserProfile,
    PLCID   DefaultLocaleId
)
{
    HpSetFilterAction(BlockSystemCall);

    *DefaultLocaleId = ((PLepGlobalData)HpGetFilterContext())->GetLepb()->LocaleID;
    return STATUS_SUCCESS;
}

NTSTATUS
HPCALL
LepNtQueryDefaultUILanguage(
    HPARGS
    LANGID *DefaultUILanguageId
)
{
    HpSetFilterAction(BlockSystemCall);

    *DefaultUILanguageId = ((PLepGlobalData)HpGetFilterContext())->GetLepb()->LocaleID;
    return STATUS_SUCCESS;
}

NTSTATUS
HPCALL
LepNtQueryInstallUILanguage(
    HPARGS
    LANGID *InstallUILanguageId
)
{
    HpSetFilterAction(BlockSystemCall);

    *InstallUILanguageId = ((PLepGlobalData)HpGetFilterContext())->GetLepb()->LocaleID;
    return STATUS_SUCCESS;
}

#if !ML_AMD64
NTSTATUS NTAPI LepLdrInitNtContinue(PCONTEXT Context, BOOLEAN TestAlert)
{
    PLepGlobalData GlobalData = LepGetGlobalData();

    CurrentTeb()->CurrentLocale = GlobalData->GetLepb()->LocaleID;

    return GlobalData->HookStub.StubLdrInitNtContinue(Context, TestAlert);
}
#endif

#if ML_AMD64
NTSTATUS
HPCALL
LepNtContinue(
    HPARGS
    PCONTEXT    Context,
    BOOLEAN     TestAlert
)
{
    PLepGlobalData GlobalData = (PLepGlobalData)HpGetFilterContext();

    if (GlobalData != nullptr)
        CurrentTeb()->CurrentLocale = GlobalData->GetLepb()->LocaleID;

    return 0;
}
#endif

NTSTATUS
NTAPI
LepLdrResSearchResource(
    PVOID       DllHandle,
    PULONG_PTR  ResourceIdPath,
    ULONG       ResourceIdPathLength,
    ULONG       Flags,
    PVOID*      Resource,
    PULONG      Size,
    PVOID       Reserve1,
    PVOID       Reserve2
)
{
    NTSTATUS Status;
    PLepGlobalData GlobalData = LepGetGlobalData();

    ULONG_PTR Type = 0;
    ULONG_PTR Name = 0;
    ULONG_PTR Language = 0;
    BOOLEAN LogManifest = FALSE;

    SEH_TRY
    {
        if (ResourceIdPath != nullptr && ResourceIdPathLength != 0)
        {
            Type = ResourceIdPath[0];
            if (ResourceIdPathLength > 1)
                Name = ResourceIdPath[1];
            if (ResourceIdPathLength > 2)
                Language = ResourceIdPath[2];
            LogManifest = Type == (ULONG_PTR)RT_MANIFEST;
        }
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        LogManifest = FALSE;
    }

    Status = GlobalData->HookStub.StubLdrResSearchResource(DllHandle, ResourceIdPath, ResourceIdPathLength, Flags, Resource, Size, Reserve1, Reserve2);
    if (LogManifest)
    {
        WriteLog(L"LdrResSearchResource manifest: module=%p depth=%u name=%p lang=%p flags=%08X status=%08X resource=%p size=%u",
            DllHandle,
            ResourceIdPathLength,
            Name,
            Language,
            Flags,
            Status,
            Resource != nullptr ? *Resource : nullptr,
            Size != nullptr ? *Size : 0);
    }
    FAIL_RETURN(Status);

    if (ResourceIdPathLength != 3)
        return Status;

    if (ResourceIdPath[0] != (ULONG_PTR)RT_VERSION || ResourceIdPath[1] != 1 || ResourceIdPath[2] != 0)
        return Status;

    if (Resource == nullptr)
        return Status;

    ReplaceMUIVersionLocaleInfo(*Resource, *Size);

    return Status;
}

NTSTATUS
NTAPI
LepRtlGetThreadPreferredUILanguages(
    ULONG Flags,
    PULONG NumberOfLanguages,
    PWSTR LanguagesBuffer,
    PULONG BufferLength
)
{
    PLepGlobalData GlobalData = LepGetGlobalData();

    NTSTATUS Status = GlobalData->HookStub.StubRtlGetThreadPreferredUILanguages(
        Flags,
        NumberOfLanguages,
        LanguagesBuffer,
        BufferLength
    );

    // Calls without MUI_LANGUAGE_ID/MUI_LANGUAGE_NAME are also used by
    // KernelBase to make ntdll initialize its private per-thread MUI cache.
    // Returning synthetic success there is unsafe: the caller subsequently
    // reads the cache pointer from the TEB, which our public buffer fallback
    // cannot initialize.  Only synthesize ordinary language-list queries.
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND &&
        (Flags & (MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME)) != 0 &&
        NumberOfLanguages != nullptr &&
        BufferLength != nullptr)
    {
        PCWSTR LanguageNames[3];
        ULONG LanguageCount = 0;
        UNICODE_STRING Candidate;

        LanguageNames[LanguageCount++] = GlobalData->HookRoutineData.Ntdll.MuiLanguageName;

        RtlInitUnicodeString(&Candidate, GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName);
        UNICODE_STRING Target;
        RtlInitUnicodeString(&Target, LanguageNames[0]);
        if (Candidate.Length != 0 && !RtlEqualUnicodeString(&Candidate, &Target, TRUE))
            LanguageNames[LanguageCount++] = GlobalData->HookRoutineData.Ntdll.MuiHostLanguageName;

        if (GlobalData->HookRoutineData.Ntdll.MuiHostFallbackLength != 0)
        {
            RtlInitUnicodeString(&Candidate, GlobalData->HookRoutineData.Ntdll.MuiHostFallback);
            BOOLEAN Duplicate = FALSE;
            for (ULONG Index = 0; Index < LanguageCount; ++Index)
            {
                UNICODE_STRING Existing;
                RtlInitUnicodeString(&Existing, LanguageNames[Index]);
                if (RtlEqualUnicodeString(&Candidate, &Existing, TRUE))
                {
                    Duplicate = TRUE;
                    break;
                }
            }
            if (!Duplicate)
                LanguageNames[LanguageCount++] = GlobalData->HookRoutineData.Ntdll.MuiHostFallback;
        }

        BOOLEAN ReturnLanguageIds = (Flags & MUI_LANGUAGE_ID) != 0;
        WCHAR LanguageIds[3][5];
        ULONG RequiredLength = 1;
        for (ULONG Index = 0; Index < LanguageCount; ++Index)
        {
            if (ReturnLanguageIds)
            {
                LCID LocaleId = 0;
                UNICODE_STRING LanguageName;
                typedef BOOLEAN (NTAPI *PRTL_CULTURE_NAME_TO_LCID)(PUNICODE_STRING, PLCID);
                PRTL_CULTURE_NAME_TO_LCID CultureNameToLcid;
                *(PVOID *)&CultureNameToLcid = LookupExportTable(
                    GetNtdllHandle(),
                    NTDLL_RtlCultureNameToLCID
                );
                RtlInitUnicodeString(&LanguageName, LanguageNames[Index]);
                if (CultureNameToLcid == nullptr || !CultureNameToLcid(&LanguageName, &LocaleId))
                    return Status;

                static const WCHAR HexDigits[] = L"0123456789ABCDEF";
                LANGID LanguageId = LANGIDFROMLCID(LocaleId);
                LanguageIds[Index][0] = HexDigits[(LanguageId >> 12) & 0xF];
                LanguageIds[Index][1] = HexDigits[(LanguageId >> 8) & 0xF];
                LanguageIds[Index][2] = HexDigits[(LanguageId >> 4) & 0xF];
                LanguageIds[Index][3] = HexDigits[LanguageId & 0xF];
                LanguageIds[Index][4] = 0;
                RequiredLength += countof(LanguageIds[Index]);
            }
            else
            {
                RequiredLength += (ULONG)StrLengthW(LanguageNames[Index]) + 1;
            }
        }

        ULONG Capacity = *BufferLength;
        *NumberOfLanguages = LanguageCount;
        *BufferLength = RequiredLength;
        if (LanguagesBuffer == nullptr || Capacity < RequiredLength)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            ULONG Offset = 0;
            for (ULONG Index = 0; Index < LanguageCount; ++Index)
            {
                PCWSTR Source = ReturnLanguageIds ? LanguageIds[Index] : LanguageNames[Index];
                ULONG Length = (ULONG)StrLengthW(Source);
                CopyMemory(LanguagesBuffer + Offset, Source, Length * sizeof(WCHAR));
                Offset += Length;
                LanguagesBuffer[Offset++] = 0;
            }
            LanguagesBuffer[Offset] = 0;
            Status = STATUS_SUCCESS;
            WriteLog(L"RtlGetThreadPreferredUILanguages virtual fallback: flags=%08X count=%u length=%u",
                Flags, LanguageCount, RequiredLength);
        }
    }

    SEH_TRY
    {
        ULONG Count = NumberOfLanguages != nullptr ? *NumberOfLanguages : 0;
        ULONG Length = BufferLength != nullptr ? *BufferLength : 0;
        WriteLog(L"RtlGetThreadPreferredUILanguages: flags=%08X status=%08X count=%u length=%u buffer=%p",
            Flags, Status, Count, Length, LanguagesBuffer);

        if (NT_SUCCESS(Status) && LanguagesBuffer != nullptr && Length != 0)
        {
            ULONG Offset = 0;
            for (ULONG Index = 0; Index < Count && Index < 16 && Offset < Length; ++Index)
            {
                WCHAR Language[LOCALE_NAME_MAX_LENGTH];
                ULONG LanguageLength = 0;
                while (Offset + LanguageLength < Length &&
                       LanguagesBuffer[Offset + LanguageLength] != 0 &&
                       LanguageLength + 1 < countof(Language))
                {
                    Language[LanguageLength] = LanguagesBuffer[Offset + LanguageLength];
                    ++LanguageLength;
                }
                Language[LanguageLength] = 0;
                WriteLog(L"RtlGetThreadPreferredUILanguages[%u]=%ws", Index, Language);
                Offset += LanguageLength + 1;
            }
        }
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLog(L"RtlGetThreadPreferredUILanguages: diagnostic buffer exception");
    }

    return Status;
}

NTSTATUS
NTAPI
LepRtlGetThreadPreferredUILanguagesPassthrough(
    ULONG Flags,
    PULONG NumberOfLanguages,
    PWSTR LanguagesBuffer,
    PULONG BufferLength
)
{
    return LepGetGlobalData()->HookStub.StubRtlGetThreadPreferredUILanguages(
        Flags,
        NumberOfLanguages,
        LanguagesBuffer,
        BufferLength
    );
}

LONG NTAPI LepKnownExceptionFilter(PEXCEPTION_POINTERS ExceptionPointers)
{
    LONG Result = LepGetGlobalData()->RtlKnownExceptionFilter(ExceptionPointers);

    if (Result == EXCEPTION_CONTINUE_SEARCH)
        CreateMiniDump(ExceptionPointers);

    return Result;
}

#define LEP_ENABLE_CUSTOM_CP_TO_UNICODE_HOOK 0

NTSTATUS NTAPI LepCustomCPToUnicodeN(IN PCPTABLEINFO CustomCP,
    OUT PWCHAR UnicodeString,
    IN ULONG UnicodeSize,
    OUT PULONG ResultSize OPTIONAL,
    IN PCHAR CustomString,
    IN ULONG CustomSize) {
    PLepGlobalData GlobalData = LepGetGlobalData();

    if (CustomCP->CodePage != 65001 && CustomCP->CodePage != GlobalData->GetLepb()->AnsiCodePage) {
        PROTECT_SECTION(&GlobalData->HookRoutineData.Ntdll.NtLock) {
            if (CustomCP->CodePage != 65001 && CustomCP->CodePage != GlobalData->GetLepb()->AnsiCodePage) {
                RtlInitCodePageTable((PUSHORT)PtrAdd(GlobalData->CodePageMapView,
                    GlobalData->AnsiCodePageOffset), CustomCP);
            }
        }
    }

    return GlobalData->HookStub.StubRtlCustomCPToUnicodeN(CustomCP, UnicodeString,
        UnicodeSize, ResultSize, CustomString, CustomSize); 
}

NTSTATUS LepGlobalData::HookNtdllRoutines(PVOID Ntdll)
{
    NTSTATUS            Status;
    OBJECT_ATTRIBUTES   ObjectAttributes;
    UNICODE_STRING      SubKey;
#if !ML_AMD64
    PVOID               LdrInitNtContinue;
#endif

#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
    if (LEP_X64_CRASH_PROBE == 1)
        return STATUS_DLL_INIT_FAILED;
#endif

#if !ML_AMD64 && !LEP_DIAG_PROCESS_ONLY
    LdrInitNtContinue = SearchLdrInitNtContinue();
    if (LdrInitNtContinue == nullptr)
        return STATUS_NOT_SUPPORTED;

	WriteLog(L"LdrInitNtContinue %08X", LdrInitNtContinue);
#endif

    Status = HpAddSystemCallFilter(NTDLL_NtCreateUserProcess, LepNtCreateUserProcess, this);
    WriteLog(L"hook NtCreateUserProcess: %08X", Status);
    FAIL_RETURN(Status);

#if LEP_DIAG_PROCESS_ONLY
    WriteLog(L"process-only: installed NtCreateUserProcess only");
    return STATUS_SUCCESS;
#else
    if (IsLepLoader())
        return STATUS_SUCCESS;

    if (GetLepb()->HookUILanguageApi == 2)
    {
        Status = LepInitializeVirtualMuiLanguage(this, Ntdll);
        FAIL_RETURN(Status);
    }

    if (this->RegistryRedirectionEntry.GetSize() != 0 || GetLepb()->HookUILanguageApi == 2)
    {
#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
        if (LEP_X64_CRASH_PROBE == 2)
            return STATUS_DLL_INIT_FAILED;
#endif
        ADD_FILTER_(NtQueryValueKey, LepNtQueryValueKey, this);
    }

    ADD_FILTER_(NtQuerySystemInformation,   LepNtQuerySystemInformation, this);
    ADD_FILTER_(NtInitializeNlsFiles,       LepNtInitializeNlsFiles,     this);
    ADD_FILTER_(NtQueryDefaultLocale,       LepNtQueryDefaultLocale,     this);
    ADD_FILTER_(NtQueryDefaultUILanguage,   LepNtQueryDefaultUILanguage, this);
    if (GetLepb()->HookUILanguageApi == 2)
    {
        Status = ADD_FILTER_(NtQueryLicenseValue, LepNtQueryLicenseValue, this);
        WriteLog(L"hook NtQueryLicenseValue: %08X", Status);
        FAIL_RETURN(Status);
        ADD_FILTER_(NtClose,                  LepNtClose,                  this);
        ADD_FILTER_(NtOpenKey,                LepNtOpenKey,                this);
        ADD_FILTER_(NtEnumerateKey,           LepNtEnumerateKey,           this);
        ADD_FILTER_(NtEnumerateValueKey,      LepNtEnumerateValueKey,      this);
        ADD_FILTER_(NtQueryKey,               LepNtQueryKey,               this);
        ADD_FILTER_(NtQueryInstallUILanguage, LepNtQueryInstallUILanguage, this);
    }
#if ML_AMD64
    ADD_FILTER_(NtContinue,                 LepNtContinue,               this);
#endif

    //ADD_FILTER_(NtQueryInformationThread,   LepNtQueryInformationThread, this);
    //ADD_FILTER_(NtTerminateThread,          LepNtTerminateThread,        this);

#if !ML_AMD64 || LEP_ENABLE_CUSTOM_CP_TO_UNICODE_HOOK
    Mp::PATCH_MEMORY_DATA p[] =
    {
#if !ML_AMD64
        Mp::FunctionCallVa(LdrInitNtContinue, LepLdrInitNtContinue, &HookStub.StubLdrInitNtContinue),
#endif
#if LEP_ENABLE_CUSTOM_CP_TO_UNICODE_HOOK
        Mp::FunctionJumpVa(::RtlCustomCPToUnicodeN, LepCustomCPToUnicodeN, &HookStub.StubRtlCustomCPToUnicodeN, LEP_FUNCTION_JUMP_OP),
#endif
    };

    Mp::PatchMemory(p, countof(p));
#endif

#if ML_AMD64 && defined(LEP_X64_CRASH_PROBE)
    if (LEP_X64_CRASH_PROBE == 3)
        return STATUS_DLL_INIT_FAILED;
#endif

    RtlInitializeCriticalSectionAndSpinCount(&HookRoutineData.Ntdll.NtLock, 4000);

    return STATUS_SUCCESS;
#endif
}

NTSTATUS LepGlobalData::UnHookNtdllRoutines()
{
#if !ML_AMD64
    Mp::RestoreMemory(HookStub.StubLdrInitNtContinue);
#endif
    Mp::RestoreMemory(HookStub.StubRtlKnownExceptionFilter);
#if LEP_ENABLE_CUSTOM_CP_TO_UNICODE_HOOK
    Mp::RestoreMemory(HookStub.StubRtlCustomCPToUnicodeN);
#endif
    Mp::RestoreMemory(HookStub.StubLdrResSearchResource);

    return 0;
}
