#include "stdafx.h"
#include "MessageTable.h"

#define LEP_DISABLE_ANSI_WNDPROC_WRAP 0

VOID ResetDCCharset(PLepGlobalData GlobalData, HWND hWnd);

ForceInline VOID CheckDC(HDC hDC)
{
    // if (GdiGetCodePage(hDC) == 0x3A4) _asm ud2;
}

ForceInline BOOL IsCallProcHandle(PVOID WndProc)
{
    return ((ULONG_PTR)WndProc & 0xFFFF0000) == 0xFFFF0000;
}

ForceInline WNDPROC GetWindowProcA(PLepGlobalData GlobalData, HWND hWnd)
{
#if ML_AMD64
    return (WNDPROC)GlobalData->GetWindowLongPtrA(hWnd, GWLP_WNDPROC);
#else
    return (WNDPROC)GlobalData->GetWindowLongA(hWnd, GWLP_WNDPROC);
#endif
}

ForceInline VOID InitUnicodeProc(PLepGlobalData GlobalData, HWND hWnd, PVOID UnicodeProc, PVOID OriginalProcA)
{
    SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)UnicodeProc);
    GlobalData->SetWindowDataA(hWnd, OriginalProcA);
    ResetDCCharset(GlobalData, hWnd);
}

ForceInline BOOL IsComboBoxStringMessage(UINT Message)
{
    switch (Message)
    {
        case CB_ADDSTRING:
        case CB_INSERTSTRING:
        case CB_GETLBTEXT:
        case CB_GETLBTEXTLEN:
        case CB_FINDSTRING:
        case CB_SELECTSTRING:
        case CB_FINDSTRINGEXACT:
            return TRUE;
    }

    return FALSE;
}

ForceInline BOOL IsListBoxStringMessage(UINT Message)
{
    switch (Message)
    {
        case LB_ADDSTRING:
        case LB_INSERTSTRING:
        case LB_GETTEXT:
        case LB_GETTEXTLEN:
        case LB_SELECTSTRING:
        case LB_FINDSTRING:
        case LB_FINDSTRINGEXACT:
            return TRUE;
    }

    return FALSE;
}

ForceInline BOOL IsWindowClass(HWND Window, PCWSTR ClassName)
{
    WCHAR WindowClass[16];
    PWSTR CurrentWindowClass;

    if (GetClassNameW(Window, WindowClass, countof(WindowClass)) == 0)
        return FALSE;

    CurrentWindowClass = WindowClass;

    for (;;)
    {
        WCHAR Left = *CurrentWindowClass++;
        WCHAR Right = *ClassName++;

        if (Left >= L'a' && Left <= L'z')
            Left -= L'a' - L'A';

        if (Right >= L'a' && Right <= L'z')
            Right -= L'a' - L'A';

        if (Left != Right)
            return FALSE;

        if (Left == 0)
            return TRUE;
    }
}

ForceInline BOOL IsOwnerDrawItemDataMessage(HWND Window, UINT Message)
{
    LONG_PTR Style;

    if (!IsComboBoxStringMessage(Message) && !IsListBoxStringMessage(Message))
        return FALSE;

    Style = GetWindowLongPtrW(Window, GWL_STYLE);

    if (IsComboBoxStringMessage(Message))
    {
        if (!IsWindowClass(Window, L"ComboBox"))
            return FALSE;

        return FLAG_ON(Style, CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE) &&
              !FLAG_ON(Style, CBS_HASSTRINGS);
    }

    if (!IsWindowClass(Window, L"ListBox"))
        return FALSE;

    return FLAG_ON(Style, LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE) &&
          !FLAG_ON(Style, LBS_HASSTRINGS);
}

ForceInline BOOL IsEditControl(HWND Window)
{
    return IsWindowClass(Window, L"Edit");
}

ForceInline BOOL IsEditTextPositionMessage(UINT Message)
{
    switch (Message)
    {
        case EM_GETSEL:
        case EM_SETSEL:
        case EM_LINEINDEX:
        case EM_LINELENGTH:
            return TRUE;
    }

    return FALSE;
}

static BOOL CaptureEditTextA(WNDPROC PrevProc, HWND Window, PSTR *Text, ULONG_PTR *Length)
{
    LRESULT TextLength;
    LRESULT Copied;
    PSTR    Buffer;

    *Text = nullptr;
    *Length = 0;

    TextLength = CallWindowProcA(PrevProc, Window, WM_GETTEXTLENGTH, 0, 0);
    if (TextLength < 0)
        return FALSE;

    Buffer = AllocAnsi(TextLength + 1);
    if (Buffer == nullptr)
        return FALSE;

    ZeroMemory(Buffer, TextLength + 1);

    Copied = CallWindowProcA(PrevProc, Window, WM_GETTEXT, TextLength + 1, (LPARAM)Buffer);
    if (Copied < 0)
    {
        FreeString(Buffer);
        return FALSE;
    }

    *Text = Buffer;
    *Length = Copied;

    return TRUE;
}

static BOOL CaptureEditTextW(HWND Window, ULONG_PTR xParam, ULONG xpfnProc, ULONG Flags, PWSTR *Text, ULONG_PTR *Length)
{
    LRESULT TextLength;
    LRESULT Copied;
    PWSTR   Buffer;
    PLepGlobalData GlobalData = LepGetGlobalData();

    *Text = nullptr;
    *Length = 0;
    CLEAR_FLAG(Flags, WINDOW_FLAG_ANSI);

    TextLength = GlobalData->NtUserMessageCall(Window, WM_GETTEXTLENGTH, 0, 0, xParam, xpfnProc, Flags);
    if (TextLength < 0)
        return FALSE;

    Buffer = AllocUnicode(TextLength + 1);
    if (Buffer == nullptr)
        return FALSE;

    ZeroMemory(Buffer, (TextLength + 1) * sizeof(WCHAR));

    Copied = GlobalData->NtUserMessageCall(Window, WM_GETTEXT, TextLength + 1, (LPARAM)Buffer, xParam, xpfnProc, Flags);
    if (Copied < 0)
    {
        FreeString(Buffer);
        return FALSE;
    }

    *Text = Buffer;
    *Length = Copied;

    return TRUE;
}

static ULONG_PTR AnsiCharOffsetToByteOffset(PCSTR Text, ULONG_PTR CharOffset)
{
    PCSTR Current = Text;

    for (ULONG_PTR i = 0; i != CharOffset && *Current != 0; ++i)
        Current = CharNextA(Current);

    return Current - Text;
}

static ULONG_PTR AnsiByteOffsetToCharOffset(PCSTR Text, ULONG_PTR ByteOffset)
{
    PCSTR     Current = Text;
    PCSTR     Target;
    ULONG_PTR CharOffset = 0;
    ULONG_PTR TextLength;

    TextLength = StrLengthA(Text);
    ByteOffset = ML_MIN(ByteOffset, TextLength);
    Target = Text + ByteOffset;

    while (Current < Target && *Current != 0)
    {
        PCSTR Next = CharNextA(Current);
        if (Next <= Current)
            break;

        Current = Next;
        ++CharOffset;
    }

    return CharOffset;
}

static ULONG_PTR AnsiLineStartByteFromOffset(PCSTR Text, ULONG_PTR ByteOffset)
{
    PCSTR     Current = Text;
    PCSTR     Target;
    ULONG_PTR LineStart = 0;
    ULONG_PTR TextLength;

    TextLength = StrLengthA(Text);
    ByteOffset = ML_MIN(ByteOffset, TextLength);
    Target = Text + ByteOffset;

    while (Current < Target && *Current != 0)
    {
        PCSTR Next = CharNextA(Current);

        if (*Current == '\n')
            LineStart = (Current - Text) + 1;

        if (Next <= Current)
            break;

        Current = Next;
    }

    return LineStart;
}

static ULONG_PTR UnicodeLineStartCharFromOffset(PCWSTR Text, ULONG_PTR CharOffset)
{
    ULONG_PTR LineStart = 0;
    ULONG_PTR TextLength;

    TextLength = StrLengthW(Text);
    CharOffset = ML_MIN(CharOffset, TextLength);

    for (ULONG_PTR i = 0; i != CharOffset && Text[i] != 0; ++i)
    {
        if (Text[i] == L'\n')
            LineStart = i + 1;
    }

    return LineStart;
}

static BOOL ConvertEditSelToAnsiByteOffsets(WNDPROC PrevProc, HWND Window, WPARAM *Start, LPARAM *End)
{
    ULONG_PTR Length;
    PSTR      Text;
    BOOL    Converted = FALSE;

    if (*Start == (WPARAM)-1)
        return FALSE;

    if (!CaptureEditTextA(PrevProc, Window, &Text, &Length))
        return FALSE;

    WPARAM NewStart = AnsiCharOffsetToByteOffset(Text, *Start);
    LPARAM NewEnd = *End;

    if (*End != -1)
        NewEnd = (LPARAM)AnsiCharOffsetToByteOffset(Text, (ULONG_PTR)*End);

    *Start = NewStart;
    *End = NewEnd;
    Converted = TRUE;

    FreeString(Text);

    return Converted;
}

static LRESULT UserfnEDIT_GETSEL(WNDPROC PrevProc, HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    LRESULT   Result;
    DWORD     StartA = 0;
    DWORD     EndA = 0;
    DWORD     StartW;
    DWORD     EndW;
    PSTR      Text;
    ULONG_PTR Length;

    Result = CallWindowProcA(PrevProc, Window, Message, (WPARAM)&StartA, (LPARAM)&EndA);

    if (CaptureEditTextA(PrevProc, Window, &Text, &Length))
    {
        StartW = (DWORD)AnsiByteOffsetToCharOffset(Text, StartA);
        EndW = (DWORD)AnsiByteOffsetToCharOffset(Text, EndA);
        FreeString(Text);
    }
    else
    {
        StartW = LOWORD(Result);
        EndW = HIWORD(Result);
    }

    if (wParam != 0)
        *(LPDWORD)wParam = StartW;

    if (lParam != 0)
        *(LPDWORD)lParam = EndW;

    return MAKELRESULT(StartW, EndW);
}

static LRESULT UserfnEDIT_SETSEL(WNDPROC PrevProc, HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    ConvertEditSelToAnsiByteOffsets(PrevProc, Window, &wParam, &lParam);
    return CallWindowProcA(PrevProc, Window, Message, wParam, lParam);
}

static LRESULT UserfnEDIT_LINEINDEX(WNDPROC PrevProc, HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    LRESULT   Result;
    LRESULT   Converted;
    PSTR      Text;
    ULONG_PTR Length;

    Result = CallWindowProcA(PrevProc, Window, Message, wParam, lParam);
    if (Result < 0)
        return Result;

    Converted = Result;
    if (CaptureEditTextA(PrevProc, Window, &Text, &Length))
    {
        Converted = AnsiByteOffsetToCharOffset(Text, Result);
        FreeString(Text);
    }

    return Converted;
}

static LRESULT UserfnEDIT_LINELENGTH(WNDPROC PrevProc, HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    LRESULT   Result;
    LRESULT   Converted;
    PSTR      Text;
    ULONG_PTR Length;
    WPARAM    AnsiIndex;
    ULONG_PTR LineStart;

    if (!CaptureEditTextA(PrevProc, Window, &Text, &Length))
        return CallWindowProcA(PrevProc, Window, Message, wParam, lParam);

    AnsiIndex = wParam;
    if (wParam != (WPARAM)-1)
        AnsiIndex = AnsiCharOffsetToByteOffset(Text, wParam);

    Result = CallWindowProcA(PrevProc, Window, Message, AnsiIndex, lParam);
    if (Result < 0)
    {
        FreeString(Text);
        return Result;
    }

    if (AnsiIndex == (WPARAM)-1)
    {
        LRESULT CurrentLineStart = CallWindowProcA(PrevProc, Window, EM_LINEINDEX, (WPARAM)-1, 0);
        LineStart = CurrentLineStart < 0 ? 0 : CurrentLineStart;
    }
    else
    {
        LineStart = AnsiLineStartByteFromOffset(Text, AnsiIndex);
    }

    Converted = AnsiByteOffsetToCharOffset(Text + LineStart, Result);

    FreeString(Text);

    return Converted;
}

static LRESULT UserfnEDIT_TEXT_POSITION(WNDPROC PrevProc, HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    switch (Message)
    {
        case EM_GETSEL:
            return UserfnEDIT_GETSEL(PrevProc, Window, Message, wParam, lParam);

        case EM_SETSEL:
            return UserfnEDIT_SETSEL(PrevProc, Window, Message, wParam, lParam);

        case EM_LINEINDEX:
            return UserfnEDIT_LINEINDEX(PrevProc, Window, Message, wParam, lParam);

        case EM_LINELENGTH:
            return UserfnEDIT_LINELENGTH(PrevProc, Window, Message, wParam, lParam);
    }

    return CallWindowProcA(PrevProc, Window, Message, wParam, lParam);
}

static LRESULT KernelfnEDIT_GETSEL(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam, ULONG_PTR xParam, ULONG xpfnProc, ULONG Flags)
{
    LRESULT   Result;
    WPARAM    OutputStart;
    LPARAM    OutputEnd;
    DWORD     StartW = 0;
    DWORD     EndW = 0;
    DWORD     StartA;
    DWORD     EndA;
    PWSTR     TextW;
    ULONG_PTR LengthW;
    PSTR      TextA;

    OutputStart = wParam;
    OutputEnd = lParam;
    wParam = (WPARAM)&StartW;
    lParam = (LPARAM)&EndW;

    Result = CallNtUserMessageCallW();

    if (CaptureEditTextW(Window, xParam, xpfnProc, Flags, &TextW, &LengthW))
    {
        TextA = WCharToMByte(TextW, LengthW);
        if (TextA != nullptr)
        {
            StartA = (DWORD)AnsiCharOffsetToByteOffset(TextA, StartW);
            EndA = (DWORD)AnsiCharOffsetToByteOffset(TextA, EndW);
            FreeString(TextA);
        }
        else
        {
            StartA = LOWORD(Result);
            EndA = HIWORD(Result);
        }
        FreeString(TextW);
    }
    else
    {
        StartA = LOWORD(Result);
        EndA = HIWORD(Result);
    }

    if (OutputStart != 0)
        *(LPDWORD)OutputStart = StartA;

    if (OutputEnd != 0)
        *(LPDWORD)OutputEnd = EndA;

    return MAKELRESULT(StartA, EndA);
}

static LRESULT KernelfnEDIT_SETSEL(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam, ULONG_PTR xParam, ULONG xpfnProc, ULONG Flags)
{
    PWSTR     TextW;
    ULONG_PTR LengthW;
    PSTR      TextA;
    WPARAM    NewStart;
    LPARAM    NewEnd;

    if (wParam != (WPARAM)-1 && CaptureEditTextW(Window, xParam, xpfnProc, Flags, &TextW, &LengthW))
    {
        TextA = WCharToMByte(TextW, LengthW);
        if (TextA != nullptr)
        {
            NewStart = AnsiByteOffsetToCharOffset(TextA, wParam);
            NewEnd = lParam;
            if (lParam != -1)
                NewEnd = (LPARAM)AnsiByteOffsetToCharOffset(TextA, (ULONG_PTR)lParam);

            wParam = NewStart;
            lParam = NewEnd;
            FreeString(TextA);
        }
        FreeString(TextW);
    }

    return CallNtUserMessageCallW();
}

static LRESULT KernelfnEDIT_LINEINDEX(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam, ULONG_PTR xParam, ULONG xpfnProc, ULONG Flags)
{
    LRESULT   Result;
    LRESULT   Converted;
    PWSTR     TextW;
    ULONG_PTR LengthW;
    PSTR      TextA;

    Result = CallNtUserMessageCallW();
    if (Result < 0)
        return Result;

    Converted = Result;
    if (CaptureEditTextW(Window, xParam, xpfnProc, Flags, &TextW, &LengthW))
    {
        TextA = WCharToMByte(TextW, LengthW);
        if (TextA != nullptr)
        {
            Converted = AnsiCharOffsetToByteOffset(TextA, Result);
            FreeString(TextA);
        }
        FreeString(TextW);
    }

    return Converted;
}

static LRESULT KernelfnEDIT_LINELENGTH(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam, ULONG_PTR xParam, ULONG xpfnProc, ULONG Flags)
{
    LRESULT   Result;
    LRESULT   Converted;
    PWSTR     TextW;
    ULONG_PTR LengthW;
    PSTR      TextA;
    WPARAM    OriginalIndex;
    WPARAM    UnicodeIndex;
    ULONG_PTR LineStart;
    PLepGlobalData GlobalData = LepGetGlobalData();

    OriginalIndex = wParam;

    if (!CaptureEditTextW(Window, xParam, xpfnProc, Flags, &TextW, &LengthW))
        return CallNtUserMessageCallW();

    UnicodeIndex = wParam;
    if (wParam != (WPARAM)-1)
    {
        TextA = WCharToMByte(TextW, LengthW);
        if (TextA != nullptr)
        {
            UnicodeIndex = AnsiByteOffsetToCharOffset(TextA, wParam);
            FreeString(TextA);
        }
    }

    wParam = UnicodeIndex;
    Result = CallNtUserMessageCallW();
    if (Result < 0)
    {
        FreeString(TextW);
        return Result;
    }

    if (UnicodeIndex == (WPARAM)-1)
    {
        ULONG WFlags = Flags;
        CLEAR_FLAG(WFlags, WINDOW_FLAG_ANSI);
        LRESULT CurrentLineStart = GlobalData->NtUserMessageCall(Window, EM_LINEINDEX, (WPARAM)-1, 0, xParam, xpfnProc, WFlags);
        LineStart = CurrentLineStart < 0 ? 0 : CurrentLineStart;
    }
    else
    {
        LineStart = UnicodeLineStartCharFromOffset(TextW, UnicodeIndex);
    }

    TextA = WCharToMByte(TextW + LineStart, Result);
    if (TextA != nullptr)
    {
        Converted = StrLengthA(TextA);
        FreeString(TextA);
    }
    else
    {
        Converted = Result;
    }

    FreeString(TextW);

    return Converted;
}

static LRESULT KernelfnEDIT_TEXT_POSITION(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam, ULONG_PTR xParam, ULONG xpfnProc, ULONG Flags)
{
    switch (Message)
    {
        case EM_GETSEL:
            return KernelfnEDIT_GETSEL(Window, Message, wParam, lParam, xParam, xpfnProc, Flags);

        case EM_SETSEL:
            return KernelfnEDIT_SETSEL(Window, Message, wParam, lParam, xParam, xpfnProc, Flags);

        case EM_LINEINDEX:
            return KernelfnEDIT_LINEINDEX(Window, Message, wParam, lParam, xParam, xpfnProc, Flags);

        case EM_LINELENGTH:
            return KernelfnEDIT_LINELENGTH(Window, Message, wParam, lParam, xParam, xpfnProc, Flags);
    }

    return CallNtUserMessageCall();
}

/************************************************************************
  ansi to unicode
************************************************************************/

UserMessageCall(EMPTY)
{
    return CallUserMessageCallA();
}

UserMessageCall(INLPCREATESTRUCT)
{
    LRESULT         Result;
    LPCREATESTRUCTW CreateStructW;
    CREATESTRUCTA   CreateStructA;

    CreateStructW = (LPCREATESTRUCTW)lParam;

    SEH_TRY
    {
        LOOP_ONCE
        {
            if (!CreateStructW) break;
            PCBT_CREATE_PARAM CbtCreateParam = (PCBT_CREATE_PARAM)CreateStructW->lpCreateParams;
            if (!CbtCreateParam) break;
            if (CbtCreateParam->Magic == CBT_PROC_PARAM_CONTEXT)
                CreateStructW->lpCreateParams = CbtCreateParam->CreateParams;
        }
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ;
    }

    CreateStructA.lpszClass = nullptr;
    CreateStructA.lpszName = nullptr;

    LOOP_ONCE
    {
        if (CreateStructW == nullptr)
            break;

        CreateStructA = *(LPCREATESTRUCTA)CreateStructW;

        CreateStructA.lpszClass = nullptr;
        CreateStructA.lpszName = nullptr;

        CreateStructA.lpszClass = ClassWCharToMByte(CreateStructW->lpszClass);
        if (CreateStructA.lpszClass == nullptr)
            break;

        if (CreateStructW->lpszName != nullptr)
        {
            CreateStructA.lpszName = TitleWCharToMByte(CreateStructW->lpszName);
            if (CreateStructA.lpszName == nullptr)
                break;
        }

        lParam = (LPARAM)&CreateStructA;
    }

    Result = CallUserMessageCallA();

    FreeClass((PVOID)CreateStructA.lpszClass);
    FreeString((PVOID)CreateStructA.lpszName);

    return Result;
}

UserMessageCall(INLPMDICREATESTRUCT)
{
    LRESULT             Result;
    LPMDICREATESTRUCTW  MdiCreateStructW;
    MDICREATESTRUCTA    MdiCreateStructA;

    MdiCreateStructW = (LPMDICREATESTRUCTW)lParam;

    MdiCreateStructA.szClass = nullptr;
    MdiCreateStructA.szTitle = nullptr;

    LOOP_ONCE
    {
        if (MdiCreateStructW == nullptr)
            break;

        MdiCreateStructA = *(LPMDICREATESTRUCTA)MdiCreateStructW;

        MdiCreateStructA.szClass = nullptr;
        MdiCreateStructA.szTitle = nullptr;

        MdiCreateStructA.szClass = ClassWCharToMByte(MdiCreateStructW->szClass);
        if (MdiCreateStructA.szClass == nullptr)
            break;

        if (MdiCreateStructW->szTitle != nullptr)
        {
            MdiCreateStructA.szTitle = TitleWCharToMByte(MdiCreateStructW->szTitle);
            if (MdiCreateStructA.szTitle == nullptr)
                break;
        }

        lParam = (LPARAM)&MdiCreateStructA;
    }

    Result = CallUserMessageCallA();

    FreeClass((PVOID)MdiCreateStructA.szClass);
    FreeString((PVOID)MdiCreateStructA.szTitle);

    return Result;
}

UserMessageCall(INSTRINGNULL)
{
    PSTR    Ansi;
    LRESULT Result, Length;

    Ansi = nullptr;

    LOOP_ONCE
    {
        if (lParam == 0)
            break;

        Length = StrLengthW((PWSTR)lParam);
        if (Length == 0)
            break;

        Ansi = WCharToMByte((PWSTR)lParam, Length);
        if (Ansi == nullptr)
            break;

        lParam = (LPARAM)Ansi;
    }

    Result = CallUserMessageCallA();

    FreeString(Ansi);

    return Result;
}

UserMessageCall(OUTSTRING)
{
    LRESULT OutputSize, Length;
    PWSTR   OutputBuffer;
    PSTR    Ansi;

    OutputSize = wParam;
    OutputBuffer = (PWSTR)lParam;

    Length = OutputSize * sizeof(WCHAR);
    Ansi = AllocAnsi(Length);
    if (Ansi == nullptr)
        return 0;

    wParam = Length;
    lParam = (LPARAM)Ansi;

    Length = CallUserMessageCallA();

    if (Length != 0)
        AnsiToUnicode(OutputBuffer, OutputSize, Ansi, Length, (PULONG_PTR)&Length);

    FreeString(Ansi);

    return Length / sizeof(WCHAR);
}

UserMessageCall(INSTRING)
{
    return CallUserLocalCall(INSTRINGNULL);
}

UserMessageCall(INCNTOUTSTRING)
{
    LRESULT OutputSize, Length;
    PWSTR   OutputBuffer;
    PSTR    Ansi;

    PMSG_INPUT_COUNT_OUPUT_STRING ParamA, ParamW;

    ParamW = (PMSG_INPUT_COUNT_OUPUT_STRING)lParam;

    OutputSize = ParamW->BufferSize;
    OutputBuffer = ParamW->UnicodeBuffer;

    Length = OutputSize * sizeof(WCHAR);
    Ansi = AllocAnsi(Length);
    if (Ansi == nullptr)
        return 0;

    ParamA = (PMSG_INPUT_COUNT_OUPUT_STRING)Ansi;
    ParamA->BufferSize = Length;
    lParam = (LPARAM)ParamA;

    Length = CallUserMessageCallA();

    if (Length != 0)
        AnsiToUnicode(OutputBuffer, OutputSize, Ansi, Length, (PULONG_PTR)&Length);

    FreeString(Ansi);

    return Length / sizeof(WCHAR);
}

UserMessageCall(INCBOXSTRING)
{
    return CallUserLocalCall(INSTRINGNULL);
}

UserMessageCall(OUTCBOXSTRING)
{
    LRESULT OutputSize, Length;
    PWSTR   OutputBuffer;
    PSTR    Ansi;

    OutputSize = CallWindowProcA(PrevProc, Window, Message + 1, wParam, 0);
    if (OutputSize == 0 || OutputSize == LB_ERR)
        return OutputSize;

    OutputBuffer = (PWSTR)lParam;
    ++OutputSize;
    Ansi = AllocAnsi(OutputSize);
    if (Ansi == nullptr)
        return 0;

    Length = CallWindowProcA(PrevProc, Window, Message, wParam, (LPARAM)Ansi);

    if (Length != 0 && Length != LB_ERR)
        AnsiToUnicode(OutputBuffer, -1, Ansi, Length, (PULONG_PTR)&Length);

    FreeString(Ansi);

    return Length / sizeof(WCHAR);
}

UserMessageCall(INLBOXSTRING)
{
    return CallUserLocalCall(INSTRINGNULL);
}

UserMessageCall(OUTLBOXSTRING)
{
    return CallUserLocalCall(OUTCBOXSTRING);
}

UserMessageCall(INCNTOUTSTRINGNULL)
{
    return CallUserLocalCall(OUTSTRING);
}

UserMessageCall(GETDBCSTEXTLENGTHS)
{
    PSTR    Ansi;
    LRESULT Length;

    Length = CallUserMessageCallA();
    if (Length == 0 || Length == LB_ERR)
        return Length;

    ++Length;
    Ansi = AllocAnsi(Length);
    if (Ansi == nullptr)
        return 0;

    wParam = Message == WM_GETTEXTLENGTH ? Length : wParam;
    lParam = (LPARAM)Ansi;
    --Message;
    Length = CallUserMessageCallA();

    if (Length != 0 && Length != LB_ERR)
    {
        ULONG UnicodeBytes;
        RtlMultiByteToUnicodeSize(&UnicodeBytes, Ansi, Length);
        Length = UnicodeBytes;
    }

    FreeString(Ansi);

    return Length / sizeof(WCHAR);
}

LRESULT NTAPI WindowProcW(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC             PrevProc;
    FNUSERMESSAGECALL   MessageCall;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    PrevProc = (WNDPROC)GlobalData->GetWindowDataA(Window);

    if (Message == WM_SETTEXT && IsWindowClass(Window, L"#32770"))
        return DefWindowProcW(Window, Message, wParam, lParam);

    if (IsEditTextPositionMessage(Message) && IsEditControl(Window))
        return UserfnEDIT_TEXT_POSITION(PrevProc, Window, Message, wParam, lParam);

    if (Message < countof(MessageTable))
    {
        if (IsOwnerDrawItemDataMessage(Window, Message))
            return CallUserMessageCallA();

        MessageCall = gapfnMessageCall[MessageTable[Message].Function].UserCall;
        return MessageCall(PrevProc, Window, Message, wParam, lParam);
    }

    return CallUserMessageCallA();
}

/************************************************************************
  unicode to ansi
************************************************************************/

KernelMessageCall(EMPTY)
{
    return CallNtUserMessageCall();
}

KernelMessageCall(INLPCREATESTRUCT)
{
    LRESULT         Result;
    LPCREATESTRUCTA CreateStructA;
    CREATESTRUCTW   CreateStructW;

    CreateStructA = (LPCREATESTRUCTA)lParam;
    CreateStructW.lpszClass = nullptr;
    CreateStructW.lpszName = nullptr;

    LOOP_ONCE
    {
        if (CreateStructA == nullptr)
            break;

        CreateStructW = *(LPCREATESTRUCTW)CreateStructA;

        CreateStructW.lpszClass = nullptr;
        CreateStructW.lpszName = nullptr;

        CreateStructW.lpszClass = ClassMByteToWChar(CreateStructA->lpszClass);
        if (CreateStructW.lpszClass == nullptr)
            break;

        if (CreateStructA->lpszName != nullptr)
        {
            CreateStructW.lpszName = TitleMByteToWChar(CreateStructA->lpszName);
            if (CreateStructW.lpszName == nullptr)
                break;
        }

        lParam = (LPARAM)&CreateStructW;
        CLEAR_FLAG(Flags, WINDOW_FLAG_ANSI);
    }

    Result = CallNtUserMessageCall();

    FreeClass((PVOID)CreateStructW.lpszClass);
    FreeString((PVOID)CreateStructW.lpszName);

    return Result;
}

KernelMessageCall(INLPMDICREATESTRUCT)
{
    LRESULT             Result;
    LPMDICREATESTRUCTA  MdiCreateStructA;
    MDICREATESTRUCTW    MdiCreateStructW;

    MdiCreateStructA = (LPMDICREATESTRUCTA)lParam;
    MdiCreateStructW.szClass = nullptr;
    MdiCreateStructW.szTitle = nullptr;

    LOOP_ONCE
    {
        if (MdiCreateStructA == nullptr)
            break;

        MdiCreateStructW = *(LPMDICREATESTRUCTW)MdiCreateStructA;

        MdiCreateStructW.szClass = nullptr;
        MdiCreateStructW.szTitle = nullptr;

        MdiCreateStructW.szClass = ClassMByteToWChar(MdiCreateStructA->szClass);
        if (MdiCreateStructW.szClass == nullptr)
            break;

        if (MdiCreateStructA->szTitle != nullptr)
        {
            MdiCreateStructW.szTitle = TitleMByteToWChar(MdiCreateStructA->szTitle);
            if (MdiCreateStructW.szTitle == nullptr)
                break;
        }

        lParam = (LPARAM)&MdiCreateStructW;
        CLEAR_FLAG(Flags, WINDOW_FLAG_ANSI);
    }

    Result = CallNtUserMessageCall();

    FreeClass((PVOID)MdiCreateStructW.szClass);
    FreeString((PVOID)MdiCreateStructW.szTitle);

    return Result;
}

KernelMessageCall(INSTRINGNULL)
{
    PWSTR   Unicode;
    LRESULT Result, Length;

    Unicode = nullptr;

    LOOP_ONCE
    {
        if (lParam == 0)
            break;

        Length = StrLengthA((PSTR)lParam);
        if (Length == 0)
            break;

        Unicode = MByteToWChar((PSTR)lParam, Length);
        if (Unicode == nullptr)
            break;

        lParam = (LPARAM)Unicode;
        CLEAR_FLAG(Flags, WINDOW_FLAG_ANSI);
    }

    Result = CallNtUserMessageCall();

    FreeString(Unicode);

    return Result;
}

KernelMessageCall(OUTSTRING)
{
    LRESULT Length, OutputSize;
    PWSTR   Unicode;
    PSTR    OutputBuffer;

    PLepGlobalData GlobalData = LepGetGlobalData();

    CLEAR_FLAG(Flags, WINDOW_FLAG_ANSI);

    OutputBuffer = (PSTR)lParam;
    OutputSize = wParam;

    Length = GlobalData->NtUserMessageCall(Window, Message + 1, wParam, lParam, xParam, xpfnProc, Flags);
    if (Length == 0)
        return 0;

    ++Length;
    Unicode = AllocUnicode(Length);
    if (Unicode == nullptr)
        return 0;

    Length = GlobalData->NtUserMessageCall(Window, Message, Length, (LPARAM)Unicode, xParam, xpfnProc, Flags);

    UnicodeToAnsi(OutputBuffer, OutputSize, Unicode, Length, (PULONG_PTR)&Length);

    FreeString(Unicode);

    return Length;
}

KernelMessageCall(INSTRING)
{
    return CallNtUserLocalCall(INSTRINGNULL);
}

KernelMessageCall(INCNTOUTSTRING)
{
    LRESULT OutputSize, Length;
    PSTR    OutputBuffer;
    PWSTR   Unicode;

    PMSG_INPUT_COUNT_OUPUT_STRING ParamA, ParamW;

    ParamA = (PMSG_INPUT_COUNT_OUPUT_STRING)lParam;

    OutputSize = ParamA->BufferSize;
    OutputBuffer = ParamA->AnsiBuffer;

    Unicode = AllocUnicode(OutputSize);
    if (Unicode == nullptr)
        return 0;

    ParamW = (PMSG_INPUT_COUNT_OUPUT_STRING)Unicode;
    ParamW->BufferSize = OutputSize;
    lParam = (LPARAM)ParamW;

    Length = CallNtUserMessageCallW();

    if (Length != 0)
        UnicodeToAnsi(OutputBuffer, OutputSize, Unicode, Length, (PULONG_PTR)&Length);

    FreeString(Unicode);

    return Length;
}

KernelMessageCall(INCBOXSTRING)
{
    return CallNtUserLocalCall(INSTRINGNULL);
}

KernelMessageCall(OUTCBOXSTRING)
{
    LRESULT Length, OutputSize;
    PWSTR   Unicode;
    PSTR    OutputBuffer;

    PLepGlobalData GlobalData = LepGetGlobalData();

    CLEAR_FLAG(Flags, WINDOW_FLAG_ANSI);

    OutputBuffer = (PSTR)lParam;

    Length = GlobalData->NtUserMessageCall(Window, Message + 1, wParam, lParam, xParam, xpfnProc, Flags);
    if (Length == 0 || Length == LB_ERR)
        return Length;

    ++Length;
    Unicode = AllocUnicode(Length);
    if (Unicode == nullptr)
        return 0;

    Length = GlobalData->NtUserMessageCall(Window, Message, wParam, (LPARAM)Unicode, xParam, xpfnProc, Flags);

    if (Length != 0 && Length != LB_ERR)
        UnicodeToAnsi(OutputBuffer, -1, Unicode, Length, (PULONG_PTR)&Length);

    FreeString(Unicode);

    return Length;
}

KernelMessageCall(INLBOXSTRING)
{
    return CallNtUserLocalCall(INSTRINGNULL);
}

KernelMessageCall(OUTLBOXSTRING)
{
    return CallNtUserLocalCall(OUTCBOXSTRING);
}

KernelMessageCall(INCNTOUTSTRINGNULL)
{
    return CallNtUserLocalCall(OUTSTRING);
}

KernelMessageCall(GETDBCSTEXTLENGTHS)
{
    LRESULT Length;
    PWSTR   Unicode;

    Length = CallNtUserMessageCallW();
    if (Length == 0 || Length == LB_ERR)
        return Length;

    ++Length;
    Unicode = AllocUnicode(Length);
    if (Unicode == nullptr)
        return 0;

    wParam = Message == WM_GETTEXTLENGTH ? Length : wParam;
    lParam = (LPARAM)Unicode;
    --Message;
    Length = CallNtUserMessageCallW();

    if (Length != 0 && Length != LB_ERR)
    {
        ULONG AnsiBytes;
        RtlUnicodeToMultiByteSize(&AnsiBytes, Unicode, Length * sizeof(WCHAR));
        Length = AnsiBytes;
    }

    FreeString(Unicode);

    return Length;
}

LRESULT
NTAPI
LepNtUserMessageCall(
    HWND         Window,
    UINT         Message,
    WPARAM       wParam,
    LPARAM       lParam,
    ULONG_PTR    xParam,
    ULONG        xpfnProc,
    ULONG        Flags
)
{
    FNMESSAGECALL MessageCall;

    LOOP_ONCE
    {
        if (Message >= countof(MessageTable))
            continue;

        if (!FLAG_ON(Flags, WINDOW_FLAG_ANSI))
            break;

        if (IsOwnerDrawItemDataMessage(Window, Message))
            return CallNtUserMessageCall();

        if (IsEditTextPositionMessage(Message) && IsEditControl(Window) && LepGetGlobalData()->GetWindowDataA(Window) != nullptr)
            return KernelfnEDIT_TEXT_POSITION(Window, Message, wParam, lParam, xParam, xpfnProc, Flags);

        MessageCall = gapfnMessageCall[MessageTable[Message].Function].KernelCall;
        return CALLNTMSGCALL(MessageCall);
    }

    return CallNtUserMessageCall();
}

BOOL
NTAPI
LepNtUserDefSetText(
    HWND                    hWnd,
    PLARGE_UNICODE_STRING   Text
)
{
    BOOL                    Success;
    LARGE_UNICODE_STRING    UnicodeText;
    PLepGlobalData           GlobalData = LepGetGlobalData();

    InitEmptyLargeString(&UnicodeText);

    LOOP_ONCE
    {
        if (Text == nullptr)
            break;

        if (GlobalData->GetWindowDataA(hWnd) == nullptr)
            break;

        if (LargeStringAnsiToUnicode(Text, &UnicodeText) == nullptr)
            break;

        Text = &UnicodeText;
    }

    Success = GlobalData->NtUserSetDefText(hWnd, Text);

    FreeLargeString(&UnicodeText);

    return Success;
}

BOOL VerifyWindowParam(PCBT_CREATE_PARAM CbtCreateParam, PCBT_PROC_PARAM CbtParam)
{
    SEH_TRY
    {
        // FIXME: SEH DOESN'T work
        if (!CbtCreateParam || !CbtParam) return FALSE;
        return CbtCreateParam->StackPointer == CbtParam->StackPointer;
    }
    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
}

VOID ResetDCCharset(PLepGlobalData GlobalData, HWND hWnd)
{
    HDC hDC;
    HFONT Font;

    Font = (HFONT)GetStockObject(SYSTEM_FONT);

    hDC = GetDC(hWnd);
    //Font = GetFontFromDC(GlobalData, hDC);
    SelectObject(hDC, Font);
    //DeleteObject(Font);

    CheckDC(hDC);

    ReleaseDC(hWnd, hDC);

    hDC = GetWindowDC(hWnd);
    //Font = GetFontFromDC(GlobalData, hDC);
    SelectObject(hDC, Font);

    CheckDC(hDC);

    //DeleteObject(Font);
    ReleaseDC(hWnd, hDC);
}

LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    PCBT_PROC_PARAM     CbtParam = (PCBT_PROC_PARAM)FindThreadFrame(CBT_PROC_PARAM_CONTEXT);
    LPCBT_CREATEWND     CreateWnd;
    PCBT_CREATE_PARAM   CreateParam;

    if (nCode == HCBT_CREATEWND) LOOP_ONCE
    {
        BOOL            UnicodeWindow;
        HWND            hWnd;
        WNDPROC         OriginalProcA, OriginalProcW;
        PLepGlobalData   GlobalData;

        hWnd        = (HWND)wParam;
        CreateWnd   = (LPCBT_CREATEWND)lParam;
        CreateParam = (PCBT_CREATE_PARAM)CreateWnd->lpcs->lpCreateParams;

        if (VerifyWindowParam(CreateParam, CbtParam) == FALSE)
            break;

        CreateWnd->lpcs->lpCreateParams = CreateParam->CreateParams;
        GlobalData = CbtParam->GlobalData;

        if (GlobalData->GetWindowDataA(hWnd) != nullptr)
            break;

#if LEP_DISABLE_ANSI_WNDPROC_WRAP
        break;
#endif
        OriginalProcA = GetWindowProcA(GlobalData, hWnd);
        //if (IsCallProcHandle(OriginalProcA) == FALSE)
        {
            InitUnicodeProc(GlobalData, hWnd, WindowProcW, OriginalProcA);
        }
    }

    return CallNextHookEx(CbtParam->Hook, nCode, wParam, lParam);
}

LRESULT CALLBACK CBTProcU(int nCode, WPARAM wParam, LPARAM lParam)
{
    LRESULT             Result;
    PCBT_PROC_PARAM     CbtParam = (PCBT_PROC_PARAM)FindThreadFrame(CBT_PROC_PARAM_CONTEXT);
    LPCBT_CREATEWND     CreateWnd;
    PCBT_CREATE_PARAM   CreateParam;

    Result = CallNextHookEx(CbtParam->Hook, nCode, wParam, lParam);

    if (nCode == HCBT_CREATEWND) LOOP_ONCE
    {
        ResetDCCharset(CbtParam->GlobalData, (HWND)wParam);
    }

    return Result;
}

BOOL
InstallCbtHook(
    PLepGlobalData       GlobalData,
    PCBT_PROC_PARAM     CbtParam,
    PCBT_CREATE_PARAM   CreateParam,
    PVOID               StackPointer,
    PVOID&              Param
)
{
    CbtParam->Hook = SetWindowsHookExA(WH_CBT, CBTProc, nullptr, CurrentTid());
    if (CbtParam->Hook == nullptr)
        return FALSE;

    CbtParam->GlobalData = GlobalData;
    CbtParam->StackPointer = StackPointer;
    CbtParam->Push();

    CreateParam->StackPointer = StackPointer;
    CreateParam->CreateParams = Param;
    Param = CreateParam;

    return TRUE;
}

BOOL
InstallUnicodeCbtHook(
    PLepGlobalData       GlobalData,
    PCBT_PROC_PARAM     CbtParam
)
{
    CbtParam->Hook = SetWindowsHookExA(WH_CBT, CBTProcU, nullptr, CurrentTid());
    if (CbtParam->Hook == nullptr)
        return FALSE;

    CbtParam->GlobalData = GlobalData;
    CbtParam->Push();

    return TRUE;
}

VOID UninstallCbtHook(PCBT_PROC_PARAM CbtParam)
{
    if (CbtParam->Hook == nullptr)
        return;

    UnhookWindowsHookEx(CbtParam->Hook);
    CbtParam->Pop();
}

typedef struct USER_CREATE_WINDOW
{
    ULONG                   ExStyle;
    PLARGE_UNICODE_STRING   ClassName;
    PLARGE_UNICODE_STRING   ClassVersion;
    PLARGE_UNICODE_STRING   WindowName;
    ULONG                   Style;
    LONG                    X;
    LONG                    Y;
    LONG                    Width;
    LONG                    Height;
    HWND                    ParentWnd;
    HMENU                   Menu;
    PVOID                   Instance;
    LPVOID                  Param;
    ULONG                   ShowMode;
    ULONG_PTR               Unknown1;
    ULONG_PTR               Unknown2;
    ULONG_PTR               Unknown3;

} USER_CREATE_WINDOW, *PUSER_CREATE_WINDOW;

typedef struct { PVOID _[15]; } USER_CREATE_WINDOW_WIN7;
typedef struct { PVOID _[16]; } USER_CREATE_WINDOW_WIN8;
typedef struct { PVOID _[17]; } USER_CREATE_WINDOW_WIN10;

HWND LepNtUserCreateWindowExWorker(PUSER_CREATE_WINDOW Parameters, HWND (*Invoker)(PVOID, PUSER_CREATE_WINDOW));
HFONT GetFontFromDC(PLepGlobalData GlobalData, HDC hDC);

#if ML_AMD64
static VOID LepInitNtUserSyscallFrame(LEP_NTUSER_SYSCALL_FRAME* Frame)
{
    Frame->NtUserCreateWindowEx = HpGetSystemCallOriginal(WIN32K_NtUserCreateWindowEx);
    Frame->NtUserMessageCall = HpGetSystemCallOriginal(WIN32K_NtUserMessageCall);
    Frame->NtUserDefSetText = HpGetSystemCallOriginal(WIN32K_NtUserDefSetText);
}

LRESULT
HPCALL
LepHpNtUserMessageCall(
    HPARGS
    HWND         Window,
    UINT         Message,
    WPARAM       wParam,
    LPARAM       lParam,
    ULONG_PTR    xParam,
    ULONG        xpfnProc,
    ULONG        Flags
)
{
    LEP_NTUSER_SYSCALL_FRAME Frame;

    HpSetFilterAction(BlockSystemCall);
    LepInitNtUserSyscallFrame(&Frame);
    Frame.Push();

    return LepNtUserMessageCall(Window, Message, wParam, lParam, xParam, xpfnProc, Flags);
}

BOOL
HPCALL
LepHpNtUserDefSetText(
    HPARGS
    HWND                    hWnd,
    PLARGE_UNICODE_STRING   Text
)
{
    LEP_NTUSER_SYSCALL_FRAME Frame;

    HpSetFilterAction(BlockSystemCall);
    LepInitNtUserSyscallFrame(&Frame);
    Frame.Push();

    return LepNtUserDefSetText(hWnd, Text);
}

HDC
HPCALL
LepHpNtUserGetDC(
    HPARGS
    HWND hWnd
)
{
    HDC DC;
    HFONT Font;
    PVOID Original;

    HpSetFilterAction(BlockSystemCall);

    Original = HpGetSystemCallOriginal(WIN32K_NtUserGetDC);
    if (Original == nullptr)
        return nullptr;

    DC = ((HDC (NTAPI *)(HWND))Original)(hWnd);
    Font = (HFONT)GetStockObject(SYSTEM_FONT);
    if (Font != nullptr)
        SelectObject(DC, Font);

    return DC;
}

HDC
HPCALL
LepHpNtUserGetDCEx(
    HPARGS
    HWND hWnd,
    HRGN hrgnClip,
    DWORD flags
)
{
    HDC DC;
    HFONT Font;
    PVOID Original;

    HpSetFilterAction(BlockSystemCall);

    Original = HpGetSystemCallOriginal(WIN32K_NtUserGetDCEx);
    if (Original == nullptr)
        return nullptr;

    DC = ((HDC (NTAPI *)(HWND, HRGN, DWORD))Original)(hWnd, hrgnClip, flags);
    Font = (HFONT)GetStockObject(SYSTEM_FONT);
    if (Font != nullptr)
        SelectObject(DC, Font);

    return DC;
}

HDC
HPCALL
LepHpNtUserGetWindowDC(
    HPARGS
    HWND hWnd
)
{
    HDC DC;
    HFONT Font;
    PVOID Original;

    HpSetFilterAction(BlockSystemCall);

    Original = HpGetSystemCallOriginal(WIN32K_NtUserGetWindowDC);
    if (Original == nullptr)
        return nullptr;

    DC = ((HDC (NTAPI *)(HWND))Original)(hWnd);
    Font = (HFONT)GetStockObject(SYSTEM_FONT);
    if (Font != nullptr)
        SelectObject(DC, Font);

    return DC;
}

HDC
HPCALL
LepHpNtUserBeginPaint(
    HPARGS
    HWND hWnd,
    LPPAINTSTRUCT lpPaint
)
{
    HDC DC;
    HFONT Font;
    PVOID Original;
    PLepGlobalData GlobalData;

    HpSetFilterAction(BlockSystemCall);

    Original = HpGetSystemCallOriginal(WIN32K_NtUserBeginPaint);
    if (Original == nullptr)
        return nullptr;

    DC = ((HDC (NTAPI *)(HWND, LPPAINTSTRUCT))Original)(hWnd, lpPaint);
    GlobalData = LepGetGlobalData();
    Font = GetFontFromDC(GlobalData, DC);
    if (Font != nullptr)
    {
        SelectObject(DC, Font);
        DeleteObject(Font);
    }

    return DC;
}

HWND
HPCALL
LepHpNtUserCreateWindowEx(
    HPARGS
    ULONG                   ExStyle,
    PLARGE_UNICODE_STRING   ClassName,
    PLARGE_UNICODE_STRING   ClassVersion,
    PLARGE_UNICODE_STRING   WindowName,
    ULONG                   Style,
    LONG                    X,
    LONG                    Y,
    LONG                    Width,
    LONG                    Height,
    HWND                    ParentWnd,
    HMENU                   Menu,
    PVOID                   Instance,
    LPVOID                  Param,
    ULONG                   ShowMode,
    ULONG_PTR               Unknown1,
    ULONG_PTR               Unknown2,
    ULONG_PTR               Unknown3
)
{
    USER_CREATE_WINDOW Parameters;
    LEP_NTUSER_SYSCALL_FRAME Frame;

    HpSetFilterAction(BlockSystemCall);
    LepInitNtUserSyscallFrame(&Frame);
    Frame.Push();

    Parameters.ExStyle      = ExStyle;
    Parameters.ClassName    = ClassName;
    Parameters.ClassVersion = ClassVersion;
    Parameters.WindowName   = WindowName;
    Parameters.Style        = Style;
    Parameters.X            = X;
    Parameters.Y            = Y;
    Parameters.Width        = Width;
    Parameters.Height       = Height;
    Parameters.ParentWnd    = ParentWnd;
    Parameters.Menu         = Menu;
    Parameters.Instance     = Instance;
    Parameters.Param        = Param;
    Parameters.ShowMode     = ShowMode;
    Parameters.Unknown1     = Unknown1;
    Parameters.Unknown2     = Unknown2;
    Parameters.Unknown3     = Unknown3;

    return LepNtUserCreateWindowExWorker(&Parameters,
        [] (PVOID Routine, PUSER_CREATE_WINDOW Parameters) -> HWND
        {
            RTL_OSVERSIONINFOW VersionInfo;

            if (NT_SUCCESS(Nt_QueryOsVersion(&VersionInfo)) &&
                VersionInfo.dwMajorVersion == 6)
            {
                if (VersionInfo.dwMinorVersion < 2)
                {
                    typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG_PTR);
                    return ((PFN)Routine)(Parameters->ExStyle, Parameters->ClassName, Parameters->ClassVersion, Parameters->WindowName, Parameters->Style, Parameters->X, Parameters->Y, Parameters->Width, Parameters->Height, Parameters->ParentWnd, Parameters->Menu, Parameters->Instance, Parameters->Param, Parameters->ShowMode, Parameters->Unknown1);
                }

                typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG, ULONG_PTR);
                return ((PFN)Routine)(Parameters->ExStyle, Parameters->ClassName, Parameters->ClassVersion, Parameters->WindowName, Parameters->Style, Parameters->X, Parameters->Y, Parameters->Width, Parameters->Height, Parameters->ParentWnd, Parameters->Menu, Parameters->Instance, Parameters->Param, Parameters->ShowMode, Parameters->Unknown1, Parameters->Unknown2);
            }

            typedef HWND (NTAPI *PFN)(ULONG, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, PLARGE_UNICODE_STRING, ULONG, LONG, LONG, LONG, LONG, HWND, HMENU, PVOID, LPVOID, ULONG, ULONG, ULONG, ULONG_PTR);
            return ((PFN)Routine)(Parameters->ExStyle, Parameters->ClassName, Parameters->ClassVersion, Parameters->WindowName, Parameters->Style, Parameters->X, Parameters->Y, Parameters->Width, Parameters->Height, Parameters->ParentWnd, Parameters->Menu, Parameters->Instance, Parameters->Param, Parameters->ShowMode, Parameters->Unknown1, Parameters->Unknown2, Parameters->Unknown3);
        });
}
#endif

template<class PARAM_TYPE>
HWND NtUserCreateWindowExInvoker(PVOID Routine, PUSER_CREATE_WINDOW Parameters)
{
    HWND (NTAPI *NtUserCreateWindowEx)(PARAM_TYPE);
    *(PVOID *)&NtUserCreateWindowEx = Routine;
    return NtUserCreateWindowEx(*(PARAM_TYPE *)Parameters);
}

HWND LepNtUserCreateWindowExWorker(PUSER_CREATE_WINDOW Parameters, HWND (*Invoker)(PVOID, PUSER_CREATE_WINDOW))
{
    HWND                    hWnd;
    NTSTATUS                LastError;
    LARGE_UNICODE_STRING    UnicodeWindowName;
    PLepGlobalData           GlobalData;
    CBT_PROC_PARAM          CbtParam;
    CBT_CREATE_PARAM        CreateParam;
    PVOID                   OriginalRoutine;

    GlobalData = LepGetGlobalData();
    OriginalRoutine = GlobalData->HookStub.StubNtUserCreateWindowEx;

#if ML_AMD64
    {
        LEP_NTUSER_SYSCALL_FRAME* Frame = LEP_NTUSER_SYSCALL_FRAME::Current();
        if (Frame != nullptr && Frame->NtUserCreateWindowEx != nullptr)
            OriginalRoutine = Frame->NtUserCreateWindowEx;
    }
#endif

    InitEmptyLargeString(&UnicodeWindowName);

    LOOP_ONCE
    {
        if (!FLAG_ON(Parameters->ExStyle, WS_EX_ANSI))
        {
            InstallUnicodeCbtHook(GlobalData, &CbtParam);
            break;
        }

        if (Parameters->WindowName != nullptr)
        {
            if (CaptureAnsiWindowName(Parameters->WindowName, &UnicodeWindowName) == nullptr)
                break;
        }

        Parameters->WindowName = &UnicodeWindowName;

        InstallCbtHook(GlobalData, &CbtParam, &CreateParam, _AddressOfReturnAddress(), Parameters->Param);
    }

    hWnd = Invoker(OriginalRoutine, Parameters);

    LastError = RtlGetLastWin32Error();

/*
    if (hWnd != nullptr)
    {
        HDC hDC;

        hDC= GetDC(hWnd);

        CheckDC(hDC);

        ReleaseDC(hWnd, hDC);

        hDC= GetWindowDC(hWnd);

        CheckDC(hDC);

        ReleaseDC(hWnd, hDC);
    }
*/
    UninstallCbtHook(&CbtParam);
    FreeLargeString(&UnicodeWindowName);

    RtlSetLastWin32Error(LastError);

    return hWnd;
}

HWND NTAPI LepNtUserCreateWindowEx_Win7(USER_CREATE_WINDOW_WIN7 Parameters)
{
    return LepNtUserCreateWindowExWorker((PUSER_CREATE_WINDOW)&Parameters, NtUserCreateWindowExInvoker<USER_CREATE_WINDOW_WIN7>);
}

HWND NTAPI LepNtUserCreateWindowEx_Win8(USER_CREATE_WINDOW_WIN8 Parameters)
{
    return LepNtUserCreateWindowExWorker((PUSER_CREATE_WINDOW)&Parameters, NtUserCreateWindowExInvoker<USER_CREATE_WINDOW_WIN8>);
}

HWND NTAPI LepNtUserCreateWindowEx_Win10(USER_CREATE_WINDOW_WIN10 Parameters)
{
    return LepNtUserCreateWindowExWorker((PUSER_CREATE_WINDOW)&Parameters, NtUserCreateWindowExInvoker<USER_CREATE_WINDOW_WIN10>);
}

#if ML_AMD64
HWND
NTAPI
LepNtUserCreateWindowEx(
    ULONG                   ExStyle,
    PLARGE_UNICODE_STRING   ClassName,
    PLARGE_UNICODE_STRING   ClassVersion,
    PLARGE_UNICODE_STRING   WindowName,
    ULONG                   Style,
    LONG                    X,
    LONG                    Y,
    LONG                    Width,
    LONG                    Height,
    HWND                    ParentWnd,
    HMENU                   Menu,
    PVOID                   Instance,
    LPVOID                  Param,
    ULONG                   ShowMode,
    ULONG_PTR               Unknown1,
    ULONG_PTR               Unknown2,
    ULONG_PTR               Unknown3
)
{
    USER_CREATE_WINDOW Parameters;

    Parameters.ExStyle      = ExStyle;
    Parameters.ClassName    = ClassName;
    Parameters.ClassVersion = ClassVersion;
    Parameters.WindowName   = WindowName;
    Parameters.Style        = Style;
    Parameters.X            = X;
    Parameters.Y            = Y;
    Parameters.Width        = Width;
    Parameters.Height       = Height;
    Parameters.ParentWnd    = ParentWnd;
    Parameters.Menu         = Menu;
    Parameters.Instance     = Instance;
    Parameters.Param        = Param;
    Parameters.ShowMode     = ShowMode;
    Parameters.Unknown1     = Unknown1;
    Parameters.Unknown2     = Unknown2;
    Parameters.Unknown3     = Unknown3;

    return LepNtUserCreateWindowExWorker(&Parameters,
        [] (PVOID Routine, PUSER_CREATE_WINDOW Parameters) -> HWND
        {
            typedef HWND (NTAPI *PFN)(
                ULONG,
                PLARGE_UNICODE_STRING,
                PLARGE_UNICODE_STRING,
                PLARGE_UNICODE_STRING,
                ULONG,
                LONG,
                LONG,
                LONG,
                LONG,
                HWND,
                HMENU,
                PVOID,
                LPVOID,
                ULONG,
                ULONG_PTR,
                ULONG_PTR,
                ULONG_PTR
            );

            return ((PFN)Routine)(
                Parameters->ExStyle,
                Parameters->ClassName,
                Parameters->ClassVersion,
                Parameters->WindowName,
                Parameters->Style,
                Parameters->X,
                Parameters->Y,
                Parameters->Width,
                Parameters->Height,
                Parameters->ParentWnd,
                Parameters->Menu,
                Parameters->Instance,
                Parameters->Param,
                Parameters->ShowMode,
                Parameters->Unknown1,
                Parameters->Unknown2,
                Parameters->Unknown3
            );
        });
}
#endif

ForceInline LONG_PTR LepGetWindowLongAWorker(PLepGlobalData GlobalData, HWND hWnd, int Index, BOOL UsePtrApi);
ForceInline LONG_PTR LepSetWindowLongAWorker(PLepGlobalData GlobalData, HWND hWnd, int Index, LONG_PTR NewLong, BOOL UsePtrApi);

LONG_PTR NTAPI LepGetWindowLongA(HWND hWnd, int Index)
{
    PLepGlobalData   GlobalData = LepGetGlobalData();

    return LepGetWindowLongAWorker(GlobalData, hWnd, Index, FALSE);
}

ForceInline LONG_PTR LepGetWindowLongAWorker(PLepGlobalData GlobalData, HWND hWnd, int Index, BOOL UsePtrApi)
{
    PVOID Proc;

    switch (Index)
    {
        case GWLP_WNDPROC:
            Proc = GlobalData->GetWindowDataA(hWnd);
            if (Proc == nullptr)
                break;

            return (LONG_PTR)Proc;
    }

#if ML_AMD64
    if (UsePtrApi)
        return GlobalData->GetWindowLongPtrA(hWnd, Index);
#else
    (VOID)UsePtrApi;
#endif
    return GlobalData->GetWindowLongA(hWnd, Index);
}

ForceInline LONG_PTR LepSetWindowLongAWorker(PLepGlobalData GlobalData, HWND hWnd, int Index, LONG_PTR NewLong, BOOL UsePtrApi)
{
    PVOID OriginalProcA;

    switch (Index)
    {
        case GWLP_WNDPROC:
#if LEP_DISABLE_ANSI_WNDPROC_WRAP
#if ML_AMD64
            if (UsePtrApi)
                return GlobalData->SetWindowLongPtrA(hWnd, Index, NewLong);
#else
            (VOID)UsePtrApi;
#endif
            return GlobalData->SetWindowLongA(hWnd, Index, NewLong);
#endif
            OriginalProcA = GlobalData->GetWindowDataA(hWnd);
            if (OriginalProcA != nullptr)
            {
                GlobalData->SetWindowDataA(hWnd, (PVOID)NewLong);
            }
            else
            {
#if ML_AMD64
                if (UsePtrApi)
                    OriginalProcA = (PVOID)GlobalData->SetWindowLongPtrA(hWnd, Index, NewLong);
                else
#endif
                    OriginalProcA = (PVOID)GlobalData->SetWindowLongA(hWnd, Index, NewLong);
                InitUnicodeProc(GlobalData, hWnd, WindowProcW, (PVOID)NewLong);
            }

            return (LONG_PTR)OriginalProcA;
    }

#if ML_AMD64
    if (UsePtrApi)
        return GlobalData->SetWindowLongPtrA(hWnd, Index, NewLong);
#else
    (VOID)UsePtrApi;
#endif
    return GlobalData->SetWindowLongA(hWnd, Index, NewLong);
}

LONG_PTR NTAPI LepSetWindowLongA(HWND hWnd, int Index, LONG_PTR NewLong)
{
    PLepGlobalData   GlobalData = LepGetGlobalData();

    return LepSetWindowLongAWorker(GlobalData, hWnd, Index, NewLong, FALSE);
}

#if ML_AMD64
LONG_PTR NTAPI LepGetWindowLongPtrA(HWND hWnd, int Index)
{
    PLepGlobalData GlobalData = LepGetGlobalData();

    return LepGetWindowLongAWorker(GlobalData, hWnd, Index, TRUE);
}

LONG_PTR NTAPI LepSetWindowLongPtrA(HWND hWnd, int Index, LONG_PTR NewLong)
{
    PLepGlobalData GlobalData = LepGetGlobalData();

    return LepSetWindowLongAWorker(GlobalData, hWnd, Index, NewLong, TRUE);
}
#endif

BOOL NTAPI LepIsWindowUnicode(HWND hWnd)
{
    PLepGlobalData GlobalData = LepGetGlobalData();

    return GlobalData->GetWindowDataA(hWnd) == nullptr ? GlobalData->IsWindowUnicode(hWnd) : FALSE;
}

HANDLE NTAPI LepSetClipboardData(UINT Format, HANDLE Memory)
{
    HGLOBAL             Data;
    PWSTR               Unicode;
    PSTR                Ansi;
    ULONG_PTR           Length;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    Ansi = nullptr;
    switch (Format)
    {
        case CF_TEXT:

            Ansi = (PSTR)GlobalLock(Memory);
            if (Ansi == nullptr)
                break;

            Length = StrLengthA(Ansi);
            if (Length == 0)
                break;

            ++Length;
            Data = GlobalAlloc(GHND, Length * sizeof(WCHAR));
            if (Data == nullptr)
                break;

            Unicode = (PWSTR)GlobalLock(Data);
            AnsiToUnicode(Unicode, Length, Ansi, Length - 1);
            GlobalUnlock(Data);

            if (GlobalData->SetClipboardData(CF_UNICODETEXT, Data) == nullptr)
            {
                GlobalFree(Data);
            }

            break;
    }

    if (Ansi != nullptr)
        GlobalUnlock(Memory);

    return GlobalData->SetClipboardData(Format, Memory);
}

HANDLE NTAPI LepGetClipboardData(UINT Format)
{
    HGLOBAL             Data, AnsiData;
    ULONG_PTR           Length, Flags;
    PWSTR               Unicode;
    PSTR                Ansi;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    switch (Format)
    {
        case CF_TEXT:
            Data = GlobalData->GetClipboardData(CF_UNICODETEXT);
            if (Data == nullptr)
                break;

            Flags = GlobalFlags(Data);
            if (FLAG_ON(Flags, GMEM_INVALID_HANDLE))
                break;

            Unicode = (PWSTR)GlobalLock(Data);
            if (Unicode == nullptr)
                break;

            Length = StrLengthW(Unicode);
            AnsiData = GlobalAlloc(GHND, Length * sizeof(WCHAR) + 1);
            if (AnsiData == nullptr)
            {
                GlobalUnlock(Data);
                break;
            }

            Ansi = (PSTR)GlobalLock(AnsiData);

            UnicodeToAnsi(Ansi, Length * sizeof(WCHAR) + 1, Unicode, Length);

            GlobalUnlock(AnsiData);
            GlobalUnlock(Data);

            Data = SetClipboardData(CF_TEXT, AnsiData);
            if (Data == nullptr)
            {
                GlobalFree(AnsiData);
            }

            break;
    }

    return GlobalData->GetClipboardData(Format);
}

#ifndef SPI_GETDEFAULTINPUTLANG
#define SPI_GETDEFAULTINPUTLANG 0x0059
#endif

static const ULONG_PTR LEP_JAPANESE_DEFAULT_INPUT_LANG = 0x04110411;

BOOL NTAPI LepSystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
{
    BOOL Result;
    PLepGlobalData GlobalData;

    GlobalData = LepGetGlobalData();
    Result = GlobalData->SystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);

    if (Result &&
        uiAction == SPI_GETDEFAULTINPUTLANG &&
        pvParam != nullptr)
    {
        *(HKL *)pvParam = (HKL)LEP_JAPANESE_DEFAULT_INPUT_LANG;
    }

    return Result;
}

#define GetSystemFont(...) (HFONT)GetStockObject(SYSTEM_FONT)

HDC NTAPI LepGetDCEx(HWND hWnd, HRGN hrgnClip, DWORD flags)
{
    HDC             DC;
    HFONT           Font;
    PLepGlobalData   GlobalData = LepGetGlobalData();

    DC = GlobalData->GetDCEx(hWnd, hrgnClip, flags);
    //if (hWnd == nullptr)
    {
        Font = GetSystemFont(GlobalData, DC);
        if (Font != nullptr)
        {
            SelectObject(DC, Font);
            //DeleteObject(Font);
        }
    }

    return DC;
}

HDC NTAPI LepGetDC(HWND hWnd)
{
    HDC             DC;
    HFONT           Font;
    PLepGlobalData   GlobalData = LepGetGlobalData();

    DC = GlobalData->GetDC(hWnd);
    //if (hWnd == nullptr)
    {
        Font = GetSystemFont(GlobalData, DC);
        if (Font != nullptr)
        {
            SelectObject(DC, Font);
            //DeleteObject(Font);
        }
    }

    return DC;
}

HDC NTAPI LepGetWindowDC(HWND hWnd)
{
    HDC             DC;
    HFONT           Font;
    PLepGlobalData   GlobalData = LepGetGlobalData();

    DC = GlobalData->GetWindowDC(hWnd);
    //if (hWnd == nullptr)
    {
        Font = GetSystemFont(GlobalData, DC);
        if (Font != nullptr)
        {
            SelectObject(DC, Font);
            //DeleteObject(Font);
        }
    }

    return DC;
}

HDC NTAPI LepBeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
{
    HDC             DC;
    HFONT           Font;
    PLepGlobalData   GlobalData = LepGetGlobalData();

    DC = GlobalData->BeginPaint(hWnd, lpPaint);
    Font = GetFontFromDC(GlobalData, DC);
    if (Font != nullptr)
    {
        SelectObject(DC, Font);
        DeleteObject(Font);
    }

    return DC;
}

#if ML_AMD64
static BOOL IsX64SyscallStub(PVOID Routine)
{
    PBYTE Function;

    Function = (PBYTE)Routine;
    if (Function == nullptr)
        return FALSE;

    if (Function[0] != 0x4C ||
        Function[1] != 0x8B ||
        Function[2] != 0xD1 ||
        Function[3] != 0xB8)
        return FALSE;

    for (ULONG_PTR i = 8; i != 0x20; ++i)
        if (Function[i] == 0x0F && Function[i + 1] == 0x05 && Function[i + 2] == 0xC3)
            return TRUE;

    return FALSE;
}

static NTSTATUS CheckX64SyscallRoutine(PVOID Routine, ULONG IdForLog, PVOID* CheckedRoutine)
{
    *CheckedRoutine = Routine;
    if (*CheckedRoutine == nullptr)
        return STATUS_NOT_FOUND;

    if (!IsX64SyscallStub(*CheckedRoutine))
    {
#if ENABLE_LOG
        PBYTE Bytes = (PBYTE)*CheckedRoutine;
        WriteLog(L"user32 x64 export is not syscall stub hash=%08X routine=%p bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            IdForLog,
            *CheckedRoutine,
            Bytes[0], Bytes[1], Bytes[2], Bytes[3],
            Bytes[4], Bytes[5], Bytes[6], Bytes[7],
            Bytes[8], Bytes[9], Bytes[10], Bytes[11]);
#endif
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS GetX64SyscallExport(PVOID Module, ULONG Hash, PVOID* Routine)
{
    return CheckX64SyscallRoutine(LookupExportTable(Module, Hash), Hash, Routine);
}

static NTSTATUS GetX64SyscallExportByName(PVOID Module, PCSTR Name, ULONG IdForLog, PVOID* Routine)
{
    return CheckX64SyscallRoutine(Nt_GetProcAddress(Module, Name), IdForLog, Routine);
}

static BOOL IsAddressInModule(PVOID Module, PVOID Address)
{
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    PBYTE ImageBase;
    PBYTE ImageEnd;

    if (Module == nullptr || Address == nullptr)
        return FALSE;

    DosHeader = (PIMAGE_DOS_HEADER)Module;
    NtHeaders = (PIMAGE_NT_HEADERS)PtrAdd(Module, DosHeader->e_lfanew);
    ImageBase = (PBYTE)Module;
    ImageEnd = PtrAdd(ImageBase, NtHeaders->OptionalHeader.SizeOfImage);

    return IN_RANGEEX(ImageBase, (PBYTE)Address, ImageEnd);
}

static PVOID FindFirstX64SyscallCall(PVOID Routine, ULONG_PTR Range)
{
    PVOID SyscallRoutine = nullptr;

    if (Routine == nullptr)
        return nullptr;

    WalkOpCodeT(Routine, Range,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            if (Buffer[0] == CALL)
            {
                PVOID Destination;

                SEH_TRY
                {
                    Destination = GetCallDestination(Buffer);
                    if (IsX64SyscallStub(Destination))
                    {
                        SyscallRoutine = Destination;
                        return STATUS_SUCCESS;
                    }
                }
                SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    return STATUS_NOT_FOUND;
                }
            }

            if (Buffer[0] == 0xC2 || Buffer[0] == 0xC3)
                return STATUS_NOTHING_TO_TERMINATE;

            return STATUS_NOT_FOUND;
        }
    );

    return SyscallRoutine;
}

static BOOL HasC0000000Immediate(PBYTE Buffer, ULONG_PTR OpLength)
{
    for (ULONG_PTR i = 0; i + sizeof(ULONG) <= OpLength; ++i)
    {
        if (Buffer[i] == 0x00 &&
            Buffer[i + 1] == 0x00 &&
            Buffer[i + 2] == 0x00 &&
            Buffer[i + 3] == 0xC0)
            return TRUE;
    }

    return FALSE;
}

static PVOID FindX64NtUserMessageCallNoWin32U(PVOID User32)
{
    PVOID Routines[] =
    {
        LookupExportTable(User32, USER32_SendNotifyMessageW),
        LookupExportTable(User32, USER32_SendNotifyMessageA),
    };

    for (ULONG_PTR i = 0; i != countof(Routines); ++i)
    {
        PVOID NtUserMessageCall = FindFirstX64SyscallCall(Routines[i], 0x30);
        if (NtUserMessageCall != nullptr)
            return NtUserMessageCall;
    }

    return nullptr;
}

static PVOID FindX64InternalCreateWindowEx(PVOID User32)
{
    PVOID Routines[] =
    {
        LookupExportTable(User32, USER32_CreateWindowExW),
        LookupExportTable(User32, USER32_CreateWindowExA),
    };

    for (ULONG_PTR i = 0; i != countof(Routines); ++i)
    {
        PVOID InternalCreateWindowEx = nullptr;

        if (Routines[i] == nullptr)
            continue;

        WalkOpCodeT(Routines[i], 0x80,
            WalkOpCodeM(Buffer, OpLength, Ret)
            {
                if (Buffer[0] == CALL)
                {
                    PVOID Destination;

                    SEH_TRY
                    {
                        Destination = GetCallDestination(Buffer);
                        if (IsAddressInModule(User32, Destination) && !IsX64SyscallStub(Destination))
                        {
                            InternalCreateWindowEx = Destination;
                            return STATUS_SUCCESS;
                        }
                    }
                    SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                    {
                        return STATUS_NOT_FOUND;
                    }
                }

                if (Buffer[0] == 0xC2 || Buffer[0] == 0xC3)
                    return STATUS_NOTHING_TO_TERMINATE;

                return STATUS_NOT_FOUND;
            }
        );

        if (InternalCreateWindowEx != nullptr)
            return InternalCreateWindowEx;
    }

    return nullptr;
}

static PVOID FindX64VerNtUserCreateWindowExNoWin32U(PVOID User32, PVOID* NtUserCreateWindowEx)
{
    PVOID InternalCreateWindowEx;
    PVOID VerNtUserCreateWindowEx;
    BOOL FoundStyleMask;

    if (NtUserCreateWindowEx != nullptr)
        *NtUserCreateWindowEx = nullptr;

    InternalCreateWindowEx = FindX64InternalCreateWindowEx(User32);
    if (InternalCreateWindowEx == nullptr)
        return nullptr;

    VerNtUserCreateWindowEx = nullptr;
    FoundStyleMask = FALSE;

    WalkOpCodeT(InternalCreateWindowEx, 0x2C0,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            if (!FoundStyleMask)
            {
                if (HasC0000000Immediate(Buffer, OpLength))
                    FoundStyleMask = TRUE;

                return STATUS_NOT_FOUND;
            }

            if (Buffer[0] == CALL)
            {
                PVOID Destination;

                SEH_TRY
                {
                    Destination = GetCallDestination(Buffer);
                    if (IsAddressInModule(User32, Destination) && !IsX64SyscallStub(Destination))
                    {
                        VerNtUserCreateWindowEx = Destination;
                        return STATUS_SUCCESS;
                    }
                }
                SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }

            if (Buffer[0] == 0xC2 || Buffer[0] == 0xC3)
                return STATUS_NOTHING_TO_TERMINATE;

            return STATUS_NOT_FOUND;
        }
    );

#if ENABLE_LOG
    WriteLog(L"user32 x64 no-win32u VerNtUserCreateWindowEx=%p", VerNtUserCreateWindowEx);
#endif

    if (VerNtUserCreateWindowEx == nullptr)
        return nullptr;

    PVOID SyscallRoutine = FindFirstX64SyscallCall(VerNtUserCreateWindowEx, 0x300);
#if ENABLE_LOG
    WriteLog(L"user32 x64 no-win32u NtUserCreateWindowEx syscall=%p", SyscallRoutine);
#endif

    if (NtUserCreateWindowEx != nullptr)
        *NtUserCreateWindowEx = SyscallRoutine;

    return VerNtUserCreateWindowEx;
}

static PVOID FindX64NtUserCreateWindowExNoWin32U(PVOID User32)
{
    PVOID NtUserCreateWindowEx;

    FindX64VerNtUserCreateWindowExNoWin32U(User32, &NtUserCreateWindowEx);
    return NtUserCreateWindowEx;
}

static PVOID FindX64RtlInitLargeUnicodeStringNoWin32U(PVOID User32)
{
    PVOID InternalCreateWindowEx;
    PVOID RtlInitLargeUnicodeString;
    PVOID PreviousCall;

    InternalCreateWindowEx = FindX64InternalCreateWindowEx(User32);
    if (InternalCreateWindowEx == nullptr)
        return nullptr;

    RtlInitLargeUnicodeString = nullptr;
    PreviousCall = nullptr;

    WalkOpCodeT(InternalCreateWindowEx, 0x2C0,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            if (HasC0000000Immediate(Buffer, OpLength))
            {
                RtlInitLargeUnicodeString = PreviousCall;
                return STATUS_SUCCESS;
            }

            if (Buffer[0] == CALL)
            {
                SEH_TRY
                {
                    PVOID Destination = GetCallDestination(Buffer);
                    if (IsAddressInModule(User32, Destination) && !IsX64SyscallStub(Destination))
                        PreviousCall = Destination;
                }
                SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }

            if (Buffer[0] == 0xC2 || Buffer[0] == 0xC3)
                return STATUS_NOTHING_TO_TERMINATE;

            return STATUS_NOT_FOUND;
        }
    );

#if ENABLE_LOG
    WriteLog(L"user32 x64 no-win32u RtlInitLargeUnicodeString=%p", RtlInitLargeUnicodeString);
#endif

    return RtlInitLargeUnicodeString;
}

static BOOL GetTextRange(PVOID Module, PBYTE* TextStart, PBYTE* TextEnd)
{
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;
    ULONG TextSize;

    if (Module == nullptr || TextStart == nullptr || TextEnd == nullptr)
        return FALSE;

    DosHeader = (PIMAGE_DOS_HEADER)Module;
    NtHeaders = (PIMAGE_NT_HEADERS)PtrAdd(Module, DosHeader->e_lfanew);
    Section = IMAGE_FIRST_SECTION(NtHeaders);
    TextSize = Section->Misc.VirtualSize != 0 ? Section->Misc.VirtualSize : Section->SizeOfRawData;
    if (TextSize == 0)
        return FALSE;

    *TextStart = (PBYTE)PtrAdd(Module, Section->VirtualAddress);
    *TextEnd = PtrAdd(*TextStart, TextSize);

    return TRUE;
}

static PVOID CheckX64NtUserDefSetTextAfterRtlInit(PBYTE CallSite, PBYTE TextEnd)
{
    PBYTE Start;
    ULONG_PTR Range;
    PVOID SyscallRoutine;
    BOOL SawRet;
    BOOL BadCandidate;

    Start = PtrAdd(CallSite, 5);
    Range = PtrOffset(TextEnd, Start);
    if (Range > 0x20)
        Range = 0x20;

    SyscallRoutine = nullptr;
    SawRet = FALSE;
    BadCandidate = FALSE;

    WalkOpCodeT(Start, Range,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            if (Buffer[0] == CALL)
            {
                PVOID Destination;

                SEH_TRY
                {
                    Destination = GetCallDestination(Buffer);
                    if (!IsX64SyscallStub(Destination))
                    {
                        BadCandidate = TRUE;
                        return STATUS_SUCCESS;
                    }

                    if (SyscallRoutine != nullptr)
                    {
                        BadCandidate = TRUE;
                        return STATUS_SUCCESS;
                    }

                    SyscallRoutine = Destination;
                }
                SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    BadCandidate = TRUE;
                    return STATUS_SUCCESS;
                }
            }

            if (Buffer[0] == 0xC2 || Buffer[0] == 0xC3)
            {
                SawRet = SyscallRoutine != nullptr;
                return STATUS_SUCCESS;
            }

            return STATUS_NOT_FOUND;
        }
    );

    if (BadCandidate || !SawRet)
        return nullptr;

    return SyscallRoutine;
}

static PVOID FindX64NtUserDefSetTextNoWin32U(PVOID User32)
{
    PVOID RtlInitLargeUnicodeString;
    PBYTE TextStart;
    PBYTE TextEnd;

    RtlInitLargeUnicodeString = FindX64RtlInitLargeUnicodeStringNoWin32U(User32);
    if (RtlInitLargeUnicodeString == nullptr)
        return nullptr;

    if (!GetTextRange(User32, &TextStart, &TextEnd))
        return nullptr;

    for (PBYTE Buffer = TextStart; Buffer + 5 < TextEnd; ++Buffer)
    {
        PVOID Destination;
        PVOID NtUserDefSetText;

        if (Buffer[0] != CALL)
            continue;

        SEH_TRY
        {
            Destination = GetCallDestination(Buffer);
        }
        SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            continue;
        }

        if (Destination != RtlInitLargeUnicodeString)
            continue;

        NtUserDefSetText = CheckX64NtUserDefSetTextAfterRtlInit(Buffer, TextEnd);
        if (NtUserDefSetText != nullptr)
        {
#if ENABLE_LOG
            WriteLog(L"user32 x64 no-win32u DefSetText rtlcall=%p syscall=%p", Buffer, NtUserDefSetText);
#endif
            return NtUserDefSetText;
        }
    }

#if ENABLE_LOG
    WriteLog(L"user32 x64 no-win32u DefSetText not found, RtlInitLargeUnicodeString=%p", RtlInitLargeUnicodeString);
#endif

    return nullptr;
}
#endif

/************************************************************************
  init
************************************************************************/

PVOID FindNtUserMessageCall(PVOID User32)
{
    PVOID NtUserMessageCall = nullptr;
    PVOID SendNotifyMessageW = LookupExportTable(User32, USER32_SendNotifyMessageW);

    NtUserMessageCall = WalkOpCodeT(SendNotifyMessageW, 0x40,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            switch (Buffer[0])
            {
                case CALL:
                    Buffer = GetCallDestination(Buffer);
                    if (IsSystemCall(Buffer) == FALSE)
                        break;

                    Ret = Buffer;
                    return STATUS_SUCCESS;

                case 0xC2:
                    return STATUS_TIMEOUT;
            }

            return STATUS_NOT_FOUND;
        }
    );

    return NtUserMessageCall;
}

PVOID FindNtUserMessageCall2(PVOID User32)
{
    ULONG Phase;
    PVOID NtUserMessageCall;
    PVOID GlobalRoutines[3];
    PVOID fnCOPYGLOBALDATA[3];

    GlobalRoutines[0] = IATLookupRoutineByHash(User32, KERNEL32_GlobalLock);
    GlobalRoutines[1] = IATLookupRoutineByHash(User32, KERNEL32_GlobalUnlock);
    GlobalRoutines[2] = IATLookupRoutineByHash(User32, KERNEL32_GlobalFree);

    NtUserMessageCall = nullptr;

    Phase = 0;

    static const ULONG FinalPhase = countof(GlobalRoutines);

    WalkRelocTableT(User32,
        WalkRelocCallbackM(ImageBase, RelocationEntry, Offset, Context)
        {
            PVOID *VA = (PVOID *)PtrAdd(ImageBase, RelocationEntry->VirtualAddress + Offset->Offset);

            if (*VA != GlobalRoutines[Phase])
            {
                Phase = 0;
                return 0;
            }

            fnCOPYGLOBALDATA[Phase++] = VA;

            if (Phase != FinalPhase)
                return 0;

            PBYTE Begin, End;

            Begin = (PBYTE)fnCOPYGLOBALDATA[0] + sizeof(*VA);
            End   = (PBYTE)fnCOPYGLOBALDATA[1] + sizeof(*VA);

            WalkOpCodeT(Begin, End - Begin,
                WalkOpCodeM(Buffer, OpLength, Ret)
                {
                    switch (Buffer[0])
                    {
                        case CALL:
                            SEH_TRY
                            {
                                Buffer = GetCallDestination(Buffer);
                                if (!Buffer || IsSystemCall(Buffer) == FALSE)
                                    break;
                            }
                            SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                            {
                                break;
                            }

                            NtUserMessageCall = Buffer;
                            return STATUS_SUCCESS;
                    }

                    return STATUS_NOT_FOUND;
                }
            );

            if (NtUserMessageCall == nullptr)
            {
                Phase = 0;
                return STATUS_SUCCESS;
            }

            return STATUS_UNSUCCESSFUL;
        },
        0
    );

    return NtUserMessageCall;
}

PVOID FindNtUserCreateWindowEx(PVOID User32)
{
    PVOID NtUserCreateWindowEx, IATRtlQueryInformationActiveActivationContext;

    IATRtlQueryInformationActiveActivationContext = IATLookupRoutineByHash(User32, NTDLL_RtlQueryInformationActiveActivationContext);
    if (IATRtlQueryInformationActiveActivationContext == nullptr)
        return nullptr;

    NtUserCreateWindowEx = nullptr;

    WalkRelocTableT(User32,
        WalkRelocCallbackM(ImageBase, RelocationEntry, Offset, Context)
        {
            PVOID *VA = (PVOID *)PtrAdd(ImageBase, RelocationEntry->VirtualAddress + Offset->Offset);

            if (*VA != IATRtlQueryInformationActiveActivationContext)
                return 0;

            VA = PtrAdd(VA, -2);
            if (*(PUSHORT)VA != 0x15FF) // call dword ptr [RtlQueryInformationActiveActivationContext]
                return 0;

            WalkOpCodeT(VA, 0x150,
                WalkOpCodeM(Buffer, OpLength, Ret)
                {
                    switch (Buffer[0])
                    {
                        case 0xE8:
                            Buffer = GetCallDestination(Buffer);
                            if (!IsSystemCall(Buffer))
                                break;

                            NtUserCreateWindowEx = Buffer;
                            return STATUS_SUCCESS;

                        case 0xC2:
                        case 0xC3:
                            return STATUS_NOTHING_TO_TERMINATE;
                    }

                    return STATUS_NOT_FOUND;
                }
            );

            return NtUserCreateWindowEx == nullptr ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        },
        0
    );

    return NtUserCreateWindowEx;
}

PVOID FindNtUserDefSetText(PVOID User32)
{
    PLDR_MODULE Module;

    PVOID Start, End;

    PVOID NotifyWinEvent;
    PVOID push_EVENT_OBJECT_NAMECHANGE;
    PVOID CallNotifyWinEvent;
    PVOID DefSetText;
    PVOID NtUserDefSetText;

    BYTE Stubpush_EVENT_OBJECT_NAMECHANGE[] =
    {
        0x68, 0x0C, 0x80, 0x00, 0x00,
    };

    SEARCH_PATTERN_DATA PushEVENT_OBJECT_NAMECHANGE[] =
    {
        ADD_PATTERN(Stubpush_EVENT_OBJECT_NAMECHANGE),
    };

    push_EVENT_OBJECT_NAMECHANGE    = nullptr;
    CallNotifyWinEvent              = nullptr;
    DefSetText                      = nullptr;
    NtUserDefSetText                = nullptr;

    *(PVOID *)&NotifyWinEvent = LookupExportTable(User32, USER32_NotifyWinEvent);

    Module = FindLdrModuleByHandle(User32);

    Start = PtrAdd(User32, IMAGE_FIRST_SECTION((PIMAGE_NT_HEADERS)PtrAdd(User32, ((PIMAGE_DOS_HEADER)User32)->e_lfanew))->VirtualAddress);
    End = PtrAdd(Start, Module->SizeOfImage - PtrOffset(Start, User32));

    while (Start < End)
    {
        push_EVENT_OBJECT_NAMECHANGE = SearchPattern(
                                            PushEVENT_OBJECT_NAMECHANGE,
                                            countof(PushEVENT_OBJECT_NAMECHANGE),
                                            Start,
                                            PtrOffset(End, Start)
                                        );

        if (push_EVENT_OBJECT_NAMECHANGE == nullptr)
            return nullptr;

        WalkOpCodeT(push_EVENT_OBJECT_NAMECHANGE, 0x10,
            WalkOpCodeM(Buffer, OpLength, Ret)
            {
                if (Buffer[0] != CALL)
                    return STATUS_NOT_FOUND;

                if (GetCallDestination(Buffer) != NotifyWinEvent)
                    return STATUS_NOT_FOUND;

                CallNotifyWinEvent = Buffer;

                return STATUS_SUCCESS;
            }
        );

        if (CallNotifyWinEvent != nullptr)
            break;

        Start = PtrAdd(push_EVENT_OBJECT_NAMECHANGE, 1);
    }

    if (CallNotifyWinEvent == nullptr)
        return nullptr;

    Start = User32;

    for (ULONG_PTR Length = 0x10; Length; --Length)
    {
        PBYTE Buffer = (PBYTE)PtrSub(push_EVENT_OBJECT_NAMECHANGE, Length);
        if (Buffer[0] != CALL)
            continue;

        DefSetText = GetCallDestination(Buffer);
        if (!IN_RANGEEX(Start, DefSetText, End))
        {
            DefSetText = nullptr;
            continue;
        }

        if (*(PULONG)DefSetText != 0x8B55FF8B)
        {
            DefSetText = nullptr;
            continue;
        }

        break;
    }

    if (DefSetText == nullptr)
        return nullptr;

    WalkOpCodeT(DefSetText, 0x50,
        WalkOpCodeM(Buffer, OpLength, Ret)
        {
            switch (Buffer[0])
            {
                case CALL:
                    Buffer = GetCallDestination(Buffer);
                    if (!IsSystemCall(Buffer))
                        break;

                    NtUserDefSetText = Buffer;
                    return STATUS_SUCCESS;

                case 0xC9:  // Lepave
                case 0xC2:
                case 0xC3:
                    return STATUS_SUCCESS;
            }

            return STATUS_NOT_FOUND;
        }
    );

    return NtUserDefSetText;
}

LRESULT CALLBACK SendMessageWorkerProbe(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    PVOID *StackFrame, *PrevFrame;

    if (Message != WM_SETTEXT)
        return 0;

    AllocStack(1);

    StackFrame = (PVOID *)_AddressOfReturnAddress() - 1;
    PrevFrame = nullptr;
    while (*StackFrame < (PVOID)lParam)
    {
        PrevFrame = StackFrame;
        StackFrame = (PVOID *)*StackFrame;
    }

    if (PrevFrame != nullptr)
    {
        PBYTE Buffer = (PBYTE)PrevFrame[1] - 5;

        if (Buffer[0] == CALL)
            return (LRESULT)GetCallDestination(Buffer);
    }

    return 0;
}

PVOID FindSendMessageWorker(PVOID User32)
{
    API_POINTER(CreateWindowExA)    CreateWindowExA;
    API_POINTER(SetWindowLongPtrA)  SetWindowLongPtrA;
    API_POINTER(SendMessageA)       SendMessageA;
    API_POINTER(DestroyWindow)      DestroyWindow;

    HWND        ProbeButton;
    LONG_PTR    WndProc;
    PVOID       SendMessageWorker;

    *(PVOID *)&CreateWindowExA      = LookupExportTable(User32, USER32_CreateWindowExA);
#if ML_AMD64
    *(PVOID *)&SetWindowLongPtrA    = LookupExportTable(User32, USER32_SetWindowLongPtrA);
#else
    *(PVOID *)&SetWindowLongPtrA    = LookupExportTable(User32, USER32_SetWindowLongA);
#endif
    *(PVOID *)&SendMessageA         = LookupExportTable(User32, USER32_SendMessageA);
    *(PVOID *)&DestroyWindow        = LookupExportTable(User32, USER32_DestroyWindow);

    ProbeButton         = CreateWindowExA(0, WC_BUTTONA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    WndProc             = SetWindowLongPtrA(ProbeButton, GWLP_WNDPROC, (LONG_PTR)SendMessageWorkerProbe);
    SendMessageWorker   = (PVOID)SendMessageA(ProbeButton, WM_SETTEXT, 0, (LPARAM)AllocStack(1));
    SetWindowLongPtrA(ProbeButton, GWLP_WNDPROC, WndProc);
    DestroyWindow(ProbeButton);

    return SendMessageWorker;
}

NTSTATUS LepGlobalData::HookUser32Routines(PVOID User32)
{
    PVOID       NtUserMessageCall;
    PVOID       NtUserCreateWindowEx;
    PVOID       NtUserDefSetText;
    PVOID       SendMessageWorker;
    PVOID       LepNtUserCreateWindowEx;
    RTL_OSVERSIONINFOW VersionInfo;
    NTSTATUS    Status;

    Status = NtAddAtom(PROP_WINDOW_ANSI_PROC, CONST_STRLEN(PROP_WINDOW_ANSI_PROC) * sizeof(WCHAR), &this->AtomAnsiProc);
    FAIL_RETURN(Status);
/*
    Status = NtAddAtom(PROP_WINDOW_UNICODE_PROC, CONST_STRLEN(PROP_WINDOW_UNICODE_PROC) * sizeof(WCHAR), &this->AtomUnicodeProc);
    FAIL_RETURN(Status);
*/
    Status = Nt_QueryOsVersion(&VersionInfo);
    FAIL_RETURN(Status);

    if (this->HasWin32U)
    {
        HMODULE Win32uMod = (HMODULE)Nt_GetModuleHandle(L"win32u.dll");
        if (Win32uMod == nullptr)
            return STATUS_NOT_FOUND;
        NtUserMessageCall = Nt_GetProcAddress(Win32uMod, "NtUserMessageCall");
        NtUserCreateWindowEx = Nt_GetProcAddress(Win32uMod, "NtUserCreateWindowEx");
        NtUserDefSetText = Nt_GetProcAddress(Win32uMod, "NtUserDefSetText");
        if (!NtUserMessageCall || !NtUserCreateWindowEx || !NtUserDefSetText)
            return STATUS_NOT_FOUND;
    }
    else
    {
#if ML_AMD64
        NtUserMessageCall = FindX64NtUserMessageCallNoWin32U(User32);
#else
        NtUserMessageCall = FindNtUserMessageCall2(User32);
#endif
#if ENABLE_LOG
        WriteLog(L"user32 find NtUserMessageCall=%p", NtUserMessageCall);
#endif
        if (NtUserMessageCall == nullptr)
            return STATUS_NOT_FOUND;

#if ML_AMD64
        NtUserCreateWindowEx = FindX64NtUserCreateWindowExNoWin32U(User32);
#else
        NtUserCreateWindowEx = FindNtUserCreateWindowEx(User32);
#endif
#if ENABLE_LOG
        WriteLog(L"user32 find NtUserCreateWindowEx=%p", NtUserCreateWindowEx);
#endif
        if (NtUserCreateWindowEx == nullptr)
            return STATUS_NOT_FOUND;

#if ML_AMD64
        NtUserDefSetText = FindX64NtUserDefSetTextNoWin32U(User32);
#else
        NtUserDefSetText = FindNtUserDefSetText(User32);
#endif
#if ENABLE_LOG
        WriteLog(L"user32 find NtUserDefSetText=%p", NtUserDefSetText);
#endif
        if (NtUserDefSetText == nullptr)
            return STATUS_NOT_FOUND;
    }

#if ENABLE_LOG
    WriteLog(L"user32 native routines msg=%p create=%p deftext=%p", NtUserMessageCall, NtUserCreateWindowEx, NtUserDefSetText);
#endif

    if (HookStub.StubEnumFontFamiliesExW != nullptr)
    {
        InitFontCharsetInfo();
    }

#if ENABLE_LOG
    WriteLog(L"user32 hook os=%u.%u build=%u peb=%u.%u build=%u win32u=%u",
        VersionInfo.dwMajorVersion,
        VersionInfo.dwMinorVersion,
        VersionInfo.dwBuildNumber,
        LepCurrentPeb()->OSMajorVersion,
        LepCurrentPeb()->OSMinorVersion,
        LepCurrentPeb()->OSBuildNumber,
        this->HasWin32U);
#endif

    switch (VersionInfo.dwMajorVersion)
    {
        case 6:
            switch (VersionInfo.dwMinorVersion)
            {
                case 2: // win 8
                case 3: // win 8.1
                    LepNtUserCreateWindowEx = LepNtUserCreateWindowEx_Win8;
                    break;

                case 4: // win 10
                    LepNtUserCreateWindowEx = LepNtUserCreateWindowEx_Win10;
                    break;

                default:
                    LepNtUserCreateWindowEx = LepNtUserCreateWindowEx_Win7;
                    break;
            }
            break;

        case 10: // win 10
#if ML_AMD64
            LepNtUserCreateWindowEx = ::LepNtUserCreateWindowEx;
#else
            LepNtUserCreateWindowEx = LepNtUserCreateWindowEx_Win10;
#endif
            break;

        default:
            return STATUS_UNKNOWN_REVISION;
    }

#if ML_AMD64
    LOOP_ONCE
    {
        PVOID NtUserGetDC;
        PVOID NtUserGetDCEx;
        PVOID NtUserGetWindowDC;
        PVOID NtUserBeginPaint;

        if (this->HasWin32U)
        {
            PVOID Win32uMod = Nt_GetModuleHandle(L"win32u.dll");
            if (Win32uMod == nullptr)
                return STATUS_NOT_FOUND;

            Status = GetX64SyscallExportByName(Win32uMod, "NtUserGetDC", WIN32K_NtUserGetDC, &NtUserGetDC);
            FAIL_RETURN(Status);

            Status = GetX64SyscallExportByName(Win32uMod, "NtUserGetDCEx", WIN32K_NtUserGetDCEx, &NtUserGetDCEx);
            FAIL_RETURN(Status);

            Status = GetX64SyscallExportByName(Win32uMod, "NtUserGetWindowDC", WIN32K_NtUserGetWindowDC, &NtUserGetWindowDC);
            FAIL_RETURN(Status);

            Status = GetX64SyscallExportByName(Win32uMod, "NtUserBeginPaint", WIN32K_NtUserBeginPaint, &NtUserBeginPaint);
            FAIL_RETURN(Status);
        }
        else
        {
            Status = GetX64SyscallExport(User32, USER32_GetDC, &NtUserGetDC);
            FAIL_RETURN(Status);

            Status = GetX64SyscallExport(User32, USER32_GetDCEx, &NtUserGetDCEx);
            FAIL_RETURN(Status);

            Status = GetX64SyscallExport(User32, USER32_GetWindowDC, &NtUserGetWindowDC);
            FAIL_RETURN(Status);

            Status = GetX64SyscallExport(User32, USER32_BeginPaint, &NtUserBeginPaint);
            FAIL_RETURN(Status);
        }

        PVOID Routines[7];
        ULONG Hashes[7];
        ULONG_PTR RoutineCount = 0;

        Routines[RoutineCount] = NtUserCreateWindowEx;
        Hashes[RoutineCount++] = WIN32K_NtUserCreateWindowEx;
        Routines[RoutineCount] = NtUserMessageCall;
        Hashes[RoutineCount++] = WIN32K_NtUserMessageCall;
        Routines[RoutineCount] = NtUserDefSetText;
        Hashes[RoutineCount++] = WIN32K_NtUserDefSetText;
        Routines[RoutineCount] = NtUserGetDC;
        Hashes[RoutineCount++] = WIN32K_NtUserGetDC;
        Routines[RoutineCount] = NtUserGetDCEx;
        Hashes[RoutineCount++] = WIN32K_NtUserGetDCEx;
        Routines[RoutineCount] = NtUserGetWindowDC;
        Hashes[RoutineCount++] = WIN32K_NtUserGetWindowDC;
        Routines[RoutineCount] = NtUserBeginPaint;
        Hashes[RoutineCount++] = WIN32K_NtUserBeginPaint;

        Status = HpAddSystemCallByRoutineRange(Routines, Hashes, RoutineCount);
        WriteLog(L"user32 add syscall routines: %08X", Status);
        FAIL_RETURN(Status);

        Status = HpAddSystemCallFilter(WIN32K_NtUserCreateWindowEx, LepHpNtUserCreateWindowEx, this);
        WriteLog(L"user32 hook NtUserCreateWindowEx: %08X", Status);
        FAIL_RETURN(Status);

        Status = HpAddSystemCallFilter(WIN32K_NtUserMessageCall, LepHpNtUserMessageCall, this);
        WriteLog(L"user32 hook NtUserMessageCall: %08X", Status);
        FAIL_RETURN(Status);

        Status = HpAddSystemCallFilter(WIN32K_NtUserDefSetText, LepHpNtUserDefSetText, this);
        WriteLog(L"user32 hook NtUserDefSetText: %08X", Status);
        FAIL_RETURN(Status);

        Status = HpAddSystemCallFilter(WIN32K_NtUserGetDC, LepHpNtUserGetDC, this);
        WriteLog(L"user32 hook NtUserGetDC: %08X", Status);
        FAIL_RETURN(Status);

        Status = HpAddSystemCallFilter(WIN32K_NtUserGetDCEx, LepHpNtUserGetDCEx, this);
        WriteLog(L"user32 hook NtUserGetDCEx: %08X", Status);
        FAIL_RETURN(Status);

        Status = HpAddSystemCallFilter(WIN32K_NtUserGetWindowDC, LepHpNtUserGetWindowDC, this);
        WriteLog(L"user32 hook NtUserGetWindowDC: %08X", Status);
        FAIL_RETURN(Status);

        Status = HpAddSystemCallFilter(WIN32K_NtUserBeginPaint, LepHpNtUserBeginPaint, this);
        WriteLog(L"user32 hook NtUserBeginPaint: %08X", Status);
        FAIL_RETURN(Status);

        ULONG_PTR SetWindowLongHookOp;

        SetWindowLongHookOp =
            (VersionInfo.dwMajorVersion == 6 && VersionInfo.dwMinorVersion == 1) ?
                LEP_FUNCTION_NO_ABSOLUTE_JUMP_OP :
                LEP_FUNCTION_JUMP_OP;

        Mp::PATCH_MEMORY_DATA p[8];
        ULONG_PTR Count = 0;

        p[Count++] = LepHookFromEATOp(User32, USER32, SetWindowLongA, SetWindowLongHookOp);
        p[Count++] = LepHookFromEAT(User32, USER32, GetWindowLongA);
        p[Count++] = LepHookFromEATOp(User32, USER32, SetWindowLongPtrA, SetWindowLongHookOp);
        p[Count++] = LepHookFromEAT(User32, USER32, GetWindowLongPtrA);
        p[Count++] = LepHookFromEAT(User32, USER32, IsWindowUnicode);

        p[Count++] = LepHookFromEAT(User32, USER32, GetClipboardData);
        p[Count++] = LepHookFromEAT(User32, USER32, SetClipboardData);

        if (GetLepb()->AnsiCodePage == CP_SHIFTJIS)
            p[Count++] = LepHookFromEAT(User32, USER32, SystemParametersInfoA);

        Status = Mp::PatchMemory(p, Count);

#if ENABLE_LOG
        WriteLog(L"user32 patch eat status=%08X", Status);
#endif

        return Status;
    }
#endif

    Mp::PATCH_MEMORY_DATA p[13];
    ULONG_PTR Count = 0;

    p[Count++] = LepFunctionJump(NtUserCreateWindowEx);
    p[Count++] = LepFunctionJump(NtUserMessageCall);
    p[Count++] = LepFunctionJump(NtUserDefSetText);

    p[Count++] = LepHookFromEAT(User32, USER32, SetWindowLongA);
    p[Count++] = LepHookFromEAT(User32, USER32, GetWindowLongA);
    p[Count++] = LepHookFromEAT(User32, USER32, IsWindowUnicode);

    p[Count++] = LepHookFromEAT(User32, USER32, GetClipboardData);
    p[Count++] = LepHookFromEAT(User32, USER32, SetClipboardData);

    if (GetLepb()->AnsiCodePage == CP_SHIFTJIS)
        p[Count++] = LepHookFromEAT(User32, USER32, SystemParametersInfoA);

#if !ML_AMD64
    p[Count++] = LepHookFromEAT(User32, USER32, GetDC);
#endif
    p[Count++] = LepHookFromEAT(User32, USER32, GetDCEx);
    p[Count++] = LepHookFromEAT(User32, USER32, GetWindowDC);

    p[Count++] = LepHookFromEAT(User32, USER32, BeginPaint);

    Status = Mp::PatchMemory(p, Count);

#if ENABLE_LOG
    WriteLog(L"user32 generic patch status=%08X", Status);
#endif

    return Status;
}

NTSTATUS LepGlobalData::UnHookUser32Routines()
{
#if ML_AMD64
    HpRemoveSystemCallFilter(WIN32K_NtUserCreateWindowEx, LepHpNtUserCreateWindowEx);
    HpRemoveSystemCallFilter(WIN32K_NtUserMessageCall, LepHpNtUserMessageCall);
    HpRemoveSystemCallFilter(WIN32K_NtUserDefSetText, LepHpNtUserDefSetText);
    HpRemoveSystemCallFilter(WIN32K_NtUserGetDC, LepHpNtUserGetDC);
    HpRemoveSystemCallFilter(WIN32K_NtUserGetDCEx, LepHpNtUserGetDCEx);
    HpRemoveSystemCallFilter(WIN32K_NtUserGetWindowDC, LepHpNtUserGetWindowDC);
    HpRemoveSystemCallFilter(WIN32K_NtUserBeginPaint, LepHpNtUserBeginPaint);
#endif

    Mp::RestoreMemory(HookStub.StubNtUserCreateWindowEx);
    Mp::RestoreMemory(HookStub.StubNtUserMessageCall);
    Mp::RestoreMemory(HookStub.StubNtUserDefSetText);

    Mp::RestoreMemory(HookStub.StubSetWindowLongA);
    Mp::RestoreMemory(HookStub.StubGetWindowLongA);
#if ML_AMD64
    Mp::RestoreMemory(HookStub.StubSetWindowLongPtrA);
    Mp::RestoreMemory(HookStub.StubGetWindowLongPtrA);
#endif
    Mp::RestoreMemory(HookStub.StubIsWindowUnicode);

    Mp::RestoreMemory(HookStub.StubGetClipboardData);
    Mp::RestoreMemory(HookStub.StubSetClipboardData);
    Mp::RestoreMemory(HookStub.StubSystemParametersInfoA);

    Mp::RestoreMemory(HookStub.StubGetDC);
    Mp::RestoreMemory(HookStub.StubGetDCEx);
    Mp::RestoreMemory(HookStub.StubGetWindowDC);

    Mp::RestoreMemory(HookStub.StubBeginPaint);

    if (AtomAnsiProc != 0)
    {
        NtDeleteAtom(AtomAnsiProc);
        AtomAnsiProc = 0;
    }
/*
    if (AtomUnicodeProc != NULL)
    {
        NtDeleteAtom(AtomUnicodeProc);
        AtomUnicodeProc = NULL;
    }
*/
    return 0;
}
