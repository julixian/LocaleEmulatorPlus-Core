#pragma comment(linker, "/SECTION:.text,ERW /MERGE:.rdata=.text /MERGE:.data=.text")
#pragma comment(linker, "/SECTION:.Asuna,ERW /MERGE:.text=.Asuna")

#include "ml.cpp"
#include "LoaderDll.h"

#if LEP_LOADER_DLL

static HANDLE g_BrokerLog = INVALID_HANDLE_VALUE;

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

static NTSTATUS LepBrokerInject(ULONG ProcessId, LPCWSTR MappingName)
{
    HANDLE Process, Mapping, Thread;
    PLEP_BROKER_CONFIG Config;
    PLEPPEB LepPeb;
    PVOID View, LdrLoadDllAddress, LocalSelfShadow, SelfShadow, ShadowLoadFirstDll;
    BYTE LdrLoadDllBackup[LDR_LOAD_DLL_BACKUP_SIZE];
    DWORD Error;
    ULONG_PTR Length, SizeOfImage;
    NTSTATUS Status;
    WCHAR Log[512];
    LPCWSTR DllPath;
    BYTE Jump[16];

    BrokerFormat(Log, countof(Log), L"begin pid=%u map=%s", ProcessId, MappingName);
    BrokerLog(Log);

    Process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessId);
    if (Process == nullptr)
    {
        BrokerFormat(Log, countof(Log), L"OpenProcess failed=%u", GetLastError());
        BrokerLog(Log);
        return ML_NTSTATUS_FROM_WIN32(GetLastError());
    }

    Mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, MappingName);
    if (Mapping == nullptr)
    {
        BrokerFormat(Log, countof(Log), L"OpenFileMapping failed=%u", GetLastError());
        BrokerLog(Log);
        NtClose(Process);
        return ML_NTSTATUS_FROM_WIN32(GetLastError());
    }

    View = MapViewOfFile(Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (View == nullptr)
    {
        BrokerFormat(Log, countof(Log), L"MapViewOfFile failed=%u", GetLastError());
        BrokerLog(Log);
        NtClose(Mapping);
        NtClose(Process);
        return ML_NTSTATUS_FROM_WIN32(GetLastError());
    }

    Config = (PLEP_BROKER_CONFIG)View;
    Config->Result = STATUS_UNSUCCESSFUL;
    if (Config->Magic != LEP_BROKER_CONFIG_MAGIC || Config->Version != LEP_BROKER_CONFIG_VERSION ||
        Config->Size < FIELD_OFFSET(LEP_BROKER_CONFIG, Environment) + sizeof(LEPB) ||
        Config->ExtraSize > 0x100000 ||
        Config->Size < FIELD_OFFSET(LEP_BROKER_CONFIG, Environment) + sizeof(LEPB) + Config->ExtraSize)
    {
        BrokerLog(L"invalid config header");
        UnmapViewOfFile(View);
        NtClose(Mapping);
        NtClose(Process);
        return STATUS_INVALID_PARAMETER;
    }

    // Paths are transported only in the Unicode shared configuration.
    DllPath = Config->DllPath;
    if (DllPath[0] == 0)
    {
        BrokerLog(L"missing dll path");
        UnmapViewOfFile(View);
        NtClose(Mapping);
        NtClose(Process);
        return STATUS_INVALID_PARAMETER;
    }
    BrokerFormat(Log, countof(Log), L"config ok size=%u extra=%u dll=%s",
                 Config->Size, Config->ExtraSize, DllPath);
    BrokerLog(Log);
    BrokerFormat(Log, countof(Log), L"injection flags=%08X", Config->InjectionFlags);
    BrokerLog(Log);
    BrokerFormat(Log, countof(Log), L"target thread id=%u", Config->ThreadId);
    BrokerLog(Log);
    if (Config->Environment.NumberOfRegistryRedirectionEntries != 0)
    {
        PREGISTRY_REDIRECTION_ENTRY64 Entry64 = &Config->Environment.RegistryReplacement[0];
        BrokerFormat(Log, countof(Log), L"config entry0 subkey=%I64X/%u value=%I64X/%u redir=%I64X/%u",
                     (ULONG64)Entry64->Original.SubKey.Buffer, Entry64->Original.SubKey.Length,
                     (ULONG64)Entry64->Original.ValueName.Buffer, Entry64->Original.ValueName.Length,
                     (ULONG64)Entry64->Redirected.SubKey.Buffer, Entry64->Redirected.SubKey.Length);
        BrokerLog(Log);
    }

    LepPeb = OpenOrCreateLepPeb(ProcessId, TRUE, Config->ExtraSize);
    if (LepPeb == nullptr)
    {
        BrokerLog(L"OpenOrCreateLepPeb failed");
        UnmapViewOfFile(View);
        NtClose(Mapping);
        NtClose(Process);
        return STATUS_NONE_MAPPED;
    }

    CopyMemory(&LepPeb->LEPB, &Config->Environment, sizeof(LEPB) + Config->ExtraSize);
    Length = (StrLengthW(DllPath) + 1) * sizeof(WCHAR);
    ZeroMemory(LepPeb->LepDllFullPath, sizeof(LepPeb->LepDllFullPath));
    ZeroMemory(LepPeb->LepDllDirPath, sizeof(LepPeb->LepDllDirPath));
    CopyMemory(LepPeb->LepDllFullPath, DllPath, ML_MIN(Length, sizeof(LepPeb->LepDllFullPath) - sizeof(WCHAR)));
    CopyMemory(LepPeb->LepDllDirPath, DllPath, ML_MIN(Length, sizeof(LepPeb->LepDllDirPath) - sizeof(WCHAR)));
    for (ULONG_PTR i = StrLengthW(LepPeb->LepDllDirPath); i != 0; --i)
    {
        if (LepPeb->LepDllDirPath[i - 1] == L'\\')
        {
            LepPeb->LepDllDirPath[i] = 0;
            break;
        }
    }

    // Same-architecture descendants use these fields to install the early
    // LdrLoadDll trampoline.  The cross-architecture broker used to omit
    // them, so the first descendant failed after its image was copied.
    LdrLoadDllAddress = EATLookupRoutineByHashPNoFix(GetNtdllHandle(), NTDLL_LdrLoadDll);
    LepPeb->LdrLoadDllAddress = LdrLoadDllAddress;
    LepPeb->LdrLoadDllBackupSize = LDR_LOAD_DLL_BACKUP_SIZE;
    LepPeb->InjectionFlags = Config->InjectionFlags;
    Status = ReadMemory(Process, LdrLoadDllAddress, LdrLoadDllBackup, LDR_LOAD_DLL_BACKUP_SIZE);
    BrokerFormat(Log, countof(Log), L"LdrLoadDll metadata address=%I64X read=%08X",
                 (ULONG64)(ULONG_PTR)LdrLoadDllAddress, Status);
    BrokerLog(Log);
    if (NT_FAILED(Status))
    {
        CloseLepPeb(LepPeb);
        UnmapViewOfFile(View);
        NtClose(Mapping);
        NtClose(Process);
        return Status;
    }
    CopyMemory(LepPeb->LdrLoadDllBackup, LdrLoadDllBackup, LDR_LOAD_DLL_BACKUP_SIZE);

    CloseLepPeb(LepPeb);

    Thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME |
                        THREAD_QUERY_INFORMATION, FALSE, Config->ThreadId);
    if (Thread == nullptr)
    {
        Error = GetLastError();
        BrokerFormat(Log, countof(Log), L"OpenThread failed error=%u tid=%u", Error, Config->ThreadId);
        BrokerLog(Log);
        UnmapViewOfFile(View);
        NtClose(Mapping);
        NtClose(Process);
        return ML_NTSTATUS_FROM_WIN32(Error);
    }

    // The target is already suspended by the creator hook. Install the same
    // first-LdrLoadDll trampoline used by same-architecture propagation.
    LocalSelfShadow = nullptr;
    Status = LoadPeImage(DllPath, &LocalSelfShadow, nullptr, 0);
    BrokerFormat(Log, countof(Log), L"load core shadow status=%08X base=%I64X",
                 Status, (ULONG64)(ULONG_PTR)LocalSelfShadow);
    BrokerLog(Log);
    if (NT_FAILED(Status))
        goto BROKER_FINISH;

    SizeOfImage = ImageGetSizeOfImage(LocalSelfShadow);
    SelfShadow = nullptr;
    Status = AllocVirtualMemoryEx(Process, &SelfShadow, SizeOfImage);
    BrokerFormat(Log, countof(Log), L"alloc core shadow status=%08X base=%I64X size=%I64X",
                 Status, (ULONG64)(ULONG_PTR)SelfShadow, (ULONG64)SizeOfImage);
    BrokerLog(Log);
    if (NT_FAILED(Status))
        goto BROKER_FINISH;

    RelocPeImage(LocalSelfShadow, LocalSelfShadow, nullptr, SelfShadow);
    Status = WriteMemory(Process, SelfShadow, LocalSelfShadow, SizeOfImage);
    BrokerFormat(Log, countof(Log), L"write core shadow status=%08X", Status);
    BrokerLog(Log);
    if (NT_FAILED(Status))
        goto BROKER_FINISH;

    ShadowLoadFirstDll = LookupExportTable(LocalSelfShadow, "LoadFirstDll");
    if (ShadowLoadFirstDll == nullptr)
    {
        Status = STATUS_ENTRYPOINT_NOT_FOUND;
        BrokerLog(L"LoadFirstDll export missing");
        goto BROKER_FINISH;
    }
    ShadowLoadFirstDll = PtrAdd(SelfShadow, PtrOffset(ShadowLoadFirstDll, LocalSelfShadow));
    ZeroMemory(Jump, sizeof(Jump));
#if ML_AMD64
    Jump[0] = 0xFF;
    Jump[1] = 0x25;
    *(PVOID *)&Jump[6] = ShadowLoadFirstDll;
#else
    Jump[0] = JUMP;
    *(LONG *)&Jump[1] = PtrOffset(ShadowLoadFirstDll, PtrAdd(LdrLoadDllAddress, 5));
#endif
    Status = WriteProtectMemory(Process, LdrLoadDllAddress, Jump, LDR_LOAD_DLL_BACKUP_SIZE);
    BrokerFormat(Log, countof(Log), L"install first-LdrLoadDll trampoline status=%08X target=%I64X shadow=%I64X",
                 Status, (ULONG64)(ULONG_PTR)LdrLoadDllAddress,
                 (ULONG64)(ULONG_PTR)ShadowLoadFirstDll);
    BrokerLog(Log);

BROKER_FINISH:
    if (LocalSelfShadow != nullptr)
        UnloadPeImage(LocalSelfShadow);
    Config->Result = (ULONG)Status;
    NtClose(Thread);
    UnmapViewOfFile(View);
    NtClose(Mapping);
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

typedef struct LEP_CREATE_PROCESS2_CONTEXT
{
	PLEPB    EnvironmentBlock;
	PWSTR   DllFullPath;
	PLDR_MODULE Module;
} LEP_CREATE_PROCESS2_CONTEXT, *PLE_CREATE_PROCESS2_CONTEXT;

typedef NTSTATUS (*PLEP_PREPARE_CALLBACK)(PML_PROCESS_INFORMATION ProcessInformation, PVOID Context);

static NTSTATUS LepPrepareRemoteLepPeb(PML_PROCESS_INFORMATION ProcessInfo, PVOID Context)
{
	PLE_CREATE_PROCESS2_CONTEXT CreateContext;
	PLEPPEB LEPPEB;
	NTSTATUS Status;
	ULONG_PTR ExtraSize;
	PVOID MaximumAddress;
	PREGISTRY_REDIRECTION_ENTRY64 Entry64;
	PLEPB EnvironmentBlock;

	CreateContext = (PLE_CREATE_PROCESS2_CONTEXT)Context;
	EnvironmentBlock = CreateContext->EnvironmentBlock;
	LEPPEB = nullptr;
	Status = STATUS_SUCCESS;

	LOOP_ONCE
	{
		if (EnvironmentBlock == nullptr)
			break;

		ExtraSize = EnvironmentBlock->NumberOfRegistryRedirectionEntries * sizeof(EnvironmentBlock->RegistryReplacement[0]);

		if (ExtraSize != 0)
		{
			MaximumAddress = EnvironmentBlock->RegistryReplacement + EnvironmentBlock->NumberOfRegistryRedirectionEntries;
			FOR_EACH(Entry64, EnvironmentBlock->RegistryReplacement, EnvironmentBlock->NumberOfRegistryRedirectionEntries)
			{
				MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.SubKey.Buffer,      EnvironmentBlock),   Entry64->Original.SubKey.MaximumLength));
				MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.ValueName.Buffer,   EnvironmentBlock),   Entry64->Original.ValueName.MaximumLength));
				MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Original.Data,        EnvironmentBlock),   Entry64->Original.DataSize));

				MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.SubKey.Buffer,    EnvironmentBlock),   Entry64->Redirected.SubKey.MaximumLength));
				MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.ValueName.Buffer, EnvironmentBlock),   Entry64->Redirected.ValueName.MaximumLength));
				MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Redirected.Data,      EnvironmentBlock),   Entry64->Redirected.DataSize));
			}

			ExtraSize += PtrOffset(MaximumAddress, EnvironmentBlock);
		}

		LEPPEB = OpenOrCreateLepPeb(ProcessInfo->dwProcessId, TRUE, ExtraSize);
		if (LEPPEB == nullptr)
		{
			Status = STATUS_NONE_MAPPED;
			break;
		}

		CopyMemory(&LEPPEB->LEPB, EnvironmentBlock, FIELD_OFFSET(LEPB, NumberOfRegistryRedirectionEntries) + ExtraSize);

			LEPPEB->LdrLoadDllAddress = ProcessInfo->FirstCallLdrLoadDll;
			LEPPEB->LdrLoadDllBackupSize = LDR_LOAD_DLL_BACKUP_SIZE;
			ReadMemory(ProcessInfo->hProcess, LEPPEB->LdrLoadDllAddress, LEPPEB->LdrLoadDllBackup, LDR_LOAD_DLL_BACKUP_SIZE);

		ULONG_PTR Length = (StrLengthW(CreateContext->DllFullPath) + 1) * sizeof(WCHAR);
		CopyMemory(LEPPEB->LepDllFullPath, CreateContext->DllFullPath, ML_MIN(Length, sizeof(LEPPEB->LepDllFullPath)));

		Length = CreateContext->Module->FullDllName.Length + sizeof(WCHAR) - CreateContext->Module->BaseDllName.Length;
		Length = ML_MIN(Length, sizeof(LEPPEB->LepDllDirPath));
		CopyMemory(LEPPEB->LepDllDirPath, CreateContext->Module->FullDllName.Buffer, Length);
		LEPPEB->LepDllDirPath[Length / sizeof(WCHAR) - 1] = 0;
	}

	CloseLepPeb(LEPPEB);
	return Status;
}

// Inject the core DLL into a freshly created suspended process.  The first
// loader-side LdrLoadDll call enters the
// mapped LoadFirstDll trampoline, which restores the original bytes and runs
// LEP initialization before continuing the loader call.
static NTSTATUS LepCreateProcessWithHook(
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
	HANDLE                  Token,
	PLEP_PREPARE_CALLBACK   PrepareCallback,
	PVOID                   PrepareContext
)
{
	ML_PROCESS_INFORMATION ProcInfo;
	NTSTATUS Status;
	PVOID LocalImage = nullptr;
	PVOID RemoteImage = nullptr;
	PVOID RemoteLoadFirstDll;
	PVOID LdrLoadDllAddress;
	BYTE Jump[16];
	ULONG_PTR ImageSize;

	Status = CreateProcess(ApplicationName, CommandLine, CurrentDirectory,
		CreationFlags | CREATE_SUSPENDED, StartupInfo, &ProcInfo,
		ProcessAttributes, ThreadAttributes, Environment, Token);
	if (NT_FAILED(Status))
		return Status;

	// ntdll is loaded before the initial thread can execute.  Same-architecture
	// processes use the same export address, matching child propagation.
	LdrLoadDllAddress = EATLookupRoutineByHashPNoFix(GetNtdllHandle(), NTDLL_LdrLoadDll);
	ProcInfo.FirstCallLdrLoadDll = LdrLoadDllAddress;
	if (PrepareCallback != nullptr)
	{
		Status = PrepareCallback(&ProcInfo, PrepareContext);
		if (NT_FAILED(Status))
			goto FAIL;
	}

	Status = LoadPeImage(DllPath, &LocalImage, nullptr, 0);
	if (NT_FAILED(Status))
		goto FAIL;

	ImageSize = ImageGetSizeOfImage(LocalImage);
	Status = AllocVirtualMemoryEx(ProcInfo.hProcess, &RemoteImage, ImageSize);
	if (NT_FAILED(Status))
		goto FAIL;

	RelocPeImage(LocalImage, LocalImage, nullptr, RemoteImage);
	Status = WriteMemory(ProcInfo.hProcess, RemoteImage, LocalImage, ImageSize);
	if (NT_FAILED(Status))
		goto FAIL;

	RemoteLoadFirstDll = LookupExportTable(LocalImage, "LoadFirstDll");
	if (RemoteLoadFirstDll == nullptr)
	{
		Status = STATUS_ENTRYPOINT_NOT_FOUND;
		goto FAIL;
	}
	RemoteLoadFirstDll = PtrAdd(RemoteImage, PtrOffset(RemoteLoadFirstDll, LocalImage));

	ZeroMemory(Jump, sizeof(Jump));
#if ML_AMD64
	Jump[0] = 0xFF;
	Jump[1] = 0x25;
	*(PVOID *)&Jump[6] = RemoteLoadFirstDll;
#else
	Jump[0] = JUMP;
	*(LONG *)&Jump[1] = (LONG)PtrOffset(RemoteLoadFirstDll, PtrAdd(LdrLoadDllAddress, 5));
#endif
	Status = WriteProtectMemory(ProcInfo.hProcess, LdrLoadDllAddress,
		Jump, LDR_LOAD_DLL_BACKUP_SIZE);
	if (NT_FAILED(Status))
		goto FAIL;

	UnloadPeImage(LocalImage);
	LocalImage = nullptr;

	if (FLAG_OFF(CreationFlags, CREATE_SUSPENDED))
		Status = NtResumeProcess(ProcInfo.hProcess);
	if (NT_FAILED(Status))
		goto FAIL;

	if (ProcessInformation != nullptr)
		*ProcessInformation = ProcInfo;
	else
	{
		NtClose(ProcInfo.hProcess);
		NtClose(ProcInfo.hThread);
	}
	return STATUS_SUCCESS;

FAIL:
	if (LocalImage != nullptr)
		UnloadPeImage(LocalImage);
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
	LEP_CREATE_PROCESS2_CONTEXT CreateContext;

	static WCHAR Dll[] = LEP_CORE_DLL_NAME;

	Module = FindLdrModuleByHandle(&__ImageBase);

	Length = Module->FullDllName.Length - Module->BaseDllName.Length;
	DllFullPath = (PWSTR)AllocStack(Length + sizeof(Dll));
	CopyMemory(DllFullPath, Module->FullDllName.Buffer, Length);
	CopyStruct(PtrAdd(DllFullPath, Length), Dll, sizeof(Dll));

	CreateContext.EnvironmentBlock = EnvironmentBlock;
	CreateContext.DllFullPath = DllFullPath;
	CreateContext.Module = Module;

    Status = LepCreateProcessWithHook(
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
        Token,
        LepPrepareRemoteLepPeb,
        &CreateContext
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
	PLEPPEB                  LEPPEB;

	static WCHAR Dll[] = LEP_CORE_DLL_NAME;

	Module = FindLdrModuleByHandle(&__ImageBase);

	Length = Module->FullDllName.Length - Module->BaseDllName.Length;
	DllFullPath = (PWSTR)AllocStack(Length + sizeof(Dll));
	CopyMemory(DllFullPath, Module->FullDllName.Buffer, Length);
	CopyStruct(PtrAdd(DllFullPath, Length), Dll, sizeof(Dll));

	LEPPEB = nullptr;

	LOOP_ONCE
	{
		if (EnvironmentBlock == nullptr)
		break;

	ULONG_PTR   ExtraSize;
	PVOID       MaximumAddress;
	PREGISTRY_REDIRECTION_ENTRY64 Entry64;

	ExtraSize = EnvironmentBlock->NumberOfRegistryRedirectionEntries * sizeof(EnvironmentBlock->RegistryReplacement[0]);

	if (ExtraSize != 0)
	{
		MaximumAddress = EnvironmentBlock->RegistryReplacement + EnvironmentBlock->NumberOfRegistryRedirectionEntries;
		FOR_EACH(Entry64, EnvironmentBlock->RegistryReplacement, EnvironmentBlock->NumberOfRegistryRedirectionEntries)
		{
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.SubKey.Buffer,      EnvironmentBlock),   Entry64->Original.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.ValueName.Buffer,   EnvironmentBlock),   Entry64->Original.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Original.Data,        EnvironmentBlock),   Entry64->Original.DataSize));

			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.SubKey.Buffer,    EnvironmentBlock),   Entry64->Redirected.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.ValueName.Buffer, EnvironmentBlock),   Entry64->Redirected.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Redirected.Data,      EnvironmentBlock),   Entry64->Redirected.DataSize));
		}

		ExtraSize += PtrOffset(MaximumAddress, EnvironmentBlock);
	}

	LEPPEB = OpenOrCreateLepPeb((ULONG_PTR)CurrentTeb()->ClientId.UniqueProcess, TRUE, ExtraSize);
	if (LEPPEB == nullptr)
	{
		Status = STATUS_NONE_MAPPED;
		break;
	}

	CopyMemory(&LEPPEB->LEPB, EnvironmentBlock, FIELD_OFFSET(LEPB, NumberOfRegistryRedirectionEntries) + ExtraSize);

	LEPPEB->LdrLoadDllAddress = LookupExportTable(GetNtdllHandle(), NTDLL_LdrLoadDll);
	LEPPEB->LdrLoadDllBackupSize = LDR_LOAD_DLL_BACKUP_SIZE;
	CopyMemory(LEPPEB->LdrLoadDllBackup, LEPPEB->LdrLoadDllAddress, LDR_LOAD_DLL_BACKUP_SIZE);

	ULONG_PTR Length = (StrLengthW(DllFullPath) + 1) * sizeof(WCHAR);
	CopyMemory(LEPPEB->LepDllFullPath, DllFullPath, ML_MIN(Length, sizeof(LEPPEB->LepDllFullPath)));

  Length = Module->FullDllName.Length + sizeof(WCHAR) - Module->BaseDllName.Length;

  Length = ML_MIN(Length, sizeof(LEPPEB->LepDllFullPath));
  CopyMemory(LEPPEB->LepDllDirPath, Module->FullDllName.Buffer, Length);
  LEPPEB->LepDllDirPath[Length / sizeof(WCHAR) - 1] = 0;
	}

	UNICODE_STRING DllFullPathString;
	TEB_ACTIVE_FRAME frame(LEP_LOADER_PROCESS);

	frame.Data = (ULONG_PTR)LEPPEB;
	frame.Push();

	RtlInitUnicodeString(&DllFullPathString, DllFullPath);

	Status = LdrLoadDll(nullptr, nullptr, &DllFullPathString, &LepDllHandle);
	CloseLepPeb(LEPPEB);

	FAIL_RETURN(Status);

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
