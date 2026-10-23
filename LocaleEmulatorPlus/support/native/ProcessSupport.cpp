#include "../Support.h"
#pragma warning(disable:4245)
#pragma warning(disable:4750)

_ML_C_HEAD_

/************************************************************************
  strings
************************************************************************/
ULONG
InternalCopyUnicodeString(
    PUNICODE_STRING Unicode,
    PWCHAR          Buffer,
    ULONG_PTR       BufferCount,
    BOOL            IsDirectory = FALSE
)
{
    ULONG_PTR Length;

    Length = MY_MIN(Unicode->Length, BufferCount * sizeof(WCHAR));
    CopyMemory(Buffer, Unicode->Buffer, Length);
    Length /= sizeof(WCHAR);

    if (IsDirectory && Length < BufferCount && Buffer[Length - 1] != '\\')
        Buffer[Length++] = '\\';

    if (Length < BufferCount)
        Buffer[Length] = 0;

    return Length;
}

NTSTATUS
FASTCALL
Nt_AnsiToUnicode(
    PWCHAR      UnicodeBuffer,
    ULONG_PTR   BufferCount,
    LPCSTR      AnsiString,
    LONG_PTR    AnsiLength,
    PULONG_PTR  BytesInUnicode
)
{
    return ml::Native::Nls::AnsiToUnicode(UnicodeBuffer, BufferCount, AnsiString, AnsiLength, BytesInUnicode);
}

NTSTATUS
Nt_UnicodeToAnsi(
    PCHAR       AnsiBuffer,
    ULONG_PTR   BufferCount,
    LPCWSTR     UnicodeString,
    LONG_PTR    UnicodeLength,
    PULONG_PTR  BytesInAnsi
)
{
    return ml::Native::Nls::UnicodeToAnsi(AnsiBuffer, BufferCount, UnicodeString, UnicodeLength, BytesInAnsi);
}

NTSTATUS
Nt_AnsiToUnicodeString(
    PUNICODE_STRING Unicode,
    PCHAR           AnsiString,
    LONG_PTR        AnsiLength,
    BOOL            AllocateDestinationString
)
{
    return ml::Native::Nls::AnsiToUnicodeString(Unicode, AnsiString, AnsiLength, AllocateDestinationString);
}

NTSTATUS
Nt_UnicodeToAnsiString(
    PANSI_STRING    Ansi,
    LPCWSTR         UnicodeString,
    LONG_PTR        UnicodeLength,
    BOOL            AllocateDestinationString
)
{
    return ml::Native::Nls::UnicodeToAnsiString(Ansi, UnicodeString, UnicodeLength, AllocateDestinationString);
}


/************************************************************************
  process api
************************************************************************/

BOOL
Nt_IsWow64Process(
    HANDLE Process /* = NtCurrentProcess() */
)
{
    PVOID       Wow64Information;
    NTSTATUS    Status;

    Status = ZwQueryInformationProcess(
                Process,
                ProcessWow64Information,
                &Wow64Information,
                sizeof(Wow64Information),
                NULL
             );

    return NT_SUCCESS(Status) && Wow64Information != NULL;
}

ULONG Nt_GetErrorMode()
{
    ULONG HardErrorMode;

    if (!NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessDefaultHardErrorMode, &HardErrorMode, sizeof(HardErrorMode), NULL)))
        HardErrorMode = 0;

    return HardErrorMode;
}

ULONG Nt_SetErrorMode(ULONG Mode)
{
    ULONG HardErrorMode, PreviousHardErrorMode;
    NTSTATUS Status;

    PreviousHardErrorMode = Nt_GetErrorMode();
    HardErrorMode = PreviousHardErrorMode | Mode;
    NtSetInformationProcess(NtCurrentProcess(), ProcessDefaultHardErrorMode, &HardErrorMode, sizeof(HardErrorMode));

    return PreviousHardErrorMode;
}

/************************************************************************
  common file io
************************************************************************/

NTSTATUS Nt_DeleteFile(PCWSTR FileName)
{
    return Io::DeleteFile(FileName);
}

/************************************************************************
  common path
************************************************************************/

ULONG_PTR
Nt_QueryDosDevice(
    PCWSTR      DeviceName,
    PWSTR       TargetPath,
    ULONG_PTR   PathSize
)
{
    NTSTATUS        Status;
    UNICODE_STRING  Target;

    Status = Io::QueryDosDevice(DeviceName, &Target);
    if (NT_FAILED(Status))
        return 0;

    PathSize = InternalCopyUnicodeString(&Target, TargetPath, PathSize);
    RtlFreeUnicodeString(&Target);

    return PathSize;
}

/************************************************************************
  common query
************************************************************************/

POBJECT_TYPES_INFORMATION QuerySystemObjectTypes()
{
    ULONG                       Size, Length;
    NTSTATUS                    Status;
    POBJECT_TYPES_INFORMATION   ObjectTypes, StackPointer;

    Size = sizeof(*ObjectTypes);
    ObjectTypes = (POBJECT_TYPES_INFORMATION)AllocStack(Size);
    StackPointer = ObjectTypes;

    LOOP_FOREVER
    {

        Status = NtQueryObject(NULL, ObjectTypesInformation, ObjectTypes, Size, &Length);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;

        if (ObjectTypes == StackPointer)
            ObjectTypes = NULL;

        ObjectTypes = (POBJECT_TYPES_INFORMATION)ReAllocateMemoryP(ObjectTypes, Length);


        if (ObjectTypes == NULL)
            return NULL;

        Size = Length;
    }

    if (!NT_SUCCESS(Status))
    {
        FreeMemoryP(ObjectTypes);
        ObjectTypes = NULL;
    }

    return ObjectTypes;
}

PSYSTEM_HANDLE_INFORMATION_EX QuerySystemHandles()
{
    ULONG                           Size, Length;
    NTSTATUS                        Status;
    PSYSTEM_HANDLE_INFORMATION_EX   SystemHandles;

    Size = sizeof(*SystemHandles) * 2;

    SystemHandles = (PSYSTEM_HANDLE_INFORMATION_EX)AllocateMemoryP(Size);
    if (SystemHandles == nullptr)
        return nullptr;

    LOOP_FOREVER
    {


        Status = NtQuerySystemInformation(SystemExtendedHandleInformation, SystemHandles, Size, &Length);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;

        SystemHandles = (PSYSTEM_HANDLE_INFORMATION_EX)ReAllocateMemoryP(SystemHandles, Length);


        if (SystemHandles == nullptr)
            return nullptr;

        Size = Length;
    }

    if (!NT_SUCCESS(Status))
    {
        FreeMemoryP(SystemHandles);
        SystemHandles = nullptr;
    }

    return SystemHandles;
}

BOOL ReleaseSystemInformation(PVOID Processes)
{
    return FreeMemoryP(Processes);
}

HANDLE QueryCsrssProcessId(ULONG_PTR Session)
{
    return NULL;
}

/************************************************************************
  common process
************************************************************************/

/************************************************************************
  common token
************************************************************************/

ULONG_PTR
Nt_GetSessionId(
    HANDLE Process
)
{
    return ml::Native::Ps::GetSessionId(Process);
}


/************************************************************************
  user mode
************************************************************************/

/************************************************************************
  user token
************************************************************************/

NTSTATUS
Nt_GetTokenInfo(
    HANDLE                  TokenHandle,
    TOKEN_INFORMATION_CLASS TokenInformationClass,
    PVOID                   Information,
    PULONG_PTR              Size
)
{
    NTSTATUS Status;
    PVOID TokenInformation;
    ULONG Length;

    TokenInformation = NULL;
    Length = 0;

    LOOP_FOREVER
    {
        Status = NtQueryInformationToken(TokenHandle, TokenInformationClass, TokenInformation, Length, &Length);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;

        TokenInformation = ReAllocateMemory(TokenInformation, Length);
    }

    if (NT_FAILED(Status))
    {
        FreeMemory(TokenInformation);
        TokenInformation = NULL;
    }
    else
    {
        *(PVOID *)Information = TokenInformation;
        if (Size != NULL)
            *Size=  Length;
    }

    return Status;
}

VOID ReleaseTokenInfo(PVOID TokenInfo)
{
    FreeMemory(TokenInfo);
}

NTSTATUS
Nt_SetProcessThreadToken(
    HANDLE Process,
    HANDLE Thread,
    HANDLE Token
)
{
    BOOLEAN                 Enabled;
    NTSTATUS                Status;
    OBJECT_ATTRIBUTES       ObjectAttributes;
    PROCESS_ACCESS_TOKEN    ProcessToken;

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);

    Status = NtDuplicateToken(Token, 0, &ObjectAttributes, FALSE, TokenPrimary, &ProcessToken.Token);
    FAIL_RETURN(Status);

    if (NT_FAILED(RtlAdjustPrivilege(SE_ASSIGNPRIMARYTOKEN_PRIVILEGE, TRUE, TRUE, &Enabled)))
        Enabled = FALSE;

    ProcessToken.Thread = Thread;
    Status = NtSetInformationProcess(Process, ProcessAccessToken, &ProcessToken, sizeof(ProcessToken));

    if (Enabled)
        RtlAdjustPrivilege(SE_ASSIGNPRIMARYTOKEN_PRIVILEGE, FALSE, TRUE, &Enabled);

    NtClose(ProcessToken.Token);

    return Status;
}

NTSTATUS
Nt_AdjustPrivilege(
    ULONG_PTR   Privilege,
    BOOL        Enable,
    BOOL        CurrentThread
)
{
    BOOLEAN Enabled;
    return RtlAdjustPrivilege(Privilege, (BOOLEAN)Enable, (BOOLEAN)CurrentThread, &Enabled);
}

/************************************************************************
  debug api
************************************************************************/

NTSTATUS Nt_DebugActiveProcess(ULONG_PTR ProcessId)
{
    HANDLE      ProcessHandle;
    NTSTATUS    Status;

    Status = DbgUiConnectToDbg();
    if (!NT_SUCCESS(Status))
        return Status;

    ProcessHandle = ml::Native::Ps::ProcessIdToHandle(ProcessId);
    if (ProcessHandle == NULL)
        return STATUS_UNSUCCESSFUL;

    Status = DbgUiDebugActiveProcess(ProcessHandle);
    NtClose(ProcessHandle);

    return Status;
}

NTSTATUS Nt_DebugActiveProcessStop(ULONG_PTR ProcessId)
{
    HANDLE      Process;
    NTSTATUS    Status;

    Process = ml::Native::Ps::ProcessIdToHandle(ProcessId);
    if (Process == NULL)
        return STATUS_UNSUCCESSFUL;

    Status = DbgUiStopDebugging(Process);
    NtClose(Process);

    return Status;
}

NTSTATUS Nt_WaitForDebugEvent(PDBGUI_WAIT_STATE_CHANGE WaitState, ULONG Timeout, HANDLE DebugObject)
{
    NTSTATUS        Status;
    LARGE_INTEGER   TimeOut;

    FormatTimeOut(&TimeOut, Timeout);
    LOOP_FOREVER
    {
        Status = NtWaitForDebugEvent(DebugObject, TRUE, &TimeOut, WaitState);
        switch (Status)
        {
            case STATUS_USER_APC:
            case STATUS_ALERTED:
                continue;
        }

        if (!NT_SUCCESS(Status) || Status == STATUS_TIMEOUT)
            return Status;

        break;
    }

    return Status;
}

/************************************************************************
  list entry
************************************************************************/

VOID
InitializeListHead(
    PLIST_ENTRY ListHead
)
{
    ListHead->Flink = ListHead->Blink = ListHead;
}

BOOL
IsListEmpty(
    LIST_ENTRY *ListHead
)
{
    return (BOOL)(ListHead->Flink == ListHead);
}

BOOL
RemoveEntryList(
    PLIST_ENTRY Entry
)
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Flink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;
    return (BOOL)(Flink == Blink);
}

PLIST_ENTRY
RemoveHeadList(
    PLIST_ENTRY ListHead
)
{
    PLIST_ENTRY Flink;
    PLIST_ENTRY Entry;

    Entry = ListHead->Flink;
    Flink = Entry->Flink;
    ListHead->Flink = Flink;
    Flink->Blink = ListHead;
    return Entry;
}

PLIST_ENTRY
RemoveTailList(
    PLIST_ENTRY ListHead
)
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Entry;

    Entry = ListHead->Blink;
    Blink = Entry->Blink;
    ListHead->Blink = Blink;
    Blink->Flink = ListHead;
    return Entry;
}

VOID
InsertTailList(
    PLIST_ENTRY ListHead,
    PLIST_ENTRY Entry
)
{
    PLIST_ENTRY Blink;

    Blink = ListHead->Blink;
    Entry->Flink = ListHead;
    Entry->Blink = Blink;
    Blink->Flink = Entry;
    ListHead->Blink = Entry;
}

VOID
InsertHeadList(
    PLIST_ENTRY ListHead,
    PLIST_ENTRY Entry
)
{
    PLIST_ENTRY Flink;

    Flink = ListHead->Flink;
    Entry->Flink = Flink;
    Entry->Blink = ListHead;
    Flink->Blink = Entry;
    ListHead->Flink = Entry;
}

VOID
AppendTailList(
    PLIST_ENTRY ListHead,
    PLIST_ENTRY ListToAppend
)
{
    PLIST_ENTRY ListEnd = ListHead->Blink;

    ListHead->Blink->Flink = ListToAppend;
    ListHead->Blink = ListToAppend->Blink;
    ListToAppend->Blink->Flink = ListHead;
    ListToAppend->Blink = ListEnd;
}

PSINGLE_LIST_ENTRY
PopEntryList(
    PSINGLE_LIST_ENTRY ListHead
)
{
    PSINGLE_LIST_ENTRY FirstEntry;
    FirstEntry = ListHead->Next;
    if (FirstEntry != NULL) {
        ListHead->Next = FirstEntry->Next;
    }

    return FirstEntry;
}

VOID
PushEntryList(
    PSINGLE_LIST_ENTRY ListHead,
    PSINGLE_LIST_ENTRY Entry
)
{
    Entry->Next = ListHead->Next;
    ListHead->Next = Entry;
}

VOID Nt_ListLock(PCRITICAL_SECTION Lock)
{
    RtlEnterCriticalSection(Lock);
}

VOID Nt_ListUnlock(PCRITICAL_SECTION Lock)
{
    RtlLeaveCriticalSection(Lock);
}

PLIST_ENTRY
ExInterlockedInsertHeadList(
    PLIST_ENTRY         ListHead,
    PLIST_ENTRY         Entry,
    PCRITICAL_SECTION   Lock
)
{
    Nt_ListLock(Lock);
    InsertHeadList(ListHead, Entry);
    Nt_ListUnlock(Lock);
    return ListHead;
}

PLIST_ENTRY
ExInterlockedInsertTailList(
    PLIST_ENTRY         ListHead,
    PLIST_ENTRY         Entry,
    PCRITICAL_SECTION   Lock
)
{
    PLIST_ENTRY TailList;

    Nt_ListLock(Lock);
    TailList = ListHead->Blink;
    InsertTailList(ListHead, Entry);
    Nt_ListUnlock(Lock);

    return TailList;
}

PLIST_ENTRY
ExInterlockedRemoveHeadList(
    PLIST_ENTRY         ListHead,
    PCRITICAL_SECTION   Lock
)
{
    Nt_ListLock(Lock);
    ListHead = RemoveHeadList(ListHead);
    Nt_ListUnlock(Lock);
    return ListHead;
}

PSINGLE_LIST_ENTRY
ExInterlockedPopEntryList(
    PSINGLE_LIST_ENTRY  ListHead,
    PCRITICAL_SECTION   Lock
)
{
    Nt_ListLock(Lock);
    ListHead = PopEntryList(ListHead);
    Nt_ListUnlock(Lock);
    return ListHead;
}

PSINGLE_LIST_ENTRY
ExInterlockedPushEntryList(
    PSINGLE_LIST_ENTRY  ListHead,
    PSINGLE_LIST_ENTRY  Entry,
    PCRITICAL_SECTION   Lock
)
{
    Nt_ListLock(Lock);
    PushEntryList(ListHead, Entry);
    Nt_ListUnlock(Lock);
    return ListHead;
}

VOID
Nt_InitializeSListHead(
    PSLIST_HEADER SListHead
)
{
    RtlZeroMemory(SListHead, sizeof(SLIST_HEADER));
}

/************************************************************************
  registry api
************************************************************************/

#if !defined(HKEY_CURRENT_USER_LOCAL_SETTINGS)
    #define HKEY_CURRENT_USER_LOCAL_SETTINGS    (( HKEY ) (ULONG_PTR)((LONG)0x80000007) )
#endif

NTSTATUS Nt_OpenPredefinedKeyHandle(PHANDLE KeyHandle, HKEY PredefinedKey, ACCESS_MASK DesiredAccess)
{
    return Reg::OpenPredefinedKeyHandle(KeyHandle, PredefinedKey, DesiredAccess);
}

NTSTATUS
Nt_RegGetValue(
    HANDLE                      hKey,
    PCWSTR                      SubKey,
    PCWSTR                      ValueName,
    KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    PVOID                       KeyValueInformation,
    ULONG                       Length,
    PULONG                      ResultLength,
    ULONG                       Flags
)
{
    return Reg::GetKeyValue(hKey, SubKey, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength, Flags);
}

NTSTATUS
Nt_RegSetValue(
    HANDLE  hKey,
    PCWSTR  SubKey,
    PCWSTR  ValueName,
    ULONG   ValueType,
    LPCVOID ValueData,
    ULONG   ValueDataLength
)
{
    return Reg::SetKeyValue(hKey, SubKey, ValueName, ValueType, ValueData, ValueDataLength);
}

NTSTATUS
Nt_RegDeleteValue(
    HANDLE  hKey,
    PCWSTR  SubKey,
    PCWSTR  ValueName
)
{
    return Reg::DeleteKeyValue(hKey, SubKey, ValueName);
}

/************************************************************************
  memory
************************************************************************/

NTSTATUS
Nt_ProtectMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    ULONG_PTR   Size,
    ULONG       NewProtect,
    PULONG      OldProtect
)
{
    return ZwProtectVirtualMemory(ProcessHandle, &BaseAddress, &Size, NewProtect, OldProtect);
}

NTSTATUS
Nt_ReadMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    PVOID       Buffer,
    ULONG_PTR   Size,
    PULONG_PTR  BytesRead /* = NULL */
)
{
    return ZwReadVirtualMemory(ProcessHandle, BaseAddress, Buffer, Size, BytesRead);
}

NTSTATUS
Nt_WriteMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    PVOID       Buffer,
    ULONG_PTR   Size,
    PULONG_PTR  BytesWritten /* = NULL */
)
{
    return ZwWriteVirtualMemory(ProcessHandle, BaseAddress, Buffer, Size, BytesWritten);
}

NTSTATUS
Nt_WriteProtectMemory(
    HANDLE      ProcessHandle,
    PVOID       BaseAddress,
    PVOID       Buffer,
    ULONG_PTR   Size,
    PULONG_PTR  BytesWritten /* = NULL */
)
{
    ULONG       Protect;
    NTSTATUS    Status;

    Status = Nt_ProtectMemory(ProcessHandle, BaseAddress, Size, PAGE_EXECUTE_READWRITE, &Protect);
    FAIL_RETURN(Status);

    Status = Nt_WriteMemory(ProcessHandle, BaseAddress, Buffer, Size, BytesWritten);

    if (Protect != PAGE_EXECUTE_READWRITE)
        Nt_ProtectMemory(ProcessHandle, BaseAddress, Size, Protect, &Protect);

    return Status;
}

/************************************************************************
  process
************************************************************************/
/*
NTSTATUS
Nt_GetProcessUserName(
    PUNICODE_STRING UserName,
    HANDLE          Process
)
{
    HANDLE      Token;
    NTSTATUS    Status;
    PTOKEN_USER User;
    ULONG       Length, DomainLength;
    WCHAR       Name[MAX_NTPATH], Domain[MAX_NTPATH];
    SID_NAME_USE SidType;

    Status = NtOpenProcessToken(Process, TOKEN_QUERY, &Token);
    FAIL_RETURN(Status);

    Status = Nt_GetTokenInfo(Token, TokenUser, &User);
    if (NT_FAILED(Status))
    {
        NtClose(Token);
        return Status;
    }

    Length = countof(Name);
    DomainLength = countof(Domain);
    Status = LookupAccountSidW(NULL, User->User.Sid, Name, &Length, Domain, &DomainLength, &SidType);

    NtClose(Token);
    ReleaseTokenInfo(User);

    if (!Status)
        return STATUS_UNSUCCESSFUL;

    return RtlCreateUnicodeString(UserName, Name) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}
*/
#if ENABLE_LOG

BOOL LepGetModuleLogDirectory(PWSTR Path, ULONG Capacity)
{
    ULONG Length;

    if (Path == nullptr || Capacity < 8)
        return FALSE;

    Length = GetModuleFileNameW((HMODULE)&__ImageBase, Path, Capacity);
    if (Length == 0 || Length >= Capacity)
        return FALSE;

    while (Length != 0 && Path[Length - 1] != L'\\' && Path[Length - 1] != L'/')
        --Length;

    static const WCHAR DirectoryName[] = L"Log";
    if (Length == 0 || Length + CONST_STRLEN(DirectoryName) + 2 > Capacity)
        return FALSE;

    CopyMemory(Path + Length, DirectoryName, sizeof(DirectoryName));
    CreateDirectoryW(Path, nullptr);

    Length += CONST_STRLEN(DirectoryName);
    Path[Length++] = L'\\';
    Path[Length] = 0;
    return TRUE;
}

static VOID LepInjectDiagAppendRaw(PCWSTR Text)
{
    HANDLE File;
    ULONG Written;
    WCHAR Path[MAX_PATH];
    ULONG Length;
#if ML_AMD64
    static const WCHAR DiagName[] = L"LocaleEmulatorPlus-inject-x64-%u.log";
#else
    static const WCHAR DiagName[] = L"LocaleEmulatorPlus-inject-x86-%u.log";
#endif

    if (!LepGetModuleLogDirectory(Path, countof(Path)))
        return;
    Length = StrLengthW(Path);
    if (Length >= countof(Path) - 64)
        return;
    Length += FormatStringW(Path + Length, DiagName, (ULONG)GetCurrentProcessId());

    File = CreateFileW(
        Path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (File == INVALID_HANDLE_VALUE)
        return;

    WriteFile(File, Text, (DWORD)(StrLengthW(Text) * sizeof(WCHAR)), &Written, nullptr);
    NtClose(File);
}

static VOID LepInjectDiagHex(PCWSTR Label, ULONG_PTR Value)
{
    WCHAR Buffer[160];
    ULONG_PTR Length;

    Length = 0;
    for (; Label != nullptr && *Label != 0 && Length != countof(Buffer) - 24; ++Label)
        Buffer[Length++] = *Label;

    Buffer[Length++] = L'0';
    Buffer[Length++] = L'x';

    for (ULONG_PTR Shift = sizeof(Value) * 8; Shift != 0; )
    {
        ULONG_PTR Digit;

        Shift -= 4;
        Digit = (Value >> Shift) & 0xF;
        Buffer[Length++] = (WCHAR)(Digit < 10 ? L'0' + Digit : L'A' + Digit - 10);
    }

    Buffer[Length++] = L'\r';
    Buffer[Length++] = L'\n';
    Buffer[Length] = 0;

    LepInjectDiagAppendRaw(Buffer);
}

#define LEP_INJECT_DIAG(_text) LepInjectDiagAppendRaw(_text L"\r\n")
#define LEP_INJECT_DIAG_HEX(_label, _value) LepInjectDiagHex(_label, (ULONG_PTR)(_value))

#else

#define LEP_INJECT_DIAG(_text) ((VOID)0)
#define LEP_INJECT_DIAG_HEX(_label, _value) ((VOID)0)

#endif

#if ML_AMD64
static const BYTE InjectRemote_LoadDll_x64[] =
{
    0x4C, 0x8B, 0xD4,                                                       // mov r10, rsp
    0x48, 0x83, 0xEC, 0x68,                                                 // sub rsp, 68h
    0x48, 0x83, 0xE4, 0xF0,                                                 // and rsp, -10h
    0x4C, 0x89, 0x54, 0x24, 0x50,                                           // mov [rsp+50h], r10
    0x31, 0xC9,                                                             // xor ecx, ecx
    0x31, 0xD2,                                                             // xor edx, edx
    0x49, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,                                     // mov r8, DllPath
    0x4C, 0x8D, 0x4C, 0x24, 0x30,                                           // Lepa r9, [rsp+30h]
    0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,                                     // mov rax, LdrLoadDll
    0xFF, 0xD0,                                                             // call rax
    0x49, 0xBA, 0, 0, 0, 0, 0, 0, 0, 0,                                     // mov r10, StatusFlag
    0x41, 0xC7, 0x02, 0, 0, 0, 0,                                           // mov dword ptr [r10], 0
    0x4C, 0x8B, 0x54, 0x24, 0x50,                                           // mov r10, [rsp+50h]
    0x49, 0x8B, 0xE2,                                                       // mov rsp, r10
    0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,                                     // mov rax, ReturnRip
    0xFF, 0xE0,                                                             // jmp rax
};

NTSTATUS
InjectDllToRemoteProcess(
    HANDLE          ProcessHandle,
    HANDLE          ThreadHandle,
    PUNICODE_STRING DllFullPath,
    ULONG_PTR       Flags,
    PVOID*          InjectBuffer,
    ULONG_PTR       Timeout
)
{
    NTSTATUS        Status;
    PVOID           Buffer;
    ULONG_PTR       DllPathLength, InjectContextSize, ShellCodeOffset;
    CONTEXT         ThreadContext;

    typedef struct
    {
        ULONG           StatusFlag;
        ULONG           Padding;
        PVOID           xLdrLoadDll;
        UNICODE_STRING  DllPath;
    } INJECT_REMOTE_LOAD_DLL64;

    INJECT_REMOTE_LOAD_DLL64 LoadDll;
    BYTE ShellCode[sizeof(InjectRemote_LoadDll_x64)];

    if (FLAG_ON(Flags, INJECT_ISSUE_REMOTE_BREAKIN | INJECT_ISSUE_BREAKIN_ONLY))
        return STATUS_NOT_IMPLEMENTED;

    LEP_INJECT_DIAG(L"InjectDllToRemoteProcess64: entry");
    LEP_INJECT_DIAG_HEX(L"  Flags=", Flags);
    LEP_INJECT_DIAG_HEX(L"  ProcessHandle=", ProcessHandle);
    LEP_INJECT_DIAG_HEX(L"  ThreadHandle=", ThreadHandle);

    Buffer = NULL;
    if (FLAG_ON(Flags, INJECT_PREALLOC_BUFFER))
    {
        if (InjectBuffer == NULL || *InjectBuffer == NULL)
            return STATUS_INVALID_PARAMETER;

        Buffer = *InjectBuffer;
    }
    else if (InjectBuffer != NULL)
    {
        *InjectBuffer = NULL;
    }

    ThreadContext.ContextFlags = CONTEXT_CONTROL;
    Status = NtGetContextThread(ThreadHandle, &ThreadContext);
    LEP_INJECT_DIAG_HEX(L"  NtGetContextThread=", Status);
    FAIL_RETURN(Status);
    LEP_INJECT_DIAG_HEX(L"  OriginalRip=", ThreadContext.Rip);
    LEP_INJECT_DIAG_HEX(L"  OriginalRsp=", ThreadContext.Rsp);

    if (!FLAG_ON(Flags, INJECT_PREALLOC_BUFFER))
    {
        Status = Mm::AllocVirtualMemoryEx(ProcessHandle, &Buffer, MEMORY_PAGE_SIZE);
        LEP_INJECT_DIAG_HEX(L"  AllocVirtualMemoryEx=", Status);
        FAIL_RETURN(Status);
    }
    LEP_INJECT_DIAG_HEX(L"  Buffer=", Buffer);

    DllPathLength = DllFullPath == nullptr ? 0 : DllFullPath->Length;
    InjectContextSize = sizeof(LoadDll);
    ShellCodeOffset = ROUND_UP(InjectContextSize + DllPathLength, 16);

    ZeroMemory(&LoadDll, sizeof(LoadDll));
    LoadDll.StatusFlag = 1;
    LoadDll.xLdrLoadDll = EATLookupRoutineByHashPNoFix(GetNtdllHandle(), NTDLL_LdrLoadDll);
    LoadDll.DllPath.Length = (USHORT)DllPathLength;
    LoadDll.DllPath.MaximumLength = (USHORT)DllPathLength;
    LoadDll.DllPath.Buffer = (PWSTR)PtrAdd(Buffer, InjectContextSize);

    CopyMemory(ShellCode, InjectRemote_LoadDll_x64, sizeof(ShellCode));
    *(PVOID*)&ShellCode[22] = &((INJECT_REMOTE_LOAD_DLL64*)Buffer)->DllPath;
    *(PVOID*)&ShellCode[37] = LoadDll.xLdrLoadDll;
    *(PVOID*)&ShellCode[49] = &((INJECT_REMOTE_LOAD_DLL64*)Buffer)->StatusFlag;
    *(PVOID*)&ShellCode[74] = (PVOID)ThreadContext.Rip;

    LEP_INJECT_DIAG_HEX(L"  DllPathLength=", DllPathLength);
    LEP_INJECT_DIAG_HEX(L"  ShellCodeOffset=", ShellCodeOffset);
    LEP_INJECT_DIAG_HEX(L"  RemoteDllPath=", LoadDll.DllPath.Buffer);
    LEP_INJECT_DIAG_HEX(L"  RemoteLdrLoadDll=", LoadDll.xLdrLoadDll);

    Status = STATUS_UNSUCCESSFUL;
    LOOP_ONCE
    {
        Status = WriteMemory(ProcessHandle, Buffer, &LoadDll, sizeof(LoadDll));
        LEP_INJECT_DIAG_HEX(L"  WriteContext=", Status);
        if (NT_FAILED(Status))
            break;

        if (DllPathLength != 0)
        {
            Status = WriteMemory(ProcessHandle, PtrAdd(Buffer, InjectContextSize), DllFullPath->Buffer, DllPathLength);
            LEP_INJECT_DIAG_HEX(L"  WriteDllPath=", Status);
            if (NT_FAILED(Status))
                break;
        }

        ThreadContext.Rip = (DWORD64)PtrAdd(Buffer, ShellCodeOffset);
        LEP_INJECT_DIAG_HEX(L"  NewRip=", ThreadContext.Rip);

        Status = WriteMemory(ProcessHandle, (PVOID)ThreadContext.Rip, ShellCode, sizeof(ShellCode));
        LEP_INJECT_DIAG_HEX(L"  WriteShellCode=", Status);
        if (NT_FAILED(Status))
            break;

        NtFlushInstructionCache(ProcessHandle, (PVOID)ThreadContext.Rip, sizeof(ShellCode));

        if (FLAG_OFF(Flags, INJECT_DONT_CHANGE_IP))
        {
            Status = NtSetContextThread(ThreadHandle, &ThreadContext);
            LEP_INJECT_DIAG_HEX(L"  NtSetContextThread=", Status);
            if (NT_FAILED(Status))
                break;
        }

        if (InjectBuffer != NULL)
            *InjectBuffer = Buffer;

        if (FLAG_ON(Flags, INJECT_THREAD_SUSPENDED))
        {
            LEP_INJECT_DIAG(L"InjectDllToRemoteProcess64: return suspended success");
            return STATUS_SUCCESS;
        }

        Status = NtResumeThread(ThreadHandle, NULL);
        LEP_INJECT_DIAG_HEX(L"  NtResumeThread=", Status);
        break;
    }

    if (NT_FAILED(Status) && FLAG_OFF(Flags, INJECT_PREALLOC_BUFFER))
        Mm::FreeVirtualMemory(Buffer, ProcessHandle);

    LEP_INJECT_DIAG_HEX(L"InjectDllToRemoteProcess64: exit ", Status);
    return Status;
}

#else // x86

ASM PVOID InjectRemote_LoadDll()
{
    INLINE_ASM
    {
        __emit  0xCC;

        push eax;           // ret addr
        pushad;
        pushfd;
        call SELF_LOCALIZATION;
SELF_LOCALIZATION:
        pop  esi;
        and  esi, 0FFFFF000h;
        lodsd;
        mov  [esp+0x24], eax;   // ret addr
        lodsd;
        xor  ecx, ecx;
        push ecx;
        push esp;               // pModuleHandle
        push esi;               // ModuleFileName
        push ecx;               // Flags
        push ecx;               // PathToFile
        call eax;               // LdrLoadDll
        pop  eax;               // pop ModuleHandle
        xchg eax, esi;
        and  eax, 0FFFFF000h;
        and  dword ptr [eax], 0;
        popfd;
        popad;
        ret;
    }
}

static ASM VOID InjectRemote_LoadDllEnd() { ASM_UNIQUE() }

ASM VOID InjectRemote_IssueRemoteBreakin()
{
    INLINE_ASM
    {
        call    SELF_LOCATE;
SELF_LOCATE:
        pop     ecx;
        lea     ecx, [ecx + 14];
        push    ecx;
        xor     eax, eax;
        push    dword ptr fs:[eax];
        mov     dword ptr fs:[eax], esp;
        __emit  0xCC;

        call    SELF_LOCATE2;
SELF_LOCATE2:
        pop     esi;
        and     esi, not (MEMORY_PAGE_SIZE - 1);
        lodsd;
        mov     ebx, eax;
        lodsd;
        mov     ecx, [esi];
        jecxz   ISSUE_BREAK_IN_ONLY;

        xor     ecx, ecx;
        push    eax;    // sub esp, 4
        push    esp;    // pModuleHandle
        push    esi;    // ModuleFileName
        push    ecx;    // Flags
        push    ecx;    // PathToFile
        call    eax;    // LdrLoadDll
//        pop     eax;    // add esp, 4;

ISSUE_BREAK_IN_ONLY:

        xchg eax, esi;
        and  eax, 0FFFFF000h;
        and  dword ptr [eax], 0;

EXIT_THREAD:
//        push    0;
        push    -2;
        call    ebx;
        jmp     EXIT_THREAD;
    }
}

static ASM VOID InjectRemote_IssueRemoteBreakinEnd() { ASM_UNIQUE() }

NTSTATUS
InjectDllToRemoteProcess(
    HANDLE          ProcessHandle,
    HANDLE          ThreadHandle,
    PUNICODE_STRING DllFullPath,
    ULONG_PTR       Flags,
    PVOID*          InjectBuffer,
    ULONG_PTR       Timeout
)
{
    NTSTATUS        Status;
    PVOID           Buffer, InjectContext, ShellCode;
    ULONG_PTR       DllPathLength, InjectContextSize, ShellCodeSize;
    CONTEXT         ThreadContext;
    LARGE_INTEGER   TimeOut;

    typedef struct
    {
        ULONG           ReturnAddr;
        PVOID           xLdrLoadDll;
        UNICODE_STRING  DllPath;
    } INJECT_REMOTE_LOAD_DLL;

    typedef struct
    {
        PVOID           xNtTerminateThread;
        PVOID           xLdrLoadDll;
        UNICODE_STRING  DllPath;
    //    WCHAR           Buffer[1];
    } INJECT_REMOTE_ISSUE_BREAK_IN;

    union
    {
        INJECT_REMOTE_LOAD_DLL          LoadDll;
        INJECT_REMOTE_ISSUE_BREAK_IN    Breakin;
    };

    Buffer = NULL;
    if (FLAG_ON(Flags, INJECT_PREALLOC_BUFFER))
    {
        if (InjectBuffer == NULL)
            return STATUS_INVALID_PARAMETER;

        Buffer = *InjectBuffer;
        if (Buffer == NULL)
            return STATUS_NO_MEMORY;
    }
    else if (InjectBuffer != NULL)
    {
        *InjectBuffer = NULL;
    }

    ThreadContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    Status = NtGetContextThread(ThreadHandle, &ThreadContext);
    LEP_INJECT_DIAG(L"InjectDllToRemoteProcess32: entry");
    LEP_INJECT_DIAG_HEX(L"  NtGetContextThread=", Status);
    LEP_INJECT_DIAG_HEX(L"  OriginalEip=", ThreadContext.Eip);
    LEP_INJECT_DIAG_HEX(L"  OriginalEsp=", ThreadContext.Esp);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FLAG_ON(Flags, INJECT_PREALLOC_BUFFER))
    {
        Status = Mm::AllocVirtualMemoryEx(ProcessHandle, &Buffer, MEMORY_PAGE_SIZE);
        LEP_INJECT_DIAG_HEX(L"  AllocVirtualMemoryEx=", Status);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    LEP_INJECT_DIAG_HEX(L"  Buffer=", Buffer);

    if (FLAG_OFF(Flags, INJECT_ISSUE_BREAKIN_ONLY) && DllFullPath != nullptr)
    {
        DllPathLength = DllFullPath->Length;
    }
    else
    {
        DllPathLength = 0;
    }

    if (FLAG_ON(Flags, INJECT_ISSUE_REMOTE_BREAKIN | INJECT_ISSUE_BREAKIN_ONLY))
    {
        PVOID ntdll;

        InjectContext       = &Breakin;
        InjectContextSize   = sizeof(Breakin);
        ShellCode           = InjectRemote_IssueRemoteBreakin;
        ShellCodeSize       = PtrOffset(InjectRemote_IssueRemoteBreakinEnd, InjectRemote_IssueRemoteBreakin);

        ntdll = GetNtdllHandle();

        Breakin.xLdrLoadDll             = EATLookupRoutineByHashPNoFix(ntdll, NTDLL_LdrLoadDll);
        Breakin.xNtTerminateThread      = EATLookupRoutineByHashPNoFix(ntdll, NTDLL_NtTerminateThread);

        if (FLAG_ON(Flags, INJECT_ISSUE_BREAKIN_ONLY))
        {
            RtlInitEmptyUnicodeString(&Breakin.DllPath, NULL, 0);
        }
        else
        {
            Breakin.DllPath.Length          = (USHORT)DllPathLength;
            Breakin.DllPath.MaximumLength   = (USHORT)DllPathLength;
            Breakin.DllPath.Buffer          = (PWSTR)((ULONG_PTR)Buffer + sizeof(Breakin));
        }
    }
    else
    {
        PVOID ntdll;

        InjectContext       = &LoadDll;
        InjectContextSize   = sizeof(LoadDll);
        ShellCode           = PtrAdd(InjectRemote_LoadDll, FLAG_OFF(Flags, INJECT_ISSUE_BREAKIN));
        ShellCodeSize       = PtrOffset(InjectRemote_LoadDllEnd, InjectRemote_LoadDll);

        ntdll = GetNtdllHandle();

        LoadDll.xLdrLoadDll             = EATLookupRoutineByHashPNoFix(ntdll, NTDLL_LdrLoadDll);
        LoadDll.ReturnAddr              = ThreadContext.Eip;
        LoadDll.DllPath.Length          = (USHORT)DllPathLength;
        LoadDll.DllPath.MaximumLength   = (USHORT)DllPathLength;
        LoadDll.DllPath.Buffer          = (PWSTR)((ULONG_PTR)Buffer + sizeof(LoadDll));
        LEP_INJECT_DIAG_HEX(L"  RemoteLdrLoadDll=", LoadDll.xLdrLoadDll);
        LEP_INJECT_DIAG_HEX(L"  DllPathLength=", DllPathLength);
    }

    Status = STATUS_UNSUCCESSFUL;
    LOOP_ONCE
    {
        Status = WriteMemory(ProcessHandle, Buffer, InjectContext, InjectContextSize);
        LEP_INJECT_DIAG_HEX(L"  WriteContext=", Status);
        if (!NT_SUCCESS(Status))
            break;

        if (DllFullPath != nullptr && FLAG_OFF(Flags, INJECT_ISSUE_BREAKIN_ONLY))
        {
            Status = WriteMemory(
                        ProcessHandle,
                        (PVOID)((ULONG_PTR)Buffer + InjectContextSize),
                        DllFullPath->Buffer,
                        DllPathLength
                        );
            LEP_INJECT_DIAG_HEX(L"  WriteDllPath=", Status);
            if (!NT_SUCCESS(Status))
                break;
        }

        ThreadContext.Eip = ROUND_UP((ULONG_PTR)Buffer + InjectContextSize + DllPathLength, 16);
        LEP_INJECT_DIAG_HEX(L"  NewEip=", ThreadContext.Eip);
        Status = WriteMemory(
                    ProcessHandle,
                    (PVOID)ThreadContext.Eip,
                    ShellCode,
                    ShellCodeSize
                 );
        LEP_INJECT_DIAG_HEX(L"  WriteShellCode=", Status);
        if (!NT_SUCCESS(Status))
            break;

        NtFlushInstructionCache(ProcessHandle, (PVOID)ThreadContext.Eip, ShellCodeSize);

        if (FLAG_OFF(Flags, INJECT_DONT_CHANGE_IP))
        {
            Status = NtSetContextThread(ThreadHandle, &ThreadContext);
            LEP_INJECT_DIAG_HEX(L"  NtSetContextThread=", Status);
            if (!NT_SUCCESS(Status))
                break;

            Status = NtGetContextThread(ThreadHandle, &ThreadContext);
        }

        if (FLAG_ON(Flags, INJECT_THREAD_SUSPENDED))
        {
            LEP_INJECT_DIAG(L"InjectDllToRemoteProcess32: return suspended success");
            if (InjectBuffer != NULL)
            {
                *InjectBuffer = FLAG_ON(Flags, INJECT_POINT_TO_SHELL_CODE) ? (PVOID)ThreadContext.Eip : Buffer;
            }

            return Status;
        }

        Status = NtResumeThread(ThreadHandle, NULL);
        if (!NT_SUCCESS(Status))
            break;

        FormatTimeOut(&TimeOut, Timeout);
        for (ULONG TryTimes = 30; TryTimes; --TryTimes)
        {
            ULONG Val;
            Status = ReadMemory(ProcessHandle, Buffer, &Val, sizeof(Val));
            if (!NT_SUCCESS(Status))
                break;

            if (Val != 0)
            {
                NtDelayExecution(FALSE, &TimeOut);
                continue;
            }

            break;
        }

        if (!NT_SUCCESS(Status))
            break;

        NtDelayExecution(FALSE, &TimeOut);
        Status = NtGetContextThread(ThreadHandle, &ThreadContext);
        if (!NT_SUCCESS(Status))
            break;

        if ((ULONG_PTR)ThreadContext.Eip < (ULONG_PTR)Buffer ||
            (ULONG_PTR)ThreadContext.Eip > (ULONG_PTR)Buffer + MEMORY_PAGE_SIZE)
        {
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_UNSUCCESSFUL;
        }
    }

    if (FLAG_OFF(Flags, INJECT_PREALLOC_BUFFER))
        Mm::FreeVirtualMemory(Buffer, ProcessHandle);

    LEP_INJECT_DIAG_HEX(L"InjectDllToRemoteProcess32: exit=", Status);
    return Status;
}

#endif // x64

VOID Nt_ExitProcess(ULONG ExitCode)
{
    return ml::Native::Ps::ExitProcess(ExitCode);
}

NTSTATUS
Nt_CreateProcess2(
    CreateProcessInternal2Routine   Routine,
    HANDLE                          Token,
    PCWSTR                          ApplicationName,
    PWSTR                           CommandLine,
    PCWSTR                          CurrentDirectory,
    ULONG                           CreationFlags,
    LPSTARTUPINFOW                  StartupInfo,
    LPPROCESS_INFORMATION           ProcessInformation,
    LPSECURITY_ATTRIBUTES           ProcessAttributes,
    LPSECURITY_ATTRIBUTES           ThreadAttributes,
    PVOID                           Environment
)
{
    BOOL                            Result;
    STARTUPINFOW                    DefaultStartupInfo;
    PROCESS_INFORMATION             DefaultProcessInformation;

    SEH_TRY
    {
        if (StartupInfo == NULL)
        {
            ZeroMemory(&DefaultStartupInfo, sizeof(DefaultStartupInfo));
            DefaultStartupInfo.cb = sizeof(DefaultStartupInfo);
            StartupInfo = &DefaultStartupInfo;
        }

        if (ProcessInformation == NULL)
            ProcessInformation = &DefaultProcessInformation;

        CREATE_PROCESS_INTERNAL_PARAM Parameter =
        {
            Token,
            ApplicationName,
            CommandLine,
            ProcessAttributes,
            ThreadAttributes,
            FALSE,
            CreationFlags,
            Environment,
            CurrentDirectory,
            StartupInfo,
            ProcessInformation,
            NULL,
        };

        Result = Routine(Parameter);
        if (!Result)
            return STATUS_UNSUCCESSFUL;

        if (ProcessInformation == &DefaultProcessInformation)
        {
            NtClose(DefaultProcessInformation.hProcess);
            NtClose(DefaultProcessInformation.hThread);
        }

        return STATUS_SUCCESS;
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ;
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
Nt_CreateProcess(
    HANDLE                  Token,
    PCWSTR                  ApplicationName,
    PWSTR                   CommandLine,
    PCWSTR                  CurrentDirectory,
    ULONG                   CreationFlags,
    LPSTARTUPINFOW          StartupInfo,
    LPPROCESS_INFORMATION   ProcessInformation,
    LPSECURITY_ATTRIBUTES   ProcessAttributes,
    LPSECURITY_ATTRIBUTES   ThreadAttributes,
    PVOID                   Environment
)
{
    BOOL                            Result;
    STARTUPINFOW                    DefaultStartupInfo;
    PROCESS_INFORMATION             DefaultProcessInformation;

    if (StartupInfo == NULL)
    {
        ZeroMemory(&DefaultStartupInfo, sizeof(DefaultStartupInfo));
        DefaultStartupInfo.cb = sizeof(DefaultStartupInfo);
        StartupInfo = &DefaultStartupInfo;
    }

    if (ProcessInformation == NULL)
        ProcessInformation = &DefaultProcessInformation;

#if ML_DISABLE_THIRD_LIB

    Result = CreateProcessAsUserW(
                    Token,
                    ApplicationName,
                    (PWSTR)CommandLine,
                    ProcessAttributes,
                    ThreadAttributes,
                    FALSE,
                    CreationFlags,
                    Environment,
                    CurrentDirectory,
                    StartupInfo,
                    ProcessInformation
             );

#else

    Result = CreateProcessInternalW(
                    Token,
                    ApplicationName,
                    CommandLine,
                    ProcessAttributes,
                    ThreadAttributes,
                    FALSE,
                    CreationFlags,
                    Environment,
                    CurrentDirectory,
                    StartupInfo,
                    ProcessInformation,
                    NULL
             );

#endif // ML_DISABLE_THIRD_LIB

    if (!Result)
        return STATUS_UNSUCCESSFUL;

    if (ProcessInformation == &DefaultProcessInformation)
    {
        NtClose(DefaultProcessInformation.hProcess);
        NtClose(DefaultProcessInformation.hThread);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Nt_CreateThread(
    PVOID                   StartAddress,
    PVOID                   StartParameter,
    BOOL                    CreateSuspended,
    HANDLE                  ProcessHandle,
    PHANDLE                 ThreadHandle,
    PCLIENT_ID              ClientID,
    ULONG                   StackZeroBits,
    ULONG                   StackReserved,
    ULONG                   StackCommit,
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
                StackZeroBits,
                StackReserved,
                StackCommit,
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

NTSTATUS
Nt_SetThreadPriority(
    HANDLE  ThreadHandle,
    LONG    Priority
)
{
    NTSTATUS Status;

    switch (Priority)
    {
        case THREAD_PRIORITY_TIME_CRITICAL:
            Priority = (HIGH_PRIORITY + 1) / 2;
            break;

        case THREAD_PRIORITY_IDLE:
            Priority = -((HIGH_PRIORITY + 1) / 2);
            break;
    }

    Status = NtSetInformationThread(
                ThreadHandle,
                ThreadBasePriority,
                &Priority,
                sizeof(LONG)
             );

    return Status;
}

PLDR_MODULE Nt_FindLdrModuleByName(PUNICODE_STRING ModuleName)
{
    return ml::Native::Ldr::FindLdrModuleByName(ModuleName);
}

PLDR_MODULE Nt_FindLdrModuleByHandle(PVOID BaseAddress)
{
    return ml::Native::Ldr::FindLdrModuleByHandle(BaseAddress);
}

PTEB_ACTIVE_FRAME Nt_FindThreadFrameByContext(ULONG_PTR Context)
{
    return ml::Native::Ps::FindThreadFrame(Context);
}


/************************************************************************
  path
************************************************************************/

ULONG
Nt_GetSystemDirectory64(
    PWSTR Buffer,
    ULONG BufferCount
)
{
    ULONG                           Length, ResultLength;
    NTSTATUS                        Status;
    PKEY_VALUE_PARTIAL_INFORMATION  ValuePartial;

    Length = MAX_NTPATH * 2 + sizeof(*ValuePartial);
    ValuePartial = (PKEY_VALUE_PARTIAL_INFORMATION)AllocStack(Length);

    LOOP_FOREVER
    {
        Status = Nt_RegGetValue(
                    HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer",
                    L"InstallerLocation",
                    KeyValuePartialInformation,
                    ValuePartial,
                    Length,
                    &ResultLength,
                    KEY_WOW64_64KEY
                 );
        if (Status != STATUS_BUFFER_TOO_SMALL || ResultLength <= Length)
            break;

        ValuePartial = (PKEY_VALUE_PARTIAL_INFORMATION)AllocStack(ResultLength - Length);
        Length = ResultLength;
    }

    if (!NT_SUCCESS(Status))
        return 0;

    Length = MY_MIN(ValuePartial->DataLength, BufferCount * sizeof(WCHAR));
    CopyMemory(Buffer, ValuePartial->Data, Length);

    Length /= sizeof(WCHAR);
    while (Buffer[Length - 1] == 0)
        --Length;

    if (Length < BufferCount)
    {
        if (Buffer[Length - 1] != '\\')
            Buffer[Length++] = '\\';
    }

    if (Length < BufferCount)
        Buffer[Length] = 0;

    return Length;
}

ULONG Nt_GetWindowsDirectory(PWCHAR Buffer, ULONG BufferCount)
{
    ULONG Length;
    PLDR_MODULE LdrModule;

    LdrModule = FIELD_BASE(Nt_CurrentPeb()->Ldr->InInitializationOrderModuleList.Flink, LDR_MODULE, InInitializationOrderLinks);
    Length = (LdrModule->FullDllName.Length - LdrModule->BaseDllName.Length) / sizeof(WCHAR) - 2;

    while (LdrModule->FullDllName.Buffer[Length] != '\\' && --Length);
    if (Length == 0)
        return 0;

    ++Length;
    Length *= sizeof(WCHAR);
    Length = MY_MIN(Length, BufferCount * sizeof(WCHAR));
    CopyMemory(Buffer, LdrModule->FullDllName.Buffer, Length);
    Length /= sizeof(WCHAR);
    if (Length < BufferCount)
        Buffer[Length] = 0;

    return Length;
}

ULONG Nt_GetSystemDirectory(PWCHAR Buffer, ULONG BufferCount)
{
    ULONG Length;
    PLDR_MODULE LdrModule;

    LdrModule = FIELD_BASE(Nt_CurrentPeb()->Ldr->InInitializationOrderModuleList.Flink, LDR_MODULE, InInitializationOrderLinks);
    Length = LdrModule->FullDllName.Length - LdrModule->BaseDllName.Length;

    if (Buffer == NULL)
        return Length / sizeof(WCHAR);

    Length = MY_MIN(Length, BufferCount * sizeof(WCHAR));
    CopyMemory(Buffer, LdrModule->FullDllName.Buffer, Length);
    Length /= sizeof(WCHAR);
    if (Length < BufferCount)
        Buffer[Length] = 0;

    return Length;
}

ULONG_PTR
Nt_GetDosPathFromNtDeviceName(
    PWSTR           DosPath,
    ULONG_PTR       DosPathLength,
    PUNICODE_STRING NtDeviceName
)
{
    ULONG_PTR       Length;
    NTSTATUS        Status;
    UNICODE_STRING  DosPathString;

    Status = Io::QueryDosPathFromNtDeviceName(&DosPathString, NtDeviceName);
    if (NT_FAILED(Status))
        return 0;

    DosPathLength = InternalCopyUnicodeString(&DosPathString, DosPath, DosPathLength);
    RtlFreeUnicodeString(&DosPathString);

    return DosPathLength;
}

NTSTATUS
Nt_GetDosLetterFromVolumeDeviceName(
    PUNICODE_STRING DosLetter,
    PUNICODE_STRING NtDeviceName
)
{
    return Io::QueryDosLetterFromVolumeDeviceName(DosLetter, NtDeviceName);
}

NTSTATUS Nt_GetDosPathFromHandle(PUNICODE_STRING DosPath, HANDLE FileHandle)
{
    return Io::QueryDosPathFromHandle(DosPath, FileHandle);
}

NTSTATUS Nt_NtPathNameToDosPathName(PUNICODE_STRING DosPath, PUNICODE_STRING NtPath)
{
    return Rtl::NtPathNameToDosPathName(DosPath, NtPath);
}

VOID ConvertFindData(PWIN32_FIND_DATAW W32, PML_FIND_DATA ML)
{
    CopyStruct(W32->cFileName, ML->FileName, sizeof(W32->cFileName));
    CopyStruct(W32->cAlternateFileName, ML->AlternateFileName, sizeof(W32->cAlternateFileName));

    W32->dwFileAttributes = ML->FileAttributes;
    W32->ftCreationTime.dwLowDateTime = ML->CreationTime.LowPart;
    W32->ftCreationTime.dwHighDateTime = ML->CreationTime.HighPart;

    W32->ftLastWriteTime.dwLowDateTime = ML->LastWriteTime.LowPart;
    W32->ftLastWriteTime.dwHighDateTime = ML->LastWriteTime.HighPart;

    W32->ftLastAccessTime.dwLowDateTime = ML->LastAccessTime.LowPart;
    W32->ftLastAccessTime.dwHighDateTime = ML->LastAccessTime.HighPart;

    W32->dwReserved0 = ML->Reserved0;
    W32->dwReserved1 = ML->Reserved1;

    W32->nFileSizeLow = ML->FileSize.LowPart;
    W32->nFileSizeHigh = ML->FileSize.HighPart;
}

BOOL Nt_FindClose(HANDLE FindFileHandle)
{
    return NT_SUCCESS(Io::QueryClose(FindFileHandle));
}

HANDLE Nt_FindFirstFile(LPCWSTR FileName, PWIN32_FIND_DATAW FindFileData)
{
    NTSTATUS        Status;
    HANDLE          DirectoryHandle;
    ML_FIND_DATA    FindData;

    Status = Io::QueryFirstFile(&DirectoryHandle, FileName, &FindData);
    if (NT_SUCCESS(Status))
        ConvertFindData(FindFileData, &FindData);

    return NT_SUCCESS(Status) ? DirectoryHandle : INVALID_HANDLE_VALUE;
}

BOOL Nt_FindNextFile(HANDLE FindFileHandle, PWIN32_FIND_DATAW FindFileData)
{
    NTSTATUS        Status;
    ML_FIND_DATA    FindData;

    Status = Io::QueryNextFile(FindFileHandle, &FindData);
    if (NT_SUCCESS(Status))
        ConvertFindData(FindFileData, &FindData);

    return NT_SUCCESS(Status);
}

ULONG Nt_GetFileAttributes(LPCWSTR FileName)
{
    return Io::QueryFileAttributes(FileName);
}

NTSTATUS Nt_MoveFile(LPCWSTR ExistingFileName, LPCWSTR NewFileName)
{
    return Io::MoveFile(ExistingFileName, NewFileName);
}

NTSTATUS Nt_CopyFile(PCWSTR ExistingFileName, PCWSTR NewFileName, BOOL FailIfExists)
{
    return Io::CopyFile(ExistingFileName, NewFileName, FailIfExists);
}

BOOL Nt_IsPathExists(LPCWSTR pszPath)
{
    return Io::IsPathExists(pszPath);
}

ULONG_PTR Nt_GetModulePath(PVOID ModuleBase, PWSTR Path, ULONG_PTR BufferCount)
{
    PLDR_MODULE LdrModule;
    ULONG_PTR   PathLength, ReturnLength;

    LdrModule   = Nt_FindLdrModuleByHandle(ModuleBase);
    ReturnLength = (LdrModule->FullDllName.Length - LdrModule->BaseDllName.Length) / sizeof(WCHAR);
    PathLength = MY_MIN(ReturnLength, BufferCount);

    InternalCopyUnicodeString(&LdrModule->FullDllName, Path, PathLength);
    if (PathLength < BufferCount)
        Path[PathLength] = 0;

    return ReturnLength;
}

ULONG Nt_GetExeDirectory(PWCHAR Path, ULONG BufferCount)
{
    return Nt_GetModulePath(NULL, Path, BufferCount);
}

ULONG_PTR Nt_GetModuleFileName(PVOID ModuleBase, LPWSTR Filename, ULONG_PTR BufferCount)
{
    ULONG_PTR               Length;
    PEB_BASE               *Peb;
    PLDR_DATA_TABLE_ENTRY   LdrModule, FirstLdrModule;

    Peb = Nt_CurrentPeb();
    LdrModule = FIELD_BASE(Peb->Ldr->InLoadOrderModuleList.Flink, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

    FirstLdrModule = LdrModule;

    if (ModuleBase == NULL)
        ModuleBase = Peb->ImageBaseAddress;

    LOOP_FOREVER
    {
        if ((ULONG_PTR)ModuleBase >= (ULONG_PTR)LdrModule->DllBase &&
            (ULONG_PTR)ModuleBase < (ULONG_PTR)LdrModule->DllBase + LdrModule->SizeOfImage)
        {
            break;
        }

        LdrModule = (PLDR_DATA_TABLE_ENTRY)LdrModule->InLoadOrderLinks.Flink;
        if (LdrModule == FirstLdrModule)
            return 0;
    }

    Length = InternalCopyUnicodeString(&LdrModule->FullDllName, Filename, BufferCount);
    if (LdrModule->FullDllName.Length / sizeof(WCHAR) < BufferCount)
        Filename[Length] = 0;
/*
    Length = min(nSize * sizeof(*lpFilename), LdrModule->FullDllName.Length);
    CopyMemory(lpFilename, LdrModule->FullDllName.Buffer, Length);
    Length /= sizeof(*lpFilename);
    if (Length < nSize)
        lpFilename[Length] = 0;
*/
    return Length;
}

BOOL Nt_SetExeDirectoryAsCurrent()
{
    return NT_SUCCESS(Rtl::SetExeDirectoryAsCurrent());
}

/************************************************************************
  misc
************************************************************************/

PVOID Nt_LoadLibrary(PWSTR ModuleFileName)
{
    return ml::Native::Ldr::LoadDll(ModuleFileName);
}

PVOID FASTCALL Nt_GetProcAddress(PVOID ModuleBase, LPCSTR lpProcName)
{
    return Ldr::GetRoutineAddress(ModuleBase, lpProcName);
}

PVOID Nt_GetModuleHandle(LPCWSTR lpModuleName)
{
    UNICODE_STRING  ModuleFileName;
    PLDR_MODULE     Module;

    RtlInitUnicodeString(&ModuleFileName, lpModuleName);
    Module = Ldr::FindLdrModuleByName(&ModuleFileName);
    return Module == NULL ? NULL : Module->DllBase;
}

VOID Nt_Sleep(ULONG_PTR Milliseconds, BOOL Alertable)
{
    return Ps::Sleep(Milliseconds, Alertable);
}


_ML_C_TAIL_

#pragma warning(default:4245)
#pragma warning(default:4750)
