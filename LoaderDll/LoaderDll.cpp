#pragma comment(linker, "/SECTION:.text,ERW /MERGE:.rdata=.text /MERGE:.data=.text")
#pragma comment(linker, "/SECTION:.Asuna,ERW /MERGE:.text=.Asuna")

#include "ml.cpp"
#include "LoaderDll.h"

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

		LEPPEB->LdrLoadDllAddress = GetCallDestination(ProcessInfo->FirstCallLdrLoadDll);
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

	Status = CreateProcessWithDll(
		CPWD_BEFORE_KERNEL32,
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
