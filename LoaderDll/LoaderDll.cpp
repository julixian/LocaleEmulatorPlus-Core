#pragma comment(linker, "/SECTION:.text,ERW /MERGE:.rdata=.text /MERGE:.data=.text")
#pragma comment(linker, "/SECTION:.Asuna,ERW /MERGE:.text=.Asuna")

#include "../LocaleEmulatorPlus/support/Support.h"
#include "LoaderDll.h"
#include "Environment.h"

#if LEP_LOADER_DLL

static HANDLE g_BrokerLog = INVALID_HANDLE_VALUE;

static ULONG LoaderFormatW(PWSTR Buffer, ULONG Capacity, PCWSTR Format, ...)
{
    Int Length;
    va_list Arguments;

    if (Buffer == nullptr || Capacity == 0)
        return 0;

    va_start(Arguments, Format);
    Length = FormatStringvnW(Buffer, (UInt)Capacity, Format, Arguments);
    va_end(Arguments);

    if (Length < 0)
    {
        Buffer[Capacity - 1] = 0;
        return Capacity - 1;
    }

    return (ULONG)Length;
}

#if ENABLE_LOG

static ULONG BrokerFormat(PWSTR Buffer, ULONG Capacity, PCWSTR Format, ...)
{
    Int Length;
    va_list Arguments;

    if (Buffer == nullptr || Capacity == 0)
        return 0;

    va_start(Arguments, Format);
    Length = FormatStringvnW(Buffer, (UInt)Capacity, Format, Arguments);
    va_end(Arguments);

    if (Length < 0)
    {
        Buffer[Capacity - 1] = 0;
        return Capacity - 1;
    }

    return (ULONG)Length;
}

static VOID BrokerLog(PCWSTR Text)
{
    WCHAR Path[MAX_PATH];
    WCHAR Buffer[512];
    DWORD Length, Written;

    if (g_BrokerLog == INVALID_HANDLE_VALUE)
    {
        if (!LepGetModuleLogDirectory(Path, countof(Path)))
            return;
        Length = StrLengthW(Path);
        if (Length >= countof(Path) - 48)
            return;

        Length += BrokerFormat(Path + Length, countof(Path) - Length,
                               L"LocaleEmulatorPlus-broker-%u.log", (ULONG)GetCurrentProcessId());
        g_BrokerLog = CreateFileW(Path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    if (g_BrokerLog == INVALID_HANDLE_VALUE)
        return;

    Length = BrokerFormat(Buffer, countof(Buffer), L"[%u] %s\r\n",
                          (ULONG)GetCurrentProcessId(), Text);
    WriteFile(g_BrokerLog, Buffer, Length * sizeof(WCHAR), &Written, nullptr);
    OutputDebugStringW(Buffer);
}

#else

static ULONG BrokerFormat(PWSTR Buffer, ULONG Capacity, PCWSTR Format, ...)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Capacity);
    UNREFERENCED_PARAMETER(Format);
    return 0;
}

static VOID BrokerLog(PCWSTR Text)
{
    UNREFERENCED_PARAMETER(Text);
}

#endif

static ULONG BrokerParseUIntA(LPCSTR& Cursor)
{
    ULONG Value = 0;
    while (*Cursor >= '0' && *Cursor <= '9')
    {
        Value = Value * 10 + (ULONG)(*Cursor++ - '0');
    }
    return Value;
}

// rundll32 exposes the command argument as LPSTR.  The broker protocol keeps
// that argument strictly ASCII (PID and generated mapping name), so widen it
// byte-for-byte instead of decoding through the process code page.
static BOOL BrokerReadTokenW(LPCSTR& Cursor, LPWSTR Buffer, ULONG Capacity)
{
    ULONG Length = 0;
    while (*Cursor == ' ' || *Cursor == '|')
        ++Cursor;
    while (*Cursor != 0 && *Cursor != '|')
    {
        if (Length + 1 >= Capacity)
            return FALSE;
        if ((UCHAR)*Cursor > 0x7F)
            return FALSE;
        Buffer[Length++] = (WCHAR)(UCHAR)*Cursor++;
    }
    Buffer[Length] = 0;
    return Length != 0;
}

static NTSTATUS LepFillFirstDllBootstrapData(
    PVOID LocalImage,
    PVOID LdrLoadDllAddress,
    PBYTE LdrLoadDllBackup,
    ULONG OriginalProtect,
    ULONG InjectionFlags,
    ULONG PayloadOffset,
    ULONG PayloadSize
)
{
    PLEP_FIRST_DLL_BOOTSTRAP_DATA Data = (PLEP_FIRST_DLL_BOOTSTRAP_DATA)
        LookupExportTable(LocalImage, LEP_FIRST_DLL_BOOTSTRAP_EXPORT);
    if (Data == nullptr)
        return STATUS_ENTRYPOINT_NOT_FOUND;

    ZeroMemory(Data, sizeof(*Data));
    Data->Magic = LEP_FIRST_DLL_BOOTSTRAP_MAGIC;
    Data->InjectionFlags = InjectionFlags;
    Data->BackupSize = LDR_LOAD_DLL_BACKUP_SIZE;
    Data->OriginalProtect = OriginalProtect;
    Data->LdrLoadDllAddress = LdrLoadDllAddress;
    Data->PayloadOffset = PayloadOffset;
    Data->PayloadSize = PayloadSize;
    CopyMemory(Data->Backup, LdrLoadDllBackup, LDR_LOAD_DLL_BACKUP_SIZE);

    return STATUS_SUCCESS;
}

static NTSTATUS LepCreateBootstrapPayload(
    PLEPB Environment,
    PCWSTR DllPath,
    PLEP_BOOTSTRAP_PAYLOAD* Payload,
    PULONG PayloadSize
)
{
    if (Environment == nullptr || DllPath == nullptr || Payload == nullptr || PayloadSize == nullptr)
        return STATUS_INVALID_PARAMETER;
    LEP_RUNTIME_ENVIRONMENT Resolved;
    NTSTATUS Status = LepResolveEnvironment(Environment, &Resolved);
    if (NT_FAILED(Status))
        return Status;
    ULONG EnvironmentSize = sizeof(Resolved);

    WCHAR DirPath[MAX_NTPATH];
    ULONG_PTR Length = StrLengthW(DllPath);
    if (Length + 1 > countof(DirPath))
    {
        return STATUS_NAME_TOO_LONG;
    }
    CopyMemory(DirPath, DllPath, (Length + 1) * sizeof(WCHAR));
    for (ULONG_PTR Index = Length; Index != 0; --Index)
    {
        if (DirPath[Index - 1] == L'\\')
        {
            DirPath[Index] = 0;
            break;
        }
    }

    ULONG Required = LepBootstrapPayloadSize(EnvironmentSize, DllPath, DirPath);
    if (Required == 0)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    PLEP_BOOTSTRAP_PAYLOAD LocalPayload = (PLEP_BOOTSTRAP_PAYLOAD)AllocateMemory(Required);
    if (LocalPayload == nullptr)
    {
        return STATUS_NO_MEMORY;
    }

    Status = LepBuildBootstrapPayload(LocalPayload, Required, &Resolved, EnvironmentSize,
                                      DllPath, DirPath);
    if (NT_FAILED(Status))
    {
        FreeMemory(LocalPayload);
        return Status;
    }
    *Payload = LocalPayload;
    *PayloadSize = Required;
    return STATUS_SUCCESS;
}

static VOID BrokerRecordStage(PLEP_BROKER_CONFIG Config, ULONG Stage, NTSTATUS Status)
{
    WCHAR Log[256];
    if (Config != nullptr)
        Config->Stage = Stage;
    BrokerFormat(Log, countof(Log), L"stage=%ws status=%08X", LepBrokerStageName(Stage), Status);
    BrokerLog(Log);
}

static NTSTATUS LepInjectSuspendedProcess(
    HANDLE Process,
    PLEP_BOOTSTRAP_PAYLOAD Payload,
    ULONG PayloadSize,
    ULONG InjectionFlags,
    PLEP_BROKER_CONFIG StageConfig
)
{
    PVOID LocalImage = nullptr;
    PVOID RemoteImage = nullptr;
    PVOID LdrLoadDllAddress = nullptr;
    ULONG LdrLoadDllProtect = 0;
    BOOL LdrLoadDllWritable = FALSE;
    BYTE Backup[LDR_LOAD_DLL_BACKUP_SIZE];
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    WCHAR Log[512];

    PCWSTR DllPath = LepBootstrapDllFullPath(Payload);
    if (DllPath == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (StageConfig != nullptr)
        StageConfig->Stage = LepBrokerStageLoadImage;
    Status = LoadPeImage(DllPath, &LocalImage, nullptr, 0);
    if (StageConfig != nullptr)
        BrokerRecordStage(StageConfig, LepBrokerStageLoadImage, Status);
    if (NT_FAILED(Status))
        goto FINISH_INJECT;

    ULONG_PTR ImageSize = ImageGetSizeOfImage(LocalImage);
    if (ImageSize > LEP_BOOTSTRAP_IMAGE_MAX_SIZE)
    {
        Status = STATUS_BUFFER_OVERFLOW;
        goto FINISH_INJECT;
    }
    ULONG_PTR PayloadOffset = ROUND_UP(ImageSize, 16);
    if (StageConfig != nullptr)
        StageConfig->Stage = LepBrokerStageAllocateMemory;
    Status = AllocVirtualMemoryEx(Process, &RemoteImage, PayloadOffset + PayloadSize);
    if (StageConfig != nullptr)
        BrokerRecordStage(StageConfig, LepBrokerStageAllocateMemory, Status);
    if (NT_FAILED(Status))
        goto FINISH_INJECT;

    LdrLoadDllAddress = EATLookupRoutineByHashPNoFix(GetNtdllHandle(), lep::HashExportName("LdrLoadDll"));
    if (LdrLoadDllAddress == nullptr)
    {
        Status = STATUS_ENTRYPOINT_NOT_FOUND;
        goto FINISH_INJECT;
    }

    if ((InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) != 0)
    {
        if (StageConfig != nullptr)
            StageConfig->Stage = LepBrokerStageReadLdr;
        Status = ReadMemory(Process, LdrLoadDllAddress, Backup, sizeof(Backup));
        if (StageConfig != nullptr)
            BrokerRecordStage(StageConfig, LepBrokerStageReadLdr, Status);
        if (NT_FAILED(Status))
            goto FINISH_INJECT;
        if (StageConfig != nullptr)
            StageConfig->Stage = LepBrokerStageProtectLdr;
        Status = ProtectVirtualMemory(LdrLoadDllAddress, sizeof(Backup), PAGE_EXECUTE_READWRITE,
                                      &LdrLoadDllProtect, Process);
        if (StageConfig != nullptr)
            BrokerRecordStage(StageConfig, LepBrokerStageProtectLdr, Status);
        if (NT_FAILED(Status))
            goto FINISH_INJECT;
        LdrLoadDllWritable = TRUE;
    }

    RelocPeImage(LocalImage, LocalImage, nullptr, RemoteImage);
    PLEP_FIRST_DLL_BOOTSTRAP_DATA BootstrapData = (PLEP_FIRST_DLL_BOOTSTRAP_DATA)
        LookupExportTable(LocalImage, LEP_FIRST_DLL_BOOTSTRAP_EXPORT);
    if (BootstrapData == nullptr)
    {
        Status = STATUS_ENTRYPOINT_NOT_FOUND;
        goto FINISH_INJECT;
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

    if (StageConfig != nullptr)
        StageConfig->Stage = LepBrokerStageWriteImage;
    Status = WriteMemory(Process, RemoteImage, LocalImage, ImageSize);
    if (StageConfig != nullptr)
        BrokerRecordStage(StageConfig, LepBrokerStageWriteImage, Status);
    if (NT_SUCCESS(Status))
    {
        if (StageConfig != nullptr)
            StageConfig->Stage = LepBrokerStageWritePayload;
        Status = WriteMemory(Process, PtrAdd(RemoteImage, PayloadOffset), Payload, PayloadSize);
        if (StageConfig != nullptr)
            BrokerRecordStage(StageConfig, LepBrokerStageWritePayload, Status);
    }
    if (NT_FAILED(Status) || (InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) == 0)
        goto FINISH_INJECT;

    PVOID LocalLoadFirstDll = LookupExportTable(LocalImage, "LoadFirstDll");
    if (LocalLoadFirstDll == nullptr)
    {
        Status = STATUS_ENTRYPOINT_NOT_FOUND;
        goto FINISH_INJECT;
    }
    PVOID RemoteLoadFirstDll = PtrAdd(RemoteImage, PtrOffset(LocalLoadFirstDll, LocalImage));
    BYTE Jump[16] = {};
#if ML_AMD64
    Jump[0] = 0xFF;
    Jump[1] = 0x25;
    *(PVOID*)&Jump[6] = RemoteLoadFirstDll;
#else
    Jump[0] = JUMP;
    *(LONG*)&Jump[1] = (LONG)PtrOffset(RemoteLoadFirstDll, PtrAdd(LdrLoadDllAddress, 5));
#endif
    if (StageConfig != nullptr)
        StageConfig->Stage = LepBrokerStagePatchLdr;
    Status = WriteMemory(Process, LdrLoadDllAddress, Jump, sizeof(Backup));
    if (StageConfig != nullptr)
        BrokerRecordStage(StageConfig, LepBrokerStagePatchLdr, Status);
    BrokerFormat(Log, countof(Log), L"metadata injection status=%08X image=%I64X payload=%u",
                 Status, (ULONG64)(ULONG_PTR)RemoteImage, PayloadSize);
    BrokerLog(Log);
    if (NT_SUCCESS(Status) && StageConfig != nullptr)
        BrokerRecordStage(StageConfig, LepBrokerStageComplete, Status);

FINISH_INJECT:
    if (LocalImage != nullptr)
        UnloadPeImage(LocalImage);
    if (NT_FAILED(Status) && LdrLoadDllWritable)
    {
        ULONG IgnoredProtect;
        ProtectVirtualMemory(LdrLoadDllAddress, sizeof(Backup), LdrLoadDllProtect,
                             &IgnoredProtect, Process);
    }
    if (NT_FAILED(Status) && RemoteImage != nullptr)
        Mm::FreeVirtualMemory(RemoteImage, Process);
    return Status;
}

static NTSTATUS LepBrokerInject(ULONG ProcessId, LPCWSTR MappingName)
{
    HANDLE Process = nullptr;
    HANDLE Mapping = nullptr;
    PVOID View = nullptr;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PLEP_BROKER_CONFIG Config = nullptr;
    WCHAR Log[512];
    OBJECT_BASIC_INFORMATION ProcessInfo = {};

    Mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, MappingName);
    if (Mapping == nullptr)
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
        BrokerFormat(Log, countof(Log), L"open-mapping status=%08X", Status);
        BrokerLog(Log);
        goto BROKER_FINISH_NEW;
    }
    View = MapViewOfFile(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (View == nullptr)
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
        BrokerFormat(Log, countof(Log), L"map-view status=%08X", Status);
        BrokerLog(Log);
        goto BROKER_FINISH_NEW;
    }

    Config = (PLEP_BROKER_CONFIG)View;
    Config->Result = STATUS_UNSUCCESSFUL;
    if (Config->Magic != LEP_BROKER_CONFIG_MAGIC ||
        Config->Version != LEP_BROKER_CONFIG_VERSION ||
        Config->PayloadSize < sizeof(LEP_BOOTSTRAP_PAYLOAD) ||
        Config->PayloadSize > LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE ||
        Config->Size < FIELD_OFFSET(LEP_BROKER_CONFIG, Payload) + Config->PayloadSize ||
        !LepValidateBootstrapPayload((PLEP_BOOTSTRAP_PAYLOAD)Config->Payload) ||
        ((PLEP_BOOTSTRAP_PAYLOAD)Config->Payload)->TotalSize != Config->PayloadSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        BrokerRecordStage(Config, LepBrokerStageValidateConfig, Status);
        goto BROKER_FINISH_NEW;
    }
    BrokerRecordStage(Config, LepBrokerStageValidateConfig, STATUS_SUCCESS);

    if (Config->ProcessHandleValue != 0)
    {
        if ((ULONGLONG)(ULONG_PTR)Config->ProcessHandleValue != Config->ProcessHandleValue)
        {
            Status = STATUS_INVALID_HANDLE;
            BrokerRecordStage(Config, LepBrokerStageAcceptHandle, Status);
            goto BROKER_FINISH_NEW;
        }
        Process = (HANDLE)(ULONG_PTR)Config->ProcessHandleValue;
        BrokerRecordStage(Config, LepBrokerStageAcceptHandle, STATUS_SUCCESS);
    }
    else
    {
        Process = OpenProcess(LEP_BROKER_PROCESS_ACCESS, FALSE, ProcessId);
        Status = Process != nullptr ? STATUS_SUCCESS : ML_NTSTATUS_FROM_WIN32(GetLastError());
        BrokerRecordStage(Config, LepBrokerStageOpenProcess, Status);
        if (NT_FAILED(Status))
            goto BROKER_FINISH_NEW;
    }

    NTSTATUS AccessStatus = NtQueryObject(Process, ObjectBasicInformation, &ProcessInfo, sizeof(ProcessInfo), nullptr);
    BrokerRecordStage(Config, LepBrokerStageQueryAccess, AccessStatus);
    if (NT_SUCCESS(AccessStatus))
    {
        // The second OBJECT_BASIC_INFORMATION field is the granted mask.
        Config->GrantedAccess = ProcessInfo.DesiredAccess;
        ACCESS_MASK MissingAccess = LEP_BROKER_PROCESS_ACCESS & ~Config->GrantedAccess;
        BrokerFormat(Log, countof(Log), L"targetPid=%u handle=%p source=%ws granted=%08X required=%08X missing=%08X",
                     ProcessId, Process, Config->ProcessHandleValue != 0 ? L"created-handle" : L"OpenProcess",
                     Config->GrantedAccess, LEP_BROKER_PROCESS_ACCESS, MissingAccess);
        BrokerLog(Log);
        if (MissingAccess != 0)
        {
            Status = STATUS_ACCESS_DENIED;
            BrokerRecordStage(Config, LepBrokerStageQueryAccess, Status);
            goto BROKER_FINISH_NEW;
        }
    }

    DWORD ActualProcessId = GetProcessId(Process);
    Status = ActualProcessId == ProcessId ? STATUS_SUCCESS :
        (ActualProcessId == 0 ? ML_NTSTATUS_FROM_WIN32(GetLastError()) : STATUS_INVALID_PARAMETER);
    BrokerRecordStage(Config, LepBrokerStageValidateTarget, Status);
    if (NT_FAILED(Status))
        goto BROKER_FINISH_NEW;

    Status = LepInjectSuspendedProcess(
        Process,
        (PLEP_BOOTSTRAP_PAYLOAD)Config->Payload,
        Config->PayloadSize,
        Config->InjectionFlags,
        Config
    );

BROKER_FINISH_NEW:
    BrokerFormat(Log, countof(Log), L"injection result=%08X stage=%ws targetPid=%u granted=%08X",
                 Status, Config != nullptr ? LepBrokerStageName(Config->Stage) : L"mapping",
                 ProcessId, Config != nullptr ? Config->GrantedAccess : 0);
    BrokerLog(Log);
    if (Config != nullptr)
        Config->Result = (ULONG)Status;
    if (View != nullptr)
        UnmapViewOfFile(View);
    if (Mapping != nullptr)
        NtClose(Mapping);
    if (Process != nullptr)
        NtClose(Process);
    return Status;
}

static BOOL BrokerBuildArchitecturePath(PCWSTR Source, BOOL TargetWow64, PWSTR Destination, ULONG Capacity)
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

static BOOL BrokerBuildLoaderPath(PCWSTR CorePath, BOOL TargetWow64, PWSTR Destination, ULONG Capacity)
{
    static const WCHAR NameX86[] = L"LoaderDll_x86.dll";
    static const WCHAR NameX64[] = L"LoaderDll_x64.dll";
    PCWSTR Name = TargetWow64 ? NameX86 : NameX64;
    ULONG_PTR Length = StrLengthW(CorePath), End = Length;
    ULONG_PTR NameLength = StrLengthW(Name);

    while (End != 0 && CorePath[End - 1] != L'\\')
        --End;
    if (End + NameLength + 1 > Capacity)
        return FALSE;
    CopyMemory(Destination, CorePath, End * sizeof(WCHAR));
    CopyMemory(Destination + End, Name, (NameLength + 1) * sizeof(WCHAR));
    return TRUE;
}

static NTSTATUS LepElevationInjectCrossArchitecture(
    HANDLE TargetProcess,
    ULONG TargetProcessId,
    ULONG TargetThreadId,
    PLEP_BOOTSTRAP_PAYLOAD SourcePayload,
    BOOL CurrentWow64,
    BOOL TargetWow64,
    ULONG InjectionFlags
)
{
    PCLEP_RUNTIME_ENVIRONMENT Environment = LepBootstrapEnvironment(SourcePayload);
    PCWSTR SourceCorePath = LepBootstrapDllFullPath(SourcePayload);
    WCHAR CorePath[MAX_NTPATH], DirPath[MAX_NTPATH], LoaderPath[MAX_NTPATH];
    WCHAR WindowsPath[MAX_NTPATH], RundllPath[MAX_NTPATH], MappingName[128], CommandLine[2048];
    PLEP_BOOTSTRAP_PAYLOAD TargetPayload = nullptr;
    ULONG TargetPayloadSize = 0;
    HANDLE Mapping = nullptr;
    PLEP_BROKER_CONFIG BrokerConfig = nullptr;
    STARTUPINFOW StartupInfo = {};
    PROCESS_INFORMATION BrokerPi = {};
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (Environment == nullptr || SourceCorePath == nullptr)
        return STATUS_INVALID_PARAMETER;
    if (!BrokerBuildArchitecturePath(SourceCorePath, TargetWow64, CorePath, countof(CorePath)) ||
        !BrokerBuildLoaderPath(CorePath, TargetWow64, LoaderPath, countof(LoaderPath)))
        return STATUS_OBJECT_PATH_INVALID;
    if (GetFileAttributesW(CorePath) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(LoaderPath) == INVALID_FILE_ATTRIBUTES)
        return STATUS_DLL_NOT_FOUND;

    ULONG_PTR CoreLength = StrLengthW(CorePath);
    if (CoreLength + 1 > countof(DirPath))
        return STATUS_NAME_TOO_LONG;
    CopyMemory(DirPath, CorePath, (CoreLength + 1) * sizeof(WCHAR));
    for (ULONG_PTR Index = CoreLength; Index != 0; --Index)
    {
        if (DirPath[Index - 1] == L'\\')
        {
            DirPath[Index] = 0;
            break;
        }
    }

    TargetPayloadSize = LepBootstrapPayloadSize(Environment->Size, CorePath, DirPath);
    if (TargetPayloadSize == 0)
        return STATUS_BUFFER_OVERFLOW;
    TargetPayload = (PLEP_BOOTSTRAP_PAYLOAD)AllocateMemory(TargetPayloadSize);
    if (TargetPayload == nullptr)
        return STATUS_NO_MEMORY;
    Status = LepBuildBootstrapPayload(TargetPayload, TargetPayloadSize, Environment,
                                      Environment->Size, CorePath, DirPath);
    if (NT_FAILED(Status))
    {
        FreeMemory(TargetPayload);
        return Status;
    }

    ULONG_PTR ConfigSize = FIELD_OFFSET(LEP_BROKER_CONFIG, Payload) + TargetPayloadSize;
    if (ConfigSize > 0x7FFFFFFF)
    {
        FreeMemory(TargetPayload);
        return STATUS_BUFFER_OVERFLOW;
    }

    LoaderFormatW(MappingName, countof(MappingName), L"Local\\LEP_EBROKER_%u_%u",
                  TargetProcessId, (ULONG)GetCurrentProcessId());
    DWORD ConfigHigh = 0;
#if ML_AMD64
    ConfigHigh = (DWORD)(ConfigSize >> 32);
#endif
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 ConfigHigh, (DWORD)ConfigSize, MappingName);
    if (Mapping == nullptr)
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
        FreeMemory(TargetPayload);
        return Status;
    }
    BrokerConfig = (PLEP_BROKER_CONFIG)MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, ConfigSize);
    if (BrokerConfig == nullptr)
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
        NtClose(Mapping);
        FreeMemory(TargetPayload);
        return Status;
    }

    ZeroMemory(BrokerConfig, ConfigSize);
    BrokerConfig->Magic = LEP_BROKER_CONFIG_MAGIC;
    BrokerConfig->Version = LEP_BROKER_CONFIG_VERSION;
    BrokerConfig->Size = (ULONG)ConfigSize;
    BrokerConfig->PayloadSize = TargetPayloadSize;
    BrokerConfig->Result = STATUS_UNSUCCESSFUL;
    BrokerConfig->ThreadId = TargetThreadId;
    BrokerConfig->InjectionFlags = InjectionFlags;
    CopyMemory(BrokerConfig->Payload, TargetPayload, TargetPayloadSize);
    FreeMemory(TargetPayload);
    TargetPayload = nullptr;

    if (GetWindowsDirectoryW(WindowsPath, countof(WindowsPath)) == 0)
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
        goto CROSS_FINISH;
    }
    if (TargetWow64)
        LoaderFormatW(RundllPath, countof(RundllPath), L"%s\\SysWOW64\\rundll32.exe", WindowsPath);
    else if (CurrentWow64)
        LoaderFormatW(RundllPath, countof(RundllPath), L"%s\\Sysnative\\rundll32.exe", WindowsPath);
    else
        LoaderFormatW(RundllPath, countof(RundllPath), L"%s\\System32\\rundll32.exe", WindowsPath);

    LoaderFormatW(CommandLine, countof(CommandLine), L"\"%s\" \"%s\",LepBrokerEntry %u|%s",
                  RundllPath, LoaderPath, TargetProcessId, MappingName);
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessW(RundllPath, CommandLine, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                        &StartupInfo, &BrokerPi))
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
        goto CROSS_FINISH;
    }

    HANDLE BrokerHandle = nullptr;
    if (DuplicateHandle(CurrentProcess, TargetProcess, BrokerPi.hProcess, &BrokerHandle,
                        0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        BrokerConfig->ProcessHandleValue = (ULONGLONG)(ULONG_PTR)BrokerHandle;
    }

    Status = NtResumeThread(BrokerPi.hThread, nullptr);
    if (NT_FAILED(Status))
    {
        TerminateProcess(BrokerPi.hProcess, (ULONG)Status);
        goto CROSS_FINISH;
    }

    WaitForSingleObject(BrokerPi.hProcess, INFINITE);
    Status = (NTSTATUS)BrokerConfig->Result;

CROSS_FINISH:
    if (BrokerPi.hThread != nullptr)
        NtClose(BrokerPi.hThread);
    if (BrokerPi.hProcess != nullptr)
        NtClose(BrokerPi.hProcess);
    if (BrokerConfig != nullptr)
        UnmapViewOfFile(BrokerConfig);
    if (Mapping != nullptr)
        NtClose(Mapping);
    return Status;
}

static BOOL ElevationValidateOptionalString(
    PLEP_ELEVATION_CONFIG Config,
    ULONG Offset,
    ULONG Length,
    PCWSTR* OutString
)
{
    *OutString = nullptr;
    if (Offset == 0)
        return Length == 0;
    if (Offset < FIELD_OFFSET(LEP_ELEVATION_CONFIG, Data) ||
        (Length & (sizeof(WCHAR) - 1)) != 0 ||
        Length > Config->Size - sizeof(WCHAR) ||
        !LepPayloadRangeValid(Offset, Length + sizeof(WCHAR), Config->Size))
        return FALSE;
    PCWSTR Value = (PCWSTR)PtrAdd(Config, Offset);
    if (Value[Length / sizeof(WCHAR)] != 0)
        return FALSE;
    *OutString = Value;
    return TRUE;
}

static VOID ElevationRecordStage(PLEP_ELEVATION_CONFIG Config, ULONG Stage, ULONG Win32Error, NTSTATUS InjectStatus)
{
    WCHAR Log[256];
    if (Config != nullptr)
        Config->Stage = Stage;
    BrokerFormat(Log, countof(Log), L"elevation stage=%ws win32=%u inject=%08X",
                 LepElevationStageName(Stage), Win32Error, InjectStatus);
    BrokerLog(Log);
}

static DWORD LepElevationBrokerLaunch(LPCWSTR MappingName)
{
    HANDLE Mapping = nullptr;
    PVOID View = nullptr;
    PLEP_ELEVATION_CONFIG Config = nullptr;
    PWSTR MutableCommandLine = nullptr;
    STARTUPINFOW StartupInfo = {};
    PROCESS_INFORMATION TargetPi = {};
    HANDLE CallerProcess = nullptr;
    HANDLE CallerTargetProcess = nullptr;
    HANDLE CallerTargetThread = nullptr;
    DWORD Win32Error = ERROR_GEN_FAILURE;
    NTSTATUS InjectStatus = STATUS_UNSUCCESSFUL;
    WCHAR Log[512];

    Mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, MappingName);
    if (Mapping == nullptr)
    {
        Win32Error = GetLastError();
        BrokerFormat(Log, countof(Log), L"elevation open-mapping failed=%u", Win32Error);
        BrokerLog(Log);
        return Win32Error;
    }
    View = MapViewOfFile(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (View == nullptr)
    {
        Win32Error = GetLastError();
        BrokerFormat(Log, countof(Log), L"elevation map-view failed=%u", Win32Error);
        BrokerLog(Log);
        NtClose(Mapping);
        return Win32Error;
    }

    Config = (PLEP_ELEVATION_CONFIG)View;
    Config->Win32Error = ERROR_GEN_FAILURE;
    Config->InjectionStatus = (ULONG)STATUS_UNSUCCESSFUL;
    Config->TargetProcessHandle = 0;
    Config->TargetThreadHandle = 0;
    Config->TargetProcessId = 0;
    Config->TargetThreadId = 0;

    PCWSTR ApplicationName = nullptr;
    PCWSTR CommandLine = nullptr;
    PCWSTR CurrentDirectory = nullptr;
    PCWSTR Desktop = nullptr;
    PCWSTR Title = nullptr;
    PLEP_BOOTSTRAP_PAYLOAD Payload = nullptr;

    if (Config->Magic != LEP_ELEVATION_CONFIG_MAGIC ||
        Config->Version != LEP_ELEVATION_CONFIG_VERSION ||
        Config->Size < FIELD_OFFSET(LEP_ELEVATION_CONFIG, Data) ||
        Config->Size > LEP_ELEVATION_CONFIG_MAX_SIZE ||
        Config->CallerProcessId == 0 ||
        Config->PayloadOffset < FIELD_OFFSET(LEP_ELEVATION_CONFIG, Data) ||
        Config->PayloadSize < sizeof(LEP_BOOTSTRAP_PAYLOAD) ||
        Config->PayloadSize > LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE ||
        !LepPayloadRangeValid(Config->PayloadOffset, Config->PayloadSize, Config->Size) ||
        !ElevationValidateOptionalString(Config, Config->ApplicationOffset, Config->ApplicationLength, &ApplicationName) ||
        !ElevationValidateOptionalString(Config, Config->CommandLineOffset, Config->CommandLineLength, &CommandLine) ||
        !ElevationValidateOptionalString(Config, Config->CurrentDirectoryOffset, Config->CurrentDirectoryLength, &CurrentDirectory) ||
        !ElevationValidateOptionalString(Config, Config->DesktopOffset, Config->DesktopLength, &Desktop) ||
        !ElevationValidateOptionalString(Config, Config->TitleOffset, Config->TitleLength, &Title))
    {
        Win32Error = ERROR_INVALID_PARAMETER;
        ElevationRecordStage(Config, LepElevationStageValidateConfig, Win32Error, InjectStatus);
        goto ELEV_FINISH;
    }

    Payload = (PLEP_BOOTSTRAP_PAYLOAD)PtrAdd(Config, Config->PayloadOffset);
    if (!LepValidateBootstrapPayload(Payload) || Payload->TotalSize != Config->PayloadSize)
    {
        Win32Error = ERROR_INVALID_PARAMETER;
        ElevationRecordStage(Config, LepElevationStageValidateConfig, Win32Error, InjectStatus);
        goto ELEV_FINISH;
    }
    ElevationRecordStage(Config, LepElevationStageValidateConfig, ERROR_SUCCESS, STATUS_SUCCESS);

    if (CommandLine != nullptr)
    {
        ULONG CommandBytes = Config->CommandLineLength + sizeof(WCHAR);
        MutableCommandLine = (PWSTR)AllocateMemory(CommandBytes);
        if (MutableCommandLine == nullptr)
        {
            Win32Error = ERROR_OUTOFMEMORY;
            ElevationRecordStage(Config, LepElevationStageCreateProcess, Win32Error, InjectStatus);
            goto ELEV_FINISH;
        }
        CopyMemory(MutableCommandLine, CommandLine, CommandBytes);
    }

    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.lpDesktop = (LPWSTR)Desktop;
    StartupInfo.lpTitle = (LPWSTR)Title;
    StartupInfo.dwX = Config->StartupX;
    StartupInfo.dwY = Config->StartupY;
    StartupInfo.dwXSize = Config->StartupXSize;
    StartupInfo.dwYSize = Config->StartupYSize;
    StartupInfo.dwXCountChars = Config->StartupXCountChars;
    StartupInfo.dwYCountChars = Config->StartupYCountChars;
    StartupInfo.dwFillAttribute = Config->StartupFillAttribute;
    StartupInfo.wShowWindow = Config->StartupShowWindow;
    const ULONG SafeStartupMask =
        STARTF_USESHOWWINDOW | STARTF_USESIZE | STARTF_USEPOSITION |
        STARTF_USECOUNTCHARS | STARTF_USEFILLATTRIBUTE | STARTF_RUNFULLSCREEN |
        STARTF_FORCEONFEEDBACK | STARTF_FORCEOFFFEEDBACK | 0x00000400; // STARTF_USEMONITOR
    StartupInfo.dwFlags = Config->StartupFlags & SafeStartupMask;
    if ((StartupInfo.dwFlags & 0x00000400) != 0)
        StartupInfo.hStdOutput = (HANDLE)(ULONG_PTR)Config->StartupMonitorHandle;

    BOOL CallerRequestedSuspended = (Config->CreationFlags & CREATE_SUSPENDED) != 0;
    ULONG EffectiveCreationFlags = (Config->CreationFlags & ~EXTENDED_STARTUPINFO_PRESENT) | CREATE_SUSPENDED;

    if (!CreateProcessW(ApplicationName, MutableCommandLine, nullptr, nullptr, FALSE,
                        EffectiveCreationFlags, nullptr, CurrentDirectory, &StartupInfo, &TargetPi))
    {
        Win32Error = GetLastError();
        ElevationRecordStage(Config, LepElevationStageCreateProcess, Win32Error, InjectStatus);
        goto ELEV_FINISH;
    }
    ElevationRecordStage(Config, LepElevationStageCreateProcess, ERROR_SUCCESS, STATUS_SUCCESS);

    BOOL CurrentWow64 = Ps::IsWow64Process(CurrentProcess);
    BOOL TargetWow64 = Ps::IsWow64Process(TargetPi.hProcess);
    if (CurrentWow64 != TargetWow64)
    {
        Config->CrossArchBrokerUsed = 1;
        InjectStatus = LepElevationInjectCrossArchitecture(
            TargetPi.hProcess, TargetPi.dwProcessId, TargetPi.dwThreadId,
            Payload, CurrentWow64, TargetWow64, LEP_INJECT_FULL);
    }
    else
    {
        InjectStatus = LepInjectSuspendedProcess(
            TargetPi.hProcess, Payload, Config->PayloadSize, LEP_INJECT_FULL, nullptr);
    }
    Config->InjectionStatus = (ULONG)InjectStatus;
    ElevationRecordStage(Config, LepElevationStageInjectTarget, ERROR_SUCCESS, InjectStatus);

    CallerProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, Config->CallerProcessId);
    if (CallerProcess == nullptr)
    {
        Win32Error = GetLastError();
        ElevationRecordStage(Config, LepElevationStageOpenCaller, Win32Error, InjectStatus);
        TerminateProcess(TargetPi.hProcess, Win32Error);
        goto ELEV_FINISH;
    }
    ElevationRecordStage(Config, LepElevationStageOpenCaller, ERROR_SUCCESS, InjectStatus);

    if (!DuplicateHandle(CurrentProcess, TargetPi.hProcess, CallerProcess, &CallerTargetProcess,
                         LEP_ELEVATION_CALLER_PROCESS_ACCESS, FALSE, 0) ||
        !DuplicateHandle(CurrentProcess, TargetPi.hThread, CallerProcess, &CallerTargetThread,
                         LEP_ELEVATION_CALLER_THREAD_ACCESS, FALSE, 0))
    {
        Win32Error = GetLastError();
        ElevationRecordStage(Config, LepElevationStageDuplicateHandles, Win32Error, InjectStatus);
        if (CallerTargetProcess != nullptr)
            DuplicateHandle(CallerProcess, CallerTargetProcess, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
        if (CallerTargetThread != nullptr)
            DuplicateHandle(CallerProcess, CallerTargetThread, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
        TerminateProcess(TargetPi.hProcess, Win32Error);
        goto ELEV_FINISH;
    }

    Config->TargetProcessHandle = (ULONGLONG)(ULONG_PTR)CallerTargetProcess;
    Config->TargetThreadHandle = (ULONGLONG)(ULONG_PTR)CallerTargetThread;
    Config->TargetProcessId = TargetPi.dwProcessId;
    Config->TargetThreadId = TargetPi.dwThreadId;
    ElevationRecordStage(Config, LepElevationStageDuplicateHandles, ERROR_SUCCESS, InjectStatus);

    if (!CallerRequestedSuspended)
    {
        if (ResumeThread(TargetPi.hThread) == (DWORD)-1)
        {
            Win32Error = GetLastError();
            ElevationRecordStage(Config, LepElevationStageResumeTarget, Win32Error, InjectStatus);
            TerminateProcess(TargetPi.hProcess, Win32Error);
            DuplicateHandle(CallerProcess, CallerTargetProcess, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
            DuplicateHandle(CallerProcess, CallerTargetThread, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
            Config->TargetProcessHandle = 0;
            Config->TargetThreadHandle = 0;
            Config->TargetProcessId = 0;
            Config->TargetThreadId = 0;
            goto ELEV_FINISH;
        }
        ElevationRecordStage(Config, LepElevationStageResumeTarget, ERROR_SUCCESS, InjectStatus);
    }

    Win32Error = ERROR_SUCCESS;
    Config->Win32Error = ERROR_SUCCESS;
    ElevationRecordStage(Config, LepElevationStageComplete, Win32Error, InjectStatus);

ELEV_FINISH:
    if (Config != nullptr && Win32Error != ERROR_SUCCESS)
        Config->Win32Error = Win32Error;
    BrokerFormat(Log, countof(Log), L"elevation result win32=%u inject=%08X targetPid=%u crossArch=%u",
                 Win32Error, InjectStatus, TargetPi.dwProcessId,
                 Config != nullptr ? Config->CrossArchBrokerUsed : 0);
    BrokerLog(Log);
    if (MutableCommandLine != nullptr)
        FreeMemory(MutableCommandLine);
    if (CallerProcess != nullptr)
        NtClose(CallerProcess);
    if (TargetPi.hThread != nullptr)
        NtClose(TargetPi.hThread);
    if (TargetPi.hProcess != nullptr)
        NtClose(TargetPi.hProcess);
    if (View != nullptr)
        UnmapViewOfFile(View);
    if (Mapping != nullptr)
        NtClose(Mapping);
    return Win32Error;
}


void CALLBACK LepBrokerEntry(HWND Window, HINSTANCE Instance, LPSTR CommandLine, int ShowCommand)
{
    LPCSTR Cursor = CommandLine;
    ULONG ProcessId;
    WCHAR MappingW[128];
    NTSTATUS Status;
    UNREFERENCED_PARAMETER(Window);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(ShowCommand);

    ProcessId = BrokerParseUIntA(Cursor);
    if (!BrokerReadTokenW(Cursor, MappingW, countof(MappingW)))
    {
        BrokerLog(L"invalid command line");
        return;
    }
    Status = LepBrokerInject(ProcessId, MappingW);
    WCHAR Log[128];
    BrokerFormat(Log, countof(Log), L"finish status=%08X", Status);
    BrokerLog(Log);
}

void CALLBACK LepElevationBrokerEntry(HWND Window, HINSTANCE Instance, LPSTR CommandLine, int ShowCommand)
{
    LPCSTR Cursor = CommandLine;
    WCHAR MappingW[128];
    DWORD Error;
    UNREFERENCED_PARAMETER(Window);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(ShowCommand);

    if (!BrokerReadTokenW(Cursor, MappingW, countof(MappingW)))
    {
        BrokerLog(L"elevation invalid command line");
        return;
    }
    Error = LepElevationBrokerLaunch(MappingW);
    WCHAR Log[128];
    BrokerFormat(Log, countof(Log), L"elevation finish win32=%u", Error);
    BrokerLog(Log);
}

#endif

#if ML_AMD64
#define LEP_CORE_DLL_NAME L"LocaleEmulatorPlus_x64.dll"
#else
#define LEP_CORE_DLL_NAME L"LocaleEmulatorPlus_x86.dll"
#endif


// Inject the core DLL into a freshly created suspended process.  The first
// loader-side LdrLoadDll call enters the
// mapped LoadFirstDll trampoline, which restores the original bytes and runs
// LEP initialization before continuing the loader call.
static NTSTATUS LepCreateProcessWithHook(
	PLEPB                   EnvironmentBlock,
	PCWSTR                  DllPath,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine,
	PCWSTR                  CurrentDirectory,
	ULONG                   CreationFlags,
	LPSTARTUPINFOW          StartupInfo,
	LPPROCESS_INFORMATION   ProcessInformation,
	LPSECURITY_ATTRIBUTES   ProcessAttributes,
	LPSECURITY_ATTRIBUTES   ThreadAttributes,
	PVOID                   Environment,
	HANDLE                  Token
)
{
	PLEP_BOOTSTRAP_PAYLOAD Payload = nullptr;
	ULONG PayloadSize = 0;
	NTSTATUS Status = LepCreateBootstrapPayload(EnvironmentBlock, DllPath, &Payload, &PayloadSize);
	if (NT_FAILED(Status))
		return Status;

	PROCESS_INFORMATION ProcInfo = {};
	PVOID LocalImage = nullptr;
	PVOID RemoteImage = nullptr;
	PVOID LdrLoadDllAddress = nullptr;
	ULONG LdrLoadDllProtect = 0;
	BOOL LdrLoadDllWritable = FALSE;
	BYTE Backup[LDR_LOAD_DLL_BACKUP_SIZE];

	Status = Ps::CreateProcess(ApplicationName, CommandLine, CurrentDirectory,
		CreationFlags | CREATE_SUSPENDED, StartupInfo, &ProcInfo,
		ProcessAttributes, ThreadAttributes, Environment, Token);
	if (NT_FAILED(Status))
	{
		FreeMemory(Payload);
		return Status;
	}

	LdrLoadDllAddress = EATLookupRoutineByHashPNoFix(GetNtdllHandle(), lep::HashExportName("LdrLoadDll"));
	if (LdrLoadDllAddress == nullptr)
	{
		Status = STATUS_ENTRYPOINT_NOT_FOUND;
		goto FAIL_NEW;
	}

	Status = LoadPeImage(DllPath, &LocalImage, nullptr, 0);
	if (NT_FAILED(Status))
		goto FAIL_NEW;
	ULONG_PTR ImageSize = ImageGetSizeOfImage(LocalImage);
	if (ImageSize > LEP_BOOTSTRAP_IMAGE_MAX_SIZE)
	{
		Status = STATUS_BUFFER_OVERFLOW;
		goto FAIL_NEW;
	}
	ULONG_PTR PayloadOffset = ROUND_UP(ImageSize, 16);
	Status = AllocVirtualMemoryEx(ProcInfo.hProcess, &RemoteImage, PayloadOffset + PayloadSize);
	if (NT_FAILED(Status))
		goto FAIL_NEW;

	Status = ReadMemory(ProcInfo.hProcess, LdrLoadDllAddress, Backup, sizeof(Backup));
	if (NT_FAILED(Status))
		goto FAIL_NEW;
	Status = ProtectVirtualMemory(LdrLoadDllAddress, sizeof(Backup), PAGE_EXECUTE_READWRITE,
		&LdrLoadDllProtect, ProcInfo.hProcess);
	if (NT_FAILED(Status))
		goto FAIL_NEW;
	LdrLoadDllWritable = TRUE;

	RelocPeImage(LocalImage, LocalImage, nullptr, RemoteImage);
	Status = LepFillFirstDllBootstrapData(LocalImage, LdrLoadDllAddress, Backup,
		LdrLoadDllProtect, LEP_INJECT_FULL, (ULONG)PayloadOffset, PayloadSize);
	if (NT_FAILED(Status))
		goto FAIL_NEW;
	Status = WriteMemory(ProcInfo.hProcess, RemoteImage, LocalImage, ImageSize);
	if (NT_SUCCESS(Status))
		Status = WriteMemory(ProcInfo.hProcess, PtrAdd(RemoteImage, PayloadOffset), Payload, PayloadSize);
	if (NT_FAILED(Status))
		goto FAIL_NEW;

	PVOID LocalLoadFirstDll = LookupExportTable(LocalImage, "LoadFirstDll");
	if (LocalLoadFirstDll == nullptr)
	{
		Status = STATUS_ENTRYPOINT_NOT_FOUND;
		goto FAIL_NEW;
	}
	PVOID RemoteLoadFirstDll = PtrAdd(RemoteImage, PtrOffset(LocalLoadFirstDll, LocalImage));
	BYTE Jump[16] = {};
#if ML_AMD64
	Jump[0] = 0xFF;
	Jump[1] = 0x25;
	*(PVOID*)&Jump[6] = RemoteLoadFirstDll;
#else
	Jump[0] = JUMP;
	*(LONG*)&Jump[1] = (LONG)PtrOffset(RemoteLoadFirstDll, PtrAdd(LdrLoadDllAddress, 5));
#endif
	Status = WriteMemory(ProcInfo.hProcess, LdrLoadDllAddress, Jump, sizeof(Backup));
	if (NT_FAILED(Status))
		goto FAIL_NEW;

	UnloadPeImage(LocalImage);
	FreeMemory(Payload);
	if (FLAG_OFF(CreationFlags, CREATE_SUSPENDED))
		Status = NtResumeProcess(ProcInfo.hProcess);
	if (NT_FAILED(Status))
		goto FAIL_NEW_NO_LOCAL;

	if (ProcessInformation != nullptr)
		*ProcessInformation = ProcInfo;
	else
	{
		NtClose(ProcInfo.hProcess);
		NtClose(ProcInfo.hThread);
	}
	return STATUS_SUCCESS;

FAIL_NEW:
	if (Payload != nullptr)
		FreeMemory(Payload);
	if (LocalImage != nullptr)
		UnloadPeImage(LocalImage);
FAIL_NEW_NO_LOCAL:
	if (LdrLoadDllWritable)
	{
		ULONG IgnoredProtect;
		ProtectVirtualMemory(LdrLoadDllAddress, sizeof(Backup), LdrLoadDllProtect,
			&IgnoredProtect, ProcInfo.hProcess);
	}
	if (RemoteImage != nullptr)
		Mm::FreeVirtualMemory(RemoteImage, ProcInfo.hProcess);
	NtTerminateProcess(ProcInfo.hProcess, Status);
	NtClose(ProcInfo.hProcess);
	NtClose(ProcInfo.hThread);
	return Status;
}


EXTC
NTSTATUS
NTAPI
LepCreateProcess2(
	PLEPB                   EnvironmentBlock,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine,
	PCWSTR                  CurrentDirectory,
	ULONG                   CreationFlags,
	LPSTARTUPINFOW          StartupInfo,
	LPPROCESS_INFORMATION   ProcessInformation,
	LPSECURITY_ATTRIBUTES   ProcessAttributes,
	LPSECURITY_ATTRIBUTES   ThreadAttributes,
	PVOID                   Environment,
	HANDLE                  Token
	)
{
	ULONG_PTR               Length;
	PWSTR                   DllFullPath;
	PLDR_MODULE             Module;
	NTSTATUS                Status;
	PROCESS_INFORMATION     ProcessInfo;

	static WCHAR Dll[] = LEP_CORE_DLL_NAME;

	Module = FindLdrModuleByHandle(&__ImageBase);

	Length = Module->FullDllName.Length - Module->BaseDllName.Length;
	DllFullPath = (PWSTR)AllocStack(Length + sizeof(Dll));
	CopyMemory(DllFullPath, Module->FullDllName.Buffer, Length);
	CopyStruct(PtrAdd(DllFullPath, Length), Dll, sizeof(Dll));

	    Status = LepCreateProcessWithHook(
		EnvironmentBlock,
        DllFullPath,
        ApplicationName,
        CommandLine,
        CurrentDirectory,
        CreationFlags | CREATE_SUSPENDED,
        StartupInfo,
        &ProcessInfo,
        ProcessAttributes,
        ThreadAttributes,
		Environment,
		Token
    );

	if (NT_FAILED(Status))
		return Status;

	if (NT_SUCCESS(Status) && FLAG_OFF(CreationFlags, CREATE_SUSPENDED))
		Status = NtResumeProcess(ProcessInfo.hProcess);

	if (NT_FAILED(Status))
	{
		NtTerminateProcess(ProcessInfo.hProcess, Status);
		NtClose(ProcessInfo.hProcess);
		NtClose(ProcessInfo.hThread);
	}
	else if (ProcessInformation != nullptr)
	{
		*ProcessInformation = ProcessInfo;
	}
	else
	{
		NtClose(ProcessInfo.hProcess);
		NtClose(ProcessInfo.hThread);
	}

	return Status;
}

EXTC
NTSTATUS
NTAPI
LepCreateProcess(
	PLEPB                   EnvironmentBlock,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine,
	PCWSTR                  CurrentDirectory,
	ULONG                   CreationFlags,
	LPSTARTUPINFOW          StartupInfo,
	LPPROCESS_INFORMATION   ProcessInformation,
	LPSECURITY_ATTRIBUTES   ProcessAttributes,
	LPSECURITY_ATTRIBUTES   ThreadAttributes,
	PVOID                   Environment,
	HANDLE                  Token
	)
{
	NTSTATUS                Status;
	PVOID                   LepDllHandle;
	ULONG_PTR               Length;
	PWSTR                   DllFullPath;
	PLDR_MODULE             Module;
	PLEP_BOOTSTRAP_PAYLOAD  Payload;
	ULONG                   PayloadSize;

	static WCHAR Dll[] = LEP_CORE_DLL_NAME;

	Module = FindLdrModuleByHandle(&__ImageBase);

	Length = Module->FullDllName.Length - Module->BaseDllName.Length;
	DllFullPath = (PWSTR)AllocStack(Length + sizeof(Dll));
	CopyMemory(DllFullPath, Module->FullDllName.Buffer, Length);
	CopyStruct(PtrAdd(DllFullPath, Length), Dll, sizeof(Dll));

	Payload = nullptr;
	PayloadSize = 0;
	Status = LepCreateBootstrapPayload(EnvironmentBlock, DllFullPath, &Payload, &PayloadSize);
	FAIL_RETURN(Status);

	UNICODE_STRING DllFullPathString;
	lep::ActiveFrame frame(LEP_LOADER_PROCESS);

	frame.Data = (ULONG_PTR)Payload;
	frame.Push();

	RtlInitUnicodeString(&DllFullPathString, DllFullPath);

	Status = LdrLoadDll(nullptr, nullptr, &DllFullPathString, &LepDllHandle);
	if (frame.Data != 0)
	{
		FreeMemory((PVOID)frame.Data);
		frame.Data = 0;
	}
	if (NT_FAILED(Status))
	{
		return Status;
	}

	Status = Ps::CreateProcess(
		ApplicationName,
		CommandLine,
		CurrentDirectory,
		CreationFlags,
		StartupInfo,
		ProcessInformation,
		ProcessAttributes,
		ThreadAttributes,
		Environment,
		Token
		);

	return Status;
}
