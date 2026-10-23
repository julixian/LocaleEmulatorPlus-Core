#ifndef _NTTOKEN_H_f4dd851c_2e83_4d40_9dbf_e2adf1753014_
#define _NTTOKEN_H_f4dd851c_2e83_4d40_9dbf_e2adf1753014_



NATIVE_API
NTSTATUS
NTAPI
RtlAdjustPrivilege(
    ULONG       Privilege,
    BOOLEAN     Enable,
    BOOLEAN     CurrentThread,
    PBOOLEAN    Enabled
);

NATIVE_API
BOOLEAN
NTAPI
RtlEqualSid (
    IN PSID Sid1,
    IN PSID Sid2
);


NATIVE_API
NTSTATUS
NTAPI
NtCreateToken(
    OUT PHANDLE                 TokenHandle,
    IN  ACCESS_MASK             DesiredAccess,
    IN  POBJECT_ATTRIBUTES      ObjectAttributes OPTIONAL,
    IN  TOKEN_TYPE              TokenType,
    IN  PLUID                   AuthenticationId,
    IN  PLARGE_INTEGER          ExpirationTime,
    IN  PTOKEN_USER             User,
    IN  PTOKEN_GROUPS           Groups,
    IN  PTOKEN_PRIVILEGES       Privileges,
    IN  PTOKEN_OWNER            Owner OPTIONAL,
    IN  PTOKEN_PRIMARY_GROUP    PrimaryGroup,
    IN  PTOKEN_DEFAULT_DACL     DefaultDacl OPTIONAL,
    IN  PTOKEN_SOURCE           TokenSource
);

NATIVE_API
NTSTATUS
NTAPI
NtOpenProcessToken(
    IN  HANDLE  ProcessHandle,
    IN  ULONG   DesiredAccess,
    OUT PHANDLE TokenHandle
);

NATIVE_API
NTSTATUS
NTAPI
ZwOpenProcessToken(
    IN  HANDLE  ProcessHandle,
    IN  ULONG   DesiredAccess,
    OUT PHANDLE TokenHandle
);

NATIVE_API
NTSTATUS
NTAPI
ZwOpenProcessToken(
    IN  HANDLE  ProcessHandle,
    IN  ULONG   DesiredAccess,
    OUT PHANDLE TokenHandle
);

NATIVE_API
NTSTATUS
NTAPI
NtOpenThreadToken(
    IN  HANDLE      ThreadHandle,
    IN  ACCESS_MASK DesiredAccess,
    IN  BOOLEAN     OpenAsSelf,
    OUT PHANDLE     TokenHandle
);

NATIVE_API
NTSTATUS
NTAPI
NtDuplicateToken(
    IN  HANDLE              ExistingTokenHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes,
    IN  BOOLEAN             EffectiveOnly,
    IN  TOKEN_TYPE          TokenType,
    OUT PHANDLE             NewTokenHandle
);

NATIVE_API
NTSTATUS
NTAPI
NtQueryInformationToken(
    IN  HANDLE                  TokenHandle,
    IN  TOKEN_INFORMATION_CLASS TokenInformationClass,
    IN  PVOID                   TokenInformation,
    IN  ULONG                   TokenInformationLength,
    OUT PULONG                  ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
NtSetInformationToken(
    IN  HANDLE TokenHandle,
    IN  TOKEN_INFORMATION_CLASS TokenInformationClass,
    IN  PVOID TokenInformation,
    IN  ULONG TokenInformationLength
);

NATIVE_API
NTSTATUS
NTAPI
NtAdjustPrivilegesToken(
    IN  HANDLE              TokenHandle,
    IN  BOOLEAN             DisableAllPrivileges,
    IN  PTOKEN_PRIVILEGES   NewState OPTIONAL,
    IN  ULONG               BufferLength,
    OUT PTOKEN_PRIVILEGES   PreviousState,
    OUT PULONG              ReturnLength
);

NATIVE_API
NTSTATUS
NTAPI
ZwAdjustPrivilegesToken(
    IN  HANDLE              TokenHandle,
    IN  BOOLEAN             DisableAllPrivileges,
    IN  PTOKEN_PRIVILEGES   NewState OPTIONAL,
    IN  ULONG               BufferLength,
    OUT PTOKEN_PRIVILEGES   PreviousState,
    OUT PULONG              ReturnLength
);

#endif // _NTTOKEN_H_f4dd851c_2e83_4d40_9dbf_e2adf1753014_
