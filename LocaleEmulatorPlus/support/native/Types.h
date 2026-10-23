#ifndef _NTBASE_H_bc4f0ce4_29e3_43c6_a3a3_98415b2adbab_
#define _NTBASE_H_bc4f0ce4_29e3_43c6_a3a3_98415b2adbab_

#if ML_AMD64
    #pragma pack(push, 8)
#else
    #pragma pack(push, 4)
#endif

#ifndef _NATIVETYPES_H_96405a6d_a23f_4fd6_b20a_e5821226bfc6
#define _NATIVETYPES_H_96405a6d_a23f_4fd6_b20a_e5821226bfc6


#ifndef _NATIVETYPES_USER_H_30d04cf8_0b66_4f36_a472_f9c1734240b8
#define _NATIVETYPES_USER_H_30d04cf8_0b66_4f36_a472_f9c1734240b8

#pragma INCLUDE_LIB(ntdll.lib)


_ML_C_HEAD_

#if CPP_DEFINED

IF_NOT_EXIST(NTSTATUS)
{
    typedef LONG NTSTATUS;
}

#endif

/************************************************************************
  base
************************************************************************/

typedef struct _CLIENT_ID
{
    HANDLE  UniqueProcess;
    HANDLE  UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _CLIENT_ID32
{
    ULONG32  UniqueProcess;
    ULONG32  UniqueThread;

} CLIENT_ID32, *PCLIENT_ID32;

/************************************************************************/
/* strings                                                              */
/************************************************************************/

typedef struct _STRING
{
    USHORT  Length;
    USHORT  MaximumLength;
    PCHAR   Buffer;
} STRING;

typedef STRING *PSTRING;
typedef STRING ANSI_STRING;
typedef PSTRING PANSI_STRING;

typedef struct _UNICODE_STRING {
    USHORT  Length;
    USHORT  MaximumLength;
    PWSTR   Buffer;
} UNICODE_STRING;

typedef struct _LARGE_UNICODE_STRING
{
    ULONG Length;
    ULONG MaximumLength : 31;
    ULONG Ansi          : 1;

    union
    {
        PWSTR   UnicodeBuffer;
        PSTR    AnsiBuffer;
        ULONG64 Buffer;
    };

} LARGE_UNICODE_STRING, *PLARGE_UNICODE_STRING;

#pragma pack(push, 8)

typedef struct
{
    USHORT Length;
    USHORT MaximumLength;
    union
    {
        PWSTR  Buffer;
        ULONG64 Dummy;
    };

} ANSI_STRING3264, *PANSI_STRING3264;

typedef struct
{
    USHORT Length;
    USHORT MaximumLength;
    union
    {
        PWSTR  Buffer;
        ULONG64 Dummy;
    };

} UNICODE_STRING3264, *PUNICODE_STRING3264;

typedef UNICODE_STRING3264 UNICODE_STRING64;
typedef PUNICODE_STRING3264 PUNICODE_STRING64;

#pragma pack(pop)

typedef UNICODE_STRING *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;

#if CPP_DEFINDED

EXTCPP
{
    char _RTL_CONSTANT_STRING_type_check(const char *s);
    char _RTL_CONSTANT_STRING_type_check(const WCHAR *s);
    // __typeof would be desirable here instead of sizeof.
    template <size_t N> class _RTL_CONSTANT_STRING_remove_const_template_class;
    template <> class _RTL_CONSTANT_STRING_remove_const_template_class<sizeof(char)>  {public: typedef  char T; };
    template <> class _RTL_CONSTANT_STRING_remove_const_template_class<sizeof(WCHAR)> {public: typedef WCHAR T; };
    #define _RTL_CONSTANT_STRING_remove_const_macro(s) \
        (const_cast<_RTL_CONSTANT_STRING_remove_const_template_class<sizeof((s)[0])>::T*>(s))
}

#else
    char _RTL_CONSTANT_STRING_type_check(const void *s);
    #define _RTL_CONSTANT_STRING_remove_const_macro(s) (s)
#endif  // CPP_DEFINDED

#define RTL_CONSTANT_STRING(s) \
{ \
    sizeof( s ) - sizeof( (s)[0] ), \
    sizeof( s ) / sizeof(_RTL_CONSTANT_STRING_type_check(s)), \
    _RTL_CONSTANT_STRING_remove_const_macro(s) \
}


/************************************************************************/
/* others                                                               */
/************************************************************************/

#if !defined(NtCurrentProcess)
    #define NtCurrentProcess() (HANDLE)-1
    #define NtCurrentProcess64() (HANDLE64)-1
#endif /* NtCurrentProcess */

#if !defined(NtCurrentThread)
    #define NtCurrentThread() (HANDLE)-2
    #define NtCurrentThread64() (HANDLE64)-2
#endif /* NtCurrentThread */

#ifndef NT_SUCCESS

ForceInline Bool IsStatusSuccess(NTSTATUS Status) { return (Status & Status) >= 0; }
//    #define NT_SUCCESS(Status)  (((NTSTATUS)(Status)) >= 0)
    #define NT_SUCCESS(Status)  IsStatusSuccess(Status)
#endif /* NT_SUCCESS */

#if !defined(RT_MESSAGETABLE)
    #define RT_MESSAGETABLE 0x0B
#endif




/************************************************************************
  object types
************************************************************************/

typedef enum
{
    ObjectBasicInformation       = 0,
    ObjectNameInformation        = 1,
    ObjectTypeInformation        = 2,
    ObjectTypesInformation       = 3,
    ObjectHandleFlagInformation  = 4,
    ObjectSessionInformation     = 5,
    MaxObjectInfoClass,

} OBJECT_INFORMATION_CLASS;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;        // Points to type SECURITY_DESCRIPTOR
    PVOID SecurityQualityOfService;  // Points to type SECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _OBJECT_ATTRIBUTES64 {
    ULONG64 Length;
    ULONG64 RootDirectory;
    ULONG64 ObjectName;
    ULONG64 Attributes;
    ULONG64 SecurityDescriptor;
    ULONG64 SecurityQualityOfService;
} OBJECT_ATTRIBUTES64;

typedef OBJECT_ATTRIBUTES64 *POBJECT_ATTRIBUTES64;

typedef CONST OBJECT_ATTRIBUTES *PCOBJECT_ATTRIBUTES;

#ifndef OBJ_INHERIT
    #define OBJ_INHERIT             0x00000002L
    #define OBJ_PERMANENT           0x00000010L
    #define OBJ_EXCLUSIVE           0x00000020L
    #define OBJ_CASE_INSENSITIVE    0x00000040L
    #define OBJ_OPENIF              0x00000080L
    #define OBJ_OPENLINK            0x00000100L
    #define OBJ_KERNEL_HANDLE       0x00000200L
    #define OBJ_FORCE_ACCESS_CHECK  0x00000400L
    #define OBJ_VALID_ATTRIBUTES    0x000007F2L
#endif // OBJ_INHERIT

#undef InitializeObjectAttributes
inline VOID InitializeObjectAttributes(POBJECT_ATTRIBUTES p, PUNICODE_STRING n, ULONG a, HANDLE r, PVOID s)
{
    p->Length = sizeof( *p );
    p->RootDirectory = r;
    p->Attributes = a;
    p->ObjectName = n;
    p->SecurityDescriptor = s;
    p->SecurityQualityOfService = NULL;

    *(volatile PUNICODE_STRING *)&n = n;
}

#ifndef InitializeObjectAttributes
    #define __InitializeObjectAttributes_unused( p, n, a, r, s ) { \
        (p)->Length = sizeof( OBJECT_ATTRIBUTES );          \
        (p)->RootDirectory = r;                             \
        (p)->Attributes = a;                                \
        (p)->ObjectName = n;                                \
        (p)->SecurityDescriptor = s;                        \
        (p)->SecurityQualityOfService = NULL;               \
    }
#endif


/************************************************************************
  object information
************************************************************************/

//
// Object Manager Object Type Specific Access Rights.
//

#define OBJECT_TYPE_CREATE (0x0001)

#define OBJECT_TYPE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x1)

typedef struct
{
    UNICODE_STRING  Name;

} OBJECT_NAME_INFORMATION, *POBJECT_NAME_INFORMATION;

/************************************************************************/
/* file                                                                 */
/************************************************************************/

#define FSRTL_FAT_LEGAL         0x01
#define FSRTL_HPFS_LEGAL        0x02
#define FSRTL_NTFS_LEGAL        0x04
#define FSRTL_WILD_CHARACTER    0x08
#define FSRTL_OLE_LEGAL         0x10
#define FSRTL_NTFS_STREAM_LEGAL (FSRTL_NTFS_LEGAL | FSRTL_OLE_LEGAL)

#define FsRtlIsUnicodeCharacterWild(C) (                                \
      (((C) >= 0x40) ? FALSE : FLAG_ON( LepGAL_ANSI_CHARACTER_ARRAY[(C)], \
                                       FSRTL_WILD_CHARACTER ) )         \
)

#define ANSI_DOS_STAR   ('<')
#define ANSI_DOS_QM     ('>')
#define ANSI_DOS_DOT    ('"')

#define DOS_STAR        (L'<')
#define DOS_QM          (L'>')
#define DOS_DOT         (L'"')

//
// Object Manager Directory Specific Access Rights.
//

#define DIRECTORY_QUERY                 (0x0001)
#define DIRECTORY_TRAVERSE              (0x0002)
#define DIRECTORY_CREATE_OBJECT         (0x0004)
#define DIRECTORY_CREATE_SUBDIRECTORY   (0x0008)

#define DIRECTORY_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0xF)

typedef struct _DIRECTORY_BASIC_INFORMATION {
    UNICODE_STRING ObjectName;
    UNICODE_STRING ObjectTypeName;
} DIRECTORY_BASIC_INFORMATION, *PDIRECTORY_BASIC_INFORMATION;

//begin_winnt

//
// Object Manager Symbolic Link Specific Access Rights.
//

//end_winnt

#define SYMBOLIC_LINK_QUERY (0x0001)

#define SYMBOLIC_LINK_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x1)


typedef enum _FSINFOCLASS {

    FileFsVolumeInformation       = 1,
    FileFsLabelInformation,      // 2
    FileFsSizeInformation,       // 3
    FileFsDeviceInformation,     // 4
    FileFsAttributeInformation,  // 5
    FileFsControlInformation,    // 6
    FileFsFullSizeInformation,   // 7
    FileFsObjectIdInformation,   // 8
    FileFsDriverPathInformation, // 9
    FileFsVolumeFlagsInformation,// 10
    FileFsMaximumInformation

} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;

typedef struct _FILE_FS_LABEL_INFORMATION {
    ULONG VolumeLabelLength;
    WCHAR VolumeLabel[1];
} FILE_FS_LABEL_INFORMATION, *PFILE_FS_LABEL_INFORMATION;

typedef struct _FILE_FS_VOLUME_INFORMATION {
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION, *PFILE_FS_VOLUME_INFORMATION;

typedef struct _FILE_FS_VOLUME_INFORMATION2 {
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[MAX_NTPATH];
} FILE_FS_VOLUME_INFORMATION2, *PFILE_FS_VOLUME_INFORMATION2;

typedef struct _FILE_FS_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER AvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_SIZE_INFORMATION, *PFILE_FS_SIZE_INFORMATION;

typedef struct _FILE_FS_FULL_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER CallerAvailableAllocationUnits;
    LARGE_INTEGER ActualAvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION, *PFILE_FS_FULL_SIZE_INFORMATION;

typedef struct _FILE_FS_OBJECTID_INFORMATION {
    UCHAR ObjectId[16];
    UCHAR ExtendedInfo[48];
} FILE_FS_OBJECTID_INFORMATION, *PFILE_FS_OBJECTID_INFORMATION;

typedef struct _FILE_FS_DEVICE_INFORMATION {
    DEVICE_TYPE DeviceType;
    ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION, *PFILE_FS_DEVICE_INFORMATION;

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION {
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION, *PFILE_FS_ATTRIBUTE_INFORMATION;

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION2 {
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[MAX_NTPATH];
} FILE_FS_ATTRIBUTE_INFORMATION2, *PFILE_FS_ATTRIBUTE_INFORMATION2;

typedef struct _FILE_FS_DRIVER_PATH_INFORMATION {
    BOOLEAN DriverInPath;
    ULONG   DriverNameLength;
    WCHAR   DriverName[1];
} FILE_FS_DRIVER_PATH_INFORMATION, *PFILE_FS_DRIVER_PATH_INFORMATION;

typedef struct _FILE_FS_VOLUME_FLAGS_INFORMATION {
    ULONG Flags;
} FILE_FS_VOLUME_FLAGS_INFORMATION, *PFILE_FS_VOLUME_FLAGS_INFORMATION;

typedef enum
{
    FileDirectoryInformation                                = 1,
    FileFullDirectoryInformation                            = 2,
    FileBothDirectoryInformation                            = 3,
    FileBasicInformation                                    = 4,
    FileStandardInformation                                 = 5,
    FileInternalInformation                                 = 6,
    FileEaInformation                                       = 7,
    FileAccessInformation                                   = 8,
    FileNameInformation                                     = 9,
    FileRenameInformation                                   = 10,
    FileLinkInformation                                     = 11,
    FileNamesInformation                                    = 12,
    FileDispositionInformation                              = 13,
    FilePositionInformation                                 = 14,
    FileFullEaInformation                                   = 15,
    FileModeInformation                                     = 16,
    FileAlignmentInformation                                = 17,
    FileAllInformation                                      = 18,
    FileAllocationInformation                               = 19,
    FileEndOfFileInformation                                = 20,
    FileAlternateNameInformation                            = 21,
    FileStreamInformation                                   = 22,
    FilePipeInformation                                     = 23,
    FilePipeLocalInformation                                = 24,
    FilePipeRemoteInformation                               = 25,
    FileMailslotQueryInformation                            = 26,
    FileMailslotSetInformation                              = 27,
    FileCompressionInformation                              = 28,
    FileObjectIdInformation                                 = 29,
    FileCompletionInformation                               = 30,
    FileMoveClusterInformation                              = 31,
    FileQuotaInformation                                    = 32,
    FileReparsePointInformation                             = 33,
    FileNetworkOpenInformation                              = 34,
    FileAttributeTagInformation                             = 35,
    FileTrackingInformation                                 = 36,
    FileIdBothDirectoryInformation                          = 37,
    FileIdFullDirectoryInformation                          = 38,
    FileValidDataLengthInformation                          = 39,
    FileShortNameInformation                                = 40,
    FileIoCompletionNotificationInformation                 = 41,
    FileIoStatusBlockRangeInformation                       = 42,
    FileIoPriorityHintInformation                           = 43,
    FileSfioReserveInformation                              = 44,
    FileSfioVolumeInformation                               = 45,
    FileHardLinkInformation                                 = 46,
    FileProcessIdsUsingFileInformation                      = 47,
    FileNormalizedNameInformation                           = 48,
    FileNetworkPhysicalNameInformation                      = 49,
    FileIdGlobalTxDirectoryInformation                      = 50,
    FileIsRemoteDeviceInformation                           = 51,
    FileAttributeCacheInformation                           = 52,
    FileNumaNodeInformation                                 = 53,
    FileStandardLinkInformation                             = 54,
    FileRemoteProtocolInformation                           = 55,
    FileMaximumInformation                                  = 56,

} FILE_INFORMATION_CLASS;

/************************************************************************/
/* FILE FLAGS                                                           */
/************************************************************************/

//
// Define the access check value for any access
//
//
// The FILE_READ_ACCESS and FILE_WRITE_ACCESS constants are also defined in
// ntioapi.h as FILE_READ_DATA and FILE_WRITE_DATA. The values for these
// constants *MUST* always be in sync.
//
//
// FILE_SPECIAL_ACCESS is checked by the NT I/O system the same as FILE_ANY_ACCESS.
// The file systems, however, may add additional access checks for I/O and FS controls
// that use this value.
//

#define FILE_ANY_ACCESS                 0
#define FILE_SPECIAL_ACCESS    (FILE_ANY_ACCESS)
#define FILE_READ_ACCESS          ( 0x0001 )    // file & pipe
#define FILE_WRITE_ACCESS         ( 0x0002 )    // file & pipe



//
// Define access rights to files and directories
//

//
// The FILE_READ_DATA and FILE_WRITE_DATA constants are also defined in
// devioctl.h as FILE_READ_ACCESS and FILE_WRITE_ACCESS. The values for these
// constants *MUST* always be in sync.
// The values are redefined in devioctl.h because they must be available to
// both DOS and NT.
//

#define FILE_READ_DATA            ( 0x0001 )    // file & pipe
#define FILE_LIST_DIRECTORY       ( 0x0001 )    // directory

#define FILE_WRITE_DATA           ( 0x0002 )    // file & pipe
#define FILE_ADD_FILE             ( 0x0002 )    // directory

#define FILE_APPEND_DATA          ( 0x0004 )    // file
#define FILE_ADD_SUBDIRECTORY     ( 0x0004 )    // directory
#define FILE_CREATE_PIPE_INSTANCE ( 0x0004 )    // named pipe


#define FILE_READ_EA              ( 0x0008 )    // file & directory

#define FILE_WRITE_EA             ( 0x0010 )    // file & directory

#define FILE_EXECUTE              ( 0x0020 )    // file
#define FILE_TRAVERSE             ( 0x0020 )    // directory

#define FILE_DELETE_CHILD         ( 0x0040 )    // directory

#define FILE_READ_ATTRIBUTES      ( 0x0080 )    // all

#define FILE_WRITE_ATTRIBUTES     ( 0x0100 )    // all

#define FILE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x1FF)

#define FILE_GENERIC_READ         (STANDARD_RIGHTS_READ     |\
                                   FILE_READ_DATA           |\
                                   FILE_READ_ATTRIBUTES     |\
                                   FILE_READ_EA             |\
                                   SYNCHRONIZE)


#define FILE_GENERIC_WRITE        (STANDARD_RIGHTS_WRITE    |\
                                   FILE_WRITE_DATA          |\
                                   FILE_WRITE_ATTRIBUTES    |\
                                   FILE_WRITE_EA            |\
                                   FILE_APPEND_DATA         |\
                                   SYNCHRONIZE)


#define FILE_GENERIC_EXECUTE      (STANDARD_RIGHTS_EXECUTE  |\
                                   FILE_READ_ATTRIBUTES     |\
                                   FILE_EXECUTE             |\
                                   SYNCHRONIZE)




//
// Define share access rights to files and directories
//

#define FILE_SHARE_READ                 0x00000001
#define FILE_SHARE_WRITE                0x00000002
#define FILE_SHARE_DELETE               0x00000004
#define FILE_SHARE_VALID_FLAGS          0x00000007

//
// Define the file attributes values
//
// Note:  0x00000008 is reserved for use for the old DOS VOLID (volume ID)
//        and is therefore not considered valid in NT.
//
// Note:  Note also that the order of these flags is set to allow both the
//        FAT and the Pinball File Systems to directly set the attributes
//        flags in attributes words without having to pick each flag out
//        individually.  The order of these flags should not be changed!
//

#define FILE_ATTRIBUTE_READONLY             0x00000001
#define FILE_ATTRIBUTE_HIDDEN               0x00000002
#define FILE_ATTRIBUTE_SYSTEM               0x00000004
//OLD DOS VOLID                             0x00000008

#define FILE_ATTRIBUTE_DIRECTORY            0x00000010
#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
#define FILE_ATTRIBUTE_DEVICE               0x00000040
#define FILE_ATTRIBUTE_NORMAL               0x00000080

#define FILE_ATTRIBUTE_TEMPORARY            0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE          0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT        0x00000400
#define FILE_ATTRIBUTE_COMPRESSED           0x00000800

#define FILE_ATTRIBUTE_OFFLINE              0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED            0x00004000

#define FILE_ATTRIBUTE_VIRTUAL              0x00010000

#define FILE_ATTRIBUTE_VALID_FLAGS          0x00007fb7
#define FILE_ATTRIBUTE_VALID_SET_FLAGS      0x000031a7

#define FILE_SUPERSEDE                  0x00000000
#define FILE_OPEN                       0x00000001
#define FILE_CREATE                     0x00000002
#define FILE_OPEN_IF                    0x00000003
#define FILE_OVERWRITE                  0x00000004
#define FILE_OVERWRITE_IF               0x00000005
#define FILE_MAXIMUM_DISPOSITION        0x00000005

//
// Define the create/open option flags
//

#define FILE_DIRECTORY_FILE                     0x00000001
#define FILE_WRITE_THROUGH                      0x00000002
#define FILE_SEQUENTIAL_ONLY                    0x00000004
#define FILE_NO_INTERMEDIATE_BUFFERING          0x00000008

#define FILE_SYNCHRONOUS_IO_ALERT               0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT            0x00000020
#define FILE_NON_DIRECTORY_FILE                 0x00000040
#define FILE_CREATE_TREE_CONNECTION             0x00000080

#define FILE_COMPLETE_IF_OPLOCKED               0x00000100
#define FILE_NO_EA_KNOWLEDGE                    0x00000200
#define FILE_OPEN_REMOTE_INSTANCE               0x00000400
#define FILE_RANDOM_ACCESS                      0x00000800

#define FILE_DELETE_ON_CLOSE                    0x00001000
#define FILE_OPEN_BY_FILE_ID                    0x00002000
#define FILE_OPEN_FOR_BACKUP_INTENT             0x00004000
#define FILE_NO_COMPRESSION                     0x00008000

#if (NTDDI_VERSION >= NTDDI_WIN7)
#define FILE_OPEN_REQUIRING_OPLOCK              0x00010000
#define FILE_DISALLOW_EXCLUSIVE                 0x00020000
#endif /* NTDDI_VERSION >= NTDDI_WIN7 */

#define FILE_RESERVE_OPFILTER                   0x00100000
#define FILE_OPEN_REPARSE_POINT                 0x00200000
#define FILE_OPEN_NO_RECALL                     0x00400000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY          0x00800000


#define FILE_VALID_OPTION_FLAGS                 0x00ffffff
#define FILE_VALID_PIPE_OPTION_FLAGS            0x00000032
#define FILE_VALID_MAILSLOT_OPTION_FLAGS        0x00000032
#define FILE_VALID_SET_FLAGS                    0x00000036

//
// Define the I/O status information return values for NtCreateFile/NtOpenFile
//

#define FILE_SUPERSEDED                 0x00000000
#define FILE_OPENED                     0x00000001
#define FILE_CREATED                    0x00000002
#define FILE_OVERWRITTEN                0x00000003
#define FILE_EXISTS                     0x00000004
#define FILE_DOES_NOT_EXIST             0x00000005

//
// Define special ByteOffset parameters for read and write operations
//

#define FILE_WRITE_TO_END_OF_FILE       0xffffffff
#define FILE_USE_FILE_POINTER_POSITION  0xfffffffe

//
// Define alignment requirement values
//

#define FILE_BYTE_ALIGNMENT             0x00000000
#define FILE_WORD_ALIGNMENT             0x00000001
#define FILE_LONG_ALIGNMENT             0x00000003
#define FILE_QUAD_ALIGNMENT             0x00000007
#define FILE_OCTA_ALIGNMENT             0x0000000f
#define FILE_32_BYTE_ALIGNMENT          0x0000001f
#define FILE_64_BYTE_ALIGNMENT          0x0000003f
#define FILE_128_BYTE_ALIGNMENT         0x0000007f
#define FILE_256_BYTE_ALIGNMENT         0x000000ff
#define FILE_512_BYTE_ALIGNMENT         0x000001ff

//
// Define the maximum Length of a filename string
//

#define MAXIMUM_FILENAME_LENGTH         256

//
// Define the various device characteristics flags
//

#define FILE_REMOVABLE_MEDIA                    0x00000001
#define FILE_READ_ONLY_DEVICE                   0x00000002
#define FILE_FLOPPY_DISKETTE                    0x00000004
#define FILE_WRITE_ONCE_MEDIA                   0x00000008
#define FILE_REMOTE_DEVICE                      0x00000010
#define FILE_DEVICE_IS_MOUNTED                  0x00000020
#define FILE_VIRTUAL_VOLUME                     0x00000040
#define FILE_AUTOGENERATED_DEVICE_NAME          0x00000080
#define FILE_DEVICE_SECURE_OPEN                 0x00000100
#define FILE_CHARACTERISTIC_PNP_DEVICE          0x00000800
#define FILE_CHARACTERISTIC_TS_DEVICE           0x00001000
#define FILE_CHARACTERISTIC_WEBDAV_DEVICE       0x00002000

typedef struct _FILE_BASIC_INFORMATION
{
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    ULONG           FileAttributes;
    ULONG           Dummy;

} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

typedef struct _FILE_NAME_INFORMATION
{
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION
{
    LARGE_INTEGER CurrentByteOffset;

} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION
{
    LARGE_INTEGER EndOfFile;

} FILE_END_OF_FILE_INFORMATION, *PFILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION
{
    BOOLEAN  DeleteFile;

} FILE_DISPOSITION_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION
{
    LARGE_INTEGER   AllocationSize;
    LARGE_INTEGER   EndOfFile;
    ULONG           NumberOfLinks;
    BOOLEAN         DeletePending;
    BOOLEAN         Directory;

} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

typedef struct
{
    union
    {
        NTSTATUS    Status;
        PVOID       Pointer;
    };
    ULONG_PTR Information;

} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef
VOID
(NTAPI
*PIO_APC_ROUTINE)(
    IN PVOID            ApcContext,
    IN PIO_STATUS_BLOCK IoStatusBlock,
    IN ULONG            Reserved
);

/************************************************************************/
/* memory                                                               */
/************************************************************************/

typedef enum _POOL_TYPE
{
    NonPagedPool,
    PagedPool,
    NonPagedPoolMustSucceed,
    DontUseThisType,
    NonPagedPoolCacheAligned,
    PagedPoolCacheAligned,
    NonPagedPoolCacheAlignedMustS,
    MaxPoolType,

    //
    // Note these per session types are carefully chosen so that the appropriate
    // masking still applies as well as MaxPoolType above.
    //

    NonPagedPoolSession                     = 32,
    PagedPoolSession                        = NonPagedPoolSession + 1,
    NonPagedPoolMustSucceedSession          = PagedPoolSession + 1,
    DontUseThisTypeSession                  = NonPagedPoolMustSucceedSession + 1,
    NonPagedPoolCacheAlignedSession         = DontUseThisTypeSession + 1,
    PagedPoolCacheAlignedSession            = NonPagedPoolCacheAlignedSession + 1,
    NonPagedPoolCacheAlignedMustSSession    = PagedPoolCacheAlignedSession + 1,
} POOL_TYPE;

typedef
NTSTATUS
(NTAPI
*PRTL_HEAP_COMMIT_ROUTINE)(
    IN     PVOID    Base,
    IN OUT PVOID   *CommitAddress,
    IN OUT PSIZE_T  CommitSize
);

typedef struct _RTL_HEAP_PARAMETERS
{
    ULONG                       Length;
    SIZE_T                      SegmentReserve;
    SIZE_T                      SegmentCommit;
    SIZE_T                      DeCommitFreeBlockThreshold;
    SIZE_T                      DeCommitTotalFreeThreshold;
    SIZE_T                      MaximumAllocationSize;
    SIZE_T                      VirtualMemoryThreshold;
    SIZE_T                      InitialCommit;
    SIZE_T                      InitialReserve;
    PRTL_HEAP_COMMIT_ROUTINE    CommitRoutine;
    SIZE_T                      Reserved[2];

} RTL_HEAP_PARAMETERS, *PRTL_HEAP_PARAMETERS;

/************************************************************************/
/* handles                                                              */
/************************************************************************/

enum { RTL_HANDLE_VALID = 0x000000001 };

typedef struct _RTL_HANDLE_TABLE_ENTRY
{
    union
    {
        ULONG                           Flags;
        struct _RTL_HANDLE_TABLE_ENTRY *NextFree;
    };
} RTL_HANDLE_TABLE_ENTRY, *PRTL_HANDLE_TABLE_ENTRY;

typedef struct _RTL_HANDLE_TABLE
{
    ULONG                   MaximumNumberOfHandles;
    ULONG                   SizeOfHandleTableEntry;
    ULONG                   Reserved[2];
    PRTL_HANDLE_TABLE_ENTRY FreeHandles;
    PRTL_HANDLE_TABLE_ENTRY CommittedHandles;
    PRTL_HANDLE_TABLE_ENTRY UnCommittedHandles;
    PRTL_HANDLE_TABLE_ENTRY MaxReservedHandles;
} RTL_HANDLE_TABLE, *PRTL_HANDLE_TABLE;


//
// Event type
//

typedef enum _EVENT_TYPE
{
    NotificationEvent,
    SynchronizationEvent

} EVENT_TYPE;

_ML_C_TAIL_

#pragma pack(pop)

#endif // _NATIVETYPES_USER_H_30d04cf8_0b66_4f36_a472_f9c1734240b8

#ifndef _NATIVETYPES_COMMON_H_e6f6abc9_91e7_452a_b418_671874fe30b3
#define _NATIVETYPES_COMMON_H_e6f6abc9_91e7_452a_b418_671874fe30b3

#pragma warning(disable:4103)



//#pragma warning(disable:4201)

_ML_C_HEAD_

#if !defined(FAIL_RETURN)
    #define FAIL_RETURN(Status) { NTSTATUS __Status__;  __Status__ = (Status); if (!NT_SUCCESS(__Status__)) return __Status__; }
    #define FAIL_CONTINUE(Status) if (!NT_SUCCESS(Status)) continue
    #define FAIL_BREAK(Status) if (!NT_SUCCESS(Status)) break
    #define SUCCESS_RETURN(Status) { NTSTATUS __Status__;  __Status__ = (Status); if (NT_SUCCESS(__Status__)) return __Status__; }
#endif  // FAIL_RETURN

#ifndef NT_FAILED
    #define NT_FAILED !NT_SUCCESS
#endif // NT_FAILED


#ifndef FACILITY_NTWIN32
    #define FACILITY_NTWIN32                 0x7
#endif // FACILITY_NTWIN32

#define DEFINE_NTSTATUS(Severity, Code) \
            (((ULONG)(Severity) << 30) | (1 << 29) | (FACILITY_NTWIN32 << 16) | (USHORT)(Code))




/************************************************************************
  base
************************************************************************/

typedef struct _CLIENT_ID64
{
    ULONG64  UniqueProcess;
    ULONG64  UniqueThread;

} CLIENT_ID64, *PCLIENT_ID64;

/************************************************************************
  strings
************************************************************************/

typedef struct _RTL_BUFFER
{
/* 0x000 */ PUCHAR                         Buffer;
/* 0x004 */ PUCHAR                         StaticBuffer;
/* 0x008 */ ULONG                          Size;
/* 0x00C */ ULONG                          StaticSize;
/* 0x010 */ ULONG                          ReservedForAllocatedSize;
/* 0x014 */ PVOID                          ReservedForIMalloc;

} RTL_BUFFER, *PRTL_BUFFER;

typedef struct _RTL_UNICODE_STRING_BUFFER
{
/* 0x000 */ UNICODE_STRING                 String;
/* 0x008 */ RTL_BUFFER                     ByteBuffer;
/* 0x020 */ UCHAR                          MinimumStaticBufferForTerminalNul[2];

} RTL_UNICODE_STRING_BUFFER, *PRTL_UNICODE_STRING_BUFFER;

//
// These must be converted to LUIDs before use.
//


enum
{
    SE_MIN_WELL_KNOWN_PRIVILEGE         = 2L,

    SE_CREATE_TOKEN_PRIVILEGE           = 2L,
    SE_ASSIGNPRIMARYTOKEN_PRIVILEGE     = 3L,
    SE_LOCK_MEMORY_PRIVILEGE            = 4L,
    SE_INCREASE_QUOTA_PRIVILEGE         = 5L,

    SE_MACHINE_ACCOUNT_PRIVILEGE        = 6L,
    SE_TCB_PRIVILEGE                    = 7L,
    SE_SECURITY_PRIVILEGE               = 8L,
    SE_TAKE_OWNERSHIP_PRIVILEGE         = 9L,
    SE_LOAD_DRIVER_PRIVILEGE            = 10L,
    SE_SYSTEM_PROFILE_PRIVILEGE         = 11L,
    SE_SYSTEMTIME_PRIVILEGE             = 12L,
    SE_PROF_SINGLE_PROCESS_PRIVILEGE    = 13L,
    SE_INC_BASE_PRIORITY_PRIVILEGE      = 14L,
    SE_CREATE_PAGEFILE_PRIVILEGE        = 15L,
    SE_CREATE_PERMANENT_PRIVILEGE       = 16L,
    SE_BACKUP_PRIVILEGE                 = 17L,
    SE_RESTORE_PRIVILEGE                = 18L,
    SE_SHUTDOWN_PRIVILEGE               = 19L,
    SE_DEBUG_PRIVILEGE                  = 20L,
    SE_AUDIT_PRIVILEGE                  = 21L,
    SE_SYSTEM_ENVIRONMENT_PRIVILEGE     = 22L,
    SE_CHANGE_NOTIFY_PRIVILEGE          = 23L,
    SE_REMOTE_SHUTDOWN_PRIVILEGE        = 24L,
    SE_UNDOCK_PRIVILEGE                 = 25L,
    SE_SYNC_AGENT_PRIVILEGE             = 26L,
    SE_ENABLE_DELEGATION_PRIVILEGE      = 27L,
    SE_MANAGE_VOLUME_PRIVILEGE          = 28L,
    SE_IMPERSONATE_PRIVILEGE            = 29L,
    SE_CREATE_GLOBAL_PRIVILEGE          = 30L,
    SE_TRUSTED_CREDMAN_ACCESS_PRIVILEGE = 31L,
    SE_RELABEL_PRIVILEGE                = 32L,
    SE_INC_WORKING_SET_PRIVILEGE        = 33L,
    SE_TIME_ZONE_PRIVILEGE              = 34L,
    SE_CREATE_SYMBOLIC_LINK_PRIVILEGE   = 35L,

    SE_MAX_WELL_KNOWN_PRIVILEGE
};



/************************************************************************
  afd
************************************************************************/


typedef struct _MDL
{
    struct _MDL *Next;
    SHORT Size;
    SHORT MdlFlags;

    struct _EPROCESS *Process;
    PVOID MappedSystemVa;   /* see creators for field size annotations. */
    PVOID StartVa;   /* see creators for validity; could be address 0.  */
    ULONG ByteCount;
    ULONG ByteOffset;

} MDL, *PMDL;


typedef struct _AFD_MAPBUF
{
     PVOID BufferAddress;
     PMDL  Mdl;

} AFD_MAPBUF, *PAFD_MAPBUF;

typedef struct _AFD_WSABUF
{
     ULONG  Length;
     PCHAR  Buffer;

} AFD_WSABUF, *PAFD_WSABUF;

typedef struct  _AFD_RECV_INFO
{
     PAFD_WSABUF    BufferArray;
     ULONG          BufferCount;
     ULONG          AfdFlags;
     ULONG          TdiFlags;

} AFD_RECV_INFO , *PAFD_RECV_INFO;

typedef struct _AFD_RECV_INFO_UDP
{
    PAFD_WSABUF     BufferArray;
    ULONG           BufferCount;
    ULONG           AfdFlags;
    ULONG           TdiFlags;
    PVOID           Address;
    PLONG           AddressLength;

} AFD_RECV_INFO_UDP, *PAFD_RECV_INFO_UDP;

typedef struct  _AFD_SEND_INFO
{
    PAFD_WSABUF BufferArray;
    ULONG       BufferCount;
    ULONG       AfdFlags;
    ULONG       TdiFlags;

} AFD_SEND_INFO , *PAFD_SEND_INFO;

typedef struct _AFD_SEND_INFO_UDP
{
    PAFD_WSABUF BufferArray;
    ULONG       BufferCount;
    ULONG       AfdFlags;
    ULONG       Padding[9];
    ULONG       SizeOfRemoteAddress;
    PVOID       RemoteAddress;

} AFD_SEND_INFO_UDP, *PAFD_SEND_INFO_UDP;

/* IOCTL Generation */
#define FSCTL_AFD_BASE                      FILE_DEVICE_NETWORK
#define _AFD_CONTROL_CODE(Operation, Method) ((FSCTL_AFD_BASE)<<12 | (Operation<<2) | Method)

/* AFD Commands */
#define AFD_BIND                        0
#define AFD_CONNECT                     1
#define AFD_START_LISTEN                2
#define AFD_WAIT_FOR_LISTEN             3
#define AFD_ACCEPT                      4
#define AFD_RECV                        5
#define AFD_RECV_DATAGRAM               6
#define AFD_SEND                        7
#define AFD_SEND_DATAGRAM               8
#define AFD_SELECT                      9
#define AFD_DISCONNECT                  10
#define AFD_GET_SOCK_NAME               11
#define AFD_GET_PEER_NAME               12
#define AFD_GET_TDI_HANDLES             13
#define AFD_SET_INFO                    14
#define AFD_GET_CONTEXT_SIZE            15
#define AFD_GET_CONTEXT                 16
#define AFD_SET_CONTEXT                 17
#define AFD_SET_CONNECT_DATA            18
#define AFD_SET_CONNECT_OPTIONS         19
#define AFD_SET_DISCONNECT_DATA         20
#define AFD_SET_DISCONNECT_OPTIONS      21
#define AFD_GET_CONNECT_DATA            22
#define AFD_GET_CONNECT_OPTIONS         23
#define AFD_GET_DISCONNECT_DATA         24
#define AFD_GET_DISCONNECT_OPTIONS      25
#define AFD_SET_CONNECT_DATA_SIZE       26
#define AFD_SET_CONNECT_OPTIONS_SIZE    27
#define AFD_SET_DISCONNECT_DATA_SIZE    28
#define AFD_SET_DISCONNECT_OPTIONS_SIZE 29
#define AFD_GET_INFO                    30
#define AFD_EVENT_SELECT                33
#define AFD_ENUM_NETWORK_EVENTS         34
#define AFD_DEFER_ACCEPT                35
#define AFD_GET_PENDING_CONNECT_DATA    41
#define AFD_VALIDATE_GROUP              42

/* AFD IOCTLs */

#define IOCTL_AFD_BIND                          _AFD_CONTROL_CODE(AFD_BIND,                         METHOD_NEITHER)     //  0x12003
#define IOCTL_AFD_CONNECT                       _AFD_CONTROL_CODE(AFD_CONNECT,                      METHOD_NEITHER)     //  0x12007
#define IOCTL_AFD_START_LISTEN                  _AFD_CONTROL_CODE(AFD_START_LISTEN,                 METHOD_NEITHER)     //  0x1200B
#define IOCTL_AFD_WAIT_FOR_LISTEN               _AFD_CONTROL_CODE(AFD_WAIT_FOR_LISTEN,              METHOD_BUFFERED)    //  0x1200C
#define IOCTL_AFD_ACCEPT                        _AFD_CONTROL_CODE(AFD_ACCEPT,                       METHOD_BUFFERED)    //  0x12010
#define IOCTL_AFD_RECV                          _AFD_CONTROL_CODE(AFD_RECV,                         METHOD_NEITHER)     //  0x12017
#define IOCTL_AFD_RECV_DATAGRAM                 _AFD_CONTROL_CODE(AFD_RECV_DATAGRAM,                METHOD_NEITHER)     //  0x1201B
#define IOCTL_AFD_SEND                          _AFD_CONTROL_CODE(AFD_SEND,                         METHOD_NEITHER)     //  0x1201F
#define IOCTL_AFD_SEND_DATAGRAM                 _AFD_CONTROL_CODE(AFD_SEND_DATAGRAM,                METHOD_NEITHER)     //  0x12023
#define IOCTL_AFD_SELECT                        _AFD_CONTROL_CODE(AFD_SELECT,                       METHOD_BUFFERED)    //  0x12024
#define IOCTL_AFD_DISCONNECT                    _AFD_CONTROL_CODE(AFD_DISCONNECT,                   METHOD_NEITHER)     //  0x1202B
#define IOCTL_AFD_GET_SOCK_NAME                 _AFD_CONTROL_CODE(AFD_GET_SOCK_NAME,                METHOD_NEITHER)     //  0x1202F
#define IOCTL_AFD_GET_PEER_NAME                 _AFD_CONTROL_CODE(AFD_GET_PEER_NAME,                METHOD_NEITHER)     //  0x12033
#define IOCTL_AFD_GET_TDI_HANDLES               _AFD_CONTROL_CODE(AFD_GET_TDI_HANDLES,              METHOD_NEITHER)     //  0x12037
#define IOCTL_AFD_SET_INFO                      _AFD_CONTROL_CODE(AFD_SET_INFO,                     METHOD_NEITHER)     //  0x1203B
#define IOCTL_AFD_GET_CONTEXT_SIZE              _AFD_CONTROL_CODE(AFD_GET_CONTEXT_SIZE,             METHOD_NEITHER)     //  0x1203F
#define IOCTL_AFD_GET_CONTEXT                   _AFD_CONTROL_CODE(AFD_GET_CONTEXT,                  METHOD_NEITHER)     //  0x12043
#define IOCTL_AFD_SET_CONTEXT                   _AFD_CONTROL_CODE(AFD_SET_CONTEXT,                  METHOD_NEITHER)     //  0x12047
#define IOCTL_AFD_SET_CONNECT_DATA              _AFD_CONTROL_CODE(AFD_SET_CONNECT_DATA,             METHOD_NEITHER)     //  0x1204B
#define IOCTL_AFD_SET_CONNECT_OPTIONS           _AFD_CONTROL_CODE(AFD_SET_CONNECT_OPTIONS,          METHOD_NEITHER)     //  0x1204F
#define IOCTL_AFD_SET_DISCONNECT_DATA           _AFD_CONTROL_CODE(AFD_SET_DISCONNECT_DATA,          METHOD_NEITHER)     //  0x12053
#define IOCTL_AFD_SET_DISCONNECT_OPTIONS        _AFD_CONTROL_CODE(AFD_SET_DISCONNECT_OPTIONS,       METHOD_NEITHER)     //  0x12057
#define IOCTL_AFD_GET_CONNECT_DATA              _AFD_CONTROL_CODE(AFD_GET_CONNECT_DATA,             METHOD_NEITHER)     //  0x1205B
#define IOCTL_AFD_GET_CONNECT_OPTIONS           _AFD_CONTROL_CODE(AFD_GET_CONNECT_OPTIONS,          METHOD_NEITHER)     //  0x1205F
#define IOCTL_AFD_GET_DISCONNECT_DATA           _AFD_CONTROL_CODE(AFD_GET_DISCONNECT_DATA,          METHOD_NEITHER)     //  0x12063
#define IOCTL_AFD_GET_DISCONNECT_OPTIONS        _AFD_CONTROL_CODE(AFD_GET_DISCONNECT_OPTIONS,       METHOD_NEITHER)     //  0x12067
#define IOCTL_AFD_SET_CONNECT_DATA_SIZE         _AFD_CONTROL_CODE(AFD_SET_CONNECT_DATA_SIZE,        METHOD_NEITHER)     //  0x1206B
#define IOCTL_AFD_SET_CONNECT_OPTIONS_SIZE      _AFD_CONTROL_CODE(AFD_SET_CONNECT_OPTIONS_SIZE,     METHOD_NEITHER)     //  0x1206F
#define IOCTL_AFD_SET_DISCONNECT_DATA_SIZE      _AFD_CONTROL_CODE(AFD_SET_DISCONNECT_DATA_SIZE,     METHOD_NEITHER)     //  0x12073
#define IOCTL_AFD_SET_DISCONNECT_OPTIONS_SIZE   _AFD_CONTROL_CODE(AFD_SET_DISCONNECT_OPTIONS_SIZE,  METHOD_NEITHER)     //  0x12077
#define IOCTL_AFD_GET_INFO                      _AFD_CONTROL_CODE(AFD_GET_INFO,                     METHOD_NEITHER)     //  0x1207B
#define IOCTL_AFD_EVENT_SELECT                  _AFD_CONTROL_CODE(AFD_EVENT_SELECT,                 METHOD_NEITHER)     //  0x12087
#define IOCTL_AFD_DEFER_ACCEPT                  _AFD_CONTROL_CODE(AFD_DEFER_ACCEPT,                 METHOD_NEITHER)     //  0x1208F
#define IOCTL_AFD_GET_PENDING_CONNECT_DATA      _AFD_CONTROL_CODE(AFD_GET_PENDING_CONNECT_DATA,     METHOD_NEITHER)     //  0x120A7
#define IOCTL_AFD_ENUM_NETWORK_EVENTS           _AFD_CONTROL_CODE(AFD_ENUM_NETWORK_EVENTS,          METHOD_NEITHER)     //  0x1208B
#define IOCTL_AFD_VALIDATE_GROUP                _AFD_CONTROL_CODE(AFD_VALIDATE_GROUP,               METHOD_NEITHER)     //  0x120AB

/************************************************************************
  iocp api
************************************************************************/

typedef enum _IO_COMPLETION_INFORMATION_CLASS
{
    IoCompletionBasicInformation,

} IO_COMPLETION_INFORMATION_CLASS;

/************************************************************************
  shim engine
************************************************************************/
typedef enum
{
    SE_PROCESS_ATTACH     = 1,
    SE_PROCESS_DETACH     = 2,
    SE_DLL_LOAD           = 3,
} SHIM_NOTIFY_REASON;

typedef struct
{
    PCSTR   ModuleName;
    PCSTR   FunctionName;
    PVOID   NewRoutine;
    PVOID   Unknown[3];
} SHIM_HOOK_API_INFO, *PSHIM_HOOK_API_INFO;

/************************************************************************
  timer api
************************************************************************/

typedef enum
{
    TimerBasicInformation   = 0,

} TIMER_INFORMATION_CLASS;

typedef struct _TIMER_BASIC_INFORMATION
{
    LARGE_INTEGER   RemainingTime;
    BOOLEAN         TimerState;

} TIMER_BASIC_INFORMATION, *PTIMER_BASIC_INFORMATION;

/************************************************************************
  misc
************************************************************************/

#define DELAY_ONE_MICROSECOND   (-10)
#define DELAY_ONE_MILLISECOND   (DELAY_ONE_MICROSECOND * 1000)
#define DELAY_ONE_SECOND        (DELAY_ONE_MILLISECOND * 1000)
#define DELAY_QUAD_INFINITE     0x8000000000000000ll


typedef enum
{
    NotificationTimer       = 0,
    SynchronizationTimer    = 1,
} TIMER_TYPE;



_ML_C_TAIL_


#pragma warning(default:4103)

//#pragma warning(default:4201)

#endif // _NATIVETYPES_COMMON_H_e6f6abc9_91e7_452a_b418_671874fe30b3

#endif // _NATIVETYPES_H_96405a6d_a23f_4fd6_b20a_e5821226bfc6

#define NATIVE_API  EXTC_IMPORT
#define NTKRNLAPI   EXTC_IMPORT

ForceInline NTSTATUS ML_NTSTATUS_FROM_WIN32(long x) { return x <= 0 ? (NTSTATUS)x : (NTSTATUS) (((x) & 0x0000FFFF) | (FACILITY_NTWIN32 << 16) | ERROR_SEVERITY_ERROR);}

#endif // _NTBASE_H_bc4f0ce4_29e3_43c6_a3a3_98415b2adbab_
