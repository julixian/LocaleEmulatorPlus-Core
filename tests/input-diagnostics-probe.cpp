#include <Windows.h>
#include <stdio.h>
#include <wchar.h>
import LocaleEmulatorPlus;

static unsigned Characters;
static LRESULT CALLBACK ProbeProc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CHAR) { ++Characters; return 0; }
    if (msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION ||
        msg == WM_IME_ENDCOMPOSITION || msg == WM_PASTE) return 0;
    return DefWindowProcA(window, msg, wp, lp);
}

static int Child(PCWSTR directory)
{
    WNDCLASSA cls{};
    cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = ProbeProc;
    cls.lpszClassName = "LepInputDiagnosticsSelfTest";
    if (!RegisterClassA(&cls)) return 2;
    HWND window = CreateWindowExA(0, cls.lpszClassName, "", WS_POPUP, 0,0,100,30,
        nullptr,nullptr,cls.hInstance,nullptr);
    HWND edit = CreateWindowExA(0,"EDIT","",WS_POPUP,0,0,100,30,nullptr,nullptr,cls.hInstance,nullptr);
    if (!window || !edit) return 3;
    SendMessageW(window,WM_CHAR,L'B',1); // Before arming: must not enter the trace.
    Characters = 0;
    WCHAR marker[1024];
    swprintf_s(marker,L"%s\\Log\\input-trace.%lu.enable",directory,GetCurrentProcessId());
    HANDLE file = CreateFileW(marker,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return 4;
    CloseHandle(file);
    printf("diagnostic selftest PID=%lu ACP=%u DBCS=%d\n",GetCurrentProcessId(),GetACP(),GetSystemMetrics(SM_DBCSENABLED));
    SendMessageW(window,WM_IME_STARTCOMPOSITION,0,0);
    SendMessageW(window,WM_IME_COMPOSITION,0,0x800);
    SendMessageW(window,WM_CHAR,0x4E2D,1);
    PostMessageW(window,WM_CHAR,L'A',1);
    MSG queued{};
    if (!PeekMessageA(&queued,window,WM_CHAR,WM_CHAR,PM_REMOVE)) return 9;
    SendMessageW(window,WM_IME_ENDCOMPOSITION,0,0);
    SendMessageW(window,WM_PASTE,0,0); // Our own procedure consumes this; no paste is performed.
    SendMessageW(edit,WM_CHAR,0x4E2D,1);
    // Intentionally fail without OpenClipboard. This exercises logging without
    // obtaining clipboard contents or changing clipboard ownership/data.
    SetLastError(ERROR_SUCCESS);
    HANDLE data = GetClipboardData(CF_TEXT);
    DWORD error = GetLastError();
    printf("unopened clipboard handle=%p error=%lu characters=%u\n",data,error,Characters);
    bool ok = !data && error == ERROR_CLIPBOARD_NOT_OPEN && Characters == 2 && queued.wParam == L'A';
    DeleteFileW(marker);
    SendMessageW(window,WM_CHAR,L'C',1); // After disarming: must not enter the trace.
    DestroyWindow(edit); DestroyWindow(window);
    return ok ? 0 : 5;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && !wcscmp(argv[1],L"--child")) return Child(argv[2]);
    if (argc != 2) return 1;
    HMODULE loader = LoadLibraryW(argv[1]);
    auto create = loader ? reinterpret_cast<LEP::LepCreateProcess2_t>(GetProcAddress(loader,"LepCreateProcess2")) : nullptr;
    if (!create) return 6;
    WCHAR executable[MAX_PATH],directory[MAX_PATH],command[1024];
    GetModuleFileNameW(nullptr,executable,MAX_PATH);
    wcscpy_s(directory,executable); *wcsrchr(directory,L'\\')=0;
    swprintf_s(command,L"\"%s\" --child \"%s\"",executable,directory);
    LEP::LEPB environment{};
    environment.Size=sizeof(environment); environment.Version=LEP::LEP_ENVIRONMENT_VERSION;
    environment.AnsiCodePage=environment.OemCodePage=936;
    environment.LocaleId=0x804; environment.FontCharset=GB2312_CHARSET;
    environment.RegistryRedirectionMode=2; environment.HookUILanguageMode=2;
    wcscpy_s(environment.TimezoneId,L"China Standard Time");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    LONG status=create(&environment,executable,command,directory,0,&startup,&process,nullptr,nullptr,nullptr,nullptr);
    if (status<0) { printf("launch failed %08lX\n",ULONG(status)); return 7; }
    if (WaitForSingleObject(process.hProcess,20000)!=WAIT_OBJECT_0) { TerminateProcess(process.hProcess,99); return 8; }
    DWORD code; GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    printf("diagnostic child exit=%lu\n",code);
    return int(code);
}
