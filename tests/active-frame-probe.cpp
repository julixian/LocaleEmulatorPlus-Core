#include "../LocaleEmulatorPlus/stdafx.h"
#include <assert.h>
#include <stdio.h>
#include <type_traits>

static_assert(std::is_standard_layout<lep::ActiveFrame>::value, "Native prefix must have a stable offset");
static_assert(std::is_trivial<TEB_ACTIVE_FRAME>::value, "Native structure must not own C++ lifetime state");
static_assert(!std::is_copy_constructible<lep::ActiveFrame>::value, "Linked frames must not be copied");
static_assert(!std::is_move_constructible<lep::ActiveFrame>::value, "Linked frames must keep their address");
static_assert(sizeof(THREAD_LOCAL_BUFFER) < 256, "Disabled version-resource buffer must not be allocated");

constexpr ULONG Kind = TAG4('TEST');

static BOOL WINAPI FindFromCallback(ULONG FrameKind, ULONG_PTR Data)
{
    auto Frame = lep::ActiveFrame::Find(FrameKind);
    return Frame != nullptr && Frame->Data == Data;
}

static DWORD WINAPI OtherThread(PVOID)
{
    assert(lep::ActiveFrame::Find(Kind) == nullptr);
    auto Before = RtlGetFrame();
    {
        lep::ActiveFrame Frame(Kind);
        Frame.Data = 0x4567;
        Frame.Push();
        assert(lep::ActiveFrame::Find(Kind) == &Frame);
        assert(Frame.Native.Previous == Before);
    }
    assert(RtlGetFrame() == Before);
    return 0;
}

static int WindowChild()
{
    if (GetACP() != 932)
        return 10;
    HWND Parent = CreateWindowExW(0, L"STATIC", L"LEP frame probe", WS_POPUP,
        0, 0, 160, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!Parent)
        return 11;
    HWND EditA = CreateWindowExA(0, "EDIT", "\x82\xa0\x82\xa2", WS_CHILD,
        0, 0, 80, 20, Parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND EditW = CreateWindowExW(0, L"EDIT", L"あい", WS_CHILD,
        0, 20, 80, 20, Parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    char TextA[32]{};
    wchar_t TextW[32]{};
    bool Success = EditA && EditW && GetWindowTextA(EditA, TextA, sizeof(TextA)) == 4 &&
        GetWindowTextW(EditW, TextW, _countof(TextW)) == 2 &&
        memcmp(TextA, "\x82\xa0\x82\xa2", 5) == 0 && wcscmp(TextW, L"あい") == 0;
    DestroyWindow(Parent);
    return Success ? 0 : 12;
}

static int LaunchWindowChild(PCWSTR LoaderPath, PCWSTR ExportName)
{
    HMODULE Loader = LoadLibraryW(LoaderPath);
    if (!Loader)
        return 20;
    char Name[64];
    sprintf_s(Name, sizeof(Name), "%ls", ExportName);
    using Create = NTSTATUS (WINAPI*)(PLEPB, PCWSTR, PWSTR, PCWSTR, ULONG,
        LPSTARTUPINFOW, LPPROCESS_INFORMATION, LPSECURITY_ATTRIBUTES,
        LPSECURITY_ATTRIBUTES, PVOID, HANDLE);
    auto Routine = reinterpret_cast<Create>(GetProcAddress(Loader, Name));
    if (!Routine)
        return 21;
    LEPB Environment{};
    Environment.Size = sizeof(Environment);
    Environment.Version = LEP_ENVIRONMENT_VERSION;
    Environment.AnsiCodePage = Environment.OemCodePage = 932;
    Environment.LocaleId = 0x411;
    Environment.FontCharset = SHIFTJIS_CHARSET;
    Environment.RegistryRedirectionMode = 2;
    Environment.HookUILanguageMode = 1;
    wcscpy_s(Environment.TimezoneId, _countof(Environment.TimezoneId), L"Tokyo Standard Time");
    wchar_t Executable[MAX_PATH], Command[2 * MAX_PATH];
    GetModuleFileNameW(nullptr, Executable, _countof(Executable));
    swprintf_s(Command, _countof(Command), L"\"%s\" --window-child", Executable);
    STARTUPINFOW Startup{};
    Startup.cb = sizeof(Startup);
    PROCESS_INFORMATION Process{};
    NTSTATUS Status = Routine(&Environment, Executable, Command, nullptr,
        CREATE_NO_WINDOW, &Startup, &Process, nullptr, nullptr, nullptr, nullptr);
    if (Status < 0)
        return 22;
    DWORD Code = 90;
    if (WaitForSingleObject(Process.hProcess, 15000) == WAIT_OBJECT_0)
        GetExitCodeProcess(Process.hProcess, &Code);
    else
        TerminateProcess(Process.hProcess, Code);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    printf("%s: hidden ANSI/Unicode EDIT child=%lu\n", Name, Code);
    return int(Code);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 2 && wcscmp(argv[1], L"--window-child") == 0)
        return WindowChild();
    if (argc == 4 && wcscmp(argv[1], L"--launch-window") == 0)
        return LaunchWindowChild(argv[2], argv[3]);
    if (argc != 2)
        return 1;
    HMODULE Module = LoadLibraryW(argv[1]);
    assert(Module != nullptr);
    auto FindInModule = reinterpret_cast<ULONG_PTR (WINAPI*)(ULONG)>(GetProcAddress(Module, "ProbeFindFrameData"));
    auto ModuleDescriptor = reinterpret_cast<PTEB_ACTIVE_FRAME_CONTEXT (WINAPI*)()>(GetProcAddress(Module, "ProbeDescriptor"));
    auto WithModuleFrame = reinterpret_cast<BOOL (WINAPI*)(ULONG, ULONG_PTR, BOOL (WINAPI*)(ULONG, ULONG_PTR))>(GetProcAddress(Module, "ProbeWithFrame"));
    assert(FindInModule && ModuleDescriptor && WithModuleFrame);

    auto Before = RtlGetFrame();
    TEB_ACTIVE_FRAME_CONTEXT ForeignContext = {0, "External.Native.Frame"};
    TEB_ACTIVE_FRAME Foreign = {0, nullptr, &ForeignContext};
    RtlPushFrame(&Foreign);
    assert(lep::ActiveFrame::Find(Kind) == nullptr);
    {
        lep::ActiveFrame Outer(Kind);
        Outer.Data = 0x1234;
        assert(Outer.Native.Flags == 0 && Outer.Native.Context != nullptr);
        assert(Outer.Native.Context->Flags == 0 && Outer.Native.Context->FrameName != nullptr);
        assert(ModuleDescriptor() != Outer.Native.Context);
        Outer.Push();
        Outer.Push(); // Repeated pushes must not make Previous point to self.
        assert(Outer.Native.Previous == &Foreign);
        assert(RtlGetFrame() == &Outer.Native);
        assert(lep::ActiveFrame::Find(Kind) == &Outer);
        assert(FindInModule(Kind) == Outer.Data);
        assert(WithModuleFrame(Kind, 0x5678, FindFromCallback));
        assert(RtlGetFrame() == &Outer.Native);

        {
            lep::ActiveFrame Inner(Kind);
            Inner.Data = 0x2345;
            Inner.Push();
            assert(lep::ActiveFrame::Find(Kind) == &Inner);
            assert(FindInModule(Kind) == Inner.Data);
            TEB_ACTIVE_FRAME Plain = {0, nullptr, nullptr};
            RtlPushFrame(&Plain);
            assert(lep::ActiveFrame::Find(Kind) == &Inner);
            RtlPopFrame(&Plain);
            Inner.Pop();
            Inner.Pop();
            assert(RtlGetFrame() == &Outer.Native);
        }
        assert(lep::ActiveFrame::Find(Kind) == &Outer);
        Outer.Version = 99;
        assert(lep::ActiveFrame::Find(Kind) == nullptr);
        Outer.Version = 1;
        ULONG Size = Outer.Size;
        Outer.Size = 0;
        assert(lep::ActiveFrame::Find(Kind) == nullptr);
        Outer.Size = Size;

        HANDLE Thread = ::CreateThread(nullptr, 0, OtherThread, nullptr, 0, nullptr);
        assert(Thread != nullptr);
        assert(WaitForSingleObject(Thread, 15000) == WAIT_OBJECT_0);
        DWORD Code;
        assert(GetExitCodeThread(Thread, &Code) && Code == 0);
        CloseHandle(Thread);
        assert(lep::ActiveFrame::Find(Kind) == &Outer);
    }
    assert(RtlGetFrame() == &Foreign);
    RtlPopFrame(&Foreign);
    assert(RtlGetFrame() == Before);
    FreeLibrary(Module);
    printf("Native frame layout, foreign nodes, nesting, thread isolation and cross-DLL lookup passed; thread buffer=%Iu bytes\n", sizeof(THREAD_LOCAL_BUFFER));
    return 0;
}
