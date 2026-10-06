module;

#include <Windows.h>
#include <stddef.h>

// Standalone declarations for LoadLibrary/GetProcAddress callers.
// No Core headers or import libraries are required.
export module LocaleEmulatorPlus;

#pragma pack(push, 8)
export namespace LEP
{
    using NTSTATUS = LONG;
    inline constexpr ULONG LEP_ENVIRONMENT_VERSION = 3;

    struct TIME_FIELDS
    {
        SHORT Year;
        SHORT Month;
        SHORT Day;
        SHORT Hour;
        SHORT Minute;
        SHORT Second;
        SHORT Milliseconds;
        SHORT Weekday;
    };
    using PTIME_FIELDS = TIME_FIELDS*;

    // These are NT TIME_FIELDS, not Win32 SYSTEMTIME transition fields.
    struct RTL_TIME_ZONE_INFORMATION
    {
        LONG Bias;
        WCHAR StandardName[32];
        TIME_FIELDS StandardStart;
        LONG StandardBias;
        WCHAR DaylightName[32];
        TIME_FIELDS DaylightStart;
        LONG DaylightBias;
    };
    using PRTL_TIME_ZONE_INFORMATION = RTL_TIME_ZONE_INFORMATION*;

    struct LOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK
    {
        ULONG Size;
        ULONG Version;
        ULONG AnsiCodePage;
        ULONG OemCodePage;
        ULONG LocaleID;
        ULONG DefaultCharset;
        ULONG RegistryRedirectionMode;
        ULONG HookUILanguageMode;
        RTL_TIME_ZONE_INFORMATION Timezone;
        WCHAR TimeZoneId[128]; // Optional; zero-terminated Windows ID.
    };
    using LEPB = LOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK;
    using PLEPB = LEPB*;
    using PLOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK = PLEPB;

    struct ML_PROCESS_INFORMATION
    {
        HANDLE hProcess;
        HANDLE hThread;
        DWORD dwProcessId;
        DWORD dwThreadId;
        PVOID FirstCallLdrLoadDll;
    };
    using PML_PROCESS_INFORMATION = ML_PROCESS_INFORMATION*;

    // Both exports use this signature. NTSTATUS is signed, not a Win32
    // GetLastError code; success is status >= 0. WINAPI matters on x86.
    using LepCreateProcess_t = NTSTATUS (WINAPI*)(
        PLEPB EnvironmentBlock,
        PCWSTR ApplicationName,
        PWSTR CommandLine,
        PCWSTR CurrentDirectory,
        ULONG CreationFlags,
        LPSTARTUPINFOW StartupInfo,
        PML_PROCESS_INFORMATION ProcessInformation,
        LPSECURITY_ATTRIBUTES ProcessAttributes,
        LPSECURITY_ATTRIBUTES ThreadAttributes,
        PVOID Environment,
        HANDLE Token);

    using LepCreateProcess2_t = LepCreateProcess_t;

    static_assert(sizeof(TIME_FIELDS) == 16);
    static_assert(sizeof(RTL_TIME_ZONE_INFORMATION) == 172);
    static_assert(sizeof(LEPB) == 460);
    static_assert(offsetof(LEPB, Timezone) == 32);
    static_assert(offsetof(LEPB, TimeZoneId) == 204);
    static_assert(offsetof(ML_PROCESS_INFORMATION, FirstCallLdrLoadDll) == sizeof(PROCESS_INFORMATION));
    static_assert(sizeof(ML_PROCESS_INFORMATION) == (sizeof(PVOID) == 8 ? 32 : 20));
}
#pragma pack(pop)
