#include "../Support.h"
#if CPP_DEFINED
ML_NAMESPACE
ML_NAMESPACE_BEGIN(Native)
ML_NAMESPACE_BEGIN(Ps);

using namespace Mm;
using namespace Ldr;

ULONG_PTR HandleToProcessId(HANDLE Process)
{

    NTSTATUS Status;
    PROCESS_BASIC_INFORMATION Basic;

    Status = NtQueryInformationProcess(Process, ProcessBasicInformation, &Basic, sizeof(Basic), nullptr);

    return NT_SUCCESS(Status) ? Basic.UniqueProcessId : INVALID_CLIENT_ID;

}

ULONG_PTR HandleToThreadId(HANDLE Thread)
{

    NTSTATUS Status;
    THREAD_BASIC_INFORMATION Basic;

    Status = NtQueryInformationThread(Thread, ThreadBasicInformation, &Basic, sizeof(Basic), nullptr);

    return NT_SUCCESS(Status) ? (ULONG_PTR)Basic.ClientId.UniqueThread : INVALID_CLIENT_ID;

}

NTSTATUS ProcessIdToHandleEx(PHANDLE ProcessHandle, ULONG_PTR ProcessId, ULONG_PTR Access /* = PROCESS_ALL_ACCESS */)
{

    NTSTATUS            Status;
    OBJECT_ATTRIBUTES   ObjectAttributes;
    CLIENT_ID           CliendID;

    CliendID.UniqueThread   = nullptr;
    CliendID.UniqueProcess  = (HANDLE)ProcessId;
    InitializeObjectAttributes(&ObjectAttributes, nullptr, 0, nullptr, nullptr);

    Status = NtOpenProcess(ProcessHandle, Access, &ObjectAttributes, &CliendID);

    return Status;

}

HANDLE ProcessIdToHandle(ULONG_PTR ProcessId, ULONG_PTR Access /* = PROCESS_ALL_ACCESS */)
{
    NTSTATUS    Status;
    HANDLE      ProcessHandle;

    Status = ProcessIdToHandleEx(&ProcessHandle, ProcessId, Access);
    return NT_SUCCESS(Status) ? ProcessHandle : nullptr;
}

NTSTATUS ThreadIdToHandleEx(PHANDLE ThreadHandle, ULONG_PTR ThreadId, ULONG_PTR Access)
{

    NTSTATUS            Status;
    OBJECT_ATTRIBUTES   ObjectAttributes;
    CLIENT_ID           CliendID;

    CliendID.UniqueProcess  = NULL;
    CliendID.UniqueThread   = (HANDLE)ThreadId;
    InitializeObjectAttributes(&ObjectAttributes, NULL, NULL, NULL, NULL);

    return NtOpenThread(ThreadHandle, Access, &ObjectAttributes, &CliendID);

}

HANDLE ThreadIdToHandle(ULONG_PTR ThreadId, ULONG_PTR Access)
{
    NTSTATUS Status;
    HANDLE Handle;

    Status = ThreadIdToHandleEx(&Handle, ThreadId, Access);
    return NT_SUCCESS(Status) ? Handle : nullptr;
}

ULONG_PTR GetSessionId(HANDLE Process)
{
    NTSTATUS Status;
    PROCESS_SESSION_INFORMATION Session;


    Status = NtQueryInformationProcess(Process, ProcessSessionInformation, &Session, sizeof(Session), NULL);


    return NT_FAILED(Status) ? INVALID_SESSION_ID : Session.SessionId;
}

ULONG_PTR GetSessionId(ULONG_PTR ProcessId)
{
    ULONG_PTR   SessionId;
    HANDLE      Process;

    Process = PidToHandle(ProcessId);
    if (Process == NULL)
        return INVALID_SESSION_ID;

    SessionId = GetSessionId(Process);


    NtClose(Process);


    return SessionId;
}

BOOL IsWow64Process(HANDLE Process)
{
    PVOID       Wow64Information;
    NTSTATUS    Status;

    API_POINTER(ZwQueryInformationProcess)  XQueryInformationProcess;


    XQueryInformationProcess = NtQueryInformationProcess;


    Status = XQueryInformationProcess(
                Process,
                ProcessWow64Information,
                &Wow64Information,
                sizeof(Wow64Information),
                NULL
             );

    return NT_SUCCESS(Status) && Wow64Information != NULL;
}

PSYSTEM_PROCESS_INFORMATION QuerySystemProcesses()
{
    ULONG                       Size, Length;
    NTSTATUS                    Status;
    PSYSTEM_PROCESS_INFORMATION ProcessInfo;

    ProcessInfo = NULL;
    Size = 0;

    LOOP_FOREVER
    {
        Status = NtQuerySystemInformation(SystemProcessInformation, ProcessInfo, Size, &Length);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;

        ProcessInfo = (PSYSTEM_PROCESS_INFORMATION)ReAllocateMemoryP(ProcessInfo, Length);


        if (ProcessInfo == NULL)
            return NULL;

        Size = Length;
    }

    if (!NT_SUCCESS(Status))
    {
        FreeMemoryP(ProcessInfo);
        ProcessInfo = NULL;
    }

    return ProcessInfo;
}

#if ML_X86

NAKED VOID CDECL CaptureRegisters(PML_THREAD_CONTEXT Context)
{
    INLINE_ASM
    {
        push    ebx;
        push    ecx;
        mov     ebx, [esp + 0Ch];
        mov     ecx, [ebx]ML_THREAD_CONTEXT.Flags;
        pushad;

        mov     edx, [esp + 020h];
        mov     eax, [esp + 024h];
        mov     [esp]PUSHAD_REGISTER.Rcx, edx;
        mov     [esp]PUSHAD_REGISTER.Rbx, eax;

        lea     edi, [ebx]ML_THREAD_CONTEXT.Rdi;
        lea     esi, [esp];

        and     ecx, (1 << 8) - 1;

        jecxz   LOOP_END;

_LOOP:
        shr     ecx, 1;
        jnc     FLAG_NOT_SET;

        mov     eax, [esi];
        mov     [edi], eax;

FLAG_NOT_SET:
        lea     esi, [esi + 04h];
        lea     edi, [edi + 04h];
        jnz     _LOOP;

LOOP_END:

        add     [ebx]ML_THREAD_CONTEXT.Rsp, 010h;

        popad;
        pop     ecx;
        pop     ebx;
        ret;
    }
}

VOID CDECL RegistersContext(PML_THREAD_CONTEXT Context)
{
    ;
}

#endif // x86

VOID Sleep(ULONG_PTR Milliseconds, BOOL Alertable)
{
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    FormatTimeOut(&Timeout, Milliseconds);

    LOOP_FOREVER
    {

        Status = NtDelayExecution((BOOLEAN)Alertable, &Timeout);

        if (Milliseconds != INFINITE)
            break;

        if (Alertable == ThreadNoAlertable)
            break;

        if (Status != STATUS_USER_APC && Status != STATUS_ALERTED)
            break;
    }
}


PTEB_ACTIVE_FRAME
FindThreadFrame(
    ULONG_PTR Context
)
{
    PTEB_ACTIVE_FRAME Frame;

    Frame = RtlGetFrame();
    while (Frame != NULL && Frame->Context != Context)
        Frame = Frame->Previous;

    return Frame;
}

PTEB_ACTIVE_FRAME
FindThreadFrameEx(
    ULONG_PTR Context,
    ULONG_PTR Data
)
{
    PTEB_ACTIVE_FRAME Frame;

    Frame = RtlGetFrame();
    while (Frame != nullptr && Frame->Context != Context && Frame->Data != Data)
        Frame = Frame->Previous;

    return Frame;
}

NTSTATUS
CreateProcess(
    PCWSTR                  ApplicationName,
    PWSTR                   CommandLine,
    PCWSTR                  CurrentDirectory,
    ULONG                   CreationFlags,
    LPSTARTUPINFOW          StartupInfo,
    LPPROCESS_INFORMATION   ProcessInformation,
    LPSECURITY_ATTRIBUTES   ProcessAttributes,
    LPSECURITY_ATTRIBUTES   ThreadAttributes,
    PVOID                   Environment,
    HANDLE                  Token,
    BOOL                    InheritHandles
)
{
    BOOL                Result;
    STARTUPINFOW        DefaultStartupInfo;
    PROCESS_INFORMATION DefaultProcessInformation;

    if (StartupInfo == NULL)
    {
        ZeroMemory(&DefaultStartupInfo, sizeof(DefaultStartupInfo));
        DefaultStartupInfo.cb = sizeof(DefaultStartupInfo);
        StartupInfo = &DefaultStartupInfo;
    }

    if (ProcessInformation == NULL)
        ProcessInformation = &DefaultProcessInformation;

    Result = CreateProcessInternalW(
                    Token,
                    ApplicationName,
                    CommandLine,
                    ProcessAttributes,
                    ThreadAttributes,
                    InheritHandles,
                    CreationFlags,
                    Environment,
                    CurrentDirectory,
                    StartupInfo,
                    ProcessInformation,
                    NULL
             );

    if (!Result)
        return ML_NTSTATUS_FROM_WIN32(RtlGetLastWin32Error());

    if (ProcessInformation == &DefaultProcessInformation)
    {
        NtClose(DefaultProcessInformation.hProcess);
        NtClose(DefaultProcessInformation.hThread);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
CreateThread(
    PVOID                   StartAddress,
    PVOID                   StartParameter,
    BOOL                    CreateSuspended,
    HANDLE                  ProcessHandle,
    PHANDLE                 ThreadHandle,
    PCLIENT_ID              ClientID,
    ULONG                   ZeroBits,
    ULONG_PTR               MaximumStackSize,
    ULONG_PTR               CommittedStackSize,
    PSECURITY_DESCRIPTOR    SecurityDescriptor
)
{
    NTSTATUS    Status;
    CLIENT_ID   ThreadId;
    HANDLE      NewThreadHandle;

    Status = RtlCreateUserThread(
                ProcessHandle,
                SecurityDescriptor,
                (BOOLEAN)CreateSuspended,
                ZeroBits,
                MaximumStackSize,
                CommittedStackSize,
                (PUSER_THREAD_START_ROUTINE)StartAddress,
                StartParameter,
                &NewThreadHandle,
                &ThreadId
             );
    if (!NT_SUCCESS(Status))
        return Status;

    if (ClientID != NULL)
        *ClientID = ThreadId;

    if (ThreadHandle != NULL)
    {
        *ThreadHandle = NewThreadHandle;
    }
    else
    {
        NtClose(NewThreadHandle);
    }

    return Status;
}

DECL_NORETURN
VOID
ExitProcess(
    NTSTATUS ExitStatus
)
{
    PVOID       LoaderLockCookie;
    NTSTATUS    Status;
    HANDLE      ProcessHeap = CurrentPeb()->ProcessHeap;

    Status = LdrLockLoaderLock(0, NULL, &LoaderLockCookie);
    RtlAcquirePebLock();
    RtlLockHeap(ProcessHeap);

    NtTerminateProcess(NULL, ExitStatus);

    RtlUnlockHeap(ProcessHeap);
    RtlReleasePebLock();

    if (NT_SUCCESS(Status))
        LdrUnlockLoaderLock(0, LoaderLockCookie);

    LdrShutdownProcess();
    NtTerminateProcess(CurrentProcess, ExitStatus);
}


ML_NAMESPACE_END_(Ps);

ML_NAMESPACE_END_(Native)
ML_NAMESPACE_END
#endif
