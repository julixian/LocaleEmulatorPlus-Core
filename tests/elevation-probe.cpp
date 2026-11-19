#include <Windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
import LocaleEmulatorPlus;

static bool Snapshot(PCWSTR directory, PCWSTR stage)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    TOKEN_ELEVATION_TYPE type{};
    BYTE integrity[256]{};
    DWORD size = 0;
    bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
        GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size) &&
        GetTokenInformation(token, TokenIntegrityLevel, integrity, sizeof(integrity), &size);
    CloseHandle(token);
    if (!ok) return false;
    PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(integrity)->Label.Sid;
    DWORD level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    WCHAR path[1024];
    swprintf_s(path, L"%s\\%s.json", directory, stage);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"wb")) return false;
    fprintf(file, "{\"stage\":\"%ls\",\"pid\":%lu,\"arch\":\"%s\",\"elevated\":%lu,"
        "\"elevationType\":%u,\"integrityRid\":%lu,\"acp\":%u,\"oemcp\":%u,"
        "\"threadLcid\":%lu,\"userLcid\":%lu,\"systemLcid\":%lu,\"uiLanguage\":%u}\n",
        stage, GetCurrentProcessId(), sizeof(void*) == 8 ? "x64" : "x86", elevation.TokenIsElevated,
        unsigned(type), level, GetACP(), GetOEMCP(), GetThreadLocale(), GetUserDefaultLCID(),
        GetSystemDefaultLCID(), GetUserDefaultUILanguage());
    return fclose(file) == 0;
}

static int Wait(HANDLE process)
{
    DWORD wait = WaitForSingleObject(process, 600000), code = 97;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return int(code);
}

static int Spawn(PCWSTR executable, PCWSTR directory, PCWSTR stage)
{
    WCHAR command[2048];
    swprintf_s(command, L"\"%s\" --leaf \"%s\" %s", executable, directory, stage);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) return int(GetLastError());
    CloseHandle(process.hThread);
    return Wait(process.hProcess);
}

int wmain(int argc, wchar_t** argv)
{
    WCHAR self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (argc == 4 && !wcscmp(argv[1], L"--leaf")) {
        if (!Snapshot(argv[2], argv[3])) return 2;
        // Does the elevated child's next ordinary child retain the locale?
        if (!wcscmp(argv[3], L"elevated")) return Spawn(self, argv[2], L"elevated-grandchild");
        return 0;
    }
    if (argc == 5 && !wcscmp(argv[1], L"--parent")) {
        if (!Snapshot(argv[2], L"parent")) return 3;
        int code = Spawn(self, argv[2], L"ordinary-child");
        if (code) return code;
        WCHAR parameters[1536];
        swprintf_s(parameters, L"--leaf \"%s\" elevated", argv[2]);
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SHELLEXECUTEINFOW launch{sizeof(launch)};
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        launch.lpVerb = !wcscmp(argv[4], L"manifest") ? L"open" : L"runas";
        launch.lpFile = argv[3];
        launch.lpParameters = parameters;
        launch.lpDirectory = argv[2];
        launch.nShow = SW_HIDE;
        BOOL started = ShellExecuteExW(&launch);
        DWORD error = started ? 0 : GetLastError();
        WCHAR path[1024];
        swprintf_s(path, L"%s\\shell-result.json", argv[2]);
        FILE* file = nullptr;
        if (!_wfopen_s(&file, path, L"wb")) {
            fprintf(file, "{\"started\":%s,\"error\":%lu,\"childPid\":%lu}\n",
                started ? "true" : "false", error, launch.hProcess ? GetProcessId(launch.hProcess) : 0);
            fclose(file);
        }
        code = started && launch.hProcess ? Wait(launch.hProcess) : int(error ? error : 98);
        if (SUCCEEDED(com)) CoUninitialize();
        return code;
    }
    if (argc != 5) return 1;
    // launcher: Loader DLL, absolute result directory, elevated executable, mode.
    if (!Snapshot(argv[2], L"native")) return 4;
    HMODULE loader = LoadLibraryW(argv[1]);
    auto create = loader ? reinterpret_cast<LEP::LepCreateProcess2_t>(GetProcAddress(loader, "LepCreateProcess2")) : nullptr;
    if (!create) return 5;
    LEP::LEPB environment{};
    environment.Size = sizeof(environment); environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
    environment.AnsiCodePage = environment.OemCodePage = 932;
    environment.LocaleId = 0x411; environment.FontCharset = SHIFTJIS_CHARSET;
    environment.RegistryRedirectionMode = environment.HookUILanguageMode = 2;
    wcscpy_s(environment.TimezoneId, L"Tokyo Standard Time");
    WCHAR command[3072];
    swprintf_s(command, L"\"%s\" --parent \"%s\" \"%s\" %s", self, argv[2], argv[3], argv[4]);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    LONG status = create(&environment, self, command, argv[2], CREATE_NO_WINDOW,
        &startup, &process, nullptr, nullptr, nullptr, nullptr);
    if (status < 0) { printf("LEP launch failed %08lX\n", ULONG(status)); return 6; }
    CloseHandle(process.hThread);
    return Wait(process.hProcess);
}
