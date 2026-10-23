#pragma once
#include "../native/Helpers.h"

#ifndef LEP_SUPPORT_DIAGNOSTICS_H
#define LEP_SUPPORT_DIAGNOSTICS_H


#if !defined(FILE_DEVICE_CONSOLE)
    #define FILE_DEVICE_CONSOLE             0x00000050
#endif

_ML_C_HEAD_

NTSTATUS    CreateMiniDump(PEXCEPTION_POINTERS ExceptionPointers);
CHAR        ConsoleReadChar();
VOID        ClearConsoleScreen();
VOID        PauseConsole(PCWSTR DEF_VAL(PauseText, nullptr));
ULONG_PTR   PrintConsoleA(PCSTR Format, ...);
ULONG_PTR   PrintConsoleW(PCWSTR Format, ...);
ULONG_PTR   PrintConsole(PCWSTR Format, ...);

#if ENABLE_LOG
BOOL LepGetModuleLogDirectory(PWSTR Path, ULONG Capacity);
#endif

_ML_C_TAIL_

#endif // LEP_SUPPORT_DIAGNOSTICS_H
