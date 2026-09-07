#pragma comment(linker, "/SECTION:.text,ERW /MERGE:.rdata=.text /MERGE:.data=.text")
#pragma comment(linker, "/SECTION:.Asuna,ERW /MERGE:.text=.Asuna")

#include "ml.cpp"
#include "LoaderDll.h"

#if LEP_LOADER_DLL

static HANDLE g_BrokerLog = INVALID_HANDLE_VALUE;

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
        Length = GetTempPathW(countof(Path), Path);
        if (Length == 0 || Length >= countof(Path) - 48)
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

    ULONG64 Count = Environment->NumberOfRegistryRedirectionEntries;
    ULONG64 EnvironmentSize64 = FIELD_OFFSET(LEPB, RegistryReplacement) +
                                Count * sizeof(REGISTRY_REDIRECTION_ENTRY64);
    if (Count > 0x10000 || EnvironmentSize64 > LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE)
        return STATUS_INVALID_PARAMETER;
    PREGISTRY_REDIRECTION_ENTRY64 SourceEntry = Environment->RegistryReplacement;
    for (ULONG64 Index = 0; Index != Count; ++Index, ++SourceEntry)
    {
        if (SourceEntry->Original.SubKey.Length > SourceEntry->Original.SubKey.MaximumLength ||
            SourceEntry->Original.ValueName.Length > SourceEntry->Original.ValueName.MaximumLength ||
            SourceEntry->Redirected.SubKey.Length > SourceEntry->Redirected.SubKey.MaximumLength ||
            SourceEntry->Redirected.ValueName.Length > SourceEntry->Redirected.ValueName.MaximumLength ||
            (SourceEntry->Original.SubKey.Length != 0 && SourceEntry->Original.SubKey.Buffer == 0) ||
            (SourceEntry->Original.ValueName.Length != 0 && SourceEntry->Original.ValueName.Buffer == 0) ||
            (SourceEntry->Redirected.SubKey.Length != 0 && SourceEntry->Redirected.SubKey.Buffer == 0) ||
            (SourceEntry->Redirected.ValueName.Length != 0 && SourceEntry->Redirected.ValueName.Buffer == 0) ||
            (SourceEntry->Redirected.DataSize != 0 && SourceEntry->Redirected.Data == nullptr))
        {
            return STATUS_INVALID_PARAMETER;
        }
        EnvironmentSize64 += SourceEntry->Original.SubKey.Length + sizeof(WCHAR);
        EnvironmentSize64 += SourceEntry->Original.ValueName.Length + sizeof(WCHAR);
        EnvironmentSize64 += SourceEntry->Redirected.SubKey.Length + sizeof(WCHAR);
        EnvironmentSize64 += SourceEntry->Redirected.ValueName.Length + sizeof(WCHAR);
        EnvironmentSize64 += SourceEntry->Redirected.DataSize;
        if (EnvironmentSize64 > LEP_BOOTSTRAP_PAYLOAD_MAX_SIZE)
            return STATUS_BUFFER_OVERFLOW;
    }

    ULONG EnvironmentSize = (ULONG)EnvironmentSize64;
    // LoaderDll has no entry point, so its private MemoryAllocator heap is not
    // initialized on this path.  Use the process heap for all bootstrap
    // construction storage.
    PLEPB CanonicalEnvironment = (PLEPB)AllocateMemory(EnvironmentSize);
    if (CanonicalEnvironment == nullptr)
        return STATUS_NO_MEMORY;
    ZeroMemory(CanonicalEnvironment, EnvironmentSize);
    CopyMemory(CanonicalEnvironment, Environment, FIELD_OFFSET(LEPB, NumberOfRegistryRedirectionEntries));
    CanonicalEnvironment->NumberOfRegistryRedirectionEntries = Count;

    PREGISTRY_REDIRECTION_ENTRY64 DestinationEntry = CanonicalEnvironment->RegistryReplacement;
    PBYTE EnvironmentBuffer = (PBYTE)(DestinationEntry + Count);
    auto CopyString = [&] (UNICODE_STRING64& Destination, UNICODE_STRING64& Source)
    {
        Destination.Length = Source.Length;
        Destination.MaximumLength = Source.Length;
        Destination.Dummy = PtrOffset(EnvironmentBuffer, CanonicalEnvironment);
        if (Source.Length != 0)
            CopyMemory(EnvironmentBuffer, PtrAdd(Environment, (ULONG_PTR)Source.Buffer), Source.Length);
        EnvironmentBuffer += Source.Length;
        *(PWCHAR)EnvironmentBuffer = 0;
        EnvironmentBuffer += sizeof(WCHAR);
    };
    SourceEntry = Environment->RegistryReplacement;
    for (ULONG64 Index = 0; Index != Count; ++Index, ++SourceEntry, ++DestinationEntry)
    {
        DestinationEntry->Original.Root = SourceEntry->Original.Root;
        DestinationEntry->Original.DataType = SourceEntry->Original.DataType;
        CopyString(DestinationEntry->Original.SubKey, SourceEntry->Original.SubKey);
        CopyString(DestinationEntry->Original.ValueName, SourceEntry->Original.ValueName);

        DestinationEntry->Redirected.Root = SourceEntry->Redirected.Root;
        DestinationEntry->Redirected.DataType = SourceEntry->Redirected.DataType;
        CopyString(DestinationEntry->Redirected.SubKey, SourceEntry->Redirected.SubKey);
        CopyString(DestinationEntry->Redirected.ValueName, SourceEntry->Redirected.ValueName);
        if (SourceEntry->Redirected.Data != nullptr && SourceEntry->Redirected.DataSize != 0)
        {
            DestinationEntry->Redirected.Data = (PVOID64)PtrOffset(EnvironmentBuffer, CanonicalEnvironment);
            DestinationEntry->Redirected.DataSize = SourceEntry->Redirected.DataSize;
            CopyMemory(EnvironmentBuffer, PtrAdd(Environment, (ULONG_PTR)SourceEntry->Redirected.Data),
                       SourceEntry->Redirected.DataSize);
            EnvironmentBuffer += SourceEntry->Redirected.DataSize;
        }
    }

    WCHAR DirPath[MAX_NTPATH];
    ULONG_PTR Length = StrLengthW(DllPath);
    if (Length + 1 > countof(DirPath))
    {
        FreeMemory(CanonicalEnvironment);
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
        FreeMemory(CanonicalEnvironment);
        return STATUS_BUFFER_OVERFLOW;
    }
    PLEP_BOOTSTRAP_PAYLOAD LocalPayload = (PLEP_BOOTSTRAP_PAYLOAD)AllocateMemory(Required);
    if (LocalPayload == nullptr)
    {
        FreeMemory(CanonicalEnvironment);
        return STATUS_NO_MEMORY;
    }

    NTSTATUS Status = LepBuildBootstrapPayload(LocalPayload, Required, CanonicalEnvironment, EnvironmentSize,
                                      DllPath, DirPath);
    FreeMemory(CanonicalEnvironment);
    if (NT_FAILED(Status))
    {
        FreeMemory(LocalPayload);
        return Status;
    }
    *Payload = LocalPayload;
    *PayloadSize = Required;
    return STATUS_SUCCESS;
}

static NTSTATUS LepBrokerInject(ULONG ProcessId, LPCWSTR MappingName)
{
    HANDLE Process = nullptr;
    HANDLE Mapping = nullptr;
    PVOID View = nullptr;
    PVOID LocalImage = nullptr;
    PVOID RemoteImage = nullptr;
    PVOID LdrLoadDllAddress = nullptr;
    ULONG LdrLoadDllProtect = 0;
    BOOL LdrLoadDllWritable = FALSE;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PLEP_BROKER_CONFIG Config = nullptr;
    BYTE Backup[LDR_LOAD_DLL_BACKUP_SIZE];
    WCHAR Log[512];

    Process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessId);
    if (Process == nullptr)
        return ML_NTSTATUS_FROM_WIN32(GetLastError());

    Mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, MappingName);
    if (Mapping == nullptr)
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
        goto BROKER_FINISH_NEW;
    }
    View = MapViewOfFile(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (View == nullptr)
    {
        Status = ML_NTSTATUS_FROM_WIN32(GetLastError());
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
        goto BROKER_FINISH_NEW;
    }

    PLEP_BOOTSTRAP_PAYLOAD Payload = (PLEP_BOOTSTRAP_PAYLOAD)Config->Payload;
    PCWSTR DllPath = LepBootstrapDllFullPath(Payload);
    Status = LoadPeImage(DllPath, &LocalImage, nullptr, 0);
    if (NT_FAILED(Status))
        goto BROKER_FINISH_NEW;

    ULONG_PTR ImageSize = ImageGetSizeOfImage(LocalImage);
    if (ImageSize > LEP_BOOTSTRAP_IMAGE_MAX_SIZE)
    {
        Status = STATUS_BUFFER_OVERFLOW;
        goto BROKER_FINISH_NEW;
    }
    ULONG_PTR PayloadOffset = ROUND_UP(ImageSize, 16);
    Status = AllocVirtualMemoryEx(Process, &RemoteImage, PayloadOffset + Config->PayloadSize);
    if (NT_FAILED(Status))
        goto BROKER_FINISH_NEW;

    LdrLoadDllAddress = EATLookupRoutineByHashPNoFix(GetNtdllHandle(), NTDLL_LdrLoadDll);
    if (LdrLoadDllAddress == nullptr)
    {
        Status = STATUS_ENTRYPOINT_NOT_FOUND;
        goto BROKER_FINISH_NEW;
    }

    if ((Config->InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) != 0)
    {
        Status = ReadMemory(Process, LdrLoadDllAddress, Backup, sizeof(Backup));
        if (NT_FAILED(Status))
            goto BROKER_FINISH_NEW;
        Status = ProtectVirtualMemory(LdrLoadDllAddress, sizeof(Backup), PAGE_EXECUTE_READWRITE,
                                      &LdrLoadDllProtect, Process);
        if (NT_FAILED(Status))
            goto BROKER_FINISH_NEW;
        LdrLoadDllWritable = TRUE;
    }

    RelocPeImage(LocalImage, LocalImage, nullptr, RemoteImage);
    PLEP_FIRST_DLL_BOOTSTRAP_DATA BootstrapData = (PLEP_FIRST_DLL_BOOTSTRAP_DATA)
        LookupExportTable(LocalImage, LEP_FIRST_DLL_BOOTSTRAP_EXPORT);
    if (BootstrapData == nullptr)
    {
        Status = STATUS_ENTRYPOINT_NOT_FOUND;
        goto BROKER_FINISH_NEW;
    }
    ZeroMemory(BootstrapData, sizeof(*BootstrapData));
    BootstrapData->Magic = LEP_FIRST_DLL_BOOTSTRAP_MAGIC;
    BootstrapData->InjectionFlags = Config->InjectionFlags;
    BootstrapData->PayloadOffset = (ULONG)PayloadOffset;
    BootstrapData->PayloadSize = Config->PayloadSize;
    if ((Config->InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) != 0)
    {
        BootstrapData->BackupSize = sizeof(Backup);
        BootstrapData->OriginalProtect = LdrLoadDllProtect;
        BootstrapData->LdrLoadDllAddress = LdrLoadDllAddress;
        CopyMemory(BootstrapData->Backup, Backup, sizeof(Backup));
    }

    Status = WriteMemory(Process, RemoteImage, LocalImage, ImageSize);
    if (NT_SUCCESS(Status))
        Status = WriteMemory(Process, PtrAdd(RemoteImage, PayloadOffset), Payload, Config->PayloadSize);
    if (NT_FAILED(Status) || (Config->InjectionFlags & LEP_INJECT_PATCH_LDR_LOAD_DLL) == 0)
        goto BROKER_FINISH_NEW;

    PVOID LocalLoadFirstDll = LookupExportTable(LocalImage, "LoadFirstDll");
    if (LocalLoadFirstDll == nullptr)
    {
        Status = STATUS_ENTRYPOINT_NOT_FOUND;
        goto BROKER_FINISH_NEW;
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
    Status = WriteMemory(Process, LdrLoadDllAddress, Jump, sizeof(Backup));
    BrokerFormat(Log, countof(Log), L"metadata injection status=%08X image=%I64X payload=%u",
                 Status, (ULONG64)(ULONG_PTR)RemoteImage, Config->PayloadSize);
    BrokerLog(Log);

BROKER_FINISH_NEW:
    if (Config != nullptr)
        Config->Result = (ULONG)Status;
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
    if (View != nullptr)
        UnmapViewOfFile(View);
    if (Mapping != nullptr)
        NtClose(Mapping);
    if (Process != nullptr)
        NtClose(Process);
    return Status;
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
	PML_PROCESS_INFORMATION ProcessInformation,
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

	ML_PROCESS_INFORMATION ProcInfo = {};
	PVOID LocalImage = nullptr;
	PVOID RemoteImage = nullptr;
	PVOID LdrLoadDllAddress = nullptr;
	ULONG LdrLoadDllProtect = 0;
	BOOL LdrLoadDllWritable = FALSE;
	BYTE Backup[LDR_LOAD_DLL_BACKUP_SIZE];

	Status = CreateProcess(ApplicationName, CommandLine, CurrentDirectory,
		CreationFlags | CREATE_SUSPENDED, StartupInfo, &ProcInfo,
		ProcessAttributes, ThreadAttributes, Environment, Token);
	if (NT_FAILED(Status))
	{
		FreeMemory(Payload);
		return Status;
	}

	LdrLoadDllAddress = EATLookupRoutineByHashPNoFix(GetNtdllHandle(), NTDLL_LdrLoadDll);
	ProcInfo.FirstCallLdrLoadDll = LdrLoadDllAddress;
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
	PLEPB                    EnvironmentBlock,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine,
	PCWSTR                  CurrentDirectory,
	ULONG                   CreationFlags,
	LPSTARTUPINFOW          StartupInfo,
	PML_PROCESS_INFORMATION ProcessInformation,
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
	ML_PROCESS_INFORMATION  ProcessInfo;

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
	PML_PROCESS_INFORMATION ProcessInformation,
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
		PLEP_BOOTSTRAP_PAYLOAD   Payload;
		ULONG                    PayloadSize;

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
	TEB_ACTIVE_FRAME frame(LEP_LOADER_PROCESS);

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
