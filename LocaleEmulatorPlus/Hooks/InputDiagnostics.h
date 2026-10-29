#pragma once

#include "../LocaleEmulatorPlus.h"

// These observations run only in existing hooks. They install no additional
// hooks, query no IME composition state, and never change USER32 server flags.
#if LEP_DIAG_INPUT

namespace lep { namespace input_diag {
    static const ULONG Context = TAG4('IDIA');
    static WCHAR ArmPath[MAX_NTPATH];
    static LONG PathState;
    static LONG EventCount;
    static LONG SnapshotWritten;

    struct Guard
    {
        ULONG Error;
        ActiveFrame Frame;
        BOOL Entered;

        Guard() : Error(RtlGetLastWin32Error()), Frame(Context), Entered(FALSE)
        {
            if (ActiveFrame::Find(Context) == nullptr)
            {
                Frame.Push();
                Entered = TRUE;
            }
        }

        ~Guard()
        {
            Frame.Pop();
            RtlSetLastWin32Error(Error);
        }
    };

    static PCWSTR GetArmPath()
    {
        if (InterlockedCompareExchange(&PathState, 1, 0) == 0)
        {
            PCWSTR Directory = LepGetGlobalData()->GetLepDllDirPath();
            ULONG_PTR Length = Directory == nullptr ? 0 : StrLengthW(Directory);
            if (Length != 0 && Length < countof(ArmPath) - 80)
            {
                CopyMemory(ArmPath, Directory, Length * sizeof(WCHAR));
                if (ArmPath[Length - 1] != L'\\')
                    ArmPath[Length++] = L'\\';
                const WCHAR Prefix[] = L"Log\\input-trace.";
                CopyMemory(ArmPath + Length, Prefix, sizeof(Prefix) - sizeof(WCHAR));
                Length += countof(Prefix) - 1;
                WCHAR Number[32];
                FormatLepUIntDecimal(Number, CurrentPid());
                ULONG_PTR Digits = StrLengthW(Number);
                CopyMemory(ArmPath + Length, Number, Digits * sizeof(WCHAR));
                Length += Digits;
                CopyMemory(ArmPath + Length, L".enable", sizeof(L".enable"));
            }
            InterlockedExchange(&PathState, 2);
        }
        return InterlockedCompareExchange(&PathState, 2, 2) == 2 && ArmPath[0] ? ArmPath : nullptr;
    }

    static LONG TakeEvent()
    {
        PCWSTR Path = GetArmPath();
        if (Path == nullptr || Nt_GetFileAttributes(Path) == INVALID_FILE_ATTRIBUTES)
            return 0;
        LONG Sequence = InterlockedIncrement(&EventCount);
        if (Sequence == 2049)
            WriteLog(L"inputdiag limit=2048 reached; restart process for a new capture");
        return Sequence <= 2048 ? Sequence : 0;
    }

    static VOID Snapshot(HWND Window)
    {
        Guard Scope;
        if (!Scope.Entered || Window == nullptr || InterlockedCompareExchange(&SnapshotWritten, 1, 0) != 0)
            return;
        auto Data = LepGetGlobalData();
        typedef BOOL (WINAPI *CHECK_DBCS)();
        auto Check = (CHECK_DBCS)GetProcAddress(GetModuleHandleW(L"user32.dll"), "CheckDBCSEnabledExt");
        WCHAR Class[96] = {};
        GetClassNameW(Window, Class, countof(Class));
        PCWSTR Path = GetArmPath();
        WriteLog(L"inputdiag-ready version=1 pid=%u tid=%u arch=%u targetAcp=%u observedAcp=%u ntdllAcp=%u nlsDbcs=%u smDbcs=%d checkDbcs=%d hkl=%p hwnd=%p class=%ws trackedA=%u nativeUnicode=%u arm=%ws",
                 (ULONG)CurrentPid(), (ULONG)CurrentTid(), (ULONG)(sizeof(PVOID) * 8),
                 Data->GetEnvironment()->AnsiCodePage, GetACP(), (ULONG)NlsAnsiCodePage, (ULONG)NlsMbCodePageTag,
                 GetSystemMetrics(SM_DBCSENABLED), Check ? Check() : -1, GetKeyboardLayout(0),
                 Window, Class, Data->GetWindowDataA(Window) != nullptr,
                 Data->IsWindowUnicode(Window), Path ? Path : L"unavailable");
    }

    static BOOL IsInputMessage(UINT Message)
    {
        return Message == WM_CHAR || Message == WM_IME_CHAR || Message == WM_SYSCHAR ||
               Message == WM_DEADCHAR || Message == WM_SYSDEADCHAR || Message == WM_UNICHAR ||
               Message == WM_IME_STARTCOMPOSITION || Message == WM_IME_ENDCOMPOSITION ||
               Message == WM_IME_COMPOSITION || Message == WM_IME_SETCONTEXT ||
               Message == WM_INPUTLANGCHANGE || Message == WM_PASTE;
    }

    static VOID Message(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam, WNDPROC Previous)
    {
        if (!IsInputMessage(Message))
            return;
        Guard Scope;
        LONG Sequence = Scope.Entered ? TakeEvent() : 0;
        if (Sequence == 0)
            return;
        WCHAR Class[96] = {};
        GetClassNameW(Window, Class, countof(Class));
        WriteLog(L"inputdiag seq=%u tick=%u pid=%u tid=%u event=message hwnd=%p class=%ws msg=%04X wp=%p lp=%p prev=%p smDbcs=%d hkl=%p",
                 Sequence, GetTickCount(), (ULONG)CurrentPid(), (ULONG)CurrentTid(), Window, Class,
                 Message, (PVOID)WParam, (PVOID)LParam, Previous,
                 GetSystemMetrics(SM_DBCSENABLED), GetKeyboardLayout(0));
    }

    static VOID Character(PCWSTR Phase, HWND Window, UINT Message, WCHAR Unicode,
                          const CHAR* Ansi, ULONG Bytes, LONG Consumed)
    {
        Guard Scope;
        LONG Sequence = Scope.Entered ? TakeEvent() : 0;
        if (Sequence == 0)
            return;
        WCHAR Hex[16] = {};
        static const WCHAR Digits[] = L"0123456789ABCDEF";
        for (ULONG i = 0; i < Bytes && i < 4; ++i)
        {
            Hex[i * 3] = Digits[(BYTE)Ansi[i] >> 4];
            Hex[i * 3 + 1] = Digits[(BYTE)Ansi[i] & 15];
            Hex[i * 3 + 2] = L' ';
        }
        WriteLog(L"inputdiag seq=%u tick=%u pid=%u tid=%u event=%ws hwnd=%p msg=%04X unicode=%04X bytes=%u hex=%ws consumed=%d",
                 Sequence, GetTickCount(), (ULONG)CurrentPid(), (ULONG)CurrentTid(), Phase,
                 Window, Message, Unicode, Bytes, Hex, Consumed);
    }

    static VOID Clipboard(PCWSTR Phase, UINT Format, HANDLE Data)
    {
        Guard Scope;
        LONG Sequence = Scope.Entered ? TakeEvent() : 0;
        if (Sequence == 0)
            return;
        // Handles and errors only. Actual clipboard text is never copied by the logger.
        WriteLog(L"inputdiag seq=%u tick=%u pid=%u tid=%u event=clipboard phase=%ws format=%u handle=%p lastError=%u",
                 Sequence, GetTickCount(), (ULONG)CurrentPid(), (ULONG)CurrentTid(), Phase,
                 Format, Data, Scope.Error);
    }

    static VOID Peek(HWND Window, UINT Min, UINT Max, UINT Remove, WPARAM Value, PVOID Caller)
    {
        Guard Scope;
        LONG Sequence = Scope.Entered ? TakeEvent() : 0;
        if (Sequence == 0)
            return;
        WriteLog(L"inputdiag seq=%u tick=%u pid=%u tid=%u event=peek-trail hwnd=%p min=%04X max=%04X remove=%08X wp=%p caller=%p",
                 Sequence, GetTickCount(), (ULONG)CurrentPid(), (ULONG)CurrentTid(), Window,
                 Min, Max, Remove, (PVOID)Value, Caller);
    }

    static VOID Queue(LPMSG Msg, BOOL Result, UINT Remove, PVOID Caller)
    {
        if (!Result || Msg == nullptr || !IsInputMessage(Msg->message))
            return;
        Guard Scope;
        LONG Sequence = Scope.Entered ? TakeEvent() : 0;
        if (Sequence == 0)
            return;
        WriteLog(L"inputdiag seq=%u tick=%u pid=%u tid=%u event=peek-native hwnd=%p msg=%04X wp=%p lp=%p remove=%08X caller=%p",
                 Sequence, GetTickCount(), (ULONG)CurrentPid(), (ULONG)CurrentTid(), Msg->hwnd,
                 Msg->message, (PVOID)Msg->wParam, (PVOID)Msg->lParam, Remove, Caller);
    }
}}

#else
namespace lep { namespace input_diag {
    inline VOID Snapshot(HWND) {}
    inline VOID Message(HWND, UINT, WPARAM, LPARAM, WNDPROC) {}
    inline VOID Character(PCWSTR, HWND, UINT, WCHAR, const CHAR*, ULONG, LONG) {}
    inline VOID Clipboard(PCWSTR, UINT, HANDLE) {}
    inline VOID Peek(HWND, UINT, UINT, UINT, WPARAM, PVOID) {}
    inline VOID Queue(LPMSG, BOOL, UINT, PVOID) {}
}}
#endif
