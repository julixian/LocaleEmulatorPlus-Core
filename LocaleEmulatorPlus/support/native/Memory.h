#ifndef _NTMEMORY_H_d4d5c9b1_7569_4859_b25a_cc5cadd2b166_
#define _NTMEMORY_H_d4d5c9b1_7569_4859_b25a_cc5cadd2b166_

#ifndef _NTOBJECT_H_44725294_0381_41e9_872d_92a0dd9f3ac0_
#define _NTOBJECT_H_44725294_0381_41e9_872d_92a0dd9f3ac0_


typedef struct
{
    ULONG           Attributes;
    ACCESS_MASK     DesiredAccess;
    ULONG           HandleCount;
    ULONG           ReferenceCount;
    ULONG           PagedPoolUsage;
    ULONG           NonPagedPoolUsage;
    ULONG           Reserved[3];
    ULONG           NameInformationLength;
    ULONG           TypeInformationLength;
    ULONG           SecurityDescriptorLength;
    LARGE_INTEGER   CreationTime;

} OBJECT_BASIC_INFORMATION, *POBJECT_BASIC_INFORMATION;

typedef struct
{
    UNICODE_STRING  Name;
    WCHAR           Buffer[MAX_NTPATH];

} OBJECT_NAME_INFORMATION2, *POBJECT_NAME_INFORMATION2;

typedef struct _OBJECT_TYPE_INFORMATION
{
    UNICODE_STRING      TypeName;
    ULONG               TotalNumberOfObjects;
    ULONG               TotalNumberOfHandles;
    ULONG               TotalPagedPoolUsage;
    ULONG               TotalNonPagedPoolUsage;
    ULONG               TotalNamePoolUsage;
    ULONG               TotalHandleTableUsage;
    ULONG               HighWaterNumberOfObjects;
    ULONG               HighWaterNumberOfHandles;
    ULONG               HighWaterPagedPoolUsage;
    ULONG               HighWaterNonPagedPoolUsage;
    ULONG               HighWaterNamePoolUsage;
    ULONG               HighWaterHandleTableUsage;
    ULONG               InvalidAttributes;
    GENERIC_MAPPING     GenericMapping;
    ULONG               ValidAccessMask;
    BOOLEAN             SecurityRequired;
    BOOLEAN             MaintainHandleCount;
    ULONG               PoolType;
    ULONG               DefaultPagedPoolCharge;
    ULONG               DefaultNonPagedPoolCharge;

} OBJECT_TYPE_INFORMATION, *POBJECT_TYPE_INFORMATION;

#if CPP_DEFINED

typedef struct OBJECT_TYPE_INFORMATION2 : public OBJECT_TYPE_INFORMATION
{
    OBJECT_TYPE_INFORMATION Type;
    WCHAR TypeNameBuffer[MAX_NTPATH];

} OBJECT_TYPE_INFORMATION2, *POBJECT_TYPE_INFORMATION2;

#endif // c++

typedef struct
{
    ULONG                   NumberOfObjectsTypes;
    OBJECT_TYPE_INFORMATION TypeInformation[1];

} OBJECT_TYPES_INFORMATION, *POBJECT_TYPES_INFORMATION;

typedef struct
{
    BOOLEAN InheritHandle;
    BOOLEAN ProtectFromClose;

} OBJECT_HANDLE_FLAG_INFORMATION, *POBJECT_HANDLE_FLAG_INFORMATION;

    typedef struct _OBJECT_TYPE *POBJECT_TYPE;

typedef struct _OBJECT_TYPE_INITIALIZER *POBJECT_TYPE_INITIALIZER;

NATIVE_API
NTSTATUS
NTAPI
NtSetInformationObject(
    IN  HANDLE                      ObjectHandle,
    IN  OBJECT_INFORMATION_CLASS    ObjectInformationClass,
    IN  PVOID                       ObjectInformation,
    IN  ULONG                       Length
);

NATIVE_API
NTSTATUS
NTAPI
NtDuplicateObject(
    IN  HANDLE      SourceProcessHandle,
    IN  HANDLE      SourceHandle,
    IN  HANDLE      TargetProcessHandle OPTIONAL,
    OUT PHANDLE     TargetHandle OPTIONAL,
    IN  ACCESS_MASK DesiredAccess,
    IN  ULONG       HandleAttributes,
    IN  ULONG       Options
);

NATIVE_API
NTSTATUS
NTAPI
ZwDuplicateObject(
    IN  HANDLE      SourceProcessHandle,
    IN  HANDLE      SourceHandle,
    IN  HANDLE      TargetProcessHandle OPTIONAL,
    OUT PHANDLE     TargetHandle OPTIONAL,
    IN  ACCESS_MASK DesiredAccess,
    IN  ULONG       HandleAttributes,
    IN  ULONG       Options
);

NATIVE_API
NTSTATUS
NTAPI
ZwQueryObject(
    HANDLE                      Handle,
    OBJECT_INFORMATION_CLASS    ObjectInformationClass,
    PVOID                       ObjectInformation,
    ULONG                       ObjectInformationLength,
    PULONG                      ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
NtQueryObject(
    HANDLE                      Handle,
    OBJECT_INFORMATION_CLASS    ObjectInformationClass,
    PVOID                       ObjectInformation,
    ULONG                       ObjectInformationLength,
    PULONG                      ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
NtClose(
    IN HANDLE Handle
);

NATIVE_API
NTSTATUS
NTAPI
ZwClose(
    IN HANDLE Handle
);


#endif // _NTOBJECT_H_44725294_0381_41e9_872d_92a0dd9f3ac0_

/************************************************************************
  memory
************************************************************************/
typedef enum _MEMORY_INFORMATION_CLASS
{
    MemoryBasicInformation              = 0,
    MemoryWorkingSetInformation         = 1,
    MemoryMappedFilenameInformation     = 2,
    MemoryRegionInformation             = 3,
    MemoryWorkingSetExInformation       = 4,

} MEMORY_INFORMATION_CLASS;


typedef OBJECT_NAME_INFORMATION MEMORY_MAPPED_FILENAME_INFORMATION, *PMEMORY_MAPPED_FILENAME_INFORMATION;
typedef OBJECT_NAME_INFORMATION2 MEMORY_MAPPED_FILENAME_INFORMATION2;

typedef struct _MEMORY_WORKING_SET_BLOCK
{
    ULONG_PTR Protection : 5;
    ULONG_PTR ShareCount : 3;
    ULONG_PTR Shared : 1;
    ULONG_PTR Node : 3;
#ifdef ML_AMD64
    ULONG_PTR VirtualPage : 52;
#else
    ULONG VirtualPage : 20;
#endif
} MEMORY_WORKING_SET_BLOCK, *PMEMORY_WORKING_SET_BLOCK;

typedef struct _MEMORY_WORKING_SET_INFORMATION
{
    ULONG_PTR NumberOfEntries;
    MEMORY_WORKING_SET_BLOCK WorkingSetInfo[1];
} MEMORY_WORKING_SET_INFORMATION, *PMEMORY_WORKING_SET_INFORMATION;

// private
typedef struct _MEMORY_REGION_INFORMATION
{
    PVOID AllocationBase;
    ULONG AllocationProtect;
    ULONG RegionType;
    SIZE_T RegionSize;
} MEMORY_REGION_INFORMATION, *PMEMORY_REGION_INFORMATION;

// private
typedef struct _MEMORY_WORKING_SET_EX_BLOCK
{
    union
    {
        struct
        {
            ULONG_PTR Valid : 1;
            ULONG_PTR ShareCount : 3;
            ULONG_PTR Win32Protection : 11;
            ULONG_PTR Shared : 1;
            ULONG_PTR Node : 6;
            ULONG_PTR Locked : 1;
            ULONG_PTR LargePage : 1;
            ULONG_PTR Priority : 3;
            ULONG_PTR Reserved : 4;
            ULONG_PTR Bad : 1;
#ifdef ML_AMD64
            ULONG_PTR ReservedUlong : 32;
#endif
        };
        struct
        {
            ULONG_PTR Valid : 1;
            ULONG_PTR Reserved0 : 14;
            ULONG_PTR Shared : 1;
            ULONG_PTR Reserved1 : 5;
            ULONG_PTR PageTable : 1;
            ULONG_PTR Location : 2;
            ULONG_PTR Reserved2 : 7;
            ULONG_PTR Bad : 1;
#ifdef ML_AMD64
            ULONG_PTR ReservedUlong : 32;
#endif
        } Invalid;
    };
} MEMORY_WORKING_SET_EX_BLOCK, *PMEMORY_WORKING_SET_EX_BLOCK;

// private
typedef struct _MEMORY_WORKING_SET_EX_INFORMATION
{
    PVOID VirtualAddress;
    union
    {
        MEMORY_WORKING_SET_EX_BLOCK VirtualAttributes;
        ULONG Long;
    } u1;
} MEMORY_WORKING_SET_EX_INFORMATION, *PMEMORY_WORKING_SET_EX_INFORMATION;

/************************************************************************
  image section information
************************************************************************/


typedef enum
{
    ViewShare = 1,
    ViewUnmap = 2,

} SECTION_INHERIT;


typedef enum
{
    SectionBasicInformation,
    SectionImageInformation,
    SectionRelocationInformation,   // ret = now_base - desire_base

} SECTION_INFORMATION_CLASS;

typedef struct _SECTION_BASIC_INFORMATION
{
    PVOID           BaseAddress;
    ULONG           Attributes;
    LARGE_INTEGER   Size;
} SECTION_BASIC_INFORMATION, *PSECTION_BASIC_INFORMATION;

typedef struct _SECTION_IMAGE_INFORMATION
{
    PVOID   TransferAddress;                    // 0x00
    ULONG   ZeroBits;                           // 0x04
    SIZE_T  MaximumStackSize;                   // 0x08
    SIZE_T  CommittedStackSize;                 // 0x0C
    ULONG   SubSystemType;                      // 0x10

    union
    {
        struct
        {
            USHORT SubSystemMinorVersion;
            USHORT SubSystemMajorVersion;
        };
        ULONG SubSystemVersion;                 // 0x14
    };

    ULONG   GpValue;                            // 0x18
    USHORT  ImageCharacteristics;               // 0x1C
    USHORT  DllCharacteristics;                 // 0x1E
    USHORT  Machine;                            // 0x20
    UCHAR   ImageContainsCode;                  // 0x22
    union
    {
        UCHAR ImageFlags;                       // 0x23
        struct
        {
            UCHAR ComPlusNativeReady        : 1;
            UCHAR ComPlusILOnly             : 1;
            UCHAR ImageDynamicallyRelocated : 1;
            UCHAR ImageMappedFlat           : 1;
        };
    } ImageFlags;

    ULONG   LoaderFlags;                        // 0x24
    ULONG   ImageFileSize;                      // 0x28
    ULONG   CheckSum;                           // 0x2C

} SECTION_IMAGE_INFORMATION, *PSECTION_IMAGE_INFORMATION;

NATIVE_API
NTSTATUS
NTAPI
NtAllocateVirtualMemory(
    IN      HANDLE      ProcessHandle,
    IN OUT  PVOID*      BaseAddress,
    IN      ULONG_PTR   ZeroBits,
    IN OUT  PSIZE_T     RegionSize,
    IN      ULONG       AllocationType,
    IN      ULONG       Protect
);

NATIVE_API
NTSTATUS
NTAPI
ZwAllocateVirtualMemory(
    IN      HANDLE      ProcessHandle,
    IN OUT  PVOID*      BaseAddress,
    IN      ULONG_PTR   ZeroBits,
    IN OUT  PSIZE_T     RegionSize,
    IN      ULONG       AllocationType,
    IN      ULONG       Protect
);

NATIVE_API
NTSTATUS
NTAPI
NtFreeVirtualMemory(
    IN      HANDLE  ProcessHandle,
    IN OUT  PVOID  *BaseAddress,
    IN OUT  PSIZE_T RegionSize,
    IN      ULONG   FreeType
);

NATIVE_API
NTSTATUS
NTAPI
ZwFreeVirtualMemory(
    IN      HANDLE  ProcessHandle,
    IN OUT  PVOID  *BaseAddress,
    IN OUT  PSIZE_T RegionSize,
    IN      ULONG   FreeType
);

NATIVE_API
NTSTATUS
NTAPI
NtProtectVirtualMemory(
    IN      HANDLE      ProcessHandle,
    IN OUT  PVOID*      BaseAddress,
    IN OUT  PULONG_PTR  ProtectSize,
    IN      ULONG       NewProtect,
    OUT     PULONG      OldProtect
);

NATIVE_API
NTSTATUS
NTAPI
ZwProtectVirtualMemory(
    IN      HANDLE      ProcessHandle,
    IN OUT  PVOID*      BaseAddress,
    IN OUT  PULONG_PTR  ProtectSize,
    IN      ULONG       NewProtect,
    OUT     PULONG      OldProtect
);

NATIVE_API
NTSTATUS
NTAPI
NtQueryVirtualMemory(
    HANDLE                      ProcessHandle,
    PVOID                       BaseAddress,
    MEMORY_INFORMATION_CLASS    MemoryInformationClass,
    PVOID                       MemoryInformation,
    ULONG_PTR                   MemoryInformationLength,
    PULONG_PTR                  ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
NtWow64QueryVirtualMemory64(
    IN  HANDLE                      ProcessHandle,
    IN  PVOID64                     BaseAddress,
    IN  MEMORY_INFORMATION_CLASS    MemoryInformationClass,
    OUT PVOID                       MemoryInformation, /* NB must be 64bit aligned */
    IN  ULONG64                     Length,
    OUT PULONGLONG                  ReturnLength OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtReadVirtualMemory(
    IN    HANDLE  ProcessHandle,
    IN    PVOID   BaseAddress,
    OUT   PVOID   Buffer,
    IN    SIZE_T  NumberOfBytesToRead,
    OUT   PSIZE_T NumberOfBytesRead OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
ZwReadVirtualMemory(
    IN    HANDLE  ProcessHandle,
    IN    PVOID   BaseAddress,
    OUT   PVOID   Buffer,
    IN    SIZE_T  NumberOfBytesToRead,
    OUT   PSIZE_T NumberOfBytesRead OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtWriteVirtualMemory(
    IN    HANDLE  ProcessHandle,
    IN    PVOID   BaseAddress,
    IN    PVOID   Buffer,
    IN    SIZE_T  NumberOfBytesToWrite,
    OUT   PSIZE_T NumberOfBytesWritten OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
ZwWriteVirtualMemory(
    IN    HANDLE  ProcessHandle,
    IN    PVOID   BaseAddress,
    IN    PVOID   Buffer,
    IN    SIZE_T  NumberOfBytesToWrite,
    OUT   PSIZE_T NumberOfBytesWritten OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtFlushInstructionCache(
    IN HANDLE ProcessHandle,
    IN PVOID  BaseAddress,
    IN SIZE_T NumberOfBytesToFlush
);

NATIVE_API
NTSTATUS
NTAPI
ZwFlushInstructionCache(
    IN HANDLE ProcessHandle,
    IN PVOID  BaseAddress,
    IN SIZE_T NumberOfBytesToFlush
);

/*++

    NtCreateSection
    ===============

    Creates a section object.

    SectionHandle - Points to a variable that will receive the section
        object handle if the call is successful.

    DesiredAccess - Specifies the type of access that the caller requires
        to the section object. This parameter can be zero, or any combination
        of the following flags:

        SECTION_QUERY       - Query access
        SECTION_MAP_WRITE   - Can be written when mapped
        SECTION_MAP_READ    - Can be read when mapped
        SECTION_MAP_EXECUTE - Can be executed when mapped
        SECTION_EXTEND_SIZE - Extend access
        SECTION_ALL_ACCESS  - All of the preceding +
                              STANDARD_RIGHTS_REQUIRED

    ObjectAttributes - Points to a structure that specifies the object??attributes.
        OBJ_OPENLINK is not a valid attribute for a section object.

    MaximumSize - Optionally points to a variable that specifies the size,
        in bytes, of the section. If FileHandle is zero, the size must be
        specified; otherwise, it can be defaulted from the size of the file
        referred to by FileHandle.

    SectionPageProtection - The protection desired for the pages
        of the section when the section is mapped. This parameter can take
        one of the following values:

        PAGE_READONLY
        PAGE_READWRITE
        PAGE_WRITECOPY
        PAGE_EXECUTE
        PAGE_EXECUTE_READ
        PAGE_EXECUTE_READWRITE
        PAGE_EXECUTE_WRITECOPY

    AllocationAttributes - The attributes for the section. This parameter must
        be a combination of the following values:

        SEC_BASED     0x00200000    // Map section at same address in each process
        SEC_NO_CHANGE 0x00400000    // Disable changes to protection of pages
        SEC_IMAGE     0x01000000    // Map section as an image
        SEC_VLM       0x02000000    // Map section in VLM region
        SEC_RESERVE   0x04000000    // Reserve without allocating pagefile storage
        SEC_COMMIT    0x08000000    // Commit pages; the default behavior
        SEC_NOCACHE   0x10000000    // Mark pages as non-cacheable

    FileHandle - Identifies the file from which to create the section object.
        The file must be opened with an access mode compatible with the protection
        flags specified by the Protect parameter. If FileHandle is zero,
        the function creates a section object of the specified size backed
        by the paging file rather than by a named file in the file system.

--*/

NATIVE_API
NTSTATUS
NTAPI
NtCreateSection(
    OUT PHANDLE             SectionHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
    IN  PLARGE_INTEGER      MaximumSize,
    IN  ULONG               SectionPageProtection,
    IN  ULONG               AllocationAttributes,
    IN  HANDLE              FileHandle OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
ZwCreateSection(
    OUT PHANDLE             SectionHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
    IN  PLARGE_INTEGER      MaximumSize OPTIONAL,
    IN  ULONG               SectionPageProtection,
    IN  ULONG               AllocationAttributes,
    IN  HANDLE              FileHandle OPTIONAL
);

NATIVE_API
NTSTATUS
NTAPI
NtOpenSection(
    OUT PHANDLE             SectionHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes
);

NATIVE_API
NTSTATUS
NTAPI
ZwOpenSection(
    OUT PHANDLE             SectionHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes
);

NATIVE_API
NTSTATUS
NTAPI
NtMapViewOfSection(
    IN      HANDLE          SectionHandle,
    IN      HANDLE          ProcessHandle,
    IN OUT  PVOID          *BaseAddress,
    IN      ULONG_PTR       ZeroBits,
    IN      SIZE_T          CommitSize,
    IN OUT  PLARGE_INTEGER  SectionOffset OPTIONAL,
    IN OUT  PSIZE_T         ViewSize,
    IN      SECTION_INHERIT InheritDisposition,
    IN      ULONG           AllocationType,
    IN      ULONG           Win32Protect
);

NATIVE_API
NTSTATUS
NTAPI
ZwMapViewOfSection(
    IN      HANDLE          SectionHandle,
    IN      HANDLE          ProcessHandle,
    IN OUT  PVOID          *BaseAddress,
    IN      ULONG_PTR       ZeroBits,
    IN      SIZE_T          CommitSize,
    IN OUT  PLARGE_INTEGER  SectionOffset OPTIONAL,
    IN OUT  PSIZE_T         ViewSize,
    IN      SECTION_INHERIT InheritDisposition,
    IN      ULONG           AllocationType,
    IN      ULONG           Win32Protect
);

NATIVE_API
NTSTATUS
NTAPI
NtUnmapViewOfSection(
    IN HANDLE ProcessHandle,
    IN PVOID BaseAddress
);

NATIVE_API
NTSTATUS
NTAPI
ZwUnmapViewOfSection(
    IN HANDLE ProcessHandle,
    IN PVOID BaseAddress
);

NATIVE_API
NTSTATUS
NTAPI
NtQuerySection(
    IN  HANDLE                      SectionHandle,
    IN  SECTION_INFORMATION_CLASS   SectionInformationClass,
    OUT PVOID                       SectionInformation,
    IN  SIZE_T                      Length,
    OUT PULONG                      ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
NtAllocateUserPhysicalPages(
    IN      HANDLE      ProcessHandle,
    IN OUT  PULONG_PTR  NumberOfPages,
    OUT     PULONG_PTR  UserPfnArray
);

NATIVE_API
NTSTATUS
NTAPI
NtGetWriteWatch(
    HANDLE      ProcessHandle,
    ULONG       Flags,
    PVOID       BaseAddress,
    SIZE_T      RegionSize,
    PVOID*      UserAddressArray,
    PULONG_PTR  EntriesInUserAddressArray,
    PULONG      Granularity
);

NATIVE_API
NTSTATUS
NTAPI
NtResetWriteWatch(
    HANDLE  ProcessHandle,
    PVOID   BaseAddress,
    SIZE_T  RegionSize
);


/************************************************************************
  user mode
************************************************************************/

/************************************************************************
  memory
************************************************************************/

NATIVE_API
HANDLE
NTAPI
RtlCreateHeap(
    IN ULONG                Flags,
    IN PVOID                HeapBase OPTIONAL,
    IN SIZE_T               ReserveSize OPTIONAL,
    IN SIZE_T               CommitSize OPTIONAL,
    IN PVOID                Lock OPTIONAL,
    IN PRTL_HEAP_PARAMETERS Parameters OPTIONAL
);

NATIVE_API
HANDLE
NTAPI
RtlDestroyHeap(
    IN HANDLE HeapBase
);

NATIVE_API
PVOID
NTAPI
RtlAllocateHeap(
    IN HANDLE   HeapBase,
    IN ULONG    Flags,
    IN SIZE_T   Bytes
);

NATIVE_API
PVOID
NTAPI
RtlReAllocateHeap(
    IN HANDLE   HeapBase,
    IN ULONG    Flags,
    IN PVOID    Memory,
    IN SIZE_T   Bytes
);

NATIVE_API
BOOLEAN
NTAPI
RtlFreeHeap(
    IN HANDLE   HeapBase,
    IN ULONG    Flags,
    IN LPVOID   Memory
);

NATIVE_API
ULONG_PTR
WINAPI
RtlSizeHeap(
    IN HANDLE   HeapBase,
    IN ULONG    Flags,
    IN PVOID    Memory
);

NATIVE_API
BOOLEAN
NTAPI
RtlLockHeap(
    IN PVOID HeapHandle
);

NATIVE_API
BOOLEAN
NTAPI
RtlUnlockHeap(
    IN PVOID HeapHandle
);


#endif // _NTMEMORY_H_d4d5c9b1_7569_4859_b25a_cc5cadd2b166_
