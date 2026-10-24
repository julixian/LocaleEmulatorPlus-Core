// Native public-API ABI probe. Build as x86 and x64; see ai-docs/lepb-v4.md.
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

struct EnvironmentV2 {
    ULONG Size, Version, AnsiCodePage, OemCodePage, LocaleId, FontCharset;
    ULONG RegistryRedirectionMode, HookUILanguageMode;
    struct {
        LONG Bias;
        WCHAR StandardName[32];
        SHORT StandardStart[8];
        LONG StandardBias;
        WCHAR DaylightName[32];
        SHORT DaylightStart[8];
        LONG DaylightBias;
    } Timezone;
};
struct EnvironmentV3 { EnvironmentV2 Base; WCHAR TimezoneId[128]; };
typedef LONG (WINAPI *CreateWithLocale)(void *, PCWSTR, PWSTR, PCWSTR,
    ULONG, LPSTARTUPINFOW, LPPROCESS_INFORMATION, void *, void *, void *, HANDLE);
static_assert(sizeof(EnvironmentV2) == 204, "v2 ABI");
static_assert(sizeof(EnvironmentV3) == 460, "v3 ABI");

static DWORD WaitChild(HANDLE process) {
    if (WaitForSingleObject(process, 15000) != WAIT_OBJECT_0) {
        TerminateProcess(process, 90); return 90;
    }
    DWORD exitCode = 0; GetExitCodeProcess(process, &exitCode); return exitCode;
}

int wmain(int argc, wchar_t **argv) {
    wchar_t executable[MAX_PATH]; GetModuleFileNameW(NULL, executable, MAX_PATH);
    if (argc == 5 && wcscmp(argv[1], L"--child") == 0) {
        TIME_ZONE_INFORMATION tzi = {};
        DWORD result = GetTimeZoneInformation(&tzi);
        bool normalized = wcscmp(tzi.StandardName, L"東京標準時") == 0;
        bool expected = wcscmp(argv[2], L"normalized") == 0;
        bool dst = wcscmp(argv[2], L"dst") == 0;
        if (result == TIME_ZONE_ID_INVALID || GetACP() != 932 || normalized != expected ||
            tzi.Bias != (dst ? 480 : -540) || tzi.StandardBias != 0 || tzi.DaylightBias != (dst ? -60 : 0) ||
            wcscmp(tzi.StandardName, tzi.DaylightName) != 0) return 30;
        if (!expected && wcscmp(tzi.StandardName, L"Host language name") != 0) return 31;
        if (dst && (tzi.StandardDate.wMonth != 11 || tzi.StandardDate.wDay != 1 ||
            tzi.StandardDate.wDayOfWeek != 0 || tzi.StandardDate.wHour != 2 ||
            tzi.DaylightDate.wMonth != 3 || tzi.DaylightDate.wDay != 2 ||
            tzi.DaylightDate.wDayOfWeek != 0 || tzi.DaylightDate.wHour != 2)) return 35;
        int depth = _wtoi(argv[3]);
        if (depth == 0) return 0;
        // A second generation exercises Core's child-payload serialization.
        // argv[4] may select the opposite architecture to exercise the broker.
        wchar_t command[2048];
        swprintf_s(command, L"\"%s\" --child %s 0 \"%s\"", argv[4], argv[2], argv[4]);
        STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        if (!CreateProcessW(argv[4], command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                NULL, NULL, &startup, &process)) return 32;
        DWORD code = WaitChild(process.hProcess);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        return (int)code;
    }
    if (argc < 3) return 20;
    HMODULE loader = LoadLibraryW(argv[1]); if (!loader) return 21;
    CreateWithLocale create = (CreateWithLocale)GetProcAddress(loader, "LepCreateProcess2");
    if (!create) return 22;
    EnvironmentV3 environment = {};
    environment.Base.Size = sizeof(environment);
    environment.Base.Version = 3;
    environment.Base.AnsiCodePage = environment.Base.OemCodePage = 932;
    environment.Base.LocaleId = 0x411;
    environment.Base.FontCharset = 128;
    environment.Base.RegistryRedirectionMode = 2;
    environment.Base.HookUILanguageMode = 1;
    environment.Base.Timezone.Bias = -540;
    wcscpy_s(environment.Base.Timezone.StandardName, L"Host language name");
    wcscpy_s(environment.Base.Timezone.DaylightName, L"Host language name");
    wcscpy_s(environment.TimezoneId, L"Tokyo Standard Time");
    bool expectNormalized = true, reject = false;
    if (wcscmp(argv[2], L"v2") == 0 || wcscmp(argv[2], L"v2-nested") == 0 ||
        wcsncmp(argv[2], L"v2-dst", 6) == 0) {
        environment.Base.Size = sizeof(EnvironmentV2); environment.Base.Version = 2;
        expectNormalized = false;
    }
    if (wcscmp(argv[2], L"empty") == 0) { environment.TimezoneId[0] = 0; expectNormalized = false; }
    if (wcscmp(argv[2], L"korea") == 0) { wcscpy_s(environment.TimezoneId, L"Korea Standard Time"); expectNormalized = false; }
    if (wcscmp(argv[2], L"uppercase") == 0) wcscpy_s(environment.TimezoneId, L"TOKYO STANDARD TIME");
    bool dst = wcsstr(argv[2], L"-dst") != nullptr;
    if (dst) {
        expectNormalized = false;
        environment.Base.Timezone.Bias = 480;
        environment.Base.Timezone.DaylightBias = -60;
        SHORT standard[] = {0,11,1,2,0,0,0,0}, daylight[] = {0,3,2,2,0,0,0,0};
        memcpy(environment.Base.Timezone.StandardStart, standard, sizeof(standard));
        memcpy(environment.Base.Timezone.DaylightStart, daylight, sizeof(daylight));
        wcscpy_s(environment.TimezoneId, L"Pacific Standard Time");
    }
    if (wcscmp(argv[2], L"bad-size") == 0) { environment.Base.Size = 459; reject = true; }
    if (wcscmp(argv[2], L"bad-version") == 0) { environment.Base.Version = 99; reject = true; }
    if (wcscmp(argv[2], L"v3-short") == 0) { environment.Base.Size = 204; reject = true; }
    if (wcscmp(argv[2], L"v2-long") == 0) { environment.Base.Version = 2; reject = true; }
    if (wcscmp(argv[2], L"unterminated") == 0) { for (auto &ch : environment.TimezoneId) ch = L'x'; reject = true; }
    if (wcscmp(argv[2], L"unknown-id") == 0) { wcscpy_s(environment.TimezoneId, L"LEP Unknown Time Zone"); reject = true; }
    if (wcscmp(argv[2], L"bad-bias") == 0) { environment.Base.Timezone.Bias = -480; reject = true; }
    if (wcscmp(argv[2], L"bad-standard-bias") == 0) { environment.Base.Timezone.StandardBias = 1; reject = true; }
    if (wcscmp(argv[2], L"bad-path") == 0) { wcscpy_s(environment.TimezoneId, L"Tokyo Standard Time\\subkey"); reject = true; }
    if (wcscmp(argv[2], L"old-dll") == 0) reject = true;

    // Place the supplied buffer immediately before an inaccessible page.
    // Any read/copy beyond Size will fault, including the v2 optional tail.
    SYSTEM_INFO system; GetSystemInfo(&system);
    BYTE *memory = (BYTE *)VirtualAlloc(NULL, system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) return 23;
    DWORD oldProtect;
    VirtualProtect(memory + system.dwPageSize, system.dwPageSize, PAGE_NOACCESS, &oldProtect);
    ULONG suppliedSize = environment.Base.Size <= 460 ? environment.Base.Size : 460;
    void *buffer = memory + system.dwPageSize - suppliedSize;
    memcpy(buffer, &environment, suppliedSize);
    VirtualProtect(memory, system.dwPageSize, PAGE_READONLY, &oldProtect);
    wchar_t command[2048], directory[MAX_PATH];
    wcscpy_s(directory, executable); *wcsrchr(directory, L'\\') = 0;
    int depth = wcsstr(argv[2], L"nested") != nullptr || argc == 4 ? 1 : 0;
    const wchar_t *grandchild = argc == 4 ? argv[3] : executable;
    swprintf_s(command, L"\"%s\" --child %s %d \"%s\"", executable,
        dst ? L"dst" : expectNormalized ? L"normalized" : L"preserved", depth, grandchild);
    STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    LONG status = create(buffer, executable, command, directory, CREATE_NO_WINDOW,
        &startup, &process, NULL, NULL, NULL, NULL);
    printf("status=%08lx ", status);
    VirtualFree(memory, 0, MEM_RELEASE);
    if (reject && status < 0) { puts("rejected as expected"); return 0; }
    if (status < 0) { puts("unexpected failure"); return 24; }
    DWORD code = WaitChild(process.hProcess);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    printf("child=%lu expected=%s\n", code, reject ? "reject" : "success");
    return reject ? 25 : (int)code;
}
