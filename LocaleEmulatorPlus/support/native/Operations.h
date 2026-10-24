#pragma once
#include "Path.h"
#include "RegistrySupport.h"
#include "../runtime/String.h"

namespace lep
{
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

ML_NAMESPACE_END_(Io);

#endif // _MLNTFILEIO_H_03daa20f_84f6_4731_b996_cf4155164012_
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

}
