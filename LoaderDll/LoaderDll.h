#ifndef _LOADERDLL_H_586bc656_348b_4b12_ba74_d39366b67f23_
#define _LOADERDLL_H_586bc656_348b_4b12_ba74_d39366b67f23_

#include <Windows.h>

// such stupid link.exe error
#if !defined(_M_AMD64) && !defined(_M_X64)
extern "C" __declspec(noreturn) void __cdecl __std_terminate() {}
#endif

#if !defined(ML_USER_MODE) && !defined(ML_KERNEL_MODE)

#error stupid

#if defined(__cplusplus)
#define DEFAULT_VALUE(type, var, value) type var = value
#define DEF_VAL(var, value)             var = value
#define EXTC extern "C"
#else
#define DEFAULT_VALUE(type, var, value) type var
#define DEF_VAL(var, value)             var
#define EXTC
#endif //CPP_DEFINED

typedef struct ML_PROCESS_INFORMATION : public PROCESS_INFORMATION
{
	PVOID FirstCallLdrLoadDll;

} ML_PROCESS_INFORMATION, *PML_PROCESS_INFORMATION;

typedef struct _TIME_FIELDS {
	SHORT Year;        // range [1601...]
	SHORT Month;       // range [1..12]
	SHORT Day;         // range [1..31]
	SHORT Hour;        // range [0..23]
	SHORT Minute;      // range [0..59]
	SHORT Second;      // range [0..59]
	SHORT Milliseconds;// range [0..999]
	SHORT Weekday;     // range [0..6] == [Sunday..Saturday]
} TIME_FIELDS, *PTIME_FIELDS;

typedef struct _RTL_TIME_ZONE_INFORMATION {
	LONG        Bias;
	WCHAR       StandardName[32];
	TIME_FIELDS StandardStart;
	LONG        StandardBias;
	WCHAR       DaylightName[32];
	TIME_FIELDS DaylightStart;
	LONG        DaylightBias;
} RTL_TIME_ZONE_INFORMATION, *PRTL_TIME_ZONE_INFORMATION;

typedef struct
{
	ULONG AnsiCodePage;
	ULONG OemCodePage;
	ULONG LocaleID;
	ULONG DefaultCharset;

	WCHAR DefaultFaceName[LF_FACESIZE];

	RTL_TIME_ZONE_INFORMATION Timezone;

} LOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK, *PLOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK, LEPB, *PLEPB;

#else

#include "../LocaleEmulatorPlus/LocaleEmulatorPlus.h"
#define LEP_API EXTC

#endif // ml

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

#endif // _LOADERDLL_H_586bc656_348b_4b12_ba74_d39366b67f23_
