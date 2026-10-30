#include <Windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
import LocaleEmulatorPlus;

static int Child(UINT target)
{
    // Clipboard state belongs to a window station. Keep the user's WinSta0 untouched.
    HWINSTA station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !SetProcessWindowStation(station)) return 2;
    HDESK desktop = CreateDesktopW(L"LepClipboardProbe", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) return 3;
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 32, 32,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window || GetACP() != target) return 4;

    const WCHAR* sample = target == 1252 ? L"€A1" : L"中A1";
    const SIZE_T sampleBytes = (wcslen(sample) + 1) * sizeof(WCHAR);
    CHAR expected[32] = {};
    int bytes = WideCharToMultiByte(target, 0, sample, -1, expected, sizeof(expected), nullptr, nullptr);
    if (bytes == 0 || !OpenClipboard(window) || !EmptyClipboard()) return 5;
    HGLOBAL unicode = GlobalAlloc(GHND, sampleBytes);
    if (!unicode) return 6;
    auto source = static_cast<WCHAR*>(GlobalLock(unicode));
    if (!source) return 7;
    memcpy(source, sample, sampleBytes);
    GlobalUnlock(unicode);
    if (!SetClipboardData(CF_UNICODETEXT, unicode)) return 8;
    CloseClipboard();

    for (unsigned repeat = 0; repeat != 3; ++repeat) {
        if (!OpenClipboard(window)) return 9;
        HGLOBAL text = GetClipboardData(CF_TEXT);
        if (!text) return 10;
        auto locked = static_cast<const CHAR*>(GlobalLock(text));
        if (!locked || GlobalSize(text) < unsigned(bytes)) return 11;
        bool valid = !memcmp(locked, expected, bytes) && text == locked &&
                     !memcmp(static_cast<const CHAR*>(text), expected, bytes);
        printf("CP%u repeat=%u bytes=", target, repeat);
        for (int i = 0; i != bytes; ++i) printf("%02X ", (BYTE)locked[i]);
        printf("direct=%u\n", text == locked);
        GlobalUnlock(text);
        if (!valid) return 12;
        unicode = GetClipboardData(CF_UNICODETEXT);
        source = static_cast<WCHAR*>(GlobalLock(unicode));
        if (!source || GlobalSize(unicode) < sampleBytes || memcmp(source, sample, sampleBytes)) return 13;
        GlobalUnlock(unicode);
        CloseClipboard();
    }
    DestroyWindow(window);
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && !wcscmp(argv[1], L"--child")) return Child(_wtoi(argv[2]));
    if (argc != 2) return 1;
    HMODULE loader = LoadLibraryW(argv[1]);
    auto create = loader ? reinterpret_cast<LEP::LepCreateProcess2_t>(GetProcAddress(loader, "LepCreateProcess2")) : nullptr;
    if (!create) return 14;
    WCHAR executable[MAX_PATH], command[1024];
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    UINT codepages[] = {936, 932, 1252};
    for (UINT target : codepages) {
        LEP::LEPB environment{};
        environment.Size = sizeof(environment); environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
        environment.AnsiCodePage = environment.OemCodePage = target;
        environment.LocaleId = target == 936 ? 0x804 : target == 932 ? 0x411 : 0x409;
        environment.FontCharset = target == 936 ? GB2312_CHARSET : target == 932 ? SHIFTJIS_CHARSET : ANSI_CHARSET;
        environment.RegistryRedirectionMode = 2; environment.HookUILanguageMode = 2;
        wcscpy_s(environment.TimezoneId, target == 936 ? L"China Standard Time" : target == 932 ? L"Tokyo Standard Time" : L"Pacific Standard Time");
        swprintf_s(command, L"\"%s\" --child %u", executable, target);
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        LONG status = create(&environment, executable, command, nullptr, 0, &startup, &process, nullptr, nullptr, nullptr, nullptr);
        if (status < 0) return 15;
        if (WaitForSingleObject(process.hProcess, 20000) != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 99); return 16;
        }
        DWORD code; GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        if (code) { printf("CP%u child failed=%lu\n", target, code); return int(code); }
    }
    puts("Private clipboard: direct reads, GlobalLock, Unicode preservation and repeated reads passed.");
    return 0;
}
