#include <windows.h>
#include <stdio.h>
#include <wchar.h>

struct Environment {
    ULONG Size, Version, AnsiCodePage, OemCodePage, LocaleID, DefaultCharset;
    ULONG RegistryRedirectionMode, HookUILanguageMode;
    TIME_ZONE_INFORMATION Timezone;
    WCHAR TimeZoneId[128];
};
struct Payload {
    ULONG Magic, Version, HeaderSize, TotalSize, EnvironmentOffset, EnvironmentSize;
    ULONG FullPathOffset, FullPathLength, DirectoryOffset, DirectoryLength;
};
struct Config {
    ULONG Magic, Version, Size, PayloadSize, Result, ThreadId, InjectionFlags, Stage;
    ULONGLONG ProcessHandleValue;
    ULONG GrantedAccess, Reserved;
    BYTE Data[1];
};
static_assert(sizeof(Environment) == 460, "environment ABI");
static_assert(offsetof(Config, Data) == 48, "broker ABI");
static ULONG Tag(char a, char b, char c, char d) {
    return (ULONG)a | ((ULONG)b << 8) | ((ULONG)c << 16) | ((ULONG)d << 24);
}

int wmain(int argc, wchar_t **argv) {
    if (argc == 2 && wcscmp(argv[1], L"--child") == 0) {
        TIME_ZONE_INFORMATION zone = {};
        return GetACP() == 932 && GetTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID &&
            wcscmp(zone.StandardName, L"\u6771\u4eac\u6a19\u6e96\u6642") == 0 ? 0 : 1;
    }
    if (argc != 3) return 2;
    bool fallback = wcscmp(argv[2], L"fallback") == 0;
    bool restricted = wcscmp(argv[2], L"restricted") == 0;
    bool wrongPid = wcscmp(argv[2], L"wrong-pid") == 0;
    bool badPid = wcscmp(argv[2], L"bad-pid") == 0;
    HMODULE loader = LoadLibraryW(argv[1]); if (!loader) return 3;
    using Entry = void (CALLBACK *)(HWND, HINSTANCE, LPSTR, int);
    auto entry = (Entry)GetProcAddress(loader, "LepBrokerEntry"); if (!entry) return 4;
    wchar_t core[MAX_PATH], directory[MAX_PATH], exe[MAX_PATH], command[1024], mappingName[128];
    wcscpy_s(core, argv[1]);
    wchar_t *name = wcsrchr(core, L'\\'); if (!name) return 5;
#ifdef _WIN64
    wcscpy_s(name + 1, MAX_PATH - (name + 1 - core), L"LocaleEmulatorPlus_x64.dll");
#else
    wcscpy_s(name + 1, MAX_PATH - (name + 1 - core), L"LocaleEmulatorPlus_x86.dll");
#endif
    wcscpy_s(directory, core); *(wcsrchr(directory, L'\\') + 1) = 0;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    swprintf_s(command, L"\"%s\" --child", exe);
    STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
        NULL, NULL, &startup, &process)) return 6;

    ULONG payloadSize = sizeof(Payload) + sizeof(Environment) +
        (ULONG)((wcslen(core) + wcslen(directory) + 2) * sizeof(wchar_t));
    ULONG configSize = (ULONG)offsetof(Config, Data) + payloadSize;
    swprintf_s(mappingName, L"Local\\LEP_TEST_BROKER_%u", GetCurrentProcessId());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, configSize, mappingName);
    auto config = (Config *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, configSize);
    if (!config) { TerminateProcess(process.hProcess, 7); return 7; }
    ZeroMemory(config, configSize);
    config->Magic = Tag('L','B','C','1'); config->Version = 5;
    config->Size = configSize; config->PayloadSize = payloadSize;
    config->Result = 0xc0000001; config->ThreadId = process.dwThreadId; config->InjectionFlags = 5;
    HANDLE transferred = NULL;
    if (!fallback && !badPid) {
        if (!DuplicateHandle(GetCurrentProcess(), process.hProcess, GetCurrentProcess(), &transferred,
                restricted ? PROCESS_QUERY_LIMITED_INFORMATION : 0, FALSE,
                restricted ? 0 : DUPLICATE_SAME_ACCESS)) {
            TerminateProcess(process.hProcess, 8); return 8;
        }
        config->ProcessHandleValue = (ULONGLONG)(ULONG_PTR)transferred;
    }
    auto payload = (Payload *)config->Data;
    payload->Magic = Tag('L','B','P','1'); payload->Version = 2;
    payload->HeaderSize = sizeof(Payload); payload->TotalSize = payloadSize;
    payload->EnvironmentOffset = sizeof(Payload); payload->EnvironmentSize = sizeof(Environment);
    auto environment = (Environment *)(config->Data + payload->EnvironmentOffset);
    environment->Size = sizeof(Environment); environment->Version = 3;
    environment->AnsiCodePage = environment->OemCodePage = 932;
    environment->LocaleID = 0x411; environment->DefaultCharset = 128;
    environment->RegistryRedirectionMode = 2; environment->HookUILanguageMode = 1;
    environment->Timezone.Bias = -540;
    wcscpy_s(environment->TimeZoneId, L"Tokyo Standard Time");
    payload->FullPathOffset = sizeof(Payload) + sizeof(Environment);
    payload->FullPathLength = (ULONG)(wcslen(core) * 2);
    memcpy(config->Data + payload->FullPathOffset, core, payload->FullPathLength + 2);
    payload->DirectoryOffset = payload->FullPathOffset + payload->FullPathLength + 2;
    payload->DirectoryLength = (ULONG)(wcslen(directory) * 2);
    memcpy(config->Data + payload->DirectoryOffset, directory, payload->DirectoryLength + 2);
    ULONG targetPid = badPid ? 0xffffffffUL : wrongPid ? GetCurrentProcessId() : process.dwProcessId;
    char args[256];
    sprintf_s(args, "%lu|Local\\LEP_TEST_BROKER_%u", targetPid, GetCurrentProcessId());
    entry(NULL, NULL, args, 0);
    bool rejected = restricted || wrongPid || badPid;
    bool passed = rejected ? (LONG)config->Result < 0 : config->Result == 0;
    if (restricted) passed = passed && config->Result == 0xc0000022 && config->Stage == 4 &&
        (config->GrantedAccess & (PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION)) == 0;
    if (wrongPid) passed = passed && config->Stage == 5;
    if (badPid) passed = passed && config->Stage == 3;
    printf("result=%08lx stage=%lu granted=%08lx\n", config->Result, config->Stage, config->GrantedAccess);
    if (!rejected && passed) {
        ResumeThread(process.hThread);
        if (WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0) {
            DWORD code; GetExitCodeProcess(process.hProcess, &code); passed = code == 0;
        } else { passed = false; TerminateProcess(process.hProcess, 9); }
    } else TerminateProcess(process.hProcess, 0);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    UnmapViewOfFile(config); CloseHandle(mapping); FreeLibrary(loader);
    return passed ? 0 : 10;
}
