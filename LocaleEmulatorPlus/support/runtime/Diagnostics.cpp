#include "../Support.h"
_ML_C_HEAD_


#pragma comment(lib, "dbghelp.lib")
#include <DbgHelp.h>

static PWSTR AppendHex64NoLeading(PWSTR Buffer, ULONG64 Value)
{
    static const WCHAR Hex[] = L"0123456789ABCDEF";
    BOOLEAN Started = FALSE;

    for (LONG_PTR Shift = 60; Shift >= 0; Shift -= 4)
    {
        ULONG Digit = (ULONG)((Value >> Shift) & 0xF);

        if (Digit != 0 || Started || Shift == 0)
        {
            *Buffer++ = Hex[Digit];
            Started = TRUE;
        }
    }

    return Buffer;
}

NTSTATUS CreateMiniDump(PEXCEPTION_POINTERS ExceptionPointers)
{
    WCHAR           MiniDumpFile[MAX_NTPATH];
    PWSTR           MiniDumpExt;
    NativeFile      File;
    BOOL            Success;
    NTSTATUS        Status;
    PROCESS_TIMES   Times;

    MINIDUMP_EXCEPTION_INFORMATION ExceptionInformation;

    NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &Times, sizeof(Times), NULL);

    Nt_GetModuleFileName(NULL, MiniDumpFile, countof(MiniDumpFile));
    MiniDumpExt = findextw(MiniDumpFile);
    *MiniDumpExt++ = L'.';
    MiniDumpExt = AppendHex64NoLeading(MiniDumpExt, (ULONG64)Times.CreationTime.QuadPart);
    CopyMemory(MiniDumpExt, L".crash.dmp", sizeof(L".crash.dmp"));

    Status = File.Create(MiniDumpFile);
    FAIL_RETURN(Status);

    ExceptionInformation.ClientPointers     = FALSE;
    ExceptionInformation.ExceptionPointers  = ExceptionPointers;
    ExceptionInformation.ThreadId           = Ps::CurrentTid();

    Success = MiniDumpWriteDump(
                NtCurrentProcess(),
                (ULONG_PTR)Ps::CurrentTeb()->ClientId.UniqueProcess,
                File,
                MiniDumpNormal,
                &ExceptionInformation,
                NULL,
                NULL
              );

    return Success ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}


_ML_C_TAIL_

_ML_C_HEAD_


ULONG_PTR PrintConsoleA(PCSTR Format, ...)
{
    BOOL        Result;
    ULONG       Length;
    CHAR        Buffer[0xF00 / 2];
    va_list     Args;
    HANDLE      StdHandle;

    va_start(Args, Format);
    Length = _vsnprintf(Buffer, countof(Buffer) - 1, Format, Args);
    va_end(Args);
    if (Length == -1)
        return Length;

    StdHandle = Nt_CurrentPeb()->ProcessParameters->StandardOutput;

    if (PtrAnd(StdHandle, 0x10000003) == (HANDLE)3)
    {
        Result = WriteConsoleA(
                    StdHandle,
                    Buffer,
                    Length,
                    &Length,
                    NULL
                 );
    }
    else
    {
        LARGE_INTEGER BytesWritten;

        Result = NT_SUCCESS(NativeFile::Write(StdHandle, Buffer, Length, &BytesWritten));
        Length = Result ? (ULONG_PTR)BytesWritten.QuadPart : 0;
    }

    return Result ? Length : 0;
}

ULONG_PTR PrintConsoleW(PCWSTR Format, ...)
{
    return PrintConsole(Format);
}

ULONG_PTR PrintConsole(PCWSTR Format, ...)
{
    BOOL        Success, IsConsole;
    ULONG       Length;
    WCHAR       Buffer[0xF00 / 2];
    va_list     Args;
    HANDLE      StdOutput;

    va_start(Args, Format);
    Length = _vsnwprintf(Buffer, countof(Buffer) - 1, Format, Args);
    va_end(Args);
    if (Length == -1)
        return Length;

    StdOutput = CurrentPeb()->ProcessParameters->StandardOutput;

    IsConsole = TRUE;

    LOOP_ONCE
    {
        IO_STATUS_BLOCK             IoStatus;
        FILE_FS_DEVICE_INFORMATION  VolumeInfo;

        if (PtrAnd(StdOutput, 0x10000003) == (HANDLE)3)
            break;

        IoStatus.Status = NtQueryVolumeInformationFile(
                            StdOutput,
                            &IoStatus,
                            &VolumeInfo,
                            sizeof(VolumeInfo),
                            FileFsDeviceInformation
                        );
        if (NT_FAILED(IoStatus.Status))
            break;

        IsConsole = VolumeInfo.DeviceType == FILE_DEVICE_CONSOLE;
    }

    // if (PtrAnd(StdHandle, (1 << (bitsof(ULONG_PTR) - 4)) | 3) == (HANDLE)3)
    if (IsConsole)
    {
        Success = WriteConsoleW(
                    StdOutput,
                    Buffer,
                    Length,
                    &Length,
                    NULL
                 );
    }
    else
    {
        LARGE_INTEGER BytesWritten;

        Success = NT_SUCCESS(NativeFile::Write(StdOutput, Buffer, Length * sizeof(WCHAR), &BytesWritten));
        Length = Success ? ((ULONG_PTR)BytesWritten.QuadPart / sizeof(WCHAR)) : 0;
    }

    return Success ? Length : 0;
}

CHAR ConsoleReadChar()
{
    HANDLE hConsole;
    INPUT_RECORD ir;
    DWORD dwRead;

    hConsole = CreateFileW(
                    L"CONIN$",
                    GENERIC_READ|GENERIC_WRITE,
                    FILE_SHARE_READ|FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL
                );
    LOOP_FOREVER
    {
        ReadConsoleInputW(hConsole, &ir, 1, &dwRead);
        if (dwRead == 0)
            break;

        if (ir.EventType == KEY_EVENT &&
            ir.Event.KeyEvent.bKeyDown == TRUE &&
            ir.Event.KeyEvent.uChar.UnicodeChar)
        {
            break;
        }
    }

    CloseHandle(hConsole);

    return ir.Event.KeyEvent.uChar.AsciiChar;
}

VOID ClearConsoleScreen()
{
    ULONG                       BytesWritten;
    COORD                       Position;
    HANDLE                      StdOutputHandle;
    CONSOLE_SCREEN_BUFFER_INFO  ScreenBufferInfo;

    StdOutputHandle = Nt_CurrentPeb()->ProcessParameters->StandardOutput;

    GetConsoleScreenBufferInfo(StdOutputHandle, &ScreenBufferInfo);

    Position.X = 0;
    Position.Y = 0;
    FillConsoleOutputCharacterW(StdOutputHandle, ' ', ScreenBufferInfo.dwSize.X * ScreenBufferInfo.dwSize.Y, Position, &BytesWritten);

    SetConsoleCursorPosition(StdOutputHandle, Position);
}

VOID PauseConsole(PCWSTR PauseText)
{
    if (PauseText != NULL)
        PrintConsole(L"%s\n", PauseText);
    ConsoleReadChar();
}


_ML_C_TAIL_

#if CPP_DEFINED
ML_NAMESPACE
ML_NAMESPACE_BEGIN(Native)
ML_NAMESPACE_BEGIN(Exp);

NTSTATUS
ExceptionBox(
    PCWSTR      Text,
    PCWSTR      Title,
    PULONG_PTR  Response,
    ULONG_PTR   Type
)
{
    UNICODE_STRING  HardErrorTitle, HardErrorText;
    ULONG_PTR       HardErrorParameters[3];
    ULONG           LocalResponse;
    NTSTATUS        Status;

    RtlInitUnicodeString(&HardErrorTitle, Title);
    RtlInitUnicodeString(&HardErrorText, Text);

    HardErrorParameters[0] = (ULONG_PTR)&HardErrorText;
    HardErrorParameters[1] = (ULONG_PTR)&HardErrorTitle;
    HardErrorParameters[2] = Type;


    Status = NtRaiseHardError(STATUS_SERVICE_NOTIFICATION, countof(HardErrorParameters), 1 | 2, HardErrorParameters, 0, &LocalResponse);


    if (NT_SUCCESS(Status) && Response != NULL)
        *Response = LocalResponse;

    return Status;
}

ML_NAMESPACE_END_(Exp);

ML_NAMESPACE_END_(Native)
ML_NAMESPACE_END
#endif

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

#endif
