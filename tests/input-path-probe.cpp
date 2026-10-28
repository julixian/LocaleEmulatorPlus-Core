#include <Windows.h>
#include <stdio.h>
#include <wchar.h>
#include <initializer_list>
import LocaleEmulatorPlus;

// Synthetic committed-character messages, not a real IME composition session.
static WPARAM Received[16];
static unsigned ReceivedCount;
static LPARAM ReceivedParams[16];
static UINT ReceivedMessages[16];
static constexpr LRESULT CharacterResult = 0x12345678;
static constexpr LPARAM CharacterFlags = 0x21340003;
static bool IsCharacterMessage(UINT message)
{
    return message == WM_CHAR || message == WM_IME_CHAR || message == WM_SYSCHAR ||
        message == WM_DEADCHAR || message == WM_SYSDEADCHAR;
}
static LRESULT RecordCharacter(UINT message, WPARAM wp, LPARAM lp)
{
    if (ReceivedCount < _countof(Received)) {
        ReceivedParams[ReceivedCount] = lp;
        ReceivedMessages[ReceivedCount] = message;
        Received[ReceivedCount++] = wp;
    }
    return CharacterResult;
}
static LRESULT CALLBACK AnsiProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (IsCharacterMessage(message)) return RecordCharacter(message, wp, lp);
    return DefWindowProcA(window, message, wp, lp);
}
static LRESULT CALLBACK UnicodeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (IsCharacterMessage(message)) return RecordCharacter(message, wp, lp);
    return DefWindowProcW(window, message, wp, lp);
}
static bool SubclassCalled;
static WNDPROC PreviousAnsiProc;
static WNDPROC PreviousEditProc;
static ULONG EditCallbackMask;
static HWND NestedEdit;
static HWND OtherWindow;
static wchar_t NestedCharacter;
static BYTE ExpectedTrail;
static bool CheckPendingTrail;
static unsigned TrailFailures;
static LRESULT CALLBACK EditSubclassProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
        case WM_CHAR: EditCallbackMask |= 1; break;
        case WM_SETTEXT: EditCallbackMask |= 2; break;
        case WM_GETTEXT: EditCallbackMask |= 4; break;
        case EM_REPLACESEL: EditCallbackMask |= 8; break;
        case EM_GETSEL: EditCallbackMask |= 16; break;
    }
    if (message == WM_CHAR && CheckPendingTrail) {
        CheckPendingTrail = false;
        MSG pending{};
        if (PeekMessageA(&pending, OtherWindow, WM_CHAR, WM_CHAR, PM_NOREMOVE) ||
            PeekMessageA(&pending, window, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE)) ++TrailFailures;
        if (!PeekMessageA(&pending, window, WM_CHAR, WM_CHAR, PM_NOREMOVE) ||
            pending.hwnd != window || pending.wParam != ExpectedTrail || pending.lParam != lp) ++TrailFailures;
        SendMessageW(NestedEdit, WM_CHAR, NestedCharacter, 1);
        if (!PeekMessageA(&pending, window, WM_CHAR, WM_CHAR, PM_NOREMOVE) || pending.wParam != ExpectedTrail) ++TrailFailures;
    }
    return CallWindowProcA(PreviousEditProc, window, message, wp, lp);
}
static LRESULT CALLBACK SubclassProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (IsCharacterMessage(message)) SubclassCalled = true;
    return CallWindowProcA(PreviousAnsiProc, window, message, wp, lp);
}
static void DrainMessages(HWND window, bool ansi)
{
    MSG msg{};
    while (ansi ? PeekMessageA(&msg, window, 0, 0, PM_REMOVE) : PeekMessageW(&msg, window, 0, 0, PM_REMOVE)) {
        if (ansi) DispatchMessageA(&msg); else DispatchMessageW(&msg);
    }
}
static bool CheckReceived(PCSTR path, UINT message, const char* expected, int bytes)
{
    WPARAM packed = 0;
    for (int i = 0; i < bytes; ++i) packed = (packed << 8) | (unsigned char)expected[i];
    bool match = ReceivedCount == unsigned(message == WM_CHAR ? bytes : 1);
    for (unsigned i = 0; match && i < ReceivedCount; ++i) {
        match = Received[i] == (message == WM_CHAR ? (unsigned char)expected[i] : packed) &&
            ReceivedParams[i] == CharacterFlags && ReceivedMessages[i] == message;
    }
    if (!match) {
        printf("FAIL %s message=%04X received:", path, message);
        for (unsigned i = 0; i < ReceivedCount; ++i) printf(" %04llX", (unsigned long long)Received[i]);
        puts("");
    }
    return match;
}
static void PrintBytes(const char* data, size_t count)
{
    for (size_t i = 0; i < count; ++i) printf("%s%02X", i ? " " : "", (unsigned char)data[i]);
}
static int Probe(UINT target, bool emulated)
{
    printf("ACP=%u OEM=%u target=%u arch=%u\n", GetACP(), GetOEMCP(), target, unsigned(sizeof(void*) * 8));
    if (GetACP() != target) return 20;
    WNDCLASSA cls{};
    cls.lpfnWndProc = AnsiProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = "LepInputPathProbe";
    if (!RegisterClassA(&cls)) return 21;
    HWND window = CreateWindowExA(0, cls.lpszClassName, "probe", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) return 22;
    int failures = 0;
    WNDCLASSW wideClass{};
    wideClass.lpfnWndProc = UnicodeProc;
    wideClass.hInstance = cls.hInstance;
    wideClass.lpszClassName = L"LepUnicodeInputProbe";
    if (!RegisterClassW(&wideClass)) return 25;
    HWND wideWindow = CreateWindowExW(0, wideClass.lpszClassName, L"probe", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
    if (!wideWindow) return 26;
    wchar_t primary = target == 1252 ? L'\x20ac' : L'\x4e2d';
    for (wchar_t character : {primary, L'A', L'\x2603', L'\0', L'\xe9'}) {
        char expected[16]{};
        int expectedLength = WideCharToMultiByte(target, 0, &character, 1, expected, sizeof(expected), nullptr, nullptr);
        if (expectedLength < 1 || expectedLength > 2) return 27;
        printf("sample U+%04X -> ", unsigned(character)); PrintBytes(expected, expectedLength); puts("");
        for (UINT message : {UINT(WM_CHAR), UINT(WM_IME_CHAR), UINT(WM_SYSCHAR), UINT(WM_DEADCHAR), UINT(WM_SYSDEADCHAR)}) {
            ReceivedCount = 0;
            LRESULT result = SendMessageW(window, message, character, CharacterFlags);
            if (!CheckReceived("SendMessageW", message, expected, expectedLength) || result != CharacterResult) ++failures;
            // ASCII ANSI callers exercise the reverse path without relying
            // on the host input layout's separate non-ASCII conversion.
            if (expectedLength == 1 && (unsigned char)expected[0] < 0x80) {
                ReceivedCount = 0;
                SendMessageA(window, message, (unsigned char)expected[0], CharacterFlags);
                if (!CheckReceived("SendMessageA", message, expected, expectedLength)) ++failures;
            }
            ReceivedCount = 0;
            result = SendMessageW(wideWindow, message, character, CharacterFlags);
            if (ReceivedCount != 1 || Received[0] != character || ReceivedParams[0] != CharacterFlags || result != CharacterResult) {
                puts("FAIL Unicode window"); ++failures;
            }
        }
        for (bool ansiDispatch : {false, true}) {
            ReceivedCount = 0;
            PostMessageW(window, WM_CHAR, character, CharacterFlags);
            DrainMessages(window, ansiDispatch);
            if (!CheckReceived(ansiDispatch ? "PostW/DispatchA" : "PostW/DispatchW", WM_CHAR, expected, expectedLength)) ++failures;
            if (expectedLength == 1 && (unsigned char)expected[0] < 0x80) {
                ReceivedCount = 0;
                PostMessageA(window, WM_CHAR, (unsigned char)expected[0], CharacterFlags);
                DrainMessages(window, ansiDispatch);
                if (!CheckReceived(ansiDispatch ? "PostA/DispatchA" : "PostA/DispatchW", WM_CHAR, expected, expectedLength)) ++failures;
            }
        }
    }
    HWND edit = CreateWindowExA(0, "EDIT", "", WS_POPUP | ES_AUTOHSCROLL, 0, 0, 100, 30, nullptr, nullptr, cls.hInstance, nullptr);
    if (!edit) return 23;
    for (wchar_t character : {primary, L'\xe9', L'\xff83'}) {
        char expected[16]{};
        BOOL substituted = FALSE;
        int expectedLength = WideCharToMultiByte(target, WC_NO_BEST_FIT_CHARS, &character, 1, expected, sizeof(expected), nullptr, &substituted);
        if (!expectedLength || substituted) continue;
        SetWindowTextW(edit, L"");
        // The synchronous DBCS character must not steal this actual queued
        // WM_CHAR while the ANSI EDIT combines its lead and trail bytes.
        if (!PostMessageW(edit, WM_CHAR, L'Z', 0x4321)) return 29;
        SendMessageW(edit, WM_CHAR, character, 1);
        MSG queued{};
        if (!PeekMessageW(&queued, edit, WM_CHAR, WM_CHAR, PM_REMOVE) || queued.wParam != L'Z' || queued.lParam != 0x4321) {
            puts("FAIL EDITA consumed an unrelated queued character"); ++failures;
        }
        char text[64]{};
        int length = GetWindowTextA(edit, text, sizeof(text));
        printf("EDITA U+%04X text=", unsigned(character)); PrintBytes(text, length); puts("");
        if (length != expectedLength || memcmp(text, expected, expectedLength) != 0) ++failures;
        if (!emulated) continue;
        wchar_t wideText[64]{};
        if (GetWindowTextW(edit, wideText, _countof(wideText)) != 1 || wideText[0] != character) ++failures;
        SendMessageW(edit, WM_CHAR, L'A', 1);
        SendMessageW(edit, EM_SETSEL, 0, 1);
        DWORD start = ~0u, end = ~0u;
        SendMessageW(edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        if (start != 0 || end != 1) { printf("FAIL EDITA Unicode selection=%lu/%lu\n", start, end); ++failures; }
        using NativeMessageCall = LRESULT (WINAPI*)(HWND, UINT, WPARAM, LPARAM, ULONG_PTR, ULONG, ULONG);
        auto nativeCall = (NativeMessageCall)GetProcAddress(GetModuleHandleW(L"win32u.dll"), "NtUserMessageCall");
        if (!nativeCall) return 32;
        // Exercise the existing A->W boundary directly, independently of
        // user32's same-thread SendMessageA fast paths.
        nativeCall(edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end, 0, 0x2b1, 1);
        if (start != 0 || end != unsigned(expectedLength)) { puts("FAIL EDITA kernel ANSI selection"); ++failures; }
        nativeCall(edit, EM_SETSEL, expectedLength, expectedLength + 1, 0, 0x2b1, 1);
        SendMessageW(edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        if (start != 1 || end != 2) { printf("FAIL EDITA Unicode selection=%lu/%lu\n", start, end); ++failures; }
        SendMessageW(edit, WM_CHAR, L'B', 1);
        if (GetWindowTextW(edit, wideText, _countof(wideText)) != 2 || wideText[0] != character || wideText[1] != L'B') {
            printf("FAIL EDITA replace selection=%04X %04X %04X\n", unsigned(wideText[0]), unsigned(wideText[1]), unsigned(wideText[2])); ++failures;
        }
    }
    PreviousEditProc = (WNDPROC)SetWindowLongPtrA(edit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    if (!PreviousEditProc) return 31;
    char encodedPrimary[16]{};
    int primaryLength = WideCharToMultiByte(target, 0, &primary, 1, encodedPrimary, sizeof(encodedPrimary), nullptr, nullptr);
    if (emulated && primaryLength == 2) {
        NestedEdit = CreateWindowExA(0, "EDIT", "", WS_POPUP, 0, 0, 100, 30, nullptr, nullptr, cls.hInstance, nullptr);
        if (!NestedEdit) return 33;
        OtherWindow = wideWindow;
        NestedCharacter = primary;
        ExpectedTrail = BYTE(encodedPrimary[1]);
        CheckPendingTrail = true;
    }
    SetWindowTextW(edit, L"");
    SendMessageW(edit, WM_CHAR, primary, 1);
    SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)L"A");
    wchar_t subclassText[32]{};
    int subclassLength = GetWindowTextW(edit, subclassText, _countof(subclassText));
    SendMessageW(edit, EM_GETSEL, 0, 0);
    if (subclassLength != 2 || subclassText[0] != primary || subclassText[1] != L'A' || EditCallbackMask != 31) {
        printf("FAIL EDITA subclass text=%04X/%04X mask=%lX\n", unsigned(subclassText[0]), unsigned(subclassText[1]), EditCallbackMask);
        ++failures;
    }
    if (NestedEdit) {
        wchar_t nestedText[16]{};
        if (TrailFailures || GetWindowTextW(NestedEdit, nestedText, _countof(nestedText)) != 1 || nestedText[0] != primary) {
            puts("FAIL EDITA pending trail filtering or reentrancy"); ++failures;
        }
        DestroyWindow(NestedEdit);
    }
    MSG leftover{};
    if (PeekMessageA(&leftover, edit, WM_CHAR, WM_CHAR, PM_NOREMOVE)) { puts("FAIL EDITA leaked a pending trail"); ++failures; }
    SetWindowLongPtrA(edit, GWLP_WNDPROC, (LONG_PTR)PreviousEditProc);
    char expected[16]{};
    int expectedLength = WideCharToMultiByte(target, 0, &primary, 1, expected, sizeof(expected), nullptr, nullptr);
    PreviousAnsiProc = (WNDPROC)SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
    if (!PreviousAnsiProc) return 28;
    ReceivedCount = 0;
    SendMessageW(window, WM_CHAR, primary, CharacterFlags);
    if (!SubclassCalled || !CheckReceived("ANSI subclass", WM_CHAR, expected, expectedLength)) ++failures;
    SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)PreviousAnsiProc);
    DestroyWindow(edit);
    DestroyWindow(window);
    DestroyWindow(wideWindow);
    printf("character failures=%d\n", failures);
    return failures ? 30 : 0;
}
int wmain(int argc, wchar_t** argv)
{
    if (argc == 4 && wcscmp(argv[1], L"--child") == 0) return Probe(_wtoi(argv[2]), _wtoi(argv[3]) != 0);
    if (argc != 2) return 1;
    HMODULE loader = LoadLibraryW(argv[1]);
    auto create = loader ? reinterpret_cast<LEP::LepCreateProcess2_t>(GetProcAddress(loader, "LepCreateProcess2")) : nullptr;
    if (!create) return 5;
    wchar_t executable[MAX_PATH], directory[MAX_PATH];
    GetModuleFileNameW(nullptr, executable, _countof(executable));
    wcscpy_s(directory, executable); *wcsrchr(directory, L'\\') = 0;
    int result = 0;
    for (UINT target : {0u, 936u, 932u, 1252u}) {
        UINT actualTarget = target ? target : GetACP();
        printf("\n=== %s cp=%u ===\n", target ? "LEP" : "NATIVE", actualTarget); fflush(stdout);
        wchar_t command[1024];
        swprintf_s(command, L"\"%s\" --child %u %u", executable, actualTarget, target ? 1u : 0u);
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (target) {
            LEP::LEPB environment{};
            environment.Size = sizeof(environment); environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
            environment.AnsiCodePage = environment.OemCodePage = target;
            environment.LocaleId = target == 932 ? 0x411 : target == 936 ? 0x804 : 0x409;
            environment.FontCharset = target == 932 ? SHIFTJIS_CHARSET : target == 936 ? GB2312_CHARSET : ANSI_CHARSET;
            environment.RegistryRedirectionMode = 2; environment.HookUILanguageMode = 2;
            wcscpy_s(environment.TimezoneId, target == 932 ? L"Tokyo Standard Time" : target == 936 ? L"China Standard Time" : L"Pacific Standard Time");
            LONG status = create(&environment, executable, command, directory, 0, &startup, &process, nullptr, nullptr, nullptr, nullptr);
            if (status < 0) { printf("launch status=%08lX\n", ULONG(status)); result = 7; continue; }
        } else if (!CreateProcessW(executable, command, nullptr, nullptr, FALSE, 0, nullptr, directory, &startup, &process)) return 8;
        if (WaitForSingleObject(process.hProcess, 20000) != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 99); result = 9; }
        DWORD code = 0; GetExitCodeProcess(process.hProcess, &code);
        printf("child exit=%lu\n", code); if (code) result = 10;
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
    FreeLibrary(loader);
    return result;
}
