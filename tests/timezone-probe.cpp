#include <Windows.h>
#include <stdio.h>
#include <wchar.h>
import LocaleEmulatorPlus;

namespace
{
    constexpr wchar_t ExpectedVariable[] = L"LEP_TEST_TIMEZONE_SNAPSHOT";
    constexpr wchar_t JapaneseName[] = L"東京標準時";
    struct RegistryTimeZone
    {
        LONG Bias, StandardBias, DaylightBias;
        SYSTEMTIME StandardDate, DaylightDate;
    };
    struct NativeTimeZone
    {
        LONG Bias;
        WCHAR StandardName[32];
        SHORT StandardStart[8];
        LONG StandardBias;
        WCHAR DaylightName[32];
        SHORT DaylightStart[8];
        LONG DaylightBias;
    };
    static_assert(sizeof(NativeTimeZone) == 172);

    DWORD WaitChild(HANDLE Process)
    {
        if (WaitForSingleObject(Process, 15000) != WAIT_OBJECT_0)
        {
            TerminateProcess(Process, 90);
            return 90;
        }
        DWORD Code = 0;
        GetExitCodeProcess(Process, &Code);
        return Code;
    }

    bool ReadExpected(const LEP::LEPB& Environment, TIME_ZONE_INFORMATION& Zone)
    {
        wchar_t Path[256];
        swprintf_s(Path, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\%s", Environment.TimeZoneId);
        HKEY Key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ | KEY_WOW64_64KEY, &Key) != ERROR_SUCCESS)
            return false;
        RegistryTimeZone Tzi{};
        DWORD Size = sizeof(Tzi);
        bool Success = RegGetValueW(Key, nullptr, L"TZI", RRF_RT_REG_BINARY, nullptr, &Tzi, &Size) == ERROR_SUCCESS && Size == sizeof(Tzi);
        Size = sizeof(Zone.StandardName);
        Success = Success && RegGetValueW(Key, nullptr, L"Std", RRF_RT_REG_SZ, nullptr, Zone.StandardName, &Size) == ERROR_SUCCESS;
        Size = sizeof(Zone.DaylightName);
        Success = Success && RegGetValueW(Key, nullptr, L"Dlt", RRF_RT_REG_SZ, nullptr, Zone.DaylightName, &Size) == ERROR_SUCCESS;
        RegCloseKey(Key);
        if (!Success)
            return false;
        Zone.Bias = Tzi.Bias;
        Zone.StandardBias = Tzi.StandardBias;
        Zone.DaylightBias = Tzi.DaylightBias;
        Zone.StandardDate = Tzi.StandardDate;
        Zone.DaylightDate = Tzi.DaylightDate;
        if (_wcsicmp(Environment.TimeZoneId, L"Tokyo Standard Time") == 0)
        {
            ZeroMemory(Zone.StandardName, sizeof(Zone.StandardName));
            ZeroMemory(Zone.DaylightName, sizeof(Zone.DaylightName));
            wcscpy_s(Zone.StandardName, JapaneseName);
            wcscpy_s(Zone.DaylightName, JapaneseName);
        }
        if (Environment.StandardNameOverride[0])
            memcpy(Zone.StandardName, Environment.StandardNameOverride, sizeof(Zone.StandardName));
        if (Environment.DaylightNameOverride[0])
            memcpy(Zone.DaylightName, Environment.DaylightNameOverride, sizeof(Zone.DaylightName));
        return true;
    }

    void PublishExpected(const TIME_ZONE_INFORMATION& Zone)
    {
        wchar_t Hex[sizeof(Zone) * 2 + 1];
        const BYTE* Bytes = reinterpret_cast<const BYTE*>(&Zone);
        for (size_t Index = 0; Index < sizeof(Zone); ++Index)
            swprintf_s(Hex + Index * 2, 3, L"%02x", Bytes[Index]);
        SetEnvironmentVariableW(ExpectedVariable, Hex);
    }

    void ConvertDate(const SYSTEMTIME& Date, SHORT (&Fields)[8])
    {
        SHORT Expected[] = {SHORT(Date.wYear), SHORT(Date.wMonth), SHORT(Date.wDay),
            SHORT(Date.wHour), SHORT(Date.wMinute), SHORT(Date.wSecond),
            SHORT(Date.wMilliseconds), SHORT(Date.wDayOfWeek)};
        memcpy(Fields, Expected, sizeof(Expected));
    }

    int CheckChild()
    {
        TIME_ZONE_INFORMATION Expected{}, Actual{};
        wchar_t Hex[sizeof(Expected) * 2 + 1];
        if (GetEnvironmentVariableW(ExpectedVariable, Hex, _countof(Hex)) != sizeof(Expected) * 2)
            return 30;
        BYTE* Bytes = reinterpret_cast<BYTE*>(&Expected);
        for (size_t Index = 0; Index < sizeof(Expected); ++Index)
        {
            unsigned Value;
            if (swscanf_s(Hex + Index * 2, L"%2x", &Value) != 1)
                return 31;
            Bytes[Index] = BYTE(Value);
        }
        if (GetACP() != 932 || GetTimeZoneInformation(&Actual) == TIME_ZONE_ID_INVALID ||
            memcmp(&Actual, &Expected, sizeof(Expected)) != 0)
        {
            wprintf(L"Win32 mismatch: bias=%ld/%ld dst=%ld/%ld dates=%hu/%hu names=%s | %s\n",
                Actual.Bias, Expected.Bias, Actual.DaylightBias, Expected.DaylightBias,
                Actual.StandardDate.wMonth, Actual.DaylightDate.wMonth, Actual.StandardName, Actual.DaylightName);
            return 32;
        }
        NativeTimeZone ExpectedNative{}, ActualNative{};
        ExpectedNative.Bias = Expected.Bias;
        ExpectedNative.StandardBias = Expected.StandardBias;
        ExpectedNative.DaylightBias = Expected.DaylightBias;
        memcpy(ExpectedNative.StandardName, Expected.StandardName, sizeof(Expected.StandardName));
        memcpy(ExpectedNative.DaylightName, Expected.DaylightName, sizeof(Expected.DaylightName));
        ConvertDate(Expected.StandardDate, ExpectedNative.StandardStart);
        ConvertDate(Expected.DaylightDate, ExpectedNative.DaylightStart);
        using Query = LONG (WINAPI*)(ULONG, void*, ULONG, ULONG*);
        auto QuerySystem = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
        ULONG Length = 0;
        if (!QuerySystem || QuerySystem(44, &ActualNative, sizeof(ActualNative), &Length) < 0 ||
            Length != sizeof(ActualNative) || memcmp(&ActualNative, &ExpectedNative, sizeof(ActualNative)) != 0)
            return 33;
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 5 && wcscmp(argv[1], L"--child") == 0)
    {
        int Result = CheckChild();
        int Depth = _wtoi(argv[2]);
        if (Result || !Depth)
            return Result;
        wchar_t Command[2048];
        swprintf_s(Command, L"\"%s\" --child %d \"%s\" \"%s\"", argv[3], Depth - 1, argv[4], argv[3]);
        STARTUPINFOW Startup{sizeof(Startup)};
        PROCESS_INFORMATION Process{};
        if (!CreateProcessW(argv[3], Command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &Startup, &Process))
            return 34;
        DWORD Code = WaitChild(Process.hProcess);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
        return int(Code);
    }
    if (argc < 4 || argc > 5)
        return 1;
    HMODULE Loader = LoadLibraryW(argv[1]);
    if (!Loader)
        return 2;
    char Export[64];
    sprintf_s(Export, "%ls", argv[3]);
    auto Create = reinterpret_cast<LEP::LepCreateProcess_t>(GetProcAddress(Loader, Export));
    if (!Create)
        return 3;

    LEP::LEPB Environment{};
    Environment.Size = sizeof(Environment);
    Environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
    Environment.AnsiCodePage = Environment.OemCodePage = 932;
    Environment.LocaleID = 0x411;
    Environment.DefaultCharset = 128;
    Environment.RegistryRedirectionMode = 2;
    Environment.HookUILanguageMode = 1;
    PCWSTR Case = argv[2];
    auto Is = [Case](PCWSTR Name) { return wcscmp(Case, Name) == 0; };
    PCWSTR Id = L"Tokyo Standard Time";
    if (Is(L"korea")) Id = L"Korea Standard Time";
    if (Is(L"utc")) Id = L"UTC";
    if (wcsstr(Case, L"pacific") || Is(L"non-tokyo-overrides")) Id = L"Pacific Standard Time";
    if (wcsstr(Case, L"southern")) Id = L"AUS Eastern Standard Time";
    if (wcsstr(Case, L"israel")) Id = L"Israel Standard Time";
    if (Is(L"uppercase")) Id = L"TOKYO STANDARD TIME";
    wcscpy_s(Environment.TimeZoneId, Id);
    if (Is(L"standard-only")) wcscpy_s(Environment.StandardNameOverride, L"標準時間");
    if (Is(L"daylight-only")) wcscpy_s(Environment.DaylightNameOverride, L"夏令时间");
    if (Is(L"overrides") || Is(L"non-tokyo-overrides") || wcsstr(Case, L"nested"))
    {
        wcscpy_s(Environment.StandardNameOverride, L"LEP custom standard");
        wcscpy_s(Environment.DaylightNameOverride, L"LEP custom daylight");
    }
    if (Is(L"max-names"))
    {
        wmemset(Environment.StandardNameOverride, L'標', 31);
        wmemset(Environment.DaylightNameOverride, L'夏', 31);
    }
    bool Reject = true;
    if (Is(L"bad-size")) Environment.Size -= 2;
    else if (Is(L"oversize")) Environment.Size += 4;
    else if (Is(L"short-header")) Environment.Size = 4;
    else if (Is(L"bad-version")) Environment.Version = 99;
    else if (Is(L"legacy-size")) Environment.Size = 204;
    else if (Is(L"empty-id")) Environment.TimeZoneId[0] = 0;
    else if (Is(L"unknown-id")) wcscpy_s(Environment.TimeZoneId, L"LEP Unknown Time Zone");
    else if (Is(L"bad-path")) wcscpy_s(Environment.TimeZoneId, L"Tokyo Standard Time\\subkey");
    else if (Is(L"slash")) wcscpy_s(Environment.TimeZoneId, L"Tokyo Standard Time/subkey");
    else if (Is(L"id-unterminated")) wmemset(Environment.TimeZoneId, L'x', 128);
    else if (Is(L"standard-unterminated")) wmemset(Environment.StandardNameOverride, L'x', 32);
    else if (Is(L"daylight-unterminated")) wmemset(Environment.DaylightNameOverride, L'x', 32);
    else if (Is(L"bad-registry-mode")) Environment.RegistryRedirectionMode = 3;
    else if (Is(L"bad-ui-mode")) Environment.HookUILanguageMode = 3;
    else if (!Is(L"null")) Reject = false;
    TIME_ZONE_INFORMATION Expected{};
    if (!Reject && !ReadExpected(Environment, Expected))
        return 4;
    PublishExpected(Expected);

    SYSTEM_INFO System;
    GetSystemInfo(&System);
    BYTE* Memory = static_cast<BYTE*>(VirtualAlloc(nullptr, System.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!Memory)
        return 5;
    DWORD Old;
    VirtualProtect(Memory + System.dwPageSize, System.dwPageSize, PAGE_NOACCESS, &Old);
    ULONG Supplied = Environment.Size < sizeof(Environment) ? Environment.Size : sizeof(Environment);
    void* Buffer = Memory + System.dwPageSize - Supplied;
    memcpy(Buffer, &Environment, Supplied);
    VirtualProtect(Memory, System.dwPageSize, PAGE_READONLY, &Old);
    wchar_t Executable[MAX_PATH], Directory[MAX_PATH], Command[2048];
    GetModuleFileNameW(nullptr, Executable, _countof(Executable));
    wcscpy_s(Directory, Executable);
    *wcsrchr(Directory, L'\\') = 0;
    int Depth = wcsstr(Case, L"nested") ? 2 : 0;
    PCWSTR Other = argc == 5 ? argv[4] : Executable;
    swprintf_s(Command, L"\"%s\" --child %d \"%s\" \"%s\"", Executable, Depth, Other, Executable);
    STARTUPINFOW Startup{sizeof(Startup)};
    struct {
        PROCESS_INFORMATION Process;
        ULONG_PTR Canary;
    } Output{};
    constexpr ULONG_PTR Canary = 0x1234abcd;
    Output.Canary = Canary;
    PROCESS_INFORMATION& Process = Output.Process;
    LONG Status = Create(Is(L"null") ? nullptr : static_cast<LEP::PLEPB>(Buffer), Executable, Command,
        Directory, CREATE_NO_WINDOW, &Startup, &Process, nullptr, nullptr, nullptr, nullptr);
    VirtualFree(Memory, 0, MEM_RELEASE);
    if (Status < 0)
    {
        printf("%ls/%s: %08lx\n", Case, Export, ULONG(Status));
        return Reject ? 0 : 6;
    }
    DWORD Code = WaitChild(Process.hProcess);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    if (Output.Canary != Canary)
        return 8;
    printf("%ls/%s: child=%lu\n", Case, Export, Code);
    return Reject ? 7 : int(Code);
}
