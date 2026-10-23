#ifndef _MLNATIVEAPI_H_c2e39676_dc3c_40cd_a77e_e9a22bb61ddc_
#define _MLNATIVEAPI_H_c2e39676_dc3c_40cd_a77e_e9a22bb61ddc_

#if CPP_DEFINED


ML_NAMESPACE_BEGIN(Native)


#ifndef _MLNTLDR_H_ce875050_5f9f_4247_aca8_371c0807af3d_
#define _MLNTLDR_H_ce875050_5f9f_4247_aca8_371c0807af3d_

#ifndef _MLNTPROCESS_H_ea409cd9_d592_4611_ba82_ef00242f7ead_
#define _MLNTPROCESS_H_ea409cd9_d592_4611_ba82_ef00242f7ead_


ML_NAMESPACE_BEGIN(Ps);

static const HANDLE CurrentProcess = NtCurrentProcess();
static const HANDLE CurrentThread = NtCurrentThread();

#define INVALID_SESSION_ID  ULONG_PTR_MAX
#define INVALID_CLIENT_ID   ULONG_PTR_MAX

ForceInline ULONG_PTR CurrentPid()
{
    return (ULONG_PTR)Nt_CurrentTeb()->ClientId.UniqueProcess;
}

ForceInline ULONG_PTR CurrentTid()
{
    return (ULONG_PTR)Nt_CurrentTeb()->ClientId.UniqueThread;
}

ForceInline PPEB_BASE CurrentPeb()
{
    return Nt_CurrentPeb();
}

ForceInline PTEB_BASE CurrentTeb()
{
    return Nt_CurrentTeb();
}

ULONG_PTR
HandleToProcessId(
    HANDLE Process
);

ULONG_PTR
HandleToThreadId(
    HANDLE Thread
);

HANDLE
ProcessIdToHandle(
    ULONG_PTR ProcessId,
    ULONG_PTR Access = PROCESS_ALL_ACCESS
);

NTSTATUS
ProcessIdToHandleEx(
    PHANDLE     ProcessHandle,
    ULONG_PTR   ProcessId,
    ULONG_PTR   Access = PROCESS_ALL_ACCESS
);

HANDLE
ThreadIdToHandle(
    ULONG_PTR ThreadId,
    ULONG_PTR Access = THREAD_ALL_ACCESS
);

NTSTATUS
ThreadIdToHandleEx(
    PHANDLE     ThreadHandle,
    ULONG_PTR   ThreadId,
    ULONG_PTR   Access = THREAD_ALL_ACCESS
);

ForceInline ULONG_PTR HandleToPid(HANDLE Process)
{
    return HandleToProcessId(Process);
}

ForceInline ULONG_PTR HandleToTid(HANDLE Process)
{
    return HandleToThreadId(Process);
}

ULONG_PTR
HandleToThreadId(
    HANDLE Thread
);

template<class PidType>
ForceInline NTSTATUS PidToHandleEx(PHANDLE ProcessHandle, PidType ProcessId, ULONG_PTR Access = PROCESS_ALL_ACCESS)
{
    return ProcessIdToHandleEx(ProcessHandle, (ULONG_PTR)ProcessId, Access);
}

ForceInline HANDLE ProcessIdToHandle(HANDLE ProcessId, ULONG_PTR Access = PROCESS_ALL_ACCESS)
{
    return ProcessIdToHandle((ULONG_PTR)ProcessId, Access);
}

template<class PidType>
ForceInline HANDLE PidToHandle(PidType ProcessId, ULONG_PTR Access = PROCESS_ALL_ACCESS)
{
    return ProcessIdToHandle((ULONG_PTR)ProcessId, Access);
}

ForceInline HANDLE ThreadIdToHandle(HANDLE ThreadId, ULONG_PTR Access = THREAD_ALL_ACCESS)
{
    return ThreadIdToHandle((ULONG_PTR)ThreadId, Access);
}

template<class TidType>
ForceInline HANDLE TidToHandle(TidType ThreadId, ULONG_PTR Access = THREAD_ALL_ACCESS)
{
    return ThreadIdToHandle((ULONG_PTR)ThreadId, Access);
}

ULONG_PTR
GetSessionId(
    HANDLE Process
);

ULONG_PTR
GetSessionId(
    ULONG_PTR ProcessId
);

inline ULONG_PTR GetCurrentSessionId()
{
    return GetSessionId(CurrentProcess);
}

BOOL
IsWow64Process(
    HANDLE Process = CurrentProcess
);

PSYSTEM_PROCESS_INFORMATION QuerySystemProcesses();

typedef struct ML_THREAD_CONTEXT
{
    union
    {
        struct
        {
            BOOLEAN Rdi : 1;
            BOOLEAN Rsi : 1;
            BOOLEAN Rbp : 1;
            BOOLEAN Rsp : 1;
            BOOLEAN Rbx : 1;
            BOOLEAN Rdx : 1;
            BOOLEAN Rcx : 1;
            BOOLEAN Rax : 1;
        };

        ULONG_PTR Value;

    } Flags;

    ULONG_PTR Rdi;
    ULONG_PTR Rsi;
    ULONG_PTR Rbp;
    ULONG_PTR Rsp;
    ULONG_PTR Rbx;
    ULONG_PTR Rdx;
    ULONG_PTR Rcx;
    ULONG_PTR Rax;

    ML_THREAD_CONTEXT()
    {
        Flags.Value = 0;
    }

} ML_THREAD_CONTEXT, *PML_THREAD_CONTEXT;

VOID
CDECL
CaptureRegisters(
    PML_THREAD_CONTEXT Context
);

VOID
CDECL
SetRegisters(
    PML_THREAD_CONTEXT Context
);

enum
{
    ThreadNoAlertable   = FALSE,
    ThreadAlertable     = TRUE,
};

VOID
Sleep(
    ULONG_PTR   Milliseconds,
    BOOL        Alertable = ThreadNoAlertable
);


PTEB_ACTIVE_FRAME
FindThreadFrame(
    ULONG_PTR Context
);

PTEB_ACTIVE_FRAME
FindThreadFrameEx(
    ULONG_PTR Context,
    ULONG_PTR Data
);

NTSTATUS
CreateProcess(
    PCWSTR                  ApplicationName,
    PWSTR                   CommandLine         = NULL,
    PCWSTR                  CurrentDirectory    = NULL,
    ULONG                   CreationFlags       = 0,
    LPSTARTUPINFOW          StartupInfo         = NULL,
    LPPROCESS_INFORMATION   ProcessInformation  = NULL,
    LPSECURITY_ATTRIBUTES   ProcessAttributes   = NULL,
    LPSECURITY_ATTRIBUTES   ThreadAttributes    = NULL,
    PVOID                   Environment         = NULL,
    HANDLE                  Token               = NULL,
    BOOL                    InheritHandles      = FALSE
);


typedef struct ML_PROCESS_INFORMATION : public PROCESS_INFORMATION
{
    PVOID FirstCallLdrLoadDll;

} ML_PROCESS_INFORMATION, *PML_PROCESS_INFORMATION;

NTSTATUS
CreateThread(
    PVOID                   StartAddress,
    PVOID                   StartParameter      = NULL,
    BOOL                    CreateSuspended     = FALSE,
    HANDLE                  ProcessHandle       = Ps::CurrentProcess,
    PHANDLE                 ThreadHandle        = NULL,
    PCLIENT_ID              ClientID            = NULL,
    ULONG                   ZeroBits            = 0,
    ULONG_PTR               MaximumStackSize    = 0,
    ULONG_PTR               CommittedStackSize  = 0,
    PSECURITY_DESCRIPTOR    SecurityDescriptor  = NULL
);

DECL_NORETURN
VOID
ExitProcess(
    NTSTATUS ExitStatus
);

inline PWSTR GetCommandLine()
{
    return CurrentPeb()->ProcessParameters->CommandLine.Buffer;
}

#define ThreadCallbackM(...) (PTHREAD_START_ROUTINE)(LambdaCastHelper<ULONG(NTAPI *)(__VA_ARGS__)>::FUNC)[] (__VA_ARGS__) -> ULONG

#define ThreadLambdaType(_type, ...) [] (_type __VA_ARGS__) -> ULONG
#define ThreadLambdaType_(_type, ...)  (PTHREAD_START_ROUTINE)ThreadLambdaType(_type, __VA_ARGS__)
#define ThreadLambda(...) ThreadLambdaType(ULONG_PTR, __VA_ARGS__)

template<typename F, typename P>
NTSTATUS
CreateThreadT(
    const F&                StartAddress,
    P                       StartParameter      = nullptr,
    BOOL                    CreateSuspended     = FALSE,
    HANDLE                  ProcessHandle       = Ps::CurrentProcess,
    PHANDLE                 ThreadHandle        = nullptr,
    PCLIENT_ID              ClientID            = nullptr,
    ULONG                   StackZeroBits       = 0,
    ULONG                   StackReserved       = 0,
    ULONG                   StackCommit         = 0,
    PSECURITY_DESCRIPTOR    SecurityDescriptor  = nullptr
)
{
    NTSTATUS Status;

    typedef struct StartParameterWrapper
    {
        ml::Function<ULONG(P)> func;
        P StartParameter;

        StartParameterWrapper(const F &StartAddress, const P &Parameter) : func(StartAddress), StartParameter(Parameter)
        {
        }

    } StartParameterWrapper;

    StartParameterWrapper *func;

    func = new StartParameterWrapper(StartAddress, StartParameter);
    if (func == NULL)
        return STATUS_NO_MEMORY;

    Status = CreateThread(PTHREAD_START_ROUTINE(
                [] (PVOID Function) -> ULONG
                {
                    ULONG RetValue;
                    StartParameterWrapper *func = (StartParameterWrapper *)Function;

                    RetValue = (func->func)(func->StartParameter);

                    delete func;

                    return RetValue;
                }),
                func,
                CreateSuspended, ProcessHandle, ThreadHandle, ClientID, StackZeroBits, StackReserved, StackCommit, SecurityDescriptor
            );

    if (NT_FAILED(Status))
    {
        delete func;
    }

    return Status;
}

template<typename F>
NTSTATUS QueueApcRoutine(const F &ApcFunction, HANDLE Thread = CurrentThread)
{
    NTSTATUS Status;
    ml::Function<void()> *func;

    func = new ml::Function<void()>(ApcFunction);
    if (func == NULL)
        return STATUS_NO_MEMORY;

    Status = NtQueueApcThread(Thread,
                [] (PVOID ApcFunction, PVOID, PVOID) -> VOID
                {
                    ml::Function<void()> *func = (ml::Function<void()> *)ApcFunction;

                    (*func)();

                    delete func;
                },
                func,
                NULL,
                NULL
            );

    if (NT_FAILED(Status))
    {
        delete func;
    }

    return Status;
}


ML_NAMESPACE_END_(Ps);

#endif // _MLNTPROCESS_H_ea409cd9_d592_4611_ba82_ef00242f7ead_

ML_NAMESPACE_BEGIN(Ldr);

inline PVOID GetExeModuleHandle()
{
    return (PVOID)Ps::CurrentPeb()->ImageBaseAddress;
}

PLDR_MODULE
FindLdrModuleByName(
    PUNICODE_STRING ModuleName
);

PLDR_MODULE
FindLdrModuleByHandle(
    PVOID BaseAddress
);

PIMAGE_RESOURCE_DATA_ENTRY
FindResource(
    PVOID   Module,
    PCWSTR  Name,
    PCWSTR  Type
);

PVOID
LoadResource(
    PVOID                       Module,
    PIMAGE_RESOURCE_DATA_ENTRY  ResourceDataEntry,
    PULONG                      Size = nullptr
);

#define LOAD_PE_IGNORE_IAT              0x00000001
#define LOAD_PE_IGNORE_RELOC            0x00000002
#define LOAD_PE_DLL_NOT_FOUND_CONTINUE  0x00000004
#define LOAD_PE_NOT_RESOLVE_PATH        0x00000010
#define LOAD_PE_TOP_TO_DOWN             0x00000020

NTSTATUS
LoadPeImage(
    PCWSTR      FullDllPath,
    PVOID*      DllBaseAddress,
    PVOID       OldBaseAddress   = nullptr,
    ULONG_PTR   Flags            = 0
);

NTSTATUS
UnloadPeImage(
    PVOID DllBase
);

NTSTATUS
RelocPeImage(
    PVOID   ImageBuffer,
    PVOID64 ImageDefaultBase,
    PVOID   ImageOldBase    = nullptr,
    PVOID   ImageNewBase    = nullptr
);

NTSTATUS
RelocBlock(
    PVOID                   ImageBase,
    LONG64                  ImageBaseOffset,
    PIMAGE_BASE_RELOCATION2 Relocation
);

PVOID
FASTCALL
GetRoutineAddress(
    PVOID ModuleBase,
    PCSTR ProcedureName
);

inline
PVOID
FASTCALL
GetRoutineAddress(
    PVOID ModuleBase,
    ULONG Ordinal
)
{
    return GetRoutineAddress(ModuleBase, (PCSTR)(ULONG_PTR)Ordinal);
}

PVOID
LoadDll(
    PCWSTR ModuleFileName
);

NTSTATUS
UnloadDll(
    PVOID DllHandle
);

ML_NAMESPACE_END_(Ldr);

#endif // _MLNTLDR_H_ce875050_5f9f_4247_aca8_371c0807af3d_
#ifndef _MLNTMEMORY_H_3b0738ba_74a9_48c5_9804_cd1307c21092_
#define _MLNTMEMORY_H_3b0738ba_74a9_48c5_9804_cd1307c21092_


ML_NAMESPACE_BEGIN(Mm);

#ifndef SEC_IMAGE
#define SEC_IMAGE         0x1000000
#endif

NTSTATUS
AllocVirtualMemory(
    PVOID*      BaseAddress,
    ULONG_PTR   Size,
    ULONG       Protect         = PAGE_EXECUTE_READWRITE,
    ULONG       AllocationType  = MEM_RESERVE | MEM_COMMIT,
    HANDLE      ProcessHandle   = Ps::CurrentProcess
);

NTSTATUS
AllocVirtualMemoryEx(
    HANDLE      ProcessHandle,
    PVOID*      BaseAddress,
    ULONG_PTR   Size,
    ULONG       Protect         = PAGE_EXECUTE_READWRITE,
    ULONG       AllocationType  = MEM_RESERVE | MEM_COMMIT
);

NTSTATUS
ProtectVirtualMemory(
    PVOID       BaseAddress,
    ULONG_PTR   Size,
    ULONG       NewProtect,
    PULONG      OldProtect      = nullptr,
    HANDLE      ProcessHandle   = Ps::CurrentProcess
);

NTSTATUS
ProtectMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    ULONG_PTR   Size,
    ULONG       NewProtect,
    PULONG      OldProtect = nullptr
);

NTSTATUS
FreeVirtualMemory(
    PVOID   BaseAddress,
    HANDLE  ProcessHandle   = Ps::CurrentProcess,
    ULONG   FreeType        = MEM_RELEASE
);

NTSTATUS
ReadMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    PVOID       Buffer,
    ULONG_PTR   Size,
    PULONG_PTR  BytesRead = nullptr
);

NTSTATUS
WriteMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    PVOID       Buffer,
    ULONG_PTR   Size,
    PULONG_PTR  BytesWritten = nullptr
);

NTSTATUS
WriteProtectMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    PVOID       Buffer,
    ULONG_PTR   Size,
    PULONG_PTR  BytesWritten = nullptr
);

NTSTATUS
QueryMappedImageName(
    HANDLE  ProcessHandle,
    PVOID   ImageBase,
    String& ImageName
);

ML_NAMESPACE_END_(Mm);

#endif // _MLNTMEMORY_H_3b0738ba_74a9_48c5_9804_cd1307c21092_
#ifndef _MLNTRTL_H_8ca66ccf_34b3_42f8_8dae_1a65beec3402_
#define _MLNTRTL_H_8ca66ccf_34b3_42f8_8dae_1a65beec3402_


ML_NAMESPACE_BEGIN(Rtl);

inline PWSTR QueryCommandLine()
{
    return Ps::CurrentPeb()->ProcessParameters->CommandLine.Buffer;
}

NTSTATUS
DosPathNameToNtPathName(
    IN  PCWSTR                  DosName,
    OUT PUNICODE_STRING         NtName,
    OUT PCWSTR*                 DEF_VAL(DosFilePath, NULL) OPTIONAL,
    OUT PRTL_RELATIVE_NAME_U    DEF_VAL(FileName, NULL) OPTIONAL
);

NTSTATUS
NtPathNameToDosPathName(
    IN  PUNICODE_STRING DosPath,
    OUT PUNICODE_STRING NtPath
);

NTSTATUS
GetSystemDirectory(
    PUNICODE_STRING Buffer,
    BOOL            Wow64NoRedirect = FALSE
);

NTSTATUS
GetModuleDirectory(
    ml::String& Path,
    PVOID       ModuleBase = nullptr
);

NTSTATUS
GetWorkingDirectory(
    PUNICODE_STRING Buffer
);

NTSTATUS
SetWorkingDirectory(
    PCWSTR PathName
);

NTSTATUS
SetWorkingDirectory(
    PUNICODE_STRING PathName
);

NTSTATUS
SetExeDirectoryAsCurrent(
    VOID
);

BOOL
IsNameInExpression(
    IN PUNICODE_STRING  Expression,
    IN PUNICODE_STRING  Name,
    IN BOOL             DEF_VAL(IgnoreCase, TRUE),
    IN PWSTR            DEF_VAL(UpcaseTable, NULL) OPTIONAL
);

NTSTATUS
EnvironmentAppend(
    PUNICODE_STRING Key,
    PUNICODE_STRING Value
);

inline
BOOL
IsNameInExpression(
    IN PCWSTR   Expression,
    IN PCWSTR   Name,
    IN BOOL     DEF_VAL(IgnoreCase, TRUE),
    IN PWSTR    DEF_VAL(UpcaseTable, NULL) OPTIONAL
)
{
    UNICODE_STRING ExpressionString, NameString;

    RtlInitUnicodeString(&ExpressionString, Expression);
    RtlInitUnicodeString(&NameString, Name);

    return IsNameInExpression(&ExpressionString, &NameString, IgnoreCase, UpcaseTable);
}

ML_NAMESPACE_END_(Rtl);

#endif // _MLNTRTL_H_8ca66ccf_34b3_42f8_8dae_1a65beec3402_
#ifndef _MLNTNLS_H_6adf14d1_ea54_410e_a7e3_0da09303e8d0_
#define _MLNTNLS_H_6adf14d1_ea54_410e_a7e3_0da09303e8d0_


ML_NAMESPACE_BEGIN(Nls);

NTSTATUS
FASTCALL
AnsiToUnicode(
    PWSTR       UnicodeBuffer,
    ULONG_PTR   BufferCount,
    PCSTR       AnsiString,
    LONG_PTR    AnsiLength = -1,
    PULONG_PTR  BytesInUnicode = nullptr
);

NTSTATUS
UnicodeToAnsi(
    PSTR        AnsiBuffer,
    ULONG_PTR   BufferCount,
    PCWSTR      UnicodeString,
    LONG_PTR    UnicodeLength = -1,
    PULONG_PTR  BytesInAnsi = nullptr
);

NTSTATUS
AnsiToUnicodeString(
    PUNICODE_STRING Unicode,
    PCSTR           AnsiString,
    LONG_PTR        AnsiLength = -1,
    BOOL            AllocateDestinationString = TRUE
);

NTSTATUS
UnicodeToAnsiString(
    PANSI_STRING    Ansi,
    PCWSTR          UnicodeString,
    LONG_PTR        UnicodeLength = -1,
    BOOL            AllocateDestinationString = TRUE
);

ML_NAMESPACE_END_(Nls);

#endif // _MLNTNLS_H_6adf14d1_ea54_410e_a7e3_0da09303e8d0_
#ifndef _MLSTRING_H_8ceda0f8_e680_4166_a056_6300386e156d_
#define _MLSTRING_H_8ceda0f8_e680_4166_a056_6300386e156d_


ML_NAMESPACE_BEGIN(Str);

class UnicodeString : public UNICODE_STRING
{
protected:

public:
    UnicodeString();
    ~UnicodeString();

    NTSTATUS Create(PCWSTR Buffer, ULONG_PTR Length = -1);
    NTSTATUS Create(PCUNICODE_STRING Buffer);

    BOOL operator==(PCWSTR Buffer) const;
    BOOL operator==(PCUNICODE_STRING String) const;
    BOOL operator==(const UnicodeString &String) const;

    UnicodeString& operator=(PCWSTR Buffer);
    UnicodeString& operator=(PCUNICODE_STRING Buffer);
};

ML_NAMESPACE_END_(Str);

#endif // _MLSTRING_H_8ceda0f8_e680_4166_a056_6300386e156d_
#ifndef _MLNTREGISTRY_H_ffe728ea_b2d9_416c_bf5b_aea45e0fe4fc_
#define _MLNTREGISTRY_H_ffe728ea_b2d9_416c_bf5b_aea45e0fe4fc_


ML_NAMESPACE_BEGIN(Reg);

#if !defined(HKEY_CURRENT_USER_LOCAL_SETTINGS)
    #define HKEY_CURRENT_USER_LOCAL_SETTINGS    (( HKEY ) (ULONG_PTR)((LONG)0x80000007) )
#endif


#define HKEY_MACHINE_CLASS                  (( HKEY ) (ULONG_PTR)((LONG)0x80000020) )

PVOID
AllocateKeyInfo(
    ULONG_PTR Size
);

VOID
FreeKeyInfo(
    PVOID Info
);

NTSTATUS
OpenPredefinedKeyHandle(
    PHANDLE     KeyHandle,
    HANDLE      PredefinedKey,
    ACCESS_MASK DEF_VAL(DesiredAccess, KEY_ALL_ACCESS)
);

NTSTATUS
OpenKey(
    PHANDLE     KeyHandle,
    HANDLE      hKey,
    ACCESS_MASK DesiredAccess,
    PCWSTR      SubKey
);

NTSTATUS
CloseKeyHandle(
    HANDLE KeyHandle
);

NTSTATUS
GetKeyValue(
    HANDLE                      hKey,
    PCWSTR                      SubKey,
    PCWSTR                      ValueName,
    KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    PVOID                       KeyValueInformation,
    ULONG                       Length,
    PULONG                      DEF_VAL(ResultLength, NULL),
    ULONG                       DEF_VAL(Flags, 0)
);

NTSTATUS
GetKeyValue(
    HANDLE                          hKey,
    PCWSTR                          SubKey,
    PCWSTR                          ValueName,
    PKEY_VALUE_PARTIAL_INFORMATION* Value
);

NTSTATUS
SetKeyValue(
    HANDLE      hKey,
    PCWSTR      SubKey,
    PCWSTR      ValueName,
    ULONG       ValueType,
    LPCVOID     ValueData,
    DWORD       ValueDataLength,
    ULONG_PTR   Flags = 0
);

NTSTATUS
DeleteKey(
    HANDLE      hKey,
    PCWSTR      SubKey,
    ULONG_PTR   Flags = 0
);

NTSTATUS
DeleteKeyValue(
    HANDLE      hKey,
    PCWSTR      SubKey,
    PCWSTR      ValueName,
    ULONG_PTR   Flags = 0
);

ML_NAMESPACE_END_(Reg);

#endif // _MLNTREGISTRY_H_ffe728ea_b2d9_416c_bf5b_aea45e0fe4fc_
#ifndef _MLNTEXCEPTION_H_ae088609_e9de_4aca_85ce_be784e368fc1_
#define _MLNTEXCEPTION_H_ae088609_e9de_4aca_85ce_be784e368fc1_


ML_NAMESPACE_BEGIN(Exp);


NTSTATUS
ExceptionBox(
    PCWSTR      Text,
    PCWSTR      Title       = nullptr,
    PULONG_PTR  Response    = nullptr,
    ULONG_PTR   Type        = MB_ICONASTERISK | MB_OK
);

ML_NAMESPACE_END_(Exp);

#endif // _MLNTEXCEPTION_H_ae088609_e9de_4aca_85ce_be784e368fc1_
#ifndef _MLNTLPC_H_a6aabfd9_718b_4fea_938c_b62ea34136b8_
#define _MLNTLPC_H_a6aabfd9_718b_4fea_938c_b62ea34136b8_


ML_NAMESPACE_BEGIN(Lpc);

typedef NTSTATUS IPC_STATUS;

enum
{
    IPCType_None,
    IPCType_ServerListen,
    IPCType_ServerConnected,
    IPCType_Client,
};

class InterProcessBase
{
protected:
    ULONG_PTR IpcType;

public:
    InterProcessBase()
    {
        IpcType = IPCType_None;
    }

    ULONG_PTR GetIpcType()
    {
        return IpcType;
    }

    IPC_STATUS ReleaseAll()
    {
        return STATUS_NOT_IMPLEMENTED;
    }
};

class InterProcessServerBase : public InterProcessBase
{
public:
    InterProcessServerBase()
    {
    }
};

class InterProcessClientBase : public InterProcessBase
{
public:
    InterProcessClientBase()
    {
        IpcType = IPCType_Client;
    }
};

enum IPC_MESSAGE_TYPE
{
    IpcMessageType_ConnectionRequest,
    IpcMessageType_SmallData,
};

typedef struct IPC_MESSAGE : public PORT_MESSAGE
{
    PVOID GetMessageData()
    {
        return PtrAdd(this, sizeof(PORT_MESSAGE));
    }

    ULONG   MessageType;
    ULONG64 MessageDataLength;

    static const ULONG_PTR kOverhead = sizeof(ULONG) + sizeof(ULONG64);
    static const ULONG_PTR kMaxSmallDataLength = PORT_CREATE_MAXIMUM_MESSAGE_LENGTH - sizeof(PORT_MESSAGE) - kOverhead;

    union
    {
        struct
        {
            ULONG64 MaxMessageLength;

        } ConnectionRequest;

        struct
        {
            BYTE Buffer[kMaxSmallDataLength];

        } SmallData;

    } MessageData;

} IPC_MESSAGE, *PIPC_MESSAGE;


/************************************************************************
  InterProcessLpcServer
************************************************************************/

class InterProcessLpcServer : public InterProcessServerBase
{
protected:
    union
    {
        LPC_HANDLE          PortHandle;
        LPC_HANDLE          LpcPort;
        LPC_HANDLE          ConnectedPort;
    };

    union
    {
        struct
        {
            ULONG_PTR MaxConnectionInfoLength;
            ULONG_PTR MaxMessageLength;
            ULONG_PTR MaxSmallDataLength;

        } Listening;

        struct
        {
            PORT_VIEW           ServerView;
            REMOTE_PORT_VIEW    ClientView;
            PVOID               Context;

        } Connected;
    };

public:
    InterProcessLpcServer();
    ~InterProcessLpcServer();

    VOID Reset();
    IPC_STATUS ReleaseAll();

    operator LPC_HANDLE()
    {
        return PortHandle;
    }

    /************************************************************************
      listen
    ************************************************************************/

    IPC_STATUS
    Create(
        IN PWSTR Name,
        IN ULONG MaxConnectionInfoLength    = sizeof(IPC_MESSAGE),
        IN ULONG MaxMessageLength           = PORT_CREATE_MAXIMUM_MESSAGE_LENGTH
    );

    IPC_STATUS
    Listen(
        IN PIPC_MESSAGE ConnectionRequest,
        IN ULONG        Timeout = INFINITE
    );

    IPC_STATUS
    Listen(
        IN PIPC_MESSAGE     ConnectionRequest,
        IN PLARGE_INTEGER   Timeout = NULL
    );

    IPC_STATUS
    Accept(
        OUT InterProcessLpcServer&  ConnectedPort,
        IN  PIPC_MESSAGE            ConnectionRequest,
        IN  BOOL                    AcceptConnection    = TRUE,
        IN  PVOID                   Context             = NULL OPTIONAL
    );

    /************************************************************************
      connected
    ************************************************************************/

    PVOID GetDataBuffer(PIPC_MESSAGE Message)
    {
        return Message->MessageDataLength > Message->kMaxSmallDataLength ?
                    this->Connected.ClientView.ViewBase :
                    Message->MessageData.SmallData.Buffer;
    }

    IPC_STATUS
    Receive(
        OUT PVOID           Buffer,
        IN  ULONG_PTR       Size,
        OUT PULONG_PTR      BytesTransfered = NULL,
        OUT PVOID*          Context = NULL OPTIONAL,
        IN  ULONG           Timeout = INFINITE OPTIONAL
    );

    IPC_STATUS
    Receive(
        OUT PVOID           Buffer,
        IN  ULONG_PTR       Size,
        OUT PULONG_PTR      BytesTransfered = NULL,
        OUT PVOID*          Context = NULL OPTIONAL,
        IN  PLARGE_INTEGER  Timeout = NULL OPTIONAL
    );
};


/************************************************************************
  InterProcessLpcClient
************************************************************************/

class InterProcessLpcClient : public InterProcessClientBase
{
protected:
    LPC_HANDLE          ConnetionPort;
    PORT_VIEW           ClientView;
    REMOTE_PORT_VIEW    ServerView;
    PVOID               Context;
    ULONG_PTR           MaxSmallDataLength;

public:
    InterProcessLpcClient();
    ~InterProcessLpcClient();

    VOID Reset();
    IPC_STATUS ReleaseAll();

    LPC_STATUS
    Connect(
        IN PWSTR Name,
        IN ULONG MaxMessageLength = 0
    );

    LPC_STATUS
    Send(
        PVOID       Buffer,
        ULONG_PTR   Size,
        PULONG_PTR  BytesTransfered = NULL
    );
};

ML_NAMESPACE_END_(Lpc);

#endif // _MLNTLPC_H_a6aabfd9_718b_4fea_938c_b62ea34136b8_
#ifndef _MLNTFILEIO_H_03daa20f_84f6_4731_b996_cf4155164012_
#define _MLNTFILEIO_H_03daa20f_84f6_4731_b996_cf4155164012_


ML_NAMESPACE_BEGIN(Io);

#if !defined(FILE_ATTRIBUTE_NO_SCRUB_DATA)
    #define FILE_ATTRIBUTE_NO_SCRUB_DATA        0x00020000
#endif


typedef struct _ML_FIND_DATA {
    ULONG           FileAttributes;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   FileSize;
    ULONG           Reserved0;
    ULONG           Reserved1;
    WCHAR           FileName[MAX_NTPATH];
    WCHAR           AlternateFileName[14];

} ML_FIND_DATA, *PML_FIND_DATA;

NTSTATUS
QueryFirstFile(
    PHANDLE         Handle,
    PCWSTR          FileName,
    PML_FIND_DATA   FindFileData
);

NTSTATUS
QueryNextFile(
    HANDLE          FindFileHandle,
    PML_FIND_DATA   FindFileData
);

inline
NTSTATUS
QueryClose(
    HANDLE FindFileHandle
)
{
    return NtClose(FindFileHandle);
}

ULONG_PTR
QueryFileAttributes(
    PCWSTR FileName
);

NTSTATUS
QueryFileAttributesEx(
    PCWSTR      FileName,
    PULONG_PTR  FileAttributes
);

NTSTATUS
ApplyFileAttributes(
    PCWSTR      FileName,
    ULONG_PTR   Attributes
);

NTSTATUS
MoveFile(
    PCWSTR  ExistingFileName,
    PCWSTR  NewFileName,
    BOOL    DEF_VAL(ReplaceIfExists, FALSE)
);

NTSTATUS
CopyFile(
    PCWSTR  ExistingFileName,
    PCWSTR  NewFileName,
    BOOL    FailIfExists
);

NTSTATUS
DeleteFile(
    PCWSTR FileName
);

NTSTATUS
DeleteDirectory(
    PCWSTR DirectoryName
);

BOOL
IsPathExists(
    PCWSTR Path
);

NTSTATUS
QueryDosLetterFromVolumeDeviceName(
    OUT PUNICODE_STRING DosLetter,
    IN  PUNICODE_STRING NtDeviceName
);

NTSTATUS
QueryDosPathFromHandle(
    OUT PUNICODE_STRING DosPath,
    IN  HANDLE          FileHandle
);

NTSTATUS
QueryDosDevice(
    PCWSTR          DeviceName,
    PUNICODE_STRING TargetPath
);

NTSTATUS
QueryDosPathFromNtDeviceName(
    OUT PUNICODE_STRING DosPath,
    IN  PUNICODE_STRING NtDeviceName
);

NTSTATUS
CreateNamedPipe(
    OUT PHANDLE                 ReadPipe,
    OUT PHANDLE                 WritePipe,
    IN  PUNICODE_STRING         DEF_VAL(PipeName, nullptr) OPTIONAL,
    IN  PSECURITY_ATTRIBUTES    DEF_VAL(PipeAttributes, nullptr) OPTIONAL,
    IN  ULONG_PTR               DEF_VAL(BufferSize, 0) OPTIONAL
);

template<typename F, typename P>
NTSTATUS
SetAsyncCall(
    CONST F&    Callback,
    ULONG64     DueTime,
    LONG        Period = 0,
    CONST P&    Context = nullptr
)
{
    NTSTATUS        Status;
    HANDLE          Timer;
    LARGE_INTEGER   _DueTime;

    typedef struct PARAMETER
    {
        ml::Function<BOOL(P)> func;
        P Context;
        LONG Period;
        HANDLE Timer;

        PARAMETER(const F &Callback, const P &Context, const LONG &Period) : func(Callback), Context(Context)
        {
            this->Period = Period;
            this->Timer = nullptr;
        }

        ~PARAMETER()
        {
            if (this->Timer != nullptr)
            {
                NtCancelTimer(this->Timer, nullptr);
                NtClose(this->Timer);
            }
        }

    } PARAMETER;

    PARAMETER *func = new PARAMETER(Callback, Context, Period);
    if (func == nullptr)
        return STATUS_NO_MEMORY;

    Status = NtCreateTimer(&Timer, TIMER_ALL_ACCESS, nullptr, SynchronizationTimer);
    if (NT_FAILED(Status))
    {
        delete func;
        return Status;
    }

    func->Timer = Timer;
    FormatTimeOut(&_DueTime, DueTime);

    Status = NtSetTimer(Timer, &_DueTime,
                [](PVOID p, ULONG, LONG)
                {
                    PARAMETER *param = (PARAMETER *)p;
                    BOOL cancel = param->func(param->Context);
                    if (param->Period == 0 || cancel)
                    {
                        delete param;
                    }
                },
                func, FALSE, Period, nullptr
            );

    if (NT_FAILED(Status))
    {
        delete func;
    }

    return Status;
}

template<typename F>
NTSTATUS
SetAsyncCall(
    CONST F&    Callback,
    ULONG64     DueTime,
    LONG        Period = 0
)
{
    NTSTATUS        Status;
    HANDLE          Timer;
    LARGE_INTEGER   _DueTime;

    typedef struct PARAMETER
    {
        ml::Function<BOOL()> func;
        LONG Period;
        HANDLE Timer;

        PARAMETER(const F &Callback, const LONG &Period) : func(Callback)
        {
            this->Period = Period;
            this->Timer = nullptr;
        }

        ~PARAMETER()
        {
            if (this->Timer != nullptr)
            {
                NtCancelTimer(this->Timer, nullptr);
                NtClose(this->Timer);
            }
        }

    } PARAMETER;

    PARAMETER *func = new PARAMETER(Callback, Period);
    if (func == nullptr)
        return STATUS_NO_MEMORY;

    Status = NtCreateTimer(&Timer, TIMER_ALL_ACCESS, nullptr, SynchronizationTimer);
    if (NT_FAILED(Status))
    {
        delete func;
        return Status;
    }

    func->Timer = Timer;
    FormatTimeOut(&_DueTime, DueTime);

    Status = NtSetTimer(Timer, &_DueTime,
                [](PVOID p, ULONG, LONG)
                {
                    PARAMETER *param = (PARAMETER *)p;
                    BOOL cancel = param->func();
                    if (param->Period == 0 || cancel)
                    {
                        delete param;
                    }
                },
                func, FALSE, Period, nullptr
            );

    if (NT_FAILED(Status))
    {
        delete func;
    }

    return Status;
}

ML_NAMESPACE_END_(Io);

#endif // _MLNTFILEIO_H_03daa20f_84f6_4731_b996_cf4155164012_
#ifndef _MLNTOBJECT_H_a9853fbd_9aa2_4bfd_8efc_84dfd9ff9679_
#define _MLNTOBJECT_H_a9853fbd_9aa2_4bfd_8efc_84dfd9ff9679_


ML_NAMESPACE_BEGIN(Ob);


ML_NAMESPACE_END_(Ob);

#endif // _MLNTOBJECT_H_a9853fbd_9aa2_4bfd_8efc_84dfd9ff9679_
#ifndef _MLNTGDI_H_27034c70_52be_440c_9442_2e83bff34107_
#define _MLNTGDI_H_27034c70_52be_440c_9442_2e83bff34107_



ML_NAMESPACE_BEGIN(Gdi);

typedef struct
{
    USHORT MajorVersion;
    USHORT MinorVersion;
    USHORT NumOfTables;
    USHORT SearchRange;
    USHORT EntrySelector;
    USHORT RangeShift;

} TT_OFFSET_TABLE;

// Tables in TTF file and there placement and name (tag)

typedef struct
{
    ULONG Tag;      // table name
    ULONG CheckSum; // Check sum
    ULONG Offset;   // Offset from beginning of file
    ULONG Length;   // Length of the table in bytes

} TT_TABLE_DIRECTORY;

//Header of names table

typedef struct
{
    USHORT FormatSelector;    // format selector. Always 0
    USHORT NameRecordCount;   // Name Records count
    USHORT StorageOffset;     // Offset for strings storage, from start of the table

} TT_NAME_TABLE_HEADER, *PTT_NAME_TABLE_HEADER;

// Record in names table

enum
{
    TT_PLATFORM_ID_MAC_OS       = 0x0100,
    TT_PLATFORM_ID_WINDOWS      = 0x0300,

    TT_TABLE_TAG_NAME           = TAG4('name'),
    TT_TABLE_TAG_CMAP           = TAG4('cmap'),

    TT_ENCODEING_ID_ANSI        = 0x0000,
    TT_ENCODEING_ID_UTF16_BE    = 0x0100,

    TT_NAME_ID_FACENAME         = 0x0100,
    TT_NAME_ID_TYPE             = 0x0200,
    TT_NAME_ID_FOUNDER          = 0x0300,
    TT_NAME_ID_FULLNAME         = 0x0400,
    TT_NAME_ID_VERSION          = 0x0500,
};

typedef struct
{
    USHORT PlatformID;
    USHORT EncodingID;
    USHORT LanguageID;
    USHORT NameID;
    USHORT StringLength;
    USHORT StringOffset; // from start of storage area

} TT_NAME_RECORD, *PTT_NAME_RECORD;

typedef struct
{
    USHORT  Version;        // Table version number (0).
    USHORT	NumberOfTables; // Number of encoding tables, n.

} TT_CMAP_TABLE_HEADER, *PTT_CMAP_TABLE_HEADER;

typedef struct
{
    USHORT	PlatformID;                 // Platform ID.
    USHORT	EncodingID;                 // Platform-specific encoding ID.
    ULONG	DataOffset;                 // Byte offset from beginning of table to the subtable for this encoding.

} TT_CMAP_RECORD, *PTT_CMAP_RECORD;

#pragma pack()

ML_NAMESPACE_END_(Gdi);


#endif // _MLNTGDI_H_27034c70_52be_440c_9442_2e83bff34107_


ML_NAMESPACE_END_(Native);

#endif // cpp

#endif // _MLNATIVEAPI_H_c2e39676_dc3c_40cd_a77e_e9a22bb61ddc_
