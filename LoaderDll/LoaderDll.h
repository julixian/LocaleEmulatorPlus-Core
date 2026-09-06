#ifndef _LOADERDLL_H_586bc656_348b_4b12_ba74_d39366b67f23_
#define _LOADERDLL_H_586bc656_348b_4b12_ba74_d39366b67f23_

#include <Windows.h>

// such stupid link.exe error
#if !defined(_M_AMD64) && !defined(_M_X64)
extern "C" __declspec(noreturn) void __cdecl __std_terminate() {}
#endif


#include "../LocaleEmulatorPlus/LocaleEmulatorPlus.h"
#include "BrokerProtocol.h"
#define LEP_API EXTC


#if !LEP_LOADER_DLL
#undef LEP_API
#define LEP_API EXTC __declspec(dllimport)
#endif

LEP_API
NTSTATUS
WINAPI
LepCreateProcess(
	PLEPB                    EnvironmentBlock,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine = nullptr,
	PCWSTR                  CurrentDirectory = nullptr,
	ULONG                   CreationFlags = 0,
	LPSTARTUPINFOW          StartupInfo = nullptr,
	PML_PROCESS_INFORMATION ProcessInformation = nullptr,
	LPSECURITY_ATTRIBUTES   ProcessAttributes = nullptr,
	LPSECURITY_ATTRIBUTES   ThreadAttributes = nullptr,
	PVOID                   Environment = nullptr,
	HANDLE                  Token = nullptr
	);

LEP_API
NTSTATUS
WINAPI
LepCreateProcess2(
	PLEPB                    EnvironmentBlock,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine = nullptr,
	PCWSTR                  CurrentDirectory = nullptr,
	ULONG                   CreationFlags = 0,
	LPSTARTUPINFOW          StartupInfo = nullptr,
	PML_PROCESS_INFORMATION ProcessInformation = nullptr,
	LPSECURITY_ATTRIBUTES   ProcessAttributes = nullptr,
	LPSECURITY_ATTRIBUTES   ThreadAttributes = nullptr,
	PVOID                   Environment = nullptr,
	HANDLE                  Token = nullptr
	);

#if LEP_LOADER_DLL
LEP_API
void
CALLBACK
LepBrokerEntry(HWND Window, HINSTANCE Instance, LPSTR CommandLine, int ShowCommand);
#endif

#endif // _LOADERDLL_H_586bc656_348b_4b12_ba74_d39366b67f23_
