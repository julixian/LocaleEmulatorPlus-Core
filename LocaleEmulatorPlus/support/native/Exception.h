#ifndef _NTEXCEPTION_H_a57b7443_d72e_4c1e_a4b5_15c7e4c037dd_
#define _NTEXCEPTION_H_a57b7443_d72e_4c1e_a4b5_15c7e4c037dd_


BOOLEAN
NTAPI
RtlDispatchException(
    PEXCEPTION_RECORD   ExceptionRecord,
    PCONTEXT            Context
);


NATIVE_API
VOID
NTAPI
KiUserExceptionDispatcher(
    PEXCEPTION_RECORD   ExceptionRecord,
    PCONTEXT            Context
);

NATIVE_API
VOID
NTAPI
RtlRaiseException(
    PEXCEPTION_RECORD ExceptionRecord
);

NATIVE_API
VOID
NTAPI
RtlSetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER TopLevelExceptionFilter
);

NATIVE_API
LONG
NTAPI
RtlKnownExceptionFilter(
    PEXCEPTION_POINTERS ExceptionPointers
);

NATIVE_API
PVOID
NTAPI
RtlAddVectoredExceptionHandler(
    IN LONG                         FirstHandler,
    IN PVECTORED_EXCEPTION_HANDLER  Handler
);

NATIVE_API
ULONG
NTAPI
RtlRemoveVectoredExceptionHandler(
    IN PVOID Handler
);

NATIVE_API
PVOID
NTAPI
RtlAddVectoredContinueHandler(
    IN LONG                         FirstHandler,
    IN PVECTORED_EXCEPTION_HANDLER  Handler
);

NATIVE_API
ULONG
NTAPI
RtlRemoveVectoredContinueHandler(
    IN PVOID Handler
);


typedef enum _HARDERROR_RESPONSE_OPTION {
    OptionAbortRetryIgnore      = 0,
    OptionOk                    = 1,
    OptionOkCancel              = 2,
    OptionRetryCancel           = 3,
    OptionYesNo                 = 4,
    OptionYesNoCancel           = 5,
    OptionShutdownSystem        = 6,
    OptionOkNoWait              = 7,
    OptionCancelTryContinue     = 8,

} HARDERROR_RESPONSE_OPTION;

typedef enum _HARDERROR_RESPONSE
{
    ResponseReturnToCaller      = 0,
    ResponseNotHandled          = 1,
    ResponseAbort               = 2,
    ResponseCancel              = 3,
    ResponseIgnore              = 4,
    ResponseNo                  = 5,
    ResponseOk                  = 6,
    ResponseRetry               = 7,
    ResponseYes                 = 8,
    ResponseTryAgain            = 9,
    ResponseContinue            = 10,

} HARDERROR_RESPONSE;

#define HARDERROR_PARAMETERS_FLAGSPOS   4
#define HARDERROR_FLAGS_DEFDESKTOPONLY  0x00020000

#define MAXIMUM_HARDERROR_PARAMETERS    5
#define HARDERROR_OVERRIDE_ERRORMODE    0x10000000

NATIVE_API
NTSTATUS
NTAPI
NtRaiseHardError(
    IN  NTSTATUS    ErrorStatus,
    IN  ULONG       NumberOfParameters,
    IN  ULONG       UnicodeStringParameterMask,
    IN  PULONG_PTR  Parameters,
    IN  ULONG       ValidResponseOptions,
    OUT PULONG      Response
);

NATIVE_API
NTSTATUS
NTAPI
ZwRaiseHardError(
    IN  NTSTATUS    ErrorStatus,
    IN  ULONG       NumberOfParameters,
    IN  ULONG       UnicodeStringParameterMask,
    IN  PULONG_PTR  Parameters,
    IN  ULONG       ValidResponseOptions,
    OUT PULONG      Response
);

NATIVE_API
NTSTATUS
NTAPI
NtRaiseException(
    IN  PEXCEPTION_RECORD   ExceptionRecord,
    IN  PCONTEXT            ContextRecord,
    IN  BOOLEAN             FirstChance
);

NATIVE_API
NTSTATUS
NTAPI
ZwRaiseException(
    IN  PEXCEPTION_RECORD   ExceptionRecord,
    IN  PCONTEXT            ContextRecord,
    IN  BOOLEAN             FirstChance
);

NATIVE_API
NTSTATUS
NTAPI
ExRaiseHardError(
    IN  NTSTATUS    ErrorStatus,
    IN  ULONG       NumberOfParameters,
    IN  ULONG       UnicodeStringParameterMask,
    IN  PULONG_PTR  Parameters,
    IN  ULONG       ValidResponseOptions,
    OUT PULONG      Response
);

#endif // _NTEXCEPTION_H_a57b7443_d72e_4c1e_a4b5_15c7e4c037dd_
