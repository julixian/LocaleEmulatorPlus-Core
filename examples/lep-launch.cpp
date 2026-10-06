#include <Windows.h>
#include <stdio.h>
#include <wchar.h>
import LocaleEmulatorPlus;

// Usage: lep-launch.exe <absolute LoaderDll path> <absolute target EXE path>
// Use the matching x86/x64 launcher and DLL. The DLLs must be a matched set.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        puts("Usage: lep-launch.exe <LoaderDll path> <target EXE path>");
        return 1;
    }
    HMODULE loader = LoadLibraryW(argv[1]);
    if (loader == nullptr)
    {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 2;
    }
    auto create = reinterpret_cast<LEP::LepCreateProcess2_t>(GetProcAddress(loader, "LepCreateProcess2"));
    if (create == nullptr)
    {
        FreeLibrary(loader);
        return 3;
    }

    LEP::LEPB environment{};
    environment.Size = sizeof(environment);
    environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
    environment.AnsiCodePage = environment.OemCodePage = 932;
    environment.LocaleID = 0x411;
    environment.DefaultCharset = SHIFTJIS_CHARSET;
    environment.RegistryRedirectionMode = 2;
    environment.HookUILanguageMode = 1;
    // Japan has no DST transitions. Core supplies the Japanese names by ID.
    environment.Timezone.Bias = -540;
    wcscpy_s(environment.TimeZoneId, L"Tokyo Standard Time");

    wchar_t currentDirectory[32768];
    wchar_t* filePart = nullptr;
    DWORD pathLength = GetFullPathNameW(argv[2], 32768, currentDirectory, &filePart);
    if (pathLength == 0 || pathLength >= 32768 || filePart == nullptr)
    {
        FreeLibrary(loader);
        return 5;
    }
    *filePart = L'\0';
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    LEP::ML_PROCESS_INFORMATION process{};
    LEP::NTSTATUS status = create(&environment, argv[2], nullptr, currentDirectory, 0,
        &startup, &process, nullptr, nullptr, nullptr, nullptr);
    printf("LepCreateProcess2: %08lx\n", static_cast<ULONG>(status));
    FreeLibrary(loader);
    if (status < 0)
        return 4;

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
