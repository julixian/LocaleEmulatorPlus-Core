#include <Windows.h>
#include <stdio.h>
#include <wchar.h>
#include <initializer_list>
import LocaleEmulatorPlus;

struct Event { UINT Message; WPARAM Character; LPARAM Flags; };
static Event Events[32];
static unsigned Count;
static bool Forward;
static bool Capture(UINT m) { return m == WM_CHAR || m == WM_IME_CHAR || m == WM_IME_COMPOSITION; }
static LRESULT CALLBACK AnsiProc(HWND w, UINT m, WPARAM p, LPARAM l)
{
    if (Capture(m)) {
        if (Count < 32) Events[Count++] = {m,p,l};
        if (!Forward || m != WM_IME_CHAR) return 77;
    }
    return DefWindowProcA(w,m,p,l);
}
static LRESULT CALLBACK WideProc(HWND w, UINT m, WPARAM p, LPARAM l)
{
    if (Capture(m)) {
        if (Count < 32) Events[Count++] = {m,p,l};
        return 77;
    }
    return DefWindowProcW(w,m,p,l);
}
static bool One(UINT m, WPARAM p, LPARAM l)
{
    if (Count == 1 && Events[0].Message == m && Events[0].Character == p && Events[0].Flags == l) return true;
    printf("FAIL expected=%04X/%llX count=%u actual=",m,(unsigned long long)p,Count);
    for (unsigned i=0;i<Count;++i) printf("%04X/%llX ",Events[i].Message,(unsigned long long)Events[i].Character);
    puts("");return false;
}
static void Drain(HWND w, bool ansi)
{
    MSG m{};
    for (unsigned i=0;i<32 && (ansi ? PeekMessageA(&m,w,0,0,PM_REMOVE) : PeekMessageW(&m,w,0,0,PM_REMOVE));++i) {
        if (ansi) DispatchMessageA(&m); else DispatchMessageW(&m);
    }
}
static int Child(UINT cp)
{
    WNDCLASSA a{};a.lpfnWndProc=AnsiProc;a.hInstance=GetModuleHandleW(nullptr);a.lpszClassName="LepImeAnsiProbe";
    WNDCLASSW u{};u.lpfnWndProc=WideProc;u.hInstance=a.hInstance;u.lpszClassName=L"LepImeWideProbe";
    if (!RegisterClassA(&a) || !RegisterClassW(&u)) return 2;
    HWND aw=CreateWindowExA(0,a.lpszClassName,"",WS_POPUP,0,0,32,32,nullptr,nullptr,a.hInstance,nullptr);
    HWND uw=CreateWindowExW(0,u.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,u.hInstance,nullptr);
    if (!aw || !uw || GetACP()!=cp) return 3;
    WCHAR ch=cp==1252?L'€':L'中';CHAR bytes[8]{};
    int n=WideCharToMultiByte(cp,0,&ch,1,bytes,sizeof(bytes),nullptr,nullptr);if(n<1||n>2)return 4;
    auto queryDbcs=(BOOL (WINAPI*)())GetProcAddress(GetModuleHandleW(L"user32.dll"),"CheckDBCSEnabledExt");
    if(!queryDbcs || queryDbcs()!=(cp!=1252) || GetSystemMetrics(SM_DBCSENABLED)!=(cp!=1252))return 9;
    WPARAM packed=0;for(int i=0;i<n;++i)packed=(packed<<8)|(BYTE)bytes[i];
    int failures=0;
    using NativeCall=LRESULT (WINAPI*)(HWND,UINT,WPARAM,LPARAM,ULONG_PTR,ULONG,ULONG);
    auto native=(NativeCall)GetProcAddress(GetModuleHandleW(L"win32u.dll"),"NtUserMessageCall");
    UINT messages[]={WM_IME_CHAR,WM_IME_COMPOSITION};
    for (UINT m:messages) {
        LPARAM flags=m==WM_IME_COMPOSITION?0x1c00:0x12340001;
        Count=0;SendMessageW(aw,m,ch,flags);if(!One(m,packed,flags))++failures;
        Count=0;SendMessageA(aw,m,packed,flags);if(!One(m,packed,flags))++failures;
        Count=0;SendMessageA(uw,m,packed,flags);if(!One(m,ch,flags))++failures;
        if(native){Count=0;native(aw,m,packed,flags,0,0x2b1,1);if(!One(m,packed,flags))++failures;}
        for(unsigned route=0;route<4;++route) {
            Count=0;
            if(route&1)PostMessageA(aw,m,packed,flags);else PostMessageW(aw,m,ch,flags);
            Drain(aw,(route&2)!=0);
            if(!One(m,packed,flags)){printf("route=%u msg=%04X\n",route,m);++failures;}
        }
    }
    Count=0;SendMessageA(aw,WM_IME_COMPOSITION,0,0);if(!One(WM_IME_COMPOSITION,0,0))++failures;
    // A default-procedure forward must submit the Unicode character exactly
    // once; both W and A message-pump paths must deliver the target bytes.
    for(bool ansi:{false,true}) {
        Count=0;Forward=true;SendMessageW(aw,WM_IME_CHAR,ch,1);Drain(aw,ansi);Forward=false;
        bool ok=Count==unsigned(n+1) && Events[0].Message==WM_IME_CHAR && Events[0].Character==packed;
        for(int i=0;ok&&i<n;++i)ok=Events[i+1].Message==WM_CHAR && Events[i+1].Character==(BYTE)bytes[i];
        if(!ok){puts("FAIL default IME submission");++failures;}
    }
    DestroyWindow(aw);DestroyWindow(uw);
    printf("IME CP%u arch=%zu failures=%d hostDbcs=%d\n",cp,sizeof(void*)*8,failures,GetSystemMetrics(SM_DBCSENABLED));
    return failures?5:0;
}
int wmain(int argc,wchar_t**argv)
{
    if(argc==3&&!wcscmp(argv[1],L"--child"))return Child(_wtoi(argv[2]));
    if(argc!=2)return 1;
    HMODULE dll=LoadLibraryW(argv[1]);auto create=dll?(LEP::LepCreateProcess2_t)GetProcAddress(dll,"LepCreateProcess2"):nullptr;
    if(!create)return 6;
    WCHAR exe[MAX_PATH],command[1024];GetModuleFileNameW(nullptr,exe,MAX_PATH);
    int parentDbcs=GetSystemMetrics(SM_DBCSENABLED);
    UINT codepages[]={936,932,1252};
    for(UINT cp:codepages){
        LEP::LEPB e{};e.Size=sizeof(e);e.Version=4;e.AnsiCodePage=e.OemCodePage=cp;e.LocaleId=cp==936?0x804:cp==932?0x411:0x409;
        e.FontCharset=cp==936?GB2312_CHARSET:cp==932?SHIFTJIS_CHARSET:ANSI_CHARSET;e.RegistryRedirectionMode=2;e.HookUILanguageMode=2;
        wcscpy_s(e.TimezoneId,cp==936?L"China Standard Time":cp==932?L"Tokyo Standard Time":L"Pacific Standard Time");
        swprintf_s(command,L"\"%s\" --child %u",exe,cp);STARTUPINFOW s{sizeof(s)};PROCESS_INFORMATION p{};
        if(create(&e,exe,command,nullptr,0,&s,&p,nullptr,nullptr,nullptr,nullptr)<0)return 7;
        if(WaitForSingleObject(p.hProcess,20000)!=WAIT_OBJECT_0){TerminateProcess(p.hProcess,99);return 8;}
        DWORD code;GetExitCodeProcess(p.hProcess,&code);CloseHandle(p.hThread);CloseHandle(p.hProcess);if(code)return int(code);
        if(GetSystemMetrics(SM_DBCSENABLED)!=parentDbcs)return 10;
    }
    return 0;
}
