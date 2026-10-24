#ifndef _NTMISC_H_e2789f20_6dd7_48d9_841c_b38cc5e46666_
#define _NTMISC_H_e2789f20_6dd7_48d9_841c_b38cc5e46666_

#ifndef _NTPROCESS_H_39560d0d_86be_4220_839a_0fcdb8e556cf_
#define _NTPROCESS_H_39560d0d_86be_4220_839a_0fcdb8e556cf_




/************************************************************************
  process information
************************************************************************/

typedef struct _PROCESS_TIMES
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER ExitTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;

} PROCESS_TIMES, *PPROCESS_TIMES;

typedef struct
{
    UNICODE_STRING  ImageFileName;
    WCHAR           Buffer[1];
} PROCESS_IMAGE_FILE_NAME, *PPROCESS_IMAGE_FILE_NAME;

typedef struct
{
    UNICODE_STRING  ImageFileName;
    WCHAR           Buffer[MAX_NTPATH];
} PROCESS_IMAGE_FILE_NAME2;



typedef struct
{
    ULONG       Length;                             // +0x00
    BOOL        Initialized;                        // +0x04
    PVOID       SsHandle;                           // +0x08
    LIST_ENTRY  InLoadOrderModuleList;              // +0x0c
    LIST_ENTRY  InMemoryOrderModuleList;            // +0x14
    LIST_ENTRY  InInitializationOrderModuleList;    // +0x1c
    PVOID       EntryInProgress;                    // +0x24
    ULONG       ShutdownInProgress;                 // +0x28
    ULONG_PTR   ShutdownThreadId;                   // +0x2C
} PEB_LDR_DATA, *PPEB_LDR_DATA;                     // +0x30

typedef struct
{
    UNICODE_STRING  DosPath;
    HANDLE          Handle;
} CURDIR;

typedef struct _RTLP_CURDIR_REF *PRTLP_CURDIR_REF;

typedef struct _RTL_RELATIVE_NAME_U
{
    UNICODE_STRING RelativeName;
    HANDLE ContainingDirectory;
    PRTLP_CURDIR_REF CurDirRef;
} RTL_RELATIVE_NAME_U, *PRTL_RELATIVE_NAME_U;

typedef struct
{
/* 0x000 */ USHORT      Flags;
/* 0x002 */ USHORT      Length;
/* 0x004 */ ULONG       TimeStamp;
/* 0x008 */ ANSI_STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR;

#define USER_PROCESS_PARAMETERS_NORMALIZED  (1u << 0)

typedef struct
{
/* 0x000 */ ULONG                   MaximumLength;
/* 0x004 */ ULONG                   Length;
/* 0x008 */ ULONG                   Flags;
/* 0x00c */ ULONG                   DebugFlags;
/* 0x010 */ HANDLE                  ConsoleHandle;
/* 0x014 */ ULONG                   ConsoleFlags;
/* 0x018 */ HANDLE                  StandardInput;
/* 0x01c */ HANDLE                  StandardOutput;
/* 0x020 */ HANDLE                  StandardError;
/* 0x024 */ CURDIR                  CurrentDirectory;
/* 0x030 */ UNICODE_STRING          DllPath;
/* 0x038 */ UNICODE_STRING          ImagePathName;
/* 0x040 */ UNICODE_STRING          CommandLine;
/* 0x048 */ PWCHAR                  Environment;
/* 0x04c */ ULONG                   StartingX;
/* 0x050 */ ULONG                   StartingY;
/* 0x054 */ ULONG                   CountX;
/* 0x058 */ ULONG                   CountY;
/* 0x05c */ ULONG                   CountCharsX;
/* 0x060 */ ULONG                   CountCharsY;
/* 0x064 */ ULONG                   FillAttribute;
/* 0x068 */ ULONG                   WindowFlags;
/* 0x06c */ ULONG                   ShowWindowFlags;
/* 0x070 */ UNICODE_STRING          WindowTitle;
/* 0x078 */ UNICODE_STRING          DesktopInfo;
/* 0x080 */ UNICODE_STRING          ShellInfo;
/* 0x088 */ UNICODE_STRING          RuntimeData;
/* 0x090 */ RTL_DRIVE_LETTER_CURDIR CurrentDirectores[32];
/* 0x290 */ ULONG_PTR               EnvironmentSize;
/* 0x294 */ ULONG_PTR               EnvironmentVersion;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

//  NtCreateUserProcess defs

typedef enum _PS_CREATE_STATE
{
    PsCreateInitialState            = 0,
    PsCreateFailOnFileOpen          = 1,
    PsCreateFailOnSectionCreate     = 2,
    PsCreateFailExeFormat           = 3,
    PsCreateFailMachineMismatch     = 4,
    PsCreateFailExeName             = 5,    // Debugger specified
    PsCreateSuccess                 = 6,

    PsCreateMaximumStates

} PS_CREATE_STATE;

typedef struct _PS_CREATE_INFO
{
    SIZE_T Size;
    PS_CREATE_STATE State;
    union
    {
        // PsCreateInitialState
        struct
        {
            union
            {
                ULONG InitFlags;
                struct
                {
                    UCHAR WriteOutputOnExit : 1;
                    UCHAR DetectManifest : 1;
                    UCHAR SpareBits1 : 6;
                    UCHAR IFEOKeyState : 2; // PS_IFEO_KEY_STATE
                    UCHAR SpareBits2 : 6;
                    USHORT ProhibitedImageCharacteristics : 16;
                };
            };
            ACCESS_MASK AdditionalFileAccess;
        } InitState;

        // PsCreateFailOnSectionCreate
        struct
        {
            HANDLE FileHandle;
        } FailSection;

        // PsCreateFailExeName
        struct
        {
            HANDLE IFEOKey;
        } ExeName;

        // PsCreateSuccess
        struct
        {
            union
            {
                ULONG OutputFlags;
                struct
                {
                    UCHAR ProtectedProcess : 1;
                    UCHAR AddressSpaceOverride : 1;
                    UCHAR DevOverrideEnabled : 1; // from Image File Execution Options
                    UCHAR ManifestDetected : 1;
                    UCHAR SpareBits1 : 4;
                    UCHAR SpareBits2 : 8;
                    USHORT SpareBits3 : 16;
                };
            };

            HANDLE FileHandle;
            HANDLE SectionHandle;
            ULONGLONG UserProcessParametersNative;
            ULONG UserProcessParametersWow64;
            ULONG CurrentParameterFlags;
            ULONGLONG PebAddressNative;
            ULONG PebAddressWow64;
            ULONGLONG ManifestAddress;
            ULONG ManifestSize;
        } SuccessState;
    };
} PS_CREATE_INFO, *PPS_CREATE_INFO;

// Attributes

// begin_rev
#define PS_ATTRIBUTE_NUMBER_MASK    0x0000FFFF
#define PS_ATTRIBUTE_THREAD         0x00010000 // can be used with threads
#define PS_ATTRIBUTE_INPUT          0x00020000 // input only
#define PS_ATTRIBUTE_UNKNOWN        0x00040000

#define PS_ATTRIBUTE_FLAG_THREAD    0x0001 // can be used with threads
#define PS_ATTRIBUTE_FLAG_INPUT     0x0002 // input only
#define PS_ATTRIBUTE_FLAG_UNKNOWN   0x0004

#define PS_ATTRIBUTE_FLAG_INPUT_HANDLE   (PS_ATTRIBUTE_FLAG_INPUT | PS_ATTRIBUTE_FLAG_UNKNOWN)

// end_rev

// private
typedef enum _PS_ATTRIBUTE_NUM
{
    PsAttributeParentProcess    = 0, // in HANDLE
    PsAttributeDebugPort        = 1, // in HANDLE
    PsAttributeToken            = 2, // in HANDLE
    PsAttributeClientId         = 3, // out PCLIENT_ID
    PsAttributeTebAddress       = 4, // out PTEB *
    PsAttributeImageName        = 5, // in PWSTR
    PsAttributeImageInfo        = 6, // out PSECTION_IMAGE_INFORMATION
    PsAttributeMemoryReserve    = 7, // in PPS_MEMORY_RESERVE
    PsAttributePriorityClass    = 8, // in UCHAR
    PsAttributeErrorMode        = 9, // in ULONG
    PsAttributeStdHandleInfo    = 10, // in PPS_STD_HANDLE_INFO
    PsAttributeHandleList       = 11, // in PHANDLE
    PsAttributeGroupAffinity    = 12, // in PGROUP_AFFINITY
    PsAttributePreferredNode    = 13, // in PUSHORT
    PsAttributeIdealProcessor   = 14, // in PPROCESSOR_NUMBER
    PsAttributeUmsThread        = 15, // ? in PUMS_CREATE_THREAD_ATTRIBUTES
    PsAttributeMitigationOptions= 16, // in UCHAR
    PsAttributeMax

} PS_ATTRIBUTE_NUM;

typedef struct _PS_STD_HANDLE_INFO
{
    union
    {
/* 0x000 */ ULONG                          Flags;

        struct
        {
/* 0x000 */     UCHAR                      StdHandleState : 2;
/* 0x000 */     UCHAR                      PseudoHandleMask : 3;
        };
    };

/* 0x004 */ ULONG                          StdHandleSubsystemType;

} PS_STD_HANDLE_INFO, *PPS_STD_HANDLE_INFO;

typedef struct _PS_MEMORY_RESERVE
{
/* 0x000 */ PVOID   ReserveAddress;
/* 0x004 */ ULONG   ReserveSize;

} PS_MEMORY_RESERVE, *PPS_MEMORY_RESERVE;

// begin_rev

#define PsAttributeValue(Number, Thread, Input, Unknown) \
    (((Number) & PS_ATTRIBUTE_NUMBER_MASK) | \
    ((Thread) ? PS_ATTRIBUTE_THREAD : 0) | \
    ((Input) ? PS_ATTRIBUTE_INPUT : 0) | \
    ((Unknown) ? PS_ATTRIBUTE_UNKNOWN : 0))

#define PS_ATTRIBUTE_PARENT_PROCESS         PsAttributeValue(PsAttributeParentProcess, FALSE, TRUE, TRUE)
#define PS_ATTRIBUTE_DEBUG_PORT             PsAttributeValue(PsAttributeDebugPort, FALSE, TRUE, TRUE)
#define PS_ATTRIBUTE_TOKEN                  PsAttributeValue(PsAttributeToken, FALSE, TRUE, TRUE)
#define PS_ATTRIBUTE_CLIENT_ID              PsAttributeValue(PsAttributeClientId, TRUE, FALSE, FALSE)
#define PS_ATTRIBUTE_TEB_ADDRESS            PsAttributeValue(PsAttributeTebAddress, TRUE, FALSE, FALSE)
#define PS_ATTRIBUTE_IMAGE_NAME             PsAttributeValue(PsAttributeImageName, FALSE, TRUE, FALSE)
#define PS_ATTRIBUTE_IMAGE_INFO             PsAttributeValue(PsAttributeImageInfo, FALSE, FALSE, FALSE)
#define PS_ATTRIBUTE_MEMORY_RESERVE         PsAttributeValue(PsAttributeMemoryReserve, FALSE, TRUE, FALSE)
#define PS_ATTRIBUTE_PRIORITY_CLASS         PsAttributeValue(PsAttributePriorityClass, FALSE, TRUE, FALSE)
#define PS_ATTRIBUTE_ERROR_MODE             PsAttributeValue(PsAttributeErrorMode, FALSE, TRUE, FALSE)
#define PS_ATTRIBUTE_STD_HANDLE_INFO        PsAttributeValue(PsAttributeStdHandleInfo, FALSE, TRUE, FALSE)
#define PS_ATTRIBUTE_HANDLE_LIST            PsAttributeValue(PsAttributeHandleList, FALSE, TRUE, FALSE)
#define PS_ATTRIBUTE_GROUP_AFFINITY         PsAttributeValue(PsAttributeGroupAffinity, TRUE, TRUE, FALSE)
#define PS_ATTRIBUTE_PREFERRED_NODE         PsAttributeValue(PsAttributePreferredNode, FALSE, TRUE, FALSE)
#define PS_ATTRIBUTE_IDEAL_PROCESSOR        PsAttributeValue(PsAttributeIdealProcessor, TRUE, TRUE, FALSE)
#define PS_ATTRIBUTE_MITIGATION_OPTIONS     PsAttributeValue(PsAttributeMitigationOptions, FALSE, TRUE, TRUE)

// end_rev

// begin_private

typedef struct _PS_ATTRIBUTE
{
//    ULONG       Attribute;
    USHORT      AttributeNumber;
    USHORT      AttributeFlags;

    ULONG_PTR   Size;

    union
    {
        ULONG_PTR   Value;
        PVOID       ValuePtr;
    };

    PULONG_PTR  ReturnLength;

} PS_ATTRIBUTE, *PPS_ATTRIBUTE;

typedef struct _PS_ATTRIBUTE_LIST
{
    ULONG_PTR       TotalLength;
    PS_ATTRIBUTE    Attributes[1];
} PS_ATTRIBUTE_LIST, *PPS_ATTRIBUTE_LIST;

#define PROCESS_CREATE_FLAGS_BREAKAWAY              0x00000001
#define PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT       0x00000002
#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES        0x00000004
#define PROCESS_CREATE_FLAGS_OVERRIDE_ADDRESS_SPACE 0x00000008
#define PROCESS_CREATE_FLAGS_LARGE_PAGES            0x00000010

#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED        0x00000001
#define THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH      0x00000002 // ?
#define THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER      0x00000004
#define THREAD_CREATE_FLAGS_HAS_SECURITY_DESCRIPTOR 0x00000010 // ?
#define THREAD_CREATE_FLAGS_ACCESS_CHECK_IN_TARGET  0x00000020 // ?
#define THREAD_CREATE_FLAGS_INITIAL_THREAD          0x00000080

#if CPP_DEFINED

IF_NOT_EXIST(EXCEPTION_ROUTINE)
{
    typedef
    EXCEPTION_DISPOSITION
    STDCALL
    EXCEPTION_ROUTINE(
        PEXCEPTION_RECORD   ExceptionRecord,
        PVOID               EstablisherFrame,
        PCONTEXT            ContextRecord,
        PVOID               DispatcherContext
    );
}

#endif // CPP_DEFINED

#ifndef _WIN32_WINNT_WIN8

typedef struct _EXCEPTION_REGISTRATION_RECORD
{
/* 0x000 */ struct _EXCEPTION_REGISTRATION_RECORD   *Next;
/* 0x004 */ EXCEPTION_ROUTINE                       *Handler;
} EXCEPTION_REGISTRATION_RECORD, *PEXCEPTION_REGISTRATION_RECORD;

#endif // _WIN32_WINNT_WIN8


typedef struct
{
    UNICODE_STRING  WindowsDirectory;
    UNICODE_STRING  SystemDirectory;
    UNICODE_STRING  BaseNamedObjects;
    ULONG           Unknown;
    USHORT          Unknown2;
    USHORT          ServicePackLength;
    USHORT          ServicePackMaximumLength;
    WCHAR           ServicePack[1];

} *PSTATIC_SERVER_DATA;

typedef struct
{
    UNICODE_STRING64    WindowsDirectory;
    UNICODE_STRING64    SystemDirectory;
    UNICODE_STRING64    BaseNamedObjects;
    ULONG               Unknown;
    USHORT              Unknown2;
    USHORT              ServicePackLength;
    USHORT              ServicePackMaximumLength;
    WCHAR               ServicePack[1];

} *PSTATIC_SERVER_DATA64;

typedef struct
{
    PVOID               Reserve;
    PSTATIC_SERVER_DATA StaticServerData;

} *STATIC_SERVER_DATA_PTR;

//
// Global flags that can be set to control system behavior.
// Flag word is 32 bits.
//

#define FLG_STOP_ON_EXCEPTION           0x00000001      // user and kernel mode
#define FLG_SHOW_LDR_SNAPS              0x00000002      // user and kernel mode
#define FLG_DEBUG_INITIAL_COMMAND       0x00000004      // kernel mode only up until WINLOGON started
#define FLG_STOP_ON_HUNG_GUI            0x00000008      // kernel mode only while running

#define FLG_HEAP_ENABLE_TAIL_CHECK      0x00000010      // user mode only
#define FLG_HEAP_ENABLE_FREE_CHECK      0x00000020      // user mode only
#define FLG_HEAP_VALIDATE_PARAMETERS    0x00000040      // user mode only
#define FLG_HEAP_VALIDATE_ALL           0x00000080      // user mode only

#define FLG_APPLICATION_VERIFIER        0x00000100      // user mode only
#define FLG_POOL_ENABLE_TAGGING         0x00000400      // kernel mode only
#define FLG_HEAP_ENABLE_TAGGING         0x00000800      // user mode only

#define FLG_USER_STACK_TRACE_DB         0x00001000      // x86 user mode only
#define FLG_KERNEL_STACK_TRACE_DB       0x00002000      // x86 kernel mode only at boot time
#define FLG_MAINTAIN_OBJECT_TYPELIST    0x00004000      // kernel mode only at boot time
#define FLG_HEAP_ENABLE_TAG_BY_DLL      0x00008000      // user mode only

#define FLG_DISABLE_STACK_EXTENSION     0x00010000      // user mode only
#define FLG_ENABLE_CSRDEBUG             0x00020000      // kernel mode only at boot time
#define FLG_ENABLE_KDEBUG_SYMBOL_LOAD   0x00040000      // kernel mode only
#define FLG_DISABLE_PAGE_KERNEL_STACKS  0x00080000      // kernel mode only at boot time

#define FLG_ENABLE_SYSTEM_CRIT_BREAKS   0x00100000      // user mode only
#define FLG_HEAP_DISABLE_COALESCING     0x00200000      // user mode only
#define FLG_ENABLE_CLOSE_EXCEPTIONS     0x00400000      // kernel mode only
#define FLG_ENABLE_EXCEPTION_LOGGING    0x00800000      // kernel mode only

#define FLG_ENABLE_HANDLE_TYPE_TAGGING  0x01000000      // kernel mode only
#define FLG_HEAP_PAGE_ALLOCS            0x02000000      // user mode only
#define FLG_DEBUG_INITIAL_COMMAND_EX    0x04000000      // kernel mode only up until WINLOGON started
#define FLG_DISABLE_DBGPRINT            0x08000000      // kernel mode only

#define FLG_CRITSEC_EVENT_CREATION      0x10000000      // user mode only, Force early creation of resource events
#define FLG_LDR_TOP_DOWN                0x20000000      // user mode only, win64 only
#define FLG_ENABLE_HANDLE_EXCEPTIONS    0x40000000      // kernel mode only
#define FLG_DISABLE_PROTDLLS            0x80000000      // user mode only (smss/winlogon)

#define FLG_VALID_BITS                  0xFFFFFDFF

#define FLG_USERMODE_VALID_BITS        (FLG_STOP_ON_EXCEPTION           | \
                                        FLG_SHOW_LDR_SNAPS              | \
                                        FLG_HEAP_ENABLE_TAIL_CHECK      | \
                                        FLG_HEAP_ENABLE_FREE_CHECK      | \
                                        FLG_HEAP_VALIDATE_PARAMETERS    | \
                                        FLG_HEAP_VALIDATE_ALL           | \
                                        FLG_APPLICATION_VERIFIER        | \
                                        FLG_HEAP_ENABLE_TAGGING         | \
                                        FLG_USER_STACK_TRACE_DB         | \
                                        FLG_HEAP_ENABLE_TAG_BY_DLL      | \
                                        FLG_DISABLE_STACK_EXTENSION     | \
                                        FLG_ENABLE_SYSTEM_CRIT_BREAKS   | \
                                        FLG_HEAP_DISABLE_COALESCING     | \
                                        FLG_DISABLE_PROTDLLS            | \
                                        FLG_HEAP_PAGE_ALLOCS            | \
                                        FLG_CRITSEC_EVENT_CREATION      | \
                                        FLG_LDR_TOP_DOWN)

#define FLG_BOOTONLY_VALID_BITS        (FLG_KERNEL_STACK_TRACE_DB       | \
                                        FLG_MAINTAIN_OBJECT_TYPELIST    | \
                                        FLG_ENABLE_CSRDEBUG             | \
                                        FLG_DEBUG_INITIAL_COMMAND       | \
                                        FLG_DEBUG_INITIAL_COMMAND_EX    | \
                                        FLG_DISABLE_PAGE_KERNEL_STACKS)

#define FLG_KERNELMODE_VALID_BITS      (FLG_STOP_ON_EXCEPTION           | \
                                        FLG_SHOW_LDR_SNAPS              | \
                                        FLG_STOP_ON_HUNG_GUI            | \
                                        FLG_POOL_ENABLE_TAGGING         | \
                                        FLG_ENABLE_KDEBUG_SYMBOL_LOAD   | \
                                        FLG_ENABLE_CLOSE_EXCEPTIONS     | \
                                        FLG_ENABLE_EXCEPTION_LOGGING    | \
                                        FLG_ENABLE_HANDLE_TYPE_TAGGING  | \
                                        FLG_DISABLE_DBGPRINT            | \
                                        FLG_ENABLE_HANDLE_EXCEPTIONS      \
                                       )

typedef struct PEB_BASE
{
#if ML_AMD64
/* 0x000 */ UCHAR                           InheritedAddressSpace;
/* 0x001 */ UCHAR                           ReadImageFileExecOptions;
/* 0x002 */ UCHAR                           BeingDebugged;
/* 0x003 */ struct
            {
                UCHAR                       ImageUsesLargePages             : 1;
                UCHAR                       IsProtectedProcess              : 1;
                UCHAR                       IsImageDynamicallyRelocated     : 1;
                UCHAR                       SkipPatchingUser32Forwarders    : 1;
                UCHAR                       IsPackagedProcess               : 1;
                UCHAR                       IsAppContainer                  : 1;
                UCHAR                       IsProtectedProcessLight         : 1;
                UCHAR                       IsLongPathAwareProcess          : 1;
            };
/* 0x004 */ UCHAR                           Padding0[4];
/* 0x008 */ PVOID                           Mutant;
/* 0x010 */ PVOID                           ImageBaseAddress;
/* 0x018 */ PPEB_LDR_DATA                   Ldr;
/* 0x020 */ PRTL_USER_PROCESS_PARAMETERS    ProcessParameters;
/* 0x028 */ PVOID                           SubSystemData;
/* 0x030 */ HANDLE                          ProcessHeap;
/* 0x038 */ PRTL_CRITICAL_SECTION           FastPebLock;
/* 0x040 */ PVOID                           AtlThunkSListPtr;
/* 0x048 */ PVOID                           IFEOKey;
/* 0x050 */ union
            {
                ULONG                       CrossProcessFlags;
                struct
                {
                    UCHAR                   ProcessInJob           : 1;
                    UCHAR                   ProcessInitializing    : 1;
                    UCHAR                   ProcessUsingVEH        : 1;
                    UCHAR                   ProcessUsingVCH        : 1;
                    UCHAR                   ProcessUsingFTH        : 1;
                };
            };
/* 0x054 */ UCHAR                           Padding1[4];
/* 0x058 */ union
            {
                PVOID                       KernelCallbackTable;
                PVOID                       UserSharedInfoPtr;
            };
/* 0x060 */ ULONG                           SystemReserved[1];
/* 0x064 */ ULONG                           AtlThunkSListPtr32;
/* 0x068 */ PVOID                           ApiSetMap;
/* 0x070 */ ULONG                           TlsExpansionCounter;
/* 0x074 */ UCHAR                           Padding2[4];
/* 0x078 */ PVOID                           TlsBitmap;
/* 0x080 */ ULONG                           TlsBitmapBits[2];
/* 0x088 */ PVOID                           ReadOnlySharedMemoryBase;
/* 0x090 */ PVOID                           HotpatchInformation;
/* 0x098 */ STATIC_SERVER_DATA_PTR          ReadOnlyStaticServerData;
/* 0x0a0 */ PUSHORT                         AnsiCodePageData;
/* 0x0a8 */ PUSHORT                         OemCodePageData;
/* 0x0b0 */ PVOID                           UnicodeCaseTableData;
/* 0x0b8 */ ULONG                           NumberOfProcessors;
/* 0x0bc */ ULONG                           NtGlobalFlag;
/* 0x0c0 */ LARGE_INTEGER                   CriticalSectionTimeout;
/* 0x0c8 */ ULONG_PTR                       HeapSegmentReserve;
/* 0x0d0 */ ULONG_PTR                       HeapSegmentCommit;
/* 0x0d8 */ ULONG_PTR                       HeapDeCommitTotalFreeThreshold;
/* 0x0e0 */ ULONG_PTR                       HeapDeCommitFreeBlockThreshold;
/* 0x0e8 */ ULONG                           NumberOfHeaps;
/* 0x0ec */ ULONG                           MaximumNumberOfHeaps;
/* 0x0f0 */ PVOID                           ProcessHeaps;
/* 0x0f8 */ PVOID                           GdiSharedHandleTable;
/* 0x100 */ PVOID                           ProcessStarterHelper;
/* 0x108 */ ULONG                           GdiDCAttributeList;
/* 0x10c */ UCHAR                           Padding3[4];
/* 0x110 */ PRTL_CRITICAL_SECTION           LoaderLock;
/* 0x118 */ ULONG                           OSMajorVersion;
/* 0x11c */ ULONG                           OSMinorVersion;
/* 0x120 */ USHORT                          OSBuildNumber;
/* 0x122 */ USHORT                          OSCSDVersion;
/* 0x124 */ ULONG                           OSPlatformId;
#else
/* 0x000 */ UCHAR                           InheritedAddressSpace;
/* 0x001 */ UCHAR                           ReadImageFileExecOptions;
/* 0x002 */ UCHAR                           BeingDebugged;
/* 0x003 */ struct
            {
                UCHAR                       ImageUsesLargePages             : 1;
                UCHAR                       IsProtectedProcess              : 1;
                UCHAR                       IsLegacyProcess                 : 1;
                UCHAR                       IsImageDynamicallyRelocated     : 1;
                UCHAR                       SkipPatchingUser32Forwarders    : 1;
                UCHAR                       SpareBits                       : 3;
            };
/* 0x004 */ PVOID                           Mutant;
/* 0x008 */ PVOID                           ImageBaseAddress;
/* 0x00c */ PPEB_LDR_DATA                   Ldr;
/* 0x010 */ PRTL_USER_PROCESS_PARAMETERS    ProcessParameters;
/* 0x014 */ PVOID                           SubSystemData;
/* 0x018 */ HANDLE                          ProcessHeap;
/* 0x01c */ PRTL_CRITICAL_SECTION           FastPebLock;
/* 0x020 */ PVOID                           AtlThunkSListPtr;
/* 0x024 */ PVOID                           IFEOKey;
/* 0x028 */ union
            {
                ULONG_PTR CrossProcessFlags;
                struct
                {
                    UCHAR ProcessInJob           : 1;
                    UCHAR ProcessInitializing    : 1;
                    UCHAR ProcessUsingVEH        : 1;
                    UCHAR ProcessUsingVCH        : 1;
                    UCHAR ProcessUsingFTH        : 1;
                };
            };

/* 0x02C */ union
            {
                PVOID                       KernelCallbackTable;
                PVOID                       UserSharedInfoPtr;
            };

/* 0x030 */ ULONG                           SystemReserved[1];
/* 0x034 */ ULONG                           AtlThunkSListPtr32;
/* 0x038 */ PVOID                           ApiSetMap;
/* 0x03c */ ULONG                           TlsExpansionCounter;
/* 0x040 */ PVOID                           TlsBitmap;
/* 0x044 */ ULONG                           TlsBitmapBits[2];
/* 0x04c */ PVOID                           ReadOnlySharedMemoryBase;
/* 0x050 */ PVOID                           HotpatchInformation;
/* 0x054 */ STATIC_SERVER_DATA_PTR          ReadOnlyStaticServerData;
/* 0x058 */ PUSHORT                         AnsiCodePageData;
/* 0x05c */ PUSHORT                         OemCodePageData;
/* 0x060 */ PVOID                           UnicodeCaseTableData;
/* 0x064 */ ULONG                           NumberOfProcessors;
/* 0x068 */ ULONG                           NtGlobalFlag;
/* 0x06C */ ULONG                           Dummy;
/* 0x070 */ LARGE_INTEGER                   CriticalSectionTimeout;
/* 0x078 */ ULONG                           HeapSegmentReserve;
/* 0x07c */ ULONG                           HeapSegmentCommit;
/* 0x080 */ ULONG                           HeapDeCommitTotalFreeThreshold;
/* 0x084 */ ULONG                           HeapDeCommitFreeBlockThreshold;
/* 0x088 */ ULONG                           NumberOfHeaps;
/* 0x08c */ ULONG                           MaximumNumberOfHeaps;
/* 0x090 */ PVOID                           ProcessHeaps;
/* 0x094 */ PVOID                           GdiSharedHandleTable;
/* 0x098 */ PVOID                           ProcessStarterHelper;
/* 0x09c */ ULONG                           GdiDCAttributeList;
/* 0x0a0 */ PRTL_CRITICAL_SECTION           LoaderLock;
/* 0x0a4 */ ULONG                           OSMajorVersion;
/* 0x0a8 */ ULONG                           OSMinorVersion;
/* 0x0ac */ USHORT                          OSBuildNumber;
/* 0x0ae */ USHORT                          OSCSDVersion;
/* 0x0b0 */ ULONG                           OSPlatformId;
#endif
} PEB_BASE, *PPEB_BASE;

typedef struct TEB_BASE
{
#if ML_AMD64
/* 0x000 */ NT_TIB      NtTib;
/* 0x038 */ PVOID       EnvironmentPointer;
/* 0x040 */ CLIENT_ID   ClientId;
/* 0x050 */ HANDLE      ActiveRpcHandle;
/* 0x058 */ PVOID       ThreadLocalStoragePointer;
/* 0x060 */ PPEB_BASE   ProcessEnvironmentBlock;
/* 0x068 */ ULONG       LastErrorValue;
/* 0x06C */ ULONG       CountOfOwnedCriticalSections;
/* 0x070 */ PVOID       CsrClientThread;
/* 0x078 */ PVOID       Win32ThreadInfo;
/* 0x080 */ ULONG       User32Reserved[26];
/* 0x0E8 */ ULONG       UserReserved[5];
/* 0x100 */ PVOID       WOW32Reserved;
/* 0x108 */ ULONG       CurrentLocale;
/* 0x10C */ ULONG       FpSoftwareStatusRegister;
#else
/* 0x000 */ NT_TIB      NtTib;
/* 0x01C */ PVOID       EnvironmentPointer;
/* 0x020 */ CLIENT_ID   ClientId;
/* 0x028 */ HANDLE      ActiveRpcHandle;
/* 0x02C */ PVOID       ThreadLocalStoragePointer;
/* 0x030 */ PPEB_BASE   ProcessEnvironmentBlock;
/* 0x034 */ ULONG       LastErrorValue;
/* 0x038 */ ULONG       CountOfOwnedCriticalSections;
/* 0x03C */ PVOID       CsrClientThread;
/* 0x040 */ PVOID       Win32ThreadInfo;
/* 0x044 */ ULONG       User32Reserved[26];
/* 0x0AC */ ULONG       UserReserved[5];
/* 0x0C0 */ PVOID       WOW32Reserved;
/* 0x0C4 */ ULONG       CurrentLocale;
/* 0x0C8 */ ULONG       FpSoftwareStatusRegister;
/* 0x0CC */ PVOID       SystemReserved1[54];
/* 0x1A4 */ LONG        ExceptionCode;
#endif

} TEB_BASE, *PTEB_BASE;

#if ML_AMD64
C_ASSERT(FIELD_OFFSET(TEB_BASE, ProcessEnvironmentBlock) == 0x060);
C_ASSERT(FIELD_OFFSET(PEB_BASE, ImageBaseAddress) == 0x010);
C_ASSERT(FIELD_OFFSET(PEB_BASE, Ldr) == 0x018);
C_ASSERT(FIELD_OFFSET(PEB_BASE, ProcessParameters) == 0x020);
C_ASSERT(FIELD_OFFSET(PEB_BASE, ProcessHeap) == 0x030);
C_ASSERT(FIELD_OFFSET(PEB_BASE, AnsiCodePageData) == 0x0a0);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OemCodePageData) == 0x0a8);
C_ASSERT(FIELD_OFFSET(PEB_BASE, UnicodeCaseTableData) == 0x0b0);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OSMajorVersion) == 0x118);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OSMinorVersion) == 0x11c);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OSBuildNumber) == 0x120);
#else
C_ASSERT(FIELD_OFFSET(TEB_BASE, ProcessEnvironmentBlock) == 0x030);
C_ASSERT(FIELD_OFFSET(PEB_BASE, ImageBaseAddress) == 0x008);
C_ASSERT(FIELD_OFFSET(PEB_BASE, Ldr) == 0x00c);
C_ASSERT(FIELD_OFFSET(PEB_BASE, ProcessParameters) == 0x010);
C_ASSERT(FIELD_OFFSET(PEB_BASE, ProcessHeap) == 0x018);
C_ASSERT(FIELD_OFFSET(PEB_BASE, AnsiCodePageData) == 0x058);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OemCodePageData) == 0x05c);
C_ASSERT(FIELD_OFFSET(PEB_BASE, UnicodeCaseTableData) == 0x060);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OSMajorVersion) == 0x0a4);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OSMinorVersion) == 0x0a8);
C_ASSERT(FIELD_OFFSET(PEB_BASE, OSBuildNumber) == 0x0ac);
#endif

/************************************************************************
  thread information
************************************************************************/

#ifndef _LDT_ENTRY_DEFINED
#define _LDT_ENTRY_DEFINED

typedef struct _LDT_ENTRY {
    WORD    LimitLow;
    WORD    BaseLow;
    union {
        struct {
            BYTE    BaseMid;
            BYTE    Flags1;     // Declare as bytes to avoid alignment
            BYTE    Flags2;     // Problems.
            BYTE    BaseHi;
        } Bytes;
        struct {
            DWORD   BaseMid : 8;
            DWORD   Type : 5;
            DWORD   Dpl : 2;
            DWORD   Pres : 1;
            DWORD   LimitHi : 4;
            DWORD   Sys : 1;
            DWORD   Reserved_0 : 1;
            DWORD   Default_Big : 1;
            DWORD   Granularity : 1;
            DWORD   BaseHi : 8;
        } Bits;
    } HighWord;
} LDT_ENTRY, *PLDT_ENTRY;

#endif

typedef struct _DESCRIPTOR_TABLE_ENTRY {
    ULONG Selector;
    LDT_ENTRY Descriptor;
} DESCRIPTOR_TABLE_ENTRY, *PDESCRIPTOR_TABLE_ENTRY;

typedef
VOID
(NTAPI
*PKNORMAL_ROUTINE)(
    IN PVOID NormalContext OPTIONAL,
    IN PVOID SystemArgument1 OPTIONAL,
    IN PVOID SystemArgument2 OPTIONAL
);



//
// Define process debug flags
//
#define PROCESS_DEBUG_INHERIT 0x00000001

typedef enum
{
    ProcessBasicInformation                     = 0,        // 0, q: PROCESS_BASIC_INFORMATION, PROCESS_EXTENDED_BASIC_INFORMATION
    ProcessQuotaLimits                          = 1,        // qs: QUOTA_LIMITS, QUOTA_LIMITS_EX
    ProcessIoCounters                           = 2,        // q: IO_COUNTERS
    ProcessVmCounters                           = 3,        // q: VM_COUNTERS, VM_COUNTERS_EX
    ProcessTimes                                = 4,        // q: KERNEL_USER_TIMES
    ProcessBasePriority                         = 5,        // s: KPRIORITY
    ProcessRaisePriority                        = 6,        // s: ULONG
    ProcessDebugPort                            = 7,        // q: HANDLE
    ProcessExceptionPort                        = 8,        // s: HANDLE
    ProcessAccessToken                          = 9,        // s: PROCESS_ACCESS_TOKEN
    ProcessLdtInformation                       = 10,
    ProcessLdtSize                              = 11,
    ProcessDefaultHardErrorMode                 = 12,       // qs: ULONG
    ProcessIoPortHandlers                       = 13,       // (kernel-mode only)
    ProcessPooledUsageAndLimits                 = 14,       // q: POOLED_USAGE_AND_LIMITS
    ProcessWorkingSetWatch                      = 15,       // q: PROCESS_WS_WATCH_INFORMATION[]; s: void
    ProcessUserModeIOPL                         = 16,
    ProcessEnableAlignmentFaultFixup            = 17,       // s: BOOLEAN
    ProcessPriorityClass                        = 18,       // qs: PROCESS_PRIORITY_CLASS
    ProcessWx86Information                      = 19,
    ProcessHandleCount                          = 20,       // q: ULONG, PROCESS_HANDLE_INFORMATION
    ProcessAffinityMask                         = 21,       // s: KAFFINITY
    ProcessPriorityBoost                        = 22,       // qs: ULONG
    ProcessDeviceMap                            = 23,       // qs: PROCESS_DEVICEMAP_INFORMATION, PROCESS_DEVICEMAP_INFORMATION_EX
    ProcessSessionInformation                   = 24,       // q: PROCESS_SESSION_INFORMATION
    ProcessForegroundInformation                = 25,       // s: PROCESS_FOREGROUND_BACKGROUND
    ProcessWow64Information                     = 26,       // q: ULONG_PTR
    ProcessImageFileName                        = 27,       // q: UNICODE_STRING
    ProcessLUIDDeviceMapsEnabled                = 28,       // q: ULONG
    ProcessBreakOnTermination                   = 29,       // qs: ULONG
    ProcessDebugObjectHandle                    = 30,       // q: HANDLE
    ProcessDebugFlags                           = 31,       // qs: ULONG
    ProcessHandleTracing                        = 32,       // q: PROCESS_HANDLE_TRACING_QUERY; s: size 0 disables, otherwise enables
    ProcessIoPriority                           = 33,       // qs: ULONG
    ProcessExecuteFlags                         = 34,       // qs: ULONG
    ProcessTlsInformation                       = 35,
    ProcessCookie                               = 36,       // q: ULONG
    ProcessImageInformation                     = 37,       // q: SECTION_IMAGE_INFORMATION
    ProcessCycleTime                            = 38,       // q: PROCESS_CYCLE_TIME_INFORMATION
    ProcessPagePriority                         = 39,       // q: ULONG
    ProcessInstrumentationCallback              = 40,
    ProcessThreadStackAllocation                = 41,       // s: PROCESS_STACK_ALLOCATION_INFORMATION, PROCESS_STACK_ALLOCATION_INFORMATION_EX
    ProcessWorkingSetWatchEx                    = 42,       // q: PROCESS_WS_WATCH_INFORMATION_EX[]
    ProcessImageFileNameWin32                   = 43,       // q: UNICODE_STRING
    ProcessImageFileMapping                     = 44,       // q: HANDLE (input)
    ProcessAffinityUpdateMode                   = 45,       // qs: PROCESS_AFFINITY_UPDATE_MODE
    ProcessMemoryAllocationMode                 = 46,       // qs: PROCESS_MEMORY_ALLOCATION_MODE
    ProcessGroupInformation                     = 47,       // q: USHORT[]
    ProcessTokenVirtualizationEnabled           = 48,       // s: ULONG
    ProcessConsoleHostProcess                   = 49,       // q: ULONG_PTR
    ProcessWindowInformation                    = 50,       // q: PROCESS_WINDOW_INFORMATION
    ProcessHandleInformation                    = 51,       // q: PROCESS_HANDLE_SNAPSHOT_INFORMATION // since WIN8
    ProcessMitigationPolicy                     = 52,       // s: PROCESS_MITIGATION_POLICY_INFORMATION
    ProcessDynamicFunctionTableInformation      = 53,
    ProcessHandleCheckingMode                   = 54,
    ProcessKeepAliveCount                       = 55,       // q: PROCESS_KEEPALIVE_COUNT_INFORMATION
    ProcessRevokeFileHandles                    = 56,       // s: PROCESS_REVOKE_FILE_HANDLES_INFORMATION
    ProcessWorkingSetControl                    = 57,

    MaxProcessInfoClass

} PROCESSINFOCLASS;

typedef struct _PROCESS_BASIC_INFORMATION
{
    NTSTATUS    ExitStatus;
    struct PEB* PebBaseAddress;
    ULONG_PTR   AffinityMask;
    LONG        BasePriority;
    ULONG_PTR   UniqueProcessId;
    ULONG_PTR   InheritedFromUniqueProcessId;

} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

typedef struct _PROCESS_SESSION_INFORMATION {
    ULONG SessionId;
} PROCESS_SESSION_INFORMATION, *PPROCESS_SESSION_INFORMATION;


typedef struct _PROCESS_ACCESS_TOKEN {

    //
    // Handle to Primary token to assign to the process.
    // TOKEN_ASSIGN_PRIMARY access to this token is needed.
    //

    HANDLE Token;

    //
    // Handle to the initial thread of the process.
    // A process's access token can only be changed if the process has
    // no threads or a single thread that has not yet begun execution.
    //
    // N.B. This field is unused.
    //

    HANDLE Thread;

} PROCESS_ACCESS_TOKEN, *PPROCESS_ACCESS_TOKEN;

typedef struct _PROCESS_DEVICEMAP_INFORMATION {
    union {
        struct {
            HANDLE DirectoryHandle;
        } Set;
        struct {
            ULONG DriveMap;
            UCHAR DriveType[ 32 ];
        } Query;
    } DUMMYUNIONNAME;
} PROCESS_DEVICEMAP_INFORMATION, *PPROCESS_DEVICEMAP_INFORMATION;

typedef struct _PROCESS_DEVICEMAP_INFORMATION_EX {
    union {
        struct {
            HANDLE DirectoryHandle;
        } Set;
        struct {
            ULONG DriveMap;
            UCHAR DriveType[ 32 ];
        } Query;
    } DUMMYUNIONNAME;
    ULONG Flags;    // specifies that the query type
} PROCESS_DEVICEMAP_INFORMATION_EX, *PPROCESS_DEVICEMAP_INFORMATION_EX;


typedef enum
{
    ThreadBasicInformation              = 0,        // q: THREAD_BASIC_INFORMATION
    ThreadTimes                         = 1,        // q: KERNEL_USER_TIMES
    ThreadPriority                      = 2,        // s: KPRIORITY
    ThreadBasePriority                  = 3,        // s: LONG
    ThreadAffinityMask                  = 4,        // s: KAFFINITY
    ThreadImpersonationToken            = 5,        // s: HANDLE
    ThreadDescriptorTableEntry          = 6,
    ThreadEnableAlignmentFaultFixup     = 7,        // s: BOOLEAN
    ThreadEventPair_Reusable            = 8,
    ThreadQuerySetWin32StartAddress     = 9,        // q: PVOID
    ThreadZeroTlsCell                   = 10,
    ThreadPerformanceCount              = 11,       // q: LARGE_INTEGER
    ThreadAmILastThread                 = 12,       // q: ULONG
    ThreadIdealProcessor                = 13,       // s: ULONG
    ThreadPriorityBoost                 = 14,       // qs: ULONG
    ThreadSetTlsArrayAddress            = 15,       // Obsolet
    ThreadIsIoPending                   = 16,       // q: ULONG
    ThreadHideFromDebugger              = 17,       // s: void
    ThreadBreakOnTermination            = 18,       // qs: ULONG
    ThreadSwitchLegacyState             = 19,
    ThreadIsTerminated                  = 20,       // q: ULONG
    ThreadLastSystemCall                = 21,       // q: THREAD_LAST_SYSCALL_INFORMATION
    ThreadIoPriority                    = 22,       // qs: ULONG
    ThreadCycleTime                     = 23,       // q: THREAD_CYCLE_TIME_INFORMATION
    ThreadPagePriority                  = 24,       // q: ULONG
    ThreadActualBasePriority            = 25,
    ThreadTebInformation                = 26,       // q: THREAD_TEB_INFORMATION (requires THREAD_GET_CONTEXT + THREAD_SET_CONTEXT)
    ThreadCSwitchMon                    = 27,       // Obsolete
    ThreadCSwitchPmu                    = 28,
    ThreadWow64Context                  = 29,       // q: WOW64_CONTEXT
    ThreadGroupInformation              = 30,       // q: GROUP_AFFINITY
    ThreadUmsInformation                = 31,
    ThreadCounterProfiling              = 32,
    ThreadIdealProcessorEx              = 33,       // q: PROCESSOR_NUMBER
    ThreadCpuAccountingInformation      = 34,       // since WIN8
    ThreadSwitchStackCheck              = 35,

    MaxThreadInfoClass

} THREADINFOCLASS;


#define LOW_PRIORITY            0              // Lowest thread priority Lepvel
#define LOW_REALTIME_PRIORITY   16    // Lowest realtime priority Lepvel
#define HIGH_PRIORITY           31            // Highest thread priority Lepvel
#define MAXIMUM_PRIORITY        32         // Number of thread priority Lepvels

typedef LONG KPRIORITY;

typedef struct _KERNEL_USER_TIMES {
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER ExitTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
} KERNEL_USER_TIMES, *PKERNEL_USER_TIMES;





/************************************************************************
  threads
************************************************************************/

typedef struct _THREAD_BASIC_INFORMATION
{
    NTSTATUS    ExitStatus;
    PVOID       TebBaseAddress;
    CLIENT_ID   ClientId;
    KAFFINITY   AffinityMask;
    KPRIORITY   Priority;
    KPRIORITY   BasePriority;

} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

typedef struct _THREAD_TEB_INFORMATION
{
    PVOID TebInformation; // buffer to place data in
    ULONG TebOffset; // offset in TEB to begin reading from
    ULONG BytesToRead; // number of bytes to read
} THREAD_TEB_INFORMATION, *PTHREAD_TEB_INFORMATION;


typedef struct _USER_STACK
{
    PVOID  FixedStackBase;
    PVOID  FixedStackLimit;
    PVOID  ExpandableStackBase;
    PVOID  ExpandableStackLimit;
    PVOID  ExpandableStackBottom;

} USER_STACK, *PUSER_STACK;

/************************************************************************
  Process api
************************************************************************/

/*
typedef struct _SECTION_IMAGE_INFORMATION
{
    PVOID   EntryPoint;
    ULONG   StackZeroBits;
    ULONG   StackReserved;
    ULONG   StackCommit;
    ULONG   ImageSubsystem;
    WORD    SubSystemVersionLow;
    WORD    SubSystemVersionHigh;
    ULONG   Unknown1;
    ULONG   ImageCharacteristics;
    ULONG   ImageMachineType;
    ULONG   Unknown2[3];
} SECTION_IMAGE_INFORMATION, *PSECTION_IMAGE_INFORMATION;
*/
typedef struct _RTL_USER_PROCESS_INFORMATION
{
    ULONG                       Size;
    HANDLE                      ProcessHandle;
    HANDLE                      ThreadHandle;
    CLIENT_ID                   ClientId;
    SECTION_IMAGE_INFORMATION   ImageInformation;
} RTL_USER_PROCESS_INFORMATION, *PRTL_USER_PROCESS_INFORMATION;


typedef struct _INITIAL_TEB
{
    struct
    {
        PVOID OldStackBase;
        PVOID OldStackLimit;
    } OldInitialTeb;

    PVOID StackBase;
    PVOID StackLimit;
    PVOID StackAllocationBase;

} INITIAL_TEB, *PINITIAL_TEB;

typedef struct _WOW64_PROCESS
{
    PVOID Wow64;
} WOW64_PROCESS, *PWOW64_PROCESS;


NATIVE_API
HANDLE
NTAPI
CsrGetProcessId(
    VOID
);


#define RTL_USER_PROC_PARAMS_NORMALIZED         0x00000001
#define RTL_USER_PROC_PROFILE_USER              0x00000002
#define RTL_USER_PROC_PROFILE_KERNEL            0x00000004
#define RTL_USER_PROC_PROFILE_SERVER            0x00000008
#define RTL_USER_PROC_RESERVE_1MB               0x00000020
#define RTL_USER_PROC_RESERVE_16MB              0x00000040
#define RTL_USER_PROC_CASE_SENSITIVE            0x00000080
#define RTL_USER_PROC_DISABLE_HEAP_DECOMMIT     0x00000100
#define RTL_USER_PROC_DLL_REDIRECTION_LOCAL     0x00001000
#define RTL_USER_PROC_APP_MANIFEST_PRESENT      0x00002000
#define RTL_USER_PROC_IMAGE_KEY_MISSING         0x00004000
#define RTL_USER_PROC_OPTIN_PROCESS             0x00020000

NATIVE_API
NTSTATUS
NTAPI
RtlCreateProcessParameters(
    OUT PRTL_USER_PROCESS_PARAMETERS*   ProcessParameters,
    IN  PUNICODE_STRING                 ImagePathName,
    IN  PUNICODE_STRING                 DllPath             OPTIONAL,
    IN  PUNICODE_STRING                 CurrentDirectory    OPTIONAL,
    IN  PUNICODE_STRING                 CommandLine         OPTIONAL,
    IN  PWCHAR                          Environment         OPTIONAL,
    IN  PUNICODE_STRING                 WindowTitle         OPTIONAL,
    IN  PUNICODE_STRING                 DesktopInfo         OPTIONAL,
    IN  PUNICODE_STRING                 ShellInfo           OPTIONAL,
    IN  PUNICODE_STRING                 RuntimeData         OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
RtlCreateProcessParametersEx(
    OUT PRTL_USER_PROCESS_PARAMETERS*   ProcessParameters,
    IN  PUNICODE_STRING                 ImagePathName,
    IN  PUNICODE_STRING                 DllPath          OPTIONAL,
    IN  PUNICODE_STRING                 CurrentDirectory OPTIONAL,
    IN  PUNICODE_STRING                 CommandLine      OPTIONAL,
    IN  PVOID                           Environment      OPTIONAL,
    IN  PUNICODE_STRING                 WindowTitle      OPTIONAL,
    IN  PUNICODE_STRING                 DesktopInfo      OPTIONAL,
    IN  PUNICODE_STRING                 ShellInfo        OPTIONAL,
    IN  PUNICODE_STRING                 RuntimeData      OPTIONAL,
    IN  ULONG                           Flags // pass RTL_USER_PROC_PARAMS_NORMALIZED to keep parameters normalized
);

NATIVE_API
NTSTATUS
NTAPI
RtlDestroyProcessParameters(
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters
);

NATIVE_API
PRTL_USER_PROCESS_PARAMETERS
NTAPI
RtlNormalizeProcessParams(
    IN OUT PRTL_USER_PROCESS_PARAMETERS ProcessParameters
);

NATIVE_API
PRTL_USER_PROCESS_PARAMETERS
NTAPI
RtlDeNormalizeProcessParams(
    IN OUT PRTL_USER_PROCESS_PARAMETERS ProcessParameters
);

NATIVE_API
NTSTATUS
NTAPI
RtlCreateUserProcess(
    IN      PUNICODE_STRING                 ImagePath,
    IN      ULONG                           ObjectAttributes,
    IN OUT  PRTL_USER_PROCESS_PARAMETERS    ProcessParameters,
    IN      PSECURITY_DESCRIPTOR            ProcessSecurityDescriptor OPTIONAL,
    IN      PSECURITY_DESCRIPTOR            ThreadSecurityDescriptor OPTIONAL,
    IN      HANDLE                          ParentProcess,
    IN      BOOLEAN                         InheritHandles,
    IN      HANDLE                          DebugPort OPTIONAL,
    IN      HANDLE                          ExceptionPort OPTIONAL,
    OUT     PRTL_USER_PROCESS_INFORMATION   ProcessInformation
);

NATIVE_API
NTSTATUS
NTAPI
RtlQueryEnvironmentVariable_U(
    IN PVOID                Environment OPTIONAL,
    IN PUNICODE_STRING      VariableName,
    OUT PUNICODE_STRING     VariableValue
);

NATIVE_API
NTSTATUS
NTAPI
NtQueryInformationProcess(
    HANDLE              ProcessHandle,
    PROCESSINFOCLASS    ProcessInformationClass,
    PVOID               ProcessInformation,
    ULONG               ProcessInformationLength,
    PULONG              ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
ZwQueryInformationProcess(
    HANDLE                      ProcessHandle,
    PROCESSINFOCLASS   ProcessInformationClass,
    PVOID                       ProcessInformation,
    ULONG                       ProcessInformationLength,
    PULONG                      ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
NtSetInformationProcess(
    IN HANDLE                       ProcessHandle,
    IN PROCESSINFOCLASS    ProcessInformationClass,
    IN PVOID                        ProcessInformation,
    IN ULONG                        ProcessInformationLength
);

NATIVE_API
NTSTATUS
NTAPI
ZwSetInformationProcess(
    IN HANDLE                       ProcessHandle,
    IN PROCESSINFOCLASS    ProcessInformationClass,
    IN PVOID                        ProcessInformation,
    IN ULONG                        ProcessInformationLength
);

NATIVE_API
NTSTATUS
NTAPI
NtCreateProcess(
    OUT PHANDLE             ProcessHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
    IN  HANDLE              ParentProcess,
    IN  BOOLEAN             InheritObjectTable,
    IN  HANDLE              SectionHandle OPTIONAL,
    IN  HANDLE              DebugPort OPTIONAL,
    IN  HANDLE              ExceptionPort OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtCreateProcessEx(
    OUT PHANDLE             ProcessHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
    IN  HANDLE              ParentProcess,
    IN  ULONG               Flags,
    IN  HANDLE              SectionHandle OPTIONAL,
    IN  HANDLE              DebugPort OPTIONAL,
    IN  HANDLE              ExceptionPort OPTIONAL,
    IN  ULONG               JobMemberLevel
);


typedef struct
{
    PHANDLE                         ProcessHandle;
    PHANDLE                         ThreadHandle;
    ACCESS_MASK                     ProcessDesiredAccess;
    ACCESS_MASK                     ThreadDesiredAccess;
    POBJECT_ATTRIBUTES              ProcessObjectAttributes;
    POBJECT_ATTRIBUTES              ThreadObjectAttributes;
    ULONG                           ProcessFlags;
    ULONG                           ThreadFlags;
    PRTL_USER_PROCESS_PARAMETERS    ProcessParameters;
    PPS_CREATE_INFO                 CreateInfo;
    PPS_ATTRIBUTE_LIST              AttributeList;
    NTSTATUS                        Status;

} CREATE_USER_PROCESS_PARAMETER, *PCREATE_USER_PROCESS_PARAMETER;


NATIVE_API
NTSTATUS
NTAPI
NtCreateUserProcess(
    OUT     PHANDLE                         ProcessHandle,
    OUT     PHANDLE                         ThreadHandle,
    IN      ACCESS_MASK                     ProcessDesiredAccess,
    IN      ACCESS_MASK                     ThreadDesiredAccess,
    IN      POBJECT_ATTRIBUTES              ProcessObjectAttributes OPTIONAL,
    IN      POBJECT_ATTRIBUTES              ThreadObjectAttributes OPTIONAL,
    IN      ULONG                           ProcessFlags,                   // PROCESS_CREATE_FLAGS_*
    IN      ULONG                           ThreadFlags,                    // THREAD_CREATE_FLAGS_*
    IN      PRTL_USER_PROCESS_PARAMETERS    ProcessParameters,
    IN OUT  PPS_CREATE_INFO                 CreateInfo,
    IN      PPS_ATTRIBUTE_LIST              AttributeList
);

NATIVE_API
NTSTATUS
NTAPI
NtOpenProcess(
    PHANDLE             ProcessHandle,
    ACCESS_MASK         DesiredAccess,
    POBJECT_ATTRIBUTES  ObjectAttributes,
    PCLIENT_ID          ClientId
);

NATIVE_API
NTSTATUS
NTAPI
ZwOpenProcess(
    PHANDLE             ProcessHandle,
    ACCESS_MASK         DesiredAccess,
    POBJECT_ATTRIBUTES  ObjectAttributes,
    PCLIENT_ID          ClientId
);

NATIVE_API
NTSTATUS
NTAPI
NtTerminateProcess(
    IN HANDLE ProcessHandle,
    IN NTSTATUS ExitStatus
);

NATIVE_API
NTSTATUS
NTAPI
ZwTerminateProcess(
    IN HANDLE ProcessHandle,
    IN NTSTATUS ExitStatus
);

NATIVE_API
NTSTATUS
NTAPI
NtSuspendProcess(
    HANDLE ProcessHandle
);

NATIVE_API
NTSTATUS
NTAPI
ZwSuspendProcess(
    HANDLE ProcessHandle
);

NATIVE_API
NTSTATUS
NTAPI
NtResumeProcess(
    HANDLE ProcessHandle
);

NATIVE_API
NTSTATUS
NTAPI
ZwResumeProcess(
    HANDLE ProcessHandle
);

NATIVE_API
NTSTATUS
NTAPI
NtGetNextProcess(
    IN  HANDLE      ProcessHandle,
    IN  ACCESS_MASK DesiredAccess,
    IN  ULONG       HandleAttributes,
    IN  ULONG       Flags,
    OUT PHANDLE     NewProcessHandle
);

NATIVE_API
NTSTATUS
NTAPI
ZwGetNextProcess(
    IN  HANDLE      ProcessHandle,
    IN  ACCESS_MASK DesiredAccess,
    IN  ULONG       HandleAttributes,
    IN  ULONG       Flags,
    OUT PHANDLE     NewProcessHandle
);

NATIVE_API
NTSTATUS
NTAPI
NtGetNextThread(
    IN  HANDLE      ProcessHandle,
    IN  HANDLE      ThreadHandle,
    IN  ACCESS_MASK DesiredAccess,
    IN  ULONG       HandleAttributes,
    IN  ULONG       Flags,
    OUT PHANDLE     NewThreadHandle
);

NATIVE_API
NTSTATUS
NTAPI
ZwGetNextThread(
    IN  HANDLE      ProcessHandle,
    IN  HANDLE      ThreadHandle,
    IN  ACCESS_MASK DesiredAccess,
    IN  ULONG       HandleAttributes,
    IN  ULONG       Flags,
    OUT PHANDLE     NewThreadHandle
);

/************************************************************************
  threads api
************************************************************************/

typedef struct
{
/* 0x000 */ ULONG   Flags;
/* 0x004 */ PCSTR   FrameName;

} TEB_ACTIVE_FRAME_CONTEXT, *PTEB_ACTIVE_FRAME_CONTEXT;

typedef struct TEB_ACTIVE_FRAME
{
    ULONG Flags;
    struct TEB_ACTIVE_FRAME* Previous;
    PTEB_ACTIVE_FRAME_CONTEXT Context;
} TEB_ACTIVE_FRAME, *PTEB_ACTIVE_FRAME;

NATIVE_API
VOID
NTAPI
RtlPushFrame(
    PTEB_ACTIVE_FRAME Frame
);

NATIVE_API
PTEB_ACTIVE_FRAME
NTAPI
RtlGetFrame(
    VOID
);

NATIVE_API
VOID
NTAPI
RtlPopFrame(
    PTEB_ACTIVE_FRAME Frame
);

NATIVE_API
NTSTATUS
NTAPI
NtGetContextThread(
    IN  HANDLE ThreadHandle,
    OUT PCONTEXT Context
);

NATIVE_API
NTSTATUS
NTAPI
NtSetContextThread(
    IN HANDLE ThreadHandle,
    IN PCONTEXT Context
);

NATIVE_API
NTSTATUS
NTAPI
ZwGetContextThread(
    IN  HANDLE ThreadHandle,
    OUT PCONTEXT Context
);

NATIVE_API
NTSTATUS
NTAPI
ZwSetContextThread(
    IN HANDLE ThreadHandle,
    IN PCONTEXT Context
);

NATIVE_API
VOID
NTAPI
RtlExitUserThread(
    ULONG ExitCode
);

NATIVE_API
NTSTATUS
NTAPI
NtYieldExecution(
    VOID
);

NATIVE_API
NTSTATUS
NTAPI
ZwYieldExecution(
    VOID
);

NATIVE_API
NTSTATUS
NTAPI
NtCreateThread(
    OUT PHANDLE             ThreadHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes,
    IN  HANDLE              ProcessHandle,
    OUT PCLIENT_ID          ClientId,
    IN  PCONTEXT            ThreadContext,
    IN  PUSER_STACK         UserStack,
    IN  BOOLEAN             CreateSuspended
);

NATIVE_API
NTSTATUS
NTAPI
ZwCreateThread(
    OUT PHANDLE             ThreadHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes,
    IN  HANDLE              ProcessHandle,
    OUT PCLIENT_ID          ClientId,
    IN  PCONTEXT            ThreadContext,
    IN  PINITIAL_TEB        InitialTeb,
    IN  BOOLEAN             CreateSuspended
);

NATIVE_API
NTSTATUS
NTAPI
NtCreateThreadEx(
    OUT PHANDLE             ThreadHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes    OPTIONAL,
    IN  HANDLE              ProcessHandle,
    IN  PVOID               StartRoutine,
    IN  PVOID               Argument            OPTIONAL,
    IN  ULONG               CreateFlags, // THREAD_CREATE_FLAGS_*
    IN  ULONG_PTR           ZeroBits            OPTIONAL,
    IN  ULONG_PTR           StackSize           OPTIONAL,
    IN  ULONG_PTR           MaximumStackSize    OPTIONAL,
    IN  PPS_ATTRIBUTE_LIST  AttributeList       OPTIONAL
);

NTSTATUS
NTAPI
ZwCreateThreadEx(
    OUT PHANDLE             ThreadHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
    IN  HANDLE              ProcessHandle,
    IN  PVOID               StartRoutine,
    IN  PVOID               Argument OPTIONAL,
    IN  ULONG               CreateFlags, // THREAD_CREATE_FLAGS_*
    IN  ULONG_PTR           ZeroBits OPTIONAL,
    IN  ULONG_PTR           StackSize OPTIONAL,
    IN  ULONG_PTR           MaximumStackSize OPTIONAL,
    IN  PPS_ATTRIBUTE_LIST  AttributeList OPTIONAL
);

typedef
NTSTATUS
(NTAPI
*PUSER_THREAD_START_ROUTINE)(
    IN PVOID ThreadParameter
);

NATIVE_API
NTSTATUS
NTAPI
RtlCreateUserThread(
    IN  HANDLE                      Process,
    IN  PSECURITY_DESCRIPTOR        ThreadSecurityDescriptor    OPTIONAL,
    IN  BOOLEAN                     CreateSuspended,
    IN  ULONG                       ZeroBits                    OPTIONAL,
    IN  ULONG_PTR                   MaximumStackSize            OPTIONAL,
    IN  ULONG_PTR                   CommittedStackSize          OPTIONAL,
    IN  PUSER_THREAD_START_ROUTINE  StartAddress,
    IN  PVOID                       Parameter                   OPTIONAL,
    OUT PHANDLE                     Thread                      OPTIONAL,
    OUT PCLIENT_ID                  ClientId                    OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtSuspendThread(
    IN  HANDLE ThreadHandle,
    OUT PULONG PreviousSuspendCount OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtResumeThread(
    IN  HANDLE ThreadHandle,
    OUT PULONG PreviousSuspendCount OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtAlertThread(
    IN HANDLE ThreadHandle
);

NATIVE_API
NTSTATUS
NTAPI
NtAlertResumeThread(
    IN  HANDLE  ThreadHandle,
    OUT PULONG  SuspendCount
);

NATIVE_API
NTSTATUS
NTAPI
NtOpenThread(
    PHANDLE             ThreadHandle,
    ACCESS_MASK         DesiredAccess,
    POBJECT_ATTRIBUTES  ObjectAttributes,
    PCLIENT_ID          ClientId
);

NATIVE_API
NTSTATUS
NTAPI
ZwOpenThread(
    PHANDLE             ThreadHandle,
    ACCESS_MASK         DesiredAccess,
    POBJECT_ATTRIBUTES  ObjectAttributes,
    PCLIENT_ID          ClientId
);

NATIVE_API
NTSTATUS
NTAPI
NtQueryInformationThread(
    HANDLE          ThreadHandle,
    THREADINFOCLASS ThreadInformationClass,
    PVOID           ThreadInformation,
    ULONG           ThreadInformationLength,
    PULONG          ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
ZwQueryInformationThread(
    HANDLE          ThreadHandle,
    THREADINFOCLASS ThreadInformationClass,
    PVOID           ThreadInformation,
    ULONG           ThreadInformationLength,
    PULONG          ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
NtSetInformationThread(
    HANDLE                      ThreadHandle,
    THREADINFOCLASS    ThreadInformationClass,
    PVOID                       ThreadInformation,
    ULONG                       ThreadInformationLength
);

NATIVE_API
NTSTATUS
NTAPI
ZwSetInformationThread(
    HANDLE                      ThreadHandle,
    THREADINFOCLASS    ThreadInformationClass,
    PVOID                       ThreadInformation,
    ULONG                       ThreadInformationLength
);

NATIVE_API
NTSTATUS
NTAPI
NtTerminateThread(
    IN HANDLE   ThreadHandle,
    IN ULONG    ExitCode
);

NATIVE_API
NTSTATUS
NTAPI
ZwTerminateThread(
    IN HANDLE   ThreadHandle,
    IN ULONG    ExitCode
);

NATIVE_API
NTSTATUS
NTAPI
NtContinue(
    IN PCONTEXT Context,
    IN BOOLEAN  TestAlert
);

NATIVE_API
NTSTATUS
NTAPI
ZwContinue(
    IN PCONTEXT Context,
    IN BOOLEAN  TestAlert
);

NATIVE_API
NTSTATUS
NTAPI
RtlRemoteCall(
    IN  HANDLE      Process,
    IN  HANDLE      Thread,
    IN  PVOID       CallSite,
    IN  ULONG       ArgumentCount,
    IN  PULONG_PTR  Arguments OPTIONAL,
    IN  BOOLEAN     PassContext,
    IN  BOOLEAN     AlreadySuspended
);

NATIVE_API
NTSTATUS
NTAPI
NtQueueApcThread(
    IN HANDLE           ThreadHandle,
    IN PKNORMAL_ROUTINE ApcRoutine,
    IN PVOID            ApcRoutineContext OPTIONAL,
    IN PVOID            Argument1 OPTIONAL,
    IN PVOID            Argument2 OPTIONAL
);

NATIVE_API
VOID
NTAPI
RtlCaptureContext(
    OUT PCONTEXT ContextRecord
);


#endif // _NTPROCESS_H_39560d0d_86be_4220_839a_0fcdb8e556cf_

/************************************************************************
  debugs
************************************************************************/

typedef enum
{
    SysDbgQueryModuleInformation        = 0,
    SysDbgQueryTraceInformation         = 1,
    SysDbgSetTracepoint                 = 2,
    SysDbgSetSpecialCall                = 3,
    SysDbgClearSpecialCalls             = 4,
    SysDbgQuerySpecialCalls             = 5,
    SysDbgBreakPoint                    = 6,
    SysDbgQueryVersion                  = 7,
    SysDbgReadVirtual                   = 8,
    SysDbgWriteVirtual                  = 9,
    SysDbgReadPhysical                  = 10,
    SysDbgWritePhysical                 = 11,
    SysDbgReadControlSpace              = 12,
    SysDbgWriteControlSpace             = 13,
    SysDbgReadIoSpace                   = 14,
    SysDbgWriteIoSpace                  = 15,
    SysDbgReadMsr                       = 16,
    SysDbgWriteMsr                      = 17,
    SysDbgReadBusData                   = 18,
    SysDbgWriteBusData                  = 19,
    SysDbgCheckLowMemory                = 20,
    SysDbgEnableKernelDebugger          = 21,
    SysDbgDisableKernelDebugger         = 22,
    SysDbgGetAutoKdEnable               = 23,
    SysDbgSetAutoKdEnable               = 24,
    SysDbgGetPrintBufferSize            = 25,
    SysDbgSetPrintBufferSize            = 26,
    SysDbgGetKdUmExceptionEnable        = 27,
    SysDbgSetKdUmExceptionEnable        = 28,
    SysDbgGetTriageDump                 = 29,
    SysDbgGetKdBlockEnable              = 30,
    SysDbgSetKdBlockEnable              = 31,
    SysDbgRegisterForUmBreakInfo        = 32,
    SysDbgGetUmBreakPid                 = 33,
    SysDbgClearUmBreakPid               = 34,
    SysDbgGetUmAttachPid                = 35,
    SysDbgClearUmAttachPid              = 36,

} SYSDBG_COMMAND;



/************************************************************************
  system information
************************************************************************/

typedef enum
{
    SystemBasicInformation                          = 0,
    SystemProcessorInformation                      = 1,
    SystemPerformanceInformation                    = 2,
    SystemTimeOfDayInformation                      = 3,
    SystemPathInformation                           = 4,
    SystemProcessInformation                        = 5,
    SystemCallCountInformation                      = 6,
    SystemDeviceInformation                         = 7,
    SystemProcessorPerformanceInformation           = 8,
    SystemFlagsInformation                          = 9,
    SystemCallTimeInformation                       = 10,
    SystemModuleInformation                         = 11,
    SystemLocksInformation                          = 12,
    SystemStackTraceInformation                     = 13,
    SystemPagedPoolInformation                      = 14,
    SystemNonPagedPoolInformation                   = 15,
    SystemHandleInformation                         = 16,
    SystemObjectInformation                         = 17,
    SystemPageFileInformation                       = 18,
    SystemVdmInstemulInformation                    = 19,
    SystemVdmBopInformation                         = 20,
    SystemFileCacheInformation                      = 21,
    SystemPoolTagInformation                        = 22,
    SystemInterruptInformation                      = 23,
    SystemDpcBehaviorInformation                    = 24,
    SystemFullMemoryInformation                     = 25,
    SystemLoadGdiDriverInformation                  = 26,
    SystemUnloadGdiDriverInformation                = 27,
    SystemTimeAdjustmentInformation                 = 28,
    SystemSummaryMemoryInformation                  = 29,
    SystemMirrorMemoryInformation                   = 30,
    SystemPerformanceTraceInformation               = 31,
    SystemObsolete0                                 = 32,
    SystemExceptionInformation                      = 33,
    SystemCrashDumpStateInformation                 = 34,
    SystemKernelDebuggerInformation                 = 35,
    SystemContextSwitchInformation                  = 36,
    SystemRegistryQuotaInformation                  = 37,
    SystemExtendServiceTableInformation             = 38,
    SystemPrioritySeperation                        = 39,
    SystemVerifierAddDriverInformation              = 40,
    SystemVerifierRemoveDriverInformation           = 41,
    SystemProcessorIdleInformation                  = 42,
    SystemLegacyDriverInformation                   = 43,
    SystemCurrentTimeZoneInformation                = 44,
    SystemLookasideInformation                      = 45,
    SystemTimeSlipNotification                      = 46,
    SystemSessionCreate                             = 47,
    SystemSessionDetach                             = 48,
    SystemSessionInformation                        = 49,
    SystemRangeStartInformation                     = 50,
    SystemVerifierInformation                       = 51,
    SystemVerifierThunkExtend                       = 52,
    SystemSessionProcessInformation                 = 53,
    SystemLoadGdiDriverInSystemSpace                = 54,
    SystemNumaProcessorMap                          = 55,
    SystemPrefetcherInformation                     = 56,
    SystemExtendedProcessInformation                = 57,
    SystemRecommendedSharedDataAlignment            = 58,
    SystemComPlusPackage                            = 59,
    SystemNumaAvailableMemory                       = 60,
    SystemProcessorPowerInformation                 = 61,
    SystemEmulationBasicInformation                 = 62,
    SystemEmulationProcessorInformation             = 63,
    SystemExtendedHandleInformation                 = 64,
    SystemLostDelayedWriteInformation               = 65,
    SystemBigPoolInformation                        = 66,
    SystemSessionPoolTagInformation                 = 67,
    SystemSessionMappedViewInformation              = 68,
    SystemHotpatchInformation                       = 69,
    SystemObjectSecurityMode                        = 70,
    SystemWatchdogTimerHandler                      = 71,
    SystemWatchdogTimerInformation                  = 72,
    SystemLogicalProcessorInformation               = 73,
    SystemWow64SharedInformationObsolete            = 74,
    SystemRegisterFirmwareTableInformationHandler   = 75,
    SystemFirmwareTableInformation                  = 76,
    SystemModuleInformationEx                       = 77,
    SystemVerifierTriageInformation                 = 78,
    SystemSuperfetchInformation                     = 79,
    SystemMemoryListInformation                     = 80,
    SystemFileCacheInformationEx                    = 81,
    SystemThreadPriorityClientIdInformation         = 82,
    SystemProcessorIdleCycleTimeInformation         = 83,
    SystemVerifierCancellationInformation           = 84,
    SystemProcessorPowerInformationEx               = 85,
    SystemRefTraceInformation                       = 86,
    SystemSpecialPoolInformation                    = 87,
    SystemProcessIdInformation                      = 88,
    SystemErrorPortInformation                      = 89,
    SystemBootEnvironmentInformation                = 90,
    SystemHypervisorInformation                     = 91,
    SystemVerifierInformationEx                     = 92,
    SystemTimeZoneInformation                       = 93,
    SystemImageFileExecutionOptionsInformation      = 94,
    SystemCoverageInformation                       = 95,
    SystemPrefetchPatchInformation                  = 96,
    SystemVerifierFaultsInformation                 = 97,
    SystemSystemPartitionInformation                = 98,
    SystemSystemDiskInformation                     = 99,
    SystemProcessorPerformanceDistribution          = 100,
    SystemNumaProximityNodeInformation              = 101,
    SystemDynamicTimeZoneInformation                = 102,
    SystemCodeIntegrityInformation                  = 103,
    SystemProcessorMicrocodeUpdateInformation       = 104,
    SystemProcessorBrandString                      = 105,
    SystemVirtualAddressInformation                 = 106,
    SystemLogicalProcessorAndGroupInformation       = 107,
    SystemProcessorCycleTimeInformation             = 108,
    SystemStoreInformation                          = 109,
    SystemRegistryAppendString                      = 110,
    SystemAitSamplingValue                          = 111,
    SystemVhdBootInformation                        = 112,
    SystemCpuQuotaInformation                       = 113,
    SystemSpare0                                    = 114,
    SystemSpare1                                    = 115,
    SystemLowPriorityIoInformation                  = 116,
    SystemTpmBootEntropyInformation                 = 117,
    SystemVerifierCountersInformation               = 118,
    SystemPagedPoolInformationEx                    = 119,
    SystemSystemPtesInformationEx                   = 120,
    SystemNodeDistanceInformation                   = 121,
    SystemAcpiAuditInformation                      = 122,
    SystemBasicPerformanceInformation               = 123,
    SystemQueryPerformanceCounterInformation        = 124,
    MaxSystemInfoClass                              = 125,

} SYSTEM_INFORMATION_CLASS;

typedef struct _SYSTEM_BASIC_INFORMATION
{
/* 0x000 */ ULONG       Reserved;
/* 0x004 */ ULONG       TimerResolution;
/* 0x008 */ ULONG       PageSize;
/* 0x00C */ ULONG       NumberOfPhysicalPages;
/* 0x010 */ ULONG       LowestPhysicalPageNumber;
/* 0x014 */ ULONG       HighestPhysicalPageNumber;
/* 0x018 */ ULONG       AllocationGranularity;
/* 0x01C */ ULONG_PTR   MinimumUserModeAddress;
/* 0x020 */ ULONG_PTR   MaximumUserModeAddress;
/* 0x024 */ ULONG_PTR   ActiveProcessorsAffinityMask;
/* 0x028 */ CHAR        NumberOfProcessors;

} SYSTEM_BASIC_INFORMATION, *PSYSTEM_BASIC_INFORMATION;


//
// Thread scheduling states.
//

typedef enum _THREAD_STATE {
    Initialized,
    Ready,
    Running,
    Standby,
    Terminated,
    Waiting,
    Transition,
    DeferredReady,
    GateWait
} KTHREAD_STATE;

typedef struct _SYSTEM_THREAD_INFORMATION
{
/* 0x000 */ LARGE_INTEGER   KernelTime;
/* 0x008 */ LARGE_INTEGER   UserTime;
/* 0x010 */ LARGE_INTEGER   CreateTime;
/* 0x018 */ ULONG           WaitTime;
/* 0x01c */ PVOID           StartAddress;
/* 0x020 */ CLIENT_ID       ClientId;
/* 0x028 */ LONG            Priority;
/* 0x02c */ LONG            BasePriority;
/* 0x030 */ ULONG           ContextSwitches;
/* 0x034 */ ULONG           ThreadState;
/* 0x038 */ ULONG           WaitReason;
/* 0x03C */ ULONG           PadFor16Byte;

} SYSTEM_THREAD_INFORMATION, *PSYSTEM_THREAD_INFORMATION;

typedef struct _SYSTEM_PROCESS_INFORMATION
{
/* 0x000 */ ULONG                       NextEntryOffset;
/* 0x004 */ ULONG                       NumberOfThreads;
/* 0x008 */ LARGE_INTEGER               WorkingSetPrivateSize;
/* 0x010 */ ULONG                       HardFaultCount;
/* 0x014 */ ULONG                       NumberOfThreadsHighWatermark;
/* 0x018 */ ULONG64                     CycleTime;
/* 0x020 */ LARGE_INTEGER               CreateTime;
/* 0x028 */ LARGE_INTEGER               UserTime;
/* 0x030 */ LARGE_INTEGER               KernelTime;
/* 0x038 */ UNICODE_STRING              ImageName;
/* 0x040 */ LONG                        BasePriority;
/* 0x044 */ ULONG_PTR                   UniqueProcessId;
/* 0x048 */ ULONG_PTR                   InheritedFromUniqueProcessId;
/* 0x04c */ ULONG                       HandleCount;
/* 0x050 */ ULONG                       SessionId;
/* 0x054 */ ULONG_PTR                   UniqueProcessKey;
/* 0x058 */ ULONG_PTR                   PeakVirtualSize;
/* 0x05c */ ULONG_PTR                   VirtualSize;
/* 0x060 */ ULONG                       PageFaultCount;
/* 0x064 */ ULONG_PTR                   PeakWorkingSetSize;
/* 0x068 */ ULONG_PTR                   WorkingSetSize;
/* 0x06c */ ULONG_PTR                   QuotaPeakPagedPoolUsage;
/* 0x070 */ ULONG_PTR                   QuotaPagedPoolUsage;
/* 0x074 */ ULONG_PTR                   QuotaPeakNonPagedPoolUsage;
/* 0x078 */ ULONG_PTR                   QuotaNonPagedPoolUsage;
/* 0x07c */ ULONG_PTR                   PagefileUsage;
/* 0x080 */ ULONG_PTR                   PeakPagefileUsage;
/* 0x084 */ ULONG_PTR                   PrivatePageCount;
/* 0x088 */ LARGE_INTEGER               ReadOperationCount;
/* 0x090 */ LARGE_INTEGER               WriteOperationCount;
/* 0x098 */ LARGE_INTEGER               OtherOperationCount;
/* 0x0a0 */ LARGE_INTEGER               ReadTransferCount;
/* 0x0a8 */ LARGE_INTEGER               WriteTransferCount;
/* 0x0b0 */ LARGE_INTEGER               OtherTransferCount;
/* 0x0b8 */ SYSTEM_THREAD_INFORMATION   Threads[1];

} SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;

typedef struct
{
/* 0x000 */ LARGE_INTEGER  IdleProcessTime;
/* 0x008 */ LARGE_INTEGER  IoReadTransferCount;
/* 0x010 */ LARGE_INTEGER  IoWriteTransferCount;
/* 0x018 */ LARGE_INTEGER  IoOtherTransferCount;
/* 0x020 */ ULONG          IoReadOperationCount;
/* 0x024 */ ULONG          IoWriteOperationCount;
/* 0x028 */ ULONG          IoOtherOperationCount;
/* 0x02c */ ULONG          AvailablePages;
/* 0x030 */ ULONG          CommittedPages;
/* 0x034 */ ULONG          CommitLimit;
/* 0x038 */ ULONG          PeakCommitment;
/* 0x03c */ ULONG          PageFaultCount;
/* 0x040 */ ULONG          CopyOnWriteCount;
/* 0x044 */ ULONG          TransitionCount;
/* 0x048 */ ULONG          CacheTransitionCount;
/* 0x04c */ ULONG          DemandZeroCount;
/* 0x050 */ ULONG          PageReadCount;
/* 0x054 */ ULONG          PageReadIoCount;
/* 0x058 */ ULONG          CacheReadCount;
/* 0x05c */ ULONG          CacheIoCount;
/* 0x060 */ ULONG          DirtyPagesWriteCount;
/* 0x064 */ ULONG          DirtyWriteIoCount;
/* 0x068 */ ULONG          MappedPagesWriteCount;
/* 0x06c */ ULONG          MappedWriteIoCount;
/* 0x070 */ ULONG          PagedPoolPages;
/* 0x074 */ ULONG          NonPagedPoolPages;
/* 0x078 */ ULONG          PagedPoolAllocs;
/* 0x07c */ ULONG          PagedPoolFrees;
/* 0x080 */ ULONG          NonPagedPoolAllocs;
/* 0x084 */ ULONG          NonPagedPoolFrees;
/* 0x088 */ ULONG          FreeSystemPtes;
/* 0x08c */ ULONG          ResidentSystemCodePage;
/* 0x090 */ ULONG          TotalSystemDriverPages;
/* 0x094 */ ULONG          TotalSystemCodePages;
/* 0x098 */ ULONG          NonPagedPoolLookasideHits;
/* 0x09c */ ULONG          PagedPoolLookasideHits;
/* 0x0a0 */ ULONG          AvailablePagedPoolPages;
/* 0x0a4 */ ULONG          ResidentSystemCachePage;
/* 0x0a8 */ ULONG          ResidentPagedPoolPage;
/* 0x0ac */ ULONG          ResidentSystemDriverPage;
/* 0x0b0 */ ULONG          CcFastReadNoWait;
/* 0x0b4 */ ULONG          CcFastReadWait;
/* 0x0b8 */ ULONG          CcFastReadResourceMiss;
/* 0x0bc */ ULONG          CcFastReadNotPossible;
/* 0x0c0 */ ULONG          CcFastMdlReadNoWait;
/* 0x0c4 */ ULONG          CcFastMdlReadWait;
/* 0x0c8 */ ULONG          CcFastMdlReadResourceMiss;
/* 0x0cc */ ULONG          CcFastMdlReadNotPossible;
/* 0x0d0 */ ULONG          CcMapDataNoWait;
/* 0x0d4 */ ULONG          CcMapDataWait;
/* 0x0d8 */ ULONG          CcMapDataNoWaitMiss;
/* 0x0dc */ ULONG          CcMapDataWaitMiss;
/* 0x0e0 */ ULONG          CcPinMappedDataCount;
/* 0x0e4 */ ULONG          CcPinReadNoWait;
/* 0x0e8 */ ULONG          CcPinReadWait;
/* 0x0ec */ ULONG          CcPinReadNoWaitMiss;
/* 0x0f0 */ ULONG          CcPinReadWaitMiss;
/* 0x0f4 */ ULONG          CcCopyReadNoWait;
/* 0x0f8 */ ULONG          CcCopyReadWait;
/* 0x0fc */ ULONG          CcCopyReadNoWaitMiss;
/* 0x100 */ ULONG          CcCopyReadWaitMiss;
/* 0x104 */ ULONG          CcMdlReadNoWait;
/* 0x108 */ ULONG          CcMdlReadWait;
/* 0x10c */ ULONG          CcMdlReadNoWaitMiss;
/* 0x110 */ ULONG          CcMdlReadWaitMiss;
/* 0x114 */ ULONG          CcReadAheadIos;
/* 0x118 */ ULONG          CcLazyWriteIos;
/* 0x11c */ ULONG          CcLazyWritePages;
/* 0x120 */ ULONG          CcDataFlushes;
/* 0x124 */ ULONG          CcDataPages;
/* 0x128 */ ULONG          ContextSwitches;
/* 0x12c */ ULONG          FirstLevelTbFills;
/* 0x130 */ ULONG          SecondLevelTbFills;
/* 0x134 */ ULONG          SystemCalls;
/* 0x138 */ ULONG64        CcTotalDirtyPages;
/* 0x140 */ ULONG64        CcDirtyPageThreshold;

} SYSTEM_PERFORMANCE_INFORMATION;

typedef struct
{
/* 0x000 */ LARGE_INTEGER   BootTime;
/* 0x008 */ LARGE_INTEGER   CurrentTime;
/* 0x010 */ LARGE_INTEGER   TimeZoneBias;
/* 0x018 */ ULONG           TimeZoneId;
/* 0x01c */ ULONG           Reserved;
/* 0x020 */ ULONG64         BootTimeBias;
/* 0x028 */ ULONG64         SleepTimeBias;

} SYSTEM_TIMEOFDAY_INFORMATION;

typedef struct
{
/* 0x000 */ USHORT  ProcessorArchitecture;
/* 0x002 */ USHORT  ProcessorLevel;
/* 0x004 */ USHORT  ProcessorRevision;
/* 0x006 */ USHORT  Reserved;
/* 0x008 */ ULONG   ProcessorFeatureBits;

} SYSTEM_PROCESSOR_INFORMATION;

typedef struct
{
/* 0x000 */ LARGE_INTEGER   IdleTime;
/* 0x008 */ LARGE_INTEGER   KernelTime;
/* 0x010 */ LARGE_INTEGER   UserTime;
/* 0x018 */ LARGE_INTEGER   DpcTime;
/* 0x020 */ LARGE_INTEGER   InterruptTime;
/* 0x028 */ ULONG           InterruptCount;

} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

typedef struct
{
/* 0x000 */ ULONG Length;
/* 0x004 */ ULONG NumberOfTables;
//          ULONG TableLimit[1]

} SYSTEM_CALL_COUNT_INFORMATION;
/*
typedef struct
{
    PVOID   Reserved1;
    PVOID   Reserved2;
    PVOID   ImageBaseAddress;
    ULONG   ImageSize;
    ULONG   Flags;
    USHORT  Id;
    USHORT  Rank;
    USHORT  w018;
    USHORT  NameOffset;
    CHAR    Name[MAXIMUM_FILENAME_LENGTH];

} SYSTEM_MODULE, *PSYSTEM_MODULE;

typedef struct
{
    USHORT  NextEntryOffset;
    USHORT  Dummy;
    ULONG   Reserved1;
    ULONG   Reserved2;
    PVOID   ImageBaseAddress;
    ULONG   ImageSize;
    ULONG   Flags;
    WORD    Id;
    WORD    Rank;
    WORD    w018;
    WORD    NameOffset;
    CHAR    Name[MAXIMUM_FILENAME_LENGTH];
    ULONG   Unknown;
    ULONG   Reserved3;
    ULONG   Reserved4;

} SYSTEM_MODULE_INFORMATION_EX, *PSYSTEM_MODULE_INFORMATION_EX;

typedef struct
{
    ULONG           ModulesCount;
    SYSTEM_MODULE   Modules[1];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;
*/
typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO
{
    USHORT  UniqueProcessId;
    USHORT  CreatorBackTraceIndex;
    UCHAR   ObjectTypeIndex;
    UCHAR   HandleAttributes;
    USHORT  HandleValue;
    PVOID   Object;
    ULONG   GrantedAccess;

} SYSTEM_HANDLE_TABLE_ENTRY_INFO, *PSYSTEM_HANDLE_TABLE_ENTRY_INFO;

typedef struct _SYSTEM_HANDLE_INFORMATION
{
    ULONG NumberOfHandles;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles[1];

} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX
{
    PVOID       Object;
    ULONG_PTR   UniqueProcessId;
    HANDLE      HandleValue;
    ULONG       GrantedAccess;
    USHORT      CreatorBackTraceIndex;
    USHORT      ObjectTypeIndex;
    ULONG       HandleAttributes;
    ULONG       Reserved;

} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX, *PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

typedef struct _SYSTEM_HANDLE_INFORMATION_EX
{
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];

} SYSTEM_HANDLE_INFORMATION_EX, *PSYSTEM_HANDLE_INFORMATION_EX;

typedef struct
{
/* 0x000 */ ULONG CurrentSize;
/* 0x004 */ ULONG PeakSize;
/* 0x008 */ ULONG PageFaultCount;
/* 0x00c */ ULONG MinimumWorkingSet;
/* 0x010 */ ULONG MaximumWorkingSet;
/* 0x014 */ ULONG CurrentSizeIncludingTransitionInPages;
/* 0x018 */ ULONG PeakSizeIncludingTransitionInPages;
/* 0x01c */ ULONG TransitionRePurposeCount;
/* 0x020 */ ULONG Flags;

} SYSTEM_FILECACHE_INFORMATION;

typedef struct
{
    BOOLEAN DebuggerEnabled;
    BOOLEAN DebuggerNotPresent;

} SYSTEM_KERNEL_DEBUGGER_INFORMATION, *PSYSTEM_KERNEL_DEBUGGER_INFORMATION;

NATIVE_API
NTSTATUS
NTAPI
NtQueryLicenseValue(
    IN  PUNICODE_STRING Name,
    IN  PULONG          Type OPTIONAL,
    OUT PVOID           Buffer,
    IN  ULONG           Length,
    OUT PULONG          DataLength
);

NATIVE_API
BOOL
NTAPI
RtlQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceCount
);

NATIVE_API
NTSTATUS
NTAPI
NtQueryPerformanceCounter(
    IN PLARGE_INTEGER   Counter,
    IN PLARGE_INTEGER   Frequency
);

NATIVE_API
NTSTATUS
NTAPI
NtQuerySystemInformation(
    IN  SYSTEM_INFORMATION_CLASS    SystemInformationClass,
    OUT PVOID                       SystemInformation,
    IN  ULONG_PTR                   SystemInformationLength,
    OUT PULONG                      ReturnLength OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
ZwQuerySystemInformation(
    IN  SYSTEM_INFORMATION_CLASS    SystemInformationClass,
    OUT PVOID                       SystemInformation,
    IN  ULONG_PTR                   SystemInformationLength,
    OUT PULONG                      ReturnLength OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtAddAtom(
    PCWSTR  AtomString,
    ULONG   BytesInString,
    ATOM   *Atom
);

NATIVE_API
NTSTATUS
NTAPI
ZwAddAtom(
    PCWSTR  AtomString,
    ULONG   BytesInString,
    ATOM   *Atom
);

NATIVE_API
NTSTATUS
NTAPI
NtDeleteAtom(
    ATOM Atom
);

NATIVE_API
NTSTATUS
NTAPI
ZwDeleteAtom(
    ATOM Atom
);

NATIVE_API
NTSTATUS
NTAPI
NtSystemDebugControl(
    IN  SYSDBG_COMMAND  Command,
    IN  PVOID           InputBuffer,
    IN  ULONG_PTR       InputBufferLength,
    OUT PVOID           OutputBuffer,
    IN  ULONG_PTR       OutputBufferLength,
    OUT PULONG          ReturnLength
);


NATIVE_API
VOID
NTAPI
LdrInitializeThunk(
    PCONTEXT    ThreadContext,
    PVOID       NtdllBase
);

NATIVE_API
VOID
FASTCALL
KiFastSystemCall(
    VOID
);

NATIVE_API
VOID
NTAPI
KiUserCallbackDispatcher(
    ULONG_PTR,
    ULONG_PTR   apfnDispatchIndex,
    PVOID       Parameter
);

NATIVE_API
ULONG64
NTAPI
NtGetTickCount(
    VOID
);

NATIVE_API
ULONG
NTAPI
RtlRandom(
    IN OUT PULONG Seed
);

NATIVE_API
ULONG
NTAPI
RtlRandomEx(
    IN OUT PULONG Random
);

NATIVE_API
NTSTATUS
NTAPI
RtlExpandEnvironmentStrings_U(
    IN  PVOID           Environment OPTIONAL,
    IN  PUNICODE_STRING SourceString,
    OUT PUNICODE_STRING DestinationString,
    OUT PULONG          DestinationBufferLength OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
RtlCreateSecurityDescriptor(
    IN OUT  PSECURITY_DESCRIPTOR    SecurityDescriptor,
    IN      ULONG                   Revision
);

NATIVE_API
NTSTATUS
NTAPI
RtlCreateAcl(
    IN PACL     Acl,
    IN ULONG    AclLength,
    IN ULONG    AclRevision
);

NATIVE_API
NTSTATUS
NTAPI
RtlAddAccessDeniedAce(
    IN OUT  PACL            Acl,
    IN      ULONG           AceRevision,
    IN      ACCESS_MASK     AccessMask,
    IN      PSID            Sid
);

NATIVE_API
NTSTATUS
NTAPI
RtlAddAccessAllowedAce(
    IN OUT  PACL            Acl,
    IN      ULONG           AceRevision,
    IN      ACCESS_MASK     AccessMask,
    IN      PSID            Sid
);

NATIVE_API
NTSTATUS
NTAPI
RtlSetOwnerSecurityDescriptor(
    IN PSECURITY_DESCRIPTOR SecurityDescriptor,
    IN PSID                 Owner OPTIONAL,
    IN BOOLEAN              OwnerDefaulted OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
RtlSetGroupSecurityDescriptor(
    IN PSECURITY_DESCRIPTOR SecurityDescriptor,
    IN PSID                 Group OPTIONAL,
    IN BOOLEAN              GroupDefaulted OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
RtlSetDaclSecurityDescriptor(
    IN OUT  PSECURITY_DESCRIPTOR    SecurityDescriptor,
    IN      BOOLEAN                 DaclPresent,
    IN      PACL                    Dacl OPTIONAL,
    IN      BOOLEAN                 DaclDefaulted OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
RtlFormatMessage(
    LPWSTR  Message,              // [In] Message to format
    UCHAR   MaxWidth,             // [In] Maximum width in characters of each output line
    BOOLEAN IgnoreInserts,        // [In] Whether to copy the message without processing inserts
    BOOLEAN Ansi,                 // [In] Whether Arguments may have ANSI strings
    BOOLEAN ArgumentIsArray,      // [In] Whether Arguments is actually an array rather than a va_list *
    va_list *Arguments,
    LPWSTR  Buffer,               // [Out] Buffer to store processed message in
    ULONG   BufferSize            // [In] Size of Buffer (in bytes?)
);

NATIVE_API
BOOLEAN
NTAPI
RtlDosPathNameToNtPathName_U(
    IN  PCWSTR                  DosName,
    OUT PUNICODE_STRING         NtName,
    OUT PCWSTR*                 DosFilePath OPTIONAL,
    OUT PRTL_RELATIVE_NAME_U    FileName OPTIONAL
);

NATIVE_API
VOID
NTAPI
RtlReleaseRelativeName(
    IN OUT PRTL_RELATIVE_NAME_U RelativeName
);

NATIVE_API
NTSTATUS
NTAPI
RtlNtPathNameToDosPathName(
    IN      ULONG                       Flags,
    IN OUT  PRTL_UNICODE_STRING_BUFFER  Path,
    OUT     PULONG                      Disposition OPTIONAL,
    OUT     PWSTR*                      FilePart OPTIONAL
);

NATIVE_API
ULONG
NTAPI
RtlGetFullPathName_U(
    LPCWSTR FileName,
    ULONG   BufferSizeInBytes,
    LPWSTR  Buffer,
    LPWSTR *FilePart
);

NATIVE_API
BOOLEAN
NTAPI
RtlDosPathNameToRelativeNtPathName_U(
    IN  PWSTR                   DosFileName,
    OUT PUNICODE_STRING         NtFileName,
    OUT PWSTR*                  FilePart OPTIONAL,
    OUT PRTL_RELATIVE_NAME_U    RelativeName OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
RtlGetVersion(
    IN OUT PRTL_OSVERSIONINFOW lpVersionInformation
);

NATIVE_API
ULONG
NTAPI
RtlNtStatusToDosError(
    NTSTATUS Status
);

NATIVE_API
NTSTATUS
NTAPI
RtlGetLastWin32Error(
    VOID
);

NATIVE_API
VOID
NTAPI
RtlSetLastWin32Error(
    IN LONG ErrorCode
);

NATIVE_API
VOID
NTAPI
RtlSetLastWin32ErrorAndNtStatusFromNtStatus(
    IN NTSTATUS NtStatus
);

NATIVE_API
NTSTATUS
NTAPI
RtlFindMessage(
    PVOID                    DllHandle,
    ULONG                    MessageTableId,
    ULONG                    MessageLanguageId,
    ULONG                    MessageId,
    PMESSAGE_RESOURCE_ENTRY *MessageEntry
);

typedef struct
{
    ULONG Count[2];
    ULONG State[4];
    UCHAR Buffer[64];
    ULONG Digest[4];

} RTL_MD5_CTX, *PRTL_MD5_CTX;

NATIVE_API
VOID
NTAPI
MD5Init(
    OUT PRTL_MD5_CTX Context
);

NATIVE_API
VOID
NTAPI
MD5Update(
    IN OUT  PRTL_MD5_CTX    Context,
    IN      PVOID           Buffer,
    IN      ULONG           BufferSize
);

NATIVE_API
VOID
NTAPI
MD5Final(
    IN OUT  PRTL_MD5_CTX Context
);

typedef struct
{
    ULONG Unknown[6];
    ULONG State[5];
    ULONG Count[2];
    UCHAR Buffer[64];

} RTL_SHA_CTX, *PRTL_SHA_CTX;

typedef struct
{
    ULONG Digest[5];
} RTL_SHA_DIG, *PRTL_SHA_DIG;

NATIVE_API
VOID
NTAPI
A_SHAInit(
    PRTL_SHA_CTX ShaContext
);

NATIVE_API
VOID
NTAPI
A_SHAUpdate(
    PRTL_SHA_CTX    ShaContext,
    PVOID           Buffer,
    ULONG           BufferSize
);

NATIVE_API
VOID
NTAPI
A_SHAFinal(
    PRTL_SHA_CTX ShaContext,
    PRTL_SHA_DIG Digest
);

/************************************************************************
  handles
************************************************************************/

NATIVE_API
VOID
NTAPI
RtlInitializeHandleTable(
    IN  ULONG               TableSize,
    IN  ULONG               HandleSize,
    OUT PRTL_HANDLE_TABLE   HandleTable
);

NATIVE_API
PRTL_HANDLE_TABLE_ENTRY
NTAPI
RtlAllocateHandle(
    IN OUT  PRTL_HANDLE_TABLE   HandleTable,
    OUT     PULONG              Index
);

NATIVE_API
BOOLEAN
NTAPI
RtlFreeHandle(
    IN OUT  PRTL_HANDLE_TABLE       HandleTable,
    IN      PRTL_HANDLE_TABLE_ENTRY Handle
);

NATIVE_API
BOOLEAN
NTAPI
RtlIsValidHandle(
    IN PRTL_HANDLE_TABLE        HandleTable,
    IN PRTL_HANDLE_TABLE_ENTRY  Handle
);

NATIVE_API
BOOLEAN
NTAPI
RtlIsValidIndexHandle(
    IN  PRTL_HANDLE_TABLE        HandleTable,
    IN  ULONG                    Index,
    OUT PRTL_HANDLE_TABLE_ENTRY *Handle
);

NATIVE_API
VOID
NTAPI
RtlDestroyHandleTable(
    IN OUT PRTL_HANDLE_TABLE HandleTable
);

#define COMPRESSION_FORMAT_NONE          (0x0000)   // winnt
#define COMPRESSION_FORMAT_DEFAULT       (0x0001)   // winnt
#define COMPRESSION_FORMAT_LZNT1         (0x0002)   // winnt
#define COMPRESSION_FORMAT_XPRESS        (0x0003)
#define COMPRESSION_FORMAT_XPRESS_HUFF   (0x0004)

#define COMPRESSION_ENGINE_STANDARD      (0x0000)   // winnt
#define COMPRESSION_ENGINE_MAXIMUM       (0x0100)   // winnt
#define COMPRESSION_ENGINE_HIBER         (0x0200)   // winnt

NATIVE_API
NTSTATUS
NTAPI
RtlGetCompressionWorkSpaceSize(
    IN  USHORT CompressionFormatAndEngine,
    OUT PULONG CompressBufferWorkSpaceSize,
    OUT PULONG CompressFragmentWorkSpaceSize
);

NATIVE_API
NTSTATUS
NTAPI
RtlCompressBuffer(
    IN  USHORT  CompressionFormatAndEngine,
    IN  PVOID   UncompressedBuffer,
    IN  ULONG   UncompressedBufferSize,
    OUT PVOID   CompressedBuffer,
    IN  ULONG   CompressedBufferSize,
    IN  ULONG   UncompressedChunkSize,
    OUT PULONG  FinalCompressedSize,
    IN  PVOID   WorkSpace
);

NATIVE_API
NTSTATUS
NTAPI
RtlDecompressBuffer(
    IN  USHORT  CompressionFormat,
    OUT PVOID   UncompressedBuffer,
    IN  ULONG   UncompressedBufferSize,
    IN  PVOID   CompressedBuffer,
    IN  ULONG   CompressedBufferSize,
    OUT PULONG  FinalUncompressedSize
);


#endif // _NTMISC_H_e2789f20_6dd7_48d9_841c_b38cc5e46666_
