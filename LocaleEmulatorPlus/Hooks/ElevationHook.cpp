#include "stdafx.h"
#include "ElevationLocator.h"
#include "../../LoaderDll/BrokerProtocol.h"

#define LEP_ELEVATION_LAUNCH_CONTEXT TAG4('LELV')

namespace
{
    volatile LONG g_ElevationSequence = 0;
    PVOID g_Shell32ModuleBase = nullptr;
    PVOID g_WindowsStorageModuleBase = nullptr;

    typedef ULONG (NTAPI *PFN_AIC_LAUNCH_ADMIN_PROCESS_STDCALL)(
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        ULONG AicFlags,
        ULONG CreationFlags,
        PCWSTR CurrentDirectory,
        HWND Window,
        LPSTARTUPINFOW StartupInfo,
        LPPROCESS_INFORMATION ProcessInformation,
        PULONG ElevationReason
    );

#if !ML_AMD64
    typedef ULONG (FASTCALL *PFN_AIC_LAUNCH_ADMIN_PROCESS_FASTCALL)(
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        ULONG AicFlags,
        ULONG CreationFlags,
        PCWSTR CurrentDirectory,
        HWND Window,
        LPSTARTUPINFOW StartupInfo,
        LPPROCESS_INFORMATION ProcessInformation,
        PULONG ElevationReason
    );
#endif

    ULONG FormatBufferW(PWSTR Buffer, ULONG Capacity, PCWSTR Format, ...)
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

    BOOL BuildCurrentLoaderPath(PCWSTR CorePath, PWSTR Destination, ULONG Capacity)
    {
#if ML_AMD64
        static const WCHAR LoaderName[] = L"LoaderDll_x64.dll";
#else
        static const WCHAR LoaderName[] = L"LoaderDll_x86.dll";
#endif
        if (CorePath == nullptr)
            return FALSE;
        ULONG_PTR Length = StrLengthW(CorePath);
        ULONG_PTR End = Length;
        ULONG_PTR NameLength = CONST_STRLEN(LoaderName);

        while (End != 0 && CorePath[End - 1] != L'\\')
            --End;
        if (End + NameLength + 1 > Capacity)
            return FALSE;

        CopyMemory(Destination, CorePath, End * sizeof(WCHAR));
        CopyMemory(Destination + End, LoaderName, (NameLength + 1) * sizeof(WCHAR));
        return TRUE;
    }

    BOOL BuildElevatedRundllPath(BOOL CurrentWow64, PWSTR Destination, ULONG Capacity)
    {
        WCHAR WindowsPath[MAX_NTPATH];
        if (GetWindowsDirectoryW(WindowsPath, countof(WindowsPath)) == 0)
            return FALSE;

        // AppInfo runs as a native service and cannot resolve Wow64's Sysnative
        // alias. Use explicit SysWOW64 for 32-bit Wow64 callers and System32
        // for native x64 or native 32-bit OS callers.
#if ML_AMD64
        UNREFERENCED_PARAMETER(CurrentWow64);
        PCWSTR Suffix = L"\\System32\\rundll32.exe";
#else
        PCWSTR Suffix = CurrentWow64 ? L"\\SysWOW64\\rundll32.exe" : L"\\System32\\rundll32.exe";
#endif
        ULONG Written = FormatBufferW(Destination, Capacity, L"%s%s", WindowsPath, Suffix);
        return Written != 0 && Written + 1 < Capacity;
    }

    ULONG CallOriginalAicLaunchAdminProcess(
        PLepGlobalData GlobalData,
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        ULONG AicFlags,
        ULONG CreationFlags,
        PCWSTR CurrentDirectory,
        HWND Window,
        LPSTARTUPINFOW StartupInfo,
        LPPROCESS_INFORMATION ProcessInformation,
        PULONG ElevationReason
    )
    {
        PVOID Stub = GlobalData->HookStub.StubAicLaunchAdminProcess;
        if (Stub == nullptr)
            return ERROR_PROC_NOT_FOUND;

#if ML_AMD64
        return ((PFN_AIC_LAUNCH_ADMIN_PROCESS_STDCALL)Stub)(
            ApplicationName, CommandLine, AicFlags, CreationFlags,
            CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
#else
        if (GlobalData->HookRoutineData.Elevation.RegisterArguments)
        {
            return ((PFN_AIC_LAUNCH_ADMIN_PROCESS_FASTCALL)Stub)(
                ApplicationName, CommandLine, AicFlags, CreationFlags,
                CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
        }
        return ((PFN_AIC_LAUNCH_ADMIN_PROCESS_STDCALL)Stub)(
            ApplicationName, CommandLine, AicFlags, CreationFlags,
            CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
#endif
    }

    VOID MeasureOptionalString(PCWSTR Value, ULONG* OutBytes, ULONG64* TotalSize)
    {
        *OutBytes = 0;
        if (Value == nullptr)
            return;
        ULONG64 Bytes = (ULONG64)StrLengthW(Value) * sizeof(WCHAR);
        if (Bytes > LEP_ELEVATION_CONFIG_MAX_SIZE)
        {
            *TotalSize = LEP_ELEVATION_CONFIG_MAX_SIZE + 1;
            return;
        }
        *OutBytes = (ULONG)Bytes;
        *TotalSize = ROUND_UP(*TotalSize, sizeof(WCHAR)) + Bytes + sizeof(WCHAR);
    }

    VOID WriteOptionalString(
        PLEP_ELEVATION_CONFIG Config,
        ULONG* Cursor,
        PCWSTR Value,
        ULONG Bytes,
        ULONG* OutOffset,
        ULONG* OutLength
    )
    {
        *OutOffset = 0;
        *OutLength = 0;
        if (Value == nullptr)
            return;

        *Cursor = ROUND_UP(*Cursor, sizeof(WCHAR));
        *OutOffset = *Cursor;
        *OutLength = Bytes;
        CopyMemory(PtrAdd(Config, *Cursor), Value, Bytes + sizeof(WCHAR));
        *Cursor += Bytes + sizeof(WCHAR);
    }

    ULONG HandleAicLaunchAdminProcess(
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        ULONG AicFlags,
        ULONG CreationFlags,
        PCWSTR CurrentDirectory,
        HWND Window,
        LPSTARTUPINFOW StartupInfo,
        LPPROCESS_INFORMATION ProcessInformation,
        PULONG ElevationReason
    )
    {
        PLepGlobalData GlobalData = LepGetGlobalData();
        if (GlobalData == nullptr || GlobalData->HookStub.StubAicLaunchAdminProcess == nullptr)
            return ERROR_PROC_NOT_FOUND;

        if (lep::ActiveFrame::Find(LEP_ELEVATION_LAUNCH_CONTEXT) != nullptr)
        {
            return CallOriginalAicLaunchAdminProcess(
                GlobalData, ApplicationName, CommandLine, AicFlags, CreationFlags,
                CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
        }

        WCHAR LoaderPath[MAX_NTPATH];
        WCHAR RundllPath[MAX_NTPATH];
        if (!BuildCurrentLoaderPath(GlobalData->GetLepDllFullPath(), LoaderPath, countof(LoaderPath)) ||
            GetFileAttributesW(LoaderPath) == INVALID_FILE_ATTRIBUTES ||
            !BuildElevatedRundllPath(GlobalData->IsWow64(), RundllPath, countof(RundllPath)) ||
            GetFileAttributesW(RundllPath) == INVALID_FILE_ATTRIBUTES)
        {
            WriteLog(L"elevation fallback: loader or rundll32 missing");
            return CallOriginalAicLaunchAdminProcess(
                GlobalData, ApplicationName, CommandLine, AicFlags, CreationFlags,
                CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
        }

        PLEP_BOOTSTRAP_PAYLOAD Payload = nullptr;
        ULONG PayloadSize = 0;
        NTSTATUS Status = GlobalData->BuildBootstrapPayload(
            GlobalData->GetLepDllFullPath(), &Payload, &PayloadSize);
        if (NT_FAILED(Status))
        {
            WriteLog(L"elevation fallback: payload build failed status=%08X", Status);
            return CallOriginalAicLaunchAdminProcess(
                GlobalData, ApplicationName, CommandLine, AicFlags, CreationFlags,
                CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
        }

        WCHAR CurrentDirFallback[MAX_NTPATH];
        PCWSTR EffectiveCurrentDirectory = CurrentDirectory;
        if (EffectiveCurrentDirectory == nullptr)
        {
            DWORD DirChars = GetCurrentDirectoryW(countof(CurrentDirFallback), CurrentDirFallback);
            if (DirChars != 0 && DirChars < countof(CurrentDirFallback))
                EffectiveCurrentDirectory = CurrentDirFallback;
        }

        PCWSTR Desktop = StartupInfo != nullptr ? StartupInfo->lpDesktop : nullptr;
        PCWSTR Title = StartupInfo != nullptr ? StartupInfo->lpTitle : nullptr;

        ULONG AppBytes = 0, CmdBytes = 0, DirBytes = 0, DesktopBytes = 0, TitleBytes = 0;
        ULONG64 TotalSize = FIELD_OFFSET(LEP_ELEVATION_CONFIG, Data);
        MeasureOptionalString(ApplicationName, &AppBytes, &TotalSize);
        MeasureOptionalString(CommandLine, &CmdBytes, &TotalSize);
        MeasureOptionalString(EffectiveCurrentDirectory, &DirBytes, &TotalSize);
        MeasureOptionalString(Desktop, &DesktopBytes, &TotalSize);
        MeasureOptionalString(Title, &TitleBytes, &TotalSize);
        TotalSize = ROUND_UP(TotalSize, 8) + PayloadSize;

        if (TotalSize > LEP_ELEVATION_CONFIG_MAX_SIZE)
        {
            FreeMemoryP(Payload);
            WriteLog(L"elevation fallback: request too large size=%I64u", TotalSize);
            return CallOriginalAicLaunchAdminProcess(
                GlobalData, ApplicationName, CommandLine, AicFlags, CreationFlags,
                CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
        }

        WCHAR MappingName[128];
        LONG Sequence = _InterlockedIncrement(&g_ElevationSequence);
        FormatBufferW(MappingName, countof(MappingName), L"Local\\LEP_ELEV_%u_%u",
                      (ULONG)GetCurrentProcessId(), (ULONG)Sequence);

        HANDLE Mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)TotalSize, MappingName);
        if (Mapping == nullptr)
        {
            DWORD Error = GetLastError();
            FreeMemoryP(Payload);
            WriteLog(L"elevation fallback: CreateFileMapping failed=%u", Error);
            return CallOriginalAicLaunchAdminProcess(
                GlobalData, ApplicationName, CommandLine, AicFlags, CreationFlags,
                CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
        }

        PLEP_ELEVATION_CONFIG Config = (PLEP_ELEVATION_CONFIG)MapViewOfFile(
            Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (SIZE_T)TotalSize);
        if (Config == nullptr)
        {
            DWORD Error = GetLastError();
            CloseHandle(Mapping);
            FreeMemoryP(Payload);
            WriteLog(L"elevation fallback: MapViewOfFile failed=%u", Error);
            return CallOriginalAicLaunchAdminProcess(
                GlobalData, ApplicationName, CommandLine, AicFlags, CreationFlags,
                CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
        }

        ZeroMemory(Config, (SIZE_T)TotalSize);
        Config->Magic = LEP_ELEVATION_CONFIG_MAGIC;
        Config->Version = LEP_ELEVATION_CONFIG_VERSION;
        Config->Size = (ULONG)TotalSize;
        Config->Stage = LepElevationStageNone;
        Config->Win32Error = ERROR_GEN_FAILURE;
        Config->InjectionStatus = (ULONG)STATUS_UNSUCCESSFUL;
        Config->CallerProcessId = GetCurrentProcessId();
        Config->CreationFlags = CreationFlags;

        if (StartupInfo != nullptr)
        {
            Config->StartupFlags = StartupInfo->dwFlags;
            Config->StartupX = StartupInfo->dwX;
            Config->StartupY = StartupInfo->dwY;
            Config->StartupXSize = StartupInfo->dwXSize;
            Config->StartupYSize = StartupInfo->dwYSize;
            Config->StartupXCountChars = StartupInfo->dwXCountChars;
            Config->StartupYCountChars = StartupInfo->dwYCountChars;
            Config->StartupFillAttribute = StartupInfo->dwFillAttribute;
            Config->StartupShowWindow = StartupInfo->wShowWindow;
            if ((StartupInfo->dwFlags & 0x00000400) != 0)
                Config->StartupMonitorHandle = (ULONGLONG)(ULONG_PTR)StartupInfo->hStdOutput;
        }

        ULONG Cursor = FIELD_OFFSET(LEP_ELEVATION_CONFIG, Data);
        WriteOptionalString(Config, &Cursor, ApplicationName, AppBytes,
                            &Config->ApplicationOffset, &Config->ApplicationLength);
        WriteOptionalString(Config, &Cursor, CommandLine, CmdBytes,
                            &Config->CommandLineOffset, &Config->CommandLineLength);
        WriteOptionalString(Config, &Cursor, EffectiveCurrentDirectory, DirBytes,
                            &Config->CurrentDirectoryOffset, &Config->CurrentDirectoryLength);
        WriteOptionalString(Config, &Cursor, Desktop, DesktopBytes,
                            &Config->DesktopOffset, &Config->DesktopLength);
        WriteOptionalString(Config, &Cursor, Title, TitleBytes,
                            &Config->TitleOffset, &Config->TitleLength);

        Cursor = ROUND_UP(Cursor, 8);
        Config->PayloadOffset = Cursor;
        Config->PayloadSize = PayloadSize;
        CopyMemory(PtrAdd(Config, Cursor), Payload, PayloadSize);
        FreeMemoryP(Payload);
        Payload = nullptr;

        WCHAR BrokerCommandLine[2048];
        FormatBufferW(BrokerCommandLine, countof(BrokerCommandLine),
                      L"\"%s\" \"%s\",LepElevationBrokerEntry %s",
                      RundllPath, LoaderPath, MappingName);

        STARTUPINFOW BrokerStartup = {};
        BrokerStartup.cb = sizeof(BrokerStartup);
        BrokerStartup.dwFlags = STARTF_USESHOWWINDOW;
        BrokerStartup.wShowWindow = SW_HIDE;
        if (Desktop != nullptr)
            BrokerStartup.lpDesktop = (LPWSTR)Desktop;

        PROCESS_INFORMATION BrokerPi = {};
        ULONG BrokerReason = 6;
        // Set bit 0 (explicit runas) because rundll32.exe has an asInvoker
        // manifest even when the original target used requireAdministrator.
        ULONG BrokerAicFlags = AicFlags | 1;
        ULONG BrokerCreationFlags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;

        WriteLog(L"elevation intercept begin aicFlags=%08X createFlags=%08X app=%ws mapping=%ws",
                 AicFlags, CreationFlags,
                 ApplicationName != nullptr ? ApplicationName : L"<null>", MappingName);

        lep::ActiveFrame ElevationFrame(LEP_ELEVATION_LAUNCH_CONTEXT);
        ElevationFrame.Push();
        ULONG LaunchError = CallOriginalAicLaunchAdminProcess(
            GlobalData, RundllPath, BrokerCommandLine, BrokerAicFlags, BrokerCreationFlags,
            nullptr, Window, &BrokerStartup, &BrokerPi, &BrokerReason);
        ElevationFrame.Pop();

        if (ElevationReason != nullptr)
            *ElevationReason = BrokerReason;

        if (ProcessInformation != nullptr)
            ZeroMemory(ProcessInformation, sizeof(*ProcessInformation));

        if (LaunchError != ERROR_SUCCESS)
        {
            WriteLog(L"elevation broker launch error=%u reason=%u", LaunchError, BrokerReason);
            if (BrokerPi.hThread != nullptr)
                CloseHandle(BrokerPi.hThread);
            if (BrokerPi.hProcess != nullptr)
                CloseHandle(BrokerPi.hProcess);
            UnmapViewOfFile(Config);
            CloseHandle(Mapping);
            return LaunchError;
        }

        if (BrokerPi.hProcess != nullptr)
            WaitForSingleObject(BrokerPi.hProcess, INFINITE);

        DWORD BrokerExitCode = 0;
        if (BrokerPi.hProcess != nullptr)
            GetExitCodeProcess(BrokerPi.hProcess, &BrokerExitCode);
        if (BrokerPi.hThread != nullptr)
            CloseHandle(BrokerPi.hThread);
        if (BrokerPi.hProcess != nullptr)
            CloseHandle(BrokerPi.hProcess);

        ULONG ResultStage = Config->Stage;
        ULONG ResultError = Config->Win32Error;
        ULONG InjectStatus = Config->InjectionStatus;
        ULONGLONG RawProcessHandle = Config->TargetProcessHandle;
        ULONGLONG RawThreadHandle = Config->TargetThreadHandle;
        ULONG TargetPid = Config->TargetProcessId;
        ULONG TargetTid = Config->TargetThreadId;
        ULONG CrossArchUsed = Config->CrossArchBrokerUsed;

        UnmapViewOfFile(Config);
        CloseHandle(Mapping);

        HANDLE TargetProcess = ((ULONGLONG)(ULONG_PTR)RawProcessHandle == RawProcessHandle)
            ? (HANDLE)(ULONG_PTR)RawProcessHandle : nullptr;
        HANDLE TargetThread = ((ULONGLONG)(ULONG_PTR)RawThreadHandle == RawThreadHandle)
            ? (HANDLE)(ULONG_PTR)RawThreadHandle : nullptr;

        WriteLog(L"elevation broker complete exit=%08X stage=%ws win32=%u inject=%08X targetPid=%u targetTid=%u crossArch=%u",
                 BrokerExitCode, LepElevationStageName(ResultStage), ResultError,
                 InjectStatus, TargetPid, TargetTid, CrossArchUsed);

        if (ResultStage == LepElevationStageComplete && ResultError == ERROR_SUCCESS)
        {
            if (ProcessInformation != nullptr)
            {
                ProcessInformation->hProcess = TargetProcess;
                ProcessInformation->hThread = TargetThread;
                ProcessInformation->dwProcessId = TargetPid;
                ProcessInformation->dwThreadId = TargetTid;
            }
            else
            {
                if (TargetThread != nullptr)
                    CloseHandle(TargetThread);
                if (TargetProcess != nullptr)
                    CloseHandle(TargetProcess);
            }
            return ERROR_SUCCESS;
        }

        if (TargetThread != nullptr)
            CloseHandle(TargetThread);
        if (TargetProcess != nullptr)
            CloseHandle(TargetProcess);

        return ResultError != ERROR_SUCCESS ? ResultError : ERROR_GEN_FAILURE;
    }

#if ML_AMD64
    ULONG NTAPI LepAicLaunchAdminProcess(
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        ULONG AicFlags,
        ULONG CreationFlags,
        PCWSTR CurrentDirectory,
        HWND Window,
        LPSTARTUPINFOW StartupInfo,
        LPPROCESS_INFORMATION ProcessInformation,
        PULONG ElevationReason
    )
    {
        return HandleAicLaunchAdminProcess(
            ApplicationName, CommandLine, AicFlags, CreationFlags,
            CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
    }
#else
    ULONG FASTCALL LepAicLaunchAdminProcessFastcall(
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        ULONG AicFlags,
        ULONG CreationFlags,
        PCWSTR CurrentDirectory,
        HWND Window,
        LPSTARTUPINFOW StartupInfo,
        LPPROCESS_INFORMATION ProcessInformation,
        PULONG ElevationReason
    )
    {
        return HandleAicLaunchAdminProcess(
            ApplicationName, CommandLine, AicFlags, CreationFlags,
            CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
    }

    ULONG STDCALL LepAicLaunchAdminProcessStdcall(
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        ULONG AicFlags,
        ULONG CreationFlags,
        PCWSTR CurrentDirectory,
        HWND Window,
        LPSTARTUPINFOW StartupInfo,
        LPPROCESS_INFORMATION ProcessInformation,
        PULONG ElevationReason
    )
    {
        return HandleAicLaunchAdminProcess(
            ApplicationName, CommandLine, AicFlags, CreationFlags,
            CurrentDirectory, Window, StartupInfo, ProcessInformation, ElevationReason);
    }
#endif
}

NTSTATUS LepGlobalData::HookElevationRoutines(PVOID ModuleBase)
{
    if (ModuleBase == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (HookStub.StubAicLaunchAdminProcess != nullptr)
        return STATUS_SUCCESS;

    lep::ElevationEntry Entry = lep::FindElevationEntry(ModuleBase);
    if (Entry.Routine == nullptr)
    {
        WriteLog(L"elevation locator no match module=%p", ModuleBase);
        return STATUS_NOT_FOUND;
    }

#if ML_AMD64
    PVOID Replacement = (PVOID)LepAicLaunchAdminProcess;
#else
    PVOID Replacement = Entry.RegisterArguments
        ? (PVOID)LepAicLaunchAdminProcessFastcall
        : (PVOID)LepAicLaunchAdminProcessStdcall;
#endif

    HookRoutineData.Elevation.ModuleBase = ModuleBase;
    HookRoutineData.Elevation.TargetRoutine = Entry.Routine;
    HookRoutineData.Elevation.RegisterArguments = Entry.RegisterArguments;

    Mp::PATCH_MEMORY_DATA Patches[] =
    {
        Mp::FunctionJumpVa(Entry.Routine, Replacement, &HookStub.StubAicLaunchAdminProcess, LEP_FUNCTION_JUMP_OP),
    };

    NTSTATUS Status = Mp::PatchMemory(Patches, countof(Patches));
    if (NT_FAILED(Status))
    {
        HookRoutineData.Elevation.ModuleBase = nullptr;
        HookRoutineData.Elevation.TargetRoutine = nullptr;
        HookRoutineData.Elevation.RegisterArguments = FALSE;
        HookStub.StubAicLaunchAdminProcess = nullptr;
    }

    WriteLog(L"elevation hook module=%p routine=%p regArgs=%u stub=%p status=%08X",
             ModuleBase, Entry.Routine, (ULONG)Entry.RegisterArguments,
             HookStub.StubAicLaunchAdminProcess, Status);
    return Status;
}

NTSTATUS LepGlobalData::UnHookElevationRoutines(PVOID ModuleBase)
{
    if (HookStub.StubAicLaunchAdminProcess == nullptr)
        return STATUS_SUCCESS;

    if (ModuleBase != nullptr && ModuleBase != HookRoutineData.Elevation.ModuleBase)
        return STATUS_SUCCESS;

    Mp::RestoreMemory(HookStub.StubAicLaunchAdminProcess);
    HookStub.StubAicLaunchAdminProcess = nullptr;
    HookRoutineData.Elevation.ModuleBase = nullptr;
    HookRoutineData.Elevation.TargetRoutine = nullptr;
    HookRoutineData.Elevation.RegisterArguments = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS LepGlobalData::HookShell32Routines(PVOID Shell32)
{
    g_Shell32ModuleBase = Shell32;
    return HookElevationRoutines(Shell32);
}

NTSTATUS LepGlobalData::UnHookShell32Routines()
{
    PVOID Base = g_Shell32ModuleBase;
    g_Shell32ModuleBase = nullptr;
    return Base != nullptr ? UnHookElevationRoutines(Base) : STATUS_SUCCESS;
}

NTSTATUS LepGlobalData::HookWindowsStorageRoutines(PVOID WindowsStorage)
{
    g_WindowsStorageModuleBase = WindowsStorage;
    return HookElevationRoutines(WindowsStorage);
}

NTSTATUS LepGlobalData::UnHookWindowsStorageRoutines()
{
    PVOID Base = g_WindowsStorageModuleBase;
    g_WindowsStorageModuleBase = nullptr;
    return Base != nullptr ? UnHookElevationRoutines(Base) : STATUS_SUCCESS;
}
